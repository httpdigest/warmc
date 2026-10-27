"""Time a corpus of kernels: warmc against itself, against wasmtime, against native.

Separate from test/bench.py, which builds one C workload and knows its cases
by name.  This one discovers its kernels, and it takes them in whichever
language the corpus is written in -- so what it measures is the wasm a real
producer emits rather than the wasm clang emits:

  * `test/gobench`, `test/vecbench`   TinyGo, so LLVM's wasm back end.  See
    test/bench/goloops/main.go for why those loop shapes differ from clang's
    and why it matters.
  * `test/asbench`                    AssemblyScript, so binaryen.  A module
    warmc sees *after* another optimiser has had it, with a garbage collector
    and an allocator compiled into it rather than linked beside it.
  * `test/stdgo`                      Go built by Go's own compiler rather than
    TinyGo: the real runtime, with goroutines, a scheduler and a garbage
    collector that actually runs.  Its back end is not LLVM and keeps almost
    everything 64-bit -- 15,457 `i64.extend` in a program whose body is a
    thousand-iteration loop.  The kernels mirror `test/gobench`'s where they
    can, so a row against that one measures the two producers rather than two
    programs.
  * `test/rsbench`                    Rust, so LLVM again but nothing else in
    common with TinyGo: no garbage collector, no scheduler, generics
    monomorphised, and iterator chains inlined into the loop that drives them.
    What dominates a TinyGo module is its runtime; what dominates a Rust one is
    the code the program actually wrote.
  * `test/javabench`                  Java through TeaVM's Wasm GC back end, a
    producer that reads JVM bytecode rather than a source language: objects
    are wasm GC structs, a virtual call goes through a vtable struct of
    funcrefs, and TeaVM's own optimiser has inlined and devirtualised what it
    could before warmc sees any of it.

The name is historical: this drove only test/gobench first, and every note and
memory of the Go performance work names it, so it keeps the name.

Three things this harness does that a `time ./prog` does not:

  * **Subtracts each engine's floor.**  An empty program is timed the same way
    and its time removed, so what is left is the loops rather than process
    start, and warmc's exec is compared against wasmtime's load-and-compile
    rather than pretended to be the same thing.

  * **Interleaves the variants.**  A machine does not hold still for the
    length of a run: thermal state and core placement drift, and measuring A
    fully then B fully attributes that drift to the change.  Every round times
    every variant, and the reported figure is the minimum per variant.

  * **Reports the spread.**  A win inside the run-to-run spread is not a win.
    The `±` column is the gap between the best and second-best round, which is
    the cheapest honest estimate of what this machine's noise floor is.

`--rss` adds a second table: the peak resident set of the same launches, taken
from each child's own rusage rather than from a run of its own.  It subtracts no
floor, and the reason is given at `rss_report` -- a footprint an engine reaches
before the program starts is a footprint the machine has to find.

Usage:  python3 test/gobench.py [--corpus NAME] [--only KERNEL ...] [--rounds N]
                                [--variant 'FLAGS' ...] [--check] [--spills]
                                [--rss] [--record | --compare [--threshold P]]

`--help` prints that list with what each one does, and the corpora on disk.

Every --variant is a warmc flag string, timed alongside the defaults.  With
none given it compares -O2 against wasmtime and native.

`--record` writes this run's ratio against wasmtime, per kernel, into
test/baseline.json; `--compare` runs the same timings and reports every kernel
that has moved more than `--threshold` percent either way, exiting non-zero if
any got worse.  That file is the only place a figure is kept in a form a later
run can check itself against -- everything else this tree records is prose, next
to the decision it justified, which is good for explaining a choice and no use at
all for noticing that something drifted.
"""

import atexit
import base64
import concurrent.futures
import glob
import json
import os
import shlex
import shutil
import re
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.environ.get("BUILD", "build")
WARMC = os.path.join(ROOT, BUILD, "warmc") + (".exe" if os.name == "nt" else "")
CORPUS = "gobench"
WORK = os.path.join(ROOT, BUILD, "gobench")

LANGS = {"main.go": "go", "main.ts": "as", "main.rs": "rs",
         "main.kt": "kt", "main.zig": "zig", "main.c": "c", "main.cpp": "cpp",
         "Main.hs": "hs", "Main.java": "java"}

CORPUS_LANG = {"stdgo": "gostd", "stdgotests": "gostd"}
KDIR = os.path.join(ROOT, "test", "gobench")

ASC_FLAGS = ["-O3", "--exportStart", "_start", "--use", "abort=",
             "--enable", "simd"]

ZIG_FLAGS = ["-target", "wasm32-wasi", "-O", "ReleaseFast",
             "-mcpu=baseline+tail_call+simd128"]

# wasi-sdk's clang, which `make wasi-sdk` fetches.  C++ is compiled with
# exceptions on, which takes the standard (exnref) encoding and the copy of
# libc++ built for it, and C with setjmp/longjmp lowered onto the same
# proposal; a kernel that never throws or jumps pays nothing for either.
# --no-wasm-opt because clang otherwise runs whatever wasm-opt it finds on
# PATH over its output: the corpus would be LLVM's code on one machine and
# binaryen's rewrite of it on the next, and the pass also drops the name
# section, which names a profile's functions after their C ones.
WASI_SDK_BIN = os.path.join(ROOT, "build", "tools", "wasi-sdk", "bin")
C_FLAGS = ["-O3", "--no-wasm-opt", "-mllvm", "-wasm-enable-sjlj", "-mllvm",
           "-wasm-use-legacy-eh=false", "-lsetjmp"]
CXX_FLAGS = ["-O3", "--no-wasm-opt", "-std=c++20", "-fwasm-exceptions",
             "-mllvm", "-wasm-use-legacy-eh=false", "-lunwind"]

# GHC's wasm backend, wherever ghc-wasm-meta's bootstrap.sh put it: on PATH after
# `source ~/.ghc-wasm/env`, or in its default prefix.
GHC_WASM = (shutil.which("wasm32-wasi-ghc") or
            os.path.expanduser("~/.ghc-wasm/wasm32-wasi-ghc/bin/wasm32-wasi-ghc"))
GHC_FLAGS = ["-O2"]

RUSTC_FLAGS = ["-C", "opt-level=3", "--target", "wasm32-wasip1",
               "-C", "panic=abort", "-C", "lto=fat", "-C", "codegen-units=1"]
RUST_TARGET = "wasm32-wasip1"

# kotlinc-wasm compiles to wasm by itself; -Xwasm is deprecated since 2.4.0.
KOTLIN_FLAGS = ["-Xwasm-target=wasm-wasi", "-nowarn"]

def _pinned(name):
    """A version the Makefile pins, so the two cannot drift apart."""
    with open(os.path.join(ROOT, "Makefile")) as f:
        for line in f:
            if line.startswith(name + " "):
                return line.split(":=", 1)[1].strip()
    raise SystemExit("gobench: no %s in the Makefile" % name)

# TeaVM, which `make teavm` fetches and patches (test/teavm/patches): its
# compiler, driven by test/teavm/TeaVMBuild.java, writes a module for a
# JavaScript host, which test/teavm/wasi.py makes a WASI command of.
TEAVM_VERSION = _pinned("TEAVM_VERSION")
TEAVM_DIR = os.path.join(ROOT, "build", "tools", "teavm-" + TEAVM_VERSION)
TEAVM_SRC = os.path.join(ROOT, "test", "teavm")
# Built with TeaVM's --strict, where a null dereference or an index out of
# bounds is Java's exception rather than a trap: what a test that catches one
# needs.  The benchmarks are built as a project builds them by default.
TEAVM_STRICT = {"javatests"}

# TinyGo optimises for size (-opt=z) unless told otherwise; -opt=2 is its
# setting for speed, and the one a benchmark of generated code should start
# from.
FLAGS_FOR = {"go": ["-target=wasip1", "-scheduler=none", "-opt=2"],
             "as": ASC_FLAGS, "rs": RUSTC_FLAGS,
             "gostd": ["GOOS=wasip1", "GOARCH=wasm"],
             "kt": KOTLIN_FLAGS, "zig": ZIG_FLAGS,
             "c": C_FLAGS, "cpp": CXX_FLAGS, "hs": GHC_FLAGS,
             "java": ["teavm-" + TEAVM_VERSION, "FULL"]}

