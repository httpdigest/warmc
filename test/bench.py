"""Benchmark warmc across optimisation levels and individual passes.

Three numbers per variant:

  * **time** -- nanoseconds per call of the exported function, measured inside
    the process so that exec, dyld and the guard reservation are not in it.
    Reported as the *median over launches* of the *minimum within a launch*.
    Inside one process every source of noise only adds time, so the minimum is
    right; across processes it is not, because address layout puts a workload
    into one of two regimes and the minimum then reports whichever regime was
    luckier. One workload here measures 108 us or 180 us depending on nothing
    but the launch, and a min-of-N of that is a coin toss, not a measurement.
    Only meaningful when the host runs arm64 natively; under emulation the
    driver falls back to reporting instruction counts alone.
  * **spread** -- the ratio of the slowest launch to the fastest, printed per
    case, so a bimodal workload is visible rather than silently averaged.
  * **dynamic instructions** -- exact, from `--profile`.  Machine-independent,
    and the only number that was available while this was developed under
    emulation.
  * **code size** -- the __text/.text of warmc's own object, so the runtime is
    not counted.

The same workload compiled straight to the host by clang is measured as a
fourth variant.  It is the ceiling: warmc is trying to reach it.

Methodology, because wall clock deserves the same care as anything else:
each launch reports the minimum of `--reps` in-process calls, the suite is
repeated `--rounds` times with the variants interleaved, and the per-variant
figure is the median of those launches.  Interleaving matters -- the machine
drifts by a few percent over minutes, so running all of -O0 and then all of
-O2 would attribute that drift to the compiler.  The runtime raises its QoS
class before timing so the scheduler keeps it on a performance core, and warms
up for 100 ms so the cluster is at its top frequency before anything is
recorded.  That ramp belongs to the machine rather than to any one workload,
so a launch benches every case in turn and pays for it once.

Options: `--rounds N` launches per variant, `--reps N` timed samples per
launch, `--raw` to use the `clang -O0` input, `--passes` to turn each pass off
in turn, `--ab CC` to interleave a second compiler, and `--cases a,b` to cut
the case list down while iterating on one workload.

`--module` picks what is benchmarked.  It takes a comma-separated list, and
when it names more than one the modules become variants alongside the
optimisation levels and are interleaved with them -- which is the only honest
way to compare `addr32` with `addr64`, since the whole point of those two is
the ratio between them and the machine drifts by a few percent over a minute.
Modules benched together have to export the same case names; the driver says
so rather than lining up two different things.
"""
import os
import re
import subprocess
import sys
import time

BUILD = os.environ.get("BUILD", "build")
WORK = os.path.join(BUILD, "bench")
os.makedirs(WORK, exist_ok=True)

NATIVE = os.path.join(WORK, "native")

WORKLOAD_CASES = [
    ("sha_rounds", 25000),
    ("sort_check", 8192),
    ("matmul", 48),
    ("mix64", 1000000),
    ("dispatch", 20000000),
    ("nbody", 30000),
    ("strings", 100000),
    ("bitops", 1000000),
]

VECWORK_CASES = [(n, 2) for n in (
    "v_axpy", "v_blend", "v_scale", "v_mix64", "v_fmul", "v_fsqrt", "v_sum",
    "v_xor", "v_bxor", "v_bmul", "v_hadd", "v_max", "v_clamp", "v_ptr",
    "v_miv", "v_wide", "v_down", "v_deint", "v_alt", "v_cload", "v_cstore",
    "v_short")]

ADDR_CASES = [("k_sum", 1024), ("k_axpy", 1024), ("k_gather", 1024),
              ("k_hist", 1024), ("k_bytes", 1024), ("k_chase", 1024)]

SIMD_CASES = [(n, 256) for n in (
    "s_dot", "s_bytes", "s_shuf", "s_widen", "s_cmp", "s_mask", "s_lane",
    "s_i64", "s_dotp", "s_fma")]

CONTROL_CASES = [("c_call", 200000), ("c_indirect", 200000),
                 ("c_callref", 200000), ("c_tail", 200000),
                 ("c_tailref", 200000), ("c_plain", 200000),
                 ("c_guard", 200000), ("c_region", 200000),
                 ("c_throw", 2000), ("c_deep", 2000)]

