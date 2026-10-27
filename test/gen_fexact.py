"""test/cases_opt/trunc_exact.wat: a float computation of integers that
rounds nowhere, truncated, is integer arithmetic (opt.c fold_trunc_exact),
and its compares with constants are decided by its bounds (fcmp_exact).
Answers are wasm's float semantics, worked out here with Python floats
(numpy-free: f32 by struct round trips).

  python3 test/gen_fexact.py test/cases_opt/trunc_exact.wat
"""
import math, struct, sys

M32, M64 = (1 << 32) - 1, (1 << 64) - 1


def f32(x): return struct.unpack('<f', struct.pack('<f', x))[0]
def s32(v): v &= M32; return v - (1 << 32) if v >> 31 else v
def s64(v): v &= M64; return v - (1 << 64) if v >> 63 else v
def u32(v): return v & M32


def trunc_sat(x, bits, sgn):
    if x != x: return 0
    lo, hi = (-(1 << (bits - 1)), (1 << (bits - 1)) - 1) if sgn else (0, (1 << bits) - 1)
    if math.isinf(x): return hi if x > 0 else lo
    return max(lo, min(hi, math.trunc(x)))


# name: (result type, params, wat body, python function of the params, folds?)
CASES = []


def case(name, rt, params, body, fn, folds):
    CASES.append((name, rt, params, body, fn, folds))


# gobench/bigstruct's int(float64(r & 3) + 5 + 9)
case('bigstruct', 'i32', ['i32'],
     '(i32.trunc_sat_f64_s (f64.add (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.const 9)))',
     lambda x: trunc_sat(float(u32(x) & 3) + 5.0 + 9.0, 32, True), True)
# subtract, multiply, negate; the trapping truncation, never out of range here
case('mixed', 'i32', ['i32', 'i32'],
     '(i32.trunc_f64_s (f64.mul (f64.sub (f64.convert_i32_s (i32.and (local.get 0) (i32.const 255))) (f64.const 100))'
     ' (f64.neg (f64.convert_i32_u (i32.and (local.get 1) (i32.const 15))))))',
     lambda x, y: math.trunc((float(u32(x) & 255) - 100.0) * -float(u32(y) & 15)), True)
# a constant first
case('constfirst', 'i32', ['i32'],
     '(i32.trunc_sat_f64_s (f64.sub (f64.const 1000) (f64.convert_i32_u (i32.and (local.get 0) (i32.const 4095)))))',
     lambda x: trunc_sat(1000.0 - float(u32(x) & 4095), 32, True), True)
# i64 from an i64 below 2^52, unsigned
case('wide52', 'i64', ['i64'],
     '(i64.trunc_sat_f64_u (f64.add (f64.convert_i64_u (i64.shr_u (local.get 0) (i64.const 12))) (f64.const 1)))',
     lambda z: trunc_sat(float((z & M64) >> 12) + 1.0, 64, False), True)
# an i32 widened, either way, into an i64
case('wideu', 'i64', ['i32'],
     '(i64.trunc_sat_f64_s (f64.add (f64.convert_i32_u (local.get 0)) (f64.const 1)))',
     lambda x: trunc_sat(float(u32(x)) + 1.0, 64, True), True)
case('wides', 'i64', ['i32'],
     '(i64.trunc_sat_f64_s (f64.add (f64.convert_i32_s (local.get 0)) (f64.const 1)))',
     lambda x: trunc_sat(float(s32(x)) + 1.0, 64, True), True)
# an i64 narrowed into an i32
case('narrow', 'i32', ['i64'],
     '(i32.trunc_sat_f64_s (f64.add (f64.convert_i64_u (i64.and (local.get 0) (i64.const 65535))) (f64.const 2)))',
     lambda z: trunc_sat(float(z & 65535) + 2.0, 32, True), True)
# f32 below 2^24
case('small32', 'i32', ['i32'],
     '(i32.trunc_sat_f32_s (f32.add (f32.convert_i32_u (i32.and (local.get 0) (i32.const 65535))) (f32.const 7)))',
     lambda x: trunc_sat(f32(f32(float(u32(x) & 65535)) + 7.0), 32, True), True)
