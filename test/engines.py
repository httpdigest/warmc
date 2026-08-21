"""Compare warmc against the general-purpose wasm engines on the same modules.

warmc compiles ahead of time to a native executable; wasmtime, wasmer and
wazero compile at load time and run the module in a host process.  Comparing
them fairly means comparing the same thing, so this harness does not use any
engine's own timing facility:

  * **One module per case, entered through `_start`.**  Every engine here can
    run a `_start` export and only some can invoke an arbitrary one by name,
    so the workload is compiled once per case with the argument baked in.
    warmc's runtime calls `_start` too when given no export name, so all four
    sides run byte-identical wasm through their own front door.

  * **Whole-process wall clock**, because that is all three engines expose,
    and warmc is measured the same way rather than with its in-process timer.

  * **Minus that engine's own floor.**  A module whose `_start` returns
    immediately is timed the same way, and its time is subtracted.  For the
    engines that floor is process start plus parsing plus compiling the
    module, and it is reported on its own line because for short runs it is
    most of the answer.  For warmc it is exec and the guard reservation.

The `startup` column is therefore the price of running a module at all, and
the per-case columns are what the compiled code costs once it is running.

Each launch also records process CPU time (user+system) and peak resident
memory.  On POSIX both come from wait4()'s rusage.  On Windows they come
from test/winmeasure.c, a Job-object wrapper built on first use: MSYS
python execs a native PE through a Cygwin proxy stub and its wait4() rusage
describes the stub, not the child, so nothing short of a native wrapper
measures the right process — and the Job also counts helper subprocesses an
engine may spawn, and reports the tree's peak commit charge (for warmc that
includes the probe-free stacks, whose commit equals their reserve).  CPU
time gets the same startup-floor treatment as wall clock; memory is
reported absolute, because the startup column IS an engine's baseline
footprint for running a module at all.
"""
import os
import subprocess
import sys
import time

BUILD = os.environ.get("BUILD", "build")
WORK = os.path.join(BUILD, "engines")
os.makedirs(WORK, exist_ok=True)

CASES = [
    ("sha_rounds", 250000, 1),
    ("sort_check", 8192, 200),
    ("matmul", 48, 400),
    ("mix64", 10000000, 1),
    ("dispatch", 100000000, 1),
    ("nbody", 300000, 1),
    ("strings", 1000000, 1),
    ("bitops", 10000000, 1),
]

ENGINES = [
    ("wasmtime", ["wasmtime", "run"]),
    ("wasmer", ["wasmer", "run"]),
    ("wazero", ["wazero", "run"]),
]

def sh(cmd):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True)

def host_setup():
    if os.environ.get("WARMC_TARGET_FLAGS"):
        return (os.environ["WARMC_TARGET_FLAGS"],
                (os.environ.get("WARMC_RUN") or "").split())
    r = subprocess.run('. test/hostenv.sh; echo "$WARMC_TARGET_FLAGS"; echo "$WARMC_RUN"; '
                       'echo "$PATH"; echo "$WARMC_MACOS_LIBDIR"',
                       shell=True, capture_output=True, text=True,
                       env=dict(os.environ, BUILD=BUILD), executable="/bin/bash")
    lines = r.stdout.splitlines() + ["", "", "", ""]
    os.environ["PATH"] = lines[2] or os.environ["PATH"]
    if lines[3]:
        os.environ["WARMC_MACOS_LIBDIR"] = lines[3]
    return lines[0], (lines[1].split() if lines[1] else [])

TARGET_FLAGS, RUN = host_setup()

WINMEASURE = None
if "--target=windows" in TARGET_FLAGS:
    WINMEASURE = os.path.join(BUILD, "winmeasure.exe")
    src = os.path.join("test", "winmeasure.c")
    if (not os.path.exists(WINMEASURE)
            or os.path.getmtime(WINMEASURE) < os.path.getmtime(src)):
        r = sh("clang -O2 -o %s %s" % (WINMEASURE, src))
        if r.returncode:
            print(r.stderr)
            sys.exit(1)

SHIM = """#include "workload.c"
static volatile unsigned long long sink;
__attribute__((export_name("_start"))) void _start(void) {%s}
"""

def build_case(name, call):
    """One wasm module and one warmc-compiled executable per case."""
    src = os.path.join(WORK, name + ".c")
    with open(src, "w") as fh:
        fh.write(SHIM % call)
    wasm = os.path.join(WORK, name + ".wasm")
    r = sh("clang --target=wasm32 -O2 -nostdlib -ffp-contract=off -I test/bench "
           f"-Wl,--no-entry -Wl,--export-dynamic -o {wasm} {src}")
    if r.returncode:
        print(r.stderr)
        sys.exit(1)
    exe = os.path.join(WORK, name)
    r = sh(f"./{BUILD}/warmc -O2 {TARGET_FLAGS} -o {exe} {wasm}")
    if r.returncode:
        print(r.stdout + r.stderr)
        sys.exit(1)
    return wasm, exe

