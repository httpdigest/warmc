"""Function symbols named as the module's name section names them.

A function's symbol is wasm_func_N, which every reference in the object
names; its entry in the symbol table, which only tools read, takes the name
section's name (src/module_data.c, src/parse.c's read_func_names).  So a
name may be anything a symbol can carry, a runtime function's included,
twice over or a mangled one, and nothing resolves to it.  This checks:

  * the names in an ELF, a Mach-O and a COFF object, exactly: each named
    function by its name, two functions of one name both, a name that is
    empty or holds a NUL left as wasm_func_N, and --no-function-names
    giving every one wasm_func_N;
  * that a name section not as the appendix has it gives no names at all,
    and the module is no less valid for it;
  * that the code is the same with names and without: every section but the
    symbol and string tables, byte for byte;
  * that a module in text gets the names of its functions' $ids, and of
    their (@name "...") annotations, which take precedence, into the name
    section warmc's assembler writes, and so into the symbols;
  * that a program whose functions are named memset, warmc_trap, main and
    wasm_func_0 runs as it should -- its 4 KiB fill goes to the C library's
    memset on macOS, its trap to the runtime's warmc_trap -- and carries
    the names in what is linked, where the host's format has a symbol table.
"""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import custom_check
import gen_gosp
import hostbuild

ROOT = os.path.dirname(HERE)
BUILD = os.environ.get("BUILD", "build")
WARMC = os.path.join(BUILD, "warmc")
RUN = os.environ.get("WARMC_RUN", "").split()

def leb(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)

def sub(sid, payload):
    return bytes([sid]) + leb(len(payload)) + payload

def fnames(pairs):
    """a function-names subsection: (index, name bytes) each"""
    return sub(1, leb(len(pairs)) + b"".join(leb(i) + leb(len(n)) + n for i, n in pairs))

# the functions' names, by index; 7, 8 and 9 end up without one, and 12 to
# 14 are the exports, which have none
NAMES = [(0, b"memset"), (1, b"warmc_trap"), (2, b"main"), (3, b"wasm_func_0"),
         (4, b"dup"), (5, b"dup"), (6, b"fmt.(*pp).doPrintf"), (8, b"a\0b"), (9, b""),
         (10, "grüße 名前".encode()), (11, b"_ZN4core3fmt5write17h0123456789abcdefE")]
NFUNCS = 15
WANT = {0: "memset", 1: "warmc_trap", 2: "main", 3: "wasm_func_0", 4: "dup", 5: "dup",
        6: "fmt.(*pp).doPrintf", 10: "grüße 名前",
        11: "_ZN4core3fmt5write17h0123456789abcdefE"}

def module(name_section):
    funcs = "\n".join("  (func (result i32) (i32.const %d))" % (k + 1) for k in range(12))
    expr = "(call 0)"
    for k in range(1, 12):
        expr = "(i32.add %s (call %d))" % (expr, k)
    data = "".join("\\%02x" % b for b in name_section)
    return """(module
  (memory 1)
%s
  (func (export "sum") (result i32) %s)
  (func (export "fill") (result i32)
    (memory.fill (i32.const 0) (i32.const 0xab) (i32.const 4096))
    (i32.and (i32.eq (i32.load8_u (i32.const 4095)) (i32.const 0xab))
             (i32.eqz (i32.load8_u (i32.const 4096)))))
  (func (export "div") (param i32) (result i32) (i32.div_s (i32.const 1) (local.get 0)))
  (@custom "name" "%s"))
""" % (funcs, expr, data)

GOOD = fnames(NAMES)
MALFORMED = {
    "a subsection past the section's end": GOOD[:-3],
    "a subsection after a later one": sub(2, b"\x00") + GOOD,
    "the function names twice": GOOD + GOOD,
    "an index past the functions": fnames([(0, b"a"), (NFUNCS, b"b")]),
    "an index not increasing": fnames([(1, b"a"), (1, b"b")]),
    "a name that is not UTF-8": fnames([(0, b"a"), (1, b"\xc3")]),
    "bytes after the names": sub(1, leb(1) + leb(0) + leb(1) + b"a" + b"\x00"),
}

# the same through warmc's own assembler: an import named by its $id, a
# function by its $id, one by the @name after its $id, one by a @name and
# no $id, and one by nothing; and @name where it names no function
TEXT = """(module (@name "the module")
  (import "m" "imp" (func $imp))
  (func $helper (result i32) (i32.const 7))
  (func $other (@name "renamed by annotation") (param $p (@name "a param") i32) (result i32)
    (local.get $p))
  (func (@name "no id") (result i32) (i32.const 9))
  (func (export "f") (result i32)
    (i32.add (call $helper) (i32.add (call $other (i32.const 8)) (call 3)))))
"""
TEXT_NAMES = {0: "imp", 1: "helper", 2: "renamed by annotation", 3: "no id"}

