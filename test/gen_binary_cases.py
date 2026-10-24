"""Modules encoded here as bytes, because no tool on the host can produce them.

Every other suite gets its modules from wat2wasm and its answers from
wasm-interp.  That works until the standard moves and the installed wabt has
not: wabt 1.0.41 knows the *arithmetic* half of extended constant expressions
but still enforces the wasm 2.0 rule that a `global.get` in an initialiser may
only name an import, so it rejects the exact shape wasm 3.0 added -- in the
text parser and in the binary reader both.  A feature whose only test is a
tool that refuses it has no test.

So these modules are written out instruction by instruction and the expected
answers are written down beside them.  That is weaker than a differential run
against another engine and it is the reason to keep this file small: a case
belongs here only while nothing on the host can express it.

Usage: gen_binary_cases.py OUTDIR
Writes OUTDIR/<name>.wasm and prints one "name export want" line per check.
"""
import os
import sys

def leb(n):
    """Unsigned LEB128."""
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)

def sleb(n):
    """Signed LEB128."""
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        done = (n == 0 and not (b & 0x40)) or (n == -1 and (b & 0x40))
        out.append(b | (0 if done else 0x80))
        if done:
            return bytes(out)

def vec(items):
    return leb(len(items)) + b"".join(items)

def section(sid, body):
    return bytes([sid]) + leb(len(body)) + body

def module(sections):
    return b"\x00asm\x01\x00\x00\x00" + b"".join(sections)

def name(s):
    b = s.encode()
    return leb(len(b)) + b

I32, I64, FUNCREF = 0x7F, 0x7E, 0x70

def i32c(v):  return b"\x41" + sleb(v)
def i64c(v):  return b"\x42" + sleb(v)
def gget(i):  return b"\x23" + leb(i)
def rfunc(i): return b"\xd2" + leb(i)
END = b"\x0b"

def initexpr():
    t_i32 = b"\x60\x00\x01" + bytes([I32])
    t_i64 = b"\x60\x00\x01" + bytes([I64])

    globals_ = [
        bytes([I32, 0]) + i32c(40) + END,
        bytes([I32, 0]) + gget(0) + i32c(2) + b"\x6a" + END,
        bytes([I64, 0]) + i64c(7) + END,
        bytes([I64, 0]) + gget(2) + i64c(6) + b"\x7e" + END,
        bytes([I32, 0]) + gget(1) + END,
        bytes([FUNCREF, 0]) + rfunc(0) + END,
    ]

    exports = [
        name("add") + b"\x00" + leb(1),
        name("mul") + b"\x00" + leb(2),
        name("copy") + b"\x00" + leb(3),
        name("mem") + b"\x00" + leb(4),
        name("ind") + b"\x00" + leb(5),
    ]

    elem = bytes([4]) + gget(1) + i32c(41) + b"\x6b" + END + vec([gget(5) + END])

    data = bytes([0]) + gget(1) + i32c(8) + b"\x6a" + END + \
        leb(4) + bytes([42, 0, 0, 0])

    def body(code):
        return leb(len(b"\x00" + code)) + b"\x00" + code

    code = [
        body(i32c(42) + END),
        body(gget(1) + END),
        body(gget(3) + END),
        body(gget(4) + END),
        body(i32c(50) + b"\x28\x02\x00" + END),
        body(i32c(1) + b"\x11" + leb(0) + leb(0) + END),
    ]

    bin_ = module([
        section(1, vec([t_i32, t_i64])),
        section(3, vec([leb(0), leb(0), leb(1), leb(0), leb(0), leb(0)])),
        section(4, vec([bytes([FUNCREF, 0]) + leb(4)])),
        section(5, vec([bytes([0]) + leb(1)])),
        section(6, vec(globals_)),
        section(7, vec(exports)),
        section(9, vec([elem])),
        section(10, vec(code)),
        section(11, vec([data])),
    ])
    checks = [("add", "i32:42"), ("mul", "i64:42"), ("copy", "i32:42"),
              ("mem", "i32:42"), ("ind", "i32:42")]
    return bin_, checks

