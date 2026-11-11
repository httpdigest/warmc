"""test/cases_opt/wide_mul.wat: a 64-bit multiply of two 32-bit values
extended alike is umull or smull, and a sum with one umaddl (isel.c, ahead
of the folds into adds; isel_ops.inc's I_MUL and the add of a folded
multiply).  The module's header says what each function is; every answer is
worked out here.

  python3 test/gen_wide_mul.py test/cases_opt/wide_mul.wat
"""
import sys

M64 = (1 << 64) - 1
M32 = (1 << 32) - 1
K = 2654435761           # under 2^32, over 2^31: an unsigned extension leaves it
P = 1000003              # either extension leaves it
B = 3000000019           # under 2^32, over 2^31
W = 4294967301           # over 2^32: no extension leaves it


def s64(v):
    v &= M64
    return v - (1 << 64) if v >> 63 else v


def zx(v):
    return v & M32


def sx(v):
    v &= M32
    return v - (1 << 32) if v >> 31 else v


# the memory's first bytes, which the loads read: every byte's top bit set
# somewhere
MEM = bytes((i * 37 + 0x85) & 0xff for i in range(64))


def ld(p, n, signed):
    return int.from_bytes(MEM[p:p + n], "little", signed=signed)


def sx16(v):
    v &= 0xffff
    return v - (1 << 16) if v >> 15 else v


# name: (parameters, body, the answer, the shapes: {pattern: count})
U, S = "i64.extend_i32_u", "i64.extend_i32_s"
UMULL, SMULL, MUL = r"\sumull\s", r"\ssmull\s", r"\smul\s"
EXT = r"\s(sxtw\s|mov\s+w\d+, w\d+$)"
NO_EXT = {EXT: 0}

