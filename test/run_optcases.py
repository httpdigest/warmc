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

LEVELS = ["-O0", "-O1", "-O2", "-O3"]
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


def shape_fails(name, exe, want):
    """Each (function, pattern, count) of want against exe's code."""
    d = subprocess.run([OBJDUMP, "-d", "--no-show-raw-insn", exe], capture_output=True,
                       text=True).stdout
    fails = []
    for fn, pat, n in want:
        body = re.search(r"<_?%s>:\n(.*?)(?:\n\n|\Z)" % re.escape(fn), d, re.S)
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
    counts = stats(path)
    for level in LEVELS:
        exe = hostbuild.exe(os.path.join(tmp, name + level))
        r = subprocess.run([warmc, level] + target + ["-o", exe, wasm] +
                           (["--stats"] if level in counts else []),
                           capture_output=True, text=True, timeout=120)
        if r.returncode:
            fails.append("%s %s: warmc: %s" % (name, level, r.stderr.strip()[:300]))
            continue
        want_shape = shapes(path)
        if want_shape and level == "-O2" and AARCH64 and OBJDUMP:
            fails += shape_fails(name, exe, want_shape)
        if level in counts:
            got = r.stderr.split()
            for c in counts[level]:
                if c not in got:
                    have = [g for g in got if g.startswith(c.split("=")[0] + "=")]
                    fails.append("%s %s: --stats has %s, not %s"
                                 % (name, level, " ".join(have) or "nothing", c))
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