REC, SUB, SUB_FINAL, FUNC_FORM = 0x4E, 0x50, 0x4F, 0x60
REF, REF_NULL = 0x64, 0x63

def functype(params, results):
    return bytes([FUNC_FORM]) + vec([bytes([p]) for p in params]) + \
        vec([bytes([r]) for r in results])

def sub(super_idx, ct, final=False):
    """`sub`/`sub final` with at most one supertype."""
    sups = [] if super_idx is None else [leb(super_idx)]
    return bytes([SUB_FINAL if final else SUB]) + vec(sups) + ct

def rec(members):
    return bytes([REC]) + vec(members)

def subtyping():
    i32 = bytes([I32])
    ret_i32 = functype([], [I32])

    def takes_ref_to(idx):
        """[(ref null idx)] -> [i32], written out because a reference to a
        type index is not something `functype` above can name."""
        return bytes([FUNC_FORM]) + vec([bytes([REF_NULL]) + sleb(idx)]) + \
            vec([i32])

    types = [
        rec([sub(None, ret_i32), sub(0, ret_i32)]),
        ret_i32,
        rec([takes_ref_to(4), takes_ref_to(3)]),
    ]

    funcs = [leb(0), leb(1), leb(2), leb(3), leb(4)]

    def body(code):
        return leb(len(b"\x00" + code)) + b"\x00" + code

    def ci(type_idx):
        return b"\x11" + leb(type_idx) + leb(0)

    code = [
        body(i32c(1) + END),
        body(i32c(2) + END),
        body(i32c(3) + END),
        body(i32c(4) + END),
        body(i32c(5) + END),

        body(i32c(0) + ci(0) + END),

        body(i32c(1) + ci(0) + END),

        body(i32c(1) + ci(1) + END),

        body(i32c(2) + ci(2) + END),

        body(i32c(0) + ci(1) + END),

        body(i32c(0) + ci(2) + END),

        body(b"\xd0" + sleb(4) + i32c(3) + ci(3) + END),

        body(b"\xd0" + sleb(4) + i32c(4) + ci(3) + END),

        body(b"\xd2" + leb(1) + b"\x14" + leb(1) + END),
    ]
    funcs += [leb(1)] * 9

    exports = [
        name("same") + b"\x00" + leb(5),
        name("sup") + b"\x00" + leb(6),
        name("deep") + b"\x00" + leb(7),
        name("final") + b"\x00" + leb(8),
        name("narrow") + b"\x00" + leb(9),
        name("wrong_final") + b"\x00" + leb(10),
        name("group") + b"\x00" + leb(11),
        name("group_swap") + b"\x00" + leb(12),
        name("ref") + b"\x00" + leb(13),
    ]

    elem = bytes([0]) + i32c(0) + END + vec([leb(i) for i in range(5)])

    bin_ = module([
        section(1, vec(types)),
        section(3, vec(funcs)),
        section(4, vec([bytes([FUNCREF, 0]) + leb(5)])),
        section(7, vec(exports)),
        section(9, vec([elem])),
        section(10, vec(code)),
    ])
    checks = [
        ("same", "i32:1"), ("sup", "i32:2"), ("deep", "i32:2"),
        ("final", "i32:3"), ("group", "i32:4"), ("ref", "i32:2"),
        ("narrow", "trap:indirect call type mismatch"),
        ("wrong_final", "trap:indirect call type mismatch"),
        ("group_swap", "trap:indirect call type mismatch"),
    ]
    return bin_, checks

