"""test/cases_opt/sign_bits.wat: how many of a value's top bits are copies
of its sign (ir.c ir_sign_ge), asked by the fold that drops a sign extension
of a value that is one already (opt.c's I_EXT8S, I_EXT16S and I_EXT32S).
Each function is an extension of an expression: dropped where the
expression has the copies, kept one short of them.  The module's header
says which; every answer is worked out here.

  python3 test/gen_sign_bits.py test/cases_opt/sign_bits.wat
"""
import sys

M64 = (1 << 64) - 1
M32 = (1 << 32) - 1


def sx(v, bits, w=64):
    v &= (1 << bits) - 1
    if v >> (bits - 1):
        v -= 1 << bits
    return v & ((1 << w) - 1)


def s(v, w=64):
    v &= (1 << w) - 1
    return v - (1 << w) if v >> (w - 1) else v


def shrs(v, k, w=64):
    return (s(v, w) >> k) & ((1 << w) - 1)


MEM = bytes((i * 37 + 0x85) & 0xff for i in range(64))


def ld(p, n, signed, w=64):
    return int.from_bytes(MEM[p:p + n], "little", signed=signed) & ((1 << w) - 1)


P, Q = "(local.get $p)", "(local.get $q)"
A, B = "(local.get $a)", "(local.get $b)"

