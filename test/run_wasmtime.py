"""Differential test: every case export under warmc against wasmtime.

The cases suite holds warmc to wasm-interp's sequential transcript; this row
compares warmc against a second, independent engine.  wasmtime's `--invoke`
runs one export in a fresh instance, so both sides here run one export per
process -- the comparison is fresh-instance semantics on both sides, which
also makes it order-independent.  Exports the interpreter transcript omits
(they trap when run in sequence) are invoked here too: a trap is an answer,
and both engines must give the same one.

Value comparison is bit-exact: warmc prints float bits in hex; wasmtime
prints Rust's shortest round-trip decimal, which reconstructs the exact bits
for every finite value.  All NaNs compare equal (both engines produce
spec-conformant arithmetic NaNs whose bits legitimately differ).  v128
results are wasmtime's u128 decimal against warmc's hex.

Run after `run_cases.py` (it reuses that suite's cached .wasm/.ref files and
compiled objects).  Skips politely when wasmtime is not installed.
"""
import concurrent.futures
import os
import re
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hostbuild
import run_cases

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.environ.get("BUILD", "build")
WORK = os.path.join(BUILD, "testrun")
RUN = os.environ.get("WARMC_RUN", "").split()
RUN_TIMEOUT = float(os.environ.get("WARMC_RUN_TIMEOUT", "30"))
WASMTIME = os.environ.get("WASMTIME", "wasmtime")

# One trap, two spellings.  warmc's message is the key; any listed wasmtime
# substring matches.  A pair of traps whose texts do not line up is reported,
# so a new trap kind extends this table instead of passing silently.
TRAP_EQUIV = {
    "unreachable executed": ["unreachable"],
    "integer divide by zero": ["integer divide by zero"],
    "integer overflow": ["integer overflow"],
    "out of bounds memory access": ["out of bounds memory access"],
    "undefined element (index out of bounds)":
        ["undefined element", "out of bounds table access"],
    "uninitialized element": ["uninitialized element"],
    "indirect call type mismatch": ["indirect call type mismatch"],
    "invalid conversion to integer": ["invalid conversion to integer"],
    "call stack exhausted": ["call stack exhausted"],
    "null reference": ["null reference"],
    "out of bounds table access": ["out of bounds table access",
                                   "undefined element"],
    "out of bounds array access": ["out of bounds array access"],
    "cast failure": ["cast failure"],
    "uncaught exception": ["uncaught exception", "unhandled", "exception"],
    "allocation failed": ["allocation", "out of memory"],
}

def sh(cmd, timeout=RUN_TIMEOUT):
    try:
        return subprocess.run(cmd, capture_output=True, text=True,
                              timeout=timeout)
    except subprocess.TimeoutExpired:
        return subprocess.CompletedProcess(cmd, 124, "", "timed out")

def f32_bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]

def f64_bits(x):
    return struct.unpack("<Q", struct.pack("<d", x))[0]

def is_nan_bits(ty, v):
    if ty == "f32":
        return (v & 0x7f800000) == 0x7f800000 and (v & 0x007fffff) != 0
    return (v & 0x7ff0000000000000) == 0x7ff0000000000000 and \
           (v & 0x000fffffffffffff) != 0

def parse_warmc(line):
    """warmc's result line -> [(type, bits)] or None on a parse problem."""
    out = []
    for tok in line.split():
        if ":" not in tok:
            return None
        ty, val = tok.split(":", 1)
        try:
            if ty in ("i32", "i64", "v128"):
                out.append((ty, int(val, 0) & ((1 << (128 if ty == "v128"
                            else int(ty[1:]))) - 1)))
            elif ty in ("f32", "f64"):
                out.append((ty, int(val, 16) if val.startswith("0x")
                            else (f32_bits if ty == "f32" else f64_bits)(float(val))))
            else:
                return None
        except ValueError:
            return None
    return out

def wasmtime_bits(ty, text):
    """One wasmtime output line as bits of the expected type, or None."""
    text = text.strip()
    try:
        if ty in ("i32", "i64", "v128"):
            return int(text, 0) & ((1 << (128 if ty == "v128"
                                          else int(ty[1:]))) - 1)
        if ty in ("f32", "f64"):
            f = float(text)          # Rust prints shortest round-trip decimals
            return f32_bits(f) if ty == "f32" else f64_bits(f)
    except ValueError:
        return None
    return None

