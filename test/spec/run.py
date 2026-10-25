"""Run the upstream WebAssembly spec testsuite against warmc.

The corpus is the one the spec is defined by, and it is the only instrument
that can say whether a rejection is the *right* rejection: a module that
warmc refuses for the wrong reason, or accepts when the spec says it is
invalid, is invisible to a differential harness that only ever feeds it valid
modules.

What runs:

  assert_malformed   the module must be rejected as malformed  (exit 2)
  assert_invalid     the module must be rejected as invalid    (exit 3)
  module             the module must compile

each for a binary and for a quoted text module alike, the text going through
warmc's own text front end; then every module the .wast writes as text is
assembled by warmc and must come out as wasm-tools' binary of it does
(test/spec/wasttext.py), so that the binaries the rest of this runs are the
ones warmc's text front end would have made; and then, against the compiled
modules linked into one driver (driver.c, with the tables test/spec/gen.py
writes), the commands that execute: assert_return, assert_trap,
assert_exhaustion, assert_exception, assert_uninstantiable, assert_unlinkable,
register and action.  A quoted module that is valid runs as warmc assembles
it, wasm-tools having made no binary of it.  A command the generator cannot
express -- one naming a module that did not compile -- is counted as
`skipped` rather than dropped, so the totals do not silently shrink.

Every file's result is compared against test/spec/expected.json, which records
what warmc fails today.  A file that starts passing is as much of a mismatch
as one that starts failing: the point of the file is that the number moves
only when someone means it to.

Cost.  On macOS the first run of a freshly linked binary costs a fifth of a
second of signature validation whatever is in it, and this used to link one
per file per level -- five hundred and fourteen of them, a minute of waiting
before a single assertion ran.  Three things follow, and between them they are
most of what this file does that a straight loop would not:

  * Every file's modules go into *one* executable per level, which is then
    invoked once per file.  The validation is paid once; the remaining
    invocations cost milliseconds.  Each file still gets its own process, so a
    module that faults ends its own run and every other file still reports,
    and its memory is returned in between.
  * `--validate-only` does not take an optimisation level -- it parses and
    type-checks and stops.  Running the corpus at two levels therefore has
    exactly one validation half, not two, and the second was pure repetition.
  * The compiling, and the .wast conversion before it, is per file and
    independent, so it runs in a process pool; the conversion's output is a
    function of the .wast and of the converter, neither of which changes
    between runs, so it is also cached.

The converter is `wasm-tools json-from-wast`, not wabt's `wast2json`.  That is
not a preference.  wabt 1.0.41 has no garbage-collection support in its text
parser at all -- no `struct`, no `array`, no `i8` field, no `rec`, no `sub`,
not even `anyref` in a `ref.null` -- so thirty of the corpus's files could not
be converted, and they are precisely the files for the last three proposals
warmc implemented or is implementing.  A milestone whose only test is a tool
that refuses to read it has no test.

wabt was also silently wrong on one it did read: `align=0x8000000000000000` in
`align.wast` came out encoded as align 0, so the module warmc was handed was
not the module the file describes.  With custom sections dropped the two
converters agree on 5,161 of 5,330 modules; the rest are wabt folding a
`(type $t)` block type down to 0x40, wabt dropping the name section, and that
one miscoding.
"""
import concurrent.futures
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import gen
import hostbuild
import proposals
import wasttext

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.environ.get("BUILD", os.path.join(ROOT, "build"))
SUITE = os.environ.get("WASM_TESTSUITE", os.path.join(BUILD, "testsuite"))
WORK = os.path.join(BUILD, "spec")
WARMC = os.path.join(BUILD, "warmc") + (".exe" if os.name == "nt" else "")
EXPECTED = os.path.join(ROOT, "test", "spec", "expected.json")

EXIT_MALFORMED = 2
EXIT_INVALID = 3

RUNTIME_COMMANDS = {
    "action", "assert_return", "assert_trap", "assert_exhaustion",
    "assert_exception",
    "assert_uninstantiable", "assert_unlinkable", "register",
}

DARWIN = platform.system() == "Darwin"

WABT_FLAGS = hostbuild.wabt_flags()