# name, parameters, the extension (its bits and width), the expression, its
# model, whether the extension goes
I64, I32 = "(param $p i64) (param $q i64)", "(param $a i32) (param $b i32)"
CASES = [
    # a right shift: its amount and one, and its operand's beyond
    ("shrs32", I64, 32, 64, "(i64.shr_s %s (i64.const 32))" % P, lambda p, q: shrs(p, 32), True),
    ("shrs31", I64, 32, 64, "(i64.shr_s %s (i64.const 31))" % P, lambda p, q: shrs(p, 31), False),
    ("shrs_of", I64, 16, 64, "(i64.shr_s (i64.extend32_s %s) (i64.const 16))" % P,
     lambda p, q: shrs(sx(p, 32), 16), True),
    ("shrs_of15", I64, 16, 64, "(i64.shr_s (i64.extend32_s %s) (i64.const 15))" % P,
     lambda p, q: shrs(sx(p, 32), 15), False),
    # zeros known from there up
    ("shru33", I64, 32, 64, "(i64.shr_u %s (i64.const 33))" % P, lambda p, q: p >> 33, True),
    ("shru32", I64, 32, 64, "(i64.shr_u %s (i64.const 32))" % P, lambda p, q: p >> 32, False),
    ("andk", I64, 32, 64, "(i64.and %s (i64.const 0x7fffffff))" % P, lambda p, q: p & 0x7fffffff, True),
    ("andk32", I64, 32, 64, "(i64.and %s (i64.const 0xffffffff))" % P, lambda p, q: p & M32, False),
    # a constant in range, with a value that has them
    ("ork", I64, 32, 64, "(i64.or (i64.extend16_s %s) (i64.const 0x7fff0000))" % P,
     lambda p, q: sx(p, 16) | 0x7fff0000, True),
    ("ork31", I64, 32, 64, "(i64.or (i64.extend16_s %s) (i64.const 0x80000000))" % P,
     lambda p, q: sx(p, 16) | 0x80000000, False),
    ("orneg", I64, 32, 64, "(i64.xor (i64.extend16_s %s) (i64.const -0x80000000))" % P,
     lambda p, q: sx(p, 16) ^ (-0x80000000 & M64), True),
    ("orneg31", I64, 32, 64, "(i64.xor (i64.extend16_s %s) (i64.const -0x80000001))" % P,
     lambda p, q: sx(p, 16) ^ (-0x80000001 & M64), False),
    # an extension, from where it extends
    ("ext8in16", I64, 16, 64, "(i64.xor (i64.extend8_s %s) (i64.const 0x55))" % P,
     lambda p, q: sx(p, 8) ^ 0x55, True),
    ("ext16in8", I64, 8, 64, "(i64.xor (i64.extend16_s %s) (i64.const 0x55))" % P,
     lambda p, q: sx(p, 16) ^ 0x55, False),
    ("ext32in16", I64, 16, 64, "(i64.xor (i64.extend32_s %s) (i64.const 0x55))" % P,
     lambda p, q: sx(p, 32) ^ 0x55, False),
    ("ext16in32", I64, 32, 64, "(i64.xor (i64.extend16_s %s) (i64.const 0x55))" % P,
     lambda p, q: sx(p, 16) ^ 0x55, True),
    ("ext8in8", I64, 8, 64, "(i64.xor (i64.extend8_s %s) (i64.const 0x15))" % P,
     lambda p, q: sx(p, 8) ^ 0x15, True),
    ("ext16in16", I64, 16, 64, "(i64.xor (i64.extend16_s %s) (i64.const 0x15))" % P,
     lambda p, q: sx(p, 16) ^ 0x15, True),
    ("ext32in32", I64, 32, 64, "(i64.xor (i64.extend32_s %s) (i64.const 0x15))" % P,
     lambda p, q: sx(p, 32) ^ 0x15, True),
    # a widening, and the i32's own beyond it
    ("widen", "(param $a i32) (param $b i32)", 32, 64,
     "(i64.xor (i64.extend_i32_s %s) (i64.const 0x55))" % A, lambda a, b: sx(a, 32) ^ 0x55, True),
    ("widen16", "(param $a i32) (param $b i32)", 16, 64,
     "(i64.xor (i64.extend_i32_s (i32.extend16_s %s)) (i64.const 0x55))" % A,
     lambda a, b: sx(a, 16) ^ 0x55, True),
    ("widen15", "(param $a i32) (param $b i32)", 16, 64,
     "(i64.xor (i64.extend_i32_s (i32.shr_s %s (i32.const 15))) (i64.const 0x55))" % A,
     lambda a, b: sx(shrs(a, 15, 32), 32) ^ 0x55, False),
    # a signed load
    ("ld16s", I64, 16, 64, "(i64.xor (i64.load16_s (i32.const 3)) %s)" % "(i64.extend8_s %s)" % P,
     lambda p, q: ld(3, 2, True) ^ sx(p, 8), True),
    ("ld16s8", I64, 8, 64, "(i64.xor (i64.load16_s (i32.const 3)) %s)" % "(i64.extend8_s %s)" % P,
     lambda p, q: ld(3, 2, True) ^ sx(p, 8), False),
    ("ld32s", I64, 32, 64, "(i64.xor (i64.load32_s (i32.const 3)) %s)" % "(i64.extend8_s %s)" % P,
     lambda p, q: ld(3, 4, True) ^ sx(p, 8), True),
    ("ld32s16", I64, 16, 64, "(i64.xor (i64.load32_s (i32.const 3)) %s)" % "(i64.extend8_s %s)" % P,
     lambda p, q: ld(3, 4, True) ^ sx(p, 8), False),
    ("ld64", I64, 32, 64, "(i64.xor (i64.load (i32.const 3)) %s)" % "(i64.extend8_s %s)" % P,
     lambda p, q: ld(3, 8, True) ^ sx(p, 8), False),
    ("ld32u", I64, 32, 64, "(i64.xor (i64.load32_u (i32.const 0)) %s)" % "(i64.extend8_s %s)" % P,
     lambda p, q: ld(0, 4, False) ^ sx(p, 8), False),
    ("ld16u", I64, 32, 64, "(i64.xor (i64.load16_u (i32.const 3)) %s)" % "(i64.extend8_s %s)" % P,
     lambda p, q: ld(3, 2, False) ^ sx(p, 8), True),
    # a wrap of a value with 32 more
    ("wrap48", I64, 16, 32, "(i32.wrap_i64 (i64.shr_s %s (i64.const 48)))" % P,
     lambda p, q: shrs(p, 48) & M32, True),
    ("wrap47", I64, 16, 32, "(i32.wrap_i64 (i64.shr_s %s (i64.const 47)))" % P,
     lambda p, q: shrs(p, 47) & M32, False),
    # a left shift takes its amount away
    ("shl16", I64, 32, 64, "(i64.shl (i64.extend16_s %s) (i64.const 16))" % P,
     lambda p, q: (sx(p, 16) << 16) & M64, True),
    ("shl17", I64, 32, 64, "(i64.shl (i64.extend16_s %s) (i64.const 17))" % P,
     lambda p, q: (sx(p, 16) << 17) & M64, False),
    # and, or, xor, select: both
    ("and2", I64, 32, 64, "(i64.and (i64.extend16_s %s) (i64.shr_s %s (i64.const 40)))" % (P, Q),
     lambda p, q: sx(p, 16) & shrs(q, 40), True),
    ("and1", I64, 32, 64, "(i64.and (i64.extend16_s %s) %s)" % (P, Q), lambda p, q: sx(p, 16) & q, False),
    ("and1b", I64, 32, 64, "(i64.and %s (i64.extend16_s %s))" % (Q, P), lambda p, q: sx(p, 16) & q, False),
    ("or2", I64, 32, 64, "(i64.or (i64.extend16_s %s) (i64.shr_s %s (i64.const 40)))" % (P, Q),
     lambda p, q: sx(p, 16) | shrs(q, 40), True),
    ("xor1", I64, 32, 64, "(i64.xor (i64.extend16_s %s) %s)" % (P, Q), lambda p, q: sx(p, 16) ^ q, False),
    ("sel2", I64, 32, 64,
     "(select (i64.extend16_s %s) (i64.shr_s %s (i64.const 40)) (i64.gt_s %s %s))" % (P, Q, P, Q),
     lambda p, q: sx(p, 16) if s(p) > s(q) else shrs(q, 40), True),
    ("sel1", I64, 32, 64, "(select (i64.extend16_s %s) %s (i64.gt_s %s %s))" % (P, Q, P, Q),
     lambda p, q: sx(p, 16) if s(p) > s(q) else q, False),
    ("sel1b", I64, 32, 64, "(select %s (i64.extend16_s %s) (i64.gt_s %s %s))" % (Q, P, P, Q),
     lambda p, q: q if s(p) > s(q) else sx(p, 16), False),
    # a sum or a difference: one more each
    ("add34", I64, 32, 64, "(i64.add (i64.shr_s %s (i64.const 33)) (i64.shr_s %s (i64.const 33)))" % (P, Q),
     lambda p, q: (shrs(p, 33) + shrs(q, 33)) & M64, True),
    ("add33", I64, 32, 64, "(i64.add (i64.shr_s %s (i64.const 32)) (i64.shr_s %s (i64.const 33)))" % (P, Q),
     lambda p, q: (shrs(p, 32) + shrs(q, 33)) & M64, False),
    ("add33b", I64, 32, 64, "(i64.add (i64.shr_s %s (i64.const 33)) (i64.shr_s %s (i64.const 32)))" % (P, Q),
     lambda p, q: (shrs(p, 33) + shrs(q, 32)) & M64, False),
    ("sub34", I64, 32, 64, "(i64.sub (i64.shr_s %s (i64.const 33)) (i64.shr_s %s (i64.const 33)))" % (P, Q),
     lambda p, q: (shrs(p, 33) - shrs(q, 33)) & M64, True),
    ("sub33", I64, 32, 64, "(i64.sub (i64.shr_s %s (i64.const 33)) (i64.shr_s %s (i64.const 32)))" % (P, Q),
     lambda p, q: (shrs(p, 33) - shrs(q, 32)) & M64, False),
    ("add_w", I32, 32, 64, "(i64.add (i64.extend_i32_s %s) (i64.extend_i32_s %s))" % (A, B),
     lambda a, b: (sx(a, 32) + sx(b, 32)) & M64, False),
    ("add_ld", I64, 16, 64, "(i64.add (i64.load16_s (i32.const 5)) (i64.load16_s (i32.const 12)))",
     lambda p, q: (ld(5, 2, True) + ld(12, 2, True)) & M64, False),
    ("xor_ld", I64, 16, 64, "(i64.xor (i64.load16_s (i32.const 5)) (i64.load16_s (i32.const 12)))",
     lambda p, q: ld(5, 2, True) ^ ld(12, 2, True), True),
    # a product: half of the copies wanted and the width together, each
    ("mul49", I64, 32, 64, "(i64.mul (i64.shr_s %s (i64.const 48)) (i64.shr_s %s (i64.const 48)))" % (P, Q),
     lambda p, q: (s(shrs(p, 48)) * s(shrs(q, 48))) & M64, True),
    ("mul48", I64, 32, 64, "(i64.mul (i64.shr_s %s (i64.const 48)) (i64.shr_s %s (i64.const 47)))" % (P, Q),
     lambda p, q: (s(shrs(p, 48)) * s(shrs(q, 47))) & M64, False),
    ("mul48b", I64, 32, 64, "(i64.mul (i64.shr_s %s (i64.const 47)) (i64.shr_s %s (i64.const 48)))" % (P, Q),
     lambda p, q: (s(shrs(p, 47)) * s(shrs(q, 48))) & M64, False),
    # an i64 narrowed and widened again (opt.c's I_EXTENDS of a wrap)
    ("rewiden33", I64, 32, 64, "(i64.shr_s (i64.xor %s %s) (i64.const 32))" % (P, Q),
     lambda p, q: shrs(p ^ q, 32), True),
    ("rewiden32", I64, 32, 64, "(i64.shr_s (i64.xor %s %s) (i64.const 31))" % (P, Q),
     lambda p, q: shrs(p ^ q, 31), False),
    # an i32's
    ("w_shrs24", I32, 8, 32, "(i32.shr_s %s (i32.const 24))" % A, lambda a, b: shrs(a, 24, 32), True),
    ("w_shrs23", I32, 8, 32, "(i32.shr_s %s (i32.const 23))" % A, lambda a, b: shrs(a, 23, 32), False),
    ("w_ld8", I32, 16, 32, "(i32.xor (i32.load8_s (i32.const 5)) (i32.shr_s %s (i32.const 20)))" % A,
     lambda a, b: ld(5, 1, True, 32) ^ shrs(a, 20, 32), True),
    ("w_ld16", I32, 8, 32, "(i32.xor (i32.load16_s (i32.const 5)) (i32.shr_s %s (i32.const 26)))" % A,
     lambda a, b: ld(5, 2, True, 32) ^ shrs(a, 26, 32), False),
    ("w_k", I32, 8, 32, "(i32.xor (i32.shr_s %s (i32.const 25)) (i32.const 0x40))" % A,
     lambda a, b: shrs(a, 25, 32) ^ 0x40, True),
    ("w_k80", I32, 8, 32, "(i32.xor (i32.shr_s %s (i32.const 25)) (i32.const 0x80))" % A,
     lambda a, b: shrs(a, 25, 32) ^ 0x80, False),
    ("w_kneg", I32, 8, 32, "(i32.xor (i32.shr_s %s (i32.const 25)) (i32.const -128))" % A,
     lambda a, b: shrs(a, 25, 32) ^ (-128 & M32), True),
    ("w_kneg81", I32, 8, 32, "(i32.xor (i32.shr_s %s (i32.const 25)) (i32.const -129))" % A,
     lambda a, b: shrs(a, 25, 32) ^ (-129 & M32), False),
    ("w_add18", I32, 16, 32, "(i32.add (i32.shr_s %s (i32.const 17)) (i32.shr_s %s (i32.const 17)))" % (A, B),
     lambda a, b: (shrs(a, 17, 32) + shrs(b, 17, 32)) & M32, True),
    ("w_add17", I32, 16, 32, "(i32.add (i32.shr_s %s (i32.const 16)) (i32.shr_s %s (i32.const 17)))" % (A, B),
     lambda a, b: (shrs(a, 16, 32) + shrs(b, 17, 32)) & M32, False),
]