def u16(b, o): return struct.unpack_from("<H", b, o)[0]
def u32(b, o): return struct.unpack_from("<I", b, o)[0]
def u64(b, o): return struct.unpack_from("<Q", b, o)[0]
def cstr(b, o): return b[o:b.index(b"\0", o)].decode("utf-8")

def elf_sections(b):
    shoff, shent, shnum = u64(b, 0x28), u16(b, 0x3a), u16(b, 0x3c)
    return [struct.unpack_from("<IIQQQQIIQQ", b, shoff + i * shent) for i in range(shnum)]

def elf_local_funcs(b):
    """the names of an ELF's local function symbols"""
    out = []
    shs = elf_sections(b)
    for sh in shs:
        if sh[1] != 2:
            continue
        stroff = shs[sh[6]][4]
        for k in range(1, sh[5] // 24):
            st_name, info, _, shndx, _, _ = struct.unpack_from("<IBBHQQ", b, sh[4] + k * 24)
            if info >> 4 == 0 and info & 0xF == 2:
                out.append(cstr(b, stroff + st_name))
    return out

def elf_code(b):
    """an ELF's sections but its symbol and string tables, by name"""
    shs = elf_sections(b)
    shstr = shs[u16(b, 0x3e)][4]
    return {cstr(b, shstr + sh[0]): b[sh[4]:sh[4] + sh[5]]
            for sh in shs if sh[1] not in (2, 3) and sh[1] != 8}

def macho_local_text(b):
    """the names of a Mach-O's local symbols in __text, the underscore off"""
    ncmds, off, out = u32(b, 16), 32, []
    text, nsect, symtab = set(), 0, None
    for _ in range(ncmds):
        cmd, size = u32(b, off), u32(b, off + 4)
        if cmd == 0x19:
            for s in range(u32(b, off + 64)):
                nsect += 1
                if b[off + 72 + 80 * s:off + 72 + 80 * s + 16].rstrip(b"\0") == b"__text":
                    text.add(nsect)
        elif cmd == 0x2:
            symtab = (u32(b, off + 8), u32(b, off + 12), u32(b, off + 16))
        off += size
    if symtab:
        symoff, nsyms, stroff = symtab
        for k in range(nsyms):
            strx, ntype, nsec = struct.unpack_from("<IBB", b, symoff + k * 16)
            if ntype & 0x0E == 0x0E and not ntype & 0x01 and not ntype & 0xE0 and nsec in text:
                nm = cstr(b, stroff + strx)
                out.append(nm[1:] if nm.startswith("_") else nm)
    return out

def macho_code(b):
    """a Mach-O's section contents and relocations, by name"""
    ncmds, off, out = u32(b, 16), 32, {}
    for _ in range(ncmds):
        cmd, size = u32(b, off), u32(b, off + 4)
        if cmd == 0x19:
            for s in range(u32(b, off + 64)):
                so = off + 72 + 80 * s
                name = b[so:so + 16].rstrip(b"\0").decode() + "," + b[so + 16:so + 32].rstrip(b"\0").decode()
                sz, fo = u64(b, so + 40), u32(b, so + 48)
                reloff, nreloc = u32(b, so + 56), u32(b, so + 60)
                out[name] = (b[fo:fo + sz] if fo else b"", b[reloff:reloff + 8 * nreloc])
        off += size
    return out

def coff_local_funcs(b):
    """the names of a COFF object's static function symbols"""
    nsec, symptr, nsyms = u16(b, 2), u32(b, 8), u32(b, 12)
    strtab = symptr + 18 * nsyms
    out, k = [], 0
    while k < nsyms:
        e = symptr + 18 * k
        raw = b[e:e + 8]
        name = cstr(b, strtab + u32(raw, 4)) if raw[:4] == b"\0\0\0\0" else raw.rstrip(b"\0").decode()
        sec, typ, cls, naux = struct.unpack_from("<hHBB", b, e + 12)
        if cls == 3 and sec > 0 and typ == 0x20:
            out.append(name)
        k += 1 + naux
    return out

def module_names(names, prefix=""):
    """what the module's own functions are called, as a sorted list"""
    out = [names.get(i, "wasm_func_%d" % i) for i in range(NFUNCS)]
    return sorted(prefix + n for n in out)

def main():
    os.chdir(ROOT)
    fails = []
    def sh(cmd):
        return subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    with tempfile.TemporaryDirectory() as tmp:
        def build(label, section, extra=()):
            wat = os.path.join(tmp, label + ".wat")
            with open(wat, "w", encoding="utf-8") as f:
                f.write(module(section))
            objs = {}
            for tgt, ext in (("linux", "o"), ("macos", "mo"), ("windows", "obj")):
                out = os.path.join(tmp, "%s.%s" % (label, ext))
                r = sh([WARMC, "-O0", "--target=" + tgt, "-c", "-o", out, wat] + list(extra))
                if r.returncode:
                    fails.append("%s: %s object not built: %s" % (label, tgt, r.stderr.strip()[:200]))
                    continue
                with open(out, "rb") as f:
                    objs[tgt] = f.read()
            return wat, objs

        def check_names(label, objs, want):
            readers = {"linux": elf_local_funcs, "macos": macho_local_text,
                       "windows": coff_local_funcs}
            for tgt, b in objs.items():
                got = sorted(readers[tgt](b))
                exp = module_names(want)
                if got != exp:
                    fails.append("%s: %s object's functions are %r, not %r"
                                 % (label, tgt, got, exp))

        wat, named = build("named", GOOD)
        check_names("named", named, WANT)
        _, plain = build("plain", GOOD, ["--no-function-names"])
        check_names("--no-function-names", plain, {})
        for why, section in MALFORMED.items():
            _, objs = build("bad%d" % len(fails), section)
            check_names("a name section with %s" % why, objs, {})

        # names from text: $ids, and @name annotations over them
        tw = os.path.join(tmp, "text.wat")
        with open(tw, "w", encoding="utf-8") as f:
            f.write(TEXT)
        tb = os.path.join(tmp, "text.wasm")
        r = sh([WARMC, "--emit-wasm", "-o", tb, tw])
        if r.returncode:
            fails.append("the text module does not assemble: " + r.stderr.strip()[:200])
        else:
            with open(tb, "rb") as f:
                got = custom_check.func_names(custom_check.wasm_customs(f.read()))
            if got != TEXT_NAMES:
                fails.append("the text module's name section has %r, not %r" % (got, TEXT_NAMES))
            to = os.path.join(tmp, "text.o")
            r = sh([WARMC, "-O0", "--target=linux", "-c", "-o", to, tw])
            with open(to, "rb") as f:
                syms = sorted(elf_local_funcs(f.read()))
            want = sorted([TEXT_NAMES.get(i, "wasm_func_%d" % i) for i in range(1, 5)])
            if syms != want:
                fails.append("the text module's functions are %r, not %r" % (syms, want))

        # the code the same with names as without
        if "linux" in named and "linux" in plain and elf_code(named["linux"]) != elf_code(plain["linux"]):
            diff = [k for k in elf_code(named["linux"]) if elf_code(named["linux"])[k] != elf_code(plain["linux"]).get(k)]
            fails.append("the ELF object's sections %s differ with names and without" % diff)
        if "macos" in named and "macos" in plain and macho_code(named["macos"]) != macho_code(plain["macos"]):
            fails.append("the Mach-O object's sections or relocations differ with names and without")

        # linked for the host, run, and named in what is linked
        exe = os.path.join(tmp, "named" + (".exe" if hostbuild.WINDOWS else ""))
        r = sh([WARMC, "-O2"] + gen_gosp.target_flags(BUILD) + ["-o", exe, wat])
        if r.returncode:
            fails.append("the named program does not link: " + r.stderr.strip()[:200])
        else:
            for args, want in ((["sum"], "i32:78"), (["fill"], "i32:1")):
                r = sh(RUN + [exe] + args)
                if r.returncode or r.stdout.strip() != want:
                    fails.append("%s: %s, not %s (exit %d: %s)" % (args[0], r.stdout.strip(), want,
                                                                   r.returncode, r.stderr.strip()[:120]))
            r = sh(RUN + [exe, "div", "0"])
            if r.returncode != 1 or "integer divide by zero" not in r.stderr:
                fails.append("div 0 does not trap through the runtime: exit %d: %s"
                             % (r.returncode, r.stderr.strip()[:120]))
            # -O2 inlines most of them; the ones left carry their names
            with open(exe, "rb") as f:
                img = f.read()
            local = (elf_local_funcs(img) if img[:4] == b"\x7fELF" else
                     macho_local_text(img) if img[:4] == b"\xcf\xfa\xed\xfe" else None)
            if local is not None:
                r = sh([WARMC, "-O0"] + gen_gosp.target_flags(BUILD) + ["-o", exe + "0", wat])
                with open(exe + "0", "rb") as f:
                    img0 = f.read()
                local0 = elf_local_funcs(img0) if img0[:4] == b"\x7fELF" else macho_local_text(img0)
                missing = [n for n in set(WANT.values()) if n not in local0]
                if missing:
                    fails.append("the -O0 program's symbols lack %r" % sorted(missing))
                if local0.count("dup") != 2:
                    fails.append("the -O0 program has %d symbols named dup, not 2" % local0.count("dup"))

    for f in fails:
        print("FAIL[names] " + f)
    print("names: function symbols from the name section in ELF, Mach-O and COFF, "
          "%d malformed sections refused, code alike with and without; %d failed"
          % (len(MALFORMED), len(fails)))
    return 1 if fails else 0

if __name__ == "__main__":
    sys.exit(main())
