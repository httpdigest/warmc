"""tramp.c, GHC's trampoline as tail calls: where it applies and where not.

Its rewrite changes the signature of every function the loop calls, so the
conditions under which it is made are what keeps it unobservable.  Each
module here is a small one in GHC's shape -- functions of type () -> i32 in
the table, each returning the index of the next, a loop that calls through
the table until one returns 0, and a register only they touch -- with one
thing more.  Some of those rule the rewrite out, and the module must
compile with it not made (--stats says tramp_funcs=0):
- the call not StgRun's loop, or more in the loop than the call;
- a second call of the loop's type, or one through another table, a tail
  call or call_ref of that type;
- a second table, or the table exported or written;
- table.get;
- the loop's type not final;
- a function of that type exported, called directly, taken by ref.func,
  named by a passive or declarative segment or a global, imported into
  the table, making a tail call, or handling an exception;
- the register exported, imported or read outside.
None is made with -fexact-trap, where a trap must see the registers in
their globals.  Others leave the rewrite made with what it must do besides:
- StgRun's loop with local.tee as well as with local.set and local.get;
- a global it must not take as a register -- immutable, or an i64 --
  used more than the one it can take;
- the rec group of the type it makes for the rewritten functions, which
  is then made with another empty type, written by the module;
- a trampoline run inside another: called directly, through a function
  that calls it, through the table by its type or a supertype of it, by
  call_ref.  The registers must go to their globals before that call and
  come back after;
- an exception thrown through the loop and caught, after which the loop
  runs again.  The registers must go to their globals before every call.
Each module's result is worked out by hand here.  Where it runs on its
own, the result must be the same at -O2 as at -O0, which makes no rewrite.
Each module is the one test of its condition: removing the condition from
tramp.c fails it.  The host calling back into the module from an import is
test/shared/tramp.wat's.  wasm-tools assembles the modules, since wabt
reads no subtypes.
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_gosp
import hostbuild

# $a adds 3 to the register and goes to $b, which multiplies it by 5 and
# goes to $show, which leaves it in memory; $c adds 100, for the runs
# inside another.  Two runs from $a: (0 + 3) * 5 = 15, then (15 + 3) * 5 =
# 90.  Memory at 12 counts what a loop that does more counts.
BASE = r'''(module
  (type $stg {stgtype})
  {types}
  (type $c1 {c1type})
  {imports}
  (memory 1)
  {globals}
  (global $r1 (mut i32) (i32.const 0))
  (table 8 funcref)
  (elem (i32.const 1) $a $b $c $show {elem})
  {pre}
  (func $a (type $stg)
    (global.set $r1 (i32.add (global.get $r1) (i32.const 3)))
    {a}
    (i32.const 2))
  (func $b (type $stg)
    (global.set $r1 (i32.mul (global.get $r1) (i32.const 5)))
    (i32.const 4))
  (func $c (type $stg)
    (global.set $r1 (i32.add (global.get $r1) (i32.const 100)))
    (i32.const 0))
  (func $show (type $stg)
    (i32.store (i32.const 0) (global.get $r1))
    (i32.const 0))
  (func $run (type $c1) (param $f i32)
    {loop})
  {funcs}
  (func (export "run") (result i32)
    {run}
    (i32.add (i32.load (i32.const 0)) (i32.mul (i32.load (i32.const 12)) (i32.const 1000))))
)
'''

STGRUN = '''(loop $l
      (local.set $f (call_indirect (type $stg) (local.get $f)))
      (br_if $l (local.get $f)))'''

DEFAULTS = {"stgtype": "(func (result i32))", "c1type": "(func (param i32))", "types": "",
            "imports": "", "globals": "", "elem": "", "pre": "", "a": "", "loop": STGRUN, "funcs": "",
            "run": "(call $run (i32.const 1)) (call $run (i32.const 1))"}

# a run inside $a, from $c: (3 + 100) * 5 = 515, then (515 + 3 + 100) * 5
NESTED = 3090

# (what, substitutions, functions rewritten, the result, whether it runs alone)
CASES = [
    ("the control", {}, 4, 90, True),
    ("StgRun's loop with local.tee",
     {"loop": "(loop $l (br_if $l (local.tee $f (call_indirect (type $stg) (local.get $f)))))"},
     4, 90, True),
    ("an immutable global used more than the register",
     {"globals": "(global $k i32 (i32.const 0))",
      "a": "(global.set $r1 (i32.add (global.get $r1) (i32.add (global.get $k) (global.get $k))))"
           " (drop (i32.add (global.get $k) (global.get $k)))"}, 4, 90, True),
    # the rec group tramp.c appends for a type of its own, so it takes
    # another empty type to be new
    ("the rewrite's new type's rec group written by the module",
     {"types": "(rec (type $p (func (param i32) (result i32))) (type $e (func)))"}, 4, 90, True),
    ("an i64 global used more than the register",
     {"globals": "(global $w (mut i64) (i64.const 0))",
      "a": "(global.set $w (i64.add (global.get $w) (i64.const 1)))"
           " (global.set $w (i64.add (global.get $w) (i64.const 1)))"}, 4, 90, True),

    # a run inside another
    ("a run inside another, called directly", {"a": "(call $run (i32.const 3))"}, 4, NESTED, True),
    ("a run inside another, through a function that calls it",
     {"a": "(call $helper)", "funcs": "(func $helper (call $run (i32.const 3)))"}, 4, NESTED, True),
    ("a run inside another, through the table",
     {"a": "(call_indirect (type $c1) (i32.const 3) (i32.const 5))", "elem": "$run"},
     4, NESTED, True),
    ("a run inside another, through the table by a supertype",
     {"types": "(type $p (sub (func (param i32))))", "c1type": "(sub $p (func (param i32)))",
      "a": "(call_indirect (type $p) (i32.const 3) (i32.const 5))", "elem": "$run"},
     4, NESTED, True),
    ("a run inside another, by call_ref",
     {"a": "(call_ref $c1 (i32.const 3) (ref.func $run))", "funcs": "(elem declare func $run)"},
     4, NESTED, True),
    ("a run inside another, through a function that calls it through the table",
     {"a": "(call $helper)", "elem": "$run",
      "funcs": "(func $helper (call_indirect (type $c1) (i32.const 3) (i32.const 5)))"},
     4, NESTED, True),
    ("a run inside another, through a function that calls it by call_ref",
     {"a": "(call $helper)",
      "funcs": "(elem declare func $run) (func $helper (call_ref $c1 (i32.const 3) (ref.func $run)))"},
     4, NESTED, True),
    # $a throws the first time: the first run leaves 3, $show stores it,
    # and the next run from $a makes (3 + 3) * 5
    ("an exception thrown through the loop and caught",
     {"funcs": "(tag $e) (func $thrower (throw $e))",
      "a": "(if (i32.eqz (i32.load (i32.const 8))) (then (i32.store (i32.const 8) (i32.const 1))"
           " (call $thrower)))",
      "run": "(block $h (try_table (catch $e $h) (call $run (i32.const 1))))"
             " (call $run (i32.const 4)) (call $run (i32.const 1))"}, 4, 30, True),

    # not StgRun's loop: $a once each, and nothing stored
    ("the call not in a loop",
     {"loop": "(local.set $f (call_indirect (type $stg) (local.get $f)))"}, 0, 0, True),
    ("more in the loop than the call",
     {"loop": STGRUN.replace("(br_if $l",
                             "(i32.store (i32.const 12) (i32.add (i32.load (i32.const 12)) (i32.const 1)))"
                             " (br_if $l")}, 0, 6090, True),
    ("a second call of the loop's type, after the loop",
     {"funcs": "(func $other (drop (call_indirect (type $stg) (i32.const 0))))"}, 0, 90, True),
    ("a second call of the loop's type, before the loop",
     {"pre": "(func $other (drop (call_indirect (type $stg) (i32.const 0))))"}, 0, 90, True),
    ("a call of the loop's type through another table",
     {"funcs": "(table $t2 1 funcref) (func $other (drop (call_indirect $t2 (type $stg) (i32.const 0))))"},
     0, 90, True),
    ("a tail call of the loop's type",
     {"funcs": "(func $other (result i32) (return_call_indirect (type $stg) (i32.const 0)))"},
     0, 90, True),
    ("call_ref of the loop's type",
     {"funcs": "(func $other (param (ref $stg)) (result i32) (call_ref $stg (local.get 0)))"},
     0, 90, True),
    ("a second table", {"funcs": "(table $t2 1 funcref)"}, 0, 90, True),
    ("the table exported", {"funcs": '(export "t" (table 0))'}, 0, 90, True),
    ("the table written", {"funcs": "(func $ts (table.set 0 (i32.const 6) (ref.null func)))"},
     0, 90, True),
    ("table.get", {"funcs": "(func $tg (result funcref) (table.get 0 (i32.const 1)))"}, 0, 90, True),
    ("the loop's type not final", {"stgtype": "(sub (func (result i32)))"}, 0, 90, True),
    ("a function of the loop's type exported", {"funcs": '(export "b" (func $b))'}, 0, 90, True),
    ("a function of the loop's type called directly",
     {"funcs": "(func $d (result i32) (call $b))"}, 0, 90, True),
    ("a function of the loop's type taken by ref.func",
     {"funcs": "(func $r (drop (ref.func $b)))"}, 0, 90, True),
    ("a function of the loop's type in a passive segment", {"funcs": "(elem $ps func $b)"},
     0, 90, True),
    ("a function of the loop's type in a declarative segment",
     {"funcs": "(elem declare func $b)"}, 0, 90, True),
    ("a function of the loop's type in a global",
     {"globals": "(global $gr funcref (ref.func $b))"}, 0, 90, True),
    ("a function of the loop's type imported into the table",
     {"imports": '(import "env" "f" (func $imp (type $stg)))', "elem": "$imp"}, 0, None, False),
    ("a function of the loop's type making a tail call",
     {"funcs": "(func $two (result i32) (i32.const 2))",
      "a": "(if (i32.load (i32.const 16)) (then (return_call $two)))"}, 0, 90, True),
    ("a function of the loop's type handling an exception",
     {"a": "(block $h (try_table (catch_all $h) (nop)))"}, 0, 90, True),
    ("the register exported", {"funcs": '(export "r1" (global $r1))'}, 0, 90, True),
    ("the register read outside", {"funcs": "(func $peek (result i32) (global.get $r1))"},
     0, 90, True),
]


def module(subs):
    d = dict(DEFAULTS)
    d.update(subs)
    return BASE.format(**d)


def main():
    root = os.path.dirname(HERE)
    os.chdir(root)
    build = os.environ.get("BUILD", "build")
    warmc = os.path.join(build, "warmc")
    target = gen_gosp.target_flags(build)
    run = os.environ.get("WARMC_RUN", "").split()
    sh = lambda c, **kw: subprocess.run(c, capture_output=True, text=True, **kw)
    fails = []
    with tempfile.TemporaryDirectory() as tmp:
        for ci, (what, subs, want_n, want, runs) in enumerate(CASES):
            wat, wasm = os.path.join(tmp, "m.wat"), os.path.join(tmp, "m.wasm")
            with open(wat, "w") as f:
                f.write(module(subs))
            r = sh([os.path.join(build, "tools", "bin", "wasm-tools"), "parse", wat, "-o", wasm])
            if r.returncode:
                fails.append("%s: not assembled: %s" % (what, r.stderr.strip()[:200]))
                continue
            got = {}
            # -fexact-trap turns the rewrite off: the registers in arguments
            # are not where a trap would see them
            for level in ("-O0", "-O2", "-O2 -fexact-trap"):
                # a binary of its own per case: Windows refuses to write over
                # the image of one that has only just exited
                binary = os.path.join(tmp, "m%d%s" % (ci, level.replace(" ", "")))
                if runs:
                    binary = hostbuild.exe(binary)
                out = target + ["-o", binary] if runs else ["-c", "-o", binary + ".o"]
                r = sh([warmc] + level.split() + ["--stats"] + out + [wasm], timeout=60)
                if r.returncode:
                    fails.append("%s: warmc %s: %s" % (what, level, r.stderr.strip()[:300]))
                    break
                m = re.search(r"\btramp_funcs=(\d+)\b", r.stderr)
                n = int(m.group(1)) if m else -1
                if n != (want_n if level == "-O2" else 0):
                    fails.append("%s: tramp.c rewrote %d functions at %s, not %d"
                                 % (what, n, level, want_n if level == "-O2" else 0))
                    break
                if runs:
                    p = sh(run + [binary, "run"], timeout=30)
                    got[level] = p.stdout.strip() or p.stderr.strip()[:200]
            else:
                if runs and not (got["-O0"] == got["-O2"] == got["-O2 -fexact-trap"] ==
                                 "i32:%d" % want):
                    fails.append("%s: run() is %s at -O0, %s at -O2 and %s with -fexact-trap, not %d"
                                 % (what, got["-O0"], got["-O2"], got["-O2 -fexact-trap"], want))
    for f in fails:
        print("FAIL[tramp]: %s" % f)
    if not fails:
        print("tramp: the rewrite made where it applies, with what a run inside another and a "
              "caught exception need, and in none of %d modules that rule it out"
              % sum(1 for c in CASES if not c[2]))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