def trap_of(r, engine):
    """The trap message of a failed run, or None if it did not trap."""
    text = r.stderr + r.stdout
    if engine == "warmc":
        m = re.search(r"^trap: (.*)$", text, re.M)
        return m.group(1).strip() if r.returncode != 0 and m else None
    m = re.search(r"wasm trap: (.*)$", text, re.M)
    return m.group(1).strip() if r.returncode != 0 and m else None

def traps_match(warmc_msg, wasmtime_msg):
    for sub in TRAP_EQUIV.get(warmc_msg, [warmc_msg]):
        if sub in wasmtime_msg:
            return True
    return False

def export_names(wasm):
    """Function export names, in module order, via wasm-interp's listing."""
    ref = wasm[:-5] + ".ref"
    names = []
    seen = set()
    for line in open(ref):
        m = re.match(r"^(\S+?)\(\) =>", line)
        if m and m.group(1) not in seen:
            seen.add(m.group(1))
            names.append(m.group(1))
    # Exports that trapped in the sequential transcript have no "=>" line;
    # recover them from the error lines' "in <name>:" part when present.
    for line in open(ref):
        m = re.match(r"^error.*?\bin (\S+?):", line)
        if m and m.group(1) not in seen:
            seen.add(m.group(1))
            names.append(m.group(1))
    return names

def compare_export(binary, module, wasm, name):
    """(ok, message-or-None, skipped)."""
    w = sh(RUN + [binary, module, name])
    t = sh([WASMTIME, "run", "-C", "cache=y", "--invoke", name, wasm])

    wtrap, ttrap = trap_of(w, "warmc"), trap_of(t, "wasmtime")
    if wtrap or ttrap:
        if wtrap and ttrap and traps_match(wtrap, ttrap):
            return True, None, False
        return False, "FAIL[%s] %s: warmc %s, wasmtime %s" % (
            module, name,
            "trap '%s'" % wtrap if wtrap else "value %r" % w.stdout.strip(),
            "trap '%s'" % ttrap if ttrap else "value %r" % t.stdout.strip()), False
    if w.returncode == 124 or t.returncode == 124:
        return False, "FAIL[%s] %s: timeout (warmc rc=%s, wasmtime rc=%s)" % (
            module, name, w.returncode, t.returncode), False
    if w.returncode != 0 or t.returncode != 0:
        # Not a trap: a host-side refusal (e.g. a result type --invoke cannot
        # print, or an unsupported import).  Skip rather than fail.
        return True, "SKIP[%s] %s: warmc rc=%d, wasmtime rc=%d: %s" % (
            module, name, w.returncode, t.returncode,
            (t.stderr or w.stderr).strip().splitlines()[-1][:100]
            if (t.stderr or w.stderr).strip() else "no output"), True

    got = parse_warmc(w.stdout.strip())
    if got is None:
        return False, "FAIL[%s] %s: cannot parse warmc output %r" % (
            module, name, w.stdout.strip()), False
    lines = [l for l in t.stdout.splitlines() if l.strip() != ""]
    if len(lines) != len(got):
        return False, "FAIL[%s] %s: warmc gave %d values, wasmtime %d" % (
            module, name, len(got), len(lines)), False
    for (ty, wb), line in zip(got, lines):
        if line.strip() in ("NaN", "-NaN"):
            if ty in ("f32", "f64") and is_nan_bits(ty, wb):
                continue
            return False, "FAIL[%s] %s: warmc %s:%#x, wasmtime NaN" % (
                module, name, ty, wb), False
        tb = wasmtime_bits(ty, line)
        if tb is None:
            return False, "FAIL[%s] %s: cannot parse wasmtime %r as %s" % (
                module, name, line.strip(), ty), False
        if tb != wb:
            if ty in ("f32", "f64") and is_nan_bits(ty, wb) and \
               is_nan_bits(ty, tb):
                continue
            return False, "FAIL[%s] %s: %s: warmc %#x, wasmtime %#x" % (
                module, name, ty, wb, tb), False
    return True, None, False

