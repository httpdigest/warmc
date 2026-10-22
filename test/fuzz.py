"""Random-module differential fuzzer for warmc.

Generates trap-free wasm modules of nullary exports, runs them through
wasm-interp for the answers, and compares warmc's compiled output at several
optimisation levels.  Seeds are printed so a failure reproduces.

Each seed picks a flavour -- one 32-bit memory, two memories, or a 64-bit
memory -- and may add v128 values, helper functions called directly and
through a table, tail calls and br_table, so the shapes that reach the ABI and
the newer instruction sets are generated as well as the arithmetic.

Those generic modules rarely make the shapes a pattern-matching pass looks
for, so a seed of the *targeted* family adds exports built around one of them
(KINDS, one primary and up to two more a module) and mixes smaller versions of
the same shapes into the generic statements:

  argmin     an index kept beside a min or max over an i32 array: signed and
             unsigned, strict or not, the compare and the select arms either
             way round, an index offset, two elements an iteration, ties and
             identity values in the data -- and near misses of each
  invdiv     unsigned division and remainder by a divisor the loop does not
             change, in 32 bits and in 64 bits of zero-extended 32-bit values
  divrem     a remainder of the operands of a division just made, next to the
             orders and blocks in which it must stay a remainder
  xorshift   y = x ^ (x >> k), then y ^ (y >> k), around half the width
  fbits      float constants written as integer bits, used in loops
  selects    several selects on one compare with other things in between, and
             the same operands compared again in the block after a branch
  bigconst   constants that take a movz/movk chain, around calls and on both
             edges of a branch
  pressure   more live values than registers, straight-line and round a loop
  coldcall   calls on a rarely taken path with values live across them
  trapguard  a loop guard the loop does not change, leaving for a trap that
             reads the loop's counter -- false at run time, which the
             compiler cannot see, so nothing traps
  loopshape  loops to rotate, `while (n--)`, a compare of a merged constant
             to thread, and runs of tiny loops to pad

The tables these read (edge values, divisors, lengths, arrays) sit in memory 0
above the first kilobyte, which is all the generic statements address, so the
two never disturb one another.  `--flavours old` makes the generic modules
alone, exactly as before the targeted family existed: an old seed reproduces
with it.

Two things the comparison has to know:

  * A NaN's payload and sign are not determined by the spec, so a NaN must
    never reach a compared result except through a guard that folds every NaN
    to one pattern.  There are three ways it gets there and all three are shut
    off below: the value a function returns, a float stored to memory and read
    back as an integer, and -- the one that is easy to miss -- `copysign`,
    whose *second* operand contributes only a sign, so an arbitrary NaN there
    decides the sign of a perfectly finite answer.  Without all three,
    comparing bits against another engine compares nothing and every third
    module "fails" on a difference the standard permits.
  * A generated loop counter must not be writable by the loop body, or the
    module stops terminating and the fuzzer reports the interpreter, not warmc.

This is a soak tool, not part of `make check`: one module costs an
interpreter run and a link per level, so a useful sweep is minutes rather than
the seconds the suite is held to.  `make fuzz` runs a few hundred seeds; a
failure prints its seed and leaves the module behind as `bad-<seed>.wat`, and
re-running with that seed reproduces it exactly.

Usage: fuzz.py [--seed N] [--count N] [--levels=-O0,-O1,-O2] [--flags ...]
               [--flavours all|old|new|KIND,KIND...]
"""
import argparse
import os
import random
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "test"))
import compare as cmp_mod
import hostbuild

WABT_FLAGS = hostbuild.wabt_flags()

I32V = ["0", "1", "2", "7", "31", "32", "0x7fffffff", "0x80000000",
        "0xffffffff", "0xfffffffe", "0xdeadbeef", "3", "255", "65535",
        "0x40000000", "5", "0x55555555", "0xaaaaaaaa"]
I64V = ["0", "1", "63", "64", "0xff", "0x7fffffffffffffff",
        "0x8000000000000000", "0xffffffffffffffff", "0x123456789abcdef0",
        "0x100000000", "7", "0x5555555555555555"]
F32V = ["0.0", "-0.0", "1.0", "-1.0", "0.5", "3.14159", "1e30", "-1e-30",
        "inf", "-inf", "nan", "16777216.0", "2.5"]
F64V = ["0.0", "-0.0", "1.0", "-1.0", "0.5", "3.141592653589793", "1e300",
        "-1e-300", "inf", "-inf", "nan", "9007199254740992.0", "2.5"]

UN = {
    "i32": [("(i32.clz {0})", ["i32"]), ("(i32.ctz {0})", ["i32"]),
            ("(i32.popcnt {0})", ["i32"]), ("(i32.eqz {0})", ["i32"]),
            ("(i32.extend8_s {0})", ["i32"]), ("(i32.extend16_s {0})", ["i32"]),
            ("(i32.wrap_i64 {0})", ["i64"]), ("(i64.eqz {0})", ["i64"]),
            ("(i32.trunc_sat_f32_s {0})", ["f32"]),
            ("(i32.trunc_sat_f32_u {0})", ["f32"]),
            ("(i32.trunc_sat_f64_s {0})", ["f64"]),
            ("(i32.trunc_sat_f64_u {0})", ["f64"])],
    "i64": [("(i64.clz {0})", ["i64"]), ("(i64.ctz {0})", ["i64"]),
            ("(i64.popcnt {0})", ["i64"]),
            ("(i64.extend8_s {0})", ["i64"]), ("(i64.extend16_s {0})", ["i64"]),
            ("(i64.extend32_s {0})", ["i64"]),
            ("(i64.extend_i32_s {0})", ["i32"]),
            ("(i64.extend_i32_u {0})", ["i32"]),
            ("(i64.trunc_sat_f32_s {0})", ["f32"]),
            ("(i64.trunc_sat_f64_u {0})", ["f64"])],
    "f32": [("(f32.abs {0})", ["f32"]), ("(f32.neg {0})", ["f32"]),
            ("(f32.sqrt {0})", ["f32"]), ("(f32.ceil {0})", ["f32"]),
            ("(f32.floor {0})", ["f32"]), ("(f32.trunc {0})", ["f32"]),
            ("(f32.nearest {0})", ["f32"]),
            ("(f32.demote_f64 {0})", ["f64"]),
            ("(f32.convert_i32_s {0})", ["i32"]),
            ("(f32.convert_i32_u {0})", ["i32"]),
            ("(f32.convert_i64_s {0})", ["i64"]),
            ("(f32.reinterpret_i32 {0})", ["i32"])],
    "f64": [("(f64.abs {0})", ["f64"]), ("(f64.neg {0})", ["f64"]),
            ("(f64.sqrt {0})", ["f64"]), ("(f64.ceil {0})", ["f64"]),
            ("(f64.floor {0})", ["f64"]), ("(f64.trunc {0})", ["f64"]),
            ("(f64.nearest {0})", ["f64"]),
            ("(f64.promote_f32 {0})", ["f32"]),
            ("(f64.convert_i32_s {0})", ["i32"]),
            ("(f64.convert_i64_u {0})", ["i64"]),
            ("(f64.reinterpret_i64 {0})", ["i64"])],
}

BIN = {
    "i32": [("(i32.add {0} {1})", ["i32", "i32"]),
            ("(i32.sub {0} {1})", ["i32", "i32"]),
            ("(i32.mul {0} {1})", ["i32", "i32"]),
            ("(i32.div_s {0} (i32.or {1} (i32.const 1)))", ["i32", "i32"]),
            ("(i32.div_u {0} (i32.or {1} (i32.const 1)))", ["i32", "i32"]),
            ("(i32.rem_s {0} (i32.or {1} (i32.const 1)))", ["i32", "i32"]),
            ("(i32.rem_u {0} (i32.or {1} (i32.const 1)))", ["i32", "i32"]),
            ("(i32.and {0} {1})", ["i32", "i32"]),
            ("(i32.or {0} {1})", ["i32", "i32"]),
            ("(i32.xor {0} {1})", ["i32", "i32"]),
            ("(i32.shl {0} {1})", ["i32", "i32"]),
            ("(i32.shr_s {0} {1})", ["i32", "i32"]),
            ("(i32.shr_u {0} {1})", ["i32", "i32"]),
            ("(i32.rotl {0} {1})", ["i32", "i32"]),
            ("(i32.rotr {0} {1})", ["i32", "i32"]),
            ("(i32.eq {0} {1})", ["i32", "i32"]),
            ("(i32.ne {0} {1})", ["i32", "i32"]),
            ("(i32.lt_s {0} {1})", ["i32", "i32"]),
            ("(i32.lt_u {0} {1})", ["i32", "i32"]),
            ("(i32.gt_s {0} {1})", ["i32", "i32"]),
            ("(i32.gt_u {0} {1})", ["i32", "i32"]),
            ("(i32.le_s {0} {1})", ["i32", "i32"]),
            ("(i32.ge_u {0} {1})", ["i32", "i32"]),
            ("(i64.eq {0} {1})", ["i64", "i64"]),
            ("(i64.lt_s {0} {1})", ["i64", "i64"]),
            ("(i64.gt_u {0} {1})", ["i64", "i64"]),
            ("(f32.eq {0} {1})", ["f32", "f32"]),
            ("(f32.lt {0} {1})", ["f32", "f32"]),
            ("(f64.ne {0} {1})", ["f64", "f64"]),
            ("(f64.ge {0} {1})", ["f64", "f64"])],
    "i64": [("(i64.add {0} {1})", ["i64", "i64"]),
            ("(i64.sub {0} {1})", ["i64", "i64"]),
            ("(i64.mul {0} {1})", ["i64", "i64"]),
            ("(i64.div_s {0} (i64.or {1} (i64.const 1)))", ["i64", "i64"]),
            ("(i64.div_u {0} (i64.or {1} (i64.const 1)))", ["i64", "i64"]),
            ("(i64.rem_s {0} (i64.or {1} (i64.const 1)))", ["i64", "i64"]),
            ("(i64.rem_u {0} (i64.or {1} (i64.const 1)))", ["i64", "i64"]),
            ("(i64.and {0} {1})", ["i64", "i64"]),
            ("(i64.or {0} {1})", ["i64", "i64"]),
            ("(i64.xor {0} {1})", ["i64", "i64"]),
            ("(i64.shl {0} {1})", ["i64", "i64"]),
            ("(i64.shr_s {0} {1})", ["i64", "i64"]),
            ("(i64.shr_u {0} {1})", ["i64", "i64"]),
            ("(i64.rotl {0} {1})", ["i64", "i64"]),
            ("(i64.rotr {0} {1})", ["i64", "i64"])],
    "f32": [("(f32.add {0} {1})", ["f32", "f32"]),
            ("(f32.sub {0} {1})", ["f32", "f32"]),
            ("(f32.mul {0} {1})", ["f32", "f32"]),
            ("(f32.div {0} {1})", ["f32", "f32"]),
            ("(f32.min {0} {1})", ["f32", "f32"]),
            ("(f32.max {0} {1})", ["f32", "f32"])],
    "f64": [("(f64.add {0} {1})", ["f64", "f64"]),
            ("(f64.sub {0} {1})", ["f64", "f64"]),
            ("(f64.mul {0} {1})", ["f64", "f64"]),
            ("(f64.div {0} {1})", ["f64", "f64"]),
            ("(f64.min {0} {1})", ["f64", "f64"]),
            ("(f64.max {0} {1})", ["f64", "f64"])],
}

LOADS = {
    "i32": [("i32.load", 4), ("i32.load8_s", 1), ("i32.load8_u", 1),
            ("i32.load16_s", 2), ("i32.load16_u", 2)],
    "i64": [("i64.load", 8), ("i64.load8_s", 1), ("i64.load8_u", 1),
            ("i64.load16_s", 2), ("i64.load16_u", 2), ("i64.load32_s", 4),
            ("i64.load32_u", 4)],
    "f32": [("f32.load", 4)],
    "f64": [("f64.load", 8)],
}
STORES = {"i32": [("i32.store", 4), ("i32.store8", 1), ("i32.store16", 2)],
          "i64": [("i64.store", 8), ("i64.store8", 1), ("i64.store16", 2),
                  ("i64.store32", 4)],
          "f32": [("f32.store", 4)], "f64": [("f64.store", 8)]}

VUN = ["i8x16.abs", "i8x16.neg", "i8x16.popcnt", "i16x8.abs", "i16x8.neg",
       "i32x4.abs", "i32x4.neg", "i64x2.abs", "i64x2.neg", "v128.not",
       "i16x8.extend_low_i8x16_s", "i16x8.extend_high_i8x16_u",
       "i32x4.extend_low_i16x8_s", "i64x2.extend_high_i32x4_u",
       "i16x8.extadd_pairwise_i8x16_s", "i32x4.extadd_pairwise_i16x8_u"]

VBIN = ["i8x16.add", "i8x16.sub", "i8x16.add_sat_s", "i8x16.sub_sat_u",
        "i8x16.min_s", "i8x16.max_u", "i8x16.avgr_u", "i8x16.eq", "i8x16.lt_s",
        "i16x8.add", "i16x8.mul", "i16x8.q15mulr_sat_s",
        "i16x8.narrow_i32x4_s", "i16x8.extmul_low_i8x16_u", "i32x4.add",
        "i32x4.mul", "i32x4.min_u", "i32x4.dot_i16x8_s", "i64x2.add",
        "i64x2.mul", "i64x2.lt_s", "v128.and", "v128.or", "v128.xor",
        "v128.andnot", "i8x16.swizzle"]
VSHIFT = ["i8x16.shl", "i8x16.shr_s", "i16x8.shr_u", "i32x4.shl",
          "i32x4.shr_s", "i64x2.shr_u"]
VTOI = [("i8x16.extract_lane_s", 16, "i32"), ("i8x16.extract_lane_u", 16, "i32"),
        ("i16x8.extract_lane_s", 8, "i32"), ("i32x4.extract_lane", 4, "i32"),
        ("i64x2.extract_lane", 2, "i64"), ("i8x16.bitmask", 0, "i32"),
        ("i16x8.all_true", 0, "i32"), ("v128.any_true", 0, "i32"),
        ("i32x4.all_true", 0, "i32")]

TYPES = ["i32", "i64", "f32", "f64"]
CANON = {"f32": ("i32", "0x7fc00000", "i32.reinterpret_f32"),
         "f64": ("i64", "0x7ff8000000000000", "i64.reinterpret_f64")}

KINDS = ["argmin", "invdiv", "divrem", "xorshift", "fbits", "selects",
         "bigconst", "pressure", "coldcall", "trapguard", "loopshape"]

# The targeted family's tables, in memory 0 from 0x400 on: edge values,
# divisors (a few of them 0, so whoever divides by one has to guard it),
# lengths, finite doubles, a scratch area, a word that stays 0 and that
# nothing writes, and from K_ARR the arrays the argmin loops scan.
K_E32, K_E64, K_DIV, K_LEN, K_F64 = 0x400, 0x500, 0x600, 0x680, 0x6c0
K_SCR, K_ZERO, K_ARR = 0x700, 0x7f0, 0x1000
K_ARRN = 720

EDGE32 = [0, 1, 2, 3, 5, 7, 31, 32, 33, 64, 0xffff, 0x10000, 0x7ffffffe,
          0x7fffffff, 0x80000000, 0x80000001, 0xfffffffe, 0xffffffff,
          0x55555555, 0xaaaaaaaa, 0xdeadbeef, 0x40000000, 0xc0000000]
