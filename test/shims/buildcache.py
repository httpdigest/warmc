"""A compiler's build, as the test suites run it, answered from
test/testcache.py where it was asked the same before: tinygo build, go build
and rustc, each with an -o OUT.  The key is what the build is made of: the
tool's version as it reports it (tinygo and rustup's rustc are wrappers, so
their own files say nothing of the compiler behind them), every argument
(OUT by its file name alone), the contents of every file an argument names
and of the other files in a source's directory, and every GO, TINYGO, CARGO
and RUST setting.  What is kept is OUT and what the build said; a build
that failed is not kept.  Any other use of the tool goes to it.  The shims
beside this call it; run_all.sh puts wrappers that run them first on PATH,
each naming its tool in WARMC_SHIM_REAL."""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import testcache  # noqa: E402

VERSION = {"tinygo": ["version"], "go": ["version"], "rustc": ["--version", "--verbose"]}
SOURCES = {"tinygo": ".go", "go": ".go", "rustc": ".rs"}


def real(name):
    """the tool run_all.sh's wrapper names by its absolute path, taken out of
    what the tool is given; else the one PATH has after the shims"""
    p = os.environ.pop("WARMC_SHIM_REAL", None)
    if p:
        return p
    for d in os.environ.get("PATH", "").split(os.pathsep):
        if d and os.path.basename(os.path.abspath(d)) != "shims":
            p = os.path.join(d, name)
            if os.path.isfile(p) and os.access(p, os.X_OK):
                return p
    sys.exit("%s shim: no %s on PATH" % (name, name))


def keepable(name, args):
    if "-o" not in args or args.index("-o") + 1 >= len(args):
        return False
    if name in ("tinygo", "go"):
        return bool(args) and args[0] == "build"
    return any(a.endswith(".rs") for a in args)


def main(name):
    tool = real(name)
    args = sys.argv[1:]
    if testcache.OFF or not keepable(name, args):
        os.execv(tool, [tool] + args)
    out = args[args.index("-o") + 1]
    parts, dirs = [], set()
    for i, a in enumerate(args):
        if i > 0 and args[i - 1] == "-o":
            parts.append("-o " + os.path.basename(a))
            continue
        parts.append(a)
        path = a.split("=", 1)[1] if a.startswith("-") and "=" in a else a
        if os.path.isfile(path):
            parts.append(testcache.File(path))
            if path.endswith(SOURCES[name]):
                dirs.add(os.path.dirname(os.path.abspath(path)))
    for d in sorted(dirs):
        for f in sorted(os.listdir(d)):
            p = os.path.join(d, f)
            if os.path.isfile(p):
                parts += [f, testcache.File(p)]
    ver = subprocess.run([tool] + VERSION[name], capture_output=True, text=True)
    env = sorted((k, v) for k, v in os.environ.items()
                 if k.startswith(("GO", "TINYGO", "CARGO", "RUST")))
    k = testcache.key("build", name, ver.stdout + ver.stderr, parts, env)
    said = testcache.get("build-said", k)
    if said is not None and testcache.get_file("build", k, out):
        sys.stdout.write(said[0])
        sys.stderr.write(said[1])
        sys.exit(0)
    r = subprocess.run([tool] + args, capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    if r.returncode == 0 and os.path.isfile(out):
        testcache.put_file("build", k, out)
        testcache.put("build-said", k, [r.stdout, r.stderr])
    sys.exit(r.returncode)