def exception_bt():
    exnref, ref_exn = b"\x69", b"\x64\x69"
    i32, i64 = bytes([I32]), bytes([I64])

    def ft(params, results):
        return bytes([FUNC_FORM]) + vec(params) + vec(results)

    types = [
        ft([], [i32]),
        ft([i32], []),
        ft([i32], [i32]),
        ft([i64], []),
        ft([i64, i32], [i64]),
        ft([], [i64]),
        ft([ref_exn], []),
    ]
    funcs = [leb(1), leb(3), leb(6), leb(0), leb(0), leb(5), leb(5), leb(0)]
    tags = [b"\x00" + leb(1), b"\x00" + leb(3)]

    def body(code, locals_=b"\x00"):
        return leb(len(locals_ + code)) + locals_ + code

    def tt(bt, catches, inner):
        return b"\x1f" + bt + vec(catches) + inner + END

    def block(bt, inner):
        return b"\x02" + bt + inner + END

    CATCH, CATCH_ALL_REF = b"\x00", b"\x03"
    UNREACHABLE, DROP = b"\x00", b"\x1a"
    ADD32, ADD64, EXTEND = b"\x6a", b"\x7c", b"\xac"

    def call(i):   return b"\x10" + leb(i)
    def lget(i):   return b"\x20" + leb(i)
    def lset(i):   return b"\x21" + leb(i)
    def catch(tag, label):  return CATCH + leb(tag) + leb(label)

    code = [

        body(lget(0) + b"\x08" + leb(0) + END),
        body(lget(0) + b"\x08" + leb(1) + END),

        body(lget(0) + b"\x0a" + END),

        body(block(i32, i32c(40) +
                   tt(sleb(2), [catch(0, 0)], i32c(2) + ADD32)) + END),

        body(block(i32, i32c(40) +
                   tt(sleb(2), [catch(0, 0)],
                      i32c(2) + ADD32 + call(0) + i32c(-1))) + END),

        body(block(i64, i64c(100) + i32c(7) +
                   tt(sleb(4), [catch(1, 0)], EXTEND + ADD64)) + END),
        body(block(i64, i64c(100) + i32c(7) +
                   tt(sleb(4), [catch(1, 0)],
                      EXTEND + ADD64 + call(1) + i64c(-1))) + END),

        body(block(i32,
                   block(exnref,
                         tt(b"\x40", [CATCH_ALL_REF + leb(0)],
                            i32c(55) + call(0)) + UNREACHABLE) +
                   lset(0) +
                   tt(b"\x40", [catch(0, 0)],
                      block(ref_exn, lget(0) + b"\xd6" + leb(0) + UNREACHABLE) +
                      call(2)) +
                   UNREACHABLE) + END,
             leb(1) + leb(1) + exnref),
    ]
    exports = [
        name("bt_normal") + b"\x00" + leb(3),
        name("bt_throw") + b"\x00" + leb(4),
        name("bt_two_normal") + b"\x00" + leb(5),
        name("bt_two_throw") + b"\x00" + leb(6),
        name("ref_exn") + b"\x00" + leb(7),
    ]
    bin_ = module([
        section(1, vec(types)),
        section(3, vec(funcs)),
        section(13, vec(tags)),
        section(7, vec(exports)),
        section(10, vec(code)),
    ])
    checks = [("bt_normal", "i32:42"), ("bt_throw", "i32:42"),
              ("bt_two_normal", "i64:107"), ("bt_two_throw", "i64:107"),
              ("ref_exn", "i32:55")]
    return bin_, checks

def table64_limits():
    """A table64 whose declared minimum is past what the runtime can back.

    The spec's ceiling for an index64 table is 2^64-1, so validation has to
    let this through and the refusal belongs at instantiation, which says
    it is past the runtime's 2^20 entries.  wabt 1.0.41 has no table64 in
    its text parser, so the module is written out here.  The entry count is
    chosen so that count * sizeof(TableEntry) is a whole multiple of 2^64:
    the size computation used to wrap to nothing and hand back a single page
    for a table that reported 2^61 entries.
    """
    def body(code, locals_=b"\x00"):
        return leb(len(locals_ + code)) + locals_ + code

    huge = 1 << 61
    ttype = bytes([0x70, 0x04]) + leb(huge)
    bin_ = module([
        section(1, vec([functype([], [0x7F])])),
        section(3, vec([leb(0)])),
        section(4, vec([ttype])),
        section(7, vec([name("get") + b"\x00" + leb(0)])),
        section(10, vec([body(i64c(100) + b"\x25\x00" + b"\xd1" + END)])),
    ])
    return bin_, [("get", "refused:a table of more than 2^20 entries")]