FUNCS = [
    # the multiply alone
    ("umul", "i32 i32", "(i64.mul (%s (local.get 0)) (%s (local.get 1)))" % (U, U),
     lambda a, b: zx(a) * zx(b), {UMULL: 1, MUL: 0, **NO_EXT}),
    ("smul", "i32 i32", "(i64.mul (%s (local.get 0)) (%s (local.get 1)))" % (S, S),
     lambda a, b: sx(a) * sx(b), {SMULL: 1, MUL: 0, **NO_EXT}),
    ("ext32", "i64 i64", "(i64.mul (i64.extend32_s (local.get 1)) (i64.extend32_s (local.get 0)))",
     lambda a, b: sx(a) * sx(b), {SMULL: 1, MUL: 0, **NO_EXT}),
    ("smix", "i64 i32 i64", "(i64.mul (%s (local.get 1)) (i64.extend32_s (local.get 2)))" % S,
     lambda p, a, b: sx(a) * sx(b), {SMULL: 1, MUL: 0, **NO_EXT}),
    ("mixed", "i32 i32", "(i64.mul (%s (local.get 0)) (%s (local.get 1)))" % (U, S),
     lambda a, b: zx(a) * sx(b), {UMULL: 0, SMULL: 0, MUL: 1}),
    ("mixed2", "i32 i32", "(i64.mul (%s (local.get 0)) (%s (local.get 1)))" % (S, U),
     lambda a, b: sx(a) * zx(b), {UMULL: 0, SMULL: 0, MUL: 1}),
    ("half", "i32 i64", "(i64.mul (%s (local.get 0)) (local.get 1))" % U,
     lambda a, b: zx(a) * b, {UMULL: 0, SMULL: 0, MUL: 1}),
    ("half2", "i64 i32", "(i64.mul (local.get 0) (%s (local.get 1)))" % S,
     lambda a, b: a * sx(b), {UMULL: 0, SMULL: 0, MUL: 1}),
    # a constant for one of the two
    ("umul_k", "i32", "(i64.mul (%s (local.get 0)) (i64.const %d))" % (U, K),
     lambda a: zx(a) * K, {UMULL: 1, MUL: 0, **NO_EXT}),
    ("umul_p", "i32", "(i64.mul (%s (local.get 0)) (i64.const %d))" % (U, P),
     lambda a: zx(a) * P, {UMULL: 1, MUL: 0, **NO_EXT}),
    ("k_umul", "i32", "(i64.mul (i64.const %d) (%s (local.get 0)))" % (K + 2, U),
     lambda a: zx(a) * (K + 2), {UMULL: 1, MUL: 0, **NO_EXT}),
    ("umul_max", "i32", "(i64.mul (%s (local.get 0)) (i64.const %d))" % (U, M32 - 4),
     lambda a: zx(a) * (M32 - 4), {UMULL: 1, MUL: 0, **NO_EXT}),
    ("smul_p", "i32", "(i64.mul (%s (local.get 0)) (i64.const %d))" % (S, P),
     lambda a: sx(a) * P, {SMULL: 1, MUL: 0, **NO_EXT}),
    ("smul_n", "i32", "(i64.mul (%s (local.get 0)) (i64.const %d))" % (S, -P),
     lambda a: sx(a) * -P, {SMULL: 1, MUL: 0, **NO_EXT}),
    ("n_smul", "i32", "(i64.mul (i64.const %d) (%s (local.get 0)))" % (-P - 2, S),
     lambda a: sx(a) * (-P - 2), {SMULL: 1, MUL: 0, **NO_EXT}),
    ("smul_min", "i32", "(i64.mul (%s (local.get 0)) (i64.const %d))" % (S, -(1 << 31) + 5),
     lambda a: sx(a) * (-(1 << 31) + 5), {SMULL: 1, MUL: 0, **NO_EXT}),
    ("smul_top", "i32", "(i64.mul (%s (local.get 0)) (i64.const %d))" % (S, (1 << 31) - 5),
     lambda a: sx(a) * ((1 << 31) - 5), {SMULL: 1, MUL: 0, **NO_EXT}),
    # constants the extension would change: the multiply as it was
    ("u_wide", "i32", "(i64.mul (%s (local.get 0)) (i64.const %d))" % (U, W),
     lambda a: zx(a) * W, {UMULL: 0, SMULL: 0, MUL: 1}),
    ("u_neg", "i32", "(i64.mul (%s (local.get 0)) (i64.const %d))" % (U, -P),
     lambda a: zx(a) * -P, {UMULL: 0, SMULL: 0, MUL: 1}),
    ("s_big", "i32", "(i64.mul (%s (local.get 0)) (i64.const %d))" % (S, B),
     lambda a: sx(a) * B, {UMULL: 0, SMULL: 0, MUL: 1}),
    ("s_negbig", "i32", "(i64.mul (%s (local.get 0)) (i64.const %d))" % (S, -B),
     lambda a: sx(a) * -B, {UMULL: 0, SMULL: 0, MUL: 1}),
    ("s_over", "i32", "(i64.mul (%s (local.get 0)) (i64.const %d))" % (S, (1 << 31) + 5),
     lambda a: sx(a) * ((1 << 31) + 5), {UMULL: 0, SMULL: 0, MUL: 1}),
    ("s_under", "i32", "(i64.mul (%s (local.get 0)) (i64.const %d))" % (S, -(1 << 31) - 5),
     lambda a: sx(a) * (-(1 << 31) - 5), {UMULL: 0, SMULL: 0, MUL: 1}),
    ("u_over", "i32", "(i64.mul (%s (local.get 0)) (i64.const %d))" % (U, (1 << 32) + 11),
     lambda a: zx(a) * ((1 << 32) + 11), {UMULL: 0, SMULL: 0, MUL: 1}),
    ("s_wide", "i32", "(i64.mul (%s (local.get 0)) (i64.const %d))" % (S, W),
     lambda a: sx(a) * W, {UMULL: 0, SMULL: 0, MUL: 1}),
    # an extension read elsewhere too stays, and the multiply reads its
    # register's low half; the other's is dropped
    ("twice", "i32 i32",
     "(local $e i64) (local.set $e (%s (local.get 0)))\n"
     "    (i64.xor (i64.mul (local.get $e) (%s (local.get 1))) (local.get $e))" % (U, U),
     lambda a, b: (zx(a) * zx(b)) ^ zx(a), {UMULL: 1, MUL: 0, EXT: 1}),
    ("twice_s", "i32 i32",
     "(local $e i64) (local.set $e (%s (local.get 1)))\n"
     "    (i64.xor (i64.mul (%s (local.get 0)) (local.get $e)) (local.get $e))" % (S, S),
     lambda a, b: (sx(a) * sx(b)) ^ (sx(b) & M64), {SMULL: 1, MUL: 0, EXT: 1}),
    ("twice_mix", "i32 i32",
     "(local $e i64) (local.set $e (%s (local.get 1)))\n"
     "    (i64.xor (i64.mul (%s (local.get 0)) (local.get $e)) (local.get $e))" % (S, U),
     lambda a, b: (zx(a) * sx(b)) ^ (sx(b) & M64), {SMULL: 0, UMULL: 0, MUL: 1, EXT: 2}),
    ("both_twice", "i32 i32",
     "(local $e i64) (local $g i64) (local.set $e (%s (local.get 0))) (local.set $g (%s (local.get 1)))\n"
     "    (i64.xor (i64.mul (local.get $e) (local.get $g)) (i64.sub (local.get $e) (local.get $g)))" % (U, U),
     lambda a, b: (zx(a) * zx(b)) ^ ((zx(a) - zx(b)) & M64), {UMULL: 0, MUL: 1}),
    # a square: the one extension, read twice by the multiply and by nothing else
    ("square", "i32",
     "(local $e i64) (local.set $e (%s (local.get 0)))\n"
     "    (i64.mul (local.get $e) (local.get $e))" % U,
     lambda a: zx(a) * zx(a), {UMULL: 1, MUL: 0, **NO_EXT}),
    ("ssquare", "i32",
     "(local $e i64) (local.set $e (%s (local.get 0)))\n"
     "    (i64.mul (local.get $e) (local.get $e))" % S,
     lambda a: sx(a) * sx(a), {SMULL: 1, MUL: 0, **NO_EXT}),
    ("square3", "i32",
     "(local $e i64) (local.set $e (%s (local.get 0)))\n"
     "    (i64.xor (i64.mul (local.get $e) (local.get $e)) (local.get $e))" % U,
     lambda a: (zx(a) * zx(a)) ^ zx(a), {UMULL: 0, MUL: 1}),
    # a load of 32 bits or fewer is its low half extended as it was loaded
    ("ld_u", "i32 i32", "(i64.mul (i64.load32_u (local.get 0)) (%s (local.get 1)))" % U,
     lambda p, b: ld(p, 4, False) * zx(b), {UMULL: 1, MUL: 0, EXT: 0}),
    ("ld_s", "i32 i32", "(i64.mul (%s (local.get 1)) (i64.load32_s (local.get 0)))" % S,
     lambda p, b: ld(p, 4, True) * sx(b), {SMULL: 1, MUL: 0, EXT: 0}),
    ("ld8_u", "i32 i32", "(i64.mul (i64.load8_u (local.get 0)) (%s (local.get 1)))" % U,
     lambda p, b: ld(p, 1, False) * zx(b), {UMULL: 1, MUL: 0, EXT: 0}),
    ("ld8_us", "i32 i32", "(i64.mul (i64.load8_u (local.get 0)) (%s (local.get 1)))" % S,
     lambda p, b: ld(p, 1, False) * sx(b), {SMULL: 1, MUL: 0, EXT: 0}),
    ("ld16_us", "i32 i32", "(i64.mul (i64.load16_u (local.get 0)) (%s (local.get 1)))" % S,
     lambda p, b: ld(p, 2, False) * sx(b), {SMULL: 1, MUL: 0, EXT: 0}),
    ("ld16_s", "i32 i32", "(i64.mul (i64.load16_s (local.get 0)) (%s (local.get 1)))" % S,
     lambda p, b: ld(p, 2, True) * sx(b), {SMULL: 1, MUL: 0, EXT: 0}),
    ("ld16_su", "i32 i32", "(i64.mul (i64.load16_s (local.get 0)) (%s (local.get 1)))" % U,
     lambda p, b: ld(p, 2, True) * zx(b), {SMULL: 0, UMULL: 0, MUL: 1}),
    ("ld8_su", "i32 i32", "(i64.mul (i64.load8_s (local.get 0)) (%s (local.get 1)))" % U,
     lambda p, b: ld(p, 1, True) * zx(b), {SMULL: 0, UMULL: 0, MUL: 1}),
    ("ld32_us", "i32 i32", "(i64.mul (i64.load32_u (local.get 0)) (%s (local.get 1)))" % S,
     lambda p, b: ld(p, 4, False) * sx(b), {SMULL: 0, UMULL: 0, MUL: 1}),
    ("ld32_su", "i32 i32", "(i64.mul (i64.load32_s (local.get 0)) (%s (local.get 1)))" % U,
     lambda p, b: ld(p, 4, True) * zx(b), {SMULL: 0, UMULL: 0, MUL: 1}),
    ("ld64", "i32 i32", "(i64.mul (i64.load (local.get 0)) (%s (local.get 1)))" % U,
     lambda p, b: ld(p, 8, False) * zx(b), {SMULL: 0, UMULL: 0, MUL: 1}),
    ("ld_ld", "i32 i32", "(i64.mul (i64.load32_u (local.get 0)) (i64.load32_u (local.get 1)))",
     lambda p, q: ld(p, 4, False) * ld(q, 4, False), {SMULL: 0, UMULL: 0, MUL: 1}),
    ("ld_k", "i32", "(i64.mul (i64.load32_u (local.get 0)) (i64.const %d))" % K,
     lambda p: ld(p, 4, False) * K, {SMULL: 0, UMULL: 0, MUL: 1}),
    # any value that is its low half extended the way the dropped extension
    # extends: zeros known above (ir_known_bits), the sign's copies
    # (ir_sign_ge)
    ("and_u", "i64 i32", "(i64.mul (i64.and (local.get 0) (i64.const 0xfffffff)) (%s (local.get 1)))" % U,
     lambda p, b: (p & 0xfffffff) * zx(b), {UMULL: 1, MUL: 0, EXT: 0}),
    ("shru_u", "i64 i32", "(i64.mul (%s (local.get 1)) (i64.shr_u (local.get 0) (i64.const 32)))" % U,
     lambda p, b: (p >> 32) * zx(b), {UMULL: 1, MUL: 0, EXT: 0}),
    ("shru31", "i64 i32", "(i64.mul (%s (local.get 1)) (i64.shr_u (local.get 0) (i64.const 31)))" % U,
     lambda p, b: (p >> 31) * zx(b), {UMULL: 0, SMULL: 0, MUL: 1}),
    ("shrs_s", "i64 i32", "(i64.mul (i64.shr_s (local.get 0) (i64.const 32)) (%s (local.get 1)))" % S,
     lambda p, b: (s64(p) >> 32) * sx(b), {SMULL: 1, MUL: 0, EXT: 0}),
    ("shrs31", "i64 i32", "(i64.mul (i64.shr_s (local.get 0) (i64.const 31)) (%s (local.get 1)))" % S,
     lambda p, b: (s64(p) >> 31) * sx(b), {UMULL: 0, SMULL: 0, MUL: 1}),
    ("shrs_u", "i64 i32", "(i64.mul (i64.shr_s (local.get 0) (i64.const 32)) (%s (local.get 1)))" % U,
     lambda p, b: (s64(p) >> 32) * zx(b), {UMULL: 0, SMULL: 0, MUL: 1}),
    ("ext16_s", "i64 i32", "(i64.mul (i64.extend16_s (local.get 0)) (%s (local.get 1)))" % S,
     lambda p, b: sx16(p) * sx(b), {SMULL: 1, MUL: 0, EXT: 0}),
    ("ext16_u", "i64 i32", "(i64.mul (i64.extend16_s (local.get 0)) (%s (local.get 1)))" % U,
     lambda p, b: sx16(p) * zx(b), {UMULL: 0, SMULL: 0, MUL: 1}),
    ("and_s", "i64 i32", "(i64.mul (i64.and (local.get 0) (i64.const 0x7fffffff)) (%s (local.get 1)))" % S,
     lambda p, b: (p & 0x7fffffff) * sx(b), {SMULL: 1, UMULL: 0, MUL: 0, EXT: 0}),
    ("and_s32", "i64 i32", "(i64.mul (i64.and (local.get 0) (i64.const 0xfffffffe)) (%s (local.get 1)))" % S,
     lambda p, b: (p & 0xfffffffe) * sx(b), {UMULL: 0, SMULL: 0, MUL: 1}),
    ("both_ways", "i64 i32",
     "(i64.mul (i64.and (local.get 0) (i64.const 0xfff)) (%s (i32.shr_u (local.get 1) (i32.const 1))))" % U,
     lambda p, b: (p & 0xfff) * (zx(b) >> 1), {UMULL: 1, SMULL: 0, MUL: 0, EXT: 0}),
    ("sum16", "i64 i64 i32",
     "(i64.mul (i64.add (i64.extend16_s (local.get 0)) (i64.extend16_s (local.get 1))) (%s (local.get 2)))" % S,
     lambda p, q, b: (sx16(p) + sx16(q)) * sx(b), {SMULL: 1, MUL: 0, EXT: 0}),
    ("sum32", "i64 i64 i32",
     "(i64.mul (i64.add (i64.extend32_s (local.get 0)) (i64.extend32_s (local.get 1))) (%s (local.get 2)))" % S,
     lambda p, q, b: (sx(p) + sx(q)) * sx(b), {UMULL: 0, SMULL: 0, MUL: 1}),
    # a sum or a difference with the product
    ("umadd", "i32 i32 i64",
     "(i64.add (local.get 2) (i64.mul (%s (local.get 0)) (%s (local.get 1))))" % (U, U),
     lambda a, b, c: c + zx(a) * zx(b), {r"\sumaddl\s": 1, UMULL: 0, MUL: 0, r"\sadd\s": 0, **NO_EXT}),
    ("smadd", "i32 i32 i64",
     "(i64.add (local.get 2) (i64.mul (%s (local.get 0)) (%s (local.get 1))))" % (S, S),
     lambda a, b, c: c + sx(a) * sx(b), {r"\ssmaddl\s": 1, SMULL: 0, MUL: 0, r"\sadd\s": 0, **NO_EXT}),
    ("umadd_l", "i32 i32 i64",
     "(i64.add (i64.mul (%s (local.get 1)) (%s (local.get 0))) (local.get 2))" % (U, U),
     lambda a, b, c: c + zx(a) * zx(b), {r"\sumaddl\s": 1, UMULL: 0, MUL: 0, **NO_EXT}),
    ("umsub", "i32 i32 i64",
     "(i64.sub (local.get 2) (i64.mul (%s (local.get 0)) (%s (local.get 1))))" % (U, U),
     lambda a, b, c: c - zx(a) * zx(b), {r"\sumsubl\s": 1, UMULL: 0, MUL: 0, r"\ssub\s": 0, **NO_EXT}),
    ("smsub", "i32 i32 i64",
     "(i64.sub (local.get 2) (i64.mul (%s (local.get 0)) (%s (local.get 1))))" % (S, S),
     lambda a, b, c: c - sx(a) * sx(b), {r"\ssmsubl\s": 1, SMULL: 0, MUL: 0, r"\ssub\s": 0, **NO_EXT}),
    ("sub_l", "i32 i32 i64",
     "(i64.sub (i64.mul (%s (local.get 0)) (%s (local.get 1))) (local.get 2))" % (U, U),
     lambda a, b, c: zx(a) * zx(b) - c, {UMULL: 1, r"\ssub\s": 1, r"\sumsubl\s": 0, **NO_EXT}),
    ("hash", "i32 i64",
     "(i64.add (local.get 1) (i64.mul (%s (local.get 0)) (i64.const %d)))" % (U, K),
     lambda a, c: c + zx(a) * K, {r"\sumaddl\s": 1, UMULL: 0, MUL: 0, **NO_EXT}),
    ("shash", "i32 i64",
     "(i64.sub (local.get 1) (i64.mul (%s (local.get 0)) (i64.const %d)))" % (S, -P),
     lambda a, c: c - sx(a) * -P, {r"\ssmsubl\s": 1, SMULL: 0, MUL: 0, **NO_EXT}),
    ("sq_add", "i32 i64",
     "(local $e i64) (local.set $e (%s (local.get 0)))\n"
     "    (i64.add (local.get 1) (i64.mul (local.get $e) (local.get $e)))" % S,
     lambda a, c: c + sx(a) * sx(a), {r"\ssmaddl\s": 1, SMULL: 0, MUL: 0, **NO_EXT}),
]

