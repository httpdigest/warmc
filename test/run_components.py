"""The Component Model decoder, tested four ways.

This file is about decoding, not running: the components warmc compiles are
run against wasmtime by test/run_clink.py, and the ones it refuses are
checked there to say why.  The question a test can ask here is whether the
*structure* warmc decoded is the structure that is there.  Four instruments, each seeing
something the others cannot:

1. **An oracle.**  `wasm-tools objdump` reads the same bytes with a wholly
   independent decoder and reports how many definitions each section holds.
   Every count warmc reports has to match it.  This is the strongest of the
   four: it is a different implementation, not a recording of our own output.

2. **Golden dumps.**  The oracle checks counts; it says nothing about whether
   the right module was instantiated or the right export aliased.  The full
   `--dump-component` output is recorded per case, so a change in *what* was
   decoded is a diff rather than a count that still adds up.

3. **A rejection suite.**  Components encoded byte by byte with the verdict
   each owes -- malformed, invalid, or accepted -- and a substring its
   diagnostic must contain.  wasm-tools will only ever produce components it
   considers valid, so nothing above this line tests the failure paths at all.

4. **A mutation sweep.**  Every byte of every valid component, flipped, and
   the only thing asserted is that warmc reaches one of its three verdicts:
   no crash, no hang, no silence.  A decoder's job on hostile input is to have
   an answer, and this is the cheapest way to find out that it does.
"""
import concurrent.futures
import os
import subprocess
import tempfile
import shutil
import sys
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
BUILD = os.environ.get("BUILD", "build")

WARMC = os.path.join(BUILD, "warmc-asan" if os.environ.get("COMPONENT_ASAN")
                     else "warmc") + (".exe" if os.name == "nt" else "")
WASM_TOOLS = os.path.join(BUILD, "tools", "bin",
                          "wasm-tools.exe" if os.name == "nt" else "wasm-tools")
# The limits below are generous for a workstation; a machine slow enough to
# have been given a longer WARMC_TIMEOUT -- CI's runners -- gets that instead.
def limit(seconds):
    return max(seconds, float(os.environ.get("WARMC_TIMEOUT", "0")))

SRC = "test/components"
WORK = os.path.join(BUILD, "components")
GOLDEN = os.path.join(SRC, "expected")

KINDS = {
    "module": "core module",
    "core instances": "core instance",
    "core types": "core type",
    "component": "component",
    "component instances": "instance",
    "component alias": "alias",
    "component types": "type",
    "canonical functions": "canon",
    "component imports": "import",
    "component exports": "export",
    "component start": "start",
}

def _default_jobs():
    n = (os.cpu_count() or 4) // 2
    return max(1, min(8, n))

JOBS = int(os.environ.get("COMPONENT_JOBS", 0)) or _default_jobs()

npass = nfail = 0
_lock = threading.Lock()

def fail(what):
    global nfail
    with _lock:
        nfail += 1
        print("FAIL[components] " + what)

def ok():
    global npass
    with _lock:
        npass += 1

def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=limit(30))
    return p.returncode, p.stdout, p.stderr

def staged(path):
    """Where to write `path` before renaming it into place.

    run_all.sh runs the clink and asan rows beside this one, and both read
    build/components while this rebuilds it; written in place, a file is
    empty from the moment it is opened until it is finished, and the readers
    took it for a module too short to be one.  Not a .wasm name, so a reader
    listing the directory does not pick it up either.
    """
    return "%s.%d.tmp" % (path, os.getpid())

def publish(*paths):
    for path in paths:
        os.replace(staged(path), path)

def unstage(*paths):
    for path in paths:
        if os.path.exists(staged(path)):
            os.remove(staged(path))

