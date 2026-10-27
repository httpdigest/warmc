"""This tree's warmc against another revision's, on the benchmark corpora:
the cycles each kernel takes, compiled by each, timed interleaved.

  python3 test/ab.py [--base REV] [-j J] [--rounds N] [--aa] [--fresh] [CORPUS ...]

The other warmc is REV's (HEAD by default), checked out as a worktree under
build/ab and built there once.  Kernels are the corpora's modules as
test/gobench.py left them in build/CORPUS, all of make's BENCH_CORPORA by
default, each compiled at -O2 by both, the programs cached under build/ab by
the compiler's, runtimes' and module's contents.

A kernel whose two programs are the same bytes is not timed, nor one whose
two are those an earlier run timed: its figure is that run's, kept in
build/ab/results.json under the two programs' digests, so that after a
change that moves three kernels only those three run again (--fresh times
every one).  J kernels run
at once (6 by default: the author's machine has 12 performance cores), each
its two programs in turn, the other order every round.  A program's figure
is its least: what a kernel takes is never less than its own work and
anything else running only adds -- the median of two copies of one program
differed by 8% where the least agreed to 0.1%.  So the first run, which
macOS checks, needs no run before it, and a kernel is done after 3 rounds
once each program's two least agree to 0.5%, or the two programs' to 1%;
the rest go on to N (7).  Kernels timed at once share the memory's
bandwidth, so one 2% or more apart is timed again alone, up to N rounds,
done once each program's two least agree, and that is what counts.  The whole corpus
takes about a minute and a half where one kernel at a time took half an
hour.  --aa times this tree's warmc against itself, built twice, each time
anew: what the
figures move by when nothing changed (on the author's machine a handful
of kernels 3-9% -- empty, memcpy, addf64, bintree, cbench/lexer and
asbench/virtual among them -- the rest within 2%; a figure kept from an
earlier run keeps such a kernel's luck, which --fresh redraws).

Cycles are what /usr/bin/time -l reports, so this runs on macOS only.
"""
import concurrent.futures
import hashlib
import json
import math
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import gen_gosp
import hostbuild

BUILD = os.path.join(ROOT, os.environ.get("BUILD", "build"))
AB = os.path.join(BUILD, "ab")
CACHE = os.path.join(AB, "cache")
RESULTS = os.path.join(AB, "results.json")
KEEP = 20000
CORPORA = ("gobench stdgo vecbench asbench rsbench ktbench zigbench cbench cppbench "
           "hsbench javabench").split()
ENV = dict(os.environ, WARMC_RANDOM_SEED="1")
FIRST = 3


def digest(paths):
    h = hashlib.sha256()
    for p in paths:
        with open(p, "rb") as f:
            h.update(f.read())
    return h.hexdigest()[:24]


def compiler(build):
    """warmc and the runtimes it links, and their digest"""
    files = [os.path.join(build, "warmc")] + sorted(
        os.path.join(build, f) for f in os.listdir(build) if f.endswith("-macos.a"))
    return gen_gosp.target_flags(build), digest(files)


