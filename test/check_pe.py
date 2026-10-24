"""Validate a linked x86-64 PE executable produced by warmc.

The third of the writer checks, after test/check_elf.py and
test/check_macho.py, and the same argument applies: src/pe.c is its own code
and a PE that runs on this machine only proves the paths that machine took.
So this reads the image the way the loader does -- headers, section table,
then the import directory -- and finishes with the pointer chase check_elf.py
follows: `warmc_modules` -> the one-element table in rt/modules.c -> the
module descriptor -> its export table -> the export's name string.  Every
step is an ADDR64 relocation the linker had to place, and the last one is a
string this can compare against what the module exports.

The import walk is the part with no ELF twin: warmc has no import library, so
`__imp_X` resolving to the right IAT slot and each thunk's name landing in the
right DLL's hint/name table is checked here rather than by anything running.

Addresses come from `WARMC_LINK_MAP=1`, which the linker already emits, since
the PE carries no symbol table of its own.
"""
import struct
import subprocess
import sys

if len(sys.argv) < 4:
    sys.exit("usage: check_pe.py <warmc> <module.wat|wasm> <rt-windows.a> [export...]")
warmc, module, rtlib = sys.argv[1], sys.argv[2], sys.argv[3]
expected_exports = sys.argv[4:]

out = module + ".pechk"
r = subprocess.run([warmc, "-O2", "--target=windows", "--rt", rtlib, "-o", out, module,
                    "--custom-sections=" + __import__("os").environ.get("CUSTOM_SECTIONS", "default")],
                   capture_output=True, text=True,
                   env={"WARMC_LINK_MAP": "1", "PATH": "/usr/bin:/bin"})
if r.returncode != 0:
    sys.exit("link failed: " + (r.stderr.strip()[-400:] or "no message"))
syms = {}
for line in r.stderr.splitlines():
    if line.startswith("map "):
        _, addr, name = line.split(None, 2)
        syms[name.strip()] = int(addr, 16)

buf = open(out, "rb").read()
assert buf[:2] == b"MZ", "no DOS header"
pe = struct.unpack_from("<I", buf, 0x3C)[0]
assert buf[pe:pe + 4] == b"PE\0\0", "no PE signature at e_lfanew"

machine, nsec = struct.unpack_from("<HH", buf, pe + 4)
opt_size, chars = struct.unpack_from("<HH", buf, pe + 20)
assert machine == 0x8664, "not x86-64: %#x" % machine

opt = pe + 24
magic = struct.unpack_from("<H", buf, opt)[0]
assert magic == 0x20B, "not PE32+: %#x" % magic
image_base, sec_align, file_align = struct.unpack_from("<QII", buf, opt + 24)
size_of_image, size_of_headers = struct.unpack_from("<II", buf, opt + 56)
subsystem, dll_chars = struct.unpack_from("<HH", buf, opt + 68)
stack_reserve, stack_commit = struct.unpack_from("<QQ", buf, opt + 72)
ndirs = struct.unpack_from("<I", buf, opt + 108)[0]
entry = struct.unpack_from("<I", buf, opt + 16)[0]

assert sec_align >= file_align, "section alignment %#x < file alignment %#x" % (
    sec_align, file_align)
assert size_of_image % sec_align == 0, "SizeOfImage %#x is not a multiple of %#x" % (
    size_of_image, sec_align)
assert size_of_headers % file_align == 0, \
    "SizeOfHeaders %#x is not a multiple of %#x" % (size_of_headers, file_align)
assert subsystem == 3, "subsystem is %d, not console" % subsystem
assert chars & 0x0002, "not marked EXECUTABLE_IMAGE"
assert chars & 0x0020, "not marked LARGE_ADDRESS_AWARE"
# No .reloc is emitted, so the loader must be told the image cannot move.
assert chars & 0x0001, "not marked RELOCS_STRIPPED, but there is no .reloc"
assert not dll_chars & 0x0040, "DYNAMIC_BASE is set on a RELOCS_STRIPPED image"
assert dll_chars & 0x0100, "NX_COMPAT is not set"
# The prologue does not probe, so every page of the stack has to be committed
# up front or a deep frame walks off the guard page.
assert stack_commit == stack_reserve, \
    "stack commit %#x != reserve %#x, but the prologue does not probe" % (
        stack_commit, stack_reserve)