# a loop's value: every edge hands the parameter one with the copies, itself
# among them; and one that grows a bit a trip, which no number of copies
# asked of it holds for
LOOPS = """  (func $loop_ok (export "loop_ok") (param $p i64) (param $n i64) (result i64)
    (local $m i64)
    (local.set $m (i64.extend16_s (local.get $p)))
    (block $done (loop $l
      (br_if $done (i64.eqz (local.get $n)))
      (local.set $m (select (local.get $m)
                            (i64.xor (local.get $m) (i64.shr_s (i64.mul (local.get $n) (i64.const 0x9e3779b97f4a7c15)) (i64.const 40)))
                            (i64.eqz (i64.and (local.get $n) (i64.const 2)))))
      (local.set $n (i64.sub (local.get $n) (i64.const 1)))
      (br $l)))
    (i64.extend32_s (local.get $m)))
  (func $loop_grow (export "loop_grow") (param $p i64) (param $n i64) (result i64)
    (local $m i64)
    (local.set $m (i64.extend16_s (local.get $p)))
    (block $done (loop $l
      (br_if $done (i64.eqz (local.get $n)))
      (local.set $m (i64.add (local.get $m) (local.get $m)))
      (local.set $n (i64.sub (local.get $n) (i64.const 1)))
      (br $l)))
    (i64.extend32_s (local.get $m)))
"""


