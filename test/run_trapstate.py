"""What a trap leaves behind, read by the calls after it on one instance,
under -fexact-trap.

warmc's own runner ends the process at a trap, so nothing there can look
at memory or a global afterwards; an embedder can, and a library is called
again after one.  With -fexact-trap the stores and global sets before a
trap must have happened, and none after it, at every level -- a pass that
deletes a store as dead across something that can trap, keeps a global in
a register past a trap, pairs two stores into one that a fault can leave
half made, or moves a trap above a store breaks that.  Without the flag
that state is unspecified.  Each test/cases_trapstate/*.wat says what to
call and what each call answers:

    ;; run: EXPORT:ARG,ARG EXPORT => trap 1

one process per line, the calls in order on one instance
(test/link/statedriver.c), "trap" for a call that traps.  A line

    ;; stats: NAME>0 ...

asks of warmc --stats at -O2 with the flag that each counter be above 0,
where the target is AArch64: for what the hardware here cannot show, such
as a store that a fault leaves half made on other AArch64 cores than
Apple's.  An x64 store that faults writes nothing, so there none is made.  The module is
compiled at -O0 to -O3 with -fexact-trap and -c, and linked with the driver.
"""
import concurrent.futures
import glob
import os
import re
import subprocess
import sys
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
LEVELS = ["-O0", "-O1", "-O2", "-O3"]
RUN_TIMEOUT = float(os.environ.get("WARMC_RUN_TIMEOUT", "30"))
RUN_LINE = re.compile(r"^;; run: (.*?) => (.*)$")
STATS_LINE = re.compile(r"^;; stats: (.*)$")


def main():
    root = os.path.dirname(HERE)
    os.chdir(root)
    build = os.environ.get("BUILD", "build")
    warmc = os.path.join(build, "warmc")
    sys.path.insert(0, HERE)
    import gen_gosp
    target = os.environ.get("WARMC_TARGET_FLAGS", "").split() or gen_gosp.target_flags(build)
    run = os.environ.get("WARMC_RUN", "").split()
    wt = os.path.join(build, "tools", "bin", "wasm-tools")
    sh = lambda c, **kw: subprocess.run(c, capture_output=True, text=True, **kw)
    fails = []
    cases = sorted(glob.glob(os.path.join("test", "cases_trapstate", "*.wat")))
    # under build, not a temp dir: MSYS2's /tmp is a path the Windows linker
    # cannot open
    tmp = os.path.join(build, "trapstate")
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    drv = os.path.join(tmp, "drv.o")
    r = sh(["python3", "test/hostbuild.py", "cc", "test/link/statedriver.c", drv])
    if r.returncode:
        print("FAIL[trapstate]: the driver does not build: %s" % (r.stdout + r.stderr)[:300])
        return 1

    def one(path):
        name = os.path.splitext(os.path.basename(path))[0]
        lines = [m.groups() for m in (RUN_LINE.match(l.rstrip("\n")) for l in open(path)) if m]
        if not lines:
            return ["%s: no ;; run: lines" % name]
        wasm = os.path.join(tmp, name + ".wasm")
        r = sh([wt, "parse", path, "-o", wasm])
        if r.returncode:
            return ["%s: not assembled: %s" % (name, r.stderr.strip()[:200])]
        bad = []
        want_stats = [w for m in (STATS_LINE.match(l) for l in open(path)) if m
                      for w in m.group(1).split()]
        if want_stats and "--target=windows" not in target:
            r = sh([warmc, "-O2", "-fexact-trap", "--stats"] + target +
                   ["-c", "-o", os.path.join(tmp, name + ".st.o"), wasm])
            for w in want_stats:
                key = w.split(">")[0]
                m = re.search(r"\b%s=(\d+)\b" % re.escape(key), r.stderr)
                if not m or int(m.group(1)) <= 0:
                    bad.append("%s: --stats %s is %s, not above 0"
                               % (name, key, m.group(1) if m else "missing"))
        for level in LEVELS:
            obj = os.path.join(tmp, "%s%s.o" % (name, level))
            r = sh([warmc, level, "-fexact-trap"] + target +
                   ["--symbol-prefix=ts", "-c", "-o", obj, wasm])
            if r.returncode:
                bad.append("%s %s: warmc: %s" % (name, level, r.stderr.strip()[:200]))
                continue
            prog = os.path.join(tmp, "%s%s" % (name, level))
            r = sh(["python3", "test/hostbuild.py", "link", prog, drv, obj],
                   env=dict(os.environ, BUILD=build))
            if r.returncode:
                bad.append("%s %s: link: %s" % (name, level, (r.stdout + r.stderr).strip()[:200]))
                continue
            r = sh(["python3", "test/hostbuild.py", "exe", prog])
            exe = r.stdout.strip() or prog
            for calls, want in lines:
                try:
                    p = sh(run + [exe] + calls.split(), timeout=RUN_TIMEOUT)
                except subprocess.TimeoutExpired:
                    bad.append("%s %s: %s runs past %g s, not [%s]"
                               % (name, level, calls, RUN_TIMEOUT, want))
                    continue
                got = " ".join(p.stdout.split())
                if p.returncode or got != want:
                    bad.append("%s %s: %s gives [%s], not [%s]" % (name, level, calls, got, want))
        return bad

    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        for b in ex.map(one, cases):
            fails += b
    for f in fails:
        print("FAIL[trapstate]: %s" % f)
    if not fails:
        print("trapstate: %d modules leave what a trap should behind at %s with -fexact-trap"
              % (len(cases), ", ".join(LEVELS)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
