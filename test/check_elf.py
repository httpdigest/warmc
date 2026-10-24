"""Validate a linked aarch64 ELF executable produced by warmc.

The twin of test/check_macho.py, and it exists because the two writers are not
the same code and only one of them was ever looked at.  A Linux executable
linked by warmc cannot be run on a Mac -- test/hostenv.sh picks the macOS
runtime, and test/run_gowasi_linux.sh, which does run warmc-linked images,
needs a Linux container -- so without this file warmc's own ELF *executable*
path has no test that `make check` runs on this machine.  What it
was hiding: the data section's file offset was computed from the GOT's
unpadded byte count while every relocation used the padded address, so with an
odd number of GOT slots every stored pointer in .data came out holding the
value of the next slot along.  Silent, and a coin flip on the slot count.

So this reads the image the way a loader would and follows the chain that bug
broke: `warmc_modules` -> the one-element table in rt/modules.c -> the module
descriptor -> its export table -> the export's name string.  Every step is a
pointer that a relocation had to get right, and the last one is a string this
can compare against what the module actually exports.

Addresses come from `WARMC_LINK_MAP=1`, which the linker already emits.
"""
import struct
import subprocess
import sys

if len(sys.argv) < 4:
    sys.exit("usage: check_elf.py <warmc> <module.wat|wasm> <rt.a> [export...]")
warmc, module, rtlib = sys.argv[1], sys.argv[2], sys.argv[3]
expected_exports = sys.argv[4:]

out = module + ".elfchk"
r = subprocess.run([warmc, "-O2", "--rt", rtlib, "-o", out, module,
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
assert buf[:4] == b"\177ELF" and buf[4] == 2 and buf[5] == 1, "not a little-endian ELF64"
e_type, e_machine = struct.unpack_from("<HH", buf, 16)
assert e_machine == 183, "not aarch64: %d" % e_machine

e_phoff = struct.unpack_from("<Q", buf, 0x20)[0]
e_phnum = struct.unpack_from("<H", buf, 0x38)[0]
loads = []
for i in range(e_phnum):
    p_type, p_flags, p_off, p_va, p_pa, p_fsz, p_msz, _ = \
        struct.unpack_from("<IIQQQQQQ", buf, e_phoff + i * 56)
    if p_type == 1:
        loads.append((p_va, p_off, p_fsz, p_msz))
assert loads, "no PT_LOAD"

for va, off, fsz, msz in loads:
    assert (va & 0xFFF) == (off & 0xFFF), \
        "segment at %#x is at file offset %#x: not congruent mod the page size" % (va, off)
    assert fsz <= msz, "segment at %#x has filesz > memsz" % va
    assert off + fsz <= len(buf), \
        "segment at %#x runs past the end of the file" % va

def read(va, n):
    for v0, off, fsz, msz in loads:
        if v0 <= va and va + n <= v0 + fsz:
            return buf[off + (va - v0):off + (va - v0) + n]

    for v0, off, fsz, msz in loads:
        if v0 <= va < v0 + msz:
            raise AssertionError("address %#x is in the bss of the segment at %#x "
                                 "(filesz %#x): the data holding it was not "
                                 "written out" % (va, v0, fsz))
    raise AssertionError("address %#x is not mapped by any segment" % va)

def u32(va): return struct.unpack_from("<I", read(va, 4), 0)[0]
def u64(va): return struct.unpack_from("<Q", read(va, 8), 0)[0]
def cstr(va):
    s = b""
    while True:
        c = read(va + len(s), 1)
        if c == b"\0":
            return s.decode()
        s += c

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

text = [(v, f) for v, o, f, m in loads if v == loads[0][0]]
for i in range(nfuncrefs):
    code = u64(funcrefs + i * 24)
    assert any(v <= code < v + f for v, o, f, m in loads), \
        "funcref %d points at %#x, which is not mapped" % (i, code)

# The function names the linker keeps for perf and gdb: a .symtab whose FUNC
# symbols sit in an executable section, wasm functions among them.
e_shoff, = struct.unpack_from("<Q", buf, 0x28)
e_shentsize, e_shnum = struct.unpack_from("<HH", buf, 0x3a)
assert e_shnum and e_shentsize == 64, "no section headers, so no symbol table"
shs = [struct.unpack_from("<IIQQQQIIQQ", buf, e_shoff + i * 64) for i in range(e_shnum)]
tabs = [s for s in shs if s[1] == 2]
assert len(tabs) == 1, "%d symbol tables" % len(tabs)
_, _, _, _, symoff, symsize, link, _, _, _ = tabs[0]
stroff = shs[link][4]
funcs = []
for i in range(1, symsize // 24):
    st_name, st_info, _, st_shndx, st_value, st_size = \
        struct.unpack_from("<IBBHQQ", buf, symoff + i * 24)
    nm = buf[stroff + st_name:buf.index(b"\0", stroff + st_name)].decode()
    if st_info & 0xF != 2:
        continue
    assert st_shndx < e_shnum and shs[st_shndx][2] & 4, \
        "%s is not in an executable section" % nm
    funcs.append(nm)
assert "warmc_trap" in funcs, "the runtime's functions have no names"
if nfuncrefs or nexport:
    assert any(n.startswith("wasm_func_") for n in funcs), "the module's functions have no names"

# The custom sections: .wasm.custom, which the descriptor points at, holding
# what the module carries (test/custom_check.py).
sys.path.insert(0, __import__("os").path.dirname(__file__))
import custom_check
shstrndx, = struct.unpack_from("<H", buf, 0x3e)
shstr = shs[shstrndx][4]
custom = [s for s in shs if buf[shstr + s[0]:buf.index(b"\0", shstr + s[0])] == b".wasm.custom"]
assert len(custom) <= 1, "%d .wasm.custom sections" % len(custom)
problems = []
custom_check.check(problems, read(custom[0][3], custom[0][5]) if custom else None,
                   custom[0][3] if custom else 0, u64(md + custom_check.DESC_CUSTOM),
                   custom_check.module_customs(warmc, module),
                   __import__("os").environ.get("CUSTOM_SECTIONS", "default"))
assert not problems, "; ".join(problems)

print("check_elf: %s ok (%d export%s: %s; %d funcref%s)"
      % (module, nexport, "" if nexport == 1 else "s", ", ".join(names) or "-",
         nfuncrefs, "" if nfuncrefs == 1 else "s"))
