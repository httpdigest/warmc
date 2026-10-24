"""gosp.c, Go's stack pointer as an argument: where it applies and where not.

Its rewrite changes the signature of every function of Go's type, so the
conditions under which it is made are what keeps it unobservable.  Each
module here is a small one in Go's shape with one thing more that rules the
rewrite out:
- no go:buildid section, or one whose name only starts with it and a NUL;
- the pointer exported or imported or not global 0;
- a Go function exported, taken by ref.func or in a declarative segment;
- a table exported or imported;
- a tail call, exception handling, table.get, call_ref, array.new_elem or
  array.init_elem;
- a subtype, or Go's type not final;
- an imported function of Go's type in the table, as a table's initial
  value, or taken by ref.func.
With -fexact-trap, where a trap must see the pointer in its global, none
is made, the control's included.
Each must compile with the rewrite not made (--stats says gosp_funcs=0)
and, where the interpreter can run it (no imports and no subtypes), give
the interpreter's results.  Each is the one test of its condition: removing
the condition from gosp.c fails it.  The module without the addition is the
control, rewritten and with the same results; so is the module that writes
the rec group gosp.c appends for its new type, which then takes another
empty type.  wasm-tools assembles them, since wabt reads no subtypes.  A
reference the host is handed to a function that reads the pointer is
test/link/gosp_b.wat's.

Then test/gen_gosp.py's random modules in Go's shape, against wasm-interp
at -O1 and -O2, each of which gosp.c must rewrite whole.
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import compare as cmp_mod
import gen_gosp

BASE = r'''(module
  {custom}
  (type $go {gotype})
  {types}
  {imports}
  (memory 1)
  {globals}
  (global $sp (mut i32) (i32.const 8192))
  (table 4 funcref)
  (elem (i32.const 1) $leaf $mid {elem})
  (func $leaf (type $go)
    (global.set $sp (i32.add (global.get $sp) (i32.const 8)))
    (i32.const 0))
  (func $mid (type $go)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 16)))
    (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))
    (if (call $leaf (i32.const 0)) (then (return (i32.const 1))))
    (global.set $sp (i32.add (global.get $sp) (i32.const 24)))
    (i32.const 0))
  {funcs}
  (func (export "run") (result i32)
    (global.set $sp (i32.const 8192))
    (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))
    (drop (call_indirect (type $go) (i32.const 0) (i32.const 2)))
    (global.get $sp))
)
'''

GO = '(func (param i32) (result i32))'
CUSTOM = '(@custom "go:buildid" "run_gosp")'

# (what, substitutions, whether the interpreter can run it)
CASES = [
    ("no go:buildid section", {"custom": ""}, True),
    # the name is the section's bytes, which may go on past a NUL
    ("a section named go:buildid, a NUL and more", {"custom": '(@custom "go:buildid\\00x" "run_gosp")'},
     True),
    ("the pointer exported", {"funcs": '(export "sp" (global $sp))'}, True),
    ("the pointer not global 0", {"globals": "(global $x (mut i64) (i64.const 0))"}, True),
    ("the pointer imported", {"imports": '(import "env" "x" (global $x (mut i32)))'}, False),
    ("a Go function exported", {"funcs": '(export "leaf" (func $leaf))'}, True),
    # a function in an active segment needs no declaration to be taken
    ("a Go function taken by ref.func", {"funcs": "(func $r (drop (ref.func $leaf)))"}, True),
    ("a Go function in a declarative segment", {"funcs": "(elem declare func $leaf)"}, True),
    ("a table exported", {"funcs": '(export "t" (table 0))'}, True),
    ("a table imported", {"imports": '(import "env" "t" (table 1 funcref))'}, False),
    ("a tail call",
     {"funcs": "(func $t1 (param i64) (result i64) (return_call $t2 (local.get 0)))"
               " (func $t2 (param i64) (result i64) (local.get 0))"}, True),
    ("exception handling", {"funcs": "(tag $e) (func $thr (throw $e))"}, True),
    ("table.get", {"funcs": "(func $tg (result funcref) (table.get 0 (i32.const 1)))"}, True),
    ("call_ref",
     {"types": "(type $vf (func))", "funcs": "(func $cr (param (ref $vf)) (call_ref $vf (local.get 0)))"},
     True),
    ("a subtype", {"types": "(type $s (sub (func))) (type $s2 (sub $s (func)))"}, False),
    ("Go's type not final", {"gotype": "(sub %s)" % GO}, False),
    ("an import of Go's type in the table",
     {"imports": '(import "env" "f" (func $imp (type $go)))', "elem": "$imp"}, False),
    ("an import of Go's type as a table's initial value",
     {"imports": '(import "env" "f" (func $imp (type $go)))',
      "funcs": "(table $t2 2 funcref (ref.func $imp))"}, False),
    ("an exported import of Go's type taken by ref.func",
     {"imports": '(import "env" "f" (func $imp (type $go)))',
      "funcs": '(export "imp" (func $imp)) (func $r (drop (ref.func $imp)))'}, False),
    ("array.new_elem",
     {"types": "(type $fa (array funcref))",
      "funcs": "(elem $ps func $leaf) (func $an (drop (array.new_elem $fa $ps (i32.const 0) (i32.const 1))))"},
     False),
    ("array.init_elem",
     {"types": "(type $fa (array (mut funcref)))",
      "funcs": "(elem $ps func $leaf) (func $ai (array.init_elem $fa $ps (array.new_default $fa (i32.const 1))"
               " (i32.const 0) (i32.const 0) (i32.const 1)))"}, False),
]

# the rec group gosp.c appends for a type of its own, written by the module:
# the rewrite is still made, its group taking another empty type to be new
COLLIDE = "the rewrite's new type's rec group written by the module"
COLLIDE_TYPES = "(rec (type $p (func (param i32 i32) (result i64))) (type $e (func)))"

def module(subs):
    d = {"custom": CUSTOM, "gotype": GO, "types": "", "imports": "", "globals": "", "elem": "",
         "funcs": ""}
    d.update(subs)
    return BASE.format(**d)

def main():
    root = os.path.dirname(HERE)
    os.chdir(root)
    build = os.environ.get("BUILD", "build")
    warmc = os.path.join(build, "warmc")
    import hostbuild
    wabt = hostbuild.wabt_flags()
    target = gen_gosp.target_flags(build)
    sh = lambda c, **kw: subprocess.run(c, capture_output=True, text=True, **kw)
    fails = []
    checked = 0
    with tempfile.TemporaryDirectory() as tmp:
        for ci, (what, subs, runs) in enumerate(
                [("the control", {}, True), (COLLIDE, {"types": COLLIDE_TYPES}, False)] + CASES):
            wat, wasm = os.path.join(tmp, "m.wat"), os.path.join(tmp, "m.wasm")
            with open(wat, "w") as f:
                f.write(module(subs))
            # wasm-tools, as wabt has no subtypes
            r = sh([os.path.join(build, "tools", "bin", "wasm-tools"), "parse", wat, "-o", wasm])
            if r.returncode:
                fails.append("%s: not assembled: %s" % (what, r.stderr.strip()[:200]))
                continue
            # one of its own per case: Windows refuses to write over the
            # image of one that has only just exited
            binary = os.path.join(tmp, "m%d.bin" % ci)
            out = ["-c", "-o", os.path.join(tmp, "m.o")] if not runs else target + ["-o", binary]
            r = sh([warmc, "-O2", "--stats"] + out + [wasm], timeout=60)
            if r.returncode:
                fails.append("%s: warmc: %s" % (what, r.stderr.strip()[:300]))
                continue
            want = 2 if what in ("the control", COLLIDE) else 0
            m = re.search(r"\bgosp_funcs=(\d+)\b", r.stderr)
            if not m or int(m.group(1)) != want:
                fails.append("%s: gosp.c rewrote %s Go functions, not %d"
                             % (what, m.group(1) if m else "?", want))
                continue
            if want:
                # and none with -fexact-trap, where a trap must see the
                # pointer in its global
                r = sh([warmc, "-O2", "-fexact-trap", "--stats", "-c", "-o",
                        os.path.join(tmp, "x.o"), wasm], timeout=60)
                m = re.search(r"\bgosp_funcs=(\d+)\b", r.stderr)
                if r.returncode or not m or int(m.group(1)) != 0:
                    fails.append("%s: gosp.c rewrote %s Go functions with -fexact-trap, not 0"
                                 % (what, m.group(1) if m else "?"))
                    continue
            if runs:
                ref = os.path.join(tmp, "m.ref")
                r = sh(["wasm-interp"] + wabt + ["--run-all-exports", wasm], timeout=60)
                with open(ref, "w") as f:
                    f.write(r.stdout + r.stderr)
                n, bad, msgs = cmp_mod.compare(ref, binary, "keep", run=[], timeout=30)
                if bad or not n:
                    fails.append("%s: %d/%d exports differ from the interpreter: %s"
                                 % (what, bad, n, "; ".join(msgs[:2])))
                    continue
            checked += 1
    for f in fails:
        print("FAIL[gosp]: %s" % f)
    bad = gen_gosp.fuzz(1, int(os.environ.get("GOSP_SEEDS", "200")), ["-O1", "-O2"], [],
                        os.path.abspath(os.path.join(build, "fuzz-gosp")))
    if not fails and not bad:
        print("gosp: the rewrite made in the control and in none of %d modules that rule it out, "
              "and random modules in Go's shape agree with wasm-interp" % len(CASES))
    return 1 if fails or bad else 0

if __name__ == "__main__":
    sys.exit(main())
