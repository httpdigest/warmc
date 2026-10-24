"""Differential testing against wasmtime, on modules wasm-smith writes.

test/fuzz.py writes its own modules, so it finds what its author thought to
generate.  This one takes them from `wasm-tools smith`, which builds a valid
module from random bytes along a decision tree written by people who were not
thinking about warmc: control flow, locals, globals, tables, memories, SIMD
and tail calls in combinations nothing here would have picked.  Every export
is called with edge-value arguments under wasmtime and under warmc at each
level, one fresh process per call on both sides -- an export may change a
global or memory, and wasmtime's `--invoke` starts every call from a new
instance -- and the results are compared value by value.

What the comparison has to know:

  * smith is asked for modules that terminate (`--ensure-termination`, a fuel
    counter in every loop and function), that do not trap where it can avoid
    it (`--disallow-traps`), and whose NaNs are canonical
    (`--canonicalize-nans`), so that a NaN's payload, which the spec leaves
    open, never decides a result.  wasmtime prints a NaN as `NaN` whatever
    its bits, so any NaN matches any NaN.
  * wasmtime prints a result per line, in decimal, and a v128 as one 128-bit
    decimal; warmc's harness prints one line of `type:value` with floats and
    v128 as bits.  Both are brought to (type, bits) before comparing.
  * A trap is compared as a trap: both must trap or neither.  Stack overflow
    is a trap too, and smith's recursion can reach it in one engine's frame
    sizes and not the other's, so a call where only one side runs out of
    stack is reported apart and not counted.
  * smith's constant expressions build arrays of a few gigabytes, which
    wasmtime's GC heap holds and warmc's default 1 GiB reservation does not,
    so warmc's programs run with a 16 GiB reservation (WARMC_GC_HEAP_KB).
    What one engine can allocate and the other cannot is each engine's own
    limit, which the spec leaves to it -- wasmtime refuses an object over
    its largest size or past its GC heap, warmc one past its reservation --
    so a call where only one side runs out (RESOURCE_LIMITS) is not counted
    either.
  * No compile may take more than a second, link included.  One that does is
    timed again, since the other jobs share the machine, and a module whose
    faster compile is still over the limit is reported as bad.

Relaxed SIMD, threads and anything whose result an engine may choose are left
out.  `--profile` widens what smith may use: `gc`, `eh` (exceptions) and
`mem64` add those proposals to the default set.

Usage: smith.py [--seed N] [--count N] [--levels=-O0,-O1,-O2] [--jobs N]
                [--profile default|gc|eh|mem64] [--calls N] [--out DIR]
                [--flags 'warmc flags']

A failure leaves its module and the call that differed under --out, named by
seed, and re-running with that seed reproduces it.
"""
import argparse
import concurrent.futures
import math
import re
import os
import random
import shutil
import struct
import subprocess

# the traps that are an engine running out of what it may allocate
RESOURCE_LIMITS = ("GC heap out of memory", "allocation size too large",
                   "allocation failed")
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.environ.get("BUILD", "build")
WARMC = os.path.join(ROOT, BUILD, "warmc") + (".exe" if os.name == "nt" else "")
os.environ.setdefault("WARMC_GC_HEAP_KB", str(16 << 20))

BASE = ["--ensure-termination", "--disallow-traps", "true",
        "--canonicalize-nans", "true", "--export-everything", "true",
        "--max-imports", "0", "--allow-start-export", "false",
        "--min-funcs", "5", "--simd-enabled", "true",
        "--relaxed-simd-enabled", "false", "--threads-enabled", "false",
        "--multi-value-enabled", "true", "--bulk-memory-enabled", "true",
        "--reference-types-enabled", "true", "--tail-call-enabled", "true",
        "--saturating-float-to-int-enabled", "true",
        "--sign-extension-ops-enabled", "true",
        "--extended-const-enabled", "true",
        "--wide-arithmetic-enabled", "false",
        "--custom-page-sizes-enabled", "false"]
