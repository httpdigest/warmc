"""What a trap leaves behind, read by the calls after it on one instance.

warmc's own runner ends the process at a trap, so nothing there can look
at memory or a global afterwards; an embedder can, and a library is called
again after one.  So the stores and global sets before a trap must have
happened, and none after it, at every level -- a pass that deletes a store
as dead across something that can trap, keeps a global in a register past
a trap, or moves a trap above a store breaks that.  Each
test/cases_trapstate/*.wat says what to call and what each call answers:

    ;; run: EXPORT:ARG,ARG EXPORT => trap 1

one process per line, the calls in order on one instance
(test/link/statedriver.c), "trap" for a call that traps.  The module is
compiled at -O0 to -O3 with -c and linked with the driver.
"""
import concurrent.futures
import glob
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
LEVELS = ["-O0", "-O1", "-O2", "-O3"]
RUN_LINE = re.compile(r"^;; run: (.*?) => (.*)$")


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
    with tempfile.TemporaryDirectory() as tmp:
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
            for level in LEVELS:
                obj = os.path.join(tmp, "%s%s.o" % (name, level))
                r = sh([warmc, level] + target + ["--symbol-prefix=ts", "-c", "-o", obj, wasm])
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
                    p = sh(run + [exe] + calls.split(), timeout=30)
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
        print("trapstate: %d modules leave what a trap should behind at %s"
              % (len(cases), ", ".join(LEVELS)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
