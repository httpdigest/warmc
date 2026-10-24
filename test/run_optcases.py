"""The optimiser's own regressions: small modules each of which some pass
once compiled wrong, run at every level against the answer it must give.

Each test/cases_opt/*.wat says what to run and what comes out, one line per
call:

    ;; run: EXPORT ARGS... => i32:42
    ;; run: EXPORT ARGS... => trap: out of bounds memory access

The answer is worked out by hand from the module, so it holds whichever
level is wrong, -O0 included; the module is compiled at -O0, -O1, -O2 and
-O3 and each call's output must be it.  A pass the module was written for
is named in its header.  wasm-tools assembles the modules, since several
use garbage collection, which wabt does not read.
"""
import concurrent.futures
import glob
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_gosp

LEVELS = ["-O0", "-O1", "-O2", "-O3"]
RUN_LINE = re.compile(r"^;; run: (\S+)((?: \S+)*) => (.*)$")


def calls(path):
    out = []
    for line in open(path):
        m = RUN_LINE.match(line.rstrip("\n"))
        if m:
            out.append((m.group(1), m.group(2).split(), m.group(3).strip()))
    return out


def check(path, build, warmc, target, run, tmp):
    name = os.path.splitext(os.path.basename(path))[0]
    want = calls(path)
    if not want:
        return ["%s: no ;; run: lines" % name]
    wasm = os.path.join(tmp, name + ".wasm")
    r = subprocess.run([os.path.join(build, "tools", "bin", "wasm-tools"), "parse", path, "-o", wasm],
                       capture_output=True, text=True)
    if r.returncode:
        return ["%s: not assembled: %s" % (name, r.stderr.strip()[:200])]
    fails = []
    for level in LEVELS:
        exe = os.path.join(tmp, name + level)
        r = subprocess.run([warmc, level] + target + ["-o", exe, wasm], capture_output=True,
                           text=True, timeout=120)
        if r.returncode:
            fails.append("%s %s: warmc: %s" % (name, level, r.stderr.strip()[:300]))
            continue
        for export, args, expect in want:
            p = subprocess.run(run + [exe, export] + args, capture_output=True, text=True,
                               timeout=30)
            got = p.stdout.strip()
            if expect.startswith("trap"):
                err = p.stderr.strip()
                ok = p.returncode != 0 and ("trap" in err) and expect[5:].strip() in err
                got = err.splitlines()[-1] if err else "exit %d" % p.returncode
            else:
                ok = p.returncode == 0 and got == expect
            if not ok:
                fails.append("%s %s: %s %s gives %s, not %s"
                             % (name, level, export, " ".join(args), got or "nothing", expect))
    return fails


def main():
    root = os.path.dirname(HERE)
    os.chdir(root)
    build = os.environ.get("BUILD", "build")
    warmc = os.path.join(build, "warmc")
    target = gen_gosp.target_flags(build)
    run = os.environ.get("WARMC_RUN", "").split()
    cases = sorted(glob.glob(os.path.join("test", "cases_opt", "*.wat")))
    fails = []
    with tempfile.TemporaryDirectory() as tmp:
        with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
            for f in ex.map(lambda c: check(c, build, warmc, target, run, tmp), cases):
                fails += f
    for f in fails:
        print("FAIL[optcases]: %s" % f)
    if not fails:
        print("optcases: %d modules the optimiser once compiled wrong give their answers at %s"
              % (len(cases), ", ".join(LEVELS)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