# an extension made in a block before the multiply's, which reads nothing
# else of it: dropped as one beside the multiply is
FAR = """  (func $far (export "far") (param $a i32) (param $b i32) (param $c i32) (result i64)
    (local $e i64)
    (local.set $e (i64.extend_i32_u (local.get $a)))
    (if (local.get $c) (then (i64.store (i32.const 64) (i64.const 9))))
    (i64.add (i64.load (i32.const 64)) (i64.mul (local.get $e) (i64.extend_i32_u (local.get $b)))))
  (func $far_s (export "far_s") (param $a i32) (param $b i32) (param $c i32) (result i64)
    (local $e i64)
    (local.set $e (i64.extend_i32_s (local.get $a)))
    (if (local.get $c) (then (i64.store (i32.const 72) (i64.const 9))))
    (i64.sub (i64.load (i32.const 72)) (i64.mul (i64.extend_i32_s (local.get $b)) (local.get $e))))
"""

# the extension made before the loop its multiply is in, which every copy
# of the loop reads: it stays there, and the multiply reads its register's
# low half and drops the other's
OTHER = """  (func $other (export "other") (param $a i32) (param $n i32) (result i64)
    (local $e i64) (local $x i32) (local $acc i64)
    (local.set $e (i64.extend_i32_s (local.get $a)))
    (local.set $x (local.get $a))
    (block $done (loop $l
      (br_if $done (i32.eqz (local.get $n)))
      (local.set $x (i32.add (i32.mul (local.get $x) (i32.const 1664525)) (i32.const 1013904223)))
      (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7))
        (i64.mul (local.get $e) (i64.extend_i32_s (local.get $x)))))
      (local.set $n (i32.sub (local.get $n) (i32.const 1)))
      (br $l)))
    (local.get $acc))
"""