def _pinned(name):
    """A version the Makefile pins, so the two cannot drift apart."""
    with open(os.path.join(ROOT, "Makefile")) as f:
        for line in f:
            if line.startswith(name):
                return line.split(":=", 1)[1].strip()
    raise SystemExit("spec: no %s in the Makefile" % name)

WASM_TOOLS_VERSION = _pinned("WASM_TOOLS_VERSION")
TESTSUITE_COMMIT = _pinned("TESTSUITE_COMMIT")
WASM_TOOLS = os.path.join(BUILD, "tools", "bin",
                          "wasm-tools.exe" if os.name == "nt" else "wasm-tools")

WARMC_TIMEOUT = float(os.environ.get("WARMC_TIMEOUT", "3"))
RUN_TIMEOUT = float(os.environ.get("WARMC_RUN_TIMEOUT", "120"))

def sh(cmd, timeout=None):
    """Run a tool.  A timeout is reported as an exit code and a message rather
    than raised, so one hung file costs its own line and not the whole run."""
    try:
        return subprocess.run(cmd, capture_output=True, text=True,
                              timeout=timeout)
    except subprocess.TimeoutExpired:
        return subprocess.CompletedProcess(
            cmd, 124, "", "timed out after %gs" % timeout)

def level_tag(level):
    """A filename-safe name for one flag set, so levels do not share objects."""
    return "".join(c if c.isalnum() else "_" for c in "_".join(level)) or "default"

def _tool_version(tool):
    r = subprocess.run([tool, "--version"], capture_output=True, text=True)
    return (r.stdout + r.stderr).strip()

CONVERTER_VERSION = None

def wast2json(wast, outdir):
    """Convert one .wast, reusing the last conversion when nothing moved.

    The corpus is pinned to a commit and the converter is pinned to a version,
    so the output is a function of those two alone.  Both go into the stamp:
    keyed on the .wast by itself, a converter upgrade would be read through a
    cache full of the old converter's answers.
    """
    stem = os.path.basename(wast)[:-5]
    out = os.path.join(outdir, stem + ".json")
    with open(wast, "rb") as f:
        key = hashlib.sha256(f.read()).hexdigest()
    key += " " + (CONVERTER_VERSION or "")
    stamp = os.path.join(outdir, ".wast2json.stamp")
    if os.path.exists(out):
        try:
            with open(stamp) as f:
                if f.read() == key:
                    return out, None
        except OSError:
            pass

    if os.path.exists(stamp):
        os.remove(stamp)
    os.makedirs(outdir, exist_ok=True)
    r = sh([WASM_TOOLS, "json-from-wast", "--wasm-dir", outdir,
            "-o", out, wast], timeout=RUN_TIMEOUT)
    if r.returncode != 0:

        msg = [ln for ln in r.stderr.strip().splitlines() if ln.strip()]
        return None, msg[:1]
    with open(stamp, "w") as f:
        f.write(key)
    return out, None

def validate(path):
    """Compile-time verdict for one module file: (exit code, first message).

    No optimisation level: this parses and type-checks and stops, so its
    answer is the same whichever level the executables are built at.
    """
    r = sh([WARMC, "--validate-only", path], timeout=WARMC_TIMEOUT)
    msg = (r.stderr.strip().splitlines() or [""])[0]
    return r.returncode, msg