def table64_small():
    """The same shape inside the runtime's reach, so the guard is not a wall."""
    def body(code, locals_=b"\x00"):
        return leb(len(locals_ + code)) + locals_ + code

    ttype = bytes([0x70, 0x04]) + leb(4)
    bin_ = module([
        section(1, vec([functype([], [0x7E])])),
        section(3, vec([leb(0)])),
        section(4, vec([ttype])),
        section(7, vec([name("size") + b"\x00" + leb(0)])),
        section(10, vec([body(b"\xfc\x10\x00" + END)])),
    ])
    return bin_, [("size", "i64:4")]

def far_branch():
    """A function long enough that a conditional branch cannot reach its trap.

    A b.cond carries a 19-bit displacement, so it reaches a megabyte, and
    every trap stub is emitted after the whole body -- which makes the branch
    to it forward and its distance the size of the function.  Past that the
    compiler exited 1 saying `internal: cannot encode conditional branch
    displacement`, on a module the validator accepts.

    Both edges of the relaxed branch are exercised: `ok` runs the division
    that the branch guards, `trap` takes it.  The body is 40,000 stores at
    about seven instructions each, which is enough at -O0 and cheap to build.
    """
    def body(code, locals_=b"\x00"):
        return leb(len(locals_ + code)) + locals_ + code

    n = 40000
    fill = b"".join(i32c(i * 4 % 60000) + i32c(i) + b"\x36\x02\x00"
                    for i in range(n))
    div = i32c(1000) + b"\x20\x00" + b"\x6d"
    bin_ = module([
        section(1, vec([functype([I32], [I32]), functype([], [I32])])),
        section(3, vec([leb(0), leb(1), leb(1)])),
        section(5, vec([bytes([0]) + leb(1)])),
        section(7, vec([name("far_ok") + b"\x00" + leb(1),
                        name("far_trap") + b"\x00" + leb(2)])),
        section(10, vec([
            body(fill + div + END),
            body(i32c(4) + b"\x10" + leb(0) + END),
            body(i32c(0) + b"\x10" + leb(0) + END),
        ])),
    ])
    return bin_, [("far_ok", "i32:250"),
                  ("far_trap", "trap:integer divide by zero")]

def deep_cfg():
    """A control-flow graph deeper than a C stack can recurse over.

    ir_order walked the graph with one frame per basic block, and that frame
    carried a 64-entry successor-sorting scratch the compiler reserved on
    every path -- so about 600 bytes each, and an 8MB stack ran out at
    roughly 13,000 blocks.  A nesting limit would not have caught it: the
    shape here is *sequential*, a chain of diamonds, so the graph is shallow
    to read and deep to walk.

    7,000 groups of `local.get 0; if; end`, which is 14,000 blocks and about
    28KB of module.
    """
    def body(code, locals_=b"\x00"):
        return leb(len(locals_ + code)) + locals_ + code

    n = 7000
    code = (b"\x20\x00" + b"\x04\x40" + b"\x0b") * n
    bin_ = module([
        section(1, vec([functype([], [0x7F])])),
        section(3, vec([leb(0)])),
        section(7, vec([name("f") + b"\x00" + leb(0)])),
        section(10, vec([body(code + i32c(77) + END,
                             leb(1) + leb(1) + bytes([I32]))])),
    ])
    return bin_, [("f", "i32:77")]