def other(a, n):
    x, acc = zx(a), 0
    for _ in range(zx(n)):
        x = (x * 1664525 + 1013904223) & M32
        acc = rotl(acc, 7) ^ ((sx(a) * sx(x)) & M64)
    return acc


# a hash's loop: two 32-bit states a trip, their product into the sum
N = 37
DOT = """  (func $dot (export "dot") (param $n i32) (param $seed i32) (result i64)
    (local $acc i64) (local $x i32) (local $y i32)
    (local.set $x (local.get $seed))
    (local.set $y (i32.xor (local.get $seed) (i32.const 0x9e3779b9)))
    (block $done (loop $l
      (br_if $done (i32.eqz (local.get $n)))
      (local.set $x (i32.add (i32.mul (local.get $x) (i32.const 1664525)) (i32.const 1013904223)))
      (local.set $y (i32.add (i32.mul (local.get $y) (i32.const 22695477)) (i32.const 1)))
      (local.set $acc (i64.add (i64.shr_u (local.get $acc) (i64.const 7))
        (i64.mul (i64.extend_i32_u (local.get $x)) (i64.extend_i32_u (local.get $y)))))
      (local.set $n (i32.sub (local.get $n) (i32.const 1)))
      (br $l)))
    (local.get $acc))
  (func $sdot (export "sdot") (param $n i32) (param $seed i32) (result i64)
    (local $acc i64) (local $x i32) (local $y i32)
    (local.set $x (local.get $seed))
    (local.set $y (i32.xor (local.get $seed) (i32.const 0x9e3779b9)))
    (block $done (loop $l
      (br_if $done (i32.eqz (local.get $n)))
      (local.set $x (i32.add (i32.mul (local.get $x) (i32.const 1664525)) (i32.const 1013904223)))
      (local.set $y (i32.add (i32.mul (local.get $y) (i32.const 22695477)) (i32.const 1)))
      (local.set $acc (i64.sub (i64.rotl (local.get $acc) (i64.const 5))
        (i64.mul (i64.extend_i32_s (local.get $x)) (i64.extend_i32_s (local.get $y)))))
      (local.set $n (i32.sub (local.get $n) (i32.const 1)))
      (br $l)))
    (local.get $acc))
"""