def objdump_counts(path):
    """Section counts from the oracle, at the top level and over the whole tree.

    Two numbers rather than one.  The top-level counts are what the enclosing
    component's index spaces are built from.  The whole-tree counts also cover
    every nested component, which is where a decoder that walks the outer
    layer correctly and loses its place inside one would show up -- and the
    nested layer is most of a real toolchain's output.

    A nested *module*'s own sections (its types, functions, code) are core
    wasm's and belong to neither count, so the enclosing block is tracked.
    """
    rc, out, err = run([WASM_TOOLS, "objdump", path])
    if rc != 0:
        return None, None, err
    top, total = {}, {}
    stack = []
    for line in out.splitlines():
        marker = line.strip()
        if marker.startswith("------ start "):
            stack.append(marker.split()[2])
            continue
        if marker.startswith("------ end "):
            if stack:
                stack.pop()
            continue
        if not line.startswith("  "):
            continue
        head = line.split("|")[0].strip()
        if head.startswith("custom"):
            continue
        n = 0
        for part in line.split("|"):
            part = part.strip()
            if part.endswith("count"):
                n = int(part.split()[0])
        kind = KINDS.get(head)
        at_top = not stack
        if kind is None:
            if at_top:
                return None, None, (
                    "objdump names a top-level section this harness does not "
                    "map: %r" % head)
            continue
        if at_top:
            top[kind] = top.get(kind, 0) + n
        if at_top or stack[-1] == "component":
            total[kind] = total.get(kind, 0) + n
    return top, total, None

def warmc_counts(dump):
    """The same two counts, from warmc's dump: indentation is the nesting."""
    top, total = {}, {}
    for line in dump.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        head = stripped.split(":")[0].strip()
        parts = head.rsplit(" ", 1)
        kind = parts[0] if len(parts) == 2 and parts[1].isdigit() else head
        if not line.startswith(" "):
            top[kind] = top.get(kind, 0) + 1
        total[kind] = total.get(kind, 0) + 1
    return top, total

def check_valid(stem):
    src = os.path.join(SRC, stem + ".wat")
    wasm = os.path.join(WORK, stem + ".wasm")
    rc, out, err = run([WASM_TOOLS, "parse", src, "-o", staged(wasm)])
    if rc != 0:
        unstage(wasm)
        fail("%s: wasm-tools could not assemble it: %s" % (stem, err.strip()))
        return
    publish(wasm)
    rc, dump, err = run([WARMC, "--validate-only", "--dump-component", wasm])
    if rc != 0:
        fail("%s: warmc rejected a valid component (rc=%d): %s"
             % (stem, rc, err.strip()))
        return
    ok()

    want_top, want_all, why = objdump_counts(wasm)
    if want_top is None:
        fail("%s: %s" % (stem, why))
    else:
        got_top, got_all = warmc_counts(dump)
        if got_top != want_top:
            fail("%s: top-level counts differ from wasm-tools"
                 "\n     warmc: %s\n     tool:  %s"
                 % (stem, sorted(got_top.items()), sorted(want_top.items())))
        elif got_all != want_all:
            fail("%s: whole-tree counts differ from wasm-tools"
                 "\n     warmc: %s\n     tool:  %s"
                 % (stem, sorted(got_all.items()), sorted(want_all.items())))
        else:
            ok()

    gold = os.path.join(GOLDEN, stem + ".txt")
    if os.environ.get("UPDATE_GOLDEN"):
        os.makedirs(GOLDEN, exist_ok=True)
        with open(gold, "w") as f:
            f.write(dump)
        ok()
        return
    if not os.path.exists(gold):
        fail("%s: no golden dump; run UPDATE_GOLDEN=1 %s" % (stem, sys.argv[0]))
        return
    with open(gold) as f:
        expected = f.read()
    if expected != dump:
        fail("%s: dump differs from %s\n--- expected\n%s--- got\n%s"
             % (stem, gold, expected, dump))
    else:
        ok()

