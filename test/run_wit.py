"""The WIT parser, tested six ways.

WIT is text, so unlike the component decoder there is a second implementation
of exactly this job to compare against: `wasm-tools component wit` parses the
same documents, and `--json` hands back its whole resolved model.  That makes
a *semantic* differential possible rather than only an acceptance one.

1. **Structural agreement with `--json`.**  For every document both parsers
   read, the interfaces, the worlds, the type names in each interface, and the
   function names in each interface must be the same sets.  Function names are
   compared in the component model's mangled spelling (`[method]res.name`), so
   the resource sugar is expanded the same way on both sides.

2. **Acceptance agreement.**  warmc accepts a document iff wasm-tools does.
   This covers the documents `--json` cannot be asked about because they are
   meant to be rejected.

3. **A fuzz corpus from an independent generator.**  `wasm-tools wit-smith`
   turns a seed into a random valid WIT package; the seeds are fixed, so the
   corpus is reproducible.  This found five bugs on its first run, one of them
   a use-after-free.

4. **Golden dumps**, so a change in *what* was parsed is a diff rather than a
   count that still adds up.

5. **A rejection suite** -- documents that must not parse, each naming a
   substring its diagnostic must contain.

6. **A mutation sweep**, asserting only that warmc reaches a verdict rather
   than crashing.  Under `WIT_ASAN=1` this runs the sanitiser build instead,
   which is what turns a use-after-free from an exit code into a diagnosis.
"""
import concurrent.futures
import json
import os
import subprocess
import sys
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
BUILD = os.environ.get("BUILD", "build")
WARMC = os.path.join(BUILD, "warmc-asan" if os.environ.get("WIT_ASAN")
                     else "warmc") + (".exe" if os.name == "nt" else "")
WASM_TOOLS = os.path.join(BUILD, "tools", "bin",
                          "wasm-tools.exe" if os.name == "nt" else "wasm-tools")
SRC = "test/wit"
WORK = os.path.join(BUILD, "wit")
GOLDEN = os.path.join(SRC, "expected")

FUZZ_SEEDS = int(os.environ.get("WIT_FUZZ_SEEDS", "24"))

