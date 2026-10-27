"""Mutation testing: each mutant of a transform -- a test it makes dropped
or loosened, a bound moved, an operand swapped -- must change what some
test sees, or the tests do not pin the transform down.

  python3 test/mutate.py SPEC.py [-j J] [NAME ...]

SPEC.py, one per transform under test/mutants, defines

    M = {name: (path, [(old, new), ...]), ...}
    SUITES = ((label, argv, env), ...)
    EQUIVALENT = {name: why, ...}        (optional)

A mutant replaces each old, which must occur in path exactly once, by its
new.  The suites run in order, from the repository's root, with BUILD set;
the first that fails kills the mutant, and one that passes them all
survives.  EQUIVALENT names the mutants no test can kill because they
change nothing a program can see -- each with the reason -- which are
reported but do not fail the run.  The run fails where any other survives
or where an old no longer occurs once in the source as it is now.

Without rebuilding the world: each of J workers (4 by default) is a
directory under build/mut with a copy of src/ of its own beside symlinks to
the rest of the repository and of build/ -- corpora, tools, runtimes, the
test cache, whose results are keyed by contents and so are as good for a
mutant's programs as for any -- and builds only warmc there, without LTO,
so that a mutant costs one object's compile and a link.  A worker's warmc,
built, must compile the optimiser's cases (test/cases_opt) to the bytes the
tree's build/warmc does -- once more after building it all anew -- or the
run stops: a mutant tried on some other warmc tells nothing.  What a suite
writes into build/ is the worker's own (OWN): two mutants' programs must
never meet under one name.  Workers run mutants side by side.
"""
import concurrent.futures
import glob
import hashlib
import os
import shutil
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
WS = os.path.join(ROOT, "build", "mut")
CFLAGS = ("-std=c11 -O2 -fvisibility=hidden -fno-exceptions -g0 "
          "-Wall -Wextra -Wno-unused-parameter")
OWN = {"obj", "warmc", "testrun", "checklogs", "obj-asan", "obj-tsan", "warmc.dSYM", "mut",
       "gobench", "rsbench", "cbench", "zigbench", "hsbench", "asbench", "cppbench",
       "vecbench", "stdgo", "ktbench", "javabench"}


def sh(cmd, cwd, env=None, t=900):
    try:
        r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=t,
                           env=dict(os.environ, **(env or {})))
        return r.returncode, r.stdout + r.stderr
    except subprocess.TimeoutExpired:
        return -9, "TIMEOUT"


def sync(src, dst):
    """dst's files made src's: one that differs copied with the time now, so
    that make compiles it again.  Not a copy that keeps src's times: a
    worker's last mutant, written back after its suites, is compiled back
    only by the next mutant's make -- a copy dated as the tree's file, older
    than that mutant's object, left the next run's worker built with it,
    and every mutant of that run killed or not by the leftover one."""
    os.makedirs(dst, exist_ok=True)
    want = set(os.listdir(src))
    for e in os.listdir(dst):
        if e not in want:
            p = os.path.join(dst, e)
            shutil.rmtree(p) if os.path.isdir(p) and not os.path.islink(p) else os.remove(p)
    for e in want:
        a, b = os.path.join(src, e), os.path.join(dst, e)
        if os.path.isdir(a):
            sync(a, b)
            continue
        with open(a, "rb") as f:
            data = f.read()
        try:
            with open(b, "rb") as f:
                if f.read() == data:
                    continue
        except OSError:
            pass
        with open(b, "wb") as f:
            f.write(data)


def objects(warmc, mods):
    """The digest of what warmc compiles the modules to, at -O2"""
    h = hashlib.sha256()
    tmp = os.path.join(WS, "check-%d.o" % abs(hash(warmc)))
    for m in mods:
        r = subprocess.run([warmc, "-O2", "-c", "-o", tmp, m], capture_output=True)
        h.update(b"%d" % r.returncode)
        if r.returncode == 0:
            with open(tmp, "rb") as f:
                h.update(f.read())
    os.remove(tmp)
    return h.hexdigest()


def reference():
    """The optimiser's cases as modules, and what build/warmc makes of them"""
    d = os.path.join(WS, "cases")
    os.makedirs(d, exist_ok=True)
    wt = os.path.join(ROOT, "build", "tools", "bin", "wasm-tools")
    mods = []
    for p in sorted(glob.glob(os.path.join(HERE, "cases_opt", "*.wat"))):
        out = os.path.join(d, os.path.basename(p)[:-4] + ".wasm")
        r = subprocess.run([wt, "parse", p, "-o", out], capture_output=True, text=True)
        if r.returncode:
            sys.exit("mutate: %s does not assemble (make wasm-tools)\n%s" % (p, r.stderr))
        mods.append(out)
    return mods, objects(os.path.join(ROOT, "build", "warmc"), mods)


