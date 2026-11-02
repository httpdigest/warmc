"""The JIT's embedding examples (example/jit/): a host in each language
that has one -- C, C++, Java, Kotlin, Haskell, Go, Node.js, Bun, Zig and
Rust -- built against libwarmc as its comment says and run on
example/jit/guest.wat, whose output must be example/jit/expected.out: the
guest calling the host's function with a string in its memory, a loop, and
two traps, one of them a fault on the guard pages that the runtime's
signal handler turns into a trap beside the host language's own runtime.

Each runs twice: as it is, and with every call of the runtime from the
module's code through a veneer (WARMC_JIT_VENEERS), as code the JIT places
out of a branch's reach of the runtime makes them -- in a Go or Node
process, whose own runtime has taken the room around the library, every
module's are.  A host whose toolchain is not installed, or too old for its
foreign function interface, is skipped and said so.

    python3 test/run_jit_examples.py [-v] [LANG ...]
"""
import os
import platform
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import hostbuild  # noqa: E402

BUILD = os.path.abspath(os.environ.get("BUILD", os.path.join(ROOT, "build")))
EX = os.path.join(ROOT, "example", "jit")
WORK = os.path.join(BUILD, "jit-examples")
GUEST = os.path.join(EX, "guest.wat")
TIMEOUT = float(os.environ.get("WARMC_RUN_TIMEOUT", "300"))
WINDOWS = os.name == "nt" or platform.system().startswith(("MINGW", "MSYS", "CYGWIN"))


def sh(cmd, cwd=None, env=None, timeout=TIMEOUT):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout,
                          env=dict(os.environ, **(env or {})))


def have(*cmd):
    try:
        return sh(list(cmd), timeout=60).returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