def _default_jobs():
    return max(1, min(8, (os.cpu_count() or 4) // 2))

JOBS = int(os.environ.get("WIT_JOBS", 0)) or _default_jobs()

npass = nfail = 0
_lock = threading.Lock()

def fail(what):
    global nfail
    with _lock:
        nfail += 1
        print("FAIL[wit] " + what)

def ok():
    global npass
    with _lock:
        npass += 1

# The limits below are generous for a workstation; a machine slow enough to
# have been given a longer WARMC_TIMEOUT -- CI's runners -- gets that instead.
def limit(seconds):
    return max(seconds, float(os.environ.get("WARMC_TIMEOUT", "0")))

def run(cmd, timeout=60):
    p = subprocess.run(cmd, capture_output=True, timeout=limit(timeout))
    return (p.returncode,
            p.stdout.decode("utf-8", "replace"),
            p.stderr.decode("utf-8", "replace"))

def oracle_model(path):
    """wasm-tools' resolved model, reduced to what both parsers can state."""
    rc, out, err = run([WASM_TOOLS, "component", "wit", path, "--json"])
    if rc != 0:
        return None
    try:
        d = json.loads(out)
    except ValueError:
        return None

    pkgs = [q.get("name") for q in d.get("packages", [])]

    def where(entry):

        i = entry.get("package")
        return pkgs[i] if i is not None and i < len(pkgs) else "-"
    ifaces = {}
    for i in d.get("interfaces", []):
        name = i.get("name")
        if name is None:
            continue
        ifaces[(where(i), name)] = {
            "types": sorted(i.get("types", {})),
            "funcs": sorted(i.get("functions", {})),
        }
    worlds = sorted((where(w), w.get("name"))
                    for w in d.get("worlds", []) if w.get("name"))
    return {"interfaces": ifaces, "worlds": worlds}

def warmc_model(dump):
    """The same reduction, from warmc's own dump."""
    ifaces = {}
    worlds = []
    cur = None
    for line in dump.splitlines():
        if line.startswith("package "):
            cur = None
            continue
        if line.startswith("interface-inline "):
            cur = None
            continue
        if line.startswith("interface ") or line.startswith("world "):

            head, _, tail = line.partition(" [")
            pkg = tail.rstrip("]\n") or "-"
            parts = head.split(None, 2)
            name = parts[2] if len(parts) > 2 else None
            if line.startswith("world "):
                if name:
                    worlds.append((pkg, name))
                cur = None
                continue
            cur = (pkg, name) if name else None
            if cur is not None:
                ifaces[cur] = {"types": [], "funcs": []}
            continue
        if cur is None or not line.startswith("  "):
            continue
        body = line.strip()
        head = body.split(None, 1)
        if len(head) < 2:
            continue
        kind, rest = head[0], head[1]
        if kind == "func":
            ifaces[cur]["funcs"].append(rest.split("(")[0].replace("async ", ""))
        elif kind in ("type", "record", "variant", "enum", "flags", "resource",
                      "use"):
            ifaces[cur]["types"].append(rest.split()[0])
    for v in ifaces.values():
        v["types"].sort()
        v["funcs"].sort()
    return {"interfaces": ifaces, "worlds": sorted(worlds)}

def compare(stem, path, dump):
    want = oracle_model(path)
    if want is None:
        return
    if want.get("_inline_present"):
        return
    got = warmc_model(dump)
    if got["worlds"] != want["worlds"]:
        fail("%s: world names differ\n     warmc: %s\n     tool:  %s"
             % (stem, got["worlds"], want["worlds"]))
        return
    if sorted(got["interfaces"]) != sorted(want["interfaces"]):
        fail("%s: interface names differ\n     warmc: %s\n     tool:  %s"
             % (stem, sorted(got["interfaces"]), sorted(want["interfaces"])))
        return
    for name, w in want["interfaces"].items():
        g = got["interfaces"][name]
        if g["types"] != w["types"]:
            fail("%s: interface %s type names differ\n     warmc: %s\n     tool:  %s"
                 % (stem, name, g["types"], w["types"]))
            return
        if g["funcs"] != w["funcs"]:
            fail("%s: interface %s function names differ\n     warmc: %s\n     tool:  %s"
                 % (stem, name, g["funcs"], w["funcs"]))
            return
    ok()

RESOLUTION_ONLY = ("feature not being activated", "cannot import function")

def is_resolution_error(err):
    return any(m in err for m in RESOLUTION_ONLY)

def check_case(path, stem, golden=True):
    rc, dump, err = run([WARMC, "--dump-wit", path])
    tool_rc, _, tool_err = run([WASM_TOOLS, "component", "wit", path])
    tool_ok = tool_rc == 0 or is_resolution_error(tool_err)
    if rc != 0:
        if tool_ok:
            fail("%s: warmc rejected a document wasm-tools accepts: %s"
                 % (stem, err.strip().splitlines()[:1]))
        else:
            ok()
        return
    if not tool_ok:
        fail("%s: warmc accepted a document wasm-tools rejects: %s"
             % (stem, tool_err.strip().splitlines()[:1]))
        return
    ok()
    compare(stem, path, dump)

    if not golden:
        return
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

def build_fuzz_corpus():
    """Random valid WIT from wit-smith, one document per fixed seed.

    Deterministic: the seed bytes come from Python's Mersenne twister with the
    seed number, so the corpus is the same on every machine and a failure can
    be reproduced by naming the seed.
    """
    import random
    out = []
    d = os.path.join(WORK, "fuzz")
    os.makedirs(d, exist_ok=True)
    for i in range(1, FUZZ_SEEDS + 1):
        wit = os.path.join(d, "seed%03d.wit" % i)
        if not os.path.exists(wit):
            raw = os.path.join(d, "seed%03d.bin" % i)
            comp = os.path.join(d, "seed%03d.wasm" % i)
            rng = random.Random(i)
            with open(raw, "wb") as f:
                f.write(bytes(rng.randrange(256) for _ in range(600)))
            rc, _, _ = run([WASM_TOOLS, "wit-smith", raw, "-o", comp])
            if rc != 0:
                continue
            rc, text, _ = run([WASM_TOOLS, "component", "wit", comp])
            if rc != 0:
                continue
            with open(wit, "w") as f:
                f.write(text)
        out.append(wit)
    return out

REJECTS = [
    ("no_closing_brace", "unterminated interface",
     "package a:b;\ninterface i {\n  f: func();\n"),
    ("missing_semicolon", "expected ; after a function",
     "package a:b;\ninterface i {\n  f: func()\n}\n"),
    ("unknown_type", "unknown type",
     "package a:b;\ninterface i {\n  f: func(x: nope);\n}\n"),
    ("bad_type_in_record", "unknown type",
     "package a:b;\ninterface i {\n  record r { f: missing }\n}\n"),
    ("keyword_as_name", "expected an identifier",
     "package a:b;\ninterface i {\n  record record { f: u32 }\n}\n"),
    ("bad_toplevel", "expected package, use, interface or world",
     "package a:b;\nnonsense\n"),
    ("unterminated_block_comment", "unterminated block comment",
     "package a:b;\n/* never closed\ninterface i {}\n"),
    ("unterminated_string", "unterminated string",
     'package a:b;\ninterface i {\n  @external-id("oops\n}\n'),
    ("bad_world_item", "expected import, export, use, include or a type",
     "package a:b;\nworld w {\n  nonsense;\n}\n"),
    ("world_item_bad_type", "expected a name after : in a use path",
     "package a:b;\nworld w {\n  import x: 42;\n}\n"),
    ("missing_gt", "expected > to close",
     "package a:b;\ninterface i {\n  type t = list<u32;\n}\n"),
    ("bad_version", "expected a version after @",
     "package a:b@;\n"),
    ("empty_result_arrow", "expected a type",
     "package a:b;\ninterface i {\n  f: func() -> ;\n}\n"),
    ("nested_package_unterminated", "unterminated nested package",
     "package a:b;\npackage c:d {\n  interface i {}\n"),
    ("use_without_dot", "expected . after a use path",
     "package a:b;\ninterface i {\n  use other;\n}\n"),

    ("use_unknown_package", "no package `nope:missing`",
     "package a:b;\ninterface i {\n  use nope:missing/x.{t};\n}\n"),
    ("use_unknown_interface", "has no interface",
     "package a:b;\ninterface i {\n  use c:d/absent.{t};\n}\n"
     "package c:d {\n  interface present { type t = u32; }\n}\n"),
    ("use_unknown_name", "has no type",
     "package a:b;\ninterface i {\n  use c:d/x.{missing};\n}\n"
     "package c:d {\n  interface x { type t = u32; }\n}\n"),

    ("unversioned_does_not_match", "no package `c:d`",
     "package a:b;\ninterface i {\n  use c:d/x.{t};\n}\n"
     "package c:d@1.0.0 {\n  interface x { type t = u32; }\n}\n"),
    ("wrong_version", "no package `c:d@2.0.0`",
     "package a:b;\ninterface i {\n  use c:d/x@2.0.0.{t};\n}\n"
     "package c:d@1.0.0 {\n  interface x { type t = u32; }\n}\n"),
    ("world_unknown_interface", "no interface `absent`",
     "package a:b;\nworld w {\n  import absent;\n}\n"),

    ("accept_versioned_use", None,
     "package a:b;\ninterface i {\n  use c:d/x@1.0.0.{t};\n  f: func() -> t;\n}\n"
     "package c:d@1.0.0 {\n  interface x { type t = u32; }\n}\n"),
    ("accept_toplevel_use", None,
     "package a:b;\nuse c:d/x@1.0.0 as renamed;\n"
     "world w {\n  import renamed;\n}\n"
     "package c:d@1.0.0 {\n  interface x { type t = u32; }\n}\n"),

    ("accept_empty", None, "package a:b;\n"),
    ("accept_no_package", None, "interface i {\n  f: func();\n}\n"),
    ("accept_only_comments", None, "// nothing here\n/* nor here */\n"),
]

def check_rejects():
    d = os.path.join(WORK, "reject")
    os.makedirs(d, exist_ok=True)

    def one(case):
        stem, want, text = case
        path = os.path.join(d, stem + ".wit")
        with open(path, "w") as f:
            f.write(text)
        rc, out, err = run([WARMC, "--dump-wit", path])
        if want is None:
            if rc != 0:
                fail("%s: expected it to parse, got: %s" % (stem, err.strip()))
            else:
                ok()
            return
        if rc == 0:
            fail("%s: expected a rejection, it parsed" % stem)
            return
        if want not in err:
            fail("%s: rejected, but the diagnostic does not mention %r: %s"
                 % (stem, want, err.strip()))
            return
        ok()

    with concurrent.futures.ThreadPoolExecutor(max_workers=JOBS) as pool:
        list(pool.map(one, REJECTS))

def check_mutations(paths):
    """Flip bytes and require a verdict rather than a crash."""
    probes = []
    for path in paths:
        with open(path, "rb") as f:
            data = bytearray(f.read())
        if not data:
            continue
        base = os.path.basename(path)
        step = max(1, len(data) // int(os.environ.get("WIT_MUTATION_DENSITY", "40")))
        for i in range(0, len(data), step):
            for delta in (0x01, 0x7f, 0xff):
                o = data[i]
                data[i] = o ^ delta
                probes.append(("%s byte %d^%02x" % (base, i, delta), bytes(data)))
                data[i] = o
        for n in range(0, len(data), max(1, len(data) // 30)):
            probes.append(("%s truncated %d" % (base, n), bytes(data[:n])))

    d = os.path.join(WORK, "mutants")
    os.makedirs(d, exist_ok=True)

    def one(job):
        label, data = job
        path = os.path.join(d, "m-%d.wit" % threading.get_ident())
        with open(path, "wb") as f:
            f.write(data)
        try:
            rc, out, err = run([WARMC, "--dump-wit", path])
        except subprocess.TimeoutExpired:
            return label + ": made warmc hang"
        if "AddressSanitizer" in err or "runtime error" in err:
            return label + ": " + err.strip().splitlines()[0][:120]

        if rc not in (0, 2, 3):
            return "%s: exit %d (not a verdict): %s" % (label, rc, err.strip()[:160])
        return None

    bad = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=JOBS) as pool:
        for why in pool.map(one, probes, chunksize=16):
            if why:
                fail(why)
                bad += 1
    print("wit: %d mutations, %s" % (len(probes),
                                     "every one answered" if not bad
                                     else "%d without a verdict" % bad))
    if not bad:
        ok()

cwit_pass = cwit_skip = 0

def check_component_wit(name, path):
    global cwit_pass, cwit_skip
    rc, mine, err = run([WARMC, "--validate-only", "--dump-component-wit", path])
    if rc != 0:
        fail("%s: --dump-component-wit failed: %s" % (name, err.strip()[:120]))
        return
    rc, theirs, err = run([WASM_TOOLS, "component", "wit", path])
    if rc != 0:
        return
    if "unknown" in mine or "// not yet:" in mine:
        cwit_skip += 1
        return
    a = os.path.join(WORK, name.replace("/", "_") + ".mine.wit")
    b = os.path.join(WORK, name.replace("/", "_") + ".theirs.wit")
    with open(a, "w") as f: f.write(mine)
    with open(b, "w") as f: f.write(theirs)
    rca, da, ea = run([WARMC, "--dump-wit", a])
    if rca != 0:
        fail("%s: warmc will not parse its own rendering: %s"
             % (name, ea.strip().splitlines()[0][:160] if ea.strip() else ""))
        return
    rcb, db, eb = run([WARMC, "--dump-wit", b])
    if rcb != 0:
        return
    ma, mb = warmc_model(da), warmc_model(db)
    if ma != mb:
        ka, kb = set(ma["interfaces"]), set(mb["interfaces"])
        if ka != kb:
            fail("%s: interfaces differ: only warmc %s, only wasm-tools %s"
                 % (name, sorted(ka - kb)[:3], sorted(kb - ka)[:3]))
            return
        for k in sorted(ka):
            if ma["interfaces"][k] != mb["interfaces"][k]:
                fail("%s: %s differs: warmc %s, wasm-tools %s"
                     % (name, k, ma["interfaces"][k], mb["interfaces"][k]))
                return
        fail("%s: worlds differ: warmc %s, wasm-tools %s"
             % (name, ma["worlds"], mb["worlds"]))
        return
    ok()
    cwit_pass += 1

def main():
    if not os.path.exists(WASM_TOOLS):
        print("wit: %s not found (run `make wasm-tools`)" % WASM_TOOLS)
        return 1
    if not os.path.exists(WARMC):
        print("wit: %s not found (run `make asan`)" % WARMC)
        return 1

    newest = max(os.path.getmtime(f) for f in
                 ("src/wit.c", "src/wit.h", "src/component.c", "src/main.c"))
    if os.path.getmtime(WARMC) < newest:
        print("wit: %s is older than the source (run `make asan`)" % WARMC)
        return 1
    os.makedirs(WORK, exist_ok=True)

    cases = sorted(f for f in os.listdir(SRC)
                   if f.endswith(".wit") or
                   (os.path.isdir(os.path.join(SRC, f)) and f != "expected"))
    if "--prepare" in sys.argv:
        build_fuzz_corpus()
        return 0

    def stem_of(f):
        return f[:-4] if f.endswith(".wit") else f

    with concurrent.futures.ThreadPoolExecutor(max_workers=JOBS) as pool:
        list(pool.map(lambda f: check_case(os.path.join(SRC, f), stem_of(f)),
                      cases))

    fuzz = build_fuzz_corpus()
    print("wit: %d documents from wit-smith" % len(fuzz))
    with concurrent.futures.ThreadPoolExecutor(max_workers=JOBS) as pool:
        list(pool.map(lambda p: check_case(p, os.path.basename(p)[:-4],
                                           golden=False), fuzz))

    try:
        import run_canon
        comps = run_canon.components()
    except Exception:
        comps = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=JOBS) as pool:
        list(pool.map(lambda np: check_component_wit(np[0], np[1]), comps))
    print("wit: %d component(s) render back to the same interfaces wasm-tools "
          "reads out of them, %d not yet" % (cwit_pass, cwit_skip))

    check_rejects()
    check_mutations([os.path.join(SRC, f) for f in cases
                     if f.endswith(".wit")] + fuzz[:6])

    print("wit: %d passed, %d failed" % (npass, nfail))
    return 1 if nfail else 0

if __name__ == "__main__":
    sys.exit(main())
