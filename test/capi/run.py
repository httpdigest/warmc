"""The C API's tests: libwarmc as an embedder of wasm.h sees it.

  * The WebAssembly C API's own examples (WebAssembly/wasm-c-api, pinned by
    commit, every file checked against test/fetch.sha256): each built
    unchanged against libwarmc, run, and its output compared with
    test/capi/expected/NAME.out.  threads.c's threads print in no fixed
    order, so its lines are compared sorted, and the pointers hostref.c
    prints as each C library spells them.  reflect.wat writes `anyfunc`,
    the name funcref had before the reference types proposal, which warmc's
    text front end, as the spec's grammar, does not read: it is assembled
    with funcref in its place.
  * The wasm.h of the same commit, which include/wasm.h must be byte for
    byte: the header libwarmc implements is that release of the standard's.
  * test/capi/api.c, the library's own tests of what the examples do not
    reach.

Run by `make check` (test/run_all.sh) and on its own:
    python3 test/capi/run.py [-v]
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "test"))
import hostbuild  # noqa: E402
# absolute: the examples run in their own directory, and find the library
# by the rpath it names
BUILD = os.path.abspath(os.environ.get("BUILD", os.path.join(ROOT, "build")))
WARMC = hostbuild.exe(os.path.join(BUILD, "warmc"))
COMMIT = "9d6b93764ac96cdd9db51081c363e09d2d488b4d"
BASE = "https://raw.githubusercontent.com/WebAssembly/wasm-c-api/%s/" % COMMIT
EXAMPLES = ["callback", "finalize", "global", "hello", "hostref", "memory", "multi",
            "reflect", "serialize", "start", "table", "threads", "trap"]
UNORDERED = {"threads"}
WORK = os.path.join(BUILD, "capi")
TIMEOUT = float(os.environ.get("WARMC_RUN_TIMEOUT", "120"))

# hostref.c prints references with %p, which each C library spells its own
# way: a null is 0x0 on macOS, (nil) with glibc and 0000000000000000 on
# Windows.  Compared as the value, as macOS spells it.
PTR = re.compile(r"^(> > )(\(nil\)|0x[0-9a-fA-F]+|[0-9A-Fa-f]{16})$", re.M)

def canon_ptrs(text):
    def one(m):
        v = 0 if m.group(2) == "(nil)" else int(m.group(2), 16)
        return m.group(1) + "0x%x" % v
    return PTR.sub(one, text)

def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)

def fetch(rel, out):
    if os.path.exists(out):
        return None
    r = sh([os.path.join(ROOT, "test", "fetch.sh"), BASE + rel, out])
    return None if r.returncode == 0 else (r.stderr.strip() or "fetch failed")

def build_c(src, exe):
    """A program against the library in BUILD, found there when it runs."""
    rpath = [] if hostbuild.WINDOWS else ["-Wl,-rpath," + BUILD]
    return sh(["clang", "-O1", "-g", "-Wall", "-I" + os.path.join(ROOT, "include"),
               "-o", exe, src, "-L" + BUILD, "-lwarmc", "-lpthread"] + rpath)

def main():
    verbose = "-v" in sys.argv
    hostbuild.find_libwarmc(BUILD)
    up = os.path.join(WORK, "upstream-" + COMMIT[:12])
    os.makedirs(up, exist_ok=True)
    fails = []
    ran = 0

    hdr = os.path.join(up, "wasm.h")
    err = fetch("include/wasm.h", hdr)
    if err:
        fails.append("wasm.h: " + err)
    else:
        with open(hdr, "rb") as a, open(os.path.join(ROOT, "include", "wasm.h"), "rb") as b:
            if a.read() != b.read():
                fails.append("include/wasm.h is not wasm-c-api %s's wasm.h" % COMMIT[:12])
        ran += 1

    for name in EXAMPLES:
        c, wat = os.path.join(up, name + ".c"), os.path.join(up, name + ".wat")
        err = fetch("example/%s.c" % name, c) or fetch("example/%s.wat" % name, wat)
        if err:
            fails.append("%s: %s" % (name, err))
            continue
        src = wat
        if name == "reflect":
            with open(wat) as f:
                text = f.read().replace("anyfunc", "funcref")
            src = os.path.join(up, name + ".funcref.wat")
            with open(src, "w") as f:
                f.write(text)
        wasm = os.path.join(up, name + ".wasm")
        r = sh([WARMC, "--emit-wasm", "-o", wasm, src])
        if r.returncode:
            fails.append("%s: warmc --emit-wasm: %s" % (name, r.stderr.strip()[:200]))
            continue
        exe = hostbuild.exe(os.path.join(up, name))
        r = build_c(c, exe)
        if r.returncode:
            fails.append("%s: cannot build: %s" % (name, r.stderr.strip()[:300]))
            continue
        try:
            r = subprocess.run([exe], cwd=up, capture_output=True, text=True, timeout=TIMEOUT)
        except subprocess.TimeoutExpired:
            fails.append("%s: timed out after %gs" % (name, TIMEOUT))
            continue
        ran += 1
        got = canon_ptrs(r.stdout)
        with open(os.path.join(ROOT, "test", "capi", "expected", name + ".out")) as f:
            want = f.read()
        if name in UNORDERED:
            got = "".join(sorted(got.splitlines(True)))
        if r.returncode or got != want:
            fails.append("%s: exit %d, output %s" % (name, r.returncode,
                         "as expected" if got == want else "differs:\n" + got[-600:]))
        elif verbose:
            print("ok  %s" % name)

    exe = hostbuild.exe(os.path.join(WORK, "api"))
    r = build_c(os.path.join(ROOT, "test", "capi", "api.c"), exe)
    if r.returncode:
        fails.append("api.c: cannot build: %s" % r.stderr.strip()[:300])
    else:
        try:
            r = subprocess.run([exe], cwd=WORK, capture_output=True, text=True, timeout=TIMEOUT)
            ran += 1
            if r.returncode:
                fails.append("api.c: exit %d:\n%s" % (r.returncode, (r.stdout + r.stderr)[-1500:]))
            elif verbose:
                print(r.stdout.strip())
        except subprocess.TimeoutExpired:
            fails.append("api.c: timed out after %gs" % TIMEOUT)

    for f in fails:
        print("FAIL " + f)
    print("capi: %d of %d passed (%d upstream examples, wasm.h, api.c)"
          % (ran - len([f for f in fails if "exit" in f or "differs" in f or "is not" in f]),
             len(EXAMPLES) + 2, len(EXAMPLES)))
    return 1 if fails else 0

if __name__ == "__main__":
    sys.exit(main())