def run_file(wast, idx, levels, verbose=False):
    """Validate one file once, then build its tables for each level."""
    stem = os.path.basename(wast)[:-5]
    outdir = os.path.join(WORK, stem)
    js, err = wast2json(wast, outdir)
    if js is None:

        note = err[0] if err else ""
        if note.startswith(SUITE + os.sep):
            note = note[len(SUITE) + 1:]
        return {"wast2json": False, "note": note, "fail": 0,
                "pass": 0, "skip": 0,
                "build": {lv_key(lv): {"status": "wast2json: " + note[:70]}
                          for lv in levels}}

    with open(js) as f:
        spec = json.load(f)

    npass = nfail = nskip = 0
    failures = []
    for cmd in spec["commands"]:
        kind = cmd["type"]
        if kind in RUNTIME_COMMANDS:
            nskip += 1
            continue
        if kind in ("module", "module_quote", "module_definition", "module_instance"):

            if kind not in ("module", "module_definition") or "filename" not in cmd:
                nskip += 1
                continue
            path = os.path.join(outdir, cmd["filename"])
            rc, msg = validate(path)
            if rc == 0:
                npass += 1
            else:
                nfail += 1
                failures.append("line %s: module rejected: %s" % (cmd.get("line"), msg))
            if rc == 0 and cmd.get("module_type") == "text":
                # wasm-tools made no binary of a quoted module, so the one
                # the execution half runs is warmc's
                wasm = os.path.splitext(path)[0] + ".quote.wasm"
                r = sh([WARMC, "--emit-wasm", "-o", wasm, path], timeout=WARMC_TIMEOUT)
                if r.returncode == 0:
                    cmd["filename"] = os.path.basename(wasm)
                    cmd["module_type"] = "binary"
            continue
        if kind in ("assert_malformed", "assert_invalid"):

            want = EXIT_MALFORMED if kind == "assert_malformed" else EXIT_INVALID
            rc, msg = validate(os.path.join(outdir, cmd["filename"]))
            if rc == want:
                npass += 1
            else:
                nfail += 1
                got = {0: "accepted", EXIT_MALFORMED: "malformed",
                       EXIT_INVALID: "invalid"}.get(rc, "exit %d" % rc)
                failures.append("line %s: want %s (%s), got %s: %s"
                                % (cmd.get("line"), kind[7:], cmd.get("text", ""), got, msg))
            continue
        nskip += 1

    tpass, tfail, tfailures = wasttext.check(wast, spec, outdir, WARMC, sh,
                                             WARMC_TIMEOUT)
    failures += tfailures

    if verbose:
        for f in failures[:20]:
            print("    " + f)

    build = {lv_key(lv): build_file(stem, outdir, spec, lv, idx)
             for lv in levels}
    return {"pass": npass, "fail": nfail, "skip": nskip, "failures": failures,
            "text_pass": tpass, "text_fail": tfail, "build": build}

def cc_host(src, obj):

    return hostbuild.cc_host(src, obj, [os.path.join(ROOT, "test", "spec")])

def link(objs, out):
    return hostbuild.link(objs, out, BUILD)

def compile_module(wasm, obj, prefix, level):
    target = ["--target=macos"] if DARWIN \
        else ["--target=windows"] if hostbuild.WINDOWS else []
    return sh([WARMC] + level + target +
              ["--symbol-prefix=" + prefix, "-c", "-o", obj, wasm],
              timeout=WARMC_TIMEOUT)

def spectest_obj(level):
    return os.path.join(WORK, "spectest%s.o" % level_tag(level))

def driver_obj():
    return os.path.join(WORK, "driver.o")

def prebuild(levels):
    """Build the objects every file links against, once, before the pool.

    They were rebuilt per invocation rather than cached on disk because a
    stale one from before a descriptor change is read by the new runtime as
    garbage and the symptom is a fault deep inside instantiation; building
    them here keeps that property while paying for it once instead of once
    per worker.
    """
    os.makedirs(WORK, exist_ok=True)
    r = cc_host(os.path.join(ROOT, "test", "spec", "driver.c"), driver_obj())
    if r.returncode != 0:
        return "cannot build the driver: " + r.stderr.strip()[:120]

    wat = os.path.join(ROOT, "test", "spec", "spectest.wat")
    wasm = os.path.join(WORK, "spectest.wasm")
    r = sh(["wat2wasm"] + WABT_FLAGS + [wat, "-o", wasm],
           timeout=RUN_TIMEOUT)
    if r.returncode != 0:
        return "spectest: " + r.stderr.strip()[:80]
    for lv in levels:
        r = compile_module(wasm, spectest_obj(lv), "spectest", lv)
        if r.returncode != 0:
            return "spectest: " + r.stderr.strip()[:80]
    return None

