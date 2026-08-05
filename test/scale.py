"""Double one axis of a module at a time and see whether warmc's time doubles.

t(2N)/t(N) near 2 is linear; near 4 is quadratic and worth a look.  Each axis
is generated on its own so a slow one names the thing that is slow.

Run it as `python3 test/scale.py [N] [axis...]`.  It is not part of `run_all`:
nothing here checks an answer, and a ratio is a noisy number on a laptop with
other work on it -- what it is for is the question no corpus can ask, which is
what happens when a producer emits ten times more of something than any module
in the tree has.  Two quadratics were found with it on 2026-08-12, both in a
function with many blocks: a dataflow sweep in block-index order and a
dominance test that walked an idom chain.  Both were invisible on every corpus
because the corpora have small functions.

`--wasi` is deliberately not passed: these modules import nothing, and the CLI
harness is the shorter link.
"""
import os, re, subprocess, sys, time

OUT = os.environ.get("BUILD", "build") + "/scale"
os.makedirs(OUT, exist_ok=True)
WARMC = "./build/warmc"
RT = "build/warmrt-macos.a"

def gen(axis, n):
    L = ["(module"]
    if axis == "funcs":
        for i in range(n):
            L.append(f' (func $f{i} (result i32) (i32.const {i}))')
        L.append(' (func (export "e") (result i32) (call $f0))')
    elif axis == "types":

        for i in range(n):
            ps = " ".join("i64" if (i >> b) & 1 else "i32" for b in range(20))
            L.append(f' (type $t{i} (func (param {ps}) (result i32)))')
        L.append(' (table 1 funcref)')
        L.append(' (func (export "e") (result i32) (i32.const 0))')
    elif axis == "globals":
        for i in range(n):
            L.append(f' (global $g{i} (mut i32) (i32.const {i}))')
        L.append(' (func (export "e") (result i32) (global.get $g0))')
    elif axis == "datas":
        L.append(' (memory 100)')
        for i in range(n):
            L.append(f' (data (i32.const {i * 8}) "abcdefgh")')
        L.append(' (func (export "e") (result i32) (i32.const 0))')
    elif axis == "elems":
        L.append(' (func $t (result i32) (i32.const 7))')
        L.append(f' (table {n} funcref)')
        L.append(' (elem (i32.const 0) func ' + " ".join(["$t"] * n) + ')')
        L.append(' (func (export "e") (result i32) (i32.const 0))')
    elif axis == "exports":
        L.append(' (func $f (result i32) (i32.const 1))')
        for i in range(n):
            L.append(f' (export "e{i}" (func $f))')
    elif axis == "imports":
        for i in range(n):
            L.append(f' (import "m" "f{i}" (func $i{i} (result i32)))')
        L.append(' (func (export "e") (result i32) (i32.const 0))')
    elif axis == "onebig":
        body = "".join(f"(local.set $a (i32.add (local.get $a) (i32.const {i})))"
                       for i in range(n))
        L.append(f' (func (export "e") (result i32) (local $a i32) {body} (local.get $a))')
    elif axis == "blocks":
        body = "".join(f"(block (br_if 0 (i32.const {i % 2})))" for i in range(n))
        L.append(f' (func (export "e") (result i32) {body} (i32.const 0))')
    elif axis == "locals":
        decl = " ".join("(local i32)" for _ in range(n))
        L.append(f' (func (export "e") (result i32) {decl} (i32.const 0))')
    elif axis == "structs":
        for i in range(n):
            L.append(f' (type $s{i} (struct (field i32) (field (ref null $s{max(0, i - 1)}))))')
        L.append(' (func (export "e") (result i32) (i32.const 0))')
    elif axis == "chain":
        # The distance a local's lookup has to walk.  Value i is written once
        # (implicitly, to zero) at the top and read in arm i of a ladder, so
        # looking it up walks back through every arm above it, and SSA
        # construction leaves a block parameter on each one.  Every parameter
        # it leaves is trivial -- the arm's two edges carry the same value --
        # so what the shape asks is whether construction notices that while it
        # wires them, or hands the optimiser a function with N*N/2 parameters
        # to delete afterwards.  Until 2026-08-14 it did the latter, and the
        # cost was not in the walk but downstream: at N=4000 the built function
        # held 8.0M values where 32k survive, and `opt` and `emit` were 8548 ms
        # and 220 ms of a 9.7 s compile.  Collapsing them as they are wired
        # makes those 23 ms and 3 ms of 0.79 s.
        #
        # This is `holes` with the memory taken out.  There the loads keep the
        # values distinct and the live sets quadratic, which is the allocator's
        # problem and hides this one; here every value folds to the same zero,
        # so what is left to measure is lookup distance alone.
        #
        # The ratio is still near 4 and cannot be 2: the walk itself is
        # quadratic, because doubling the arms doubles both the number of
        # locals and the distance each is read from.  It measured 205.7, 1354.4
        # and 9648.5 ms at N=1000, 2000 and 4000 before, ratios 6.59 and 7.19 --
        # above 4 because the parameters cost more downstream than they cost to
        # make -- and 48.5, 187.6 and 785.6 ms after, ratios 3.86 and 4.19.  So
        # what this axis watches is the constant rather than the exponent: a
        # change that re-materialises the parameters shows up as seconds at
        # 4000, and as a ratio walking back up past 4.
        L.append(' (func (export "e") (param $p i32) (result i32)')
        L.append('  (local $acc i32)')
        for i in range(n):
            L.append(f'  (local $v{i} i32)')
        for i in range(n):
            L.append(f'  (if (i32.eq (local.get $p) (i32.const {i})) (then')
            L.append(f'    (local.set $acc (i32.add (local.get $acc) (local.get $v{i})))))')
        L.append('  (local.get $acc))')
    elif axis == "inlinechain":
        # N tiny functions each calling the next: every callee is an inline
        # candidate carrying one returning call, so this measures the memo
        # and the transitive worklist -- one build per callee per caller,
        # bounded depth -- rather than the splice itself.  Rebuilding
        # candidates per look, or unbounded transitivity, turns this axis
        # quadratic.
        for i in range(n):
            inner = (f"(call $f{i + 1} (i32.add (local.get 0) (i32.const 1)))"
                     if i + 1 < n else "(local.get 0)")
            L.append(f' (func $f{i} (param i32) (result i32)'
                     f' (i32.add {inner} (i32.const 1)))')
        L.append(' (func (export "e") (param $p i32) (result i32)'
                 ' (call $f0 (local.get $p)))')
    elif axis == "unroll":
        # N sibling counted loops in one function, every one a const-trip
        # unroll candidate.  The matcher visits each header once and the
        # clone cost is linear in the bodies it copies; escape repair is a
        # single deferred walk.  The pipeline already reads ~3.9 on this
        # shape with WARMC_NO_UNROLL=1 (the per-header loop passes), so
        # like `chain` this axis watches the constant, not the exponent:
        # at N=4000 the pass and the 4x loop growth it feeds downstream
        # cost 876 -> 1415 ms.  A pass-side rescan per fired loop would
        # push the exponent past the baseline.
        L.append(' (memory 100)')
        L.append(' (func (export "e") (param $p i32) (result i32)')
        L.append('  (local $acc i32) (local $i i32)')
        for i in range(n):
            L.append(f'  (local.set $i (i32.const 0))')
            L.append(f'  (loop $l{i}')
            L.append(f'    (local.set $acc (i32.add (i32.mul (local.get $acc)'
                     f' (i32.const 3)) (i32.load offset={i * 4 % 6000000}'
                     f' (local.get $i))))')
            L.append(f'    (local.set $i (i32.add (local.get $i)'
                     f' (i32.const 4)))')
            L.append(f'    (br_if $l{i} (i32.ne (local.get $i)'
                     f' (i32.const 64))))')
        L.append('  (local.get $acc))')
    elif axis == "sharedbase":
        # Like `unroll`, but every loop indexes off the same base param, so
        # after unrolling all the clone addresses re-root on adds that share
        # one value.  This is the shape that made pass_unroll_addr's
        # existing-add lookup quadratic when it walked the base's use list
        # per clone (the list is loops x K long); the add index keeps the
        # pass invisible here -- canon on and off read the same wall.
        L.append(' (memory 100)')
        L.append(' (func (export "e") (param $p i32) (result i32)')
        L.append('  (local $acc i32) (local $i i32)')
        for i in range(n):
            L.append('  (local.set $i (i32.const 0))')
            L.append(f'  (loop $l{i}')
            L.append('    (local.set $acc (i32.add (i32.mul (local.get $acc)'
                     ' (i32.const 3)) (i32.load (i32.add (local.get $p)'
                     ' (local.get $i)))))')
            L.append('    (local.set $i (i32.add (local.get $i)'
                     ' (i32.const 4)))')
            L.append(f'    (br_if $l{i} (i32.ne (local.get $i)'
                     ' (i32.const 4096))))')
        L.append('  (local.get $acc))')
    elif axis == "holes":
        # One function whose values all have a gap in the middle: value i is
        # defined at the top and read in arm i near the bottom, so it is live in
        # two small pieces with the whole ladder between them.  That is the
        # shape the register allocator's live ranges are built for, and the
        # shape that makes its per-register occupancy lists long: an interval
        # that is dormant rather than finished cannot be retired, because its
        # end is the end of its *last* range.
        #
        # Distinct load offsets matter.  With one offset, CSE merges the
        # definitions into a single value and the ladder measures nothing.
        #
        # The ratio here is near 4 and always was: doubling the arms doubles
        # both the values and the blocks, so the live sets themselves are
        # quadratic and every allocator pays that.  At N=250 it measured 3.38
        # with live-range holes and 3.30 with WARMC_NO_HOLES=1 -- so what this
        # axis watches is not the ratio but a *change* in it, and the cheapest
        # reading is to run it both ways.  Past a few thousand arms the range
        # budget gives up and falls back to whole-span intervals, and beyond
        # that point the axis is measuring `locals` and `blocks` instead.
        L.append(' (memory 100)')
        L.append(' (func (export "e") (param $p i32) (result i32)')
        L.append('  (local $acc i32)')
        for i in range(n):
            L.append(f'  (local $v{i} i32)')
        for i in range(n):
            L.append(f'  (local.set $v{i} (i32.load offset={i * 4 % 6000000} (i32.const 0)))')
        for i in range(n):
            L.append(f'  (if (i32.eq (local.get $p) (i32.const {i})) (then')
            L.append(f'    (local.set $acc (i32.add (local.get $acc) (local.get $v{i})))))')
        L.append('  (local.get $acc))')
    L.append(")")
    p = os.path.join(OUT, f"sc_{axis}_{n}.wat")
    open(p, "w").write("\n".join(L))
    return p

