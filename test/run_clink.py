"""Component linking: the graph warmc resolves, and the program it becomes.

Everything else about components here compares *structure* -- what warmc
decoded against what wasm-tools decoded, or against a golden dump.  Linking is
the first part where there is an answer to be wrong about, so this suite has an
instrument the others could not have:

1. **A second engine runs the same component.**  `wasmtime run --invoke` walks
   the graph its own way, at its own start-up, in a process that shares nothing
   with ours -- and has to arrive at the same number.  warmc resolves the whole
   graph at compile time and emits a table; wasmtime resolves it at load time
   from the bytes.  Two implementations of one specification with no shared
   code, which is the only kind of agreement worth much.

2. **A fixture built to be got wrong.**  `linkable.wat` is arranged so that
   each way of resolving the graph badly produces a *different* wrong number:
   binding by module name rather than by instantiation argument reads one cell
   twice (13013 rather than 10003); sharing one instance between two
   instantiations of a nested component collapses two answers into one;
   carrying an inline instance's renaming into the program fails to find the
   export at all.  A test that only checked "it ran" would pass through all
   three.

3. **Golden plans.**  `--dump-link` per component, so a change in *what* was
   resolved is a diff rather than a number that still comes out right by
   accident -- and so the components that cannot yet be compiled, which are
   half of them, are covered by something.  The plan now includes the adapter
   modules warmc synthesises, which is where an adapter bound to the wrong
   memory shows up as a diff rather than as a number.

4. **Two fixtures that go through an adapter.**  `composed.wat` crosses a
   string on the way back and `fused.wat` crosses one on the way out, lifts a
   `bool` the callee gets wrong on purpose, observes post-return inside a
   single call, and spills seventeen parameters through memory.  Both are
   arranged so the answer depends on the bytes that crossed: a length is the
   argument again, so `len(5)` answers 5 whether or not the string was ever
   copied.

5. **The refusals.**  A component warmc cannot compile has to say which of the
   things standing in the way it stopped at, and produce nothing.  Silence and
   a broken binary are both worse than an error.

`--prepare` is a no-op: the .wasm files come from run_components.py's own
prepare step, which is what run_all.sh orders before this.
"""
import os
import platform
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
BUILD = os.environ.get("BUILD", "build")
WARMC = os.path.join(BUILD, "warmc") + (".exe" if os.name == "nt" else "")
SRC = "test/components"
WORK = os.path.join(BUILD, "components")
GOLDEN = os.path.join(SRC, "expected")
DARWIN = platform.system() == "Darwin"
WINDOWS = os.name == "nt" or "NT" in platform.system()
# The linked programs are run, so they have to be built for this host.
RT = os.path.join(BUILD, "warmrt-macos.a" if DARWIN else
                  "warmrt-windows.a" if WINDOWS else "warmrt.a")
TARGET = ["--target", "macos"] if DARWIN else \
         ["--target", "windows"] if WINDOWS else []

npass = nfail = 0

def ok():
    global npass
    npass += 1

def fail(msg):
    global nfail
    nfail += 1
    print("FAIL " + msg)

def run(cmd, **kw):
    p = subprocess.run(cmd, capture_output=True, text=True, **kw)
    return p.returncode, p.stdout, p.stderr

CASES = {
    "coreinline": [("run", [])],

    "prims": [("id", ["7"])],
    "restable": [("run", []), ("stress", [])],
    "nested": [("f", ["41"])],

    "composed": [("len", ["5"]), ("len", ["1"]), ("len", ["0"])],
    "fused": [("run", []), ("wide", [])],
    "outer2": [("outer", []), ("local", [])],
    "xreffunc": [("run", ["21"])],
    "xreffunc2": [("t1", []), ("t3", []), ("tt", []), ("selftest", [])],
    "linkable": [
        ("test:link/ops#split", []),
        ("test:link/ops#through-mem", ["5"]),
        ("test:link/ops#step-a", ["4"]),
        ("test:link/ops#step-b", ["9"]),
        ("split-flat", []),
    ],
}

def golden(stem, wasm):
    """--dump-link, against the recorded plan."""
    rc, dump, err = run([WARMC, "--dump-link", wasm])
    if rc != 0:
        fail("%s: --dump-link failed (rc=%d): %s" % (stem, rc, err.strip()))
        return None
    path = os.path.join(GOLDEN, stem + ".link.txt")
    if os.environ.get("UPDATE_GOLDEN"):
        with open(path, "w") as f:
            f.write(dump)
        ok()
        return dump
    if not os.path.exists(path):
        fail("%s: no golden plan at %s (run with UPDATE_GOLDEN=1)" % (stem, path))
        return dump
    with open(path) as f:
        want = f.read()
    if want != dump:
        fail("%s: the plan changed\n--- expected\n%s--- got\n%s" % (stem, want, dump))
    else:
        ok()
    return dump

