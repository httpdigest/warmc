"""The test suites at combinations of passes turned off.

A bug that needs two passes to meet -- one leaving a shape only the other
mishandles -- hides from a suite run at the defaults and with each pass off
alone.  All 2^46 combinations of the passes cannot be run, but every pair of
them in all four on/off combinations can, in nine settings: a covering
array of strength 2.  Its columns, one per pass, are distinct sets of five
of rows 2 to 9 (a pass is off in the rows of its set), and row 1 turns
nothing off.  Row 1 has every pair on together; any two sets of five of
eight rows meet, so some row has both off; and two distinct sets of one
size never contain one another, so each pair also has a row with the first
off and the second on, and one the other way round.  There are C(8, 5) = 56
such sets for the 46 passes.  --strength 3 covers every triple in all eight
combinations instead, with rows a seeded greedy search picks, which takes
about thirty.

The passes are all of -f's but four: fma and dead-frames change what a
program can see, which these suites compare, and have rows of their own;
gssa-all is not a pass but a stronger gssa; and exact-trap is none either,
but a mode that turns trampoline, go-sp and dead-frames off, with rows of
its own too.  This script reads src/main.c's
list and fails if a pass is missing from both PASSES and LEFT_OUT, so a new
pass joins the combinations.

  python3 test/run_combos.py                    # make check: pairs, cases + traps + gc
  python3 test/run_combos.py --strength 3 --suites cases,traps,gc &&
    python3 test/run_combos.py --suites corpora          # make check-combos
"""
import argparse
import itertools
import os
import random
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PASSES = """fold dce cse licm branch thread nodesplit mem bce strength inline reassoc
rotate addroff memptr align guard-pages regalloc split vectorize edge-tests
selftail ldcomb escape partial-escape gloop loopclose cpool ictab trampoline
go-sp gssa small-loop-align labcache barelide merge reach prune devirt
reinline throw-jump unroll ifconv mulwide gcpure closed-world""".split()
LEFT_OUT = {"fma", "dead-frames", "gssa-all", "exact-trap"}

SUITES = {
    "cases": ["./test/run_tests.sh"],
    "traps": ["./test/run_traps.sh"],
    "gc": ["./test/run_gc.sh"],
}
CORPORA = ["ctests", "cpptests", "rstests", "gotests", "stdgotests", "astests", "kttests",
           "zigtests", "hstests"]

def check_passes():
    src = open(os.path.join(ROOT, "src", "main.c")).read()
    names = set(re.findall(r'strcmp\(name, "([a-z-]+)"\)', src))
    missing = names - set(PASSES) - LEFT_OUT
    stale = (set(PASSES) | LEFT_OUT) - names
    return missing, stale

def pairs_array(k):
    """Strength 2: row 0 nothing off, column j off in the rows of its set."""
    n = 2
    while True:
        n += 1
        w = (n + 1) // 2
        sets = list(itertools.combinations(range(1, n), w))
        if len(sets) >= k:
            break
    rows = [[False] * k for _ in range(n)]
    for j in range(k):
        for r in sets[j]:
            rows[r][j] = True
    return rows

def triples_array(k, seed=1):
    """Strength 3, greedily: each row the best of a few hundred random ones
    at covering what no row has yet."""
    rng = random.Random(seed)
    trip = list(itertools.combinations(range(k), 3))
    need = {(t, v) for t in trip for v in range(8)}
    rows = []
    while need:
        best, best_n = None, -1
        for _ in range(300):
            row = [rng.random() < 0.5 for _ in range(k)]
            n = sum(((a, b, c), row[a] * 4 + row[b] * 2 + row[c]) in need for a, b, c in trip)
            if n > best_n:
                best, best_n = row, n
        rows.append(best)
        need -= {((a, b, c), best[a] * 4 + best[b] * 2 + best[c]) for a, b, c in trip}
    return rows

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--strength", type=int, default=2, choices=(2, 3))
    ap.add_argument("--suites", default="cases,traps,gc",
                    help="comma-separated: %s, corpora (the language test corpora), or all"
                    % ", ".join(SUITES))
    ap.add_argument("--print", action="store_true", help="print the settings and stop")
    args = ap.parse_args()
    os.chdir(ROOT)
    missing, stale = check_passes()
    if missing or stale:
        print("FAIL[combos]: src/main.c's passes and this script's disagree: %s"
              % ", ".join(["new %s" % p for p in sorted(missing)] + ["gone %s" % p for p in sorted(stale)]))
        return 1
    suites = list(SUITES) + ["corpora"] if args.suites == "all" else args.suites.split(",")
    rows = pairs_array(len(PASSES)) if args.strength == 2 else triples_array(len(PASSES))
    settings = [" ".join(["-O2"] + ["-fno-" + p for p, off in zip(PASSES, row) if off]) for row in rows]
    if args.print:
        for s in settings:
            print(s)
        return 0
    fails = 0
    for i, flags in enumerate(settings):
        for s in suites:
            if s == "corpora":
                cmds = [["python3", "test/gobench.py", "--corpus", c, "--check", "--variant", flags]
                        for c in CORPORA]
            else:
                cmds = [SUITES[s]]
            for cmd in cmds:
                # WARMC_COMBO only names the setting: its binaries and
                # objects go under their own name, apart from those of the
                # rows make check runs beside it at the same flags (row 1's
                # are -O2's, and the two used to overwrite each other)
                env = dict(os.environ, WARMC_FLAGS=flags, WARMC_COMBO=str(i + 1))
                p = subprocess.run(cmd, env=env, capture_output=True, text=True)
                if p.returncode:
                    fails += 1
                    name = cmd[3] if s == "corpora" else s
                    print("FAIL[combos]: setting %d (%s), %s:" % (i + 1, flags, name))
                    for line in (p.stdout + p.stderr).splitlines():
                        if "FAIL" in line or "differ" in line:
                            print("    " + line)
    if not fails:
        print("combos: %s at %d settings that turn every %s of %d passes off and on in every "
              "combination" % ("+".join(suites), len(settings),
                               "pair" if args.strength == 2 else "triple", len(PASSES)))
    return 1 if fails else 0

if __name__ == "__main__":
    sys.exit(main())