def build_file(stem, outdir, spec, level, idx):
    """Compile one file's modules and command table for one level.

    Returns {"objs": [...], "ncmds": n} when the file has something to run and
    it all built, otherwise {"status": why} -- the file is then left out of the
    index and reported as it was before.
    """

    wanted = []
    for cmd in spec["commands"]:
        if cmd.get("type") in ("module", "module_definition",
                               "assert_uninstantiable", "assert_unlinkable"):
            fn = cmd.get("filename")
            if fn and cmd.get("module_type", "binary") == "binary" and fn not in wanted:
                wanted.append(fn)
    if not wanted:
        return {"status": "nothing to run"}

    objdir = os.path.join(outdir, level_tag(level))
    os.makedirs(objdir, exist_ok=True)

    objs = []
    module_files = []
    for i, fn in enumerate(wanted):
        obj = os.path.join(objdir, "m%d.o" % (i + 1))

        r = compile_module(os.path.join(outdir, fn), obj,
                           "f%dm%d" % (idx, i + 1), level)
        if r.returncode != 0:
            msg = (r.stderr.strip().splitlines() or [""])[0]
            return {"status": "cannot compile %s: %s" % (fn, msg[:70])}
        objs.append(obj)
        module_files.append((i + 1, fn))

    try:
        src, ncmds, skipped = gen.generate(spec, module_files, stem, idx)
    except Exception as e:
        return {"status": "cannot generate: %s" % e}
    if not ncmds:
        return {"status": "nothing to run"}

    cfile = os.path.join(objdir, "cmds.c")
    with open(cfile, "w") as f:
        f.write(src)
    cobj = os.path.join(objdir, "cmds.o")
    r = cc_host(cfile, cobj)
    if r.returncode != 0:
        return {"status": "cannot build the table: " + r.stderr.strip()[:120]}

    return {"objs": objs + [cobj], "ncmds": ncmds + 2, "skipped": len(skipped)}

def try_link(level, group, prog):
    """Link one set of files into `prog`.  group: [(idx, name, ncmds, objs)]."""
    if not group:
        return "no file built"
    src = gen.generate_index([(i, n, c) for i, n, c, _ in group])
    cfile = prog + ".index.c"
    with open(cfile, "w") as f:
        f.write(src)
    cobj = prog + ".index.o"
    r = cc_host(cfile, cobj)
    if r.returncode != 0:
        return "cannot build the index: " + r.stderr.strip()[:200]
    objs = [o for _, _, _, group_objs in group for o in group_objs]
    r = link([driver_obj(), cobj, spectest_obj(level)] + objs, prog)
    if r.returncode != 0:
        return "cannot link: " + r.stderr.strip()[:200]
    return None

def link_level(level, group):
    """Link every file that built at this level into one executable.

    Some modules do not link -- an element segment naming an imported function
    emits a relocation against a symbol the object never defines, and
    `start.wast` has been recorded as failing that way for as long as
    expected.json has existed.  One executable per file made that one file's
    problem; one executable for the corpus makes it everybody's, so a failed
    link is bisected down to the files responsible and the rest are linked
    without them.  Rare, and the search is logarithmic when it does happen.

    Returns (path, {wast name: why it was left out}).
    """
    prog = os.path.join(WORK, "spec%s" % level_tag(level))
    err = try_link(level, group, prog)
    if err is None:
        return prog, {}

    good, bad = _bisect_link(level, group, prog + ".try")
    if not good:
        return None, {n: err for _, n, _, _ in group}
    if try_link(level, good, prog) is not None:
        return None, {n: err for _, n, _, _ in group}
    return prog, bad

def _bisect_link(level, group, scratch):
    """Split until the files that will not link are alone.  (kept, {name: why})"""

    if not group:
        return [], {}
    err = try_link(level, group, scratch)
    if err is None:
        return group, {}
    if len(group) == 1:
        return [], {group[0][1]: err}
    mid = len(group) // 2
    lo, elo = _bisect_link(level, group[:mid], scratch)
    hi, ehi = _bisect_link(level, group[mid:], scratch)
    elo.update(ehi)
    merged = lo + hi

    if merged and try_link(level, merged, scratch) is not None:
        kept = []
        for item in merged:
            if try_link(level, kept + [item], scratch) is None:
                kept.append(item)
            else:
                elo[item[1]] = "cannot link alongside the rest of the corpus"
        merged = kept
    return merged, elo