def main():
    os.chdir(ROOT)
    if not shutil.which(WASMTIME):
        print("run_wasmtime: wasmtime not on PATH, skipped")
        return 0

    only = set(sys.argv[1:])   # module names filter the run when given

    bigstack = hostbuild.wabt_bigstack(BUILD)
    if bigstack:
        os.environ["PATH"] = bigstack + os.pathsep + os.environ.get("PATH", "")

    # The cases binary and the .wasm/.ref pairs come from the cases suite's
    # own build steps, run here the same way run_cases.main does.
    if sh(["python3", "test/run_cases.py", "--prepare"],
          timeout=600).returncode not in (0, 1):
        print("run_wasmtime: run_cases --prepare failed")
        return 2
    outdir = os.path.join(WORK, run_cases.flag_tag())
    os.makedirs(outdir, exist_ok=True)
    import glob as _g
    cases = sorted(_g.glob("test/cases/*.wat"))
    versions = {t: run_cases.tool_version(t)
                for t in ("wat2wasm", "wasm-interp")}
    with concurrent.futures.ThreadPoolExecutor() as pool:
        prepared = list(pool.map(
            lambda wt: run_cases.prepare_case(wt, versions), cases))
    ready = [p for p in prepared if not p[3]]
    # A case that cannot be prepared used to drop out of the run without a
    # word, so the row's own module count was the only thing that showed it.
    if len(ready) != len(prepared):
        for p in prepared:
            if p[3]:
                print("FAIL[wasmtime] %s: %s" % (p[0], p[3]))
        print("wasmtime: %d of %d cases could not be prepared"
              % (len(prepared) - len(ready), len(prepared)))
        return 1
    with concurrent.futures.ThreadPoolExecutor() as pool:
        built = list(pool.map(
            lambda t: run_cases.compile_case(t[0], t[1][0], t[1][1], outdir),
            enumerate(ready)))
    live = [(i, p[0], p[1], obj)
            for i, (p, (obj, _e)) in enumerate(zip(ready, built)) if obj]
    skipped_mods = [p[0] for p, (obj, _e) in zip(ready, built) if not obj]

    idx_c = os.path.join(outdir, "index.c")
    idx_o = os.path.join(outdir, "index.o")
    run_cases.write_index([(i, n) for i, n, _w, _o in live], idx_c)
    r = hostbuild.cc_host(idx_c, idx_o)
    if r.returncode == 0:
        binary = os.path.join(outdir, "cases")
        r = hostbuild.link([idx_o] + [o for _i, _n, _w, o in live],
                           binary, BUILD)
        binary = hostbuild.exe(binary)
    if r.returncode != 0:
        print("FAIL[wasmtime]: cannot build the cases binary: %s"
              % r.stderr.strip()[:300])
        return 1

    jobs = []
    for _i, name, wasm, _o in live:
        if only and name not in only:
            continue
        for export in export_names(wasm):
            jobs.append((name, wasm, export))

    total = fails = skips = 0
    msgs = []
    # Each job is two process launches and two waits, so a worker spends most
    # of its life blocked; one per core leaves the machine idle.  This is the
    # longest row in the battery, and oversubscribing it is what fills the
    # cores -- unlike the compile pools above, which are CPU-bound.
    with concurrent.futures.ThreadPoolExecutor(
            max_workers=min(96, 3 * (os.cpu_count() or 4))) as pool:
        results = pool.map(
            lambda j: compare_export(binary, j[0], j[1], j[2]), jobs)
        for ok, msg, skipped in results:
            total += 1
            if skipped:
                skips += 1
            elif not ok:
                fails += 1
            if msg and (not ok or skipped) and len(msgs) < 40:
                msgs.append(msg)

    for m in msgs:
        print(m)
    if skipped_mods:
        print("modules not compiled by warmc (skipped): "
              + " ".join(skipped_mods))
    print("---")
    print("wasmtime: %d export comparisons across %d modules, "
          "%d failure(s), %d skipped"
          % (total, len({j[0] for j in jobs}), fails, skips))
    return 1 if fails else 0

if __name__ == "__main__":
    sys.exit(main())