def deep(a, q, n):
    x = sx(a, 32)
    for k in range(n):
        if s(q) > k:
            x = sx(q, 16)
    return sx(x, 32)


def loop_ok(p, n):
    m = sx(p, 16)
    while n:
        if n & 2:
            m ^= shrs(n * 0x9e3779b97f4a7c15, 40)
        n -= 1
    return sx(m, 32)


def loop_grow(p, n):
    m = sx(p, 16)
    for _ in range(n):
        m = (m + m) & M64
    return sx(m, 32)


V64 = [0, 1, 0x7f, 0x80, 0x7fff, 0x8000, 0xffff, 0x12345, 0x7fffffff, 0x80000000, 0xffffffff,
       0x1234567890, 0x7fffffffffff, 0x800000000000, 0x7fffffffffffffff, 0x8000000000000000,
       0xfedcba9876543210, M64, 0xffffffff7fffffff, 0xffff7fffffffffff, 0x0000800000000000]
V32 = [0, 1, 0x7f, 0x80, 0x7fff, 0x8000, 0x10000, 0x7fffff, 0x800000, 0x1000000, 0x7fffffff,
       0x80000000, 0xfedcba98, M32, 0xff7fffff, 0x00800000, 0x01ffffff, 0xfe000000]
MN = {8: "sxtb", 16: "sxth", 32: "sxtw"}