def set_corpus(name):
    """Point the harness at another directory of kernels.

    Eleven of them, and each is a different selection rather than more of the
    same.  `test/gobench` is ordinary Go, chosen to be what programs actually
    look like.  `test/vecbench` is the opposite selection -- loops written to be
    vectorisable -- so a summary over the two mixed together would mean nothing.
    `test/asbench` is a different producer again: AssemblyScript through
    binaryen, whose output has already been optimised once and carries its
    allocator and collector inside the module.  `test/rsbench` is Rust, which
    shares LLVM with TinyGo and nothing else: no collector to step around and no
    scheduler, so a loop arrives as the loop that was written.  `test/stdgo` is
    Go through Go's own compiler, which is not LLVM at all.  `test/ktbench` is
    Kotlin/Wasm, and it is the one producer that does not use linear memory for
    its objects: they are wasm GC structs the engine owns, so what the other
    corpora measure as a load is a `struct.get` here and what they measure as a
    virtual call is a `ref.cast` and a `call_ref`.  `test/zigbench` is Zig,
    LLVM again but built with tail calls and SIMD enabled.  `test/cbench` and
    `test/cppbench` are C and C++ through wasi-sdk's clang, with the C library
    compiled into every module -- malloc, stdio, qsort -- and, for C++, libc++
    built with exceptions on, so a throw is wasm's own `throw` under libc++abi's
    unwinder and C's setjmp/longjmp are lowered onto the same proposal.
    `test/hsbench` is Haskell through GHC's wasm backend, which shares nothing
    with the rest: GHC keeps its heap and its stack in linear memory, every
    function returns the next one to a trampoline in the RTS, and laziness makes
    most of a program closures entered through `call_indirect`.
    `test/javabench` is Java through TeaVM, the second producer of GC structs
    and the one that starts from bytecode: its classes are compiled whole-program,
    with what TeaVM's optimiser could prove about them already folded in, and
    their host calls are defined in the module by test/teavm/wasi.py.

    Separate corpora over one harness rather than one corpus, because what is
    worth averaging is what a corpus was selected for.
    """
    global CORPUS, KDIR, WORK
    CORPUS = name
    KDIR = os.path.join(ROOT, "test", name)
    WORK = os.path.join(ROOT, BUILD, name)

def kernels():
    """The corpus's kernel names and the language they are written in.

    A corpus is one language.  The floor this harness subtracts is an empty
    program built by the same producer, and there is only one of those per run,
    so a mixed corpus is rejected rather than half-measured.
    """
    if not os.path.isdir(KDIR):
        return [], None
    out, langs = [], set()
    forced = CORPUS_LANG.get(CORPUS)
    for n in sorted(os.listdir(KDIR)):
        for entry, lang in sorted(LANGS.items()):
            if os.path.exists(os.path.join(KDIR, n, entry)):
                out.append(n)
                langs.add(forced or lang)
                break
    if len(langs) > 1:
        print("%s: mixed languages in %s (%s); one corpus is one language"
              % (CORPUS, KDIR, ", ".join(sorted(langs))), file=sys.stderr)
        return [], None
    return out, (langs.pop() if langs else None)

EMPTY = {
    "go": ("empty.go", "package main\nfunc main() {}\n"),

    "as": ("empty.ts", "// nothing: the floor\n"),

    "rs": ("empty.rs", "fn main() {}\n"),

    "gostd": ("empty.go", "package main\n\nfunc main() {}\n"),

    "kt": ("empty.kt", "fun main() {}\n"),

    "zig": ("empty.zig", "pub fn main() void {}\n"),

    "c": ("empty.c", "int main(void) { return 0; }\n"),

    "cpp": ("empty.cpp", "int main() { return 0; }\n"),

    "hs": ("empty.hs", "main :: IO ()\nmain = return ()\n"),

    # in a directory of its own, as TeaVMBuild compiles a directory
    "java": ("empty.java/Main.java",
             "public class Main {\n    public static void main(String[] args) {\n    }\n}\n"),
}

TOOLCHAIN = {"go": ("tinygo", ["version"]), "as": ("asc", ["--version"]),
             "rs": ("rustc", ["--version"]), "gostd": ("go", ["version"]),

             "kt": ("kotlinc-wasm", ["-help"]),
             "zig": ("zig", ["version"]),
             "c": (os.path.join(WASI_SDK_BIN, "clang"), ["--version"]),
             "cpp": (os.path.join(WASI_SDK_BIN, "clang++"), ["--version"]),
             "hs": (GHC_WASM, ["--version"]),
             "java": ("javac", ["-version"])}

# What to run when a corpus's producer is missing, for the ones that are not
# simply a package to install.
TOOLCHAIN_HINT = {"c": "make wasi-sdk", "cpp": "make wasi-sdk",
                  "hs": "ghc-wasm-meta's bootstrap.sh, see the README",
                  "java": "a JDK, 21 or later"}

KOTLIN_KLIB = os.path.join(ROOT, "build", "tools",
                           "kotlin-stdlib-wasm-wasi.klib")

def kotlin_klib_current():
    """Whether the stdlib `make kotlin-stdlib` fetched matches the compiler.

    The compiler refuses a stdlib older than itself, and says so only after a
    JVM start, behind its own warnings.  The Makefile stamps the version it
    fetched; a missing stamp for this compiler means a stale or absent klib.
    """
    if not os.path.exists(KOTLIN_KLIB):
        return False
    v = kotlinc_version()
    if v is None:
        return True
    return os.path.exists(os.path.join(os.path.dirname(KOTLIN_KLIB),
                                       ".kotlin-stdlib-" + v))

# The klib step below leaves a packed klib for the module step to include
# only from 2.4.20 on, where that became the default; an older compiler
# reports success and writes nothing there.
KOTLIN_MIN = (2, 4, 20)

def kotlinc_version():
    """The compiler's version as `kotlinc -version` gives it, or None."""
    try:
        r = subprocess.run(["kotlinc", "-version"], capture_output=True, text=True)
    except OSError:
        return None
    m = re.search(r"kotlinc-jvm ([0-9.]+)", r.stdout + r.stderr)
    return m.group(1) if m else None

def javac_version():
    """javac's major version, or None."""
    r = subprocess.run(["javac", "-version"], capture_output=True, text=True)
    m = re.match(r"javac (\d+)", (r.stdout + r.stderr).strip())
    return int(m.group(1)) if m else None

GO_WASI_ENV = {"GOOS": "wasip1", "GOARCH": "wasm"}

def rust_target_installed():
    """Whether rustc can actually build for wasm, not merely whether it exists.

    Asked separately from `have(["rustc"])` because the two fail differently and
    only one of them is the user's to fix by installing a compiler: a rustc with
    no wasm std is the far more likely state of a machine, and it reports itself
    as a wall of E0463 rather than as anything about targets.  `--print
    target-libdir` answers for a target that is not installed as readily as for
    one that is -- it prints where the directory *would* be -- so the answer is
    whether that directory holds an std.
    """
    r = sh(["rustc", "--print", "target-libdir", "--target", RUST_TARGET])
    if r.returncode != 0:
        return False
    d = r.stdout.strip()
    return bool(d) and os.path.isdir(d) and any(
        n.startswith("libstd-") and n.endswith(".rlib") for n in os.listdir(d))

def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)

def have(cmd):
    """Whether an external tool can be run at all.

    Not `sh(cmd).returncode == 0`, which is how this was asked: subprocess
    raises rather than returning a status when the binary is not on PATH, so
    asking that way turned "tinygo is not installed" -- the one case the check
    exists for -- into a traceback.  Both callers claimed to skip and neither
    did.
    """
    try:
        return sh(cmd).returncode == 0
    except OSError:
        return False

def host_flags():
    r = subprocess.run('. test/hostenv.sh; echo "$WARMC_TARGET_FLAGS"',
                       shell=True, capture_output=True, text=True,
                       cwd=ROOT, env=dict(os.environ, BUILD=BUILD),
                       executable="/bin/bash")
    return r.stdout.strip().split()

def staging():
    """This process's directory to build in before moving a module into place:
    one of its own, because two rows may build one corpus at once (gobench and
    gobench-gssa do), and with a shared one the first to finish moved the other's
    module away from under it -- `make check` in a fresh tree failed with "No
    such file or directory: .../.part/mandel.wasm"."""
    d = os.path.join(WORK, ".part", str(os.getpid()))
    if not os.path.isdir(d):
        os.makedirs(d)
        atexit.register(shutil.rmtree, d, True)
    return d

