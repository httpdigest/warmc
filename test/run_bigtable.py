"""br_table's code tables, past where an arm64 halfword reaches.

An entry is a halfword, its target's distance from the table in
instructions: 128 KB either way (src/isel_ops.inc T_BRTABLE).  A table of
70000 entries is 140 KB itself, so every target after it is out of that
reach, and the function has to be made again with its tables of words
(isel.c's far retry).  Two such functions -- one whose edges are plain
jumps, whose entries name the target blocks, and one whose edges move a
value first, whose entries name stubs after the table -- answer for indices
spread over the table and past it.  A function with a table of 41 keeps its
halfwords: an odd number of them, which the code after has to be aligned
past.  At -O0 and -O2, and with every edge through a stub of its own after
the table (WARMC_NO_DIRECT_BRTABLE), whose entries name the stubs.

Usage: python3 test/run_bigtable.py
"""
import os
import platform
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_gosp

ROOT = os.path.dirname(HERE)
BUILD = os.environ.get("BUILD", "build")
WARMC = os.path.join(ROOT, BUILD, "warmc")
N = 70000
SMALL = 41
PICKS = [0, 1, 2, 3, 17, 34999, 35000, 69997, 69998, 69999, 70000, 1 << 30]


def module():
    labels = " ".join(str(i % 3) for i in range(N))
    moves = " ".join(str(i % 3) for i in range(N))
    return """(module
  (func $plain (export "plain") (param $i i32) (result i32)
    (block $c (block $b (block $a
      (br_table %s 2 (local.get $i)))
      (return (i32.const 11)))
      (return (i32.const 22)))
    (i32.const 33))
  ;; each edge carries a value, which its stub moves before it jumps
  (func $moves (export "moves") (param $i i32) (result i32)
    (i32.add (i32.const 100)
      (block $c (result i32) (block $b (result i32) (block $a (result i32)
        (br_table %s 2 (i32.mul (local.get $i) (i32.const 7)) (local.get $i)))
        (return (i32.add (i32.const 11))))
        (return (i32.add (i32.const 22))))))
  (func $small (export "small") (param $i i32) (result i32)
    (block $c (block $b (block $a
      (br_table %s 2 (local.get $i)))
      (return (i32.const 1)))
      (return (i32.const 2)))
    (i32.const 3)))
""" % (labels, moves, " ".join(str(i % 3) for i in range(SMALL)))


def want(name, i):
    k = min(i, N) % 3 if i < N else 2
    if name == "plain":
        return [11, 22, 33][k]
    if name == "moves":   # the two that return leave before the outer add
        return [11 + i * 7, 22 + i * 7, 100 + i * 7][k] & 0xffffffff
    return [1, 2, 3][i % 3] if i < SMALL else 3


def main():
    fails = []
    flags = gen_gosp.target_flags(BUILD)
    with tempfile.TemporaryDirectory() as tmp:
        wat = os.path.join(tmp, "bigtable.wat")
        with open(wat, "w") as f:
            f.write(module())
        for level, stubs in (("-O0", False), ("-O2", False), ("-O0", True), ("-O2", True)):
            exe = os.path.join(tmp, "bigtable" + level + ("-stubs" if stubs else ""))
            env = dict(os.environ, WARMC_NO_DIRECT_BRTABLE="1") if stubs else None
            r = subprocess.run([WARMC, level] + flags + ["-o", exe, wat],
                               capture_output=True, text=True, env=env)
            level += " with stubs" if stubs else ""
            if r.returncode:
                fails.append("%s: warmc: %s" % (level, r.stderr.strip()[:300]))
                continue
            for name in ("plain", "moves", "small"):
                for i in PICKS + list(range(8)) + [39, 40, 41, 42]:
                    r = subprocess.run([exe, name, str(i)], capture_output=True, text=True)
                    got = r.stdout.strip()
                    exp = "i32:%d" % (want(name, i) - (1 << 32) if want(name, i) >= 1 << 31
                                      else want(name, i))
                    if got != exp:
                        fails.append("%s %s(%d): %s, want %s" % (level, name, i, got, exp))
            # which loads the tables take: words for the big ones, halfwords
            # for the small one
            if platform.machine() not in ("arm64", "aarch64"):
                continue   # x64's tables are of words throughout
            r = subprocess.run(["llvm-objdump", "-d", "--no-show-raw-insn", exe],
                               capture_output=True, text=True)
            if r.returncode == 0:
                fn, loads = None, {}
                for line in r.stdout.split("\n"):
                    if line.endswith(">:"):
                        fn = line.split("<")[-1][:-2].lstrip("_")
                    elif "ldrsh" in line or "ldrsw" in line:
                        loads.setdefault(fn, set()).add("ldrsh" if "ldrsh" in line else "ldrsw")
                for name, kind in (("plain", "ldrsw"), ("moves", "ldrsw"), ("small", "ldrsh")):
                    if level.startswith("-O2") and name != "small" and kind not in loads.get(name, set()):
                        fails.append("%s %s: its table is not of words: %s"
                                     % (level, name, sorted(loads.get(name, set()))))
                    if name == "small" and "ldrsh" not in loads.get(name, set()):
                        fails.append("%s small: its table is not of halfwords: %s"
                                     % (level, sorted(loads.get(name, set()))))
    for f in fails[:20]:
        print("FAIL[bigtable] " + f)
    if fails:
        return 1
    print("bigtable: tables of 70000 entries, words past a halfword's reach, at -O0 and -O2")
    return 0


if __name__ == "__main__":
    sys.exit(main())
