"""test/cases_opt/die_tie.wat: a result takes the register it is hinted to
from an operand that dies where it is made (regalloc.inc, the scan's rule
ahead of its hint; RA_DIE_TIE, arm64).  The module's header says what each
function is; every answer is worked out here.

  python3 test/gen_die_tie.py test/cases_opt/die_tie.wat
"""
import math
import struct
import sys

M64 = (1 << 64) - 1
ROUNDS = 64


def s64(v):
    v &= M64
    return v - (1 << 64) if v >> 63 else v


def rotl(x, k):
    k &= 63
    return ((x << k) | (x >> (64 - k))) & M64 if k else x


def consts():
    x, out = 0x243f6a8885a308d3, []
    for i in range(ROUNDS):
        x = (x * 6364136223846793005 + 1442695040888963407) & M64
        out.append((1 + i * 7 % 63, x | 1, (x >> 7) ^ 0x9e3779b97f4a7c15))
    return out


def big(x):
    """a function of more instructions than any call takes in"""
    x &= M64
    for r, m, k in consts():
        x = ((rotl(x, r) ^ k) * m) & M64
    return x


BIG = "  (func $big (param $x i64) (result i64)\n" + "".join(
    "    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const %d)) (i64.const %d)) (i64.const %d)))\n"
    % (r, s64(k), s64(m)) for r, m, k in consts()) + "    (local.get $x))\n"

MEM = bytes((i * 37 + 0x85) & 0xff for i in range(64))


def ld(p):
    return int.from_bytes(MEM[p:p + 8], "little")