def build_wasm(name, lang):
    """Compile one kernel to wasm.  `empty` is synthesised rather than kept as
    a directory: it exists only to be the floor that is subtracted."""
    os.makedirs(WORK, exist_ok=True)
    wasm = os.path.join(WORK, name + ".wasm")
    entry = "main.go" if lang == "gostd" else \
        [e for e, l in LANGS.items() if l == lang][0]
    env = None
    deps = []
    if lang == "java":
        return build_java([name]).get(name)
    if name == "empty":
        fname, text = EMPTY[lang]
        src = os.path.join(WORK, fname)
        # Written only when it differs.  This file's mtime is what `newest`
        # below is taken from, so rewriting it unconditionally made the stamp
        # cache miss every time and the floor was rebuilt by every corpus on
        # every run, to arrive back at the module already on disk: 3.52s of it
        # for ktbench, whose producer is two JVM starts, and 0.35 / 0.17 / 0.10 /
        # 0.04 for asbench, rsbench, zigbench and stdgo.  It is also what kept
        # this one module being written while other rows read it.
        current = None
        if os.path.exists(src):
            with open(src) as f:
                current = f.read()
        if current != text:
            with open(src, "w") as f:
                f.write(text)
    else:
        src = os.path.join(KDIR, name, entry)
        if lang == "zig":
            deps.append(os.path.join(KDIR, "wasi.zig"))
        if lang == "as":

            deps.append(os.path.join(KDIR, "wasi.ts"))

    newest = max([os.path.getmtime(p) for p in [src] + deps])

    stampf = wasm + ".flags"
    stamp = " ".join(FLAGS_FOR[lang])
    old = None
    if os.path.exists(stampf):
        with open(stampf) as f:
            old = f.read()
    if (os.path.exists(wasm) and os.path.getmtime(wasm) > newest
            and old == stamp):
        return wasm
    # Built aside and moved into place, because the corpora are read while they
    # are written.  run_all.sh starts these rows alongside every consumer of
    # what they produce, so a producer writing straight to its final path lets a
    # reader see a prefix of a module: run_nobar.sh reported
    # `build/asbench/empty.wasm does not compile -- file too short to be a wasm
    # module` on one run in five, and the file was whole by the time anyone
    # looked.  os.replace publishes it in one step instead, so a reader sees the
    # old module or the new one.  build_kotlin already worked this way, which is
    # where the shape comes from.
    #
    # The staging name keeps its .wasm suffix, because tinygo and asc both decide
    # what to emit from the extension, and the directory is hidden so that the
    # suffix costs nothing: the readers that glob `<corpus>/*.wasm` cannot see a
    # subdirectory at all, and run_wat.py, which does walk the whole tree, skips
    # hidden ones.  It would not have failed on a partial module -- an oracle
    # cannot disassemble one either, so it counts as a module with no oracle --
    # but a file nobody has finished writing is not a corpus member.
    part = staging()
    out = os.path.join(part, name + ".wasm")
    if lang == "go":
        cmd = ["tinygo", "build", "-o", out] + FLAGS_FOR["go"] + [src]
    elif lang == "gostd":
        cmd = ["go", "build", "-o", out, src]
        env = dict(os.environ, **GO_WASI_ENV)
    elif lang == "rs":
        cmd = ["rustc", src, "-o", out] + RUSTC_FLAGS
    elif lang == "zig":

        cmd = ["zig", "build-exe"] + ZIG_FLAGS + ["-femit-bin=" + out]
        helper = os.path.join(KDIR, "wasi.zig")
        if name == "empty":
            cmd += ["-Mroot=" + src]
        else:
            cmd += ["--dep", "wasi", "-Mroot=" + src, "-Mwasi=" + helper]
    elif lang in ("c", "cpp"):
        cmd = [TOOLCHAIN[lang][0], src, "-o", out] + FLAGS_FOR[lang]
    elif lang == "hs":
        # GHC writes its .hi and .o files beside the source unless told
        # otherwise, and the source directory is the repository's.
        hwork = os.path.join(part, name + ".hsbuild")
        shutil.rmtree(hwork, ignore_errors=True)
        cmd = [GHC_WASM, src, "-o", out, "-outputdir", hwork] + GHC_FLAGS
    elif lang == "kt":
        return build_kotlin(name, src, wasm, stampf, stamp)
    else:
        cmd = ["asc", src, "-o", out] + ASC_FLAGS
    r = sh(cmd, env=env) if env else sh(cmd)
    if r.returncode != 0:
        print("%s build %s failed:\n%s" % (cmd[0], name, r.stderr[:400]),
              file=sys.stderr)
        return None
    os.replace(out, wasm)
    with open(stampf, "w") as f:
        f.write(stamp)
    return wasm

def build_kotlin(name, src, wasm, stampf, stamp):
    """Sources to klib, klib to module.  Returns the .wasm path, or None.

    Two invocations because K2 will not do both in one, and each is a JVM start,
    so this is the slowest producer in the suite by some way -- which is what the
    mtime cache above is for.

    The compiler names its own output and will not be told otherwise: it writes
    `<name>.wasm` into `-ir-output-dir`, alongside a .mjs loader and, with
    -Xwasm-generate-wat, a .wat.  So it is pointed at a scratch directory per
    kernel and the module is moved to where the harness expects it.
    """
    if not kotlin_klib_current():
        print("ktbench needs the Kotlin/Wasm standard library matching the "
              "compiler: run `make kotlin-stdlib`", file=sys.stderr)
        return None
    work = os.path.join(staging(), name + ".ktbuild")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)
    lib = os.path.join(work, "lib")
    out = os.path.join(work, "out")
    r = sh(["kotlinc-wasm", src] + KOTLIN_FLAGS +
           ["-libraries", KOTLIN_KLIB,
            "-ir-output-dir", lib, "-ir-output-name", name])
    if r.returncode != 0:
        print("kotlinc-wasm klib %s failed:\n%s" % (name, r.stderr[:400]),
              file=sys.stderr)
        return None
    r = sh(["kotlinc-wasm"] + KOTLIN_FLAGS +
           ["-libraries", KOTLIN_KLIB,
            "-Xinclude=" + os.path.join(lib, name + ".klib"),
            "-Xir-produce-js", "-ir-output-dir", out, "-ir-output-name", name])
    if r.returncode != 0:
        print("kotlinc-wasm module %s failed:\n%s" % (name, r.stderr[:400]),
              file=sys.stderr)
        return None
    produced = os.path.join(out, name + ".wasm")
    if not os.path.exists(produced):
        print("kotlinc-wasm %s produced no module" % name, file=sys.stderr)
        return None
    shutil.move(produced, wasm)
    shutil.rmtree(work, ignore_errors=True)
    with open(stampf, "w") as f:
        f.write(stamp)
    return wasm

def write_empty(lang):
    """The empty program's source, written only when it differs: its mtime is
    what the cache below is keyed on (see build_wasm)."""
    fname, text = EMPTY[lang]
    src = os.path.join(WORK, fname)
    os.makedirs(os.path.dirname(src), exist_ok=True)
    current = None
    if os.path.exists(src):
        with open(src) as f:
            current = f.read()
    if current != text:
        with open(src, "w") as f:
            f.write(text)
    return src

def java_dir(name):
    """The directory a Java kernel's sources are in."""
    if name == "empty":
        return os.path.dirname(write_empty("java"))
    return os.path.join(KDIR, name)

TEAVM_PATCHED = os.path.join(TEAVM_DIR, "patched")

def teavm_current():
    """Whether `make teavm` fetched the release the Makefile pins and compiled
    test/teavm/patches into it as they are now."""
    stamp = os.path.join(TEAVM_PATCHED, ".complete")
    patches = os.path.join(TEAVM_SRC, "patches")
    made_from = [patches, os.path.join(TEAVM_SRC, "patch.py")] + [
        os.path.join(patches, f) for f in os.listdir(patches)]
    return (os.path.exists(stamp) and
            os.path.getmtime(stamp) >= max(os.path.getmtime(p) for p in made_from))

def teavm_flags():
    return FLAGS_FOR["java"] + (["strict"] if CORPUS in TEAVM_STRICT else [])

def teavm_newest(name):
    """The newest of what a kernel's module is made from: its sources, the
    driver, TeaVM's patches, the adapter and the math it calls."""
    src = java_dir(name)
    files = [os.path.join(src, f) for f in os.listdir(src)]
    files += [os.path.join(TEAVM_SRC, f) for f in ("TeaVMBuild.java", "FdLibm.java", "wasi.py")]
    files.append(os.path.join(TEAVM_PATCHED, ".complete"))
    return max(os.path.getmtime(p) for p in files)