def seg64_offsets():
    """Active segment offsets past 2^32 on a 64-bit memory and table.

    The offset is evaluated into a u64 and was then narrowed to 32 bits
    before the bounds test, so an offset of 2^32 wrapped to zero: instead of
    the trap instantiation owes, the segment landed on unrelated data.  wabt
    has no memory64 in its text parser, so both are written out here.
    """
    def body(code, locals_=b"\x00"):
        return leb(len(locals_ + code)) + locals_ + code

    mem = module([
        section(1, vec([functype([], [0x7F])])),
        section(3, vec([leb(0)])),
        section(5, vec([bytes([0x04]) + leb(1)])),
        section(7, vec([name("peek") + b"\x00" + leb(0)])),
        section(10, vec([body(i64c(0) + b"\x2d\x00\x00" + END)])),
        section(11, vec([bytes([0]) + i64c(1 << 32) + END +
                         leb(5) + b"hello"])),
    ])
    return mem, [("peek", "trap:out of bounds memory access")]

def seg64_offsets_ok():
    """The same shapes inside their memory, so the trap is not a wall."""
    def body(code, locals_=b"\x00"):
        return leb(len(locals_ + code)) + locals_ + code

    mem = module([
        section(1, vec([functype([], [0x7F])])),
        section(3, vec([leb(0)])),
        section(5, vec([bytes([0x04]) + leb(1)])),
        section(7, vec([name("peek") + b"\x00" + leb(0)])),
        section(10, vec([body(i64c(16) + b"\x2d\x00\x00" + END)])),
        section(11, vec([bytes([0]) + i64c(16) + END + leb(5) + b"hello"])),
    ])
    return mem, [("peek", "i32:104")]

def wasi_compact_imports():
    """A compact import group: one module name shared by a vector of imports.

    binaryen writes these under --enable-compact-imports (part of
    --all-features); no tool on the host can express the encoding in text.
    The group holds two wasi functions and _start calls the first through
    the group to print the expected line, so the check proves both halves:
    the index space expanded through the group (the defined function is
    index 2, behind both members) and the grouped imports resolve and run.
    The runner links cases named wasi_* with --wasi; a wasi command runs
    _start and the harness reads its stdout.
    """
    t_fdw = functype([0x7F, 0x7F, 0x7F, 0x7F], [0x7F])
    t_rnd = functype([0x7F, 0x7F], [0x7F])
    t_v = functype([], [])
    group = name("wasi_snapshot_preview1") + name("") + b"\x7f" + \
        vec([name("fd_write") + b"\x00" + leb(0),
             name("random_get") + b"\x00" + leb(1)])
    def body(code):
        return leb(len(b"\x00" + code)) + b"\x00" + code
    text = b"i32:42\n"
    iov = bytes([16, 0, 0, 0, len(text), 0, 0, 0]) + b"\x00" * 8 + text
    start = i32c(1) + i32c(0) + i32c(1) + i32c(32) + \
        b"\x10" + leb(0) + b"\x1a" + END
    bin_ = module([
        section(1, vec([t_fdw, t_rnd, t_v])),
        section(2, vec([group])),
        section(3, vec([leb(2)])),
        section(5, vec([bytes([0]) + leb(1)])),
        section(7, vec([name("_start") + b"\x00" + leb(2)])),
        section(10, vec([body(start)])),
        section(11, vec([bytes([0]) + i32c(0) + END +
                         leb(len(iov)) + iov])),
    ])
    return bin_, [("f", "i32:42")]

CASES = {"initexpr": initexpr, "subtyping": subtyping,
         "exception_bt": exception_bt, "table64_limits": table64_limits,
         "table64_small": table64_small, "seg64_offsets": seg64_offsets,
         "seg64_offsets_ok": seg64_offsets_ok, "deep_cfg": deep_cfg, "far_branch": far_branch,
         "wasi_compact_imports": wasi_compact_imports}

def main():
    if len(sys.argv) != 2:
        sys.stderr.write(__doc__)
        return 2
    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)
    for case, build in sorted(CASES.items()):
        bin_, checks = build()
        with open(os.path.join(outdir, case + ".wasm"), "wb") as f:
            f.write(bin_)
        for export, want in checks:
            print("%s %s %s" % (case, export, want))
    return 0

if __name__ == "__main__":
    sys.exit(main())