MODULES = {
    "workload":  ("test/bench/workload.c", WORKLOAD_CASES, True),
    "vecwork":   ("test/bench/vecwork.c", VECWORK_CASES, False),
    "addr32":    ("test/bench/addr32.wat", ADDR_CASES, False),
    "addrmulti": ("test/bench/addrmulti.wat", ADDR_CASES, False),
    "addr64":    ("test/bench/addr64.wat", ADDR_CASES, False),
    "simd":      ("test/bench/simd.wat", SIMD_CASES, False),
    "control":   ("test/bench/control.wat", CONTROL_CASES, False),
}
GENERATED = ("addr32", "addrmulti", "addr64")

CASES = WORKLOAD_CASES

PASSES = ["fold", "dce", "cse", "licm", "branch", "mem", "bce",
          "strength", "inline", "rotate", "addroff", "memptr", "align",
          "guard-pages", "regalloc", "vectorize", "selftail"]

def sh(cmd, **kw):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True, **kw)

def host_setup():
    """Reuse test/hostenv.sh rather than re-deriving the host rules here."""
    r = sh('. test/hostenv.sh; echo "$WARMC_TARGET_FLAGS"; echo "$WARMC_RUN"; '
           'echo "$WARMC_HOST"; echo "$PATH"; echo "$WARMC_MACOS_LIBDIR"; '
           'echo "$WABT_FLAGS"',
           env=dict(os.environ, BUILD=BUILD), executable="/bin/bash")
    lines = r.stdout.splitlines() + ["", "", "", "", "", ""]
    os.environ["PATH"] = lines[3] or os.environ["PATH"]
    if lines[4]:
        os.environ["WARMC_MACOS_LIBDIR"] = lines[4]
    return lines[0], (lines[1].split() if lines[1] else []), lines[2], lines[5]

TARGET_FLAGS, RUN, HOST, WABT_FLAGS = host_setup()
NATIVE_TIMING = not RUN

def build_module(name, raw):
    """The .wasm for one module, built the way its source asks to be."""
    src = MODULES[name][0]
    out = os.path.join(WORK, "%s%s.wasm" % (name, "_o0" if raw else ""))
    if src.endswith(".wat"):
        if name in GENERATED:
            if sh("python3 test/gen_bench.py").returncode:
                sys.exit(1)
        r = sh(f"wat2wasm {WABT_FLAGS} -o {out} {src}")
    else:
        opt = "-O0" if raw else "-O2"
        r = sh(f"clang --target=wasm32 {opt} -nostdlib -ffp-contract=off "
               f"-Wl,--no-entry -Wl,--export-dynamic -o {out} {src}")
    if r.returncode:
        print(r.stdout + r.stderr)
        sys.exit(1)
    return out

def build_native():
    r = sh(f"clang -O2 -ffp-contract=off -I test/bench -o {NATIVE} "
           "test/bench/native_main.c")
    if r.returncode:
        print(r.stderr)
        sys.exit(1)

def text_size(flags, wasm, cc=None):
    obj = os.path.join(WORK, "size.o")
    tgt = "--target=macos" if "macos" in TARGET_FLAGS else ""
    if sh(f"{cc or './' + BUILD + '/warmc'} {flags} {tgt} -c -o {obj} {wasm}").returncode:
        return 0
    for tool in ("llvm-size", "size"):
        r = sh(f"{tool} -A {obj}")
        if r.returncode == 0:
            m = re.search(r"^(?:__text|\.text)\s+(\d+)", r.stdout, re.M)
            if m:
                return int(m.group(1))
    return 0

def compile_at(flags, tag, wasm, cc=None):
    cc = cc or "./" + BUILD + "/warmc"
    out = os.path.join(WORK, "wl_" + tag.replace("/", "_"))
    r = sh(f"{cc} {flags} {TARGET_FLAGS} -o {out} {wasm}")
    if r.returncode:
        print(f"compile failed for {tag}:\n{r.stdout}\n{r.stderr}")
        return None, None, 0
    prof = out + "_p"
    sh(f"{cc} {flags} --profile {TARGET_FLAGS} -o {prof} {wasm}")
    return out, prof, text_size(flags, wasm, cc)

def emitted_insts(flags, wasm, cc=None):
    cc = cc or "./" + BUILD + "/warmc"
    tgt = "--target=macos" if "macos" in TARGET_FLAGS else ""
    r = sh(f"{cc} {flags} {tgt} --stats -c -o /dev/null {wasm}")
    m = re.search(r"insts=(\d+)", r.stderr)
    return int(m.group(1)) if m else 0