def execute_file(prog, wast_name):
    """Run one file out of the shared executable.

    Its own process, so a module that faults ends its own run and every other
    file still reports.  The image was validated by the
    first of these invocations, so the rest cost a few milliseconds.
    """
    r = sh([prog, wast_name], timeout=RUN_TIMEOUT)
    npass = nfail = 0
    for ln in r.stderr.splitlines():
        if ln.startswith("spec-run: ") and " passed," in ln:
            parts = ln.split()
            npass, nfail = int(parts[2]), int(parts[4])
    if " passed," not in r.stderr:
        note = (r.stderr.strip().splitlines() or ["no output"])[-1][:100]
        return {"status": "crashed: " + note, "pass": 0, "fail": 1,
                "failures": r.stderr.strip().splitlines()[:5]}
    return {"status": "ok", "pass": npass, "fail": nfail,
            "failures": [l for l in r.stderr.splitlines() if l.startswith("FAIL")][:5]}

def _worker(args):
    wast, idx, levels, verbose = args
    try:
        return os.path.basename(wast), run_file(wast, idx, levels, verbose)
    except Exception as e:
        return os.path.basename(wast), {
            "pass": 0, "fail": 0, "skip": 0, "failures": ["harness error: %s" % e],
            "build": {lv_key(lv): {"status": "harness error: %s" % e}
                      for lv in levels}}

def _init_worker(version):
    global CONVERTER_VERSION
    CONVERTER_VERSION = version

