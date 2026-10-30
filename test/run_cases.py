"""Differential test: every hand-written case module against wasm-interp.

Compile each case with warmc, run every export, and compare against wabt's
reference interpreter.  Driven by test/run_tests.sh, which sets the host
variables; run directly it does the same thing.

Three things this does that a loop over the cases would not, all of them
because `make check` runs it many times over the same modules, one per
test/cases/*.wat, at a row per flag setting:

  * The interpreter's answers depend on the module alone and not on which
    warmc flags produced the binary, so they are computed once and cached
    against the module, not once per setting.
  * The modules are independent, so they are compiled and compared in
    parallel.  Everything here is a subprocess, so threads are enough.
  * All of them go into *one* executable, which is then asked for a module
    by name.  On macOS the first run of a freshly linked binary costs a fifth
    of a second of signature validation whatever is in it, which with an
    executable per module would be paid per module per setting; it is paid
    once per setting, and each further launch costs milliseconds.
"""
import concurrent.futures
import glob
import hashlib
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare as cmp_mod
import hostbuild

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.environ.get("BUILD", "build")
WORK = os.path.join(BUILD, "testrun")
WABT_FLAGS = os.environ.get("WABT_FLAGS", "").split()
TARGET_FLAGS = os.environ.get("WARMC_TARGET_FLAGS", "").split()
WARMC_FLAGS = os.environ.get("WARMC_FLAGS", "").split()
RUN = os.environ.get("WARMC_RUN", "").split()
TIMEOUT = float(os.environ.get("WARMC_TIMEOUT", "3"))

RUN_TIMEOUT = float(os.environ.get("WARMC_RUN_TIMEOUT", "30"))

# Cases left out by name, CHECK_SKIP_CASES="vector ...": where a runner is
# too slow for one -- GitHub's macOS runner, three cores beside make check's
# Java rows, starves vector.wat's compile, about a second of CPU, past its timeout
# -- it is skipped and said so.  A CHECK_ setting is in a cached row's key
# and reaches no compile.
SKIP = set(os.environ.get("CHECK_SKIP_CASES", "").split())

def sh(cmd, timeout=None):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)

# Harness plumbing: how long to wait, how to run the result, where the SDK
# lives.  None of it reaches the compiler, so none of it belongs in the name
# of the directory the compiler writes to.
TAG_IGNORED_ENV = frozenset((
    "WARMC_FLAGS", "WARMC_TIMEOUT", "WARMC_RUN_TIMEOUT", "WARMC_RUN",
    "WARMC_HOST", "WARMC_TARGET_FLAGS", "WARMC_MACOS_LIBDIR", "WARMC_MACOS_LD",
    "WARMC_CACHE", "WARMC_NO_TESTCACHE", "WARMC_TESTCACHE_MB", "WARMC_TESTCACHE_REFS_ONLY",
))

def flag_tag():
    """A directory name for one configuration, so parallel runs cannot collide.

    The flags alone are not enough to name one.  warmc reads about a hundred
    WARMC_* variables that change what it emits, and two rows can share a flag
    set while differing only in one of them: `cases-O2` and `cases-farbr` both
    run at -O2 and differ only in WARMC_FAR_BRANCH, so both used to land in
    build/testrun/_O2 -- 48 of the 59 objects differ between those two modes,
    and each row linked and checked whatever mixture of them won the race.
    Anything that reaches the compiler is therefore part of the name.
    """
    env = ["%s_%s" % (k, os.environ[k]) for k in sorted(os.environ)
           if k.startswith("WARMC_") and k not in TAG_IGNORED_ENV]
    parts = WARMC_FLAGS + env
    tag = "".join(c if c.isalnum() else "_" for c in "_".join(parts)) or "default"
    # a file name holds 255 bytes: a long configuration, such as one of
    # test/run_combos.py's, is named by its start and a hash of the whole
    if len(tag) > 80:
        tag = tag[:40] + "_" + hashlib.sha1(tag.encode()).hexdigest()[:12]
    return tag

