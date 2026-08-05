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
"""
import os
import re
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

def timed(argv, reps):
    """Minimum wall clock over `reps` launches, in milliseconds."""
    best = None
    for _ in range(reps):
        t0 = time.perf_counter()
        p = subprocess.run(argv, capture_output=True)
        dt = time.perf_counter() - t0
        if p.returncode != 0:
            return None
        if best is None or dt < best:
            best = dt
    return best * 1e3

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
    rows = []
    warmc_ms = {}
    for name in names:
        wasm, exe = built[name]
        warmc_ms[name] = timed(RUN + [exe], reps)
    rows.append(("warmc -O2 (AOT)", warmc_ms))
    for label, cmd in engines:
        ms = {}
        for name in names:
            wasm, _exe = built[name]
            ms[name] = timed(cmd + [wasm], reps)
        rows.append((label, ms))

    hdr = [n for n in names if n != "startup"]
    print("\nwall clock per launch, minimum of %d, milliseconds" % reps)
    print("%-18s %10s |" % ("engine", "startup") +
          "".join("%11s" % n[:10] for n in hdr))
    print("-" * (31 + 11 * len(hdr)))
    for label, ms in rows:
        base = ms.get("startup") or 0.0
        print("%-18s %10.1f |" % (label, base) +
              "".join("%11.1f" % max(ms[n] - base, 0.0) if ms.get(n) else "%11s" % "-"
                      for n in hdr))

    ref = rows[0][1]
    rbase = ref.get("startup") or 0.0
    print("\nratio to warmc -O2 (higher = slower than warmc)")
    print("%-18s %10s |" % ("engine", "startup") +
          "".join("%11s" % n[:10] for n in hdr) + "%11s" % "geomean")
    print("-" * (42 + 11 * len(hdr)))
    for label, ms in rows[1:]:
        base = ms.get("startup") or 0.0
        cells, ratios = [], []
        for n in hdr:
            a, b = ref.get(n), ms.get(n)
            if not a or not b or a - rbase <= 0:
                cells.append("%11s" % "-")
            else:
                r = (b - base) / (a - rbase)
                ratios.append(r)
                cells.append("%10.2fx" % r)
        g = 1.0
        for r in ratios:
            g *= r
        g = g ** (1.0 / len(ratios)) if ratios else 0.0
        print("%-18s %9.1fx |" % (label, base / rbase if rbase else 0) +
              "".join(cells) + "%10.2fx" % g)

main()