MOV = r"\smov\s+x\d+, x\d+$"
# name, parameters and result, body, model, shapes
FUNCS = [
    # a call's argument made out of the parameter that came in its register
    ("arg", "(param $p i64) (result i64)",
     "(call $big (i64.add (local.get $p) (i64.const 3)))",
     lambda p: big(p + 3), {MOV: 0, r"\sadd\s+x1, x1, #0x3": 1}),
    ("arg2", "(param $p i64) (param $q i64) (result i64)",
     "(call $big (i64.sub (local.get $q) (local.get $p)))",
     lambda p, q: big(q - p), {MOV: 0, r"\ssub\s+x1, x2, x1": 1}),
    ("arg_sh", "(param $p i64) (param $q i64) (result i64)",
     "(call $big (i64.add (local.get $p) (i64.shl (local.get $q) (i64.const 4))))",
     lambda p, q: big(p + (q << 4)), {MOV: 0, r"\sadd\s+x1, x1, x2, lsl #4": 1}),
    # what a call gives back, and what is made of it, in x0
    ("res", "(param $p i64) (result i64)",
     "(i64.xor (call $big (local.get $p)) (i64.const 0xff))",
     lambda p: big(p) ^ 0xff, {MOV: 0, r"\seor\s+x0, x0, #0xff": 1}),
    ("res2", "(param $p i64) (result i64)",
     "(i64.sub (i64.const 0) (i64.shr_u (call $big (i64.add (local.get $p) (local.get $p))) (i64.const 9)))",
     lambda p: -(big(p + p) >> 9), {MOV: 0}),
    # a value two ways hand the function's end
    ("web", "(param $p i64) (result i64)",
     "(if (result i64) (i64.eqz (local.get $p)) (then (i64.const 77))\n"
     "      (else (i64.add (call $big (local.get $p)) (i64.const 5))))",
     lambda p: 77 if p & M64 == 0 else big(p) + 5, {MOV: 0, r"\sadd\s+x0, x0, #0x5": 1}),
    # no call: each made where the last was
    ("shift", "(param $p i64) (result i64)",
     "(i64.shr_s (i64.extend32_s (local.get $p)) (i64.const 16))",
     lambda p: s64(((p & 0xffffffff) ^ 0x80000000) - 0x80000000) >> 16, {MOV: 0, r"\sasr\s+x0, x0, #16": 1}),
    ("fsq", "(param $a f64) (result f64)", "(f64.sqrt (local.get $a))",
     lambda a: math.sqrt(a), {r"\sfmov\s": 0, r"\sfsqrt\s+d0, d0": 1}),
    ("fadd", "(param $a f64) (param $b f64) (result f64)", "(f64.add (local.get $b) (local.get $a))",
     lambda a, b: a + b, {r"\sfmov\s": 0, r"\sfadd\s+d0, d1, d0": 1}),
    # the operand is read again after the result is made: not its register
    ("live", "(param $p i32) (result i64)",
     "(local $t i64) (local $r i64)\n"
     "    (local.set $t (i64.load (local.get $p)))\n"
     "    (local.set $r (i64.mul (local.get $t) (i64.const 3)))\n"
     "    (i64.store offset=8 (local.get $p) (local.get $t))\n"
     "    (i64.add (local.get $r) (i64.load offset=8 (local.get $p)))",
     lambda p: ld(p) * 4, {}),
    # the result lives across a call: not a register the call writes
    ("cross", "(param $p i32) (result i64)",
     "(local $r i64)\n"
     "    (local.set $r (i64.add (i64.load (local.get $p)) (i64.const 1)))\n"
     "    (global.set $g (call $big (i64.const 9)))\n"
     "    (i64.xor (local.get $r) (global.get $g))",
     lambda p: (ld(p) + 1) ^ big(9), {}),
    ("cross_ret", "(param $p i32) (result i64)",
     "(local $r i64)\n"
     "    (local.set $r (i64.add (i64.load (local.get $p)) (i64.const 1)))\n"
     "    (global.set $g (call $big (i64.const 9)))\n"
     "    (local.get $r)",
     lambda p: ld(p) + 1, {}),
    # a bfi reads the value its field comes from after it has written
    ("field", "(param $x i64) (param $o i64) (result i64)",
     "(call $big (i64.or (i64.and (local.get $o) (i64.const 0xffffffffffff00ff))\n"
     "                       (i64.and (i64.shl (local.get $x) (i64.const 8)) (i64.const 0xff00))))",
     lambda x, o: big((o & 0xffffffffffff00ff) | ((x << 8) & 0xff00)),
     {r"\sbfi\s+x0, x1, #8, #8": 1, MOV: 1}),
    # ... and writes into the other, in place where that dies there, though
    # the result is hinted nowhere
    ("field2", "(param $x i64) (param $o i64) (param $c i64) (result i64)",
     "(i64.mul (local.get $c)\n"
     "             (i64.or (i64.and (local.get $o) (i64.const 0xffffffffffff00ff))\n"
     "                     (i64.and (i64.shl (local.get $x) (i64.const 8)) (i64.const 0xff00))))",
     lambda x, o, c: c * ((o & 0xffffffffffff00ff) | ((x << 8) & 0xff00)), {MOV: 0, r"\sbfi\s": 1}),
]

# A wrap stays in its operand's register where that dies there (the scan's
# older tie), also where the wrap is one of several values an edge hands a
# block and not the one their web is named by.
WRAPWEB = """  (func $wrapweb (export "wrapweb") (param $a i64) (param $b i64) (param $c i32) (result i32)
    (local $w i32)
    (if (local.get $c)
      (then (i32.store (i32.const 64) (i32.const 1))
            (local.set $w (i32.wrap_i64 (i64.add (local.get $a) (local.get $b)))))
      (else (i32.store (i32.const 68) (i32.const 2))
            (local.set $w (i32.wrap_i64 (i64.mul (local.get $a) (local.get $b))))))
    (i32.add (i32.mul (local.get $w) (i32.const 3)) (i32.load (i32.const 64))))
"""


def wrapweb(a, b, c):
    w = ((a + b) if c else (a * b)) & 0xffffffff
    return (w * 3 + (1 if c else 0)) & 0xffffffff


