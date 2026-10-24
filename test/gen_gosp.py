"""Random modules in the shape of Go's wasm port, for gosp.c.

A module of this family has what makes gosp.c pass Go's stack pointer to
its functions in place of global 0: a go:buildid section, the pointer in
global 0, and Go's functions all of type (i32) -> i32.  They call one
another as Go's do -- the argument at the caller's stack pointer, a return
address pushed below it, the result above the argument, 1 returned to
unwind -- directly and through the table, along a call graph without cycles
but for bounded self-recursion.  Between calls they keep values in their
frames, read the pointer into a checksum, call a write barrier that pushes
a return address where the global says and calls a Go function to record
where it was entered (directly, through one or two functions that only
reach it, or through the table), move the pointer by calling functions of
other types, and call Go functions back through a function of another type
that takes the global as its stack.  They leave every way a body can: the
value at the end, a return, a branch to the function's label, a br_table's
default to it.  Some switch to another stack and unwind with 1, which every
caller passes up, so an export sees the pointer they left.  Every address
stays inside the memory and every loop ends, so nothing traps.

  python3 test/gen_gosp.py --seed 1 --count 300        # differential fuzz
  test/gen_cases.py writes test/cases/gospgen.wat from seed 1, and
  test/run_gosp.py runs two hundred seeds in make check.

The fuzz mode checks each module against wasm-interp at each level and
that gosp.c rewrote all its Go functions (--stats), so a module the rewrite
silently gives up on is a failure too.
"""
import argparse
import os
import random
import re
import subprocess
import sys

BASE = 32768                     # the main stack's top
OTHER = 49152                    # the stacks a function switches to