EDGE64 = [0, 1, 2, 3, 7, 63, 64, 0xffffffff, 0x100000000, 0x7fffffff,
          0x80000000, 0x7fffffffffffffff, 0x8000000000000000,
          0x8000000000000001, 0xfffffffffffffffe, 0xffffffffffffffff,
          0x123456789abcdef0, 0x5555555555555555, 0xffffffff00000000]
DIV32 = ([1, 2, 3, 5, 6, 7, 10, 25, 100, 641, 0xffff, 0x10001, 0x7fffffff,
          0x80000001, 0xfffffffe, 0xffffffff] +
         [1 << k for k in range(1, 32)])
LENS = [0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 100,
        127, 128, 129, 255, 256, 257, 300, 511, 600]
# Doubles and floats as bits: what an fmov immediate encodes (1.0, 0.5,
# -2.0, 31.0), what it does not (-0.0, 0.1, a denormal, the largest finite
# value, the smallest normal), infinities and NaNs with payloads.  NaNs reach
# a result only through the guards described above.
F64BITS = [0x3ff0000000000000, 0x3fe0000000000000, 0xc000000000000000,
           0x403f000000000000, 0x8000000000000000, 0, 0x3fb999999999999a,
           0x3fd5555555555555, 1, 0x000fffffffffffff, 0x7fefffffffffffff,
           0x0010000000000000, 0x7ff0000000000000, 0xfff0000000000000,
           0x7ff8000000000000, 0x7ff0000000000001, 0xfff8000000000123,
           0x7ff4000000000000, 0x4330000000000000, 0xbff8000000000000]
F32BITS = [0x3f800000, 0x3f000000, 0xc0000000, 0x41f80000, 0x80000000, 0,
           0x3dcccccd, 1, 0x007fffff, 0x7f7fffff, 0x00800000, 0x7f800000,
           0xff800000, 0x7fc00000, 0x7f800001, 0xffc00123, 0x4b000000]
F64FIN = [b for b in F64BITS if (b >> 52) & 0x7ff != 0x7ff]
F32FIN = [b for b in F32BITS if (b >> 23) & 0xff != 0xff]

ICMP = ["eq", "ne", "lt_s", "lt_u", "gt_s", "gt_u", "le_s", "le_u", "ge_s",
        "ge_u"]
NEG = {"lt": "ge", "le": "gt", "gt": "le", "ge": "lt"}
MIRROR = {"lt": "gt", "le": "ge", "gt": "lt", "ge": "le"}
STRICTFLIP = {"lt": "le", "le": "lt", "gt": "ge", "ge": "gt"}

# Helpers the targeted exports call, directly or through the table at an
# index the compiler cannot see, so that some calls survive inlining.
KHELP = [("id", ["i32"], "i32", "(local.get 0)"),
         ("mix", ["i64", "i32"], "i64",
          "(i64.xor (i64.rotl (local.get 0) (i64.extend_i32_u (local.get 1)))"
          " (i64.const 0x9e3779b97f4a7c15))"),
         ("big", ["i32", "i64", "i32", "i64"], "i64",
          "(i64.add (i64.xor (i64.extend_i32_u (local.get 0)) (local.get 1))"
          " (i64.mul (i64.extend_i32_s (local.get 2)) (local.get 3)))"),
         ("fm", ["f64", "i32"], "f64",
          "(f64.add (f64.mul (local.get 0) (f64.const 0.5))"
          " (f64.convert_i32_s (local.get 1)))"),
         ("clob", ["i32"], "i32", None)]

def le_bytes(vals, size):
    return "".join("\\%02x" % ((v >> (8 * k)) & 0xff)
                   for v in vals for k in range(size))

def bigconst(r, wide):
    """A constant that takes more than one instruction to make on arm64,
    now and then one that does not (a logical immediate, a movn)."""
    k = r.random()
    if not wide:
        if k < 0.15:
            return r.choice([0xdeadbeef, 0x12345678, 0x811c9dc5, 0x9e3779b9,
                             0x80000001, 0xffff1234, 0x00ff00ff, 0xfffffff0])
        while True:
            v = r.getrandbits(32)
            if v & 0xffff and v >> 16:
                return v
    if k < 0.2:
        return r.choice([0x0123456789abcdef, 0xffff1234ffff5678,
                         0x0000123400005678, 0x5555555555555555,
                         0x8000000000000001, 0xfffffffffffff123,
                         0x9e3779b97f4a7c15, 0x00ff00ff00ff00ff,
                         0x100000001b3, 0xcbf29ce484222325])
    v = 0
    for h in r.sample(range(4), r.randint(2, 4)):
        v |= r.randrange(1, 0x10000) << (16 * h)
    return v