PROFILES = {
    "default": ["--gc-enabled", "false", "--exceptions-enabled", "false",
                "--memory64-enabled", "false"],
    "gc": ["--gc-enabled", "true", "--exceptions-enabled", "false",
           "--memory64-enabled", "false"],
    "eh": ["--gc-enabled", "false", "--exceptions-enabled", "true",
           "--memory64-enabled", "false"],
    "mem64": ["--gc-enabled", "false", "--exceptions-enabled", "false",
              "--memory64-enabled", "true"],
}
WASMTIME_FEATURES = ["-W", "gc=y,exceptions=y,memory64=y,tail-call=y,"
                     "function-references=y"]

TIMEOUT = 20
COMPILE_LIMIT = 1.0
INTS = {
    "i32": [0, 1, -1, 2, 7, 0x7fffffff, -0x80000000, 0x10000, -100],
    "i64": [0, 1, -1, 3, 0x7fffffffffffffff, -0x8000000000000000,
            0x100000000, -12345678901],
}
FLOATS = [0.0, -0.0, 1.0, -1.0, 0.5, -2.25, 1e10, -3.5e-20, 65536.0, 1e300,
          -0.1, 3.4028234663852886e38, 1e-45, 2.0 ** 63, -(2.0 ** 31)]


def sh(cmd, timeout=TIMEOUT, stdin=None):
    try:
        return subprocess.run(cmd, capture_output=True, timeout=timeout,
                              input=stdin)
    except subprocess.TimeoutExpired:
        return None


def timed(cmd, timeout):
    t0 = time.perf_counter()
    r = sh(cmd, timeout=timeout)
    return r, time.perf_counter() - t0


def float_bits(text, ty):
    """wasmtime's printed float as its bits, or 'nan'."""
    t = text.strip()
    if t.lower() in ("nan", "-nan"):
        return "nan"
    if t in ("inf", "+inf"):
        v = math.inf
    elif t == "-inf":
        v = -math.inf
    else:
        v = float(t)
    if ty == "f32":
        return struct.unpack("<I", struct.pack("<f", v))[0]
    return struct.unpack("<Q", struct.pack("<d", v))[0]


def is_nan_bits(bits, ty):
    if ty == "f32":
        return (bits & 0x7f800000) == 0x7f800000 and (bits & 0x7fffff) != 0
    return ((bits & 0x7ff0000000000000) == 0x7ff0000000000000 and
            (bits & 0xfffffffffffff) != 0)


def norm_wasmtime(lines, types):
    if len(lines) != len(types):
        return None
    out = []
    for text, ty in zip(lines, types):
        if ty in ("i32", "i64"):
            mask = (1 << (32 if ty == "i32" else 64)) - 1
            out.append((ty, int(text) & mask))
        elif ty in ("f32", "f64"):
            out.append((ty, float_bits(text, ty)))
        elif ty == "v128":
            out.append((ty, int(text) & ((1 << 128) - 1)))
        else:
            return None
    return out


def norm_warmc(line, types):
    toks = line.split()
    if len(toks) != len(types):
        return None
    out = []
    for tok, ty in zip(toks, types):
        if ":" not in tok:
            return None
        t, v = tok.split(":", 1)
        if t != ty:
            return None
        if ty in ("i32", "i64"):
            mask = (1 << (32 if ty == "i32" else 64)) - 1
            out.append((ty, int(v, 0) & mask))
        else:
            bits = int(v, 16)
            if ty in ("f32", "f64") and is_nan_bits(bits, ty):
                bits = "nan"
            out.append((ty, bits))
    return out


def rename_exports(wasm):
    """smith names exports with random bytes, control characters and all,
    which no argv carries; through the text format and back, every function
    export becomes f<index> and every other x<n>."""
    r = sh(["wasm-tools", "print", wasm])
    if r is None or r.returncode != 0:
        return False
    n = [0]
    def ren(m):
        kind = m.group(2)
        if kind == "func":
            return '(export "f%s" (func %s))' % (m.group(3), m.group(3))
        n[0] += 1
        return '(export "x%d" (%s %s))' % (n[0], kind, m.group(3))
    text = re.sub(r'\(export "((?:[^"\\]|\\.)*)" \((func|table|memory|global|tag) (\d+)\)\)',
                  ren, r.stdout.decode(errors="replace"))
    r = sh(["wasm-tools", "parse", "-o", wasm, "-"], stdin=text.encode())
    return r is not None and r.returncode == 0