def teavm_driver():
    """test/teavm/TeaVMBuild.java, compiled against the fetched tooling into
    this corpus's directory: a directory each, since two rows building two
    corpora at once would otherwise compile into one."""
    out = os.path.join(WORK, ".teavm-driver")
    src = os.path.join(TEAVM_SRC, "TeaVMBuild.java")
    cls = os.path.join(out, "warmc", "teavm", "TeaVMBuild.class")
    if os.path.exists(cls) and os.path.getmtime(cls) > os.path.getmtime(src):
        return out
    part = os.path.join(staging(), "driver")
    shutil.rmtree(part, ignore_errors=True)
    r = sh(["javac", "--release", "17", "-nowarn", "-cp",
            os.path.join(TEAVM_DIR, "*"), "-d", part, src])
    if r.returncode != 0:
        print("javac %s failed:\n%s" % (src, r.stderr[:400]), file=sys.stderr)
        return None
    shutil.rmtree(out, ignore_errors=True)
    os.replace(part, out)
    return out

_wasi = None

def teavm_adapter():
    """test/teavm/wasi.py, loaded once for every kernel of the run."""
    global _wasi
    if _wasi is None:
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "teavm_wasi", os.path.join(TEAVM_SRC, "wasi.py"))
        _wasi = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(_wasi)
    return _wasi

def run_teavm(names):
    """TeaVM over the given kernels, one JVM for all of them: each JVM start
    is most of a small program's build.  The directory per kernel it wrote
    into, for those it built."""
    if not teavm_current():
        print("%s needs TeaVM %s, patched as test/teavm/patches are: run `make teavm`"
              % (CORPUS, TEAVM_VERSION),
              file=sys.stderr)
        return {}
    driver = teavm_driver()
    if driver is None:
        return {}
    part = staging()
    outs, jobs = {}, []
    for n in names:
        outs[n] = os.path.join(part, n + ".teavm")
        shutil.rmtree(outs[n], ignore_errors=True)
        jobs.append("%s=%s" % (java_dir(n), outs[n]))
    cmd = ["java", "-cp", os.pathsep.join([TEAVM_PATCHED, os.path.join(TEAVM_DIR, "*"), driver]),
           "warmc.teavm.TeaVMBuild", "--teavm", TEAVM_DIR]
    if CORPUS in TEAVM_STRICT:
        cmd.append("--strict")
    cmd += ["--support", os.path.join(TEAVM_SRC, "FdLibm.java")]
    r = sh(cmd + jobs)
    if r.returncode != 0:
        print(r.stderr[:2000], file=sys.stderr, end="")
    return {n: d for n, d in outs.items() if os.path.exists(os.path.join(d, "main.wasm"))}

def build_java(names):
    """Java kernels to WASI modules: TeaVM for every one that is stale, in one
    JVM, then the adapter.  {name: .wasm path} for those that built."""
    os.makedirs(WORK, exist_ok=True)
    stamp = " ".join(teavm_flags())
    out, stale = {}, []
    for n in names:
        wasm = os.path.join(WORK, n + ".wasm")
        old = None
        if os.path.exists(wasm + ".flags"):
            with open(wasm + ".flags") as f:
                old = f.read()
        if (os.path.exists(wasm) and old == stamp
                and os.path.getmtime(wasm) > teavm_newest(n)):
            out[n] = wasm
        else:
            stale.append(n)
    if not stale:
        return out
    built = run_teavm(stale)
    for n in stale:
        if n not in built:
            print("teavm build %s failed" % n, file=sys.stderr)
            continue
        wasm = os.path.join(WORK, n + ".wasm")
        with open(os.path.join(built[n], "main.wasm"), "rb") as f:
            err = teavm_adapter().write(f.read(), wasm)
        if err:
            print("teavm-wasi %s: %s" % (n, err), file=sys.stderr)
            continue
        with open(wasm + ".flags", "w") as f:
            f.write(stamp)
        shutil.rmtree(built[n], ignore_errors=True)
        out[n] = wasm
    return out

TEMURIN_VERSION = _pinned("TEMURIN_VERSION")
TEMURIN_DIR = os.path.join(ROOT, "build", "tools", "temurin-" + TEMURIN_VERSION)

def temurin_bin(tool):
    """A tool of the Temurin `make temurin` fetched: macOS's JDK is a bundle."""
    for home in (os.path.join(TEMURIN_DIR, "Contents", "Home"), TEMURIN_DIR):
        p = os.path.join(home, "bin", tool + (".exe" if os.name == "nt" else ""))
        if os.path.exists(p):
            return p
    return os.path.join(TEMURIN_DIR, "bin", tool)

def build_java_native(name):
    """A Java kernel's native build: its classes, compiled by Temurin's javac
    at the release the kernels are written to, run by Temurin's JVM, through
    a launcher script as an executable to time.  The empty program goes
    through the same launcher, so the floor takes the JVM's start with the
    script's out of every kernel's time."""
    if not os.path.exists(os.path.join(TEMURIN_DIR, ".complete")):
        print("%s: no Temurin %s for the native column: run `make temurin`"
              % (CORPUS, TEMURIN_VERSION), file=sys.stderr)
        return None
    os.makedirs(WORK, exist_ok=True)
    exe = os.path.join(WORK, name + ".native")
    classes = os.path.join(WORK, name + ".classes")
    src = java_dir(name)
    sources = sorted(os.path.join(src, f) for f in os.listdir(src) if f.endswith(".java"))
    stamp = " ".join(["temurin-" + TEMURIN_VERSION] + NATIVE_FLAGS["java"])
    stampf = exe + ".flags"
    old = open(stampf).read() if os.path.exists(stampf) else None
    if (os.path.exists(exe) and old == stamp and
            os.path.getmtime(exe) > max(os.path.getmtime(p) for p in sources)):
        return exe
    shutil.rmtree(classes, ignore_errors=True)
    r = sh([temurin_bin("javac"), "--release", "21", "-nowarn", "-encoding", "UTF-8",
            "-d", classes] + sources)
    if r.returncode != 0:
        print("javac native build %s failed:\n%s" % (name, r.stderr[:400]), file=sys.stderr)
        return None
    with open(exe, "w") as f:
        f.write("#!/bin/sh\nexec %s\n" % " ".join(
            shlex.quote(a) for a in [temurin_bin("java")] + NATIVE_FLAGS["java"] +
            ["-cp", classes, "Main"]))
    os.chmod(exe, 0o755)
    with open(stampf, "w") as f:
        f.write(stamp)
    return exe

# The same kernels compiled for the host by the language's own native
# compiler, at -O2 or the nearest it has: what the detour through wasm costs
# against the producer's native back end.  Zig has no -O2 (ReleaseFast is its
# optimised build) and Go no levels at all.  AssemblyScript and Kotlin/Wasm
# have no native compiler to compare with.  Java's is the JVM: the same
# classes on Temurin (build_java_native).
NATIVE_FLAGS = {"c": ["-O2"], "cpp": ["-O2", "-std=c++20"],
                "rs": ["-C", "opt-level=2", "-C", "panic=abort", "-C", "lto=fat",
                       "-C", "codegen-units=1"],
                "zig": ["-O", "ReleaseFast", "-lc"],
                "go": ["-opt=2", "-scheduler=none"], "gostd": [],
                # -V0: no RTS timer.  With it, exit joins the ticker thread
                # in its 10 ms sleep, the empty program's floor is mostly that
                # wait, and every kernel, which waits only for the rest of its
                # tick, came out about 10 ms faster than it ran.
                "hs": ["-O2", "-with-rtsopts=-V0"],
                # the JVM as it comes: its JIT, its collector and heap sizing
                "java": []}
NATIVE_TOOL = {"c": "clang", "cpp": "clang++", "rs": "rustc", "zig": "zig",
               "go": "tinygo", "gostd": "go", "hs": "ghc", "java": "java"}
NATIVE_VERSION = {"c": ["clang", "--version"], "cpp": ["clang++", "--version"],
                  "rs": ["rustc", "--version"], "zig": ["zig", "version"],
                  "go": ["tinygo", "version"], "gostd": ["go", "version"],
                  "hs": ["ghc", "--version"], "java": [temurin_bin("java"), "-version"]}