def main():
    args = sys.argv[1:]
    verbose = "-v" in args
    if verbose:
        args.remove("-v")
    update = "--update-expected" in args
    if update:
        args.remove("--update-expected")

    by_proposal = "--by-proposal" in args
    if by_proposal:
        args.remove("--by-proposal")

    jobs = 0
    levels = []
    rest = []
    for a in args:
        if a.startswith("--jobs="):
            jobs = int(a[7:])
        elif a.startswith("--levels="):

            levels = [lv.split() for lv in a[9:].split(",") if lv.strip()]
        else:
            rest.append(a)
    if not levels:
        levels = [os.environ.get("WARMC_FLAGS", "-O1").split()]
    if not jobs:
        jobs = int(os.environ.get("SPEC_JOBS", 0)) or (os.cpu_count() or 4)
    pattern = rest[0] if rest else None

    if not os.path.isdir(SUITE):
        print("spec: testsuite not found at %s (run `make testsuite`)" % SUITE)
        return 2
    # the checkout `make testsuite` leaves names its commit in a stamp; one of
    # an older pin would be compared with counts recorded from this one
    if "WASM_TESTSUITE" not in os.environ and not os.path.exists(
            os.path.join(SUITE, ".commit-" + TESTSUITE_COMMIT)):
        print("spec: the testsuite at %s is not the pinned %s (run `make testsuite`)"
              % (SUITE, TESTSUITE_COMMIT[:7]))
        return 2
    if not os.path.exists(WASM_TOOLS):
        print("spec: %s not found (run `make wasm-tools`)" % WASM_TOOLS)
        return 2

    expected = {}
    if os.path.exists(EXPECTED):
        with open(EXPECTED) as f:
            expected = json.load(f)

    files = sorted(f for f in os.listdir(SUITE) if f.endswith(".wast"))
    if pattern:
        files = [f for f in files if pattern in f]

    global CONVERTER_VERSION
    CONVERTER_VERSION = _tool_version(WASM_TOOLS)
    if WASM_TOOLS_VERSION not in CONVERTER_VERSION:

        print("spec: expected wasm-tools %s, found %r (run `make wasm-tools`)"
              % (WASM_TOOLS_VERSION, CONVERTER_VERSION))
        return 2
    err = prebuild(levels)
    if err:
        print("spec: " + err)
        return 2

    index_of = {n: i for i, n in enumerate(files)}

    tasks = sorted(((os.path.join(SUITE, n), index_of[n], levels, verbose)
                    for n in files),
                   key=lambda t: -os.path.getsize(t[0]))
    results = {}
    jobs = max(1, min(jobs, len(tasks) or 1))
    if jobs == 1:
        _init_worker(CONVERTER_VERSION)
        for t in tasks:
            n, r = _worker(t)
            results[n] = r
    else:
        with concurrent.futures.ProcessPoolExecutor(
                max_workers=jobs, initializer=_init_worker,
                initargs=(CONVERTER_VERSION,)) as pool:
            for n, r in pool.map(_worker, tasks, chunksize=1):
                results[n] = r

    for lv in levels:
        key = lv_key(lv)
        group = [(index_of[n], n, results[n]["build"][key]["ncmds"],
                  results[n]["build"][key]["objs"])
                 for n in files if "objs" in results[n].get("build", {}).get(key, {})]
        prog, dropped = link_level(lv, group)
        for name in files:
            b = results[name].get("build", {}).get(key, {})
            results[name].setdefault("run", {})
            if "objs" not in b:
                results[name]["run"][key] = {"status": b.get("status", "not built"),
                                             "pass": 0, "fail": 0}
            elif prog is None or name in dropped:
                results[name]["run"][key] = {
                    "status": dropped.get(name, "cannot link"), "pass": 0, "fail": 0}
            else:
                results[name]["run"][key] = execute_file(prog, name)

    tot_pass = tot_fail = tot_skip = 0
    tot_tpass = tot_tfail = 0
    per_level = {level_tag(lv): {"pass": 0, "fail": 0, "runnable": 0} for lv in levels}
    mismatches = []

    for name in files:
        r = results[name]
        tot_pass += r.get("pass", 0)
        tot_fail += r.get("fail", 0)
        tot_skip += r.get("skip", 0)
        tot_tpass += r.get("text_pass", 0)
        tot_tfail += r.get("text_fail", 0)
        want = expected.get(name, {})
        got_fail = r.get("fail", 0)
        for lv in levels:
            run = r.get("run", {}).get(lv_key(lv)) or {}
            acc = per_level[level_tag(lv)]
            acc["pass"] += run.get("pass", 0)
            acc["fail"] += run.get("fail", 0)
            if run.get("status") == "ok":
                acc["runnable"] += 1

            if not update and (got_fail != want.get("fail", 0) or
                               run.get("fail", 0) != want.get("run_fail", 0) or
                               r.get("pass", 0) != want.get("pass", 0) or
                               run.get("pass", 0) != want.get("run_pass", 0) or
                               r.get("text_pass", 0) != want.get("text_pass", 0) or
                               r.get("text_fail", 0) != want.get("text_fail", 0)):
                mismatches.append((name, " ".join(lv), want.get("fail", 0), got_fail,
                                   want.get("run_fail", 0), run.get("fail", 0),
                                   ["validation %d -> %d, execution %d -> %d (%s), "
                                    "text %d/%d -> %d/%d"
                                    % (want.get("pass", 0), r.get("pass", 0),
                                       want.get("run_pass", 0), run.get("pass", 0),
                                       run.get("status", "-"),
                                       want.get("text_pass", 0), want.get("text_fail", 0),
                                       r.get("text_pass", 0), r.get("text_fail", 0))] +
                                   r.get("failures", []) + run.get("failures", [])))
        if verbose:
            first = r.get("run", {}).get(lv_key(levels[0]), {})
            print("%-34s validate: %d/%d   run: %s %d/%d"
                  % (name, r.get("pass", 0), r.get("pass", 0) + got_fail,
                     first.get("status", "-"), first.get("pass", 0),
                     first.get("pass", 0) + first.get("fail", 0)))

    if update:
        first = lv_key(levels[0])
        out = {n: {"fail": r.get("fail", 0), "pass": r.get("pass", 0),
                   "text_fail": r.get("text_fail", 0),
                   "text_pass": r.get("text_pass", 0),
                   "run_fail": r.get("run", {}).get(first, {}).get("fail", 0),
                   "run_pass": r.get("run", {}).get(first, {}).get("pass", 0),
                   "run": r.get("run", {}).get(first, {}).get("status", "-")}
               for n, r in results.items()}
        with open(EXPECTED, "w") as f:
            json.dump(out, f, indent=1, sort_keys=True)
            f.write("\n")
        print("spec: recorded %d files" % len(out))

    if by_proposal:
        write_proposal_table(files, results, levels)
        return 0

    print("spec: %d files; validation %d passed %d failed" % (len(files), tot_pass, tot_fail))
    print("spec: text modules assembled by warmc %d as wasm-tools does (%d where "
          "wasm-tools is wrong), %d not"
          % (tot_tpass, sum(1 for n, _l in wasttext.ORACLE_WRONG if n in files),
             tot_tfail))
    for lv in levels:
        acc = per_level[level_tag(lv)]

        ran = acc["pass"] + acc["fail"] - 2 * acc["runnable"]
        print("spec: %-4s execution %d passed %d failed across %d runnable files "
              "(%d assertions in files that do not build yet)"
              % (" ".join(lv), acc["pass"], acc["fail"], acc["runnable"],
                 max(0, tot_skip - ran)))

    if mismatches:
        print("spec: %d file/level pair(s) differ from test/spec/expected.json:"
              % len(mismatches))
        for name, lv, want, got, wrun, grun, fails in mismatches:
            print("  %-28s %-4s validation %d->%d  execution %d->%d"
                  % (name, lv, want, got, wrun, grun))
            for f in fails[:5]:
                print("      " + f)
        return 1
    return 0