def exports_of(wasm):
    """The module's function exports as (name, params, results), from
    `wasm-tools print`, which writes each function's signature inline.  A
    name that cannot go through argv as it is -- empty, starting with `-`
    (warmc's harness separates calls with `--`), or spelled with escapes,
    quotes or spaces -- is left out."""
    r = sh(["wasm-tools", "print", wasm])
    if r is None or r.returncode != 0:
        return []
    sigs, names = {}, []
    for line in r.stdout.decode(errors="replace").splitlines():
        t = line.strip()
        m = re.match(r'^\(func \(;(\d+);\)(.*)$', t)
        if m:
            rest = m.group(2)
            params = re.findall(r'\(param ([^)]*)\)', rest)
            results = re.findall(r'\(result ([^)]*)\)', rest)
            ps = [x for grp in params for x in grp.split() if not x.startswith("$")]
            rs = [x for grp in results for x in grp.split()]
            sigs[int(m.group(1))] = (ps, rs)
            continue
        m = re.match(r'^\(export "([^"]*)" \(func (\d+)\)\)$', t)
        if m:
            names.append((m.group(1), int(m.group(2))))
    out = []
    for name, idx in names:
        if (not name or name.startswith("-") or not name.isprintable() or
                any(c in name for c in ' \\"\'')):
            continue
        if idx in sigs:
            out.append((name,) + sigs[idx])
    return out


def args_for(rng, params):
    """Arguments for one call, as wasmtime reads them and as warmc's harness
    does: wasmtime parses a float from decimal, warmc's harness takes its
    bits.  An f32 is rounded to f32 first, so both sides get the same one."""
    wt, wc = [], []
    for p in params:
        if p in INTS:
            if rng.random() < 0.6:
                v = rng.choice(INTS[p])
            else:
                w = 32 if p == "i32" else 64
                v = rng.randrange(-(1 << (w - 1)), 1 << (w - 1))
            wt.append(str(v))
            wc.append(str(v))
        else:
            v = rng.choice(FLOATS) if rng.random() < 0.7 else rng.uniform(-1e6, 1e6)
            if p == "f32":
                v = struct.unpack("<f", struct.pack("<f", v if abs(v) < 3.4e38 else 3.0e38))[0]
                bits = struct.unpack("<I", struct.pack("<f", v))[0]
            else:
                bits = struct.unpack("<Q", struct.pack("<d", v))[0]
            wt.append(repr(v))
            wc.append("0x%x" % bits)
    return wt, wc


