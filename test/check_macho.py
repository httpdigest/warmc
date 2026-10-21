"""Validate a linked arm64 Mach-O produced by warmc.

We cannot execute Darwin binaries here, so instead we parse the linked image
and check that the relocations actually resolved: every export descriptor's
name pointer must land on the expected string, and every function pointer must
land inside __text.  That exercises the ABS64/addend path end to end.
"""
import struct
import sys

path, expected_names = sys.argv[1], sys.argv[2:]
buf = open(path, "rb").read()

magic, cputype, cpusub, filetype, ncmds, sizeofcmds, flags, _ = \
    struct.unpack_from("<IiiIIIII", buf, 0)
assert magic == 0xFEEDFACF, "not a 64-bit Mach-O: %#x" % magic
assert cputype == 0x0100000C, "not arm64: %#x" % cputype

segments = []
sections = {}
symbols = {}
local_text = []
uuid = None
off = 32
for _ in range(ncmds):
    cmd, cmdsize = struct.unpack_from("<II", buf, off)
    if cmd == 0x19:
        segname = buf[off + 8:off + 24].rstrip(b"\0").decode()
        vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", buf, off + 24)
        nsects = struct.unpack_from("<I", buf, off + 64)[0]
        segments.append((vmaddr, vmsize, fileoff, filesize))
        so = off + 72
        for _s in range(nsects):
            sn = buf[so:so + 16].rstrip(b"\0").decode()
            addr, size = struct.unpack_from("<QQ", buf, so + 32)
            sections["%s,%s" % (segname, sn)] = (addr, size)
            so += 80
    elif cmd == 0x2:
        symoff, nsyms, stroff, strsize = struct.unpack_from("<IIII", buf, off + 8)
        for i in range(nsyms):
            e = symoff + 16 * i
            strx, ntype, nsect, ndesc, value = struct.unpack_from("<IBBHQ", buf, e)
            end = buf.index(b"\0", stroff + strx)
            name = buf[stroff + strx:end].decode()
            symbols[name] = value
            if ntype == 0x0E and nsect == 1:
                local_text.append(name)
    elif cmd == 0x1B:
        uuid = buf[off + 8:off + 24]
    off += cmdsize

# The executable keeps its function names, the local ones included, so a
# profiler can name the code it samples -- and carries a UUID, without which
# Instruments will not match those names to the binary at all.
assert uuid and uuid != bytes(16), "no LC_UUID"
assert any(n.startswith("_wasm_func_") for n in local_text), \
    "no wasm function names among %d local text symbols" % len(local_text)

def read(addr, n):
    for vmaddr, vmsize, fileoff, filesize in segments:
        if vmaddr <= addr < vmaddr + vmsize:
            fo = fileoff + (addr - vmaddr)
            if fo + n <= len(buf):
                return buf[fo:fo + n]
    raise SystemExit("address %#x is not mapped by any segment" % addr)

def u32(addr):
    return struct.unpack("<I", read(addr, 4))[0]

def u64(addr):
    return struct.unpack("<Q", read(addr, 8))[0]

def cstr(addr):
    outp = b""
    while True:
        c = read(addr + len(outp), 1)
        if c == b"\0":
            return outp.decode()
        outp += c

if "_warmc_module" not in symbols:
    raise SystemExit("FAIL: _warmc_module is not in the symbol table")
md = symbols["_warmc_module"]

MD_NEXPORT, MD_EXPORTS = 44, 88
MD_NFUNCREFS, MD_FUNCREFS, MD_NIMPORTED_FUNCS = 108, 112, 124
EXPORT_SIZE = 80
EX_NAME, EX_KIND, EX_INDEX, EX_NPARAMS, EX_NRESULTS = 0, 12, 16, 20, 24
EX_PTYPES, EX_RTYPES = 32, 40
FUNCREF_SIZE, FR_CODE = 24, 0

nexport = u32(md + MD_NEXPORT)
exports_ptr = u64(md + MD_EXPORTS)
text_addr, text_size = sections["__TEXT,__text"]

def in_text(a):
    return text_addr <= a < text_addr + text_size

problems = []
seen = []

for i in range(nexport):
    e = exports_ptr + EXPORT_SIZE * i
    name = cstr(u64(e + EX_NAME))
    kind = u32(e + EX_KIND)
    if kind != 0:
        continue
    seen.append(name)
    index = u32(e + EX_INDEX)
    if index >= u32(md + MD_NFUNCREFS):
        problems.append("export %r names function %d, of %d"
                        % (name, index, u32(md + MD_NFUNCREFS)))
    nparams = u32(e + EX_NPARAMS)
    nresults = u32(e + EX_NRESULTS)
    ptypes, rtypes = u64(e + EX_PTYPES), u64(e + EX_RTYPES)
    for k in range(nparams):
        read(ptypes + k, 1)
    for k in range(nresults):
        read(rtypes + k, 1)

nfuncrefs = u32(md + MD_NFUNCREFS)
funcrefs_ptr = u64(md + MD_FUNCREFS)
nimported = u32(md + MD_NIMPORTED_FUNCS)
for i in range(nfuncrefs):
    code = u64(funcrefs_ptr + FUNCREF_SIZE * i + FR_CODE)
    if i < nimported:
        if code != 0:
            problems.append("imported function %d has code %#x, expected 0" % (i, code))
    elif not in_text(code):
        problems.append("function %d points at %#x, outside __text [%#x,%#x)"
                        % (i, code, text_addr, text_addr + text_size))

if expected_names and sorted(seen) != sorted(expected_names):
    problems.append("export names %r != expected %r" % (sorted(seen), sorted(expected_names)))

if problems:
    for p in problems:
        print("FAIL[macho] " + p)
    sys.exit(1)
print("macho: %s ok (%d exports, descriptors and pointers resolve)"
      % (path.split("/")[-1], nexport))