class Gen:
    def __init__(self, rng, kinds=None):
        self.rng = rng
        self.locals = {}
        self.readonly = []
        self.counter = 0
        self.loopdepth = 0
        self.labels = []
        self.globals = []
        self.needbuf = set()
        self.helpers = []
        self.nmem = 1
        self.mem64 = False
        self.vectors = False
        self.kinds = kinds
        self.kmods = []
        self.kextra = []
        self.kdepth = 0
        self.flavour = "old"

    def const(self, t):
        r = self.rng
        if t == "i32":
            return "(i32.const %s)" % r.choice(I32V)
        if t == "i64":
            return "(i64.const %s)" % r.choice(I64V)
        if t == "f32":
            return "(f32.const %s)" % r.choice(F32V)
        return "(f64.const %s)" % r.choice(F64V)

    def memsuf(self, m):
        return ("%d " % m) if self.nmem > 1 else ""

    def addr(self, width=8, mem=None):
        """An address in the first page, aligned for any access it carries."""
        m = self.rng.randrange(self.nmem) if mem is None else mem
        mask = 0x3F0 if width > 8 else 0x3F8
        inner = self.expr("i32", 1)
        if self.mem64:
            return ("%s(i64.and (i64.extend_i32_u %s) (i64.const 0x%x))"
                    % (self.memsuf(m), inner, mask))
        return "%s(i32.and %s (i32.const 0x%x))" % (self.memsuf(m), inner, mask)

    def leaf(self, t):
        r = self.rng
        opts = [lambda: self.const(t)]
        if self.locals.get(t):
            opts.append(lambda: "(local.get $%s)" % r.choice(self.locals[t]))
        if t == "i32" and self.readonly:
            opts.append(lambda: "(local.get $%s)" % r.choice(self.readonly))
        for name, gt, _mut in self.globals:
            if gt == t:
                opts.append(lambda n=name: "(global.get $%s)" % n)
        if t in LOADS:
            def ld():
                op, w = r.choice(LOADS[t])
                return "(%s %s)" % (op, self.addr(w))
            opts.append(ld)
        if self.vectors and t in ("i32", "i64"):
            def fromvec():
                op, n, _ = r.choice([v for v in VTOI if v[2] == t])
                lane = (" %d" % r.randrange(n)) if n else ""
                return "(%s%s %s)" % (op, lane, self.vexpr(1))
            opts.append(fromvec)
        if self.kmods:
            opts.append(lambda: self.kconst(t))
        return r.choice(opts)()

    def vexpr(self, depth):
        r = self.rng
        if depth <= 0 or r.random() < 0.3:
            opts = [lambda: "(v128.load %s)" % self.addr(16)]
            if self.locals.get("v128"):
                opts.append(lambda: "(local.get $%s)"
                            % r.choice(self.locals["v128"]))
            opts.append(lambda: "(i8x16.splat %s)" % self.expr("i32", 0))
            opts.append(lambda: "(i64x2.splat %s)" % self.expr("i64", 0))
            return r.choice(opts)()
        k = r.random()
        if k < 0.30:
            return "(%s %s)" % (r.choice(VUN), self.vexpr(depth - 1))
        if k < 0.68:
            return "(%s %s %s)" % (r.choice(VBIN), self.vexpr(depth - 1),
                                   self.vexpr(depth - 1))
        if k < 0.78:
            return "(%s %s %s)" % (r.choice(VSHIFT), self.vexpr(depth - 1),
                                   self.expr("i32", 0))
        if k < 0.86:
            return "(v128.bitselect %s %s %s)" % (
                self.vexpr(depth - 1), self.vexpr(depth - 1),
                self.vexpr(depth - 1))
        if k < 0.93:
            lanes = " ".join(str(r.randint(0, 31)) for _ in range(16))
            return "(i8x16.shuffle %s %s %s)" % (lanes, self.vexpr(depth - 1),
                                                 self.vexpr(depth - 1))
        return "(i8x16.replace_lane %d %s %s)" % (
            r.randint(0, 15), self.vexpr(depth - 1), self.expr("i32", 0))

    def expr(self, t, depth):
        r = self.rng
        if depth <= 0 or r.random() < 0.25:
            return self.leaf(t)
        pick = r.random()
        if pick < 0.09:
            return "(select %s %s %s)" % (self.expr(t, depth - 1),
                                          self.expr(t, depth - 1),
                                          self.expr("i32", depth - 1))
        if pick < 0.16:
            return "(if (result %s) %s (then %s) (else %s))" % (
                t, self.expr("i32", depth - 1), self.expr(t, depth - 1),
                self.expr(t, depth - 1))
        if pick < 0.21 and self.locals.get(t):
            return "(local.tee $%s %s)" % (r.choice(self.locals[t]),
                                           self.expr(t, depth - 1))
        if pick < 0.26:
            return "(block (result %s) %s)" % (t, self.expr(t, depth - 1))
        if pick < 0.34:
            cands = [(i, h) for i, h in enumerate(self.helpers) if h[3] == t]
            if cands:
                i, (name, ps, _rt, _out) = r.choice(cands)
                args = " ".join(self.expr(p, depth - 1) for p in ps)
                if r.random() < 0.35:
                    return ("(call_indirect (type $t%d) %s (i32.const %d))"
                            % (i, args, i))
                return "(call $%s %s)" % (name, args)
        table = UN[t] if r.random() < 0.35 else BIN[t]
        tmpl, args = r.choice(table)
        return tmpl.format(*[self.expr(a, depth - 1) for a in args])

    def stmt(self, depth):
        r = self.rng
        if self.kmods and r.random() < 0.14:
            return self.kstmt(depth)
        k = r.random()
        if k < 0.30:
            live = [t for t in TYPES if self.locals.get(t)]
            if live:
                t = r.choice(live)
                return "(local.set $%s %s)" % (r.choice(self.locals[t]),
                                               self.expr(t, depth))
        if k < 0.50:
            t = r.choice(TYPES)
            op, w = r.choice(STORES[t])
            val = self.expr(t, depth)
            if t in CANON:

                buf = "stbuf_" + t
                self.needbuf.add(t)
                val = ("(select (%s.const 0) (local.tee $%s %s)"
                       " (%s.ne (local.get $%s) (local.get $%s)))"
                       % (t, buf, val, t, buf, buf))
            return "(%s %s %s)" % (op, self.addr(w), val)
        if k < 0.56 and self.vectors:
            if self.locals.get("v128") and r.random() < 0.5:
                return "(local.set $%s %s)" % (
                    r.choice(self.locals["v128"]), self.vexpr(depth))
            return "(v128.store %s %s)" % (self.addr(16), self.vexpr(depth))
        if k < 0.66:
            muts = [g for g in self.globals if g[2]]
            if muts:
                name, gt, _ = r.choice(muts)
                return "(global.set $%s %s)" % (name, self.expr(gt, depth))
        if k < 0.72 and self.labels:
            return self.br_table(depth)
        if k < 0.88:
            return self.loop(depth)
        return "(drop %s)" % self.expr(r.choice(TYPES), depth)

    def br_table(self, depth):
        """Every target is a block label with no parameters, so any mix of
        depths is well-typed and the branch simply leaves them."""
        r = self.rng
        targets = [r.choice(self.labels) for _ in range(r.randint(2, 5))]
        return "(br_table %s %s %s)" % (
            " ".join("$" + t for t in targets), "$" + r.choice(self.labels),
            self.expr("i32", max(0, depth - 1)))

    def loop(self, depth):
        """A counted loop.  The counter is readable but never writable -- a
        `local.set` on it is how a generated module stops terminating."""
        r = self.rng
        if self.loopdepth >= 2:
            return "(drop %s)" % self.expr(r.choice(TYPES), depth)
        n = r.randint(1, 6)
        self.counter += 1
        i = "c%d" % self.counter
        self.loopdepth += 1
        self.readonly.append(i)
        self.labels.append("b" + i)
        body = "\n      ".join(self.stmt(max(0, depth - 1))
                               for _ in range(r.randint(1, 3)))
        self.labels.pop()
        self.loopdepth -= 1
        return ("(local.set $%s (i32.const 0))\n"
                "    (block $b%s (loop $l%s\n"
                "      %s\n"
                "      (local.set $%s (i32.add (local.get $%s) (i32.const 1)))\n"
                "      (br_if $l%s (i32.lt_u (local.get $%s) (i32.const %d)))))"
                % (i, i, i, body, i, i, i, i, n))

    def body_of(self, rt, params=()):
        """Locals, statements and a NaN-canonicalised result expression."""
        r = self.rng
        self.locals = {}
        self.readonly = []
        self.labels = []
        self.needbuf = set()
        self.counter = 0
        self.loopdepth = 0
        self.kextra = []
        for t in TYPES:
            self.locals[t] = ["%s_%d" % (t, k) for k in range(r.randint(1, 3))]
        if self.vectors:
            self.locals["v128"] = ["v_%d" % k for k in range(r.randint(1, 2))]
        decls = "".join("    (local $%s %s)\n" % (n, t)
                        for t in (TYPES + ["v128"])
                        for n in self.locals.get(t, []))
        for i, p in enumerate(params):
            self.locals.setdefault(p, []).append("p%d" % i)
        body = "\n    ".join(self.stmt(r.randint(1, 3))
                             for _ in range(r.randint(1, 4)))
        decls += "".join("    (local $%s i32)\n" % n for n in self.readonly)
        ret = self.expr(rt, r.randint(1, 4))
        decls += "".join("    (local $stbuf_%s %s)\n" % (t, t)
                         for t in sorted(self.needbuf))
        decls += "".join("    (local $%s %s)\n" % nt for nt in self.kextra)
        out = rt
        if rt in CANON:

            ity, canon, wrap = CANON[rt]
            tmp = "nanbuf"
            decls += "    (local $%s %s)\n" % (tmp, rt)
            body += "\n    (local.set $%s %s)" % (tmp, ret)
            ret = ("(if (result %s) (%s.ne (local.get $%s) (local.get $%s))"
                   " (then (%s.const %s)) (else (%s (local.get $%s))))"
                   % (ity, rt, tmp, tmp, ity, canon, wrap, tmp))
            out = ity
        return decls, body, ret, out

    def helper(self, idx, ps, rt):
        decls, body, ret, out = self.body_of(rt, ps)
        sig = " ".join("(param $p%d %s)" % (i, p) for i, p in enumerate(ps))
        tail = ""

        if idx > 0 and self.rng.random() < 0.3:
            prev = self.helpers[idx - 1]
            if prev[2] == rt and prev[1] == ps:
                args = " ".join("(local.get $p%d)" % i for i in range(len(ps)))
                tail = ("    (if (i32.eqz (i32.and %s (i32.const 1)))\n"
                        "      (then (return_call $%s %s)))\n"
                        % (self.expr("i32", 1), prev[0], args))
        return ('  (func $h%d %s (result %s)\n%s%s    %s\n    %s)\n'
                % (idx, sig, out, decls, tail, body, ret)), out

    def func(self, idx):
        rt = self.rng.choice(TYPES)
        decls, body, ret, out = self.body_of(rt)
        return ('  (func $f%d (export "f%d") (result %s)\n%s    %s\n    %s)\n'
                % (idx, idx, out, decls, body, ret))

    # -- the targeted family ------------------------------------------------
    #
    # Nothing here runs for a seed of the old family, and nothing above draws
    # a number more for one, so an old seed still makes the module it made.

    def kinit(self):
        r = self.rng
        self.kprim = r.choice(self.kinds)
        rest = [k for k in self.kinds if k != self.kprim]
        self.kmods = [self.kprim] + r.sample(rest, min(len(rest),
                                                       r.randint(0, 2)))
        self.flavour = "%s, %s" % ("+".join(self.kmods), self.flavour)
        e32 = [r.choice(EDGE32) if r.random() < 0.75 else r.getrandbits(32)
               for _ in range(64)]
        e64 = [r.choice(EDGE64) if r.random() < 0.75 else r.getrandbits(64)
               for _ in range(32)]
        dv = [0 if r.random() < 0.05 else
              r.choice(DIV32) if r.random() < 0.8 else r.getrandbits(32)
              for _ in range(32)]
        lens = [r.choice(LENS) if r.random() < 0.7 else r.randrange(701)
                for _ in range(16)]
        fin = [r.choice(F64FIN) for _ in range(8)]
        self.kdata = [(K_E32, le_bytes(e32, 4)), (K_E64, le_bytes(e64, 8)),
                      (K_DIV, le_bytes(dv, 4)), (K_LEN, le_bytes(lens, 4)),
                      (K_F64, le_bytes(fin, 8))]
        self.kglob = [("kseed", r.getrandbits(32)), ("kflag", 0),
                      ("kdv", r.choice(DIV32))]
        self.narr = 0
        self.nhelpers = 0
        self.klabn = 0
        self.kfuncs = []
        self.khelp = []
        for n, ps, rt, body in KHELP:
            if body is None:
                body = ("(global.set $kseed (i32.add (i32.mul (global.get $kseed)"
                        " (i32.const 1103515245)) (local.get 0)))\n    %s\n"
                        "    (i32.xor (local.get 0) (global.get $kseed))"
                        % self.kst("i32.store", K_SCR + 0xe0, "(i32.const 0)",
                                   "(global.get $kseed)"))
            self.khelp.append("  (func $k_%s %s (result %s)\n    %s)\n"
                              % (n, " ".join("(param %s)" % p for p in ps),
                                 rt, body))
        if "argmin" in self.kmods:
            self.karray()

    def karray(self):
        """The base of an array for the argmin loops: a new one, up to
        three, or one already made."""
        r = self.rng
        if self.narr == 3 or (self.narr and r.random() < 0.3):
            return K_ARR + 0x1000 * r.randrange(self.narr)
        base = K_ARR + 0x1000 * self.narr
        self.narr += 1
        self.kdata.append((base, le_bytes(self.kfill(K_ARRN), 4)))
        return base

    def kfill(self, n):
        """Random words, a few values over and over (ties), one value
        throughout, mostly the extremes, small negatives, or a run up or
        down; with the identity of every min and max dropped in here and
        there."""
        r = self.rng
        mode = r.randrange(6)
        pool = [r.getrandbits(32) for _ in range(r.randint(1, 4))]
        a = r.getrandbits(32)
        step = r.choice([1, 3, 0xffffffff, 0x10001, 0x01000193])
        ext = [0x7fffffff, 0x80000000, 0, 0xffffffff]
        out = []
        for k in range(n):
            if mode == 0:
                v = r.getrandbits(32)
            elif mode == 1:
                v = r.choice(pool)
            elif mode == 2:
                v = pool[0]
            elif mode == 3:
                v = r.choice(ext + [r.getrandbits(32)])
            elif mode == 4:
                v = (-r.randrange(16)) & 0xffffffff
            else:
                v = (a + k * step) & 0xffffffff
            if r.random() < 0.02:
                v = r.choice(ext)
            out.append(v)
        return out

    def ka(self, e):
        """An i32 byte address as memory 0 takes it."""
        return "(i64.extend_i32_u %s)" % e if self.mem64 else e

    def kld(self, op, off, e):
        return "(%s offset=%d %s)" % (op, off, self.ka(e))

    def kst(self, op, off, e, v):
        return "(%s offset=%d %s %s)" % (op, off, self.ka(e), v)

    def kzero(self):
        """0 at run time, from a word nothing writes, so the compiler cannot
        see through it."""
        return self.kld("i32.load", K_ZERO, "(i32.const 0)")

    def ke32(self, idx):
        return self.kld("i32.load", K_E32, "(i32.and (i32.shl %s (i32.const 2))"
                        " (i32.const 0xfc))" % idx)

    def ke64(self, idx):
        return self.kld("i64.load", K_E64, "(i32.and (i32.shl %s (i32.const 3))"
                        " (i32.const 0xf8))" % idx)

    def kl(self):
        self.klabn += 1
        return "q%d" % self.klabn

    def kcall(self, name, args):
        """A call of a KHELP helper: direct, which inlining may remove, or
        through the table at an index the compiler cannot know."""
        if self.rng.random() < 0.5:
            return "(call $k_%s %s)" % (name, args)
        k = [h[0] for h in KHELP].index(name)
        return ("(call_indirect (type $kt_%s) %s (i32.add (i32.const %d) %s))"
                % (name, args, self.nhelpers + k, self.kzero()))

    def kconst(self, t):
        r = self.rng
        if t == "i32":
            return "(i32.const 0x%x)" % (bigconst(r, False) if r.random() < 0.7
                                         else r.choice(EDGE32))
        if t == "i64":
            return "(i64.const 0x%x)" % (bigconst(r, True) if r.random() < 0.7
                                         else r.choice(EDGE64))
        if t == "f32":
            return "(f32.reinterpret_i32 (i32.const 0x%x))" % r.choice(F32BITS)
        return "(f64.reinterpret_i64 (i64.const 0x%x))" % r.choice(F64BITS)

    def kscr(self, t):
        """A local of the function being generated that no generic statement
        knows of, so none can write it."""
        name = "ks%d" % len(self.kextra)
        self.kextra.append((name, t))
        return name

    def klcg(self, s="s"):
        """The next word of the generator in local s, rotated so that its
        weak low bits are not the low bits."""
        return ("(i32.rotl (local.tee $%s (i32.add (i32.mul (local.get $%s)"
                " (i32.const 1103515245)) (i32.const 12345))) (i32.const %d))"
                % (s, s, self.rng.randrange(32)))

    def kin32(self, iv, s="s"):
        """An i32 that changes with the counter iv and that the compiler
        cannot know: an edge value from the table, or the generator."""
        r = self.rng
        k = r.random()
        if k < 0.45:
            return self.ke32("(i32.add (i32.mul (local.get $%s) (i32.const %d))"
                             " (i32.const %d))"
                             % (iv, r.choice([1, 3, 5, 7, 13]), r.randrange(64)))
        if k < 0.8:
            return self.klcg(s)
        if k < 0.92:
            return ("(i32.xor %s (i32.mul (local.get $%s) (i32.const 0x9e3779b1)))"
                    % (self.ke32("(local.get $%s)" % iv), iv))
        return "(global.get $kdv)"

    def kin64(self, iv, s="s"):
        r = self.rng
        k = r.random()
        if k < 0.45:
            return self.ke64("(i32.add (i32.mul (local.get $%s) (i32.const %d))"
                             " (i32.const %d))"
                             % (iv, r.choice([1, 3, 5, 7]), r.randrange(32)))
        if k < 0.7:
            return ("(i64.or (i64.shl (i64.extend_i32_u %s) (i64.const 32))"
                    " (i64.extend_i32_u %s))" % (self.klcg(s), self.klcg(s)))
        return "(i64.extend_i32_%s %s)" % (r.choice("su"), self.kin32(iv, s))

    def kfor(self, i, n, body, step=1, start=0, top=None):
        """i from start while below n, an expression read every round: tested
        at the bottom, so the body runs at least once, or at the top."""
        r = self.rng
        lab = self.kl()
        if top is None:
            top = r.random() < 0.3
        if top:
            return ("(local.set $%s (i32.const %d))\n    (block $%sx (loop $%s\n"
                    "      (br_if $%sx (i32.ge_u (local.get $%s) %s))\n      %s\n"
                    "      (local.set $%s (i32.add (local.get $%s) (i32.const %d)))\n"
                    "      (br $%s)))"
                    % (i, start, lab, lab, lab, i, n, body, i, i, step, lab))
        return ("(local.set $%s (i32.const %d))\n    (loop $%s\n      %s\n"
                "      (br_if $%s (i32.lt_u (local.tee $%s (i32.add"
                " (local.get $%s) (i32.const %d))) %s)))"
                % (i, start, lab, body, lab, i, i, step, n))

    def kh(self, v, acc="acc"):
        """Fold the i64 v into the hash acc."""
        if self.rng.random() < 0.5:
            return ("(local.set $%s (i64.add (i64.mul (local.get $%s)"
                    " (i64.const 0x100000001b3)) %s))" % (acc, acc, v))
        return ("(local.set $%s (i64.xor (i64.rotl (local.get $%s)"
                " (i64.const %d)) %s))" % (acc, acc, self.rng.randint(1, 63), v))

    def ktoi64(self, e, t):
        return {"i32": "(i64.extend_i32_u %s)", "i64": "%s",
                "f32": "(i64.trunc_sat_f32_s %s)",
                "f64": "(i64.trunc_sat_f64_s %s)"}[t] % e

    def kconv(self, e, f, t):
        """e of type f as a t, by a conversion that never traps."""
        if f == t:
            return e
        return {("i32", "i64"): "(i64.extend_i32_s %s)",
                ("i32", "f64"): "(f64.convert_i32_s %s)",
                ("i32", "f32"): "(f32.convert_i32_u %s)",
                ("i64", "i32"): "(i32.wrap_i64 %s)",
                ("i64", "f64"): "(f64.convert_i64_s %s)",
                ("i64", "f32"): "(f32.convert_i64_s %s)",
                ("f64", "i32"): "(i32.trunc_sat_f64_s %s)",
                ("f64", "i64"): "(i64.trunc_sat_f64_s %s)",
                ("f64", "f32"): "(f32.demote_f64 %s)",
                ("f32", "i32"): "(i32.trunc_sat_f32_u %s)",
                ("f32", "i64"): "(i64.trunc_sat_f32_s %s)",
                ("f32", "f64"): "(f64.promote_f32 %s)"}[(f, t)] % e

    def kcanon(self, t, name):
        """The bits of float local `name` as an i64, every NaN one pattern."""
        ity, canon, wrap = CANON[t]
        e = ("(select (%s.const %s) (%s (local.get $%s)) (%s.ne (local.get $%s)"
             " (local.get $%s)))" % (ity, canon, wrap, name, t, name, name))
        return e if ity == "i64" else "(i64.extend_i32_u %s)" % e

    def kfunc(self, idx, what, decls, body, ret="(local.get $acc)"):
        """An export of the targeted family.  s is a generator, read from and
        written back to $kseed so that each export sees other values."""
        loc = "".join("    (local $%s %s)\n" % nt
                      for nt in [("s", "i32"), ("acc", "i64")] + decls)
        return ('  (func $k%d (export "k%d") (result i64)\n    ;; %s\n%s'
                '    (local.set $s (global.get $kseed))\n    %s\n'
                '    (global.set $kseed (local.get $s))\n    %s)\n'
                % (idx, idx, what, loc, body, ret))

    def ksub(self, name, params, decls, body, ret):
        """A function an export calls, for the shapes that should also be
        seen with their inputs as parameters."""
        loc = "".join("    (local $%s %s)\n" % nt for nt in decls)
        self.kfuncs.append('  (func $%s %s (result i64)\n%s    %s\n    %s)\n'
                           % (name, " ".join("(param $%s %s)" % p
                                             for p in params), loc, body, ret))

    # argmin and argmax.  take is the relation of an element v to best that
    # makes v the new best; the select arms either take on it or keep best
    # on its negation, and either may be written with best on the left.
    def k_argmin(self, idx):
        r = self.rng
        arr = self.karray()
        sg, mx, strict = r.random() < 0.5, r.random() < 0.5, r.random() < 0.5
        swap, keep = r.random() < 0.5, r.random() < 0.5
        step = r.choice([1, 1, 1, 2, 2, 4])
        start = r.choice([0, 0, 0, 1, 3])
        off = r.choice([0, 0, 0, 1, 0xffffffff, 1000, 0xfffffff9,
                        r.getrandbits(32)])
        form = r.randrange(5)
        top = r.random() < 0.3
        inline_c = r.random() < 0.3
        at_first = r.random() < 0.5
        miss = r.choice([None] * 7 + [
            "idxcmp", "idxflip", "idxscale", "atread", "bestread", "ccount",
            "constcmp", "narrow", "backward", "i64", "samesecond",
            "bestother"])
        if miss == "samesecond" and step == 1:
            step = 2
        if miss == "backward":
            step, start, form = 1, 0, 0
        T = "i64" if miss == "i64" else "i32"
        es = 8 if T == "i64" else 4
        su = "s" if sg else "u"
        take = {(False, True): "lt", (False, False): "le",
                (True, True): "gt", (True, False): "ge"}[(mx, strict)]
        rel = NEG[take] if keep else take
        kk = r.choice(EDGE32)

        def elem(e):
            if T == "i64":
                return self.kld("i64.load", arr + 8 * e,
                                "(i32.shl (local.get $i) (i32.const 3))")
            op = "i32.load"
            if miss == "narrow":
                op = r.choice(["i32.load16_s", "i32.load8_u", "i32.load16_u"])
            sh = "(i32.shl (local.get $i) (i32.const 2))"
            if form == 0:
                return self.kld(op, arr + 4 * e, sh)
            if form == 1:
                return "(%s %s)" % (op, self.ka("(i32.add (i32.const %d) %s)"
                                                % (arr + 4 * e, sh)))
            if form == 2:
                return self.kld(op, arr + 4 * e,
                                "(i32.mul (local.get $i) (i32.const 4))")
            if form == 3:
                return self.kld(op, 4 * e, "(local.get $p)")
            return self.kld(op, 4 * e, "(i32.add (local.get $base) %s)" % sh)

        def cmp(v, rl):
            other = ("(%s.const %d)" % (T, kk) if miss == "constcmp"
                     else "(local.get $best)")
            if swap:
                return "(%s.%s_%s %s %s)" % (T, MIRROR[rl], su, other, v)
            return "(%s.%s_%s %s %s)" % (T, rl, su, v, other)

        def ix(e):
            if miss == "idxscale":
                return r.choice(["(i32.shl (local.get $i) (i32.const 1))",
                                 "(i32.mul (local.get $i) (i32.const 3))",
                                 "(i32.xor (local.get $i) (i32.const 1))"])
            o = (off + (0 if miss == "samesecond" else e)) & 0xffffffff
            if o == 0:
                return "(local.get $i)"
            if o >= 0x80000000 and r.random() < 0.6:
                return ("(i32.sub (local.get $i) (i32.const %d))"
                        % ((-o) & 0xffffffff))
            return "(i32.add (local.get $i) (i32.const %d))" % o

        def sel(new, old, c, flip=False):
            if keep != flip:
                return "(select %s %s %s)" % (old, new, c)
            return "(select %s %s %s)" % (new, old, c)

        lines = []
        for e in range(step):
            v = "(local.get $v)"
            lines.append("(local.set $v %s)" % elem(e))
            c = cmp(v, rel)
            if not inline_c:
                lines.append("(local.set $c %s)" % c)
                c = "(local.get $c)"
            atc = cmp(v, STRICTFLIP[rel]) if miss == "idxcmp" else c
            nb = "(%s.add %s (%s.const 1))" % (T, v, T) if miss == "bestother" else v
            at = "(local.set $at %s)" % sel(ix(e), "(local.get $at)", atc,
                                            miss == "idxflip")
            best = "(local.set $best %s)" % sel(nb, "(local.get $best)", c)
            lines += [at, best] if at_first else [best, at]
            if miss == "atread":
                lines.append("(local.set $q (i32.add (local.get $q) (local.get $at)))")
            if miss == "bestread":
                lines.append("(local.set $q (i32.xor (local.get $q) %s))"
                             % self.kconv("(local.get $best)", T, "i32"))
            if miss == "ccount":
                lines.append("(local.set $q (i32.add (local.get $q) %s))" % c)
        if form == 3:
            lines.append("(local.set $p (i32.add (local.get $p) (i32.const %d)))"
                         % (es * step))
        body = "\n      ".join(lines)

        ident = {("s", False): 0x7fffffff, ("s", True): 0x80000000,
                 ("u", False): 0xffffffff, ("u", True): 0}[(su, mx)]
        unbeaten = {("s", False): 0x80000000, ("s", True): 0x7fffffff,
                    ("u", False): 0, ("u", True): 0xffffffff}[(su, mx)]
        k = r.random()
        if T == "i64":
            init = "(i64.const 0x%x)" % r.choice(EDGE64)
        elif k < 0.3:
            init = "(i32.const 0x%x)" % ident
        elif k < 0.5:
            init = "(i32.const 0x%x)" % unbeaten
        elif k < 0.75:
            init = self.kld("i32.load", arr, "(i32.const %d)" % (4 * start))
        else:
            init = "(i32.const 0x%x)" % r.choice(EDGE32)
        pre = ["(local.set $best %s)" % init,
               "(local.set $at (i32.const %d))"
               % r.choice([-1, 0, 4242, r.getrandbits(31)]),
               "(local.set $q (i32.const 0))"]
        if form == 3:
            pre.append("(local.set $p (i32.const %d))" % (arr + es * start))
        if form == 4:
            pre.append("(local.set $base (i32.add (i32.const %d) %s))"
                       % (arr, self.kzero()))
        lab = self.kl()
        if miss == "backward":
            loop = ("(local.set $i (local.get $n))\n    (block $%sx (loop $%s\n"
                    "      (br_if $%sx (i32.eqz (local.get $i)))\n"
                    "      (local.set $i (i32.sub (local.get $i) (i32.const 1)))\n"
                    "      %s\n      (br $%s)))" % (lab, lab, lab, body, lab))
        elif top:
            loop = self.kfor("i", "(local.get $n)", body, step, start, True)
        else:
            nxt = "(local.tee $i (i32.add (local.get $i) (i32.const %d)))" % step
            test = r.choice(["(i32.lt_u %s (local.get $n))" % nxt,
                             "(i32.lt_s %s (local.get $n))" % nxt,
                             "(i32.gt_u (local.get $n) %s)" % nxt])
            loop = ("(local.set $i (i32.const %d))\n    (loop $%s\n      %s\n"
                    "      (br_if $%s %s))" % (start, lab, body, lab, test))
        if T == "i64":
            pack = "(i64.xor (local.get $best) (i64.extend_i32_u (local.get $at)))"
        else:
            pack = ("(i64.or (i64.shl (i64.extend_i32_u (local.get $best))"
                    " (i64.const 32)) (i64.extend_i32_u (local.get $at)))")
        pack = "(i64.xor %s (i64.extend_i32_u (local.get $q)))" % pack
        kdecl = [("i", "i32"), ("v", T), ("best", T), ("at", "i32"),
                 ("c", "i32"), ("q", "i32"), ("p", "i32"), ("base", "i32")]
        kern = "\n    ".join(pre) + "\n    " + loop
        what = "%s%s %s%s, %s compare, %s arms, %d an iteration from %d, " \
               "index +%d, address form %d%s, near miss: %s" % (
                   "un" if su == "u" else "", "signed", "max" if mx else "min",
                   " strict" if strict else "", "swapped" if swap else "plain",
                   "keep" if keep else "take", step, start, off, form,
                   ", top-tested" if top else "", miss)

        j0 = r.randrange(12)
        nr = r.randint(1, 16 - j0)
        if r.random() < 0.2:
            nsrc = "(i32.const %d)" % r.choice(LENS)
        else:
            nsrc = self.kld("i32.load", K_LEN, "(i32.shl (local.get $j) (i32.const 2))")
        if T == "i64":
            nsrc = "(i32.and %s (i32.const 255))" % nsrc
        if r.random() < 0.35:
            name = "k%d_a" % idx
            self.ksub(name, [("n", "i32")], kdecl, kern, pack)
            step_ = self.kh("(call $%s (local.get $n))" % name)
            decls = []
        else:
            step_ = kern + "\n    " + self.kh(pack)
            decls = kdecl
        body = ("(local.set $j (i32.const %d))\n    (loop $%s\n"
                "    (local.set $n %s)\n    %s\n"
                "    (br_if $%s (i32.lt_u (local.tee $j (i32.add (local.get $j)"
                " (i32.const 1))) (i32.const %d))))"
                % (j0, "aj%d" % idx, nsrc, step_, "aj%d" % idx, j0 + nr))
        return self.kfunc(idx, "argmin: " + what,
                          [("j", "i32"), ("n", "i32")] + decls, body)

    # unsigned division by a divisor the loop does not change.  The divisor
    # is guarded where it can be 0: the loop under an if, each division under
    # one, or d replaced by d ? d : 1.
    def k_invdiv(self, idx):
        r = self.rng
        src = r.choice(["table", "table", "global", "const", "outer", "outer",
                        "param", "computed"])
        guard = r.choice(["if", "if", "inner", "select"])
        if src == "const":
            guard = "none"

        def dsrc():
            if src in ("table", "param"):
                return self.kld("i32.load", K_DIV, "(i32.const %d)"
                                % (4 * r.randrange(32)))
            if src == "global":
                return "(global.get $kdv)"
            if src == "const":
                return "(i32.const 0x%x)" % r.choice(DIV32)
            if src == "outer":
                return self.kld("i32.load", K_DIV,
                                "(i32.shl (local.get $j) (i32.const 2))")
            return ("(i32.add (i32.shr_u %s (i32.const %d)) (i32.const 1))"
                    % (self.ke32("(i32.const %d)" % r.randrange(64)),
                       r.randrange(32)))

        dvd = r.choice([
            self.klcg(), "(i32.mul (local.get $i) (i32.const 0x9e3779b1))",
            "(i32.sub (i32.const -1) (local.get $i))", self.ke32("(local.get $i)"),
            "(local.get $i)", "(i32.or (i32.shl (local.get $i) (i32.const 24))"
            " (local.get $i))", "(i32.or %s (i32.const 0x80000000))" % self.klcg()])
        n64 = ["(i64.extend_i32_u (local.get $x))",
               self.kld("i64.load32_u", K_E32, "(i32.and (i32.shl (local.get $i)"
                        " (i32.const 2)) (i32.const 0xfc))"),
               "(i64.and (i64.extend_i32_s (local.get $x)) (i64.const 0xffffffff))",
               "(i64.shr_u %s (i64.const 32))" % self.ke64("(local.get $i)")]
        d64 = ["(i64.extend_i32_u (local.get $d))",
               "(i64.and (i64.extend_i32_s (local.get $d)) (i64.const 0xffffffff))"]
        wide = ["(i64.mul (i64.extend_i32_u (local.get $x)) (i64.const 0x100000001))",
                self.ke64("(local.get $i)"), "(i64.extend_i32_s (local.get $x))"]
        wided = ["(i64.or %s (i64.const 0x100000000))" % d64[0],
                 "(i64.extend_i32_s (local.get $d))"]
        ops = []
        for _ in range(r.randint(1, 3)):
            op = r.choice(["q", "q", "r", "r", "q64", "r64", "wq", "wr",
                           "sq", "sr", "q2", "r2"])
            e = {"q": "(i64.extend_i32_u (i32.div_u (local.get $x) (local.get $d)))",
                 "r": "(i64.extend_i32_u (i32.rem_u (local.get $x) (local.get $d)))",
                 "q64": "(i64.div_u %s %s)" % (r.choice(n64), r.choice(d64)),
                 "r64": "(i64.rem_u %s %s)" % (r.choice(n64), r.choice(d64)),
                 "wq": "(i64.div_u %s %s)" % (r.choice(wide), r.choice(d64)),
                 "wr": "(i64.rem_u %s %s)" % (r.choice(n64 + wide), r.choice(wided)),
                 "sq": "(i64.extend_i32_s (i32.div_s (i32.shr_u (local.get $x)"
                       " (i32.const 1)) (local.get $d)))",
                 "sr": "(i64.extend_i32_s (i32.rem_s (local.get $x) (local.get $d)))",
                 "q2": "(i64.extend_i32_u (i32.div_u (local.get $x) (local.get $e)))",
                 "r2": "(i64.extend_i32_u (i32.rem_u (local.get $x) (local.get $e)))"}[op]
            st = self.kh(e)
            if r.random() < 0.25:
                st = ("(if (i32.and (local.get $x) (i32.const %d)) (then %s))"
                      % (1 << r.randrange(32), st))
            if guard == "inner" and op not in ("q2", "r2"):
                st = "(if (local.get $d) (then %s))" % st
            ops.append(st)
        body = "(local.set $x %s)\n      %s" % (dvd, "\n      ".join(ops))
        if r.random() < 0.3:
            trip = "(i32.const %d)" % r.choice([1, 2, 3, 7, 64, 100, 300])
        else:
            trip = self.kld("i32.load", K_LEN, "(i32.const %d)" % (4 * r.randrange(16)))
        inner = ("(local.set $m %s)\n    (local.set $e (i32.add (local.get $d)"
                 " (i32.const %d)))\n    (local.set $e (select (local.get $e)"
                 " (i32.const 1) (local.get $e)))\n    %s"
                 % (trip, r.choice([1, 2, 0x7fffffff]),
                    self.kfor("i", "(local.get $m)", body)))
        if guard == "if":
            inner = "(if (local.get $d) (then\n    %s))" % inner
        if guard == "select":
            inner = ("(local.set $d (select (local.get $d) (i32.const 1)"
                     " (local.get $d)))\n    " + inner)
        decls = [("i", "i32"), ("j", "i32"), ("d", "i32"), ("e", "i32"),
                 ("x", "i32"), ("m", "i32")]
        what = "invdiv: divisor from %s, guard %s" % (src, guard)
        if src == "param":
            name = "k%d_d" % idx
            self.ksub(name, [("pd", "i32")], decls + [("s", "i32"), ("acc", "i64")],
                      "(local.set $s (global.get $kseed))\n    (local.set $d"
                      " (local.get $pd))\n    " + inner, "(local.get $acc)")
            calls = [self.kh("(call $%s %s)" % (name, x)) for x in
                     [dsrc(), "(i32.const 0x%x)" % r.choice(DIV32),
                      "(global.get $kdv)", dsrc()]]
            return self.kfunc(idx, what, [], "\n    ".join(calls))
        if src == "outer" or r.random() < 0.3:
            setd = "(local.set $d %s)\n    " % dsrc() if src == "outer" else ""
            body = ("%s(local.set $j (i32.const 0))\n    (loop $oj%d\n    %s%s\n"
                    "    (br_if $oj%d (i32.lt_u (local.tee $j (i32.add (local.get $j)"
                    " (i32.const 1))) (i32.const %d))))"
                    % ("" if src == "outer" else "(local.set $d %s)\n    " % dsrc(),
                       idx, setd, inner, idx, r.randint(1, 32 if src == "outer" else 3)))
        else:
            body = "(local.set $d %s)\n    %s" % (dsrc(), inner)
        if src != "global" and r.random() < 0.3:
            body += ("\n    (global.set $kdv (i32.add (global.get $kdv) (i32.const %d)))"
                     % r.choice([1, -1, 7]))
        return self.kfunc(idx, what, decls, body)

    def kdivrem(self, w64):
        """One shape of a division and a remainder of a and b, guarded so that
        neither can trap."""
        r = self.rng
        T = "i64" if w64 else "i32"
        a, b, bb, q, rm, t = (("A", "B", "BB", "Q", "R", "T") if w64 else
                              ("a", "b", "bb", "q", "r", "t"))
        sg = r.random() < 0.5
        su = "s" if sg else "u"
        mn = "0x8000000000000000" if w64 else "0x80000000"
        shape = r.choice(["pair", "pair", "pair", "rempair", "split",
                          "splitrev", "other", "mixed", "dup", "many", "const"])
        ok = "(%s.ne (local.get $%s) (%s.const 0))" % (T, b, T)
        if sg or shape == "mixed":
            ok = ("(i32.and %s (i32.or (%s.ne (local.get $%s) (%s.const %s))"
                  " (%s.ne (local.get $%s) (%s.const -1))))"
                  % (ok, T, a, T, mn, T, b, T))
        div = "(%s.div_%s (local.get $%s) (local.get $%s))" % (T, su, a, bb)
        rem = "(%s.rem_%s (local.get $%s) (local.get $%s))" % (T, su, a, bb)
        cond = r.choice(["(i32.and (local.get $i) (i32.const 1))",
                         "(%s.lt_s (local.get $%s) (%s.const 0))" % (T, a, T)])
        L = []
        if shape == "pair":
            L = ["(local.set $%s %s)" % (q, div), "(local.set $%s %s)" % (rm, rem)]
        elif shape == "rempair":
            L = ["(local.set $%s %s)" % (rm, rem), "(local.set $%s %s)" % (q, div)]
        elif shape == "split":
            L = ["(local.set $%s %s)" % (q, div),
                 "(if %s (then (local.set $%s %s)))" % (cond, rm, rem)]
        elif shape == "splitrev":
            L = ["(if %s (then (local.set $%s %s)))" % (cond, q, div),
                 "(local.set $%s %s)" % (rm, rem)]
        elif shape == "other":
            L = ["(local.set $%s %s)" % (q, div),
                 "(local.set $%s (%s.rem_%s (%s.add (local.get $%s) (%s.const 1))"
                 " (local.get $%s)))" % (rm, T, su, T, a, T, bb)]
        elif shape == "mixed":
            o = "u" if sg else "s"
            L = ["(local.set $%s %s)" % (q, div),
                 "(local.set $%s (%s.rem_%s (local.get $%s) (local.get $%s)))"
                 % (rm, T, o, a, bb)]
        elif shape == "dup":
            L = ["(local.set $%s %s)" % (q, div), "(local.set $%s %s)" % (t, div),
                 "(local.set $%s %s)" % (rm, rem),
                 "(local.set $%s (%s.xor (local.get $%s) (local.get $%s)))"
                 % (q, T, q, t)]
        elif shape == "many":
            L = ["(local.set $%s (%s.add (local.get $%s) (%s.div_u (%s.add"
                 " (local.get $%s) (%s.const %d)) (local.get $%s))))"
                 % (t, T, t, T, T, a, T, k, bb) for k in range(1, 10)]
            L.insert(r.randrange(len(L) + 1), "(local.set $%s %s)" % (q, div))
            L.append("(local.set $%s %s)" % (rm, rem))
        else:
            ks = [1, 2, 3, 7, 10, -2, -3, 0x7fffffff, int(mn, 16), int(mn, 16) + 1,
                  r.getrandbits(31) | 1]
            k = r.choice(ks + ([-1] if not sg else []))
            k &= (1 << (64 if w64 else 32)) - 1
            L = ["(local.set $%s (%s.div_%s (local.get $%s) (%s.const 0x%x)))"
                 % (q, T, su, a, T, k),
                 "(local.set $%s (%s.rem_%s (local.get $%s) (%s.const 0x%x)))"
                 % (rm, T, su, a, T, k)]
        L.append(self.kh(self.ktoi64("(local.get $%s)" % q, T)))
        L.append(self.kh(self.ktoi64("(local.get $%s)" % rm, T)))
        if shape == "const":
            return "\n      ".join(L), shape, su
        if r.random() < 0.5:
            return ("(if %s (then\n      (local.set $%s (local.get $%s))\n      %s))"
                    % (ok, bb, b, "\n      ".join(L)), shape, su)
        return ("(local.set $%s (select (local.get $%s) (%s.const 1) %s))\n      %s"
                % (bb, b, T, ok, "\n      ".join(L)), shape, su)

    def k_divrem(self, idx):
        r = self.rng
        ni, nj = r.randint(1, 16), r.randint(1, 16)
        shapes, what = [], []
        for _ in range(r.randint(1, 3)):
            w64 = r.random() < 0.4
            text, shape, su = self.kdivrem(w64)
            shapes.append(text)
            what.append("%s %d-bit %s" % ({"s": "signed", "u": "unsigned"}[su],
                                          64 if w64 else 32, shape))
        def pick(i, w64):
            if r.random() < 0.2:
                return self.kin64(i) if w64 else self.kin32(i)
            p, q = r.choice([1, 3, 5, 7]), r.randrange(64)
            e = "(i32.add (i32.mul (local.get $%s) (i32.const %d)) (i32.const %d))" % (i, p, q)
            return self.ke64(e) if w64 else self.ke32(e)
        load = ["(local.set $a %s)" % pick("i", False),
                "(local.set $b %s)" % pick("j", False),
                "(local.set $A %s)" % pick("i", True),
                "(local.set $B %s)" % pick("j", True)]
        inner = "\n      ".join(load + shapes)
        body = ("(local.set $i (i32.const 0))\n    (loop $di%d\n    %s\n"
                "    (br_if $di%d (i32.lt_u (local.tee $i (i32.add (local.get $i)"
                " (i32.const 1))) (i32.const %d))))"
                % (idx, self.kfor("j", "(i32.const %d)" % nj, inner, top=False),
                   idx, ni))
        decls = [(n, "i32") for n in ["i", "j", "a", "b", "bb", "q", "r", "t"]]
        decls += [(n, "i64") for n in ["A", "B", "BB", "Q", "R", "T"]]
        return self.kfunc(idx, "divrem: " + "; ".join(what), decls, body)

    def kxs(self, x, y, w64):
        """x ^= x >> k twice, or a near miss of it, into y; returns the
        statements and the value to fold in."""
        r = self.rng
        T = "i64" if w64 else "i32"
        w = 64 if w64 else 32
        m = (1 << w) - 1
        k1 = r.choice([w // 2 - 1, w // 2, w // 2, w // 2 + 1, w - 1,
                       w // 2 + w, 2 * w - 1, r.randrange(w), r.randrange(2 * w),
                       (-(w // 2)) & m, r.getrandbits(w)])
        op1 = r.choice(["shr_u"] * 6 + ["shl"] * 3 + ["shr_s"])
        op2 = op1 if r.random() < 0.8 else r.choice(["shr_u", "shl", "shr_s"])
        k = r.random()
        k2 = (k1 if k < 0.6 else (k1 + w) & m if k < 0.75 else
              (k1 - w) & m if k < 0.8 else
              r.choice([w // 2, w // 2 - 1, (k1 + 1) & m]))

        def step(v, op, kk):
            sh = "(%s.%s (local.get $%s) (%s.const 0x%x))" % (T, op, v, T, kk)
            if r.random() < 0.5:
                return "(%s.xor (local.get $%s) %s)" % (T, v, sh)
            return "(%s.xor %s (local.get $%s))" % (T, sh, v)
        L = ["(local.set $%s %s)" % (y, step(x, op1, k1))]
        folds = []
        if r.random() < 0.25:
            folds.append(self.ktoi64("(local.get $%s)" % y, T))
        L.append("(local.set $%s %s)" % (y + "z", step(y, op2, k2)))
        last = y + "z"
        if r.random() < 0.15:
            L.append("(local.set $%s %s)" % (y + "w", step(last, op2,
                                                            r.choice([k1, k2]))))
            last = y + "w"
        folds.append(self.ktoi64("(local.get $%s)" % last, T))
        what = "%s %s %d/%s %d" % (T, op1, k1 % w, op2, k2 % w)
        return L, folds, what, last

    def k_xorshift(self, idx):
        r = self.rng
        lines, what, decls = [], [], []
        for v in range(r.randint(1, 4)):
            w64 = r.random() < 0.4
            T = "i64" if w64 else "i32"
            x, y = "x%d" % v, "y%d" % v
            decls += [(x, T), (y, T), (y + "z", T), (y + "w", T)]
            lines.append("(local.set $%s %s)" % (x, self.kin64("i") if w64
                                                   else self.kin32("i")))
            L, folds, wh, _ = self.kxs(x, y, w64)
            lines += L + [self.kh(f) for f in folds]
            what.append(wh)
        n = r.choice([1, 7, 64, 200])
        body = self.kfor("i", "(i32.const %d)" % n, "\n      ".join(lines))
        return self.kfunc(idx, "xorshift: " + ", ".join(what),
                          decls + [("i", "i32")], body)

    def k_fbits(self, idx):
        r = self.rng
        F, I = ("f32", "i32") if r.random() < 0.3 else ("f64", "i64")
        bits = F32BITS if F == "f32" else F64BITS

        def fb():
            return "(%s.reinterpret_%s (%s.const 0x%x))" % (F, I, I, r.choice(bits))

        def fc():
            return fb() if r.random() < 0.6 else "(local.get $c%d)" % r.randrange(3)
        pre = ["(local.set $c%d %s)" % (k, fb()) for k in range(3)]
        pre.append("(local.set $f %s)" % r.choice([fb(), "(%s.const 0)" % F]))
        xin = r.choice([
            "(%s.convert_i32_s %s)" % (F, self.kin32("i")),
            self.kconv(self.kld("f64.load", K_F64, "(i32.and (i32.shl (local.get $i)"
                                " (i32.const 3)) (i32.const 0x38))"), "f64", F),
            "(%s.mul (%s.convert_i32_u (local.get $i)) %s)" % (F, F, fc())])
        L = ["(local.set $x %s)" % xin]
        for _ in range(r.randint(1, 4)):
            op = r.choice(["add", "sub", "mul", "div", "min", "max"])
            inner = "(%s.%s (local.get $x) %s)" % (F, r.choice(["add", "mul", "sub"]), fc())
            if r.random() < 0.3:
                inner = "(%s.%s %s)" % (F, r.choice(["sqrt", "abs", "neg", "floor",
                                                     "ceil", "nearest", "trunc"]), inner)
            L.append("(local.set $f (%s.%s (local.get $f) %s))" % (F, op, inner))
        L.append(self.kh(self.ktoi64("(local.get $f)", F)))
        # start again from x once f is a NaN or huge, or every round after
        # would be the same
        L.append("(if (i32.or (%s.ne (local.get $f) (local.get $f)) (%s.gt (%s.abs"
                 " (local.get $f)) (%s.const 1e30))) (then (local.set $f (local.get $x))))"
                 % (F, F, F, F))
        if r.random() < 0.6:
            L.append(self.kh("(i64.extend_i32_u (%s.%s (local.get $f) %s))"
                             % (F, r.choice(["lt", "le", "eq", "ne", "gt"]), fc())))
        if r.random() < 0.5:
            v = r.choice(F64FIN if F == "f64" else F32FIN)
            if F == "f64":
                fl = struct.unpack("<d", struct.pack("<Q", v))[0]
                e = "(i64.reinterpret_f64 (f64.const %s))" % float.hex(fl)
            else:
                fl = struct.unpack("<f", struct.pack("<I", v))[0]
                e = "(i64.extend_i32_u (i32.reinterpret_f32 (f32.const %s)))" % float.hex(fl)
            L.append(self.kh("(i64.xor %s (i64.extend_i32_u (local.get $i)))" % e))
        n = r.choice([1, 5, 16, 64, 200])
        body = "\n    ".join(pre) + "\n    " + self.kfor(
            "i", "(i32.const %d)" % n, "\n      ".join(L))
        return self.kfunc(idx, "fbits: %s" % F,
                          [("i", "i32"), ("x", F), ("f", F), ("c0", F), ("c1", F),
                           ("c2", F)], body,
                          "(i64.xor (local.get $acc) %s)" % self.kcanon(F, "f"))

    def kcmp(self, w):
        """A compare of the selects kernel's operands: a, b (i32), A, B (i64),
        x, y (f64), or a against a constant."""
        r = self.rng
        if w == "i32":
            return "(i32.%s (local.get $a) (local.get $b))" % r.choice(ICMP)
        if w == "imm":
            return "(i32.%s (local.get $a) (i32.const %d))" % (
                r.choice(ICMP), r.choice([0, 1, 5, -1, 0xfff, 0x1000, 0x12345]))
        if w == "eqz":
            return "(i32.eqz (local.get $a))"
        if w == "i64":
            return "(i64.%s (local.get $A) (local.get $B))" % r.choice(ICMP)
        return "(f64.%s (local.get $x) (local.get $y))" % r.choice(
            ["eq", "ne", "lt", "le", "gt", "ge"])

    def ksel(self, c):
        r = self.rng
        t = r.choice(["i32", "i32", "i32", "i64", "f64"])
        if t == "i32":
            k = r.getrandbits(32) - 1
            a1, a2 = r.choice([("(local.get $a)", "(local.get $b)"),
                               ("(i32.const 1)", "(i32.const 0)"),
                               ("(i32.const 0)", "(i32.const 1)"),
                               ("(i32.const -1)", "(i32.const 0)"),
                               ("(i32.const 0)", "(i32.const -1)"),
                               ("(i32.const %d)" % (k + 1), "(i32.const %d)" % k),
                               ("(i32.const %d)" % k, "(i32.const %d)" % (k + 1)),
                               ("(local.get $a)", "(i32.const 9)"),
                               ("(i32.const 1000)", "(local.get $b)")])
            if r.random() < 0.2:
                return ("(local.set $r (select (i32.add (local.get $r) (i32.const 1))"
                        " (local.get $r) %s))" % c)
            if r.random() < 0.15:
                return ("(local.set $r (i32.rotl (local.get $r) (select (i32.const 3)"
                        " (i32.const 7) %s)))" % c)
            return ("(local.set $r (i32.%s (local.get $r) (select %s %s %s)))"
                    % (r.choice(["add", "xor", "sub"]), a1, a2, c))
        if t == "i64":
            a1, a2 = r.choice([("(local.get $A)", "(local.get $B)"),
                               ("(i64.const 3)", "(i64.const 0x100000000)"),
                               ("(i64.const 1)", "(i64.const 0)"),
                               ("(i64.const -1)", "(i64.const 0)"),
                               ("(i64.extend_i32_s (local.get $a))", "(i64.const -1)")])
            return ("(local.set $R (i64.add (local.get $R) (select %s %s %s)))"
                    % (a1, a2, c))
        # each one on its own: a NaN in a running sum would hide the rest
        a1, a2 = r.choice([("(local.get $x)", "(f64.const 0.5)"),
                           ("(local.get $y)", "(local.get $x)")])
        return ("(local.set $R (i64.add (local.get $R) (i64.trunc_sat_f64_s"
                " (select %s %s %s))))" % (a1, a2, c))

    def kbetween(self):
        """Something between two selects on one compare."""
        r = self.rng
        lab = self.kl()
        return r.choice([
            "",
            "(local.set $d %s)\n      (local.set $r (i32.xor (local.get $r) (select"
            " (i32.const 77) (i32.const 5) (local.get $d))))" % self.kcmp(
                r.choice(["i32", "imm", "i64", "f64"])),
            "(local.set $r (i32.add (local.get $r) %s))" % self.kcmp(
                r.choice(["i32", "imm", "i64", "f64", "eqz"])),
            "(local.set $r (i32.add (local.get $r) %s))"
            % self.kcall("id", "(local.get $b)"),
            "(local.set $r (i32.add (local.get $r) %s))"
            % self.kcall("clob", "(local.get $a)"),
            "(local.set $r (i32.add (local.get $r) (i32.div_s (i32.shr_s (local.get $a)"
            " (i32.const 1)) (i32.or (local.get $b) (i32.const 1)))))",
            "(local.set $r (i32.add (local.get $r) (i32.rem_s (local.get $b)"
            " (i32.or (local.get $a) (i32.const 1)))))",
            "(local.set $r (i32.add (local.get $r) (i32.div_u (local.get $a)"
            " (i32.or (local.get $b) (i32.const 1)))))",
            "(local.set $j (i32.const 0))\n      (loop $%s (local.set $r (i32.add"
            " (local.get $r) (i32.mul (local.get $j) (local.get $j))))\n"
            "        (br_if $%s (i32.lt_u (local.tee $j (i32.add (local.get $j)"
            " (i32.const 1))) (i32.and (local.get $a) (i32.const 7)))))" % (lab, lab),
            "(local.set $r (i32.xor (local.get $r) %s))"
            % self.kld("i32.load", K_SCR, "(i32.and (local.get $a) (i32.const 0xfc))"),
            self.kst("i32.store", K_SCR, "(i32.and (local.get $b) (i32.const 0xe0))",
                     "(local.get $a)"),
            "(global.set $kseed (i32.add (global.get $kseed) (local.get $a)))",
            "(local.set $F (f64.add (local.get $F) (local.get $x)))",
            "(local.set $R (i64.add (local.get $R) (i64.mul (local.get $A)"
            " (local.get $B))))"])

    def k_selects(self, idx):
        r = self.rng
        L = ["(local.set $a %s)" % self.kin32("i"),
             "(local.set $b %s)" % r.choice([
                 self.kin32("i"), self.kin32("i"), "(local.get $a)",
                 "(i32.add (local.get $a) (i32.const 1))", "(i32.const 5)"]),
             "(local.set $A %s)" % r.choice([self.kin64("i"),
                                             "(i64.extend_i32_s (local.get $a))"]),
             "(local.set $B %s)" % r.choice([self.kin64("i"),
                                             "(i64.extend_i32_u (local.get $b))",
                                             "(local.get $A)"]),
             "(local.set $x (f64.convert_i32_s (local.get $a)))",
             "(local.set $y (f64.convert_i32_s (local.get $b)))"]
        if r.random() < 0.5:
            L.append("(if (i32.eqz (i32.and (local.get $i) (i32.const 7)))"
                     " (then (local.set $y (f64.div (f64.const 0) (f64.const 0)))))")
        w = r.choice(["i32", "i32", "imm", "eqz", "i64", "f64"])
        L.append("(local.set $c %s)" % self.kcmp(w))
        c = "(local.get $c)"
        what = [w]
        for _ in range(r.randint(1, 3)):
            L.append(self.ksel(c))
            for _ in range(r.randint(0, 2)):
                b = self.kbetween()
                if b:
                    L.append(b)
            L.append(self.ksel(c))
        if r.random() < 0.4:
            L.append("(if %s (then %s) (else %s))" % (c, self.ksel(c), self.ksel(c)))
            what.append("branch")
        if r.random() < 0.3:
            L.append("(local.set $r (i32.add (local.get $r) %s))" % c)
        if r.random() < 0.6:
            L.append(self.ksucc())
            what.append("successor")
        n = r.choice([16, 64, 200])
        body = self.kfor("i", "(i32.const %d)" % n, "\n      ".join(L))
        decls = [(v, "i32") for v in "i j a b c d r".split()]
        decls += [("A", "i64"), ("B", "i64"), ("R", "i64"), ("x", "f64"),
                  ("y", "f64"), ("F", "f64")]
        return self.kfunc(idx, "selects: " + ", ".join(what), decls, body,
                          "(i64.xor (i64.extend_i32_u (local.get $r)) (i64.xor"
                          " (local.get $R) (i64.trunc_sat_f64_s (local.get $F))))")

    def ksucc(self):
        """The operands of a branch's compare compared again in the block
        after it: by another predicate, the other way round, against the same
        constant -- and once in a block a jump from elsewhere also reaches."""
        r = self.rng
        w = r.choice(["i32", "i32", "imm", "i64"])
        if w == "imm":
            k = r.choice([0, 5, -1, 0xfff, 0x12345])
            a, b, T = "(local.get $a)", "(i32.const %d)" % k, "i32"
        elif w == "i64":
            a, b, T = "(local.get $A)", "(local.get $B)", "i64"
        else:
            a, b, T = "(local.get $a)", "(local.get $b)", "i32"
        p1 = r.choice(ICMP)
        p2 = r.choice(ICMP)
        c1 = "(%s.%s %s %s)" % (T, p1, a, b)
        c2 = ("(%s.%s %s %s)" % (T, p2, b, a) if w != "imm" and r.random() < 0.4
              else "(%s.%s %s %s)" % (T, p2, a, b))
        k = r.randrange(4)
        if k == 0:
            return ("(if %s (then (local.set $r (i32.add (local.get $r) (i32.const 3))))"
                    " (else (if %s (then (local.set $r (i32.xor (local.get $r)"
                    " (i32.const 5)))) (else (local.set $r (i32.sub (local.get $r)"
                    " (i32.const 7)))))))" % (c1, c2))
        if k == 1:
            return ("(if %s (then (local.set $r (i32.add (local.get $r) (i32.const 3)))))"
                    "\n      (local.set $r (i32.add (local.get $r) (select (i32.const 11)"
                    " (i32.const 13) %s)))" % (c1, c2))
        if k == 2:
            lab = self.kl()
            return ("(block $%s\n        (br_if $%s (i32.eqz (i32.and (local.get $b)"
                    " (i32.const 3))))\n        (if %s (then (local.set $r (i32.add"
                    " (local.get $r) (i32.const 3))))))\n      (local.set $r (i32.add"
                    " (local.get $r) (select (i32.const 11) (i32.const 13) %s)))"
                    % (lab, lab, c1, c2))
        lab = self.kl()
        return ("(block $%s\n        (br_if $%s %s)\n        (local.set $r (i32.add"
                " (local.get $r) (select (local.get $a) (i32.const 17) %s))))"
                % (lab, lab, c1, c2))

    def k_bigconst(self, idx):
        r = self.rng
        B32 = lambda: "(i32.const 0x%x)" % bigconst(r, False)
        B64 = lambda: "(i64.const 0x%x)" % bigconst(r, True)
        nlive = r.randint(0, 12)
        pre = ["(local.set $v%d %s)" % (k, B64()) for k in range(nlive)]
        L = ["(local.set $x %s)" % self.kin32("i"),
             "(local.set $c (i32.and (local.get $x) (i32.const %d)))"
             % (1 << r.randrange(8))]
        what = []
        for _ in range(r.randint(2, 5)):
            shape = r.choice(["args", "mix", "edges", "edges32", "brif", "brtable",
                              "across", "header", "cmpimm", "store", "live"])
            what.append(shape)
            b1 = B64()
            if shape == "args":
                L.append(self.kh(self.kcall("big", "%s %s (local.get $x) %s"
                                            % (B32(), B64(), B64()))))
            elif shape == "mix":
                L.append("(local.set $acc %s)" % self.kcall(
                    "mix", "(i64.xor (local.get $acc) %s) %s" % (b1, B32())))
            elif shape == "edges":
                b2 = b1 if r.random() < 0.3 else B64()
                L.append(self.kh("(if (result i64) (local.get $c) (then %s) (else %s))"
                                 % (b1, b2)))
            elif shape == "edges32":
                k = B32()
                L.append("(local.set $h (i32.xor (local.get $h) (if (result i32)"
                         " (local.get $c) (then %s) (else (i32.add %s (local.get $x))))))"
                         % (k, k))
            elif shape == "brif":
                lab = self.kl()
                L.append(self.kh("(block $%s (result i64) (drop (br_if $%s %s"
                                 " (local.get $c))) (drop %s) %s)"
                                 % (lab, lab, b1, self.kcall("id", "(local.get $x)"),
                                    r.choice([b1, B64()]))))
            elif shape == "brtable":
                l1, l2, l3 = self.kl(), self.kl(), self.kl()
                L.append(self.kh(
                    "(block $%s (result i64)\n        (drop (block $%s (result i64)\n"
                    "          (drop (block $%s (result i64)\n            (br_table"
                    " $%s $%s $%s %s (i32.and (local.get $x) (i32.const 3)))))\n"
                    "          (br $%s %s)))\n        %s)"
                    % (l3, l2, l1, l1, l2, l3, b1, l3, r.choice([b1, B64()]), B64())))
            elif shape == "across":
                L += [self.kh(b1), "(local.set $h (i32.add (local.get $h) %s))"
                      % self.kcall("clob", "(local.get $x)"), self.kh(b1)]
            elif shape == "header":
                lab = self.kl()
                L += ["(local.set $k %s)" % b1, "(local.set $j (i32.const 0))",
                      "(loop $%s\n        %s\n        (local.set $k (i64.add"
                      " (local.get $k) %s))\n        (br_if $%s (i32.lt_u (local.tee $j"
                      " (i32.add (local.get $j) (i32.const 1))) (i32.const 3))))"
                      % (lab, self.kh("(i64.xor (local.get $k) %s)" % B64()), B64(), lab)]
            elif shape == "cmpimm":
                L.append("(local.set $h (i32.add (local.get $h) (select %s %s"
                         " (i32.%s (local.get $x) %s))))"
                         % (B32(), B32(), r.choice(ICMP), B32()))
            elif shape == "store":
                a = "(i32.and (local.get $x) (i32.const 0x78))"
                L += [self.kst("i64.store", K_SCR, a, b1),
                      self.kh(self.kld("i64.load", K_SCR, a))]
            else:
                if not nlive:
                    continue
                L.append(self.kh(self.kcall("mix", "(local.get $v%d) (local.get $x)"
                                            % r.randrange(nlive))))
                L.append(self.kh("(local.get $v%d)" % r.randrange(nlive)))
        for k in range(nlive):
            if r.random() < 0.5:
                L.insert(r.randrange(len(L) + 1),
                         self.kh("(i64.xor (local.get $v%d) (local.get $acc))" % k))
        n = r.choice([1, 20, 100])
        body = "\n    ".join(pre) + "\n    " + self.kfor(
            "i", "(i32.const %d)" % n, "\n      ".join(L))
        decls = [(v, "i32") for v in "i j x c h".split()] + [("k", "i64")]
        decls += [("v%d" % k, "i64") for k in range(nlive)]
        return self.kfunc(idx, "bigconst: " + ", ".join(what), decls, body,
                          "(i64.xor (local.get $acc) (i64.extend_i32_u (local.get $h)))")

    def kpexpr(self, names, types, t):
        """A value of type t from one or two of the earlier values."""
        r = self.rng
        a = r.randrange(len(names))
        e1 = self.kconv("(local.get $%s)" % names[a], types[a], t)
        if r.random() < 0.15:
            e2 = self.kconst(t) if t in ("i32", "i64") else "(%s.const %s)" % (
                t, r.choice(["0.5", "1.25", "-3.0", "1e10"]))
        else:
            b = r.randrange(len(names))
            e2 = self.kconv("(local.get $%s)" % names[b], types[b], t)
        if t in ("f32", "f64"):
            op = r.choice(["add", "sub", "mul", "min", "max"])
        else:
            op = r.choice(["add", "sub", "mul", "xor", "and", "or", "rotl",
                           "shl", "shr_u"])
        return "(%s.%s %s %s)" % (t, op, e1, e2)

    def k_pressure(self, idx):
        r = self.rng
        ty = lambda: r.choice(["i32", "i32", "i64", "i64", "f64", "f32"])
        if r.random() < 0.5:
            K = r.randint(30, 70)
            types = [ty() for _ in range(K)]
            names = ["v%d" % k for k in range(K)]
            L = []
            for k in range(K):
                if k < 2:
                    e = self.kconv(self.kin32("i") if k == 0 else self.kin64("i"),
                                   "i32" if k == 0 else "i64", types[k])
                elif r.random() < 0.06:
                    a, b = r.randrange(k), r.randrange(k)
                    e = self.kconv(self.kcall("mix", "%s %s" % (
                        self.kconv("(local.get $%s)" % names[a], types[a], "i64"),
                        self.kconv("(local.get $%s)" % names[b], types[b], "i32"))),
                        "i64", types[k])
                else:
                    lo = max(0, k - r.choice([3, 8, k]))
                    e = self.kpexpr(names[lo:k], types[lo:k], types[k])
                L.append("(local.set $%s %s)" % (names[k], e))
            order = list(range(K))
            r.shuffle(order)
            for _ in range(r.randint(0, 6)):
                k = r.randrange(len(order))
                order[k + 1:k + 1] = [order[k]] * r.randint(1, 3)
            L += [self.kh(self.ktoi64("(local.get $%s)" % names[k], types[k]))
                  for k in order]
            n = r.choice([1, 1, 3])
            body = self.kfor("i", "(i32.const %d)" % n, "\n      ".join(L))
            what = "%d values in a row" % K
        else:
            K = r.randint(16, 40)
            types = [ty() for _ in range(K)]
            names = ["v%d" % k for k in range(K)]
            pre = ["(local.set $%s %s)" % (names[k], self.kconv(
                self.kin32("i"), "i32", types[k])) for k in range(K)]
            L = []
            for k in range(K):
                p = r.randrange(K)
                e = self.kpexpr([names[k], names[p]], [types[k], types[p]], types[k])
                L.append("(local.set $%s %s)" % (names[k], e))
            if r.random() < 0.5:
                k = r.randrange(K)
                L.insert(r.randrange(len(L) + 1),
                         "(if (i32.eq (i32.and (local.get $i) (i32.const 15))"
                         " (i32.const %d)) (then (local.set $%s %s)))"
                         % (r.randrange(16), names[k], self.kconv(self.kcall(
                             "mix", "%s (local.get $i)" % self.kconv(
                                 "(local.get $%s)" % names[k], types[k], "i64")),
                             "i64", types[k])))
            if r.random() < 0.4:
                k = r.randrange(K)
                lab = self.kl()
                L.insert(r.randrange(len(L) + 1),
                         "(local.set $j (i32.const 0))\n      (loop $%s (local.set $%s"
                         " %s)\n        (br_if $%s (i32.lt_u (local.tee $j (i32.add"
                         " (local.get $j) (i32.const 1))) (i32.const 3))))"
                         % (lab, names[k], self.kpexpr([names[k], names[(k + 1) % K]],
                                                       [types[k], types[(k + 1) % K]],
                                                       types[k]), lab))
            n = r.randint(2, 64)
            body = ("\n    ".join(pre) + "\n    " +
                    self.kfor("i", "(i32.const %d)" % n, "\n      ".join(L)) +
                    "\n    " + "\n    ".join(
                        self.kh(self.ktoi64("(local.get $%s)" % names[k], types[k]))
                        for k in range(K)))
            what = "%d values round a loop" % K
        return self.kfunc(idx, "pressure: " + what,
                          [("i", "i32"), ("j", "i32")] + list(zip(names, types)), body)

    def krare(self, iv):
        r = self.rng
        m = r.choice([7, 15, 63, 255])
        return r.choice([
            "(i32.eq (i32.and (local.get $%s) (i32.const %d)) (i32.const %d))"
            % (iv, m, r.randrange(m + 1)),
            "(global.get $kflag)",
            "(i32.gt_u (local.get $a) (i32.const 0xfff00000))"])

    def k_coldcall(self, idx):
        r = self.rng
        vals = [("a", "i32"), ("b", "i64"), ("c", "i32"), ("d", "f64"),
                ("e", "i64")] + [("w%d" % k, r.choice(["i32", "i64", "f64"]))
                                 for k in range(r.randint(0, 8))]
        names = [n for n, _ in vals]
        types = [t for _, t in vals]
        pre = ["(local.set $%s %s)" % (n, self.kconv(self.kin32("i"), "i32", t))
               for n, t in vals]

        def call():
            k = r.randrange(len(vals))
            n, t = vals[k]
            c = r.choice([
                ("mix", "%s %s" % (self.kconv("(local.get $b)", "i64", "i64"),
                                   "(local.get $a)"), "i64"),
                ("clob", "(local.get $c)", "i32"),
                ("fm", "(local.get $d) (local.get $a)", "f64"),
                ("big", "(local.get $a) (local.get $b) (local.get $c) (local.get $e)",
                 "i64")])
            return "(local.set $%s %s)" % (n, self.kconv(self.kcall(c[0], c[1]),
                                                         c[2], t))

        def upd():
            k = r.randrange(len(vals))
            return "(local.set $%s %s)" % (names[k], self.kpexpr(
                names + ["i"], types + ["i32"], types[k]))
        top = []
        if r.random() < 0.6:
            top.append("(if %s (then %s))" % (self.krare("a"), call()))
        L = [upd() for _ in range(r.randint(1, 4))]
        L.insert(r.randrange(len(L) + 1), "(if %s (then %s))" % (self.krare("i"), call()))
        L += [upd() for _ in range(r.randint(1, 4))]
        n = r.choice([16, 100, 300])
        body = ("\n    ".join(pre + top) + "\n    " +
                self.kfor("i", "(i32.const %d)" % n, "\n      ".join(L)))
        if r.random() < 0.5:
            body += "\n    (if %s (then %s))" % (self.krare("a"), call())
        body += "\n    " + "\n    ".join(self.kh(self.ktoi64("(local.get $%s)" % n, t))
                                         for n, t in vals)
        return self.kfunc(idx, "coldcall: %d values" % len(vals),
                          [("i", "i32")] + vals, body)

    def k_trapguard(self, idx):
        r = self.rng
        form = r.choice(["if", "if", "shared", "exit"])
        guards = ["(local.get $f)", "(i32.ne (local.get $f) (i32.const 0))",
                  "(global.get $kflag)",
                  "(i32.gt_u (local.get $n) (i32.const 0x7fffffff))",
                  "(i64.gt_u (local.get $g) (i64.const 0xffffffffff))",
                  "(i32.lt_s (local.get $n) (i32.const 0))",
                  "(i32.and (local.get $f) (local.get $n))",
                  "(i32.ne (local.get $f) %s)" % self.kzero()]
        guard = r.choice(guards)
        a0 = "(i32.const 0)"
        reads = r.sample([
            self.kst("i32.store", K_SCR + 0x80, a0, "(local.get $i)"),
            self.kst("i64.store", K_SCR + 0x88, a0, "(local.get $acc)"),
            self.kst("i32.store", K_SCR + 0x90, a0,
                     "(i32.add (local.get $i) (i32.const 1))"),
            "(drop %s)" % self.kcall("id", "(local.get $i)"),
            self.kst("i32.store", K_SCR + 0x94, a0, "(local.get $t)")],
            r.randint(1, 3))
        trap = "\n        ".join(reads + ["(unreachable)"])
        pre_t = ("(local.set $t (i32.mul (local.get $i) (i32.const 3)))\n      "
                 if r.random() < 0.3 else "")
        body = [self.kh(self.kld("i64.load32_u", K_E32, "(i32.and (i32.shl"
                                 " (local.get $i) (i32.const 2)) (i32.const 0xfc))"))]
        if r.random() < 0.4:
            body.append(self.kst("i32.store", K_SCR, "(i32.and (i32.shl (local.get $i)"
                                 " (i32.const 2)) (i32.const 0x3c))", "(local.get $i)"))
        if r.random() < 0.3:
            body.append("(if (i32.ge_u (local.get $i) (i32.const 100000))"
                        " (then (unreachable)))")
        if r.random() < 0.2:
            body.append(self.kh("(i64.extend_i32_u %s)"
                                % self.kcall("id", "(local.get $i)")))
        body = "\n      ".join(body)
        if r.random() < 0.3:
            trip = "(i32.const %d)" % r.choice([1, 2, 7, 300])
        else:
            trip = self.kld("i32.load", K_LEN, "(i32.const %d)" % (4 * r.randrange(16)))
        start = r.choice([0, 0, 1, 5])
        lab = self.kl()
        pre = ("(local.set $f %s)\n    (local.set $n %s)\n    (local.set $g"
               " (i64.extend_i32_u (local.get $n)))\n    (local.set $i (i32.const %d))\n    "
               % (r.choice(["(global.get $kflag)", self.kzero()]), trip, start))
        latch = ("(br_if $%s (i32.lt_u (local.tee $i (i32.add (local.get $i)"
                 " (i32.const 1))) (local.get $n)))" % lab)
        if form == "if":
            loop = ("(loop $%s\n      %s(if %s (then\n        %s))\n      %s\n      %s)"
                    % (lab, pre_t, guard, trap, body, latch))
        elif form == "shared":
            loop = ("(block $%sok\n    (block $%strap\n    (loop $%s\n      %s(br_if"
                    " $%strap %s)\n      %s\n      %s)\n    (br $%sok))\n    %s)"
                    % (lab, lab, lab, pre_t, lab, guard, body, latch, lab,
                       trap.replace("\n        ", "\n    ")))
        else:
            g2 = r.choice(["(i32.gt_u (local.get $n) (i32.const %d)" % r.choice([0, 7, 100, 300])
                           + ")", guard, "(i32.eqz (local.get $n))"])
            loop = ("(local.set $acc (i64.add (local.get $acc) (i64.extend_i32_s"
                    " (block $%sx (result i32)\n    (loop $%s\n      %s(drop (br_if $%sx"
                    " (local.get $i) %s))\n      %s\n      %s)\n    (i32.const -1)))))"
                    % (lab, lab, pre_t, lab, g2, body, latch))
        decls = [("i", "i32"), ("n", "i32"), ("f", "i32"), ("t", "i32"), ("g", "i64")]
        return self.kfunc(idx, "trapguard: %s, guard %s" % (form, guard), decls,
                          pre + loop)

    def k_loopshape(self, idx):
        r = self.rng
        parts, what = [], []
        for _ in range(r.randint(1, 3)):
            kind = r.choice(["rotate", "postdec", "thread", "tiny"])
            what.append(kind)
            parts.append(getattr(self, "kls_" + kind)())
        if r.random() < 0.5:
            n = r.choice([1, 4, 16, 40])
            body = self.kfor("o", "(i32.const %d)" % n, "\n      ".join(parts))
        else:
            body = "(local.set $o (i32.const %d))\n    " % r.randrange(64) + \
                "\n    ".join(parts)
        decls = [(v, "i32") for v in "o i j m x t n h".split()]
        decls += [("T", "i64"), ("N", "i64")]
        return self.kfunc(idx, "loopshape: " + ", ".join(what), decls, body,
                          "(i64.xor (local.get $acc) (i64.extend_i32_u (local.get $h)))")

    def kls_rotate(self):
        """A loop tested at the top, its body touching memory: the shape
        rotation copies the test into the latch of."""
        r = self.rng
        lab = self.kl()
        step = r.choice([1, 1, 2, 3])
        exit_ = r.choice(["ge", "ge", "eq", "count"])
        if step != 1 and exit_ == "eq":
            exit_ = "ge"
        test = {"ge": "(i32.ge_u (local.get $i) (local.get $m))",
                "eq": "(i32.eq (local.get $i) (local.get $m))",
                "count": "(i32.eqz (local.get $n))"}[exit_]
        body = ["(local.set $x %s)" % self.ke32(
                    "(i32.add (local.get $i) (local.get $o))"),
                "(local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))"
                " (local.get $x)))"]
        if exit_ == "count":
            body.insert(0, "(local.set $n (i32.sub (local.get $n) (i32.const 1)))")
        if r.random() < 0.4:
            body.append(self.kst("i32.store", K_SCR, "(i32.and (i32.shl (local.get $i)"
                                 " (i32.const 2)) (i32.const 0x7c))", "(local.get $h)"))
        cont = ""
        if r.random() < 0.3:
            cont = "(br_if $%sc (i32.and (local.get $x) (i32.const %d)))\n        " % (
                lab, r.choice([1, 2, 12]))
            body = ["(block $%sc\n        %s\n        %s(local.set $h (i32.xor"
                    " (local.get $h) (local.get $i))))"
                    % (lab, "\n        ".join(body), cont)]
        latch = r.choice(["(br $%s)" % lab, "(br $%s)" % lab,
                          "(br_if $%s (i32.lt_u (local.get $i) (local.get $m)))" % lab,
                          "(br_if $%s (i32.ne (i32.and (local.get $x) (i32.const 15))"
                          " (i32.const 15)))" % lab])
        return ("(local.set $m %s)\n    (local.set $n (local.get $m))\n"
                "    (local.set $i (i32.const 0))\n    (block $%sx (loop $%s\n"
                "      (br_if $%sx %s)\n      %s\n      (local.set $i (i32.add"
                " (local.get $i) (i32.const %d)))\n      %s))"
                % (self.kld("i32.load", K_LEN, "(i32.const %d)" % (4 * r.randrange(16)))
                   if r.random() < 0.6 else "(i32.const %d)" % r.choice([0, 1, 2, 9, 300]),
                   lab, lab, lab, test, "\n      ".join(body), step, latch))

    def kls_postdec(self):
        """while (n--): the old count tested, the new one carried."""
        r = self.rng
        lab = self.kl()
        w64 = r.random() < 0.3
        T, n, t = ("i64", "N", "T") if w64 else ("i32", "n", "t")
        cnt = (self.kld("i32.load", K_LEN, "(i32.const %d)" % (4 * r.randrange(16)))
               if r.random() < 0.6 else "(i32.const %d)" % r.choice([0, 1, 2, 7, 300]))
        if w64:
            cnt = "(i64.extend_i32_u %s)" % cnt
        dec = r.choice(["(%s.sub (local.get $%s) (%s.const 1))" % (T, n, T),
                        "(%s.add (local.get $%s) (%s.const -1))" % (T, n, T)])
        use = (self.kconv("(local.get $%s)" % t, T, "i32") if r.random() < 0.3
               else "(local.get $j)")
        body = ["(local.set $h (i32.xor (i32.rotl (local.get $h) (i32.const 5)) %s))"
                % self.kld("i32.load8_u", K_E32, "(i32.and (i32.add (local.get $j) %s)"
                           " (i32.const 0xff))" % use),
                "(local.set $j (i32.add (local.get $j) (i32.const %d)))" % r.randint(1, 3)]
        if r.random() < 0.3:
            body.append("(br_if $%sx (i32.gt_u (local.get $h) (i32.const 0x%x)))"
                        % (lab, r.getrandbits(32)))
        body = "\n        ".join(body)
        test = r.choice(["eqz", "if"])
        if w64:
            nz = r.choice(["(i64.ne (local.get $T) (i64.const 0))",
                           "(i32.eqz (i64.eqz (local.get $T)))"])
        else:
            nz = r.choice(["(local.get $t)", "(i32.ne (local.get $t) (i32.const 0))"])
        if test == "if":
            loop = ("(block $%sx (loop $%s\n      (local.set $%s (local.get $%s))\n"
                    "      (local.set $%s %s)\n      (if %s (then\n        %s\n"
                    "        (br $%s)))))" % (lab, lab, t, n, n, dec, nz, body, lab))
        else:
            loop = ("(block $%sx (loop $%s\n      (local.set $%s (local.get $%s))\n"
                    "      (local.set $%s %s)\n      (br_if $%sx (%s.eqz (local.get $%s)))\n"
                    "        %s\n      (br $%s)))" % (lab, lab, t, n, n, dec, lab, T, t,
                                                     body, lab))
        return ("(local.set $%s %s)\n    (local.set $j (i32.const 0))\n    %s\n"
                "    (local.set $h (i32.add (local.get $h) %s))"
                % (n, cnt, loop, self.kconv("(local.get $%s)" % n, T, "i32")))

    def kls_thread(self):
        """A compare of a value merged from a constant arm: threading sends
        that arm's edge straight to the side the compare picks."""
        r = self.rng
        x = "(local.set $x %s)" % self.kin32("o")
        cond = r.choice(["(i32.ge_u (i32.and (local.get $x) (i32.const 15)) (i32.const 4))",
                         "(i32.lt_s (local.get $x) (i32.const 0))",
                         "(i32.and (local.get $x) (i32.const 2))"])
        if r.random() < 0.3:
            k = r.choice([0, 1, 0x100000000, 0xffffffffffffffff])
            m = "(if (result i64) %s (then (i64.const 0x%x)) (else (i64.and"\
                " (i64.extend_i32_u (local.get $x)) (i64.const 0xff))))" % (cond, k)
            c = "(i64.%s %s (i64.const 0x%x))" % (r.choice(["ne", "eq"]), m,
                                                  r.choice([0, k]))
        else:
            k = r.choice([0, 1, 5, 7, -9])
            arm = r.choice(["(i32.and (local.get $x) (i32.const 0x300))",
                            "(i32.const %d)" % r.choice([5, 7, 9, 0])])
            m = "(if (result i32) %s (then %s) (else (i32.const %d)))" % (cond, arm, k)
            c = r.choice(["(i32.eqz %s)" % m,
                          "(i32.%s %s (i32.const %d))" % (r.choice(["eq", "ne"]), m,
                                                          r.choice([k, k, 5, 0]))])
        if r.random() < 0.3:
            return ("%s\n    (local.set $t %s)\n    (local.set $h (i32.add (local.get $h)"
                    " (i32.add (if (result i32) (local.get $t) (then (i32.const 100))"
                    " (else (i32.const 200))) (i32.mul (local.get $t) (local.get $x)))))"
                    % (x, c))
        return ("%s\n    (local.set $h (i32.add (local.get $h) (if (result i32) %s"
                " (then (i32.rotl (local.get $x) (i32.const 3))) (else (i32.xor"
                " (local.get $x) (i32.const 0x55))))))" % (x, c))

    def kls_tiny(self):
        """A run of loops of a few instructions each, the size a fetch block
        holds, with things between them that change how far they move."""
        r = self.rng
        out = []
        for _ in range(r.randint(2, 5)):
            lab = self.kl()
            lim = r.choice(["(i32.const %d)" % r.randint(0, 9),
                            "(i32.and (local.get $o) (i32.const 7))",
                            "(local.get $m)"])
            op = r.choice(["(i32.add (local.get $h) (local.get $j))",
                           "(i32.xor (i32.rotl (local.get $h) (i32.const 1)) (local.get $j))",
                           "(i32.mul (local.get $h) (i32.const 3))"])
            inner = "(local.set $h %s)" % op
            if r.random() < 0.2:
                l2 = self.kl()
                inner = ("(local.set $i (i32.const 0))\n      (loop $%s (local.set $h"
                         " (i32.add (local.get $h) (local.get $i)))\n        (br_if $%s"
                         " (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))"
                         " (i32.const 3))))" % (l2, l2))
            out.append("(local.set $j (i32.const 0))\n    (loop $%s %s\n      (br_if $%s"
                       " (i32.lt_u (local.tee $j (i32.add (local.get $j) (i32.const 1)))"
                       " %s)))" % (lab, inner, lab, lim))
            out.append(r.choice([
                "", "(local.set $h (i32.add (local.get $h) %s))"
                % self.kcall("id", "(local.get $j)"),
                self.kh("(i64.trunc_sat_f64_s (f64.mul (f64.convert_i32_s (local.get $h))"
                        " (f64.const 0.3)))"),
                "(local.set $h (i32.add (local.get $h) (i32.div_u (local.get $h)"
                " (i32.or (local.get $o) (i32.const 1)))))",
                "(block $%s (block $%s (br_table $%s $%s (i32.and (local.get $h)"
                " (i32.const 1)))) (local.set $h (i32.add (local.get $h) (i32.const 1))))"
                % ((lambda a, b: (a, b, a, b))(self.kl(), self.kl()))]))
        return "(local.set $m (i32.and (local.get $o) (i32.const 3)))\n    " + \
            "\n    ".join(o for o in out if o)

    # Smaller versions of the same shapes, as statements among the generic
    # ones, on the generic function's own locals and expressions.
    def kstmt(self, depth):
        self.kdepth += 1
        try:
            return self.kstmt1(depth)
        finally:
            self.kdepth -= 1

    def kstmt1(self, depth):
        r = self.rng
        d = max(0, depth - 1)
        kinds = self.kmods + [self.kprim]
        if self.kdepth > 2:
            return "(drop %s)" % self.expr("i32", depth)
        if self.loopdepth >= 2:
            kinds = [k for k in kinds if k in ("divrem", "xorshift", "fbits",
                                               "selects", "bigconst", "coldcall")]
            if not kinds:
                return "(drop %s)" % self.expr("i32", depth)
        kind = r.choice(kinds)
        loc = lambda t: r.choice(self.locals[t])
        if kind in ("argmin", "invdiv", "trapguard", "pressure"):
            self.counter += 1
            cnt = "c%d" % self.counter
            self.readonly.append(cnt)
        if kind == "argmin":
            v, best, at = self.kscr("i32"), self.kscr("i32"), self.kscr("i32")
            su, rl = r.choice("su"), r.choice(["lt", "le", "gt", "ge"])
            c = "(i32.%s_%s (local.get $%s) (local.get $%s))" % (rl, su, v, best)
            n = r.choice(["(i32.const %d)" % r.choice(LENS[:20]),
                          "(i32.and %s (i32.const 127))" % self.expr("i32", d)])
            m = self.kscr("i32")
            body = ("(local.set $%s %s)\n      (local.set $%s (select (local.get $%s)"
                    " (local.get $%s) %s))\n      (local.set $%s (select (local.get $%s)"
                    " (local.get $%s) %s))"
                    % (v, self.kld("i32.load", K_ARR, "(i32.shl (local.get $%s)"
                                   " (i32.const 2))" % cnt), at, cnt, at, c,
                       best, v, best, c))
            return ("(local.set $%s %s)\n    (local.set $%s %s)\n    (local.set $%s"
                    " (i32.const -1))\n    %s\n    (local.set $%s (i32.add (local.get $%s)"
                    " (local.get $%s)))"
                    % (m, n, best, self.expr("i32", d), at,
                       self.kfor(cnt, "(local.get $%s)" % m, body), loc("i32"), best, at))
        if kind == "invdiv":
            dv = self.kscr("i32")
            w64 = r.random() < 0.4
            num = self.expr("i32", d)
            if w64:
                t = loc("i64")
                op = ("(local.set $%s (i64.add (local.get $%s) (i64.%s_u (i64.extend_i32_u"
                      " %s) (i64.extend_i32_u (local.get $%s)))))"
                      % (t, t, r.choice(["div", "rem"]), num, dv))
            else:
                t = loc("i32")
                op = ("(local.set $%s (i32.add (local.get $%s) (i32.%s_u %s"
                      " (local.get $%s))))" % (t, t, r.choice(["div", "rem"]), num, dv))
            return ("(local.set $%s %s)\n    (if (local.get $%s) (then\n    %s))"
                    % (dv, self.expr("i32", d), dv,
                       self.kfor(cnt, "(i32.const %d)" % r.randint(1, 40), op)))
        if kind == "divrem":
            w64 = r.random() < 0.4
            T = "i64" if w64 else "i32"
            a, b = self.kscr(T), self.kscr(T)
            sg = r.random() < 0.5
            su = "s" if sg else "u"
            bad = "(%s.eqz (local.get $%s))" % (T, b)
            if sg:
                bad = ("(i32.or %s (i32.and (%s.eq (local.get $%s) (%s.const %s))"
                       " (%s.eq (local.get $%s) (%s.const -1))))"
                       % (bad, T, a, T, "0x8000000000000000" if w64 else "0x80000000",
                          T, b, T))
            q = "(%s.div_%s (local.get $%s) (local.get $%s))" % (T, su, a, b)
            rm = "(%s.rem_%s (local.get $%s) (local.get $%s))" % (T, su, a, b)
            t = loc(T)
            pair = ["(local.set $%s %s)" % (t, q),
                    "(local.set $%s (%s.add (local.get $%s) %s))" % (t, T, t, rm)]
            if r.random() < 0.3:
                pair = ["(local.set $%s %s)" % (t, rm),
                        "(local.set $%s (%s.xor (local.get $%s) %s))" % (t, T, t, q)]
            return ("(local.set $%s %s)\n    (local.set $%s %s)\n    (local.set $%s (select"
                    " (%s.const 1) (local.get $%s) %s))\n    %s"
                    % (a, self.expr(T, d), b, self.expr(T, d), b, T, b, bad,
                       "\n    ".join(pair)))
        if kind == "xorshift":
            w64 = r.random() < 0.4
            T = "i64" if w64 else "i32"
            x, y = self.kscr(T), self.kscr(T)
            self.kextra += [(y + "z", T), (y + "w", T)]
            L, folds, _, last = self.kxs(x, y, w64)
            t = loc(T)
            return ("(local.set $%s %s)\n    %s\n    (local.set $%s (%s.add (local.get $%s)"
                    " (local.get $%s)))" % (x, self.expr(T, d), "\n    ".join(L), t, T, t,
                                            last))
        if kind == "fbits":
            t = r.choice(["f32", "f64"])
            f = loc(t)
            return ("(local.set $%s (%s.%s (local.get $%s) (%s.mul %s %s)))"
                    % (f, t, r.choice(["add", "sub", "mul", "min", "max"]), f, t,
                       self.expr(t, d), self.kconst(t)))
        if kind == "selects":
            c = self.kscr("i32")
            t1 = r.choice(TYPES)
            ct = r.choice(["i32", "i64", "f64"])
            cmp = "(%s.%s %s %s)" % (ct, r.choice(ICMP if ct != "f64" else
                                                  ["eq", "ne", "lt", "ge"]),
                                     self.expr(ct, d), self.expr(ct, d))
            out = ["(local.set $%s %s)" % (c, cmp)]
            for _ in range(r.randint(2, 3)):
                t1 = r.choice(TYPES)
                x = loc(t1)
                out.append("(local.set $%s (select %s (local.get $%s) (local.get $%s)))"
                           % (x, self.expr(t1, d), x, c))
                if r.random() < 0.5:
                    out.append(self.stmt(d))
            if r.random() < 0.3:
                out.append("(if (local.get $%s) (then %s) (else %s))"
                           % (c, self.stmt(d), self.stmt(d)))
            return "\n    ".join(out)
        if kind == "bigconst":
            k = r.random()
            if k < 0.4:
                t = loc("i64")
                return ("(local.set $%s (if (result i64) %s (then (i64.const 0x%x))"
                        " (else (i64.const 0x%x))))"
                        % (t, self.expr("i32", d), bigconst(r, True), bigconst(r, True)))
            if k < 0.7:
                t = loc("i32")
                return ("(local.set $%s (block (result i32) (drop (br_if 0 (i32.const 0x%x)"
                        " %s)) (i32.const 0x%x)))" % (t, bigconst(r, False),
                                                       self.expr("i32", d),
                                                       bigconst(r, False)))
            t = loc("i64")
            return ("(local.set $%s (i64.add (local.get $%s) %s))"
                    % (t, t, self.kcall("big", "(i32.const 0x%x) (i64.const 0x%x) %s"
                                        " (i64.const 0x%x)" % (
                                            bigconst(r, False), bigconst(r, True),
                                            self.expr("i32", d), bigconst(r, True)))))
        if kind == "coldcall":
            t = loc("i32")
            rare = r.choice(["(global.get $kflag)",
                             "(i32.eq (i32.and %s (i32.const 63)) (i32.const 17))"
                             % self.expr("i32", d)])
            return ("(if %s (then (local.set $%s %s)))"
                    % (rare, t, self.kcall("clob", "(local.get $%s)" % t)))
        if kind == "trapguard":
            self.loopdepth += 1
            body = "\n      ".join(self.stmt(d) for _ in range(r.randint(1, 2)))
            self.loopdepth -= 1
            trap = (self.kst("i32.store", K_SCR + 0x80, "(i32.const 0)",
                             "(local.get $%s)" % cnt) + " (unreachable)")
            return self.kfor(cnt, "(i32.const %d)" % r.randint(1, 6),
                             "(if (global.get $kflag) (then %s))\n      %s" % (trap, body))
        if kind == "pressure":
            K = r.randint(8, 16)
            types = [r.choice(TYPES) for _ in range(K)]
            names = [self.kscr(t) for t in types]
            L = []
            for k in range(K):
                e = (self.expr(types[k], d) if k < 2 or r.random() < 0.2
                     else self.kpexpr(names[:k], types[:k], types[k]))
                L.append("(local.set $%s %s)" % (names[k], e))
            t = loc("i64")
            L += ["(local.set $%s (i64.add (i64.rotl (local.get $%s) (i64.const 7)) %s))"
                  % (t, t, self.ktoi64("(local.get $%s)" % names[k], types[k]))
                  for k in r.sample(range(K), K)]
            return self.kfor(cnt, "(i32.const %d)" % r.randint(1, 3), "\n      ".join(L))
        # loopshape: a `while (n--)` round generic statements, or a threading
        # compare
        if r.random() < 0.5:
            t = loc("i32")
            k = r.choice([0, 1, 5])
            return ("(local.set $%s (if (result i32) (i32.eqz (if (result i32) %s (then %s)"
                    " (else (i32.const %d)))) (then %s) (else %s)))"
                    % (t, self.expr("i32", d), self.expr("i32", d), k,
                       self.expr("i32", d), self.expr("i32", d)))
        n, old = self.kscr("i32"), self.kscr("i32")
        lab = self.kl()
        self.loopdepth += 1
        body = self.stmt(d)
        self.loopdepth -= 1
        t = loc("i32")
        return ("(local.set $%s (i32.and %s (i32.const 7)))\n    (loop $%s\n"
                "      (local.set $%s (local.get $%s))\n      (local.set $%s (i32.sub"
                " (local.get $%s) (i32.const 1)))\n      (if (local.get $%s) (then\n"
                "        %s\n        (br $%s))))\n    (local.set $%s (i32.add (local.get $%s)"
                " (local.get $%s)))" % (n, self.expr("i32", d), lab, old, n, n, n, old,
                                        body, lab, t, t, n))

    def module(self):
        r = self.rng
        flavour = r.randrange(4)
        self.nmem = 2 if flavour == 1 else 1
        self.mem64 = flavour == 2
        self.vectors = r.random() < 0.5
        self.flavour = ["memory", "two memories", "memory64", "memory"][flavour]
        if self.kinds:
            self.kinit()
        idx = "i64 " if self.mem64 else ""
        mems = "".join("  (memory %s1)\n" % idx for _ in range(self.nmem))
        seg = ('"\\01\\02\\03\\04\\05\\06\\07\\08'
               '\\f0\\e1\\d2\\c3\\b4\\a5\\96\\87"')
        atype = "i64" if self.mem64 else "i32"
        datas = "".join('  (data %s(%s.const 0) %s)\n'
                        % (("%d " % m) if self.nmem > 1 else "", atype, seg)
                        for m in range(self.nmem))

        self.globals = []
        for k in range(r.randint(0, 3)):
            t = r.choice(TYPES)
            self.globals.append(("g%d" % k, t, r.random() < 0.5))
        gdecl = "".join(
            "  (global $%s %s %s)\n" % (n, "(mut %s)" % t if m else t,
                                        self.const(t))
            for n, t, m in self.globals)

        self.helpers = []
        htext = []
        self.nhelpers = r.randint(0, 3)
        for k in range(self.nhelpers):
            ps = [r.choice(TYPES) for _ in range(r.randint(0, 3))]
            rt = r.choice(TYPES)
            text, out = self.helper(k, ps, rt)
            htext.append(text)
            self.helpers.append(("h%d" % k, ps, rt, out))
        tdecl = "".join("  (type $t%d (func %s (result %s)))\n"
                        % (i, " ".join("(param %s)" % p for p in h[1]), h[3])
                        for i, h in enumerate(self.helpers))
        tbl = ""
        if self.helpers and not self.kmods:
            tbl = ("  (table %d funcref)\n  (elem (i32.const 0) %s)\n"
                   % (len(self.helpers),
                      " ".join("$" + h[0] for h in self.helpers)))

        if not self.kmods:
            funcs = "".join(self.func(i) for i in range(r.randint(1, 5)))
            return ("(module\n" + tdecl + mems + datas + gdecl + tbl +
                    "".join(htext) + funcs + ")\n")

        funcs = [self.func(i) for i in range(r.randint(1, 4))]
        kern = []
        for kind in [self.kprim] * r.randint(2, 4) + self.kmods[1:]:
            kern.append(getattr(self, "k_" + kind)(len(kern)))
        funcs += kern
        r.shuffle(funcs)
        names = (["$" + h[0] for h in self.helpers] +
                 ["$k_" + h[0] for h in KHELP])
        tdecl += "".join("  (type $kt_%s (func %s (result %s)))\n"
                         % (n, " ".join("(param %s)" % p for p in ps), rt)
                         for n, ps, rt, _ in KHELP)
        tbl = ("  (table %d funcref)\n  (elem (i32.const 0) %s)\n"
               % (len(names), " ".join(names)))
        datas += "".join('  (data %s(%s.const %d) "%s")\n'
                         % ("0 " if self.nmem > 1 else "", atype, off, b)
                         for off, b in self.kdata)
        gdecl += "".join('  (global $%s (export "%s") (mut i32) (i32.const 0x%x))\n'
                         % (n, n, v) for n, v in self.kglob)
        return ("(module\n" + tdecl + mems + datas + gdecl + tbl +
                "".join(htext) + "".join(self.khelp) + "".join(self.kfuncs) +
                "".join(funcs) + ")\n")

def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--count", type=int, default=200)
    ap.add_argument("--levels", default="-O0,-O1,-O2")
    ap.add_argument("--flags", default="")
    ap.add_argument("--out", default=os.path.join(
        os.environ.get("BUILD", "build"), "fuzz"))
    ap.add_argument("--warmc", default="")
    ap.add_argument("--flavours", default="all",
                    help="old (the generic modules only, as every seed made "
                    "them before the targeted family), new (the targeted "
                    "family only), all (each seed one or the other), or a "
                    "comma-separated list of KINDS for the targeted family")
    args = ap.parse_args()
    if args.flavours in ("all", "old", "new"):
        kinds = KINDS
    else:
        kinds = args.flavours.split(",")
        bad_kinds = [k for k in kinds if k not in KINDS]
        if bad_kinds:
            ap.error("unknown flavour %s; the kinds are %s"
                     % (",".join(bad_kinds), ",".join(KINDS)))
    os.chdir(ROOT)
    build = os.environ.get("BUILD", "build")
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    levels = args.levels.split(",")
    extra = args.flags.split()
    if hostbuild.DARWIN:
        if not os.environ.get("WARMC_MACOS_LIBDIR"):
            sdk = sh(["xcrun", "--show-sdk-path"]).stdout.strip()
            os.environ["WARMC_MACOS_LIBDIR"] = os.path.join(sdk, "usr", "lib")
        target = ["--target=macos", "--rt",
                  os.path.join(build, "warmrt-macos.a"),
                  "--ld", os.environ.get("WARMC_MACOS_LD", "/usr/bin/ld")]
    else:
        target = ["--rt", os.path.join(build, "warmrt.a")]

    warmc = args.warmc
    if not warmc:
        warmc = os.path.join(out, "warmc-snapshot")
        sh(["cp", os.path.join(build, "warmc"), warmc])
    bad = rejected = refused = 0

    for n in range(args.count):
        seed = args.seed + n
        rng = random.Random(seed)
        # Which family a seed of `all` belongs to is drawn apart from the
        # module's own numbers, so an old-family seed makes the module the
        # fuzzer made for it before the targeted family existed.
        old = args.flavours == "old" or (
            args.flavours == "all" and
            random.Random(seed ^ 0x5DEECE66D).random() < 0.5)
        gen = Gen(rng, None if old else kinds)
        wat = os.path.join(out, "m.wat")
        wasm = os.path.join(out, "m.wasm")
        with open(wat, "w") as f:
            f.write(gen.module())
        r = sh(["wat2wasm"] + WABT_FLAGS + [wat, "-o", wasm])
        if r.returncode != 0:
            rejected += 1
            sh(["cp", wat, os.path.join(out, "unassembled-%d.wat" % seed)])
            continue
        ref = os.path.join(out, "m.ref")
        try:
            r = sh(["wasm-interp"] + WABT_FLAGS + ["--run-all-exports", wasm],
                   timeout=20)
        except subprocess.TimeoutExpired:
            print("seed %d: wasm-interp hung on the generated module" % seed)
            refused += 1
            continue
        with open(ref, "w") as f:
            f.write(r.stdout + r.stderr)
        if "error" in (r.stdout + r.stderr):
            refused += 1
            continue
        for lv in levels:
            binary = os.path.join(out, "m.bin")
            try:
                r = sh([warmc, lv] + extra + target +
                       ["-o", binary, wasm],
                       timeout=float(os.environ.get("WARMC_TIMEOUT", "3")))
            except subprocess.TimeoutExpired:
                print("seed %d [%s] %s: warmc HUNG" % (seed, gen.flavour, lv))
                sh(["cp", wat, os.path.join(out, "hang-%d.wat" % seed)])
                bad += 1
                break
            if r.returncode != 0:
                print("seed %d [%s] %s: warmc: %s"
                      % (seed, gen.flavour, lv, r.stderr[:400]))
                sh(["cp", wat, os.path.join(out, "bad-%d.wat" % seed)])
                bad += 1
                break
            count, fails, msgs = cmp_mod.compare(
                ref, binary, "s%d%s" % (seed, lv), run=[],
                timeout=float(os.environ.get("WARMC_RUN_TIMEOUT", "30")))
            if fails:
                print("seed %d [%s] %s: %d/%d failed"
                      % (seed, gen.flavour, lv, fails, count))
                for m in msgs[:6]:
                    print("   ", m)
                sh(["cp", wat, os.path.join(out, "bad-%d.wat" % seed)])
                bad += 1
                break
    print("done: %d seeds, %d bad, %d not assembled, %d trapped in the "
          "interpreter" % (args.count, bad, rejected, refused))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