def one_seed(seed, levels, profile, calls, out_dir, flags):
    rng = random.Random(seed)
    work = tempfile.mkdtemp(prefix="smith%d-" % seed)
    try:
        raw = bytes(rng.getrandbits(8) for _ in range(rng.randrange(1000, 16000)))
        wasm = os.path.join(work, "m.wasm")
        r = sh(["wasm-tools", "smith"] + BASE + PROFILES[profile] + ["-o", wasm],
               stdin=raw)
        if r is None or r.returncode != 0:
            return ("skip", seed, "smith failed")
        if not rename_exports(wasm):
            return ("skip", seed, "could not rename the exports")
        cw = os.path.join(work, "m.cwasm")
        r = sh(["wasmtime", "compile"] + WASMTIME_FEATURES + ["-o", cw, wasm])
        if r is None or r.returncode != 0:
            return ("skip", seed, "wasmtime refused")
        exes = {}
        slow = []
        for lv in levels:
            exe = os.path.join(work, "m" + lv)
            cmd = [WARMC, lv] + flags + ["--target=macos", "--rt",
                   os.path.join(ROOT, BUILD, "warmrt-macos.a"), "-o", exe, wasm]
            r, dt = timed(cmd, 120)
            if r is None or r.returncode != 0:
                err = "timeout" if r is None else r.stderr.decode(errors="replace").strip()[:300]
                keep(out_dir, seed, wasm, "compile %s: %s" % (lv, err))
                return ("bad", seed, "warmc %s does not compile it: %s" % (lv, err))
            if dt > COMPILE_LIMIT:
                dt = min(dt, timed(cmd, 120)[1])
                if dt > COMPILE_LIMIT:
                    slow.append("%s compile took %.2fs" % (lv, dt))
            exes[lv] = exe
        funcs = exports_of(wasm)
        ncalls = nbad = 0
        notes = []
        for name, params, results in funcs:
            if any(p not in ("i32", "i64", "f32", "f64") for p in params):
                continue
            if any(t not in ("i32", "i64", "f32", "f64", "v128") for t in results):
                continue
            for _ in range(calls if params else 1):
                args, cargs = args_for(rng, params)
                w = sh(["wasmtime", "run", "--allow-precompiled"] + WASMTIME_FEATURES +
                       ["--invoke", name, cw] + args)
                if w is None:
                    continue
                w_trap = w.returncode != 0
                w_err = w.stderr.decode(errors="replace")
                want = None if w_trap else norm_wasmtime(
                    w.stdout.decode().splitlines(), results)
                if not w_trap and want is None:
                    continue
                for lv in levels:
                    g = sh([exes[lv], name] + cargs)
                    if g is None:
                        nbad += 1
                        notes.append("%s %s(%s): warmc timed out" % (lv, name, " ".join(args)))
                        continue
                    ncalls += 1
                    g_trap = g.returncode != 0
                    g_err = g.stderr.decode(errors="replace")
                    if w_trap or g_trap:
                        if w_trap and g_trap:
                            continue
                        so = ("stack" in w_err.lower() or "stack" in g_err.lower())
                        oom = any(m in w_err + g_err for m in RESOURCE_LIMITS)
                        if so or oom:
                            continue
                        nbad += 1
                        notes.append("%s %s(%s): wasmtime %s, warmc %s" % (
                            lv, name, " ".join(args),
                            "trapped: " + w_err.strip().splitlines()[-1][:120] if w_trap else "returned " + w.stdout.decode().strip().replace("\n", " "),
                            "trapped: " + g_err.strip()[:120] if g_trap else "returned " + g.stdout.decode().strip()))
                        continue
                    got = norm_warmc(g.stdout.decode().strip(), results)
                    if got != want:
                        nbad += 1
                        notes.append("%s %s(%s): wasmtime %s, warmc %s" % (
                            lv, name, " ".join(args),
                            w.stdout.decode().strip().replace("\n", " "),
                            g.stdout.decode().strip()))
        if nbad:
            keep(out_dir, seed, wasm, "\n".join(slow + notes))
            return ("bad", seed, "%d of %d calls differ; first: %s" % (nbad, ncalls, notes[0]))
        if slow:
            keep(out_dir, seed, wasm, "\n".join(slow))
            return ("bad", seed, "; ".join(slow))
        return ("ok", seed, "%d calls" % ncalls)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def keep(out_dir, seed, wasm, note):
    os.makedirs(out_dir, exist_ok=True)
    shutil.copy(wasm, os.path.join(out_dir, "bad-%d.wasm" % seed))
    with open(os.path.join(out_dir, "bad-%d.txt" % seed), "w") as f:
        f.write(note + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--count", type=int, default=100)
    ap.add_argument("--levels", default="-O0,-O1,-O2")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--profile", default="default", choices=sorted(PROFILES))
    ap.add_argument("--calls", type=int, default=3)
    ap.add_argument("--flags", default="")
    ap.add_argument("--out", default=os.path.join(ROOT, BUILD, "smith"))
    a = ap.parse_args()
    for tool in (["wasm-tools", "--version"], ["wasmtime", "--version"]):
        if sh(tool) is None or sh(tool).returncode != 0:
            print("smith: %s not installed, skipping" % tool[0], file=sys.stderr)
            return 0
    levels = a.levels.split(",")
    flags = a.flags.split()
    counts = {"ok": 0, "bad": 0, "skip": 0}
    calls = 0
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        futs = [ex.submit(one_seed, s, levels, a.profile, a.calls, a.out, flags)
                for s in range(a.seed, a.seed + a.count)]
        for fu in concurrent.futures.as_completed(futs):
            kind, seed, msg = fu.result()
            counts[kind] += 1
            if kind == "ok":
                calls += int(msg.split()[0])
            if kind == "bad":
                print("FAIL[smith seed %d]: %s" % (seed, msg), flush=True)
    print("smith (%s): %d modules, %d calls compared, %d bad, %d skipped"
          % (a.profile, counts["ok"] + counts["bad"], calls, counts["bad"],
             counts["skip"]))
    return 1 if counts["bad"] else 0


if __name__ == "__main__":
    sys.exit(main())
