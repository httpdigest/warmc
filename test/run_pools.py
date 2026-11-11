"""The constant-expression and root-list pools (src/module_data.c).

A module's constant expressions are kept as programs the runtime's
evaluator runs, and its safepoints' root lists as runs of offsets; each
pool hands an expression or a list that is there already the one copy.  A
root list is as often the end of a longer one -- the roots left when the
first have died -- so every tail of a run is recorded with it, each tail's
hash made in the one pass from the end (ro_hash_tails).  An expression's
tails are no expressions, and are not recorded: they were once, each
hashed from its own start, which made an initialiser's cost the square of
its length -- 2.7 s of a 3.2 s compile for a string constant of 25,820
bytes that luawasm makes an array.new_fixed of.

  consts   three arrays of 100,000 constants each: two the same expression,
           which is kept once -- and evaluated twice, to two arrays that a
           write to one tells apart -- and one that differs from them in
           its first constant alone.  The compile has the CPU time any has
           (6 s; hashing each tail from its start took forty), elements of
           each array answer, and the pool shares the one expression's
           100,003 words
  roots    functions of 80 references from calls, all alive over the first
           of 80 more calls and one fewer over each after, dying in the
           order they were made: where the registers and slots they are
           in rise in that order, each call's list is a tail of the one
           before, of more entries than the 64 a list has on the stack
           while it is made.  The last few made are in registers that sort
           first (three of them, on arm64), so in seven functions the last
           none to six made die before the rest.  Their answers, with
           every collection moving every object too; and on arm64 the
           entries shared are 2,000 more than with the whole runs alone
           recorded (WARMC_NO_POOL_TAILS)
  tails    the object is the same bytes when every tail is looked up
           after one was found (WARMC_POOL_ALL_TAILS): the search stops at
           the first one found, which was recorded with the rest

Usage: python3 test/run_pools.py
"""
import os
import platform
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_gosp
import hostbuild

ROOT = os.path.dirname(HERE)
BUILD = os.environ.get("BUILD", "build")
WARMC = os.environ.get("WARMC_BIN") or os.path.join(ROOT, BUILD, "warmc")
N = 100000
NREF = 80
CPU_LIMIT = 6.0
ARM64 = platform.machine() in ("arm64", "aarch64")


def val(i):
    return (i * 7 + 3) & 0xff


def consts_module():
    body = " ".join("(i32.const %d)" % val(i) for i in range(1, N))
    arr = lambda first: "(array.new_fixed $Str %d (i32.const %d) %s)" % (N, first, body)
    return """(module
  (type $Str (array (mut i8)))
  (global $a (ref $Str) %s)
  (global $b (ref $Str) %s)
  (global $c (ref $Str) %s)
  (func $pick (param $w i32) (result (ref $Str))
    (if (result (ref $Str)) (i32.eqz (local.get $w))
      (then (global.get $a))
      (else (if (result (ref $Str)) (i32.eq (local.get $w) (i32.const 1))
        (then (global.get $b)) (else (global.get $c))))))
  (func (export "get") (param $w i32) (param $i i32) (result i32)
    (array.get_u $Str (call $pick (local.get $w)) (local.get $i)))
  (func (export "len") (param $w i32) (result i32)
    (array.len (call $pick (local.get $w))))
  ;; a's element written, b's read: two arrays, of one expression
  (func (export "apart") (param $i i32) (result i32)
    (array.set $Str (global.get $a) (local.get $i) (i32.const 201))
    (i32.add (i32.mul (array.get_u $Str (global.get $a) (local.get $i)) (i32.const 1000))
             (i32.add (array.get_u $Str (global.get $b) (local.get $i))
                      (ref.eq (global.get $a) (global.get $b))))))
""" % (arr(val(0)), arr(val(0)), arr(77))


def consts_want(w, i):
    return 77 if w == 2 and i == 0 else val(i)


def roots_module():
    L = ["(module", "  (type $Box (struct (field (mut i32))))",
         "  (global $n (mut i32) (i32.const 0))",
         "  ;; a box of v, made at the end of a recursion: no part of its caller",
         "  (func $mk (param $v i32) (param $d i32) (result (ref $Box))",
         "    (if (result (ref $Box)) (i32.eqz (local.get $d))",
         "      (then (struct.new $Box (local.get $v)))",
         "      (else (call $mk (local.get $v) (i32.sub (local.get $d) (i32.const 1))))))",
         "  ;; counts, and allocates so that a collection can come",
         "  (func $churn (param $d i32) (result i32)",
         "    (if (result i32) (i32.eqz (local.get $d))",
         "      (then (global.set $n (i32.add (global.get $n) (i32.const 1)))",
         "            (struct.get $Box 0 (call $mk (global.get $n) (i32.const 1))))",
         "      (else (call $churn (i32.sub (local.get $d) (i32.const 1))))))"]
    for name, order in ORDERS:
        L += ['  (func (export "%s") (param $k i32) (result i32)' % name, "    (local $s i32)"]
        L += ["    (local $r%d (ref null $Box))" % i for i in range(NREF)]
        for i in range(NREF):
            L.append("    (local.set $r%d (call $mk (i32.add (local.get $k) (i32.const %d)) (i32.const 1)))" % (i, i))
        for j in order:
            L.append("    (local.set $s (i32.add (local.get $s) (call $churn (i32.const 2))))")
            L.append("    (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))"
                     " (struct.get $Box 0 (local.get $r%d))))" % j)
        L += ["    (local.get $s))"]
    L.append(")")
    return "\n".join(L) + "\n"