def build_native(name, lang):
    """One kernel built natively, or None when its language has no native
    compiler here or the build fails.

    The kernels are portable as written, with two exceptions the build papers
    over: Zig's print helper writes through WASI's fd_write, so the native
    build links test/zigbench/native.zig in its place, and std.heap's
    wasm_allocator exists only on wasm, so a native kernel gets libc's."""
    if lang == "java":
        return build_java_native(name)
    if lang not in NATIVE_FLAGS or not have(NATIVE_VERSION[lang]):
        return None
    os.makedirs(WORK, exist_ok=True)
    exe = os.path.join(WORK, name + ".native")
    hwork = os.path.join(WORK, name + ".nativebuild")
    if name == "empty":
        src = os.path.join(WORK, EMPTY[lang][0])
        build_wasm(name, lang)          # writes the empty program's source
    else:
        entry = "main.go" if lang == "gostd" else \
            [e for e, l in LANGS.items() if l == lang][0]
        src = os.path.join(KDIR, name, entry)
    deps = [os.path.join(KDIR, "native.zig")] if lang == "zig" else []
    newest = max(os.path.getmtime(p) for p in [src] + deps if os.path.exists(p))
    stampf = exe + ".flags"
    stamp = " ".join(NATIVE_FLAGS[lang])
    old = open(stampf).read() if os.path.exists(stampf) else None
    if os.path.exists(exe) and os.path.getmtime(exe) > newest and old == stamp:
        return exe
    if lang == "zig" and "wasm_allocator" in open(src).read():
        patched = os.path.join(WORK, name + ".native.zig")
        with open(patched, "w") as f:
            f.write(open(src).read().replace("std.heap.wasm_allocator",
                                             "std.heap.c_allocator"))
        src = patched
    env = {k: v for k, v in os.environ.items() if k not in GO_WASI_ENV}
    flags = NATIVE_FLAGS[lang]
    if lang in ("c", "cpp", "rs"):
        cmd = [NATIVE_TOOL[lang], src, "-o", exe] + flags
    elif lang == "zig":
        cmd = ["zig", "build-exe"] + flags + ["-femit-bin=" + exe]
        if name == "empty":
            cmd += ["-Mroot=" + src]
        else:
            cmd += ["--dep", "wasi", "-Mroot=" + src, "-Mwasi=" + deps[0]]
    elif lang == "go":
        cmd = ["tinygo", "build", "-o", exe] + flags + [src]
    elif lang == "gostd":
        cmd = ["go", "build", "-o", exe, src]
    else:
        shutil.rmtree(hwork, ignore_errors=True)
        cmd = ["ghc", src, "-o", exe, "-outputdir", hwork] + flags
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        print("%s native build %s failed:\n%s" % (cmd[0], name, r.stderr[:400]),
              file=sys.stderr)
        return None
    with open(stampf, "w") as f:
        f.write(stamp)
    return exe

def link(wasm, flags, tag, target_flags):
    """Link one kernel.

    The target flags go FIRST and the variant's flags after them, because
    warmc takes the last of a repeated option and a variant that names one the
    host flags also name has to win.  It was the other way round, and
    `--variant '-O2 --rt /somewhere/other.a'` -- which is how a *runtime*
    change is measured against its control in one interleaved run -- silently
    linked both columns against $BUILD/warmrt-macos.a, because
    WARMC_TARGET_FLAGS carries an `--rt` of its own on both hosts.  Two columns
    of the same binary, reported as 1.00x, on the one measurement whose whole
    purpose is to tell two runtimes apart.

    A variant that overrides `--rt` must also say `--wasi=PATH`: bare `--wasi`
    guesses the host archive from `--rt`, and the guess is only right for the
    tree's own build directory.
    """
    exe = os.path.join(WORK, os.path.basename(wasm)[:-5] + "." + tag)
    cmd = [WARMC] + target_flags + flags.split() + ["--wasi"] + ["-o", exe, wasm]
    r = sh(cmd)
    if r.returncode != 0:
        print("link %s %s failed: %s" % (wasm, flags, r.stderr.strip()[:300]),
              file=sys.stderr)
        return None
    return exe

GROUP_BYTES = 24 << 20


def link_all(names, wasm, flags, tag, target_flags):
    """Every kernel in one program, for --check: a program is asked for a
    kernel by --module, and holds each one's code as its own program would.
    A corpus row linked a program per kernel and variant, some 2000 across
    make check, and macOS checks each new program on its first run, about
    150 ms apiece -- most of what those rows cost.  Made every time, at the
    same path under the same name: warmc's WARMC_CACHE hands back a program
    nothing changed for as a hard link to the one checked before.  None
    where one kernel does not compile; link() then says which."""
    exe = os.path.join(WORK, "all." + tag)
    tmp = tempfile.mkdtemp(dir=WORK, prefix=".build.")
    try:
        out = os.path.join(tmp, os.path.basename(exe))
        r = sh([WARMC] + target_flags + flags.split() + ["--wasi", "-o", out] +
               [wasm[n] for n in names])
        if r.returncode != 0:
            return None
        os.replace(out, exe)
        return exe
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def wasmtime_version():
    r = subprocess.run(["wasmtime", "--version"], capture_output=True, text=True)
    return r.stdout.strip()


def reference(w, version):
    """wasmtime's run of the module at w: its exit status, stdout and stderr,
    kept in test/testcache under wasmtime's version and the module's bytes --
    every row that checks a corpus asked wasmtime again, which compiled each
    kernel afresh.  A run that fails is not kept: it is a broken kernel, and
    reported as one each time."""
    import testcache
    k = testcache.key("wasmtime run", version, testcache.File(w))
    v = testcache.get("ref", k)
    if v is not None:
        return v[0], base64.b64decode(v[1]), b""
    r = subprocess.run(["wasmtime", w], capture_output=True)
    if r.returncode == 0 and r.stdout.strip():
        testcache.put("ref", k, [r.returncode, base64.b64encode(r.stdout).decode()])
    return r.returncode, r.stdout, r.stderr


def run_kept(argv):
    """a kernel's run, (rc, stdout), kept under the program's bytes"""
    import testcache
    rc, out, _err = testcache.run(argv[0], argv[1:], lambda: (lambda r: (
        r.returncode, base64.b64encode(r.stdout).decode(), ""))(
            subprocess.run(argv, capture_output=True, env=RUNENV)))
    return rc, base64.b64decode(out)


RUNENV = dict(os.environ, WARMC_RANDOM_SEED="1")

RSS_UNIT = 1 if sys.platform == "darwin" else 1024

def run_once(cmd):
    """One launch: how long it took, and the peak RSS it reached.

    The memory figure comes from the launch that was timed rather than from a
    run of its own: `wait4` hands back the child's rusage at the wait this was
    doing anyway, so the footprint costs nothing to measure and -- more to the
    point -- it is the footprint of the run whose time is being reported.
    """
    t0 = time.perf_counter()
    p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, env=RUNENV)
    _, status, ru = os.wait4(p.pid, 0)
    dt = time.perf_counter() - t0

    p.returncode = os.waitstatus_to_exitcode(status)
    return dt, ru.ru_maxrss * RSS_UNIT

def check(names, wasm, exes, variants):
    """Every kernel's output against wasmtime's, at every variant.

    The programs print a checksum and nothing else, so this is a real
    differential: wasmtime runs the same module warmc compiled from, and a
    disagreement is a warmc bug rather than a timing artefact.  Cheap enough
    to run beside the timings and worth far more than they are.

    The reference is checked for being a reference first.  Agreement is not the
    whole property: a kernel that traps on its first iteration traps under both
    engines, agrees with itself, and reports a time just above the floor -- so
    it would pass this and measure nothing.  Every program here is meant to
    succeed and print, so a reference that did neither is a broken kernel and
    is reported as one rather than being quietly compared.
    """
    bad = 0
    # Compared as bytes: a program may print anything, and output that is not
    # UTF-8 has to be a difference or an agreement, not a decoding error.
    txt = lambda b: b.decode("utf-8", "replace")
    version = wasmtime_version()

    def one(k):
        """the lines k's check prints, and how many failed"""
        rc, out, err = reference(wasm[k], version)
        if rc != 0 or not out.strip():
            return ["FAIL[%s/%s]: reference does not run: rc=%d out=%r err=%r"
                    % (CORPUS, k, rc, txt(out.strip()[:80]), txt(err.strip()[:160]))], 1
        lines = []
        for v in variants:
            e = exes[(k, v)]
            grc, gout = run_kept(e if isinstance(e, list) else [e])
            if gout != out or grc != rc:
                lines.append("FAIL[%s/%s %s]: warmc %r rc=%d, wasmtime %r rc=%d"
                             % (CORPUS, k, v, txt(gout.strip()[:120]),
                                grc, txt(out.strip()[:120]), rc))
        return lines, len(lines)

    # the kernels side by side, a few at a time: make check runs other rows
    with concurrent.futures.ThreadPoolExecutor(4) as ex:
        for lines, n in ex.map(one, names):
            for l in lines:
                print(l)
            bad += n
    if not bad:
        print("%s: %d kernel(s) x %d variant(s) agree with wasmtime"
              % (CORPUS, len(names), len(variants)))
    return bad

