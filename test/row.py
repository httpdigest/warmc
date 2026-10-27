"""One row of make check, answered from test/testcache.py where it passed
before with nothing it depends on changed.

What a row's answer depends on: warmc and its sanitiser builds, the
runtimes and test programs make builds, the pinned tools (build/tools: the
small programs by their contents, the installed toolchains by their files'
names and sizes -- each is installed under its version and never changes in
place), every file of the tree outside src/ that git tracks or sees (the
suites, their cases, the corpora's sources, the runtime's sources, the
Makefile), the toolchains on PATH the suites call, and the row's own
command and settings.  run_all.sh works out all but the last once, before
the rows (base), and each row adds its own (run).  A row that fails is not
kept; WARMC_NO_TESTCACHE=1 runs every row.

  python3 test/row.py base
  python3 test/row.py run LABEL CPUFILE -- COMMAND...
"""
import hashlib
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import testcache  # noqa: E402

BUILD = os.environ.get("BUILD", "build")
TOOLS = ["wasmtime", "wasm-interp", "wat2wasm", "wasm2wat", "wasm2c", "clang", "cc", "ld",
         "go", "tinygo", "rustc", "cargo", "zig", "wasm32-wasi-ghc", "ghc", "kotlinc", "java",
         "javac", "node", "podman", "docker", "python3", "perl", "objdump", "llvm-objdump",
         "llvm-mc", "as", "wasm-opt"]


def base():
    h = hashlib.sha256()

    def add(tag, path):
        h.update(("%s %s\0" % (tag, path)).encode())
        if os.path.isfile(path):
            h.update(testcache.file_digest(path).encode())

    os.chdir(ROOT)
    files = subprocess.run(["git", "ls-files", "-co", "--exclude-standard"], capture_output=True,
                           text=True).stdout.split("\n")
    for f in sorted(files):
        if f and not f.startswith("src/") and not f.endswith(".md") and os.path.isfile(f):
            add("tree", f)
    b = os.path.join(ROOT, BUILD)
    for f in sorted(os.listdir(b)):
        p = os.path.join(b, f)
        if os.path.isfile(p) and (f.startswith("warmc") or f.endswith((".a", "_test"))):
            add("build", p)
    tools = os.path.join(b, "tools")
    for dp, dns, fns in os.walk(tools):
        dns.sort()
        for fn in sorted(fns):
            p = os.path.join(dp, fn)
            rel = os.path.relpath(p, tools)
            if rel.startswith("bin" + os.sep):
                add("tool", p)
            else:
                try:
                    h.update(("%s %d\0" % (rel, os.path.getsize(p))).encode())
                except OSError:
                    pass
    for t in TOOLS:
        p = shutil.which(t)
        add("path " + t, os.path.realpath(p) if p else "missing")
    h.update(os.uname().sysname.encode() + os.uname().machine.encode())
    return h.hexdigest()


def run(label, cpufile, argv):
    import resource
    env = sorted((k, v) for k, v in os.environ.items()
                 if k.startswith(("WARMC_", "CHECK_", "GC_")) and k not in testcache.IGNORED_ENV
                 and k != "CHECK_ROW_BASE" and k != "CHECK_TIMING")
    base_key = os.environ.get("CHECK_ROW_BASE", "")
    k = testcache.key("make check row", base_key, label, argv, env) if base_key else None
    v = testcache.get("row", k) if k else None
    if v is not None:
        sys.stdout.write(v + "(this row's answer as when nothing it depends on was as now)\n")
        if cpufile:
            open(cpufile, "w").write("0.0")
        return 0
    p = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = []
    for line in iter(p.stdout.readline, b""):
        sys.stdout.buffer.write(line)
        sys.stdout.flush()
        out.append(line)
    rc = p.wait()
    if cpufile:
        u = resource.getrusage(resource.RUSAGE_CHILDREN)
        open(cpufile, "w").write("%.1f" % (u.ru_utime + u.ru_stime))
    if rc == 0 and k:
        testcache.put("row", k, b"".join(out).decode("utf-8", "replace"))
    return rc


def main(argv):
    if argv[:1] == ["base"]:
        print(base())
        return 0
    if argv[:1] == ["run"] and "--" in argv:
        i = argv.index("--")
        label, cpufile = argv[1], argv[2] if i > 2 else ""
        return run(label, cpufile, argv[i + 1:])
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