def timeit(path):
    out = os.path.join(OUT, "sc.out")
    best = None
    for _ in range(3):
        t0 = time.time()
        r = subprocess.run([WARMC, "-O2", "--target", "macos", "--rt", RT,
                            "-o", out, path], capture_output=True,
                           env=dict(os.environ, WARMC_TIME="1"))
        dt = time.time() - t0
        if r.returncode != 0:
            return None, r.stderr.decode()[-160:].strip().replace("\n", " ")
        best = dt if best is None else min(best, dt)
    return best, r.stderr.decode()

AXES = ["funcs", "types", "globals", "datas", "elems", "exports", "imports",
        "onebig", "blocks", "locals", "structs", "chain", "inlinechain",
        "unroll", "sharedbase", "holes"]
N = int(sys.argv[1]) if len(sys.argv) > 1 else 4000
print(f"{'axis':9} {'N':>7} {'t(N) ms':>9} {'t(2N) ms':>9} {'ratio':>6}  phases at 2N")
for axis in (sys.argv[2:] or AXES):
    row = []
    for n in (N, 2 * N):
        t, info = timeit(gen(axis, n))
        row.append((t, info))
    if row[0][0] is None or row[1][0] is None:
        print(f"{axis:9} {N:7d}  skipped: {row[0][1] or row[1][1]}")
        continue
    a, b = row[0][0] * 1000, row[1][0] * 1000
    ph = " ".join(re.findall(r"(?:produce|place|moddata|warm) \d+", row[1][1])[:4])
    lk = re.search(r"link time: (.*)", row[1][1])
    print(f"{axis:9} {N:7d} {a:9.1f} {b:9.1f} {b/a:6.2f}  {ph} | {lk.group(1) if lk else ''}")