def tool_version(tool):
    r = subprocess.run([tool, "--version"], capture_output=True, text=True)
    return (r.stdout + r.stderr).strip()

def cached(path, key, produce):
    """Run `produce` unless `path` is already what `key` describes.

    The stamp carries the tool's version as well as the input, so upgrading
    wabt is not read through a cache full of the old wabt's answers.
    """
    stamp = path + ".stamp"
    if os.path.exists(path):
        try:
            with open(stamp) as f:
                if f.read() == key:
                    return None
        except OSError:
            pass
    if os.path.exists(stamp):
        os.remove(stamp)
    err = produce()
    if err is not None:
        return err
    with open(stamp, "w") as f:
        f.write(key)
    return None

def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()

def prepare_case(wat, versions):
    """Everything about one case that no warmc flag can change.

    Both artefacts are shared by every flag setting the suite runs, so they
    are built before those fan out -- concurrent runs then only read them.
    Returns (name, wasm, ref, error).
    """
    name = os.path.basename(wat)[:-4]
    wasm = os.path.join(WORK, name + ".wasm")
    ref = os.path.join(WORK, name + ".ref")

    def build_wasm():
        r = sh(["wat2wasm"] + WABT_FLAGS + [wat, "-o", wasm])
        if r.returncode != 0:
            return "FAIL[%s]: wat2wasm: %s" % (name, r.stderr.strip()[:200])
        return None

    err = cached(wasm, sha(wat) + versions["wat2wasm"] + " ".join(WABT_FLAGS),
                 build_wasm)
    if err:
        return name, wasm, ref, err

    def build_ref():
        r = sh(["wasm-interp"] + WABT_FLAGS + ["--run-all-exports", wasm])
        with open(ref, "w") as f:
            f.write(r.stdout + r.stderr)
        return None

    cached(ref, sha(wasm) + versions["wasm-interp"] + " ".join(WABT_FLAGS),
           build_ref)
    return name, wasm, ref, None