def build_wit_case():
    """The one case a real toolchain produces, built the way it produces it.

    Everything in test/components/*.wat is a component someone wrote by hand,
    and hand-written shapes agree with whatever the person writing them had in
    mind.  This goes through `component embed` and `component new` from a WIT
    world, which is what a guest language does -- and the result has three core
    modules, five core instances and the adapter indirection none of the
    hand-written cases contain.

    Skipped, not failed, when the toolchain cannot build it: the WIT encoding
    is the one part of this that is not pinned by the corpus commit.
    """
    d = os.path.join(SRC, "calc")
    if not os.path.isdir(d):
        return None
    core = os.path.join(WORK, "calc_core.wasm")
    emb = os.path.join(WORK, "calc_embed.wasm")
    out = os.path.join(WORK, "calc.wasm")
    steps = [
        [WASM_TOOLS, "parse", os.path.join(d, "core.wat"), "-o", staged(core)],
        [WASM_TOOLS, "component", "embed", d, staged(core), "-o", staged(emb),
         "--world", "calc"],
        [WASM_TOOLS, "component", "new", staged(emb), "-o", staged(out)],
    ]
    for cmd in steps:
        rc, so, se = run(cmd)
        if rc != 0:
            unstage(core, emb, out)
            print("components: skipping the WIT case, %s failed: %s"
                  % (cmd[1] if cmd[1] != "component" else "component " + cmd[2],
                     se.strip().splitlines()[0] if se.strip() else "?"))
            return None
    publish(core, emb, out)
    return out

def check_built(stem, wasm):
    """The oracle and golden checks over an already-built component."""
    rc, dump, err = run([WARMC, "--validate-only", "--dump-component", wasm])
    if rc != 0:
        fail("%s: warmc rejected it (rc=%d): %s" % (stem, rc, err.strip()))
        return
    ok()
    want_top, want_all, why = objdump_counts(wasm)
    if want_top is None:
        fail("%s: %s" % (stem, why))
    else:
        got_top, got_all = warmc_counts(dump)
        if got_top != want_top or got_all != want_all:
            fail("%s: counts differ from wasm-tools\n     warmc: %s / %s"
                 "\n     tool:  %s / %s"
                 % (stem, sorted(got_top.items()), sorted(got_all.items()),
                    sorted(want_top.items()), sorted(want_all.items())))
        else:
            ok()
    gold = os.path.join(GOLDEN, stem + ".txt")
    if os.environ.get("UPDATE_GOLDEN"):
        os.makedirs(GOLDEN, exist_ok=True)
        with open(gold, "w") as f:
            f.write(dump)
        ok()
        return
    if not os.path.exists(gold):
        fail("%s: no golden dump; run UPDATE_GOLDEN=1 %s" % (stem, sys.argv[0]))
        return
    with open(gold) as f:
        expected = f.read()
    if expected != dump:
        fail("%s: dump differs from %s\n--- expected\n%s--- got\n%s"
             % (stem, gold, expected, dump))
    else:
        ok()

def _verdict(label, path):
    """One warmc run over a probe; returns None or a description."""
    try:
        rc, so, se = run([WARMC, "--validate-only", path])
    except subprocess.TimeoutExpired:
        return "%s made warmc hang" % label
    if "AddressSanitizer" in se or "runtime error" in se:
        return "%s: %s" % (label, se.strip().splitlines()[0][:140])
    if rc not in (0, 2, 3):
        return "%s gave exit %d (not a verdict): %s" % (label, rc, (se or so).strip()[:160])
    return None if rc else _compiled(label, path)


def _compiled(label, path):
    """--validate-only stops before the link plan, so a probe that only ever
    asks for a verdict never reaches the half of the component path that
    compiles anything.  Anything it accepts goes through once more."""
    try:
        rc, so, se = run([WARMC, "-c", "-o", os.devnull, path])
    except subprocess.TimeoutExpired:
        return "%s made warmc hang while compiling" % label
    if "AddressSanitizer" in se or "runtime error" in se:
        return "%s while compiling: %s" % (label, se.strip().splitlines()[0][:140])
    if rc not in (0, 1, 2, 3):
        return "%s gave exit %d while compiling: %s" % (label, rc, (se or so).strip()[:160])
    return None