def base_build(rev):
    """REV's build directory, made once in a worktree of its own"""
    sha = subprocess.run(["git", "rev-parse", "--verify", rev + "^{commit}"], cwd=ROOT,
                         capture_output=True, text=True, check=True).stdout.strip()
    wt = os.path.join(AB, "base-" + sha[:12])
    done = os.path.join(wt, ".ab-built")
    if not os.path.exists(done):
        if not os.path.exists(os.path.join(wt, ".git")):
            shutil.rmtree(wt, ignore_errors=True)
            subprocess.run(["git", "worktree", "prune"], cwd=ROOT, check=True)
            subprocess.run(["git", "worktree", "add", "--detach", wt, sha], cwd=ROOT,
                           check=True, capture_output=True)
        print("building %s's warmc in %s" % (sha[:12], os.path.relpath(wt, ROOT)), flush=True)
        r = subprocess.run(["make", "-j8", "all"], cwd=wt, capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit("ab: building %s failed:\n%s" % (sha[:12], r.stderr[-2000:]))
        open(done, "w").close()
    return os.path.join(wt, "build"), sha


def build(warmc, flags, cdig, wasm, used):
    # one name for every program, so that the ad-hoc signature, which
    # carries it, is the same for the same code
    d = os.path.join(CACHE, digest([wasm]) + cdig)
    used.add(os.path.basename(d))
    exe = os.path.join(d, "k")
    if os.path.exists(exe):
        return exe
    tmp = "%s.tmp%d" % (d, os.getpid())
    os.makedirs(tmp, exist_ok=True)
    r = subprocess.run([warmc] + flags + ["--wasi", "-O2", "-o", os.path.join(tmp, "k"), wasm],
                       capture_output=True, env=ENV)
    if r.returncode != 0:
        shutil.rmtree(tmp, ignore_errors=True)
        return None
    try:
        os.rename(tmp, d)
    except OSError:
        shutil.rmtree(tmp, ignore_errors=True)
    return exe


def cycles(exe):
    p = subprocess.run(["/usr/bin/time", "-l", exe], stdout=subprocess.DEVNULL,
                       stderr=subprocess.PIPE, text=True, errors="replace", env=ENV)
    m = re.search(r"(\d+)\s+cycles elapsed", p.stderr)
    return int(m.group(1)) if m else float("nan")


def settled(t):
    """whether a program's two least runs agree, so that more add nothing"""
    s = sorted(t)
    return s[1] <= s[0] * 1.005


def time_pair(a, b, n, near):
    """Each program's least cycles over at most n rounds and the rounds run;
    near: also done where the two agree to 1% (the first pass, which only
    looks for kernels that moved)"""
    ta, tb = [], []
    for r in range(n):
        for exe, acc in ((a, ta), (b, tb)) if r % 2 == 0 else ((b, tb), (a, ta)):
            acc.append(cycles(exe))
        if r + 1 >= FIRST and ((settled(ta) and settled(tb)) or
                               (near and abs(min(tb) / min(ta) - 1) < 0.01)):
            break
    return min(ta), min(tb), len(ta)


def geomean(rs):
    return math.exp(sum(map(math.log, rs)) / len(rs))


def main():
    args = sys.argv[1:]
    rev, jobs, rounds, aa, fresh = "HEAD", 6, 7, False, False
    while args and args[0].startswith("-"):
        o = args.pop(0)
        if o == "--base":
            rev = args.pop(0)
        elif o == "-j":
            jobs = int(args.pop(0))
        elif o == "--rounds":
            rounds = int(args.pop(0))
        elif o == "--aa":
            aa = True
        elif o == "--fresh":
            fresh = True
        else:
            sys.exit(__doc__)
    if not hostbuild.DARWIN:
        sys.exit("ab: cycles come from macOS's /usr/bin/time -l")
    corpora = args or CORPORA
    mods = []
    for c in corpora:
        d = os.path.join(BUILD, c)
        if not os.path.isdir(d):
            print("ab: no %s in %s (test/gobench.py --corpus %s makes it)" % (c, BUILD, c))
            continue
        mods += [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith(".wasm")]
    new_flags, new_dig = compiler(BUILD)
    ENV.update(os.environ)
    if aa:
        old_build, old_flags, old_dig = BUILD, new_flags, new_dig + "aa"
    else:
        old_build, sha = base_build(rev)
        old_flags, old_dig = compiler(old_build)
    used = set()
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 8) as ex:
        olds = list(ex.map(lambda w: build(os.path.join(old_build, "warmc"), old_flags,
                                           old_dig, w, used), mods))
        news = list(ex.map(lambda w: build(os.path.join(BUILD, "warmc"), new_flags,
                                           new_dig, w, used), mods))

    try:
        with open(RESULTS) as f:
            known = json.load(f)
    except (OSError, ValueError):
        known = {}
    keys = [digest([a]) + digest([b]) if a and b else None for a, b in zip(olds, news)]

    def one(i):
        a, b = olds[i], news[i]
        if not a or not b:
            return None
        if not aa:
            if keys[i][:24] == keys[i][24:]:
                return None, None, 0, 0
            if not fresh and keys[i] in known:
                return tuple(known[keys[i]]) + (-1,)
        r = time_pair(a, b, rounds, True)
        if abs(r[1] / r[0] - 1) >= 0.02:
            return r + (2,)
        return r + (1,)

    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        res = list(ex.map(one, range(len(mods))))
    rows, same, reused = [], 0, 0
    for i, r in enumerate(res):
        name = os.path.basename(os.path.dirname(mods[i])) + "/" + os.path.basename(mods[i])[:-5]
        if r is None:
            print("ab: %s does not compile" % name)
            continue
        ma, mb, k, how = r
        if k == 0:
            same += 1
            rows.append((name, 1.0))
            continue
        if how == -1:
            reused += 1
        elif how == 2:
            ma, mb, k = time_pair(olds[i], news[i], rounds, False)
        if not aa:
            known.pop(keys[i], None)
            known[keys[i]] = [ma, mb, k]
        if abs(mb / ma - 1) >= 0.02:
            print("%-28s %9.1fM -> %9.1fM  %.3f" % (name, ma / 1e6, mb / 1e6, mb / ma),
                  flush=True)
        rows.append((name, mb / ma))
    if not aa:
        # the most recently used KEEP, oldest first
        known = dict(list(known.items())[-KEEP:])
        with open(RESULTS + ".tmp", "w") as f:
            json.dump(known, f)
        os.replace(RESULTS + ".tmp", RESULTS)
    by = {}
    for name, q in rows:
        by.setdefault(name.split("/")[0], []).append(q)
    for c, qs in by.items():
        print("%-10s geomean %.4f over %d" % (c, geomean(qs), len(qs)))
    if rows:
        print("all        geomean %.4f over %d, %d the same program, %d timed before"
              % (geomean([q for _, q in rows]), len(rows), same, reused))
    # the programs this run did not use go: the cache holds one run's
    for d in os.listdir(CACHE):
        if d not in used and ".tmp" not in d:
            shutil.rmtree(os.path.join(CACHE, d), ignore_errors=True)


if __name__ == "__main__":
    main()