def measure_once(argv):
    """One launch: (wall_ms, cpu_ms, peak_rss_kb, commit_kb) or None on failure.
    cpu/rss are None where the platform cannot report them; commit is
    Windows-only (peak commit charge of the whole process tree)."""
    if WINMEASURE:
        rpt = os.path.join(WORK, "measure.txt")
        p = subprocess.run([WINMEASURE, rpt, "300000"] + argv,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if p.returncode != 0:
            return None
        with open(rpt) as fh:
            wall, user, kern, ws, commit = map(int, fh.read().split())
        return wall / 1e6, (user + kern) / 1e6, ws / 1024.0, commit / 1024.0
    t0 = time.perf_counter()
    p = subprocess.Popen(argv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if hasattr(os, "wait4"):
        _, status, ru = os.wait4(p.pid, 0)
        wall = time.perf_counter() - t0
        p.returncode = os.waitstatus_to_exitcode(status)
        if p.returncode != 0:
            return None
        rss = ru.ru_maxrss / (1024.0 if sys.platform == "darwin" else 1.0)
        return wall * 1e3, (ru.ru_utime + ru.ru_stime) * 1e3, rss, None
    rc = p.wait()
    wall = time.perf_counter() - t0
    if rc != 0:
        return None
    return wall * 1e3, None, None, None

def measure(argv, reps):
    """Minimum of each metric over `reps` launches."""
    best = None
    for _ in range(reps):
        one = measure_once(argv)
        if one is None:
            return None
        best = one if best is None else tuple(
            None if a is None or b is None else min(a, b)
            for a, b in zip(best, one))
    return best

METRICS = [
    ("wall clock per launch, milliseconds", 0, True, 1.0),
    ("process CPU time per launch, user+system, milliseconds", 1, True, 1.0),
    ("peak resident memory (maxrss / working set), MB, absolute", 2, False, 1024.0),
    ("peak commit charge of the process tree, MB, absolute", 3, False, 1024.0),
]

def cell(ms, name, i, scale):
    v = ms.get(name)
    return None if v is None or v[i] is None else v[i] / scale

def main():
    args = sys.argv[1:]
    reps = 5
    if "--reps" in args:
        i = args.index("--reps")
        reps = int(args[i + 1])
        del args[i:i + 2]

    engines = [(n, c) for n, c in ENGINES if sh("command -v " + c[0]).returncode == 0]
    missing = [n for n, _ in ENGINES if n not in [e[0] for e in engines]]
    if missing:
        print("not installed, skipped: " + ", ".join(missing))

    cases = [("startup", "")] + [
        (n, f" for (int i = 0; i < {r}; i++) sink = (unsigned long long){n}({a});"
            if r > 1 else f" sink = (unsigned long long){n}({a});")
        for n, a, r in CASES]
    built = {}
    for name, call in cases:
        built[name] = build_case(name, call)

    names = [c[0] for c in cases]
    hdr = [n for n in names if n != "startup"]
    rows = []
    rows.append(("warmc -O2 (AOT)",
                 {n: measure(RUN + [built[n][1]], reps) for n in names}))
    for label, cmd in engines:
        rows.append((label, {n: measure(cmd + [built[n][0]], reps) for n in names}))

    print("\nminimum over %d launches; times minus each engine's startup floor" % reps)
    for title, i, sub, scale in METRICS:
        if all(cell(ms, n, i, scale) is None for _, ms in rows for n in names):
            continue
        print("\n" + title)
        print("%-18s %10s |" % ("engine", "startup") +
              "".join("%11s" % n[:10] for n in hdr))
        print("-" * (31 + 11 * len(hdr)))
        for label, ms in rows:
            base = cell(ms, "startup", i, scale)
            line = "%-18s " % label
            line += "%10.1f" % base if base is not None else "%10s" % "-"
            line += " |"
            for n in hdr:
                v = cell(ms, n, i, scale)
                if v is None:
                    line += "%11s" % "-"
                else:
                    line += "%11.1f" % (max(v - base, 0.0) if sub and base else v)
            print(line)

    ref = rows[0][1]
    print("\nratio to warmc -O2 (higher = slower / bigger than warmc)")
    print("%-18s" % "engine" +
          "".join("%11s" % t for t in ("wall", "cpu", "peak-rss", "commit")) +
          "   geomean over the per-case columns")
    print("-" * 62)
    for label, ms in rows[1:]:
        line = "%-18s" % label
        for title, i, sub, scale in METRICS:
            base = (cell(ms, "startup", i, scale) or 0.0) if sub else 0.0
            rbase = (cell(ref, "startup", i, scale) or 0.0) if sub else 0.0
            ratios = []
            for n in hdr:
                a, b = cell(ref, n, i, scale), cell(ms, n, i, scale)
                if a is not None and b is not None and a - rbase > 0:
                    ratios.append((b - base) / (a - rbase))
            g = 1.0
            for r in ratios:
                g *= r
            line += ("%10.2fx" % g ** (1.0 / len(ratios))) if ratios else "%11s" % "-"
        print(line)

main()