# A bfi's field out of a value a loop carried round -- a loop of no count,
# which is not unrolled: one of several values in the register, and the one
# the bfi reads after it has written.
LOOPFIELD = """  (func $loopfield (export "loopfield") (param $x i64) (param $o i64) (param $n i64) (result i64)
    (loop $l
      (local.set $x (i64.mul (i64.rotl (local.get $x) (i64.const 3)) (i64.const 5)))
      (br_if $l (i64.gt_u (local.get $x) (local.get $n))))
    (local.set $o (i64.and (local.get $o) (i64.const 0xffffffffffff00ff)))
    (i64.add (local.get $o)
      (call $big (i64.or (local.get $o)
                         (i64.and (i64.shl (local.get $x) (i64.const 8)) (i64.const 0xff00))))))
"""


def loopfield(x, o, n):
    for _ in range(10000):
        x = (rotl(x, 3) * 5) & M64
        if x <= n:
            break
    else:
        raise ValueError("loopfield: no end for these arguments")
    o &= 0xffffffffffff00ff
    return o + big(o | ((x << 8) & 0xff00))


# A reference loaded through the collector's barrier: its mask is read
# after the reference is in its register, so that is not the mask's.
GC = """  (type $N (struct (field (mut (ref null $N))) (field (mut i64))))
  (func $gcnext (export "gcnext") (param $n (ref $N)) (result (ref null $N))
    (struct.get $N 0 (local.get $n)))
  (func $gcrun (export "gcrun") (param $k i64) (result i64)
    (local $a (ref null $N)) (local $b (ref null $N))
    (local.set $a (struct.new $N (ref.null $N) (local.get $k)))
    (local.set $b (struct.new $N (local.get $a) (i64.add (local.get $k) (i64.const 1))))
    (i64.add (struct.get $N 1 (ref.as_non_null (call $gcnext (ref.as_non_null (local.get $b)))))
             (i64.extend_i32_u (ref.is_null (call $gcnext (ref.as_non_null (local.get $a)))))))
"""

# More values live at once than there are registers: the scan runs under
# several rules, and counts what the one it keeps did.
NLIVE = 40
MANY = "  (func $many (export \"many\") (param $p i32) (result i64)\n    " + " ".join(
    "(local $t%d i64)" % i for i in range(NLIVE)) + " (local $s i64)\n" + "".join(
    "    (local.set $t%d (i64.mul (i64.load8_u offset=%d (local.get $p)) (i64.const %d)))\n" % (i, i % 24, 2 * i + 3)
    for i in range(NLIVE)) + "".join(
    "    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t%d) (local.get $t%d))))\n"
    % (i, NLIVE - 1 - i) for i in range(NLIVE)) + "    (i64.xor (call $big (local.get $s)) (i64.const 0xff)))\n"


def many(p):
    t = [MEM[p + i % 24] * (2 * i + 3) for i in range(NLIVE)]
    acc = 0
    for i in range(NLIVE):
        acc = (rotl(acc, 7) + (t[i] ^ t[NLIVE - 1 - i])) & M64
    return big(acc) ^ 0xff


# the results given an operand's register, the module compiled alone
TIES = 20

V = [0, 1, 2, 77, 0xffffffff, 0x80000000, 0x123456789abcdef0, M64, 0x8000000000000000]