secs = []
sh = opt + opt_size
for i in range(nsec):
    o = sh + i * 40
    name = buf[o:o + 8].rstrip(b"\0").decode()
    vsz, va, rawsz, rawoff = struct.unpack_from("<IIII", buf, o + 8)
    scharacter = struct.unpack_from("<I", buf, o + 36)[0]
    secs.append((name, va, vsz, rawoff, rawsz, scharacter))
    assert va % sec_align == 0, "%s is at RVA %#x, not %#x-aligned" % (name, va, sec_align)
    assert rawoff % file_align == 0 or rawsz == 0, \
        "%s is at file offset %#x, not %#x-aligned" % (name, rawoff, file_align)
    assert rawoff + rawsz <= len(buf), "%s runs past the end of the file" % name
    assert va + vsz <= size_of_image, "%s runs past SizeOfImage" % name

# The optional header's totals: the code and the initialized data, each the
# file size of the sections that say they hold it.
size_of_code, size_of_idata = struct.unpack_from("<II", buf, opt + 4)
want_code = sum(s[4] for s in secs if s[5] & 0x20)
want_idata = sum(s[4] for s in secs if s[5] & 0x40)
assert size_of_code == want_code, "SizeOfCode %#x, the code sections hold %#x" % (
    size_of_code, want_code)
assert size_of_idata == want_idata, \
    "SizeOfInitializedData %#x, the initialized-data sections hold %#x (%s)" % (
        size_of_idata, want_idata, ", ".join(s[0] for s in secs if s[5] & 0x40))

by_name = {s[0]: s for s in secs}
for want in (".text", ".rdata", ".data"):
    assert want in by_name, "no %s section" % want
assert by_name[".text"][5] & 0x20000000, ".text is not executable"
assert not by_name[".text"][5] & 0x80000000, ".text is writable"
assert by_name[".rdata"][5] & 0x40000000, ".rdata is not readable"
assert not by_name[".rdata"][5] & 0x80000000, ".rdata is writable"
assert by_name[".data"][5] & 0x80000000, ".data is not writable"
assert not by_name[".data"][5] & 0x20000000, ".data is executable"

tva = by_name[".text"][1]
assert tva <= entry < tva + by_name[".text"][2], \
    "the entry point %#x is not in .text" % entry


def read_rva(rva, n):
    for name, va, vsz, rawoff, rawsz, _c in secs:
        if va <= rva and rva + n <= va + vsz:
            if rva + n > va + rawsz:
                raise AssertionError(
                    "RVA %#x is in the uninitialized tail of %s (raw %#x of %#x): "
                    "the data holding it was not written out" % (rva, name, rawsz, vsz))
            return buf[rawoff + (rva - va):rawoff + (rva - va) + n]
    raise AssertionError("RVA %#x is not covered by any section" % rva)


def read(va, n):
    assert va >= image_base, "address %#x is below the image base %#x" % (va, image_base)
    return read_rva(va - image_base, n)


def u16(va): return struct.unpack_from("<H", read(va, 2), 0)[0]
def u32(va): return struct.unpack_from("<I", read(va, 4), 0)[0]
def u64(va): return struct.unpack_from("<Q", read(va, 8), 0)[0]
def u32r(rva): return struct.unpack_from("<I", read_rva(rva, 4), 0)[0]
def u64r(rva): return struct.unpack_from("<Q", read_rva(rva, 8), 0)[0]


def cstr(va):
    s = b""
    while True:
        c = read(va + len(s), 1)
        if c == b"\0":
            return s.decode()
        s += c