def version(cmd, pattern):
    """The first number group pattern finds in cmd's output, as ints"""
    try:
        r = sh(cmd, timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return None
    m = re.search(pattern, r.stdout + r.stderr)
    return tuple(int(x) for x in m.groups()) if m else None


def library():
    """the library's path, which the hosts that load it by name are given;
    None when make jit has not built it"""
    hostbuild.find_libwarmc(BUILD)
    name = ("libwarmc.dll" if WINDOWS else
            "libwarmc.dylib" if platform.system() == "Darwin" else "libwarmc.so")
    path = os.path.join(BUILD, name)
    return path if os.path.exists(path) else None


def exe(name):
    return os.path.join(WORK, name + (".exe" if WINDOWS else ""))


# Each host: (what it needs, or why it is skipped; how it is built; how it runs)

def c_like(lang):
    src = os.path.join(EX, lang, "host.c" if lang == "c" else "host.cpp")
    cc = "clang" if lang == "c" else "clang++"
    if not shutil.which(cc):
        return "%s not installed" % cc, None, None
    out = exe(lang + "-host")
    std = [] if lang == "c" else ["-std=c++17"]
    link = ([os.path.join(BUILD, "libwarmc.dll.a")] if WINDOWS
            else ["-L" + BUILD, "-lwarmc", "-Wl,-rpath," + BUILD])
    build = [[cc] + std + ["-O2", "-I" + os.path.join(ROOT, "include"), "-o", out, src] + link]
    return None, build, [out, GUEST]


def java():
    v = version(["java", "-version"], r'version "(\d+)')
    if not v or v[0] < 22:
        return "java 22 or later (the foreign function API) not installed", None, None
    return None, [], ["java", "--enable-native-access=ALL-UNNAMED",
                      os.path.join(EX, "java", "Host.java"), library(), GUEST]


def kotlin():
    v = version(["java", "-version"], r'version "(\d+)')
    if not shutil.which("kotlinc") or not v or v[0] < 22:
        return "kotlinc, or java 22 or later, not installed", None, None
    jar = os.path.join(WORK, "kotlin-host.jar")
    return (None, [["kotlinc", os.path.join(EX, "kotlin", "Host.kt"), "-include-runtime", "-d", jar]],
            ["java", "--enable-native-access=ALL-UNNAMED", "-jar", jar, library(), GUEST])


def haskell():
    if not have("ghc", "--version"):
        return "ghc not installed", None, None
    out = exe("haskell-host")
    obj = os.path.join(WORK, "haskell-obj")
    link = (["-l:libwarmc.dll.a"] if WINDOWS else ["-optl-Wl,-rpath," + BUILD])
    return (None, [["ghc", "-O", "-outputdir", obj, "-o", out, os.path.join(EX, "haskell", "Host.hs"),
                    "-L" + BUILD, "-lwarmc"] + link], [out, GUEST])


def go():
    if not have("go", "version"):
        return "go not installed", None, None
    out = exe("go-host")
    # cgo, which go turns off where it finds no cc: clang then
    cc = os.environ.get("CC") or ("cc" if shutil.which("cc") else "clang")
    return None, [["env", "CGO_ENABLED=1", "CC=" + cc, "go", "build", "-o", out, "."]], [out, GUEST]


def node():
    v = version(["node", "--version"], r"v(\d+)\.(\d+)")
    if not v or v < (26, 0) or not have("node", "-e", "require('node:ffi')"):
        return "node 26 or later with node:ffi not installed", None, None
    return None, [], ["node", "--no-warnings", os.path.join(EX, "node", "host.mjs"), library(), GUEST]


def bun():
    if not have("bun", "--version"):
        return "bun not installed", None, None
    return None, [], ["bun", os.path.join(EX, "bun", "host.js"), library(), GUEST]


def zig():
    v = version(["zig", "version"], r"(\d+)\.(\d+)")
    if not v or v < (0, 16):
        return "zig 0.16 or later (build.zig's addTranslateC) not installed", None, None
    prefix = os.path.join(WORK, "zig")
    return (None, [["zig", "build", "-Doptimize=ReleaseSafe", "-p", prefix,
                    "--cache-dir", os.path.join(WORK, "zig-cache")]],
            [os.path.join(prefix, "bin", "host" + (".exe" if WINDOWS else "")), GUEST])


def rust():
    if not have("cargo", "--version") or not have("rustc", "--version"):
        return "cargo not installed", None, None
    target = os.path.join(WORK, "rust-target")
    return (None, [["cargo", "build", "--release", "--quiet", "--target-dir", target]],
            [os.path.join(target, "release", "host" + (".exe" if WINDOWS else "")), GUEST])


HOSTS = {
    "c": (lambda: c_like("c"), None), "cpp": (lambda: c_like("cpp"), None),
    "java": (java, None), "kotlin": (kotlin, None), "haskell": (haskell, None),
    "go": (go, "go"), "node": (node, None), "bun": (bun, None),
    "zig": (zig, "zig"), "rust": (rust, "rust"),
}


def main():
    args = sys.argv[1:]
    verbose = "-v" in args
    langs = [a for a in args if a != "-v"] or list(HOSTS)
    if not library():
        print("jit-examples: libwarmc not built (make jit)")
        return 1
    os.makedirs(WORK, exist_ok=True)
    with open(os.path.join(EX, "expected.out")) as f:
        want = f.read()
    fails, ran, skipped = [], [], []
    for lang in langs:
        if WINDOWS and lang not in ("c", "cpp"):
            # what the others' commands are on Windows is untried: no host
            # here has had those toolchains to try them with
            skipped.append("%s (on Windows only C and C++ are run)" % lang)
            continue
        why, build, run = HOSTS[lang][0]()
        if why:
            skipped.append("%s (%s)" % (lang, why))
            continue
        cwd = os.path.join(EX, HOSTS[lang][1]) if HOSTS[lang][1] else ROOT
        bad = None
        for cmd in build:
            r = sh(cmd, cwd=cwd)
            if r.returncode:
                bad = "does not build: %s" % (r.stdout + r.stderr).strip()[-600:]
                break
        for veneers in ([False, True] if not bad else []):
            r = sh(run, cwd=ROOT, env={"WARMC_JIT_VENEERS": "1"} if veneers else {})
            got = r.stdout.replace("\r\n", "\n")
            if r.returncode or got != want:
                bad = "%sexits %d, prints:\n%s%s" % ("with veneers, " if veneers else "", r.returncode,
                                                     got, r.stderr.strip()[-600:])
                break
        if bad:
            fails.append("FAIL[jit-examples] %s: %s" % (lang, bad))
        else:
            ran.append(lang)
            if verbose:
                print("ok  %s" % lang)
    for f in fails:
        print(f)
    print("jit-examples: %d of %d hosts print what example/jit/expected.out says%s"
          % (len(ran), len(ran) + len(fails),
             "; skipped: " + ", ".join(skipped) if skipped else ""))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