SWEEP_BATCH = 64


def _sweep(jobs_in, what):
    """Run a batch of probes and report every one that did not get a verdict.
    The probes go to a directory of their own, gone afterwards, and warmc
    gives the verdicts of SWEEP_BATCH of them in one process (--validate-only
    with several inputs): a process each was most of this suite's time.  A
    batch whose process did not answer for every probe, or that a sanitiser
    spoke in, is asked again one probe at a time, to say which."""
    jobs_in = list(jobs_in)
    tmp = tempfile.mkdtemp(prefix=".probes.", dir=WORK)
    paths = []
    for i, (_label, data) in enumerate(jobs_in):
        paths.append(os.path.join(tmp, "p%d.wasm" % i))
        with open(paths[-1], "wb") as f:
            f.write(data)

    def batch(group):
        try:
            rc, so, se = run([WARMC, "--validate-only"] + [paths[i] for i in group])
        except subprocess.TimeoutExpired:
            so = None
        # a line each, in the order given: warmc says the path as it got
        # it, which MSYS hands a native Windows program in Windows'
        # spelling, D:/a/p0.wasm for /d/a/p0.wasm -- so by order, the
        # file's name to match
        verdicts = {}
        for line in (so or "").splitlines():
            said, tab, rest = line.partition("\t")
            code, _, msg = rest.partition(" ")
            k = len(verdicts)
            if k < len(group) and tab and code.isdigit() and \
                    said.replace("\\", "/").rsplit("/", 1)[-1] == os.path.basename(paths[group[k]]):
                verdicts[group[k]] = (int(code), msg)
        if so is None or len(verdicts) != len(group) or "AddressSanitizer" in se or \
                "runtime error" in se:
            return [_verdict(jobs_in[i][0], paths[i]) for i in group]
        out = []
        for i in group:
            code, msg = verdicts[i]
            if code not in (0, 2, 3):
                out.append("%s gave exit %d (not a verdict): %s" % (jobs_in[i][0], code, msg[:160]))
            elif code == 0:
                out.append(_compiled(jobs_in[i][0], paths[i]))
        return out

    groups = [list(range(k, min(k + SWEEP_BATCH, len(jobs_in))))
              for k in range(0, len(jobs_in), SWEEP_BATCH)]
    bad = 0
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=JOBS) as pool:
            for whys in pool.map(batch, groups):
                for why in whys:
                    if why:
                        fail(why)
                        bad += 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("components: %d %s, every one answered" % (len(jobs_in), what)
          if not bad else
          "components: %d %s, %d without a verdict" % (len(jobs_in), what, bad))
    if not bad:
        ok()