class Gen:
    def __init__(self, rng, nfuncs=None, nexports=None):
        self.r = rng
        self.n = nfuncs or rng.randint(4, 10)
        self.ne = nexports or rng.randint(4, 8)
        self.retaddr = 0x10000
        # table: 0 empty, 1..n the Go functions, then the others
        self.t_wb = self.n + 1
        self.t_pair = self.n + 2
        self.t_mix = self.n + 3
        self.t_flush = self.n + 4
        self.switcher = set(k for k in range(self.n) if rng.random() < 0.3)
        self.recursive = set(k for k in range(self.n) if rng.random() < 0.3)

    def ra(self):
        self.retaddr += 1
        return "0x%x" % self.retaddr

    # --- a Go function ------------------------------------------------------
    def expr(self):
        r = self.r
        a = r.choice(["(local.get $x)", "(local.get $y)", "(i32.const %d)" % r.randint(1, 1000),
                      "(i32.wrap_i64 (local.get $z))"])
        op = r.choice(["i32.add", "i32.xor", "i32.mul", "i32.sub", "i32.rotl", "i32.or"])
        return "(%s (local.get $y) %s)" % (op, a)

    def call_go(self, j, arg, indirect):
        """The calling sequence of Go's: the argument at sp, the return
        address below it, the result above the argument once the callee has
        popped the address."""
        call = ("(call_indirect (type $go) (i32.const 0) (i32.const %d))" % (j + 1) if indirect
                else "(call $g%d (i32.const 0))" % j)
        return [
            "(i32.store (global.get $sp) %s)" % arg,
            "(global.set $sp (i32.sub (global.get $sp) (i32.const 8)))",
            "(i64.store (global.get $sp) (i64.const %s))" % self.ra(),
            "(if %s (then (return (i32.const 1))))" % call,
            "(local.set $y (i32.add (local.get $y) (i32.load offset=4 (global.get $sp))))",
        ]

    def epilogue(self):
        return ["(i32.store offset=%d (global.get $sp) (local.get $y))" % (self.F + 12),
                "(global.set $sp (i32.add (global.get $sp) (i32.const %d)))" % (self.F + 8)]

    def leave(self, flag, depth):
        """leave the function with flag, from `depth` labels in"""
        r = self.r
        how = r.choice(["return", "br", "br_table"])
        if how == "return":
            return ["(return (i32.const %d))" % flag]
        if how == "br":
            return ["(br %d (i32.const %d))" % (depth, flag)]
        # into two blocks of i32, each of whose ends leaves too; the default
        # leaves straight from the br_table, past both
        return [
            "(drop (block (result i32)",
            "  (drop (block (result i32)",
            "    (br_table 1 0 %d (i32.const %d) (i32.and (local.get $x) (i32.const 3)))))"
            % (depth + 2, flag),
            "  (return (i32.const %d))))" % flag,
            "(br %d (i32.const %d))" % (depth, flag),
        ]

    def stmts(self, k, depth, budget, grown, calls_ok):
        r = self.r
        out = []
        for _ in range(r.randint(1, 4)):
            if budget[0] <= 0:
                break
            budget[0] -= 1
            kinds = ["arith", "arith", "frame", "observe", "local"]
            if calls_ok:
                kinds += ["callgo", "callgo", "wb", "rcall", "pair"]
                if not grown:
                    kinds += ["grow"]
            if depth < 3:
                kinds += ["if", "loop"]
            if not grown:
                kinds += ["exit"]
                if k in self.switcher:
                    kinds += ["switch"]
            kind = r.choice(kinds)
            off = 32 if grown else 0
            if kind == "arith":
                out.append("(local.set $y %s)" % self.expr())
            elif kind == "local":
                out.append("(local.set $d (f64.add (local.get $d) (f64.convert_i32_u (local.get $y))))")
                out.append("(local.set $z (i64.add (local.get $z) (i64.reinterpret_f64 (local.get $d))))")
            elif kind == "frame":
                slot = off + r.randrange(8, self.F, 4)
                out.append("(i32.store offset=%d (global.get $sp) %s)" % (slot, self.expr()))
                slot2 = off + r.randrange(8, self.F, 4)
                out.append("(local.set $y (i32.add (local.get $y) (i32.load offset=%d (global.get $sp))))" % slot2)
            elif kind == "observe":
                out.append("(global.set $acc (call $mix (i64.add (global.get $acc) "
                           "(i64.extend_i32_u (global.get $sp)))))")
            elif kind == "callgo":
                if self.calls[k] >= 2:
                    continue
                if k in self.recursive and not self.selfcalled:
                    self.selfcalled = True
                    self.calls[k] += 1
                    out.append("(if (i32.gt_u (local.get $x) (i32.const 1)) (then")
                    out += ["  " + l for l in self.call_go(k, "(i32.shr_u (local.get $x) (i32.const 1))",
                                                           r.random() < 0.5)]
                    out.append("))")
                    continue
                if k + 1 >= self.n or k in self.recursive:
                    continue
                self.calls[k] += 1
                j = r.randint(k + 1, self.n - 1)
                arg = "(i32.and %s (i32.const 31))" % self.expr()
                out += self.call_go(j, arg, r.random() < 0.4)
            elif kind == "wb":
                c = r.choice(["(call $wb1)", "(call $wb2 (local.get $y))",
                              "(call_indirect (type $wbt) (i64.extend_i32_u (local.get $y)) (i32.const %d))"
                              % r.choice([self.t_wb, self.t_mix]),
                              "(call $wb (local.get $z))"])
                out.append("(local.set $z (i64.add (local.get $z) %s))" % c)
            elif kind == "rcall":
                if k + 1 >= self.n or self.calls[k] >= 2:
                    continue
                self.calls[k] += 1
                j = r.randint(k + 1, self.n - 1)
                out.append("(local.set $y (i32.add (local.get $y) (call $rcall (i32.const %d) "
                           "(i32.and (local.get $y) (i32.const 31)))))" % (j + 1))
            elif kind == "pair":
                out.append("(local.set $z (i64.add (local.get $z) (call_indirect (type $c2) "
                           "(local.get $x) (local.get $y) (i32.const %d))))" % self.t_pair)
            elif kind == "grow":
                out.append("(call $grow)")
                out += self.stmts(k, depth, budget, True, calls_ok)
                out.append("(i32.store offset=%d (global.get $sp) (local.get $y))" % (32 + 8))
                out.append("(call $shrink)")
                out.append("(local.set $y (i32.add (local.get $y) (i32.load offset=8 (global.get $sp))))")
            elif kind == "if":
                cond = "(i32.and (local.get $x) (i32.const %d))" % (1 << r.randrange(5))
                out.append("(if %s (then" % cond)
                out += ["  " + l for l in self.stmts(k, depth + 1, budget, grown, calls_ok)]
                out.append(") (else")
                out += ["  " + l for l in self.stmts(k, depth + 1, budget, grown, calls_ok)]
                out.append("))")
            elif kind == "loop":
                i = "$i%d" % depth
                out.append("(local.set %s (i32.const 0))" % i)
                out.append("(loop")
                out += ["  " + l for l in self.stmts(k, depth + 1, budget, grown, False)]
                out.append("  (br_if 0 (i32.lt_u (local.tee %s (i32.add (local.get %s) (i32.const 1))) "
                           "(i32.const %d))))" % (i, i, r.randint(2, 3)))
            elif kind == "exit":
                m = r.choice([5, 7, 11])
                out.append("(if (i32.eq (i32.rem_u (local.get $x) (i32.const %d)) (i32.const %d)) (then"
                           % (m, r.randrange(m)))
                out += ["  " + l for l in self.epilogue() + self.leave(0, depth + 1)]
                out.append("))")
            elif kind == "switch":
                out.append("(if (i32.eq (i32.and (local.get $x) (i32.const 7)) (i32.const %d)) (then"
                           % r.randrange(8))
                out.append("  (global.set $sp (i32.const %d))" % (OTHER + 64 * k + 8 * r.randrange(4)))
                out += ["  " + l for l in self.leave(1, depth + 1)]
                out.append("))")
        return out

    def go_func(self, k):
        r = self.r
        self.F = r.randrange(16, 72, 8)
        self.selfcalled = False
        L = ["(func $g%d (type $go)" % k,
             "  (local $x i32) (local $y i32) (local $z i64) (local $d f64)"
             " (local $i0 i32) (local $i1 i32) (local $i2 i32) (local $i3 i32)",
             "  (local.set $x (i32.load offset=8 (global.get $sp)))",
             "  (global.set $sp (i32.sub (global.get $sp) (i32.const %d)))" % self.F,
             "  (local.set $y (i32.add (local.get $x) (i32.const %d)))" % r.randint(1, 99)]
        body = self.stmts(k, 0, [r.randint(4, 14)], False, True)
        body += ["(local.set $y (i32.add (local.get $y) (i32.wrap_i64 (local.get $z))))"]
        body += self.epilogue()
        end = r.choice(["value", "return", "br"])
        body += {"value": ["(i32.const 0)"], "return": ["(return (i32.const 0))"],
                 "br": ["(br 0 (i32.const 0))"]}[end]
        L += ["  " + l for l in body]
        L.append(")")
        return L

    # --- the module ---------------------------------------------------------
    def body(self):
        r = self.r
        self.calls = [0] * self.n
        L = ['(@custom "go:buildid" "warmc gen_gosp")',
             "(type $go (func (param i32) (result i32)))",
             "(type $wbt (func (param i64) (result i64)))",
             ";; the shape gosp.c gives Go's functions, as a type of the module's own",
             "(type $c2 (func (param i32 i32) (result i64)))",
             "(memory 1)",
             "(global $sp (mut i32) (i32.const %d))" % BASE,
             "(global $seen (mut i64) (i64.const 0))",
             "(global $acc (mut i64) (i64.const 0))",
             "(table %d funcref)" % (self.n + 5),
             "(elem (i32.const 1) %s $wb $pair $mix $flush)" % " ".join("$g%d" % k for k in range(self.n)),
             ";; away from the global",
             "(func $mix (type $wbt)",
             "  (i64.xor (i64.mul (local.get 0) (i64.const 0x9e3779b97f4a7c15))",
             "           (i64.shr_u (local.get 0) (i64.const 29))))",
             "(func $pair (type $c2)",
             "  (i64.or (i64.shl (i64.extend_i32_u (local.get 0)) (i64.const 32)) (i64.extend_i32_u (local.get 1))))",
             ";; a Go function: where it was entered",
             "(func $flush (type $go)",
             "  (global.set $seen (i64.add (i64.mul (global.get $seen) (i64.const 31))",
             "                             (i64.extend_i32_u (global.get $sp))))",
             "  (global.set $sp (i32.add (global.get $sp) (i32.const 8)))",
             "  (i32.const 0))",
             ";; the write barrier, and two functions that reach the global only through it",
             "(func $wb (type $wbt)",
             "  (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))",
             "  (i64.store (global.get $sp) (i64.const 0x30001))",
             "  (if (call $flush (i32.const 0)) (then unreachable))",
             "  (call $mix (local.get 0)))",
             "(func $wb1 (result i64) (call $wb (i64.const 8)))",
             "(func $wb2 (param i32) (result i64) (i64.add (call $wb1) (i64.extend_i32_u (local.get 0))))",
             ";; move the pointer",
             "(func $grow (global.set $sp (i32.sub (global.get $sp) (i32.const 32))))",
             "(func $shrink (global.set $sp (i32.add (global.get $sp) (i32.const 32))))",
             ";; a Go function called back with the global as its stack, as",
             ";; wasm_pc_f_loop does; after an unwind it records the stack it was",
             ";; left on and goes back to its own",
             "(func $rcall (param $f i32) (param $a i32) (result i32)",
             "  (local $save i32) (local $flag i32) (local $r i32)",
             "  (local.set $save (global.get $sp))",
             "  (global.set $sp (i32.sub (global.get $sp) (i32.const 16)))",
             "  (i32.store (global.get $sp) (local.get $a))",
             "  (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))",
             "  (i64.store (global.get $sp) (i64.const 0x77))",
             "  (local.set $flag (call_indirect (type $go) (i32.const 0) (local.get $f)))",
             "  (local.set $r (i32.load offset=4 (global.get $sp)))",
             "  (if (local.get $flag) (then",
             "    (global.set $seen (i64.add (i64.mul (global.get $seen) (i64.const 131))",
             "                               (i64.extend_i32_u (global.get $sp))))",
             "    (local.set $r (i32.const 99))))",
             "  (global.set $sp (local.get $save))",
             "  (local.get $r))"]
        for k in range(self.n):
            L += self.go_func(k)
        for e in range(self.ne):
            j = r.randrange(min(3, self.n))
            arg = r.randint(0, 40)
            how = r.choice(["direct", "indirect", "rcall"])
            L.append('(func (export "e%d") (result i64)' % e)
            L.append("  (local $f i32) (local $r i32)")
            L.append("  (global.set $sp (i32.const %d))" % BASE)
            L.append("  (global.set $seen (i64.const 0))")
            L.append("  (global.set $acc (i64.const 0))")
            if how == "rcall":
                L.append("  (local.set $r (call $rcall (i32.const %d) (i32.const %d)))" % (j + 1, arg))
            else:
                call = ("(call_indirect (type $go) (i32.const 0) (i32.const %d))" % (j + 1)
                        if how == "indirect" else "(call $g%d (i32.const 0))" % j)
                L += ["  (global.set $sp (i32.sub (global.get $sp) (i32.const 16)))",
                      "  (i32.store (global.get $sp) (i32.const %d))" % arg,
                      "  (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))",
                      "  (i64.store (global.get $sp) (i64.const %s))" % self.ra(),
                      "  (local.set $f %s)" % call,
                      "  (local.set $r (i32.load offset=4 (global.get $sp)))"]
            L.append("  (call $mix (i64.add (i64.add (i64.extend_i32_u (local.get $r))")
            L.append("    (i64.shl (i64.extend_i32_u (i32.add (i32.mul (local.get $f) (i32.const 100000))")
            L.append("                                        (global.get $sp))) (i64.const 32)))")
            L.append("    (i64.add (call $mix (global.get $seen)) (global.get $acc)))))")
        return "".join("  " + l + "\n" for l in L)

    def module(self):
        return "(module\n" + self.body() + ")\n"