# negative, signed result
case('negs', 'i32', ['i32'],
     '(i32.trunc_sat_f64_s (f64.neg (f64.convert_i32_u (i32.and (local.get 0) (i32.const 255)))))',
     lambda x: trunc_sat(-float(u32(x) & 255), 32, True), True)

# -- not folded --
# f32 past 2^24: 0xffffff + 2 rounds
case('badf32', 'i32', ['i32'],
     '(i32.trunc_sat_f32_s (f32.add (f32.convert_i32_u (i32.and (local.get 0) (i32.const 0xffffff))) (f32.const 2)))',
     lambda x: trunc_sat(f32(f32(float(u32(x) & 0xffffff)) + 2.0), 32, True), False)
# a constant with a fraction
case('half', 'i32', ['i32'],
     '(i32.trunc_sat_f64_s (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 255))) (f64.const 0.5)))',
     lambda x: trunc_sat(float(u32(x) & 255) + 0.5, 32, True), False)
# an i64 of any size
case('anyi64', 'i64', ['i64'],
     '(i64.trunc_sat_f64_s (f64.add (f64.convert_i64_u (local.get 0)) (f64.const 1)))',
     lambda z: trunc_sat(float(z & M64) + 1.0, 64, True), False)
# past the i32 the truncation makes: it saturates
case('sat31', 'i32', ['i32'],
     '(i32.trunc_sat_f64_s (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 0x7fffffff))) (f64.const 1)))',
     lambda x: trunc_sat(float(u32(x) & 0x7fffffff) + 1.0, 32, True), False)
# below 0 into an unsigned result, by a convert_s, a subtraction, a product, a negation, by one
case('negu', 'i32', ['i32'],
     '(i32.trunc_sat_f64_u (f64.add (f64.convert_i32_s (local.get 0)) (f64.const 1)))',
     lambda x: trunc_sat(float(s32(x)) + 1.0, 32, False), False)
# a convert_s of any i32, which may be negative, into an unsigned i64 the
# positive half would fit
case('negu64', 'i64', ['i32'],
     '(i64.trunc_sat_f64_u (f64.add (f64.convert_i32_s (local.get 0)) (f64.const 1)))',
     lambda x: trunc_sat(float(s32(x)) + 1.0, 64, False), False)
case('subu', 'i32', ['i32', 'i32'],
     '(i32.trunc_sat_f64_u (f64.sub (f64.convert_i32_u (i32.and (local.get 0) (i32.const 255))) (f64.convert_i32_u (i32.and (local.get 1) (i32.const 255)))))',
     lambda x, y: trunc_sat(float(u32(x) & 255) - float(u32(y) & 255), 32, False), False)
case('mulu', 'i32', ['i32', 'i32'],
     '(i32.trunc_sat_f64_u (f64.mul (f64.sub (f64.convert_i32_u (i32.and (local.get 0) (i32.const 255))) (f64.const 128)) (f64.convert_i32_u (i32.and (local.get 1) (i32.const 255)))))',
     lambda x, y: trunc_sat((float(u32(x) & 255) - 128.0) * float(u32(y) & 255), 32, False), False)
case('negx', 'i32', ['i32'],
     '(i32.trunc_sat_f64_u (f64.neg (f64.convert_i32_u (i32.and (local.get 0) (i32.const 255)))))',
     lambda x: trunc_sat(-float(u32(x) & 255), 32, False), False)
case('subone', 'i32', ['i32'],
     '(i32.trunc_sat_f64_u (f64.sub (f64.convert_i32_u (i32.and (local.get 0) (i32.const 255))) (f64.const 1)))',
     lambda x: trunc_sat(float(u32(x) & 255) - 1.0, 32, False), False)
