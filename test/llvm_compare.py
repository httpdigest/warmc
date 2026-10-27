"""warmc against LLVM on the same wasm: each kernel compiled by warmc at -O2
and by wasm2c and clang -O2, the two timed interleaved, in cycles -- what
warmc's own code generation leaves behind, apart from whatever the source's
compiler put in the wasm.

  python3 test/llvm_compare.py [-j J] [--rounds N] [--fresh] [--all] [CORPUS[/KERNEL] ...]

The kernels are the corpora's modules as test/gobench.py left them in
build/CORPUS; by default every corpus wasm2c takes, which leaves out
Kotlin's and Java's (garbage collection).  wasm2c is WABT's on the PATH,
with the runtime it installs beside itself; SIMD kernels' C goes through
SIMDe (make simde); test/llvm_shim.c is the WASI host the programs run
under.  Programs are cached under build/llvm by their inputs' contents --
the module, warmc and its runtimes, or wasm2c, clang, the shim and SIMDe --
the least recently used going past LLVM_CACHE_MB (2000).

Timing is test/ab.py's: J kernels at once (6 by default), each its two
programs in turn, the other order every round, each program's figure its
least, a kernel done after 3 rounds once each program's two least agree to
0.5%, otherwise after N (7).  -j 1 times one kernel at a time, for figures
no other kernel's traffic touches.  A kernel's figures are kept in
build/llvm/results.json under its two programs' digests: a change to warmc
times again only the kernels whose code it changed (--fresh times every
one).

Printed: each corpus's geometric mean of warmc's cycles over LLVM's, and
each kernel warmc takes more than 5% longer on -- warmc's own gaps -- or,
with --all, every kernel; and a kernel whose two programs print different
things, which a figure is no comparison for.  macOS only, as test/ab.py.
"""
import concurrent.futures
import hashlib
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import ab
import hostbuild

BUILD = ab.BUILD
WORK = os.path.join(BUILD, "llvm")
CACHE = os.path.join(WORK, "cache")
RESULTS = os.path.join(WORK, "results.json")
CACHE_MB = int(os.environ.get("LLVM_CACHE_MB", "2000"))
CORPORA = "gobench stdgo vecbench asbench rsbench zigbench cbench cppbench hsbench".split()
SHIM = os.path.join(HERE, "llvm_shim.c")
GAP = 1.05


def simde():
    """make simde's directory: the newest version there is"""
    tools = os.path.join(BUILD, "tools")
    ds = sorted(d for d in os.listdir(tools) if d.startswith("simde-") and
                os.path.exists(os.path.join(tools, d, ".fetched"))) if os.path.isdir(tools) else []
    return os.path.join(tools, ds[-1]) if ds else None


def toolchain():
    """wasm2c, its runtime's sources and headers, and the digest of all that
    goes into a program besides the module"""
    w2c = shutil.which("wasm2c")
    if not w2c:
        sys.exit("llvm_compare: no wasm2c on the PATH (WABT: brew install wabt)")
    prefix = os.path.dirname(os.path.dirname(os.path.realpath(w2c)))
    rt = os.path.join(prefix, "share", "wabt", "wasm2c")
    inc = os.path.join(prefix, "include")
    sd = simde()
    if not sd:
        sys.exit("llvm_compare: no SIMDe in %s/tools (make simde)" % BUILD)
    srcs = [os.path.join(rt, f) for f in
            ("wasm-rt-impl.c", "wasm-rt-mem-impl.c", "wasm-rt-exceptions-impl.c")]
    h = hashlib.sha256()
    for cmd in (["wasm2c", "--version"], ["clang", "--version"]):
        h.update(subprocess.run(cmd, capture_output=True, text=True).stdout.encode())
    h.update(sd.encode())
    for p in [SHIM] + srcs:
        with open(p, "rb") as f:
            h.update(f.read())
    return srcs, ["-I", inc, "-I", rt, "-I", sd], h.hexdigest()[:24]


def build_llvm(wasm, srcs, incs, tdig, used):
    """The kernel through wasm2c and clang -O2; (program, error).  A failure
    is kept as one, for the same inputs fail the same way."""
    d = os.path.join(CACHE, ab.digest([wasm]) + tdig)
    used.add(os.path.basename(d))
    exe = os.path.join(d, "k")
    if os.path.exists(exe):
        return exe, None
    if os.path.exists(os.path.join(d, "err")):
        with open(os.path.join(d, "err")) as f:
            return None, f.read()
    tmp = "%s.tmp%d" % (d, os.getpid())
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    r = subprocess.run(["wasm2c", "--module-name=m", "-o", os.path.join(tmp, "m.c"), wasm],
                       capture_output=True, text=True)
    if r.returncode == 0:
        with open(os.path.join(tmp, "m.h")) as f:
            host = "struct w2c_wasi__snapshot__preview1*" in f.read()
        r = subprocess.run(["clang", "-O2", "-w"] + incs + ([] if host else ["-DM_NO_WASI=1"]) +
                           ["-I", tmp, "-o", os.path.join(tmp, "k"), os.path.join(tmp, "m.c"),
                            SHIM] + srcs, capture_output=True, text=True)
        what = "clang"
    else:
        what = "wasm2c"
    err = None
    if r.returncode != 0:
        lines = [l for l in r.stderr.splitlines() if "error" in l] or r.stderr.splitlines()
        first = (lines[0] if lines else "failed").replace(tmp + "/", "").replace(HERE, "test")
        err = "%s: %s" % (what, first.strip()[:160])
    # the C is many times the program and of no use once compiled
    for f in os.listdir(tmp):
        if f != "k":
            os.remove(os.path.join(tmp, f))
    if err:
        with open(os.path.join(tmp, "err"), "w") as f:
            f.write(err)
    try:
        os.rename(tmp, d)
    except OSError:
        shutil.rmtree(tmp, ignore_errors=True)
    return (None, err) if err else (exe, None)