def body(seed):
    return Gen(random.Random(seed), 10, 8).body()

def target_flags(build):
    """warmc's flags to link a runnable binary on this host"""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import hostbuild
    if hostbuild.DARWIN:
        if not os.environ.get("WARMC_MACOS_LIBDIR"):
            sdk = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True).stdout.strip()
            os.environ["WARMC_MACOS_LIBDIR"] = os.path.join(sdk, "usr", "lib")
        return ["--target=macos", "--rt", os.path.join(build, "warmrt-macos.a")]
    return ["--rt", os.path.join(build, "warmrt.a")]

def fuzz(seed, count, levels, flags, out):
    """Each seed's module against wasm-interp at each level; the number bad."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import compare as cmp_mod
    build = os.environ.get("BUILD", "build")
    os.makedirs(out, exist_ok=True)
    import hostbuild
    wabt = hostbuild.wabt_flags()
    target = target_flags(build)
    warmc = os.path.join(build, "warmc")
    sh = lambda c, **kw: subprocess.run(c, capture_output=True, text=True, **kw)
    bad = 0
    for n in range(count):
        s = seed + n
        g = Gen(random.Random(s))
        wat, wasm, ref = (os.path.join(out, "m." + x) for x in ("wat", "wasm", "ref"))
        with open(wat, "w") as f:
            f.write(g.module())
        r = sh(["wat2wasm"] + wabt + [wat, "-o", wasm])
        if r.returncode:
            print("seed %d: not assembled: %s" % (s, r.stderr[:300]))
            bad += 1
            continue
        r = sh(["wasm-interp"] + wabt + ["--run-all-exports", wasm], timeout=60)
        with open(ref, "w") as f:
            f.write(r.stdout + r.stderr)
        if "error" in r.stdout + r.stderr or "trap" in r.stdout + r.stderr:
            print("seed %d: the interpreter trapped: %s" % (s, (r.stdout + r.stderr)[:300]))
            bad += 1
            continue
        for lv in levels:
            binary = os.path.join(out, "m.bin")
            r = sh([warmc] + lv.split() + ["--stats"] + flags + target + ["-o", binary, wasm], timeout=30)
            if r.returncode:
                print("seed %d %s: warmc: %s" % (s, lv, r.stderr[:400]))
                bad += 1
                break
            if not re.search(r"\bgosp_funcs=%d\b" % (g.n + 1), r.stderr):
                print("seed %d %s: gosp.c did not rewrite the %d Go functions" % (s, lv, g.n + 1))
                bad += 1
                break
            nexp, fails, msgs = cmp_mod.compare(ref, binary, "g%d%s" % (s, lv), run=[], timeout=30)
            if fails:
                print("seed %d %s: %d/%d failed" % (s, lv, fails, nexp))
                for m in msgs[:6]:
                    print("   ", m)
                os.replace(wat, os.path.join(out, "bad-%d.wat" % s))
                bad += 1
                break
    print("gosp fuzz: %d seeds from %d at %s, %d bad" % (count, seed, " ".join(levels), bad))
    return bad

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--count", type=int, default=100)
    ap.add_argument("--levels", default="-O1,-O2")
    ap.add_argument("--flags", default="")
    ap.add_argument("--out", default=os.path.join(os.environ.get("BUILD", "build"), "fuzz-gosp"))
    args = ap.parse_args()
    os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    return 1 if fuzz(args.seed, args.count, args.levels.split(","), args.flags.split(),
                     os.path.abspath(args.out)) else 0

if __name__ == "__main__":
    sys.exit(main())