def setup(k, ref):
    """Worker k's directory, its src/ the tree's as it is now, warmc built"""
    w = os.path.join(WS, "w%d" % k)
    os.makedirs(w, exist_ok=True)
    for e in os.listdir(ROOT):
        p = os.path.join(w, e)
        if e not in ("src", "build", ".git") and not os.path.lexists(p):
            os.symlink(os.path.join(ROOT, e), p)
    sync(os.path.join(ROOT, "src"), os.path.join(w, "src"))
    b = os.path.join(w, "build")
    os.makedirs(b, exist_ok=True)
    for e in OWN:
        if os.path.islink(os.path.join(b, e)):
            os.remove(os.path.join(b, e))
    for e in os.listdir(os.path.join(ROOT, "build")):
        p = os.path.join(b, e)
        if e not in OWN and not os.path.lexists(p):
            os.symlink(os.path.join(ROOT, "build", e), p)
    rc, out = sh(["make", "-j8", "BUILD=build", "CFLAGS=" + CFLAGS, "build/warmc"], w)
    if rc:
        sys.exit("mutate: worker %d's warmc does not build\n%s" % (k, out[-2000:]))
    for again in (True, False):
        if objects(os.path.join(w, "build", "warmc"), ref[0]) == ref[1]:
            return w
        if again:
            # an object newer than its source and still not of it: all anew
            rc, out = sh(["make", "-B", "-j8", "BUILD=build", "CFLAGS=" + CFLAGS, "build/warmc"], w)
            if rc:
                sys.exit("mutate: worker %d's warmc does not build\n%s" % (k, out[-2000:]))
    sys.exit("mutate: worker %d's warmc compiles test/cases_opt otherwise than build/warmc "
             "does: build/warmc is not src/'s (make), or warmc's code depends on how warmc "
             "was built" % k)


def main():
    args = sys.argv[1:]
    if not args or args[0].startswith("-"):
        sys.exit(__doc__)
    spec = args.pop(0)
    jobs = 4
    if "-j" in args:
        i = args.index("-j")
        jobs = int(args[i + 1])
        del args[i:i + 2]
    ns = {}
    with open(spec) as f:
        exec(f.read(), ns)
    M, SUITES, EQUIV = ns["M"], ns["SUITES"], ns.get("EQUIVALENT", {})
    names = args or list(M)
    orig, stale = {}, []
    for n in names:
        path, edits = M[n]
        if path not in orig:
            with open(os.path.join(ROOT, path)) as f:
                orig[path] = f.read()
        for a, _ in edits:
            if orig[path].count(a) != 1:
                stale.append("%s: %s occurs %d times in %s"
                             % (n, a.splitlines()[0].strip()[:60], orig[path].count(a), path))
    if stale:
        sys.exit("mutate: the spec no longer fits the source\n  " + "\n  ".join(stale))
    t0 = time.time()
    os.makedirs(WS, exist_ok=True)
    ref = reference()
    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        workers = list(ex.map(lambda k: setup(k, ref), range(jobs)))
    print("%d workers built in %.0fs" % (jobs, time.time() - t0), flush=True)
    free = list(workers)
    lock = threading.Lock()

    def one(n):
        with lock:
            w = free.pop()
        try:
            path, edits = M[n]
            s = orig[path]
            for a, b in edits:
                s = s.replace(a, b)
            dst = os.path.join(w, path)
            with open(dst, "w") as f:
                f.write(s)
            t = time.time()
            rc, out = sh(["make", "BUILD=build", "CFLAGS=" + CFLAGS, "build/warmc"], w)
            if rc:
                res, killed = "does not build: " + out[-160:].replace("\n", " "), True
            else:
                res, killed = [], False
                for label, cmd, env in SUITES:
                    rc, out = sh(cmd, w, dict(env, BUILD="build"))
                    res.append("%s:%s" % (label, "KILL" if rc else "pass"))
                    if rc:
                        killed = True
                        break
                res = " ".join(res)
            # the next mutant's make compiles this file back, being newer
            with open(dst, "w") as f:
                f.write(orig[path])
            print("%-18s %s  %.0fs" % (n, res, time.time() - t), flush=True)
            return n, killed
        finally:
            with lock:
                free.append(w)

    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        results = list(ex.map(one, names))
    survivors = [n for n, killed in results if not killed]
    bad = [n for n in survivors if n not in EQUIV]
    print("--- %d mutants in %.0fs: %d killed, %d equivalent, %d survive"
          % (len(names), time.time() - t0, len(names) - len(survivors),
             len(survivors) - len(bad), len(bad)))
    for n in survivors:
        print("  %-18s %s" % (n, EQUIV.get(n, "SURVIVES")))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