def write_proposal_table(files, results, levels):
    """Print the conformance table as markdown, one row per proposal.

    Generated rather than written by hand because that kind of table is wrong
    within a week otherwise, and because the numbers are ones the suite has
    already computed.  test/spec/proposals.py says how a file is assigned to a
    proposal, and where that assignment is a judgement rather than a fact.
    """
    groups = proposals.classify_all(files)
    key = lv_key(levels[0])

    print("| proposal | since | files | validation | text | execution | "
          "validation-only | cannot compile |")
    print("|---|---|---:|---:|---:|---:|---:|---:|")
    for label, ver, _m in proposals.ORDER:
        names = groups[label]
        if not names:
            continue
        vp = vf = tp = tf = xp = xf = built = 0

        vonly = badbuild = 0
        for n in names:
            r = results[n]
            vp += r.get("pass", 0)
            vf += r.get("fail", 0)
            tp += r.get("text_pass", 0)
            tf += r.get("text_fail", 0)
            run = r.get("run", {}).get(key) or {}
            xp += run.get("pass", 0)
            xf += run.get("fail", 0)
            status = run.get("status")
            if status == "ok":
                built += 1
            elif status == "nothing to run":
                vonly += 1
            else:
                badbuild += 1

        xp -= 2 * built
        v = "%d" % vp if not vf else "%d, **%d fail**" % (vp, vf)
        t = "%d" % tp if not tf else "%d, **%d fail**" % (tp, tf)
        x = "%d" % xp if not xf else "%d, **%d fail**" % (xp, xf)
        print("| %s | %s | %d | %s | %s | %s | %d | %s |"
              % (label, ver, len(names), v, t, x, vonly,
                 "%d" % badbuild if not badbuild else "**%d**" % badbuild))
    print()
    print("`text` counts the modules a file writes as text that warmc assembles "
          "as wasm-tools")
    print("does, byte for byte (test/spec/wasttext.py).")
    print()
    print("`validation-only` files hold nothing but `assert_malformed` and "
          "`assert_invalid`,")
    print("so they have no execution phase by design.  `cannot compile` is "
          "the column that")
    print("counts against conformance.")
    print()
    print("Execution counts are corpus assertions only: each runnable file "
          "also runs two")
    print("commands the harness prepends -- bring up `spectest`, register it "
          "-- and those are")
    print("excluded here, so these numbers are 2 x (runnable files) below the "
          "`make check-spec`")
    print("summary line, which counts them.")

def lv_key(level):
    """Levels are lists; they index the per-file result dict as strings."""
    return " ".join(level)

if __name__ == "__main__":
    sys.exit(main())