def states(n, seed):
    x, y = zx(seed), zx(seed) ^ 0x9e3779b9
    for _ in range(zx(n)):
        x = (x * 1664525 + 1013904223) & M32
        y = (y * 22695477 + 1) & M32
        yield x, y


def dot(n, seed):
    acc = 0
    for x, y in states(n, seed):
        acc = ((acc >> 7) + x * y) & M64
    return acc


def rotl(x, k):
    return ((x << k) | (x >> (64 - k))) & M64


def sdot(n, seed):
    acc = 0
    for x, y in states(n, seed):
        acc = (rotl(acc, 5) - sx(x) * sx(y)) & M64
    return acc


# a few arguments each, the ones an extension the wrong way round, a dropped
# high half or a constant read at the wrong width gives another answer for:
# every run is a program started
E32 = [(0xffffffff, 0xffffffff), (0x80000000, 0x7fffffff), (0x7fffffff, 0x80000001), (3, 0xfffffffe),
       (123456789, 3141592653), (0, 5), (1, 0x80000000), (0x80000001, 0x80000001)]
E1 = [0, 1, 0x7fffffff, 0x80000000, 0xffffffff, 3141592653]
WIDE = [0, 0xffffffff, 0x100000000, 0x7fffffffffffffff, 0x8000000000000000, M64, 0x123456789abcdef0,
        0x7fffffff, 0x80000000, 0xfffffffe, 0x7fffffff00000000, 0x8000, 0xffff, 0x7fffffffffff0000]