def compile_case(idx, name, wasm, outdir):
    """Compile one case to an object, named apart from every other case."""
    obj = os.path.join(outdir, name + ".o")
    target = ["--target=macos"] if hostbuild.DARWIN \
        else ["--target=windows"] if hostbuild.WINDOWS else []
    # -j1, before the flags so that a row can still ask for threads: the
    # cases compile on a worker per CPU, and a compile that also took every
    # core for its own threads, beside the rest of make check starting,
    # could miss its timeout on a module it compiles in milliseconds
    # (test/run_jobs.sh shows the thread count never reaches the output)
    p = subprocess.Popen([os.path.join(BUILD, "warmc"), "-j1"] + WARMC_FLAGS + target +
                         ["--symbol-prefix=c%d" % idx, "-c", "-o", obj, wasm],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        _out, err = p.communicate(timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        where = hostbuild.stack_of(p.pid, os.path.join(outdir, name + ".sample.txt"))
        p.kill()
        p.communicate()
        return None, "FAIL[%s]: warmc timed out after %gs%s" % (name, TIMEOUT, where)
    if p.returncode != 0:
        return None, "FAIL[%s]: warmc: %s" % (name, err.strip()[:200])
    return obj, None

def write_index(entries, path):
    """The table the runtime's harness reads to pick a module by name.

    `entries` is (symbol index, module name) and the symbol index is the one
    the object was *compiled* with, not the position in this list.  They are
    the same number only while every case compiles: renumbering from zero
    here made one case failing to build rename every case after it, so the
    link failed with `undefined _c1_module` and the compiler's own message --
    which names the case and says what was wrong with it -- was never
    reached.  One module's problem must not become everybody's diagnosis.
    """
    out = ['/* Generated by test/run_cases.py -- do not edit. */',
           '#include "warm_rt.h"', ""]
    for i, _n in entries:
        out.append("extern const ModuleDesc c%d_module;" % i)
    out.append("")
    out.append("static const ModuleDesc *const tbl[] = {")
    out += ["  &c%d_module," % i for i, _n in entries]
    out.append("};")
    out.append("static const char *const names[] = {")
    out += ['  "%s",' % n for _i, n in entries]
    out.append("};")
    out.append("")
    out.append("const ModuleDesc *const *warmc_modules = tbl;")
    out.append("const char *const *warmc_module_names = names;")
    out.append("const unsigned warmc_nmodules = %d;" % len(entries))
    out.append("")
    with open(path, "w") as f:
        f.write("\n".join(out))

def main():
    os.chdir(ROOT)
    outdir = os.path.join(WORK, flag_tag())
    os.makedirs(outdir, exist_ok=True)

    bigstack = hostbuild.wabt_bigstack(BUILD)
    if bigstack:
        os.environ["PATH"] = bigstack + os.pathsep + os.environ.get("PATH", "")

    for tool in ("wat2wasm", "wasm-interp"):
        if subprocess.run(["which", tool], capture_output=True).returncode != 0:
            print("run_cases: wabt (wat2wasm, wasm-interp) not found on PATH",
                  file=sys.stderr)
            return 2

    if sh(["python3", "test/gen_cases.py", "test/cases"]).returncode != 0:
        return 1

    versions = {t: tool_version(t) for t in ("wat2wasm", "wasm-interp")}
    cases = sorted(glob.glob("test/cases/*.wat"))
    skipped = [c for c in cases if os.path.splitext(os.path.basename(c))[0] in SKIP]
    cases = [c for c in cases if c not in skipped]
    jobs = int(os.environ.get("CASE_JOBS", 0)) or (os.cpu_count() or 4)

    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        prepared = list(pool.map(lambda w: prepare_case(w, versions), cases))
    msgs = [p[3] for p in prepared if p[3]]
    broken = len(msgs)
    ready = [p for p in prepared if not p[3]]

    if "--prepare" in sys.argv:
        for m in msgs:
            print(m)
        return 1 if msgs else 0

    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        built = list(pool.map(
            lambda t: compile_case(t[0], t[1][0], t[1][1], outdir),
            enumerate(ready)))
    msgs += [err for _obj, err in built if err]
    broken += sum(1 for _obj, err in built if err)

    live = [(i, p[0], p[2], obj)
            for i, (p, (obj, _e)) in enumerate(zip(ready, built)) if obj]

    total = failed = files = 0
    if live:

        idx_c = os.path.join(outdir, "index.c")
        idx_o = os.path.join(outdir, "index.o")
        write_index([(i, n) for i, n, _r, _o in live], idx_c)
        r = hostbuild.cc_host(idx_c, idx_o)
        if r.returncode == 0:
            binary = os.path.join(outdir, "cases")
            r = hostbuild.link([idx_o] + [o for _i, _n, _r, o in live], binary, BUILD)
            binary = hostbuild.exe(binary)
        if r.returncode != 0:

            for m in msgs:
                print(m)
            print("FAIL[cases]: %s" % r.stderr.strip()[:400])
            return 1

        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            results = list(pool.map(
                lambda t: cmp_mod.compare(t[2], binary, t[1], run=RUN,
                                          timeout=RUN_TIMEOUT, prefix=[t[1]]),
                live))

        for (_i, name, _ref, _obj), (n, f, m) in sorted(zip(live, results)):
            total += n
            failed += f
            if n:
                files += 1
            msgs += m

    for m in msgs:
        print(m)
    if skipped:
        print("skipped (CHECK_SKIP_CASES): %s"
              % " ".join(os.path.splitext(os.path.basename(c))[0] for c in skipped))
    print("---")
    print("%d export comparisons across %d modules, %d failure(s)"
          % (total, files, failed + broken))
    return 1 if (failed or broken) else 0

if __name__ == "__main__":
    sys.exit(main())