def main(path):
    funcs, shapes, runs, kept = [], [], [], []
    for name, params, bits, w, expr, model, gone in CASES:
        t = "i64" if w == 64 else "i32"
        if name.startswith("rewiden"):
            funcs.append('  (func $%s (export "%s") %s (result i64)\n    (i64.extend_i32_s (i32.wrap_i64 %s)))\n'
                         % (name, name, params, expr))
        else:
            funcs.append('  (func $%s (export "%s") %s (result %s)\n    (%s.extend%d_s %s))\n'
                         % (name, name, params, t, t, bits, expr))
        # the expression's own extensions of that kind stay either way
        inner = expr.count("extend%d_s" % bits) + (expr.count("extend_i32_s") if bits == 32 else 0)
        # (a sum of two widened i32s widens one of them in the add)
        inner = {"add_w": 1}.get(name, inner)
        shapes.append(r";; shape: %s \s%s\s=%d" % (name, MN[bits], inner + (0 if gone else 1)))
        vals = V32 if "i32" in params else V64
        top = (1 << (63 if vals is V64 else 31))
        cands = [(x, y) for i, x in enumerate(vals)
                 for y in (vals[(i * 7 + 3) % len(vals)], vals[(i * 5 + 11) % len(vals)])]
        # the extremes together, where a sum or a product is widest
        cands += [(x, y) for x in (top - 1, top) for y in (top - 1, top)]
        # a few each -- every run is a program started: ones the extension
        # changes the value for, where there are any, and ones it does not
        chg = [c for c in cands if sx(model(*c), bits, w) != model(*c) & ((1 << w) - 1)]
        same = [c for c in cands if c not in chg]
        pairs = chg[::max(1, len(chg) // 4)][:5] + same[::max(1, len(same) // 4)][:5]
        differs = bool(chg)
        for x, y in pairs:
            r = sx(model(x, y), bits, w)
            runs.append(";; run: %s %d %d => %s:%d" % (name, x, y, t, s(r, w)))
        # a kept extension must change some answer, or dropping it would show nowhere
        assert differs != gone, name
        kept.append((name, gone))
    for name, n in (("deep8", 8), ("deep9", 9)):
        body = "(i64.extend_i32_s (local.get $a))"
        for k in range(n):
            body = "(select (i64.extend16_s (local.get $q)) %s (i64.gt_s (local.get $q) (i64.const %d)))" % (body, k)
        funcs.append('  (func $%s (export "%s") (param $a i32) (param $q i64) (result i64)\n    (i64.extend32_s %s))\n'
                     % (name, name, body))
        # the widening is an sxtw of its own
        shapes.append(r";; shape: %s \ssxtw\s=%d" % (name, 1 if n == 8 else 2))
        for a, q in ((0x80000000, 0), (5, 3), (0xffffffff, 0x18000), (7, 20), (0x7fffffff, M64)):
            runs.append(";; run: %s %d %d => i64:%d" % (name, a, q, s(deep(a, q, n))))
    funcs.append(LOOPS)
    shapes += [r";; shape: loop_ok \ssxtw\s=0", r";; shape: loop_grow \ssxtw\s=1"]
    for p in (3, 0x7fff, 0x8000):
        for n in (0, 1, 3, 15, 16, 17, 40):
            runs.append(";; run: loop_ok %d %d => i64:%d" % (p, n, s(loop_ok(p, n))))
            runs.append(";; run: loop_grow %d %d => i64:%d" % (p, n, s(loop_grow(p, n))))
    data = "".join("\\%02x" % b for b in MEM)
    with open(path, "w") as f:
        f.write(HEAD + "\n".join(shapes) + "\n;;\n" + "\n".join(runs) +
                "\n\n(module\n  (memory 1)\n  (data (i32.const 0) \"%s\")\n" % data + "".join(funcs) + ")\n")
    print("%s: %d functions, %d runs" % (path, len(CASES) + 4, len(runs)))


HEAD = """;; How many of a value's top bits are copies of its sign (ir.c ir_sign_ge):
;; a sign extension of a value that is one already is dropped (opt.c's
;; I_EXT8S, I_EXT16S and I_EXT32S), which every function here ends in.
;; From test/gen_sign_bits.py, which works out every answer; a function
;; whose extension stays has arguments it changes the answer for.
;;
;;   shrs32 shrs31 shrs_of shrs_of15
;;                     a right shift has its amount and one, and what its
;;                     operand had beyond: gone, kept one short, twice
;;   shru33 shru32 andk andk32
;;                     zeros from there up
;;   ork ork31 orneg orneg31
;;                     a constant in range, either sign, and one a bit over
;;   ext8in16 ext16in8 ext32in16 ext16in32 ext8in8 ext16in16 ext32in32
;;                     an extension from fewer bits, from more, and from as
;;                     many
;;   widen widen16 widen15
;;                     a widened i32, and one with copies of its own
;;   ld16s ld16s8 ld32s ld32s16 ld64 ld32u ld16u
;;                     a signed load, a wider one, and unsigned ones
;;   wrap48 wrap47     a wrap of a value with 32 more
;;   shl16 shl17       a left shift takes its amount away
;;   and2 and1 and1b or2 xor1 sel2 sel1 sel1b
;;                     both operands of an and, or, xor or select
;;   add34 add33 add33b sub34 sub33
;;                     both of a sum or difference, with one more each
;;   add_w add_ld xor_ld
;;                     a sum of two widened i32s and of two signed loads,
;;                     each a copy short, and the loads' xor
;;   rewiden33 rewiden32
;;                     an i64 narrowed and widened again: itself with 33
;;   mul49 mul48 mul48b
;;                     both of a product, with half of the copies and the
;;                     width together each
;;   w_*               the same of an i32: shifts, loads, constants, a sum
;;   deep8 deep9       a widened i32 under eight selects, as deep as the
;;                     walk goes, and under nine: gone, kept
;;   loop_ok           a loop's value every edge hands one: gone
;;   loop_grow         one doubled each trip, which no count of copies
;;                     holds for: kept
;;
"""

if __name__ == "__main__":
    main(sys.argv[1])