def spill_traffic(names, wasm, target_flags):
    """How much of each kernel's emitted code moves values to and from spill
    slots.

    A static proxy, and a coarse one -- it counts instructions rather than
    executions -- but it is the number the register allocator moves, and it is
    stable enough to read across a change.  It exists because the shape it
    measures is invisible in `workload.c`: whole-module frame traffic there is
    0.30% of *executed* instructions, while TinyGo's garbage collector runs at
    thirty-odd percent of the instructions in its hottest functions.  A
    conclusion drawn from one of those does not transfer to the other.
    """
    # A frame reference is spelled differently by each backend.
    frame = "(%rsp)" if any("windows" in f for f in target_flags) else "[sp, #"
    print("%-12s %9s %9s %8s" % ("kernel", "insts", "spill", "ratio"))
    for k in names:
        obj = os.path.join(WORK, k + ".spill.o")
        r = sh([WARMC, "-O2", "-c", "-o", obj] +
               [f for f in target_flags if f.startswith("--target")] + [wasm[k]])
        if r.returncode != 0:
            print("%-12s  (compile failed)" % k)
            continue
        d = sh(["llvm-objdump", "-d", obj]).stdout
        insts = sum(1 for l in d.splitlines() if re.match(r"^\s+[0-9a-f]+:", l))
        spill = sum(1 for l in d.splitlines() if frame in l)
        print("%-12s %9d %9d %7.1f%%"
              % (k, insts, spill, 100.0 * spill / insts if insts else 0.0))
    return 0

BASELINE = os.path.join(ROOT, "test", "baseline.json")

BASE_THRESHOLD = 6.0

def rss_report(names, cols, rss):
    """Peak resident set, per kernel and per engine.

    **No floor is subtracted, and that is the difference between this table and
    the one above it.**  A time is worth having with process start taken out of
    it, because nobody cares how long `exit(0)` takes; a footprint is not, because
    the pages an engine maps before it reaches the program's first instruction
    are pages the machine really has to find.  wasmtime's floor is its own
    answer -- a JIT that has to hold the module, the compiler and the compiled
    code -- and hiding it would be flattering the wrong side.  The `empty` row is
    printed instead, so the floor is visible rather than removed.

    The peak, not the mean, and the maximum across rounds rather than the
    minimum: what a program costs to run is what it needed at its worst moment.
    """
    hdr = "%-8s" % "peak RSS"
    for c in cols:
        hdr += "%14s" % (c if len(c) <= 13 else c[:13])
    if len(cols) > 1:
        hdr += "%10s" % "ratio"
    print(hdr)
    base = cols[0]
    for k in ["empty"] + names:
        row = "%-8s" % k
        for c in cols:
            row += "%12.1f MB" % (rss[c][k] / (1024.0 * 1024.0))
        if len(cols) > 1:
            row += "%9.2fx" % (rss[base][k] / max(rss[cols[-1]][k], 1))
        print(row)

def baseline_load():
    if not os.path.exists(BASELINE):
        return {}
    with open(BASELINE) as f:
        return json.load(f)

def baseline_rows(names, variants, best, second, have_wasmtime, rss=None,
                  differs=()):
    """The figures worth keeping from a timing run.

    The *ratio* to wasmtime is the number recorded and compared, not the times.
    An absolute time belongs to one machine on one afternoon -- a different core,
    a warmer chassis or another laptop moves every row at once -- while the ratio
    divides that out, because both engines ran on the same machine in the same
    interleaved round.  The times are kept beside it anyway: a ratio that holds
    while both halves double says the machine changed, and that is worth being
    able to see rather than having to guess.
    """
    base = variants[0]
    out = {}
    for k in names:
        w = best[base][k] - best[base]["empty"]

        spread = 100.0 * (second[base][k] - best[base][k]) / max(w, 1e-9)
        row = {"warmc": round(w, 5), "noise": round(spread, 2)}
        if have_wasmtime:
            r = best["wasmtime"][k] - best["wasmtime"]["empty"]
            row["wasmtime"] = round(r, 5)
            row["ratio"] = round(w / max(r, 1e-9), 4)

        # A native time under 5 ms is lost in the spread of a process
        # start -- GHC's runtime takes 16 ms to start and varies by 3 -- so
        # it is kept, but no ratio is taken from it.
        if k in best.get("native", {}):
            n = best["native"][k] - best["native"]["empty"]
            row["native"] = round(max(n, 0.0), 5)
            if n >= 0.005:
                row["native_ratio"] = round(w / n, 4)
            if k in differs:
                row["native_differs"] = True
        if rss is not None:
            row["rss_kb"] = int(rss[base][k] / 1024)
            if have_wasmtime:
                row["rss_kb_wasmtime"] = int(rss["wasmtime"][k] / 1024)
            if k in best.get("native", {}):
                row["rss_kb_native"] = int(rss["native"][k] / 1024)
        out[k] = row
    return out

def first_line(cmd):
    """A tool's version line, or None when it will not say."""
    try:
        r = subprocess.run(cmd, capture_output=True, text=True)
    except OSError:
        return None
    out = (r.stdout.strip() or r.stderr.strip()).splitlines()
    return out[0] if out else None

def baseline_reuse(corpus, rows):
    """Each row's wasmtime and native figures as last recorded, and its
    ratios over them: --reuse-baseline timed warmc alone."""
    old = baseline_load().get(corpus, {}).get("kernels", {})
    for k, row in rows.items():
        was = old.get(k)
        if not was:
            continue
        for key in ("wasmtime", "native", "rss_kb_wasmtime", "rss_kb_native",
                    "native_differs"):
            if key in was:
                row[key] = was[key]
        w = row["warmc"]
        if "wasmtime" in was:
            row["ratio"] = round(w / max(was["wasmtime"], 1e-9), 4)
        if "native_ratio" in was:
            row["native_ratio"] = round(w / max(was["native"], 1e-9), 4)
    return rows

def baseline_record(corpus, rows, host, variant, lang, with_native=False,
                    reuse=False):
    """Write this run's figures in as the corpus's baseline.

    The ratios are against one wasmtime and one producer, so both versions go
    in beside them: a baseline that does not say what it measured cannot be
    told stale from regressed once either tool moves.
    """
    data = baseline_load()
    producer = (["kotlinc", "-version"] if lang == "kt"
                else [TOOLCHAIN[lang][0]] + TOOLCHAIN[lang][1])
    was = data.get(corpus, {})
    data[corpus] = {"host": host, "variant": variant, "kernels": rows,
                    "wasmtime": first_line(["wasmtime", "--version"]),
                    "producer": first_line(producer)}
    if lang == "java":
        data[corpus]["producer"] = "TeaVM %s %s, %s" % (
            TEAVM_VERSION, " ".join(teavm_flags()[1:]), first_line(producer))
    if reuse:
        # the figures beside warmc's are the old run's, so are their tools
        for key in ("wasmtime", "native"):
            if key in was:
                data[corpus][key] = was[key]
            else:
                data[corpus].pop(key, None)
    if with_native:
        data[corpus]["native"] = "%s %s" % (first_line(NATIVE_VERSION[lang]),
                                            " ".join(NATIVE_FLAGS[lang]))
        if lang == "java":
            data[corpus]["native"] = "Temurin %s, %s" % (TEMURIN_VERSION,
                                                         first_line(NATIVE_VERSION[lang]))
    with open(BASELINE, "w") as f:
        json.dump(data, f, indent=2, sort_keys=True)
        f.write("\n")
    print("%s: recorded %d kernel(s) into %s"
          % (corpus, len(rows), os.path.relpath(BASELINE, ROOT)))
    return 0