def cstr_rva(rva):
    s = b""
    while True:
        c = read_rva(rva + len(s), 1)
        if c == b"\0":
            return s.decode()
        s += c


# --- imports -----------------------------------------------------------
assert ndirs > 12, "only %d data directories; no IAT entry" % ndirs
imp_rva, imp_size = struct.unpack_from("<II", buf, opt + 112 + 8 * 1)
iat_rva, iat_size = struct.unpack_from("<II", buf, opt + 112 + 8 * 12)
assert imp_rva and imp_size, "no import directory"
assert iat_rva and iat_size, "no IAT directory"

imports = {}
d = imp_rva
while True:
    ilt, _ts, _fc, name_rva, first = struct.unpack_from("<IIIII", read_rva(d, 20), 0)
    if not (ilt or name_rva or first):
        break
    dll = cstr_rva(name_rva).lower()
    assert iat_rva <= first < iat_rva + iat_size, \
        "%s's IAT at RVA %#x is outside the IAT directory" % (dll, first)
    names = []
    i = 0
    while True:
        thunk = u64r(ilt + 8 * i)
        if not thunk:
            break
        assert not thunk & (1 << 63), "%s imports by ordinal, which pe.c never emits" % dll
        names.append(cstr_rva(thunk + 2))
        # Before the loader runs, each IAT slot holds the same hint/name RVA.
        assert u64r(first + 8 * i) == thunk, \
            "%s slot %d does not start out pointing at its hint/name entry" % (dll, i)
        i += 1
    assert names, "%s imports nothing" % dll
    imports[dll] = names
    d += 20

assert "kernel32.dll" in imports, "nothing is imported from kernel32"
for must in ("ExitProcess", "VirtualAlloc"):
    assert must in imports["kernel32.dll"], "kernel32 does not supply %s" % must

# --- the descriptor chain ----------------------------------------------
for want in ("warmc_modules", "warmc_nmodules"):
    assert want in syms, "the link map does not mention %s" % want

n = u32(syms["warmc_nmodules"])
assert n >= 1, "warmc_nmodules is %d" % n
table = u64(syms["warmc_modules"])
md = u64(table)

nexport = u32(md + 44)
exports = u64(md + 88)
nfuncrefs = u32(md + 108)
funcrefs = u64(md + 112)

for name, v in (("nexport", nexport), ("nfuncrefs", nfuncrefs)):
    assert v < (1 << 20), "%s is %d, which is an address and not a count" % (name, v)

EXPORT_SIZE = 80
names = sorted(cstr(u64(exports + i * EXPORT_SIZE)) for i in range(nexport))
if expected_exports:
    assert names == sorted(expected_exports), \
        "exports are %s, expected %s" % (names, sorted(expected_exports))

text_lo = image_base + tva
text_hi = text_lo + by_name[".text"][2]
for i in range(nfuncrefs):
    code = u64(funcrefs + i * 24)
    assert text_lo <= code < text_hi, \
        "funcref %d points at %#x, which is not in .text" % (i, code)

# The custom sections: .wcustom, which the descriptor points at, holding what
# the module carries (test/custom_check.py).
sys.path.insert(0, __import__("os").path.dirname(__file__))
import custom_check
problems = []
cs = by_name.get(".wcustom")
if cs:
    assert not cs[5] & 0x80000000, ".wcustom is writable"
custom_check.check(problems, read_rva(cs[1], cs[2]) if cs else None,
                   image_base + cs[1] if cs else 0, u64(md + custom_check.DESC_CUSTOM),
                   custom_check.module_customs(warmc, module),
                   __import__("os").environ.get("CUSTOM_SECTIONS", "default"))
assert not problems, "; ".join(problems)

print("check_pe: %s ok (%d export%s: %s; %d funcref%s; %d DLL%s: %s)"
      % (module, nexport, "" if nexport == 1 else "s", ", ".join(names) or "-",
         nfuncrefs, "" if nfuncrefs == 1 else "s",
         len(imports), "" if len(imports) == 1 else "s",
         ", ".join(sorted(imports))))