# a product whose bound is 2^53 + 1, which rounds to 2^53 in double
case('edge53', 'i64', ['i32'],
     '(i64.trunc_sat_f64_s (f64.mul (f64.convert_i32_u (i32.and (local.get 0) (i32.const 321))) (f64.const 28059810762433)))',
     lambda x: trunc_sat(float(u32(x) & 321) * 28059810762433.0, 64, True), False)
# a division
case('div', 'i32', ['i32'],
     '(i32.trunc_sat_f64_s (f64.div (f64.convert_i32_u (i32.and (local.get 0) (i32.const 255))) (f64.const 2)))',
     lambda x: trunc_sat(float(u32(x) & 255) / 2.0, 32, True), False)

# -- compares the bounds decide: (x & 3) + 5 is 5 to 8 --
E = '(f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5))'
e = lambda x: float(u32(x) & 3) + 5.0
CMPS = []


def cmp(name, body, fn, folds):
    # plus a constant of its own, so that no two folded compares are the same code
    k = 10 * (len(CMPS) + 1)
    CMPS.append(name)
    case(name, 'i32', ['i32'], '(i32.add %s (i32.const %d))' % (body, k), lambda x: int(fn(x)) + k, folds)


cmp('lt9', '(f64.lt %s (f64.const 9))' % E, lambda x: e(x) < 9, True)
cmp('lt8', '(f64.lt %s (f64.const 8))' % E, lambda x: e(x) < 8, False)
cmp('le8', '(f64.le %s (f64.const 8))' % E, lambda x: e(x) <= 8, True)
cmp('le5', '(f64.le %s (f64.const 5))' % E, lambda x: e(x) <= 5, False)
cmp('le7', '(f64.le %s (f64.const 7))' % E, lambda x: e(x) <= 7, False)
cmp('gt4', '(f64.gt %s (f64.const 4))' % E, lambda x: e(x) > 4, True)
cmp('gt8', '(f64.gt %s (f64.const 8))' % E, lambda x: e(x) > 8, True)
cmp('gt5', '(f64.gt %s (f64.const 5))' % E, lambda x: e(x) > 5, False)
cmp('ge5', '(f64.ge %s (f64.const 5))' % E, lambda x: e(x) >= 5, True)
cmp('ge8', '(f64.ge %s (f64.const 8))' % E, lambda x: e(x) >= 8, False)
cmp('ge6', '(f64.ge %s (f64.const 6))' % E, lambda x: e(x) >= 6, False)
cmp('ltlow', '(f64.lt %s (f64.const 5))' % E, lambda x: e(x) < 5, True)
cmp('gehigh', '(f64.ge %s (f64.const 9))' % E, lambda x: e(x) >= 9, True)
cmp('eqfrac', '(f64.eq %s (f64.const 6.5))' % E, lambda x: e(x) == 6.5, True)
cmp('eqout', '(f64.eq %s (f64.const 20))' % E, lambda x: e(x) == 20, True)
cmp('nein', '(f64.ne %s (f64.const 6))' % E, lambda x: e(x) != 6, False)
cmp('neout', '(f64.ne %s (f64.const 4))' % E, lambda x: e(x) != 4, True)
# a single value: (x & 1) * 0 is 0 whatever x is
cmp('eqpoint', '(f64.eq (f64.mul (f64.convert_i32_u (i32.and (local.get 0) (i32.const 1))) (f64.const 0)) (f64.const 0))',
    lambda x: float(u32(x) & 1) * 0.0 == 0.0, True)