# the order the references die in: the last m made, then the rest as made
ROTS = 7
ORDERS = tuple(("rot%d" % m, list(range(NREF - m, NREF)) + list(range(NREF - m)))
               for m in range(ROTS))


def roots_want(name, k):
    s, n = 0, 0
    for j in dict(ORDERS)[name]:
        n += 1
        s = (s + n) & 0xffffffff
        s = (s * 31 + k + j) & 0xffffffff
    return s - (1 << 32) if s >> 31 else s


def compile_limited(args, env=None):
    """(error or None, stderr) of a compile with CPU_LIMIT seconds of CPU"""
    p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
    try:
        _, err = hostbuild.communicate_limited(p, CPU_LIMIT)
    except subprocess.TimeoutExpired:
        p.kill()
        p.communicate()
        return "the compile ran past %g s of CPU" % CPU_LIMIT, ""
    return (("warmc: " + err.strip()[:300]) if p.returncode else None), err


def shared(err, what):
    m = re.search(r"(\d+) %s" % what, err)
    return int(m.group(1)) if m else None


def main():
    fails = []
    flags = gen_gosp.target_flags(BUILD)
    stats = dict(os.environ, WARMC_TABLE_STATS="1")
    with tempfile.TemporaryDirectory() as tmp:
        # consts
        wat = os.path.join(tmp, "consts.wat")
        with open(wat, "w") as f:
            f.write(consts_module())
        exe = os.path.join(tmp, "consts")
        bad, err = compile_limited([WARMC, "-O2"] + flags + ["-o", exe, wat], stats)
        if bad:
            fails.append("consts: " + bad)
        else:
            got = shared(err, "constant-pool instructions shared")
            if got != N + 3:
                fails.append("consts: %s words of constant programs shared, not the %d of "
                             "the expression made twice" % (got, N + 3))
            for w in range(3):
                for i in (0, 1, 2, 63, 64, 255, 256, 4095, 50000, N - 2, N - 1):
                    r = subprocess.run([exe, "get", str(w), str(i)], capture_output=True, text=True)
                    if r.stdout.strip() != "i32:%d" % consts_want(w, i):
                        fails.append("consts: get(%d, %d) is %s, not %d"
                                     % (w, i, r.stdout.strip() or r.stderr.strip()[:80], consts_want(w, i)))
                r = subprocess.run([exe, "len", str(w)], capture_output=True, text=True)
                if r.stdout.strip() != "i32:%d" % N:
                    fails.append("consts: len(%d) is %s, not %d" % (w, r.stdout.strip(), N))
            for i in (0, 5, N - 1):
                r = subprocess.run([exe, "apart", str(i)], capture_output=True, text=True)
                if r.stdout.strip() != "i32:%d" % (201000 + val(i)):
                    fails.append("consts: apart(%d) is %s, not %d: the two arrays of one "
                                 "expression are not two" % (i, r.stdout.strip(), 201000 + val(i)))
        # roots
        rwat = os.path.join(tmp, "roots.wat")
        with open(rwat, "w") as f:
            f.write(roots_module())
        for level in ("-O0", "-O2"):
            exe = os.path.join(tmp, "roots" + level)
            bad, err = compile_limited([WARMC, level] + flags + ["-o", exe, rwat], stats)
            if bad:
                fails.append("roots %s: %s" % (level, bad))
                continue
            got = shared(err, "root entries shared")
            bad, err = compile_limited([WARMC, level] + flags + ["-c", "-o", exe + ".o", rwat],
                                       dict(stats, WARMC_NO_POOL_TAILS="1"))
            whole = shared(err, "root entries shared")
            if bad:
                fails.append("roots %s: %s" % (level, bad))
            elif ARM64 and (got is None or whole is None or got - whole < 2000):
                fails.append("roots %s: %s root entries shared, and %s with the whole runs alone: "
                             "a function's lists that are tails of its first are not shared"
                             % (level, got, bad or whole))
            for genv in ({}, {"WARMC_GC_STRESS": "1", "WARMC_GC_RELOCATE_ALL": "1"}):
                for name, _ in ORDERS[::3]:
                    for k in (0, 5, 1000000):
                        r = subprocess.run([exe, name, str(k)], capture_output=True, text=True,
                                           env=dict(os.environ, **genv))
                        if r.stdout.strip() != "i32:%d" % roots_want(name, k):
                            fails.append("roots %s%s: %s(%d) is %s, not %d"
                                         % (level, " moving" if genv else "", name, k,
                                            r.stdout.strip() or r.stderr.strip()[:80],
                                            roots_want(name, k)))
        # tails
        for name, src in (("consts", wat), ("roots", rwat)):
            objs = []
            for all_tails in (False, True):
                obj = os.path.join(tmp, "%s.%d.o" % (name, all_tails))
                env = dict(os.environ, WARMC_POOL_ALL_TAILS="1") if all_tails else None
                bad, _ = compile_limited([WARMC, "-O2"] + flags + ["-c", "-o", obj, src], env)
                if bad:
                    fails.append("tails %s: %s" % (name, bad))
                    break
                objs.append(open(obj, "rb").read())
            if len(objs) == 2 and objs[0] != objs[1]:
                fails.append("tails %s: the object differs when every tail is looked up" % name)
    for f in fails[:20]:
        print("FAIL[pools] " + f)
    if fails:
        return 1
    print("pools: an expression of %d constants made twice is kept once, in a compile's time; "
          "the lists of %d roots share their tails" % (N, NREF))
    return 0


if __name__ == "__main__":
    sys.exit(main())