def baseline_record_variant(corpus, names, variants, best, name):
    """--record-variant NAME: a second warmc flag string's figures beside the
    recorded ones, as the recorded ratios times its time over the control's
    in this run -- the two timed interleaved, warmc alone -- so that the
    two columns differ by what the flags change and not by how either run
    was timed.  Each kernel gets ratio_NAME, native_ratio_NAME (where it has
    a native ratio) and over_NAME, the corpus variant_NAME; nothing else in
    the baseline moves."""
    data = baseline_load()
    old = data.get(corpus, {}).get("kernels", {})
    a, b = variants
    n = 0
    for k in names:
        if k not in old:
            continue
        ta = best[a][k] - best[a]["empty"]
        tb = best[b][k] - best[b]["empty"]
        f = tb / max(ta, 1e-9)
        row = old[k]
        row["over_" + name] = round(f, 4)
        for key in ("ratio", "native_ratio"):
            if key in row:
                row["%s_%s" % (key, name)] = round(row[key] * f, 4)
        n += 1
    data[corpus]["variant_" + name] = "%s, over %s" % (b, a)
    with open(BASELINE, "w") as fh:
        json.dump(data, fh, indent=2, sort_keys=True)
        fh.write("\n")
    print("%s: recorded %s for %d kernel(s) into %s"
          % (corpus, name, n, os.path.relpath(BASELINE, ROOT)))
    return 0

def baseline_compare(corpus, rows, host, variant, threshold):
    """This run against the recorded one, kernel by kernel.

    Reports both directions.  An improvement that nobody recorded is how a
    baseline goes stale, and a regression nobody noticed is why the file exists
    at all -- `asbench/memcpy` moved from 1.06x to 1.09x during the work that
    prompted this, and there was no way to tell that from the placement lottery.
    """
    data = baseline_load()
    have = data.get(corpus)
    if not have:
        print("%s: no baseline recorded; run with --record" % corpus,
              file=sys.stderr)
        return 1
    if have.get("host") != host:
        print("%s: baseline is from host %r, this is %r -- ratios are still "
              "comparable but read them with that in mind"
              % (corpus, have.get("host"), host), file=sys.stderr)
    if have.get("variant") != variant:
        print("%s: baseline is for variant %r, this run is %r"
              % (corpus, have.get("variant"), variant), file=sys.stderr)
        return 1
    old = have["kernels"]
    worse, better, missing, added = [], [], [], []
    for k in sorted(rows):
        if k not in old:
            added.append(k)
            continue

        a, b = old[k]["warmc"], rows[k]["warmc"]
        pct = 100.0 * (b - a) / max(a, 1e-9)

        jitter = old[k].get("noise", 0.0) + rows[k].get("noise", 0.0)
        lim = max(threshold, jitter)
        if pct > lim:
            worse.append((k, a, b, pct))
        elif pct < -lim:
            better.append((k, a, b, pct))
    missing = [k for k in sorted(old) if k not in rows]

    fat = []
    for k in sorted(rows):
        if k not in old or "rss_kb" not in old[k] or "rss_kb" not in rows[k]:
            continue
        a, b = old[k]["rss_kb"], rows[k]["rss_kb"]
        if a and 100.0 * (b - a) / a > 20.0:
            fat.append((k, a, b, 100.0 * (b - a) / a))
    for k, a, b, pct in fat:
        print("%-10s %-12s %.1f MB -> %.1f MB  (%+.0f%%) peak RSS"
              % ("FATTER", k, a / 1024.0, b / 1024.0, pct))

    for label, rowset in (("REGRESSED", worse), ("improved", better)):
        for k, a, b, pct in rowset:
            print("%-10s %-12s %.4fs -> %.4fs  (%+.1f%%)  now %s"
                  % (label, k, a, b, pct,
                     ("%.2fx wasmtime" % rows[k]["ratio"]) if "ratio" in rows[k] else "-"))
    if added:
        print("%s: %d kernel(s) not in the baseline: %s"
              % (corpus, len(added), " ".join(added)))
    if missing:
        print("%s: %d baseline kernel(s) absent from this run: %s"
              % (corpus, len(missing), " ".join(missing)))
    if not worse and not better:
        print("%s: %d kernel(s) all within %.0f%% of the baseline"
              % (corpus, len(rows), threshold))
    return 1 if (worse or fat) else 0

def corpora():
    """Every directory under test/ this harness can be pointed at.

    Discovered rather than listed, so a new corpus appears here as soon as it
    has a kernel in it.
    """
    tdir = os.path.join(ROOT, "test")
    out = []
    for n in sorted(os.listdir(tdir)):
        d = os.path.join(tdir, n)
        if not os.path.isdir(d):
            continue
        lang = None
        for k in sorted(os.listdir(d)):
            for entry, l in sorted(LANGS.items()):
                if os.path.exists(os.path.join(d, k, entry)):
                    lang = CORPUS_LANG.get(n) or l
                    break
            if lang:
                break
        if lang:
            out.append((n, lang))
    return out

def usage():
    print(__doc__.split("\n\n")[0].strip())
    print("""
Each engine's floor is subtracted, the variants are interleaved round by round
so drift is not attributed to the change, and the figure per variant is its
minimum.  The head of this file says why each of those matters.

usage: python3 test/gobench.py [options]

  --corpus NAME      which directory of kernels to time (default %s).
  --only KERNEL      time just this kernel, floor still subtracted; repeatable.
  --rounds N         interleaved rounds to time (default 7).  More rounds lowers
                     the noise floor and costs time linearly.
  --variant 'FLAGS'  a warmc flag string to time, alongside wasmtime and native;
                     repeatable, and with none given it times -O2 alone.  Two of
                     them is how a change is measured against its control in one
                     interleaved run, which is the only way to resolve a few
                     percent:
                       --variant -O2 --variant '-O2 -fno-inline'
  --native           also time each kernel built natively, at -O2 or the
                     toolchain's nearest (see NATIVE_FLAGS), interleaved with
                     the rest; with --record its time goes into the baseline
                     too.  A native build that prints something else than
                     warmc's is still timed and is reported: Go's int, Rust's
                     usize and Haskell's Int are 32 bits on wasm32 and 64
                     natively, which changes the sums a kernel prints but not
                     the work it does.
  --check            compile and run every kernel at every variant and check its
                     output against wasmtime's.  Reports nothing about time, so
                     it needs no quiet machine.
  --spills           per kernel, instructions emitted and how many of them touch
                     the stack.  Compiles only; runs nothing.
  --rss              also report each kernel's peak resident set, per engine,
                     measured on the launches that were timed.  No floor is
                     subtracted: the `empty` row is printed instead, because an
                     engine's start-up footprint is real memory.  Implied by
                     --record and --compare.
  --record           write this run's ratio against wasmtime, per kernel, into
                     test/baseline.json.
  --reuse-baseline   time warmc alone and take each kernel's wasmtime and native
                     times from test/baseline.json, with its memory figures and
                     whether native prints differently: neither of those moves
                     when warmc does, and re-running them is most of a record.
                     With --record, the ratios are the new warmc time over the
                     recorded ones.
  --record-variant NAME
                     with two --variants, the control first, and
                     --reuse-baseline: time the two interleaved, warmc alone,
                     and record beside each kernel's figures the second's
                     ratios -- the recorded ones times its time over the
                     control's -- as ratio_NAME and native_ratio_NAME.
  --compare          time as usual and report every kernel whose ratio has moved
                     more than --threshold, exiting non-zero if any got worse.
  --threshold P      the move --compare reports, in percent (default %.0f).
  --help, -h         this text.

corpora:""" % (CORPUS, BASE_THRESHOLD))
    for name, lang in corpora():
        tool = TOOLCHAIN[lang][0]
        n = len(kernels_in(name))
        print("  %-10s %2d kernel(s), %s" % (name, n, tool))

def kernels_in(name):
    """A corpus's kernel names without disturbing the harness's own globals."""
    d = os.path.join(ROOT, "test", name)
    out = []
    for n in sorted(os.listdir(d)):
        for entry in LANGS:
            if os.path.exists(os.path.join(d, n, entry)):
                out.append(n)
                break
    return out