def wasmtime_invoke(wasm, name, args):
    """The same call through wasmtime.

    wasmtime names a function exported through an interface by its bare name,
    where the Component Model's own mangling -- which is what warmc's export
    table uses -- writes `interface#function`.  Same function, two spellings.
    """
    bare = name.split("#")[-1]
    expr = "%s(%s)" % (bare, ", ".join(args))
    rc, out, err = run(["wasmtime", "run", "--invoke", expr, wasm])
    if rc != 0:
        return None, err.strip().splitlines()[-1] if err.strip() else "rc=%d" % rc
    return out.strip(), None

def compiles(stem, wasm):
    """Compile it, then run every export against wasmtime."""
    out = os.path.join(WORK, stem + ".bin")
    rc, _, err = run([WARMC] + TARGET + ["--rt", RT, "-o", out, wasm])
    if rc != 0:
        fail("%s: warmc would not compile it (rc=%d): %s" % (stem, rc, err.strip()))
        return
    ok()

    have_wasmtime = shutil.which("wasmtime") is not None
    for name, args in CASES[stem]:
        rc, got, err = run([out, name] + args)
        if rc != 0:
            fail("%s: %s failed (rc=%d): %s" % (stem, name, rc, err.strip()))
            continue
        got = got.strip()
        if not have_wasmtime:
            ok()
            continue
        want, why = wasmtime_invoke(wasm, name, args)
        if want is None:
            fail("%s: wasmtime could not invoke %s: %s" % (stem, name, why))
            continue

        mine = got.split(":")[-1]
        if mine != want:
            fail("%s: %s(%s) -- warmc %s, wasmtime %s"
                 % (stem, name, ", ".join(args), mine, want))
        else:
            ok()

def refuses(stem, wasm, dump):
    """It must say why, and leave nothing behind."""
    out = os.path.join(WORK, stem + ".refused")
    if os.path.exists(out):
        os.remove(out)
    rc, _, err = run([WARMC] + TARGET + ["--rt", RT, "-o", out, wasm])
    if rc == 0:
        fail("%s: warmc compiled a component it has no adapter for" % stem)
        return
    if "cannot compile yet" not in err:
        fail("%s: refused without saying why: %s" % (stem, err.strip()))
        return
    if os.path.exists(out):
        fail("%s: refused but wrote %s anyway" % (stem, out))
        return

    reason = err.split("cannot compile yet:", 1)[1].strip()
    blockers = [l[len("blocked: "):] for l in dump.splitlines()
                if l.startswith("blocked: ")]
    if blockers:
        if reason not in blockers:
            fail("%s: refused with a reason the plan does not list: %s"
                 % (stem, reason))
            return
    else:

        if "canonical function" in reason:
            if "<- canon " not in dump:
                fail("%s: refused for an adapter the plan does not show: %s"
                     % (stem, reason))
                return
        elif reason == "it exports no function":
            if "export " in dump:
                fail("%s: refused for exporting nothing, but the plan lists "
                     "an export" % stem)
                return
        elif "export" in reason:
            name = reason.split('"')[1] if '"' in reason else ""
            if ('export "%s"' % name) not in dump:
                fail("%s: refused about an export the plan does not list: %s"
                     % (stem, reason))
                return
        else:
            fail("%s: nothing in the plan is blocked, so the refusal should "
                 "name an export or a binding: %s" % (stem, reason))
            return
    ok()

def main():
    if "--prepare" in sys.argv:
        return 0
    if not os.path.isdir(WORK):
        print("clink: %s not built (run test/run_components.py --prepare)" % WORK)
        return 1

    stems = sorted(f[:-4] for f in os.listdir(SRC) if f.endswith(".wat"))
    if os.path.exists(os.path.join(WORK, "calc.wasm")):
        stems.append("calc")
    stems = [s for s in stems if os.path.exists(os.path.join(WORK, s + ".wasm"))]
    if not stems:
        print("clink: no components in %s" % WORK)
        return 1

    ran = 0
    for stem in stems:
        wasm = os.path.join(WORK, stem + ".wasm")
        dump = golden(stem, wasm)
        if dump is None:
            continue
        if stem in CASES:
            compiles(stem, wasm)
            ran += 1
        else:
            refuses(stem, wasm, dump)

    print("clink: %d passed, %d failed (%d component(s) linked and run against "
          "wasmtime, %d refused with a reason)"
          % (npass, nfail, ran, len(stems) - ran))
    return 1 if nfail else 0

if __name__ == "__main__":
    sys.exit(main())