def bench(binary, cases, reps):
    """One launch, every case: {name: (min ns per call, value returned)}.

    The runtime benches each export in turn and the cluster only has to reach
    its top frequency once, so this costs one 100 ms ramp instead of one per
    case.  It also means all the cases in a launch share an address layout,
    which is what the median across launches is there to average over anyway.
    """
    argv = []
    for fn, arg in cases:
        if argv:
            argv.append("--")
        argv += [fn, str(arg)]
    p = subprocess.run(RUN + [binary, "--bench", str(reps)] + argv,
                       capture_output=True)
    times = dict(re.findall(r"bench: (\S+) n=\d+ min_ns=(\d+)", p.stderr.decode()))
    values = p.stdout.decode().split()
    out = {}
    for i, (fn, _arg) in enumerate(cases):
        out[fn] = (int(times.get(fn, 0)),
                   values[i] if i < len(values) else "")
    return out

def icount(prof, fn, arg):
    p = subprocess.run(RUN + [prof, fn, str(arg)], capture_output=True)
    m = re.search(r"icount: (\d+)", p.stderr.decode())
    return int(m.group(1)) if m else 0

def geomean(xs):
    xs = [x for x in xs if x > 0]
    if not xs:
        return 0.0
    p = 1.0
    for x in xs:
        p *= x
    return p ** (1.0 / len(xs))