def output(exe):
    r = subprocess.run([exe], capture_output=True, env=ab.ENV, timeout=600)
    return r.returncode, r.stdout


def main():
    args = sys.argv[1:]
    jobs, rounds, fresh, every = 6, 7, False, False
    while args and args[0].startswith("-"):
        o = args.pop(0)
        if o == "-j":
            jobs = int(args.pop(0))
        elif o == "--rounds":
            rounds = int(args.pop(0))
        elif o == "--fresh":
            fresh = True
        elif o == "--all":
            every = True
        else:
            sys.exit(__doc__)
    if not hostbuild.DARWIN:
        sys.exit("llvm_compare: cycles come from macOS's /usr/bin/time -l")
    mods = []
    for t in args or CORPORA:
        if "/" in t:
            mods.append(os.path.join(BUILD, t + ".wasm"))
            continue
        d = os.path.join(BUILD, t)
        if not os.path.isdir(d):
            print("llvm_compare: no %s in %s (test/gobench.py --corpus %s makes it)" % (t, BUILD, t))
            continue
        mods += [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith(".wasm")]
    os.makedirs(CACHE, exist_ok=True)
    srcs, incs, tdig = toolchain()
    flags, wdig = ab.compiler(BUILD)
    ab.ENV.update(os.environ)
    used = set()
    warmc = os.path.join(BUILD, "warmc")
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 8) as ex:
        wms = list(ex.map(lambda w: ab.build(warmc, flags, wdig, w, used, CACHE), mods))
        lls = list(ex.map(lambda w: build_llvm(w, srcs, incs, tdig, used), mods))
    try:
        with open(RESULTS) as f:
            known = json.load(f)
    except (OSError, ValueError):
        known = {}

    keys = [ab.digest([wm]) + ab.digest([ll]) if wm and ll else str(i)
            for i, (wm, (ll, _)) in enumerate(zip(wms, lls))]

    def one(i):
        wm, (ll, err) = wms[i], lls[i]
        if not wm or not ll:
            return err or "warmc: does not compile"
        key = keys[i]
        if not fresh and key in known:
            return key, known[key], True
        same = output(wm) == output(ll)
        mw, ml, k = ab.time_pair(wm, ll, rounds, False)
        return key, [mw, ml, k, same], False

    # a module two corpora share is timed once
    first = {}
    for i, k in enumerate(keys):
        first.setdefault(k, i)
    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        done = dict(zip(first.values(), ex.map(one, first.values())))
    res = [done[first[k]] for k in keys]
    by, reused, shown = {}, 0, []
    for w, r in zip(mods, res):
        name = os.path.basename(os.path.dirname(w)) + "/" + os.path.basename(w)[:-5]
        if isinstance(r, str):
            print("%-28s %s" % (name, r))
            continue
        key, (mw, ml, k, same), old = r
        reused += old
        known.pop(key, None)
        known[key] = [mw, ml, k, same]
        q = mw / ml
        by.setdefault(name.split("/")[0], []).append(q)
        if every or q > GAP or not same:
            shown.append((q, name, mw, ml, same))
    for q, name, mw, ml, same in sorted(shown, reverse=True):
        print("%-28s warmc %9.1fM  LLVM %9.1fM  %.3f%s"
              % (name, mw / 1e6, ml / 1e6, q, "" if same else "  (prints differently)"))
    for c, qs in by.items():
        print("%-10s geomean %.3f over %d, %d more than %d%% slower"
              % (c, ab.geomean(qs), len(qs), sum(q > GAP for q in qs), round((GAP - 1) * 100)))
    allq = [q for qs in by.values() for q in qs]
    if allq:
        print("all        geomean %.3f over %d, %d more than %d%% slower, %d timed before"
              % (ab.geomean(allq), len(allq), sum(q > GAP for q in allq),
                 round((GAP - 1) * 100), reused))
    known = dict(list(known.items())[-ab.KEEP:])
    with open(RESULTS + ".tmp", "w") as f:
        json.dump(known, f)
    os.replace(RESULTS + ".tmp", RESULTS)
    ab.trim(CACHE, used, CACHE_MB)


if __name__ == "__main__":
    main()
