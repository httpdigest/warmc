"""The optimiser's own regressions: small modules each of which some pass
once compiled wrong, run at every level against the answer it must give.

Each test/cases_opt/*.wat says what to run and what comes out, one line per
call:

    ;; run: EXPORT ARGS... => i32:42
    ;; run: EXPORT ARGS... => trap: out of bounds memory access

and a module can say what warmc's --stats counts for it, at -O0, where no
pass changes what there is to count, or at a level it names:

    ;; stats: slot_tests=20 slot_nullchks=7
    ;; stats -O2: once_dead=1

or, with a64, what only the AArch64 back end's target counts, checked where
warmc makes AArch64 code (everywhere but Windows, whose is x86-64):

    ;; stats -O2 a64: wrap_params=3

and how many instructions of a function's AArch64 code at -O2 match a
pattern, checked on an AArch64 host -- for what changes the code and not
what it computes:

    ;; shape: FUNCTION REGEX=N

The answer is worked out by hand from the module, so it holds whichever
level is wrong, -O0 included; the module is compiled at -O0, -O1, -O2 and
-O3 and each call's output must be it.  A pass the module was written for
is named in its header.  wasm-tools assembles the modules, since several
use garbage collection, which wabt does not read.
"""
import concurrent.futures
import glob
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_gosp
import hostbuild
import testcache

LEVELS = ["-O0", "-O1", "-O2", "-O3"]
RUN_TIMEOUT = float(os.environ.get("WARMC_RUN_TIMEOUT", "30"))
RUN_LINE = re.compile(r"^;; run: (\S+)((?: \S+)*) => (.*)$")
STATS_LINE = re.compile(r"^;; stats(?: (-O[0-3]))?( a64)?: (.*)$")
SHAPE_LINE = re.compile(r"^;; shape: (\S+) (.*)=(\d+)$")
OBJDUMP = shutil.which("llvm-objdump") or shutil.which("objdump")
AARCH64 = platform.machine() in ("arm64", "aarch64")


def calls(path):
    out = []
    for line in open(path):
        m = RUN_LINE.match(line.rstrip("\n"))
        if m:
            out.append((m.group(1), m.group(2).split(), m.group(3).strip()))
    return out


def stats(path):
    """{level: [name=value, ...]} of a module's stats lines"""
    out = {}
    for line in open(path):
        m = STATS_LINE.match(line.rstrip("\n"))
        if m and not (m.group(2) and hostbuild.WINDOWS):
            out.setdefault(m.group(1) or "-O0", []).extend(m.group(3).split())
    return out


def shapes(path):
    out = []
    for line in open(path):
        m = SHAPE_LINE.match(line.rstrip("\n"))
        if m:
            out.append((m.group(1), m.group(2), int(m.group(3))))
    return out


def shape_fails(name, dump, want):
    """Each (function, pattern, count) of want against the disassembly, where
    the case's functions are named CASE.FUNCTION"""
    fails = []
    for fn, pat, n in want:
        body = re.search(r"<_?%s\.%s>:\n(.*?)(?:\n\n|\Z)" % (re.escape(name), re.escape(fn)), dump, re.S)
        if not body:
            fails.append("%s -O2: no function %s in the code" % (name, fn))
            continue
        # nops after the function's last instruction are the padding the
        # next function starts after, not its own
        lines = body.group(1).split("\n")
        while lines and re.search(r"\snop$", lines[-1]):
            lines.pop()
        got = len([l for l in lines if re.search(pat, l)])
        if got != n:
            fails.append("%s -O2: %s has %d instructions matching %s, not %d"
                         % (name, fn, got, pat, n))
    return fails