LOADS = [0, 4, 7, 9, 16, 31, 40, 52]


def arg_sets(params, body=""):
    kinds = params.split()
    if kinds == ["i32"] and "load" in body:
        return [(p,) for p in range(0, 56, 9)]
    if kinds == ["i32"]:
        return [(a,) for a in E1]
    if kinds == ["i32", "i32"] and "load" in body:
        two = body.count("load") == 2
        return [(p, b) for p, b in zip((0, 1, 2, 3, 5, 8, 13, 21), LOADS if two else
                                       (0xffffffff, 0x80000000, 0x7fffffff, 3, 0xfffffffe, 0x80000001, 1, 123456789))]
    if kinds == ["i32", "i32"]:
        return E32
    if kinds == ["i64", "i64"]:
        # values whose high halves are not their low halves' extension
        return [(a | (h << 32), b | ((h ^ 0x5a5a5a5a) << 32)) for (a, b), h in zip(E32, (0, 0xdeadbeef) * 4)]
    if kinds == ["i32", "i64"]:
        return [(a, c) for (a, _), c in zip(E32, WIDE)]
    if kinds == ["i64", "i32"]:
        return [(c, a) for c, a in zip(WIDE, (0xffffffff, 0x80000000, 0x7fffffff, 3, 0xfffffffe, 0x80000001, 1) * 2)]
    if kinds == ["i64", "i64", "i32"]:
        return [(c, d, a) for c, d, a in zip(WIDE, (0x7fff, 0x7fffffff, 0x80000000, 0x8000) * 4,
                                             (0xffffffff, 0x80000000, 0x7fffffff, 3, 0xfffffffe) * 3)]
    if kinds == ["i64", "i32", "i64"]:
        return [(0x1111111122222222, a, b | (h << 32)) for (a, b), h in zip(E32, (0, 0xdeadbeef) * 4)]
    if kinds == ["i32", "i32", "i64"]:
        return [(a, b, c) for (a, b), c in zip(E32, WIDE)]
    raise ValueError(params)


