"""Random-module differential fuzzer for warmc.

Generates trap-free wasm modules of nullary exports, runs them through
wasm-interp for the answers, and compares warmc's compiled output at several
optimisation levels.  Seeds are printed so a failure reproduces.

Each seed picks a flavour -- one 32-bit memory, two memories, or a 64-bit
memory -- and may add v128 values, helper functions called directly and
through a table, tail calls and br_table, so the shapes that reach the ABI and
the newer instruction sets are generated as well as the arithmetic.

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

Usage: fuzz.py [--seed N] [--count N] [--levels -O0,-O1,-O2] [--flags ...]
"""
import argparse
import os
import random
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

class Gen:
    def __init__(self, rng):
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

    def module(self):
        r = self.rng
        flavour = r.randrange(4)
        self.nmem = 2 if flavour == 1 else 1
        self.mem64 = flavour == 2
        self.vectors = r.random() < 0.5
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
        for k in range(r.randint(0, 3)):
            ps = [r.choice(TYPES) for _ in range(r.randint(0, 3))]
            rt = r.choice(TYPES)
            text, out = self.helper(k, ps, rt)
            htext.append(text)
            self.helpers.append(("h%d" % k, ps, rt, out))
        tdecl = "".join("  (type $t%d (func %s (result %s)))\n"
                        % (i, " ".join("(param %s)" % p for p in h[1]), h[3])
                        for i, h in enumerate(self.helpers))
        tbl = ""
        if self.helpers:
            tbl = ("  (table %d funcref)\n  (elem (i32.const 0) %s)\n"
                   % (len(self.helpers),
                      " ".join("$" + h[0] for h in self.helpers)))

        funcs = "".join(self.func(i) for i in range(r.randint(1, 5)))
        return ("(module\n" + tdecl + mems + datas + gdecl + tbl +
                "".join(htext) + funcs + ")\n")

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
    args = ap.parse_args()
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
    bad = rejected = 0

    for n in range(args.count):
        seed = args.seed + n
        rng = random.Random(seed)
        wat = os.path.join(out, "m.wat")
        wasm = os.path.join(out, "m.wasm")
        with open(wat, "w") as f:
            f.write(Gen(rng).module())
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
            continue
        with open(ref, "w") as f:
            f.write(r.stdout + r.stderr)
        if "error" in (r.stdout + r.stderr):
            continue
        for lv in levels:
            binary = os.path.join(out, "m.bin")
            try:
                r = sh([warmc, lv] + extra + target +
                       ["-o", binary, wasm],
                       timeout=float(os.environ.get("WARMC_TIMEOUT", "3")))
            except subprocess.TimeoutExpired:
                print("seed %d %s: warmc HUNG" % (seed, lv))
                sh(["cp", wat, os.path.join(out, "hang-%d.wat" % seed)])
                bad += 1
                break
            if r.returncode != 0:
                print("seed %d %s: warmc: %s" % (seed, lv, r.stderr[:400]))
                sh(["cp", wat, os.path.join(out, "bad-%d.wat" % seed)])
                bad += 1
                break
            count, fails, msgs = cmp_mod.compare(
                ref, binary, "s%d%s" % (seed, lv), run=[],
                timeout=float(os.environ.get("WARMC_RUN_TIMEOUT", "30")))
            if fails:
                print("seed %d %s: %d/%d failed" % (seed, lv, fails, count))
                for m in msgs[:6]:
                    print("   ", m)
                sh(["cp", wat, os.path.join(out, "bad-%d.wat" % seed)])
                bad += 1
                break
    print("done: %d seeds, %d bad, %d not assembled" %
          (args.count, bad, rejected))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