# the constant first
cmp('swapgt', '(f64.gt (f64.const 9) %s)' % E, lambda x: 9 > e(x), True)
cmp('swaple', '(f64.le (f64.const 6) %s)' % E, lambda x: 6 <= e(x), False)
cmp('swaplt', '(f64.lt (f64.const 4) %s)' % E, lambda x: 4 < e(x), True)
cmp('swapge', '(f64.ge (f64.const 4) %s)' % E, lambda x: 4 >= e(x), True)
# NaN, and itself
cmp('nanlt', '(f64.lt %s (f64.const nan))' % E, lambda x: False, True)
cmp('nanne', '(f64.ne %s (f64.const nan))' % E, lambda x: True, True)
cmp('selfeq', '(f64.eq %s %s)' % (E, E), lambda x: True, True)
cmp('selfge', '(f64.ge %s %s)' % (E, E), lambda x: True, True)
cmp('selfgt', '(f64.gt %s %s)' % (E, E), lambda x: False, True)
# any float, which may be NaN: x <= x is not decided
R = '(f64.reinterpret_i64 (i64.extend_i32_u (local.get 0)))'
cmp('selfle', '(f64.le %s %s)' % (R, R), lambda x: True, False)
# f32, and a computation that is not exact
cmp('f32lt', '(f32.lt (f32.add (f32.convert_i32_u (i32.and (local.get 0) (i32.const 255))) (f32.const 1)) (f32.const 300))',
    lambda x: f32(float(u32(x) & 255) + 1.0) < 300, True)
cmp('inexact', '(f64.lt (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5.5)) (f64.const 9))',
    lambda x: float(u32(x) & 3) + 5.5 < 9, False)

V32 = [0, 1, 2, 3, 5, 100, 255, 256, 321, 4095, 65535, 0xffffff, 0x7fffffff, 0x80000000, 0xffffffff, 0xfffffffb]
V64 = [0, 1, 4096, (1 << 52) - 1, (1 << 53) + 1, (1 << 63), M64, 0x123456789abcdef]
PAIRS = [(0, 0), (1, 5), (5, 1), (255, 15), (0, 255), (128, 255), (200, 7), (0xffffffff, 0xffffffff)]

lines, funcs = [], []
for name, rt, params, body, fn, folds in CASES:
    if name in CMPS:
        lines.append(';; shape: %s fcmp=%d' % (name, 0 if folds else 1))
    elif folds:
        lines.append(';; shape: %s fcvtz=0' % name)
        lines.append(';; shape: %s cvtf=0' % name)
    else:
        lines.append(';; shape: %s fcvtz=1' % name)
for name, rt, params, body, fn, folds in CASES:
    fmt = (lambda v: s32(v)) if rt == 'i32' else (lambda v: s64(v))
    if len(params) == 2:
        args = PAIRS
    elif params[0] == 'i64':
        args = [(z,) for z in V64]
    else:
        args = [(x,) for x in V32]
    for a in args:
        shown = ' '.join(str(s32(v) if p == 'i32' else s64(v)) for v, p in zip(a, params))
        lines.append(';; run: %s %s => %s:%d' % (name, shown, rt, fmt(fn(*a))))
    funcs.append('  (func $%s (export "%s") (param %s) (result %s)\n    %s)'
                 % (name, name, ' '.join(params), rt, body))

HDR = ''';; A float computation that rounds nowhere -- integers converted, integral
;; constants, and sums, differences, products and negations of them, every
;; value inside 2^53 (2^24 for f32) -- truncated to an integer type that
;; holds its every value, is that integer arithmetic (opt.c fold_trunc_exact;
;; gobench/bigstruct's int(float64(r & 3) + 5 + 9)): saturating or trapping,
;; i32 or i64 either way, a constant either side, f32.  Not past 2^24 in f32,
;; with a fraction, from an i64 of any size, past what the truncation holds
;; (it saturates), below zero into an unsigned result by each operation, a
;; product whose bound rounds to 2^53 in double, or a division.  And a
;; compare of such a computation with a constant, either side, or with
;; itself, is what its bounds say where they decide it (opt.c fcmp_exact):
;; each compare on and past each bound, a fraction or a value outside never
;; equal, a single value, NaN, f32; not where the bounds straddle the
;; constant, the value is not exact, or itself is any float.  Answers are
;; wasm's float semantics, worked out in Python by test/gen_fexact.py, which
;; writes this file: a change goes there.
;;
'''
open(sys.argv[1], 'w').write(HDR + '\n'.join(lines) + '\n\n(module\n' + '\n'.join(funcs) + '\n)\n')