def main():
    args = sys.argv[1:]
    raw = "--raw" in args
    if raw:
        args.remove("--raw")
    passes_mode = "--passes" in args
    if passes_mode:
        args.remove("--passes")

    mods = ["workload"]
    if "--module" in args:
        i = args.index("--module")
        mods = args[i + 1].split(",")
        del args[i:i + 2]
        for m in mods:
            if m not in MODULES:
                print("no such module; known: " + ", ".join(MODULES))
                sys.exit(2)
        first = [c[0] for c in MODULES[mods[0]][1]]
        for m in mods[1:]:
            if [c[0] for c in MODULES[m][1]] != first:
                print("%s and %s export different cases and cannot share a run"
                      % (mods[0], m))
                sys.exit(2)

    def flag(name, default):
        if name in args:
            i = args.index(name)
            v = int(args[i + 1])
            del args[i:i + 2]
            return v
        return default

    reps = flag("--reps", 9)
    rounds = flag("--rounds", 3)

    global CASES
    CASES = MODULES[mods[0]][1]
    if "--cases" in args:
        i = args.index("--cases")
        want = args[i + 1].split(",")
        del args[i:i + 2]
        picked = [c for c in CASES if c[0] in want]
        if not picked:
            print("no such case; known: " + ", ".join(c[0] for c in CASES))
            sys.exit(2)
        CASES = picked

    ref_cc = None
    if "--ab" in args:
        i = args.index("--ab")
        ref_cc = args[i + 1]
        del args[i:i + 2]

    wasm = {m: build_module(m, raw and MODULES[m][0].endswith(".c"))
            for m in mods}
    want_native = MODULES[mods[0]][2] and len(mods) == 1
    if want_native:
        build_native()
    print("input: %s   host: %s   %s"
          % (", ".join("%s (%s)" % (m, "clang -O0" if raw and MODULES[m][0].endswith(".c")
                                    else "clang -O2" if MODULES[m][0].endswith(".c")
                                    else "wat") for m in mods),
             HOST,
             ("time = median over %d launches of min of %d reps" % (rounds, reps))
             if NATIVE_TIMING else "time not measured (emulated host)"))

    if passes_mode:
        levels = [("-O2", "O2")] + [(f"-O2 -fno-{p}", f"no-{p}") for p in PASSES]
    else:
        levels = [("-O0", "O0"), ("-O1", "O1"), ("-O2", "O2")]

    variants = [(f, t if len(mods) == 1 else "%s/%s" % (m, t), None, m)
                for m in mods for f, t in levels]
    if ref_cc:
        variants = [(f, t + "@ref", ref_cc, m) for f, t, _c, m in variants] + \
                   variants

    built = []
    for flags, tag, cc, m in variants:
        binary, prof, size = compile_at(flags, tag, wasm[m], cc)
        if binary:
            built.append((tag, flags, binary, prof, size,
                          emitted_insts(flags, wasm[m], cc)))
    if not passes_mode and NATIVE_TIMING and want_native:
        built.append(("native", "clang -O2", NATIVE, None, 0, 0))

    names = [c[0] for c in CASES]
    ns = {tag: {} for tag, *_ in built}
    out = {tag: {} for tag, *_ in built}

    samples = {tag: {n: [] for n in names} for tag, *_ in built}
    if NATIVE_TIMING:
        t0 = time.time()
        for r in range(rounds):
            for tag, _f, binary, _p, _s, _i in built:
                for fn, (t, o) in bench(binary, CASES, reps).items():
                    if t:
                        samples[tag][fn].append(t)
                    out[tag][fn] = o
            sys.stderr.write("  round %d/%d (%.0fs)\n" % (r + 1, rounds, time.time() - t0))
        for tag, *_ in built:
            for n in names:
                v = sorted(samples[tag][n])
                if v:
                    ns[tag][n] = v[len(v) // 2]

    ic = {tag: {} for tag, *_ in built}
    for tag, _f, _b, prof, _s, _i in built:
        if not prof:
            continue
        base = icount(prof, "startup", 0)
        for fn, arg in CASES:
            ic[tag][fn] = max(icount(prof, fn, arg) - base, 1)

    base = built[0][0]

    top = "O2" if len(mods) == 1 else "%s/O2" % mods[0]
    print("\n%-14s %12s %9s %9s %14s %9s %9s" %
          ("variant", "time(ms)", "vs " + base, "vs native", "dyn insts",
           "vs " + base, "text(B)"))
    print("-" * 82)
    for tag, flags, _b, _p, size, _i in built:
        tot = sum(ns[tag].values()) / 1e6 if ns[tag] else 0
        rel = geomean([ns[base][n] / ns[tag][n] for n in names
                       if ns[tag].get(n) and ns[base].get(n)]) if ns[tag] else 0
        vn = geomean([ns["native"][n] / ns[tag][n] for n in names
                      if ns.get("native", {}).get(n) and ns[tag].get(n)]) \
            if "native" in ns and ns[tag] else 0
        it = sum(ic[tag].values())
        irel = (sum(ic[base].values()) / it) if it else 0
        print("%-14s %12.3f %8.2fx %8.2fx %14s %8s %9d" %
              (tag, tot, rel, vn, it or "-",
               ("%.2fx" % irel) if it else "-", size))

    if NATIVE_TIMING:
        print("\nns per call:")
        print("%-14s" % "variant" + "".join("%12s" % n for n in names))
        for tag, *_ in built:
            print("%-14s" % tag + "".join("%12d" % ns[tag].get(n, 0) for n in names))
        print("%-14s" % "spread" +
              "".join("%11.2fx" % (max(samples[top][n]) / min(samples[top][n]))
                      if samples.get(top, {}).get(n) else "%11s" % "-"
                      for n in names) + "   (slowest launch / fastest)")
        if "native" in ns:
            print("%-14s" % "warmc/native" +
                  "".join("%11.2fx" % (ns[top][n] / ns["native"][n])
                          if ns.get(top, {}).get(n) and ns["native"].get(n) else "%11s" % "-"
                          for n in names))
        for tag, *_ in built:
            ref = tag + "@ref"
            if ref not in ns or not ns[ref]:
                continue
            print("%-14s" % (tag + " vs ref") +
                  "".join("%10.1f%%" % (100.0 * (ns[tag][n] / ns[ref][n] - 1.0))
                          if ns[tag].get(n) and ns[ref].get(n) else "%11s" % "-"
                          for n in names) +
                  "   (negative = faster)")

    print("\ndynamic instructions per call:")
    print("%-14s" % "variant" + "".join("%12s" % n for n in names))
    for tag, *_ in built:
        if ic[tag]:
            print("%-14s" % tag + "".join("%12d" % ic[tag].get(n, 0) for n in names))

    if passes_mode:
        print("\ncost of turning each pass off (higher = the pass helps more):")
        b_ns = ns.get(top, {})
        b_ic = sum(ic[top].values())
        b_sz = built[0][4]
        for tag, flags, _b, _p, size, _i in built[1:]:
            dt = 0.0
            if NATIVE_TIMING and ns[tag] and b_ns:
                dt = 100.0 * (geomean([ns[tag][n] / b_ns[n] for n in names
                                       if ns[tag].get(n) and b_ns.get(n)]) - 1.0)
            di = 100.0 * (sum(ic[tag].values()) - b_ic) / b_ic if b_ic else 0
            print("  %-14s %+7.2f%% time, %+8.2f%% dynamic instructions, %+6.1f%% code size"
                  % (tag, dt, di, 100.0 * (size - b_sz) / b_sz if b_sz else 0))

    ref = out.get(base, {})
    for tag, *_ in built:
        for n in names:
            if out[tag].get(n) and ref.get(n) and out[tag][n] != ref[n]:
                print(f"!! MISMATCH {tag} {n}: {out[tag][n]} != {ref[n]}")

main()