def fbits(x):
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def main(path):
    funcs, shapes, runs = [BIG], [], []
    for name, sig, body, model, shape in FUNCS:
        funcs.append('  (func $%s (export "%s") %s\n    %s)\n' % (name, name, sig, body))
        for pat, n in (shape or {}).items():
            shapes.append(";; shape: %s %s=%d" % (name, pat, n))
        np = sig.count("(param")
        if "f64" in sig:
            # a float goes in and comes out as its bits
            for args in ((2.25, 4.0), (1e300, 3.5), (0.0, 1.5), (6.25, -0.5)):
                r = model(*args[:np])
                runs.append(";; run: %s %s => f64:0x%016x"
                            % (name, " ".join(str(fbits(a)) for a in args[:np]), fbits(r)))
        elif "(param $p i32)" in sig:
            for p in (0, 3, 8, 16, 40):
                runs.append(";; run: %s %d => i64:%d" % (name, p, s64(model(p))))
        else:
            for i, x in enumerate(V):
                args = (x, V[(i * 4 + 3) % len(V)], V[(i * 2 + 5) % len(V)])[:np]
                runs.append(";; run: %s %s => i64:%d" % (name, " ".join(str(a) for a in args), s64(model(*args))))
    funcs.append(WRAPWEB)
    # the first arm's wrap in place
    shapes.append(r";; shape: wrapweb \smov\s+w0, w0$=1")
    for a, b, c in ((3, 5, 1), (3, 5, 0), (M64, 2, 1), (0x100000001, 0x100000003, 0), (0x7fffffff, 2, 7)):
        runs.append(";; run: wrapweb %d %d %d => i32:%d" % (a, b, c, wrapweb(a, b, c) - ((wrapweb(a, b, c) >> 31) << 32)))
    funcs.append(LOOPFIELD)
    shapes.append(r";; shape: loopfield \sbfi\s+x0, x1, #8, #8=1")
    # (round the loop while x is over n: a trip or two, by x's bits)
    for x, o, n in ((1, 0xffff, M64 - 1), (0x1234, 0, 1 << 62), (M64, 0x123456789abcdef0, 1 << 63), (7, M64, 1 << 61)):
        runs.append(";; run: loopfield %d %d %d => i64:%d" % (x, o, n, s64(loopfield(x, o, n))))
    funcs.append(MANY)
    for p0 in (0, 7, 16, 30):
        runs.append(";; run: many %d => i64:%d" % (p0, s64(many(p0))))
    shapes.append(r";; shape: gcnext \stst\s+(x\d+), \1$=0")
    for k in (5, 1 << 40):
        runs.append(";; run: gcrun %d => i64:%d" % (k, k + 1))
    data = "".join("\\%02x" % b for b in MEM)
    with open(path, "w") as f:
        f.write(HEAD % {"TIES": TIES} + "\n".join(shapes) + "\n;;\n" + "\n".join(runs) +
                "\n\n(module\n" + GC + "  (memory 1)\n  (data (i32.const 0) \"%s\")\n"
                "  (global $g (mut i64) (i64.const 0))\n" % data + "".join(funcs) + ")\n")
    print("%s: %d functions, %d runs" % (path, len(FUNCS) + 6, len(runs)))


HEAD = """;; A result takes the register it is hinted to from an operand that dies
;; where it is made (regalloc.inc, the scan's rule ahead of its hint): an
;; a64 instruction reads its operands before it writes its result.  big, of
;; more instructions than a call takes in, is what the others call.  From
;; test/gen_die_tie.py, which works out every answer.
;;
;;   arg arg2 arg_sh   a call's argument made out of the parameters in the
;;                     register it goes in: no move
;;   res res2          a call's result, and what is made of it, in x0
;;   web               a value two ways hand the function's end, one of
;;                     them out of a call's result: in x0 on both
;;   shift             no call: each value made where the last was
;;   fsq fadd          a float's
;;   live              the operand is read again after the result is made:
;;                     the result is not in its register
;;   cross cross_ret   the result lives across a call: not in a register the
;;                     call writes
;;   field             a bfi's field comes from a value it reads after it
;;                     has written: the result is not in that value's
;;                     register
;;   field2            and it writes into its other operand: in that one's
;;                     register where it dies there, hinted or not
;;   loopfield         the field out of a value a loop carried round, the
;;                     bfi's other operand living on: the same
;;   wrapweb           a wrap stays in its operand's register where that
;;                     dies, also as one of the values two ways hand a block
;;   many              forty values live at once: the scan runs under
;;                     several rules, and counts once
;;   gcnext gcrun      a reference loaded through the collector's barrier,
;;                     whose mask is read after the reference is written:
;;                     not in the mask's register
;;
;; stats -O2 a64: die_ties=%(TIES)d
"""

if __name__ == "__main__":
    main(sys.argv[1])