def main(path):
    runs, shapes, funcs, folds = [], [], [], 0
    for name, params, body, model, shape in FUNCS:
        funcs.append('  (func $%s (export "%s") (param %s) (result i64)\n    %s)\n' % (name, name, params, body))
        for pat, n in shape.items():
            shapes.append(";; shape: %s %s=%d" % (name, pat, n))
        made = sum(n for pat, n in shape.items() if "mull" in pat or "maddl" in pat or "msubl" in pat)
        folds += made
        for args in arg_sets(params, body):
            runs.append(";; run: %s %s => i64:%d" % (name, " ".join(str(a) for a in args), s64(model(*args))))
    funcs.append(FAR)
    shapes += [r";; shape: far \sumaddl\s=1", r";; shape: far %s=0" % EXT,
               r";; shape: far_s \ssmsubl\s=1", r";; shape: far_s %s=0" % EXT]
    folds += 2
    # the stores are of the first call's, in a memory each run starts anew
    for a, b in E32[:5]:
        runs.append(";; run: far %d %d 1 => i64:%d" % (a, b, s64(9 + zx(a) * zx(b))))
        runs.append(";; run: far %d %d 0 => i64:%d" % (a, b, s64(zx(a) * zx(b))))
        runs.append(";; run: far_s %d %d 1 => i64:%d" % (a, b, s64(9 - sx(a) * sx(b))))
        runs.append(";; run: far_s %d %d 0 => i64:%d" % (a, b, s64(-sx(a) * sx(b))))
    funcs.append(OTHER)
    shapes += [r";; shape: other \ssmull\s=5", r";; shape: other \smul\s+x=0", r";; shape: other \ssxtw\s=1"]
    folds += 5
    for a in (5, 0xffffffff, 0x80000000):
        for n in (0, 1, 3, 4, 5, 6, 100):
            runs.append(";; run: other %d %d => i64:%d" % (a, n, s64(other(a, n))))
    funcs.append(DOT)
    # each loop is unrolled by four, with the trips left over after it
    shapes += [r";; shape: dot \sumaddl\s=5", r";; shape: dot \sumull\s=0", r";; shape: dot \smadd\s+x=0",
               r";; shape: sdot \ssmsubl\s=5", r";; shape: sdot \ssmull\s=0", r";; shape: sdot \ssxtw\s=0"]
    folds += 10
    for n in (0, 1, 3, 4, 5, N, 1000):
        for seed in (1, 0xdeadbeef):
            runs.append(";; run: dot %d %d => i64:%d" % (n, seed, s64(dot(n, seed))))
            runs.append(";; run: sdot %d %d => i64:%d" % (n, seed, s64(sdot(n, seed))))
    head = HEAD % {"FOLDS": folds}
    with open(path, "w") as f:
        data = "".join("\\%02x" % b for b in MEM)
        f.write(head + "\n".join(shapes) + "\n;;\n" + "\n".join(runs) +
                "\n\n(module\n  (memory 1)\n  (data (i32.const 0) \"%s\")\n" % data +
                "".join(funcs) + ")\n")
    print("%s: %d functions, %d runs, mull_folds=%d" % (path, len(FUNCS) + 5, len(runs), folds))