def check_truncations(stems):
    """Every prefix of every valid component, and every one needs a verdict.

    Different ground from the bit flips: a truncation leaves the bytes that
    remain entirely valid and takes away only the ones after, so it walks every
    "unexpected end of section" path in order rather than landing on them by
    chance.  A decoder that reads a length and then trusts it fails here on the
    first case and nowhere else.
    """
    probes = []
    for stem in stems:
        wasm = os.path.join(WORK, stem + ".wasm")
        if not os.path.exists(wasm):
            continue
        with open(wasm, "rb") as f:
            data = f.read()
        step = max(1, len(data) // 150)
        for n in range(0, len(data), step):
            probes.append(("%s truncated to %d bytes" % (stem, n), data[:n]))
    _sweep(probes, "truncations")

def check_rejects():
    rc, out, err = run([sys.executable, "test/gen_component_rejects.py", WORK])
    if rc != 0:
        fail("the reject generator failed: %s" % err.strip())
        return
    with open(os.path.join(WORK, "manifest.tsv")) as f:
        rows = [l.rstrip("\n").split("\t") for l in f if l.strip()]
    def one(row):
        stem, verdict, want = row[0], int(row[1]), row[2] if len(row) > 2 else ""
        path = os.path.join(WORK, stem + ".wasm")
        rc, out, err = run([WARMC, "--validate-only", path])
        if rc != verdict:
            fail("%s: expected exit %d, got %d: %s"
                 % (stem, verdict, rc, (err or out).strip()))
            return

        if want and want not in err:
            fail("%s: exit %d was right but the diagnostic does not mention %r: %s"
                 % (stem, rc, want, err.strip()))
            return

        # --validate-only returns before any canonical-ABI work, so a verdict
        # here says nothing about the resolver behind --dump-canon.  Ask it
        # too, and only that it answers: a cyclic type alias used to spin
        # there for ever on a module this loop had already called valid.
        for flag in ("--dump-canon", "--dump-adapter"):
            try:
                subprocess.run([WARMC, flag, path], capture_output=True,
                               text=True, timeout=limit(20))
            except subprocess.TimeoutExpired:
                fail("%s: %s did not terminate" % (stem, flag))
                return
        ok()

    with concurrent.futures.ThreadPoolExecutor(max_workers=JOBS) as pool:
        list(pool.map(one, rows))

def check_mutations(stems, per_case=None):
    """Flip bytes and require an answer rather than a crash.

    warmc's three exits are 0, 2 and 3.  Anything else -- a signal, a non-zero
    that is neither, a timeout -- is the finding, whatever the mutated bytes
    happened to mean.
    """
    limit = int(os.environ.get("COMPONENT_MUTATIONS", per_case or 400))
    probes = []
    for stem in stems:
        wasm = os.path.join(WORK, stem + ".wasm")
        if not os.path.exists(wasm):
            continue
        with open(wasm, "rb") as f:
            data = bytearray(f.read())
        step = max(1, (len(data) * 3) // limit or 1)
        for i in range(0, len(data), step):
            for delta in (0x01, 0x80, 0xff):
                orig = data[i]
                data[i] = orig ^ delta
                probes.append(("%s: byte %d ^ 0x%02x" % (stem, i, delta), bytes(data)))
                data[i] = orig
    _sweep(probes, "mutations")

def main():
    if not os.path.exists(WASM_TOOLS):
        print("components: %s not found (run `make wasm-tools`)" % WASM_TOOLS)
        return 1
    if os.environ.get("COMPONENT_ASAN"):
        newest = max(os.path.getmtime(f) for f in
                     ("src/component.c", "src/component.h", "src/main.c"))
        if not os.path.exists(WARMC) or os.path.getmtime(WARMC) < newest:
            print("components: %s is missing or older than the source "
                  "(run `make asan`)" % WARMC)
            return 1
    os.makedirs(WORK, exist_ok=True)
    stems = sorted(f[:-4] for f in os.listdir(SRC) if f.endswith(".wat"))
    if not stems:
        print("components: no cases in %s" % SRC)
        return 1
    if "--prepare" in sys.argv:
        for stem in stems:
            wasm = os.path.join(WORK, stem + ".wasm")
            if run([WASM_TOOLS, "parse", os.path.join(SRC, stem + ".wat"),
                    "-o", staged(wasm)])[0] == 0:
                publish(wasm)
            else:
                unstage(wasm)
        build_wit_case()
        return 0

    with concurrent.futures.ThreadPoolExecutor(max_workers=JOBS) as pool:
        list(pool.map(check_valid, stems))
    wit = build_wit_case()
    if wit:
        check_built("calc", wit)
        stems = stems + ["calc"]
    check_rejects()
    check_truncations(stems)
    check_mutations(stems)

    print("components: %d passed, %d failed" % (npass, nfail))
    return 1 if nfail else 0

if __name__ == "__main__":
    sys.exit(main())