def main():
    args = sys.argv[1:]
    if "--help" in args or "-h" in args:
        usage()
        return 0
    rounds = 7
    spills_only = "--spills" in args
    if spills_only:
        args.remove("--spills")
    check_only = "--check" in args
    if check_only:
        args.remove("--check")
    record = "--record" in args
    if record:
        args.remove("--record")
    compare = "--compare" in args
    if compare:
        args.remove("--compare")

    want_rss = "--rss" in args
    if want_rss:
        args.remove("--rss")
    want_native = "--native" in args
    if want_native:
        args.remove("--native")
    reuse = "--reuse-baseline" in args
    if reuse:
        args.remove("--reuse-baseline")
        want_native = False
    want_rss = want_rss or record or compare
    threshold = BASE_THRESHOLD
    if "--threshold" in args:
        i = args.index("--threshold")
        threshold = float(args[i + 1])
        del args[i:i + 2]
    if "--corpus" in args:
        i = args.index("--corpus")
        set_corpus(args[i + 1])
        del args[i:i + 2]
    if "--rounds" in args:
        i = args.index("--rounds")
        rounds = int(args[i + 1])
        del args[i:i + 2]
    record_variant = None
    if "--record-variant" in args:
        i = args.index("--record-variant")
        record_variant = args[i + 1]
        del args[i:i + 2]

    only = []
    while "--only" in args:
        i = args.index("--only")
        only.append(args[i + 1])
        del args[i:i + 2]
    variants = []
    while "--variant" in args:
        i = args.index("--variant")
        variants.append(args[i + 1])
        del args[i:i + 2]
    if not variants:
        variants = ["-O2"]
    if record_variant and (len(variants) != 2 or not reuse):
        print("--record-variant times two --variants, the control first, with "
              "--reuse-baseline", file=sys.stderr)
        return 1

    if not os.path.exists(WARMC):
        print("%s: %s not built" % (CORPUS, WARMC), file=sys.stderr)
        return 1
    have_wasmtime = have(["wasmtime", "--version"]) and not reuse
    target_flags = host_flags()

    names, lang = kernels()
    if only:
        missing = [k for k in only if k not in names]
        if missing:
            print("%s: no such kernel(s): %s" % (CORPUS, " ".join(missing)),
                  file=sys.stderr)
            return 1
        names = [k for k in names if k in only]
    if not names:
        print("%s: no kernels in %s" % (CORPUS, KDIR), file=sys.stderr)
        return 1

    tool, probe = TOOLCHAIN[lang]
    if not have([tool] + probe):
        hint = TOOLCHAIN_HINT.get(lang)
        print("%s: %s not installed, skipping%s" % (CORPUS, tool,
              " (%s)" % hint if hint else ""), file=sys.stderr)
        return 0
    if lang == "kt":
        v = kotlinc_version()
        if v and tuple(int(x) for x in v.split(".")[:3]) < KOTLIN_MIN:
            print("%s: kotlinc %s predates %s, skipping" % (CORPUS, v,
                  ".".join(map(str, KOTLIN_MIN))), file=sys.stderr)
            return 0
    if lang == "java":
        v = javac_version()
        if v is not None and v < 21:
            print("%s: javac %d predates 21, the release the kernels are "
                  "written to, skipping" % (CORPUS, v), file=sys.stderr)
            return 0
    if lang == "rs" and not rust_target_installed():
        print("%s: rustc has no %s std, skipping "
              "(rustup target add %s)" % (CORPUS, RUST_TARGET, RUST_TARGET),
              file=sys.stderr)
        return 0
    wasm = {}
    if lang == "java":
        build_java(["empty"] + names)    # the stale ones in one JVM
    for k in ["empty"] + names:
        w = build_wasm(k, lang)
        if w is None:
            return 1
        wasm[k] = w

    exes = {}
    tags = {v: "".join(c if c.isalnum() else "_" for c in v) for v in variants}
    # the kernels in groups of at most GROUP_BYTES of wasm: warmc's linker
    # reaches the runtime from no further than a branch does, 128 MB, and
    # stdgotests at -O0 makes twice that
    groups, size = [[]], 0
    for k in names:
        n = os.path.getsize(wasm[k])
        if groups[-1] and size + n > GROUP_BYTES:
            groups.append([])
            size = 0
        groups[-1].append(k)
        size += n
    jobs = [(v, i, g) for v in (variants if check_only else []) for i, g in enumerate(groups)]
    # a few at a time: each holds a group's modules and code
    with concurrent.futures.ThreadPoolExecutor(min(4, max(1, len(jobs)))) as ex:
        alls = list(ex.map(lambda j: link_all(j[2], wasm, j[0], "%s.%d" % (tags[j[0]], j[1]),
                                              target_flags), jobs))
    for (v, _i, g), e in zip(jobs, alls):
        if e is not None:
            for k in g:
                exes[(k, v)] = [e, "--module", k]
    for k in names if check_only else ["empty"] + names:
        for v in variants:
            if (k, v) in exes:
                continue
            tag = "".join(c if c.isalnum() else "_" for c in v)
            e = link(wasm[k], v, tag, target_flags)
            if e is None:
                return 1
            exes[(k, v)] = e

    if spills_only:
        return spill_traffic(names, wasm, target_flags)

    native, differs = {}, set()
    if want_native and not check_only:
        floor = build_native("empty", lang)
        if floor is None:
            print("%s: no native build for %s, so no native column"
                  % (CORPUS, lang), file=sys.stderr)
        else:
            native["empty"] = floor
            for k in names:
                e = build_native(k, lang)
                if e is None:
                    continue
                a = subprocess.run([exes[(k, variants[0])]], capture_output=True,
                                   text=True, env=RUNENV)
                b = subprocess.run([e], capture_output=True, text=True)
                if a.returncode != b.returncode:
                    print("%s: %s native exits %d, warmc %d; left out"
                          % (CORPUS, k, b.returncode, a.returncode),
                          file=sys.stderr)
                    continue
                if a.stdout != b.stdout:
                    differs.add(k)
                native[k] = e
            if differs:
                print("%s: native prints differently (int width, fused "
                      "multiply-add): %s" % (CORPUS, " ".join(sorted(differs))))

    if check_only:
        if not have_wasmtime:
            print("%s: wasmtime not installed, cannot check" % CORPUS,
                  file=sys.stderr)
            return 0
        return 1 if check(names, wasm, exes, variants) else 0

    cols = list(variants) + (["wasmtime"] if have_wasmtime else []) + \
        (["native"] if native else [])
    print("host: %s   rounds: %d   floor subtracted per engine" % (
        subprocess.run(". test/hostenv.sh; echo $WARMC_HOST", shell=True,
                       capture_output=True, text=True, cwd=ROOT,
                       executable="/bin/bash").stdout.strip() or "?", rounds))

    samples = {c: {k: [] for k in ["empty"] + names} for c in cols}
    rss = {c: {k: 0 for k in ["empty"] + names} for c in cols}
    for _ in range(rounds):
        for k in ["empty"] + names:
            for c in cols:
                if c == "native" and k not in native:
                    continue
                cmd = ([exes[(k, c)]] if c in variants
                       else [native[k]] if c == "native"
                       else ["wasmtime", wasm[k]])
                t, m = run_once(cmd)
                samples[c][k].append(t)
                rss[c][k] = max(rss[c][k], m)
    best = {c: {} for c in cols}
    second = {c: {} for c in cols}
    for c in cols:
        for k in ["empty"] + names:
            ts = sorted(samples[c][k])
            if not ts:
                continue
            best[c][k] = ts[0]
            second[c][k] = ts[1] if len(ts) > 1 else ts[0]

    hdr = "%-8s" % "kernel"
    for c in cols:
        hdr += "%14s" % (c if len(c) <= 13 else c[:13])
    hdr += "%10s" % "±noise"
    print(hdr)
    base = cols[0]
    for k in names:
        row = "%-8s" % k
        for c in cols:
            row += ("%13.4fs" % (best[c][k] - best[c]["empty"])
                    if k in best[c] else "%14s" % "-")
        spread = second[base][k] - best[base][k]
        row += "%9.1f%%" % (100.0 * spread / max(best[base][k] - best[base]["empty"], 1e-9))
        print(row)

    if len(cols) > 1:
        print()
        for c in cols[1:]:
            print("%s vs %s:" % (base, c), "  ".join(
                "%s %.2fx" % (k, (best[base][k] - best[base]["empty"]) /
                              max(best[c][k] - best[c]["empty"], 1e-9))
                for k in names if k in best[c]))

    if want_rss:
        print()
        rss_report(names, cols, rss)

    if record_variant:
        return baseline_record_variant(CORPUS, names, variants, best, record_variant)

    if record or compare:
        host = subprocess.run(". test/hostenv.sh; echo $WARMC_HOST", shell=True,
                              capture_output=True, text=True, cwd=ROOT,
                              executable="/bin/bash").stdout.strip() or "?"
        rows = baseline_rows(names, variants, best, second, have_wasmtime, rss,
                             differs)
        if reuse:
            rows = baseline_reuse(CORPUS, rows)
        print()
        if record:
            return baseline_record(CORPUS, rows, host, base, lang,
                                   bool(native), reuse)
        return baseline_compare(CORPUS, rows, host, base, threshold)
    return 0

if __name__ == "__main__":
    sys.exit(main())