HEAD = """;; A 64-bit multiply of two 32-bit values extended alike is umull or smull,
;; which read w registers and extend them themselves, and a sum or a
;; difference with one is umaddl, smaddl, umsubl or smsubl (isel.c, ahead of
;; the folds into adds; isel_ops.inc's I_MUL and its add of a folded
;; multiply).  An extension only the multiply reads is no instruction; the
;; other operand is another, or a value that is its low half extended the
;; same way already.  From test/gen_wide_mul.py, which works out every
;; answer.
;;
;;   umul smul        both extended unsigned, both signed
;;   ext32 smix       i64.extend32_s, of values whose high halves are other
;;                    things, and one of each signed extension
;;   mixed mixed2     one unsigned and one signed: a mul of two extensions
;;   half half2       one extended and one a 64-bit value: as it was
;;   umul_k umul_p k_umul umul_max
;;                    an unsigned extension and a constant under 2^32, on
;;                    either side
;;   smul_p smul_n n_smul smul_min smul_top
;;                    a signed one and a constant that is an int32
;;   u_wide u_neg     an unsigned one and a constant of 2^32 or more, which a
;;                    negative one is: as it was
;;   s_big s_negbig s_wide s_over s_under u_over
;;                    a signed one and a constant that is no int32, by a
;;                    lot or by a little, and an unsigned one just over 2^32
;;   twice twice_s    one extension is read again after the multiply: it
;;                    stays, the multiply reads its low half, and the other
;;                    is dropped
;;   twice_mix        the same, the two extended differently: as it was
;;   both_twice       both read again: nothing to drop, a mul
;;   ld_u ld_s ld8_u ld8_us ld16_us ld16_s
;;                    one operand a load of 32 bits or fewer, extended as the
;;                    other is, or of fewer unsigned, which is both
;;   ld16_su ld8_su ld32_us ld32_su ld64 ld_ld ld_k
;;                    a load extended the other way, one of 64 bits, and two
;;                    loads or a load and a constant, with nothing to drop
;;   square ssquare   both operands the one extension
;;   square3          the same, read a third time: as it was
;;   and_u shru_u shrs_s ext16_s and_s both_ways sum16
;;                    the other operand any value with zeros above its low
;;                    half, or its sign's copies there: masked, shifted,
;;                    extended from fewer bits, a sum of two such
;;   shru31 shrs31 shrs_u ext16_u and_s32 sum32
;;                    one bit short of that, or extended the other way: as
;;                    it was
;;   umadd smadd umadd_l umsub smsub
;;                    the product added to a value, on either side, or taken
;;                    from one
;;   sub_l            a value taken from the product: umull and a sub
;;   hash shash       the sum with an extension times a constant
;;   sq_add           a square added
;;   far far_s        an extension made in a block before the multiply's:
;;                    dropped as one beside it is
;;   other            one extension made before the loop the multiply is
;;                    in, which each copy of the unrolled loop reads: it
;;                    stays there, and the other, in the loop, is dropped
;;   dot sdot         a hash's loop: two 32-bit states a trip and their product
;;                    into the sum, one umaddl or smsubl a trip (five of
;;                    each: the loop unrolled by four, and the trips left)
;;
;; stats -O2 a64: mull_folds=%(FOLDS)d
"""

if __name__ == "__main__":
    main(sys.argv[1])