def check_all(cases, build, warmc, target, run, tmp):
    """Every case at every level: one program a level holding them all
    (warmc given several modules), asked for a case by its name -- a program
    of each case at each level was a hundred and sixty that macOS checks on
    their first run.  A case's --stats come from compiling it alone, to an
    program of its own that never runs, and its shapes from the -O2 program,
    where its functions are CASE.FUNCTION."""
    wt = os.path.join(build, "tools", "bin", "wasm-tools")
    fails, names, wasms = [], [], []
    for c in cases:
        name = os.path.splitext(os.path.basename(c))[0]
        if not calls(c):
            fails.append("%s: no ;; run: lines" % name)
            continue
        wasm = os.path.join(tmp, name + ".wasm")
        r = subprocess.run([wt, "parse", c, "-o", wasm], capture_output=True, text=True)
        if r.returncode:
            fails.append("%s: not assembled: %s" % (name, r.stderr.strip()[:200]))
            continue
        names.append((name, c))
        wasms.append(wasm)

    def program(level):
        exe = hostbuild.exe(os.path.join(tmp, "all" + level))
        r = subprocess.run([warmc, level] + target + ["-o", exe] + wasms,
                           capture_output=True, text=True, timeout=600)
        if r.returncode == 0:
            return exe, []
        # which case: each alone
        out = []
        for (name, _), wasm in zip(names, wasms):
            r = subprocess.run([warmc, level] + target + ["-c", "-o", exe + ".o", wasm],
                               capture_output=True, text=True, timeout=120)
            if r.returncode:
                out.append("%s %s: warmc: %s" % (name, level, r.stderr.strip()[:300]))
        return None, out or ["%s: warmc: %s" % (level, r.stderr.strip()[:300])]

    def counted(job):
        (name, c), wasm, level, want = job
        # a program, as the cases' is (an object's module is not its program's
        # only instance, which some counts depend on), but never run
        r = subprocess.run([warmc, level] + target + ["--stats", "-o",
                            hostbuild.exe(os.path.join(tmp, "%s%s" % (name, level))), wasm],
                           capture_output=True, text=True, timeout=120)
        got = r.stderr.split()
        out = []
        for w in want:
            if w not in got:
                have = [g for g in got if g.startswith(w.split("=")[0] + "=")]
                out.append("%s %s: --stats has %s, not %s"
                           % (name, level, " ".join(have) or "nothing", w))
        return out

    def launch(argv):
        try:
            p = subprocess.run(run + argv, capture_output=True, text=True, timeout=RUN_TIMEOUT)
            return p.returncode, p.stdout, p.stderr
        except subprocess.TimeoutExpired:
            return None, "", ""

    def ran(job):
        exe, level, name, export, args, expect = job
        # kept under the program's bytes (test/testcache.py): a program warmc
        # made again the same answers the same
        rc, out, err = testcache.run(exe, run + [name, export] + args,
                                     lambda: launch([exe, name, export] + args))
        if rc is None:
            return ["%s %s: %s %s runs past %g s, not %s"
                    % (name, level, export, " ".join(args), RUN_TIMEOUT, expect)]
        got = out.strip()
        if expect.startswith("trap"):
            err = err.strip()
            ok = rc != 0 and ("trap" in err) and expect[5:].strip() in err
            got = err.splitlines()[-1] if err else "exit %d" % rc
        else:
            ok = rc == 0 and got == expect
        return [] if ok else ["%s %s: %s %s gives %s, not %s"
                              % (name, level, export, " ".join(args), got or "nothing", expect)]

    width = os.cpu_count() or 4
    with concurrent.futures.ThreadPoolExecutor(width) as ex:
        progs = dict(zip(LEVELS, ex.map(program, LEVELS)))
        jobs = [(nc, w, lv, want) for nc, w in zip(names, wasms)
                for lv, want in stats(nc[1]).items()]
        for f in ex.map(counted, jobs):
            fails += f
        runs = []
        for level in LEVELS:
            exe, f = progs[level]
            fails += f
            if exe is None:
                continue
            for name, c in names:
                runs += [(exe, level, name, e, a, x) for e, a, x in calls(c)]
        for f in ex.map(ran, runs):
            fails += f
    exe = progs["-O2"][0]
    if exe and AARCH64 and OBJDUMP:
        dump = subprocess.run([OBJDUMP, "-d", "--no-show-raw-insn", exe], capture_output=True,
                              text=True).stdout
        for name, c in names:
            want = shapes(c)
            if want:
                fails += shape_fails(name, dump, want)
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
        fails = check_all(cases, build, warmc, target, run, tmp)
    for f in fails:
        print("FAIL[optcases]: %s" % f)
    if not fails:
        print("optcases: %d modules the optimiser once compiled wrong give their answers at %s"
              % (len(cases), ", ".join(LEVELS)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
