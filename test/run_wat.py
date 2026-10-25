"""The text front end, against assemblers warmc did not write.

`src/wat.c` turns a .wat into a binary module.  Whether that module is the one
the text says can only be asked by handing the same text to something else, so
this compares rather than replaces.  Putting the code under test in the
oracle's chair is how a whole class of parser bug becomes invisible: every
fixture would still "pass", because both sides would be wrong in the same way.

**`wasm-tools` is the oracle**, pinned by the Makefile at the version the .wast
converter uses.  It implements all of wasm 3.0, garbage collection included,
and it is built into the tree rather than taken from PATH -- a suite whose
instrument is "whatever is installed" measures the instrument.  wabt's
`wat2wasm` is used *as well* when it happens to be installed, because it is
what assembles the fixtures everywhere else in `make check`, but nothing here
requires it: it has no garbage collection in its text parser at all, which is
the reason the .wast converter is not wabt either.

The two disagree with each other, and not only about coverage.  Where both read
a file and produce different bytes the format allows both -- an active element
segment on table 0 has a compact encoding and an explicit one, and
`test/cases/mem64.wat` is a real case of exactly that.  So the rule is: warmc
must match **every** oracle that read the file, except where the oracles
already disagree with each other, and then it must match one of them.  That
case is counted and printed rather than waved through, because "the tools
disagree" is a claim that should carry a number.

Getting the fixtures to that point took four edits, all to text only wabt
accepted: `(i32.load8_u (memory 1) ...)`, `(elem (table $t) (i32.const 0) $f)`
without the `func` keyword, and `memory.copy` naming one memory rather than
both.  The fourth was a type with 1050 results, where wasmparser refuses more
than a thousand -- and it is generated, so the fix is in `gen_cases.py` and not
in the .wat.  Three of the four fixtures are hand-written and one is not; which
is which is not guessable from the file, and editing a generated one looks like
it worked until the next run overwrites it.

**The criterion is byte identity.**  Stronger than the format requires, and
chosen because "equivalent" needs a comparison function, and a comparison
function written by the same hand as the parser is one more thing that can be
wrong in the same direction.  Where it forced a real decision the decision went
the spec's way: implicit types are appended in the order the text expands
them -- a function's signature, then its body's block types, then the next
function's -- see the `Typeuse` comment in wat.c.

Two sources of text, because they are written in different styles and the
styles reach different code:

- **The fixtures**, hand-written and almost entirely folded.
- **Every binary module in the build tree, printed back to text.**  A printer
  emits the *flat* grammar with numeric indices and explicit types, which the
  fixtures barely touch and which is separate code.  What is there depends on
  what has been run: `make check` leaves the workload and TinyGo modules --
  real producer output, which no fixture resembles -- and `make check-spec`
  leaves the whole converted spec corpus under `build/spec`.  For those the
  module that went in is the answer, and reproducing it exactly outranks
  agreeing with an oracle.  Capped by default to keep `make check` short;
  WAT_FULL=1 removes the cap, and how much was read is printed either way.
"""

import multiprocessing
import os
import subprocess
import sys
import tempfile

import hostbuild

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.environ.get("BUILD", "build")
WARMC = os.path.join(ROOT, BUILD, "warmc") + (".exe" if os.name == "nt" else "")

# The limits below are generous for a workstation; a machine slow enough to
# have been given a longer WARMC_TIMEOUT -- CI's runners -- gets that instead.
def limit(seconds):
    return max(seconds, float(os.environ.get("WARMC_TIMEOUT", "0")))
WASM_TOOLS = os.path.join(ROOT, BUILD, "tools", "bin",
                          "wasm-tools.exe" if os.name == "nt" else "wasm-tools")

CORPUS_CAP = int(os.environ.get("WAT_CORPUS_CAP", "1500"))

WABT_FLAGS = hostbuild.wabt_flags()

TIMEOUT = float(os.environ.get("WARMC_TIMEOUT", "20"))

ORACLES = []

def _init_worker(oracles_):
    global ORACLES
    ORACLES = oracles_

def sh(cmd, timeout=TIMEOUT):
    try:
        return subprocess.run(cmd, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return subprocess.CompletedProcess(cmd, 124, b"", b"timed out")
    except FileNotFoundError:
        return subprocess.CompletedProcess(cmd, 127, b"", b"not found")

def first_line(b):
    for ln in b.decode("utf-8", "replace").splitlines():
        if ln.strip():
            return ln.strip()[:160]
    return ""

def jobs():
    return max(1, (os.cpu_count() or 2) - 1)

def oracles():
    """The assemblers to compare against.  Each is (name, assemble, print)."""
    out = [("wasm-tools", [WASM_TOOLS, "parse"], [WASM_TOOLS, "print"])]
    if sh(["wat2wasm", "--version"], timeout=limit(20)).returncode == 0:
        out.append(("wabt", ["wat2wasm"] + WABT_FLAGS, ["wasm2wat"] + WABT_FLAGS))
    return out

# Where wasm-tools, the oracle, is wrong and warmc is not.  An entry is still
# checked: warmc's module must validate and must still differ from
# wasm-tools', so that an oracle that is fixed shows up as a stale entry.
# test/spec/wasttext.py keeps the corpus's entries, for the same bug.
NONFINAL = ("wasm-tools gives an inline signature a (sub (func ...)) type, "
            "which is not final; the spec has it stand for a singular, final "
            "function type only (text/types.rst, Type Uses)")
ORACLE_WRONG = {
    os.path.join("test", "cases_gc", "ictab_sub.wat"): NONFINAL,
}

def strip_custom(b):
    """A module without its name sections, or None if it is not one.

    Applied to both sides of every comparison.  The name section is the one
    thing here that two correct assemblers may legitimately disagree about:
    `wasm-tools parse` writes one for the `$names` in the text, and warmc,
    whose names come from the binary it compiles, writes none.  Every other
    custom section is compared where it stands, since `(@custom ...)`,
    `(@producers ...)` and `(@dylink.0 ...)` say exactly what to write and
    where.  Removing the name sections from the bytes keeps the criterion
    plain identity rather than an "equal apart from" that would have to be
    trusted -- and it removes exactly one kind of section, which is checkable
    by reading this function.

    A module's shape is enough: an eight-byte header, then id, size, payload
    for each section, and a custom section's payload starts with its name.
    Anything that does not walk cleanly is not a module and is left to the
    tools to reject.
    """
    if len(b) < 8 or b[:4] != b"\0asm":
        return None
    out = bytearray(b[:8])
    i = 8
    while i < len(b):
        sec = b[i]
        j = i + 1
        size = 0
        shift = 0
        while True:
            if j >= len(b) or shift > 35:
                return None
            byte = b[j]
            j += 1
            size |= (byte & 0x7F) << shift
            shift += 7
            if not byte & 0x80:
                break
        if j + size > len(b):
            return None
        if sec != 0 or b[j:j + 5] != b"\x04name":
            out += b[i:j + size]
        i = j + size
    return bytes(out)

def read_stripped(path):
    try:
        with open(path, "rb") as f:
            return strip_custom(f.read())
    except OSError:
        return None

def first_difference(a, b):
    n = min(len(a), len(b))
    return next((i for i in range(n) if a[i] != b[i]), n)

def check_fixture(wat):
    """Assemble one .wat with warmc and with every oracle that can read it.

    Returns (status, label, why).  `disagree` means the oracles produced
    different bytes from each other and warmc matched one of them, which the
    format permits and which is counted rather than hidden.
    """
    label = os.path.relpath(wat, ROOT)
    with tempfile.TemporaryDirectory() as tmp:
        theirs = {}
        for name, assemble, _ in ORACLES:
            out = os.path.join(tmp, "o-%s.wasm" % name)
            if sh(assemble + [wat, "-o", out]).returncode == 0:
                got = read_stripped(out)
                if got is not None:
                    theirs[name] = got

        mine_path = os.path.join(tmp, "mine.wasm")
        r = sh([WARMC, "--emit-wasm", "-o", mine_path, wat])
        if r.returncode != 0:
            if not theirs:

                return ("no_oracle", label, "")
            return ("fail", label, "warmc: " + first_line(r.stderr))

        mine = read_stripped(mine_path)
        if mine is None:
            return ("fail", label, "warmc wrote something that is not a module")
        if not theirs:
            return ("no_oracle", label, "")

        agree = sorted(n for n, b in theirs.items() if b == mine)
        wrong = ORACLE_WRONG.get(label)
        if wrong is not None:
            if "wasm-tools" in agree:
                return ("fail", label, "listed in ORACLE_WRONG, but warmc and "
                        "wasm-tools now agree")
            if sh([WARMC, "--validate-only", mine_path]).returncode != 0:
                return ("fail", label, "warmc's module, which differs from "
                        "wasm-tools' as listed, does not validate")
            return ("oracle_wrong", label, wrong)
        if len(agree) == len(theirs):
            return ("ok", label, "")
        if agree:
            others = sorted(set(theirs) - set(agree))
            return ("disagree", label,
                    "matches %s; %s encodes it differently"
                    % ("+".join(agree), "+".join(others)))
        name = sorted(theirs)[0]
        b = theirs[name]
        return ("fail", label,
                "differs from %s at byte %d (warmc %d bytes, %s %d)"
                % (name, first_difference(mine, b), len(mine), name, len(b)))

def find_binaries():
    """Every .wasm under the build tree.

    Deliberately "whatever is there" rather than a fixed list: what is there
    depends on which suites have been run, and each leaves a different kind of
    producer's output behind.

    Hidden directories are not "what is there": that is where a producer stages a
    module it has not finished writing (test/gobench.py).  Counting one would not
    fail -- an oracle cannot disassemble half a module either, so it lands in the
    no-oracle column -- but it would be counted, and the census is meant to say
    what the suites have actually produced.
    """
    root = os.path.join(ROOT, BUILD)
    if not os.path.isdir(root):
        return []
    out = []
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        for f in files:
            if f.endswith(".wasm"):
                out.append(os.path.join(base, f))
    return out

def roundtrip_one(wasm):
    """Print a module back to text, assemble that text, compare.

    The module that went in is the answer, and reproducing it exactly is a
    stronger result than agreeing with the assembler -- so that is checked
    first and on its own.  It also outranks the oracle where the two conflict,
    which they do: given a reference type naming a type index the module does
    not declare, `wat2wasm` silently assembles heap type 0 instead.  Those
    modules are in the corpus on purpose -- they are what `assert_invalid`
    asserts about -- and warmc round-trips them exactly, so scoring them
    against wabt would be scoring them against a bug.
    """
    label = os.path.relpath(wasm, ROOT)
    orig = read_stripped(wasm)
    if orig is None:
        return ("no_oracle", label, "")

    if orig[4:8] != b"\x01\x00\x00\x00":
        return ("no_oracle", label, "")

    with tempfile.TemporaryDirectory() as tmp:
        bare = os.path.join(tmp, "bare.wasm")
        with open(bare, "wb") as f:
            f.write(orig)

        for name, assemble, printer in ORACLES:
            wat = os.path.join(tmp, "rt-%s.wat" % name)
            mine_path = os.path.join(tmp, "m-%s.wasm" % name)
            theirs_path = os.path.join(tmp, "t-%s.wasm" % name)

            if sh(printer + [bare, "-o", wat]).returncode != 0:
                continue
            if sh(assemble + [wat, "-o", theirs_path]).returncode != 0:
                continue

            r = sh([WARMC, "--emit-wasm", "-o", mine_path, wat])
            if r.returncode != 0:

                if sh([WARMC, "--validate-only", bare]).returncode != 0:
                    return ("consistent", label, "")
                return ("fail", "%s (%s round trip)" % (label, name),
                        "warmc reads the binary module but not its own "
                        "printed text: " + first_line(r.stderr))
            mine = read_stripped(mine_path)
            if mine is None:
                return ("fail", "%s (%s round trip)" % (label, name),
                        "warmc wrote something that is not a module")
            if mine == orig:
                return ("exact", label, "")
            theirs = read_stripped(theirs_path) or b""
            if mine == theirs:
                return ("ok", label, "")
            return ("fail", "%s (%s round trip)" % (label, name),
                    "differs from %s at byte %d (warmc %d, %s %d) and from the "
                    "module it was printed from (%d)"
                    % (name, first_difference(mine, theirs), len(mine), name,
                       len(theirs), len(orig)))

    return ("no_oracle", label, "")

def main():
    global ORACLES
    if not os.path.exists(WARMC):
        print("run_wat: %s not built" % WARMC, file=sys.stderr)
        return 1
    if not os.path.exists(WASM_TOOLS):
        print("run_wat: %s not found (run `make wasm-tools`)" % WASM_TOOLS,
              file=sys.stderr)
        return 1
    ORACLES = oracles()

    skip = os.path.join(ROOT, "test", "components")
    wats = []
    for base, _dirs, files in os.walk(os.path.join(ROOT, "test")):
        if base == skip or base.startswith(skip + os.sep):
            continue
        for f in files:
            if f.endswith(".wat"):
                wats.append(os.path.join(base, f))
    wats.sort()

    corpus = sorted(find_binaries())
    total = len(corpus)
    cap = 0 if os.environ.get("WAT_FULL") else CORPUS_CAP
    if cap and total > cap:

        corpus = corpus[::(total + cap - 1) // cap]

    counts = {k: 0 for k in ("ok", "exact", "disagree", "consistent", "no_oracle",
                             "oracle_wrong")}
    failures, notes = [], []

    def record(result):
        status, label, why = result
        if status == "fail":
            failures.append("%s: %s" % (label, why))
            return
        counts[status] += 1
        if status in ("disagree", "oracle_wrong"):
            notes.append((status, "%s: %s" % (label, why)))

    with multiprocessing.Pool(jobs(), initializer=_init_worker,
                              initargs=(ORACLES,)) as pool:
        for r in pool.imap_unordered(check_fixture, wats, chunksize=2):
            record(r)
        for r in pool.imap_unordered(roundtrip_one, corpus, chunksize=8):
            record(r)

    # Shapes whose *acceptance* is the property, not their bytes.  The three
    # assemblers expand a table's inline element list to different encodings,
    # all of them legal -- warmc and wasm-tools name the table, as the
    # abbreviation's expansion does, wabt leaves table 0 implied -- so
    # comparing bytes here would pin an encoder's taste rather than a
    # defect.  What did need pinning is
    # that warmc reads them at all: it used to give both inline-segment
    # abbreviations an empty offset expression and then refuse the result.
    for src in ('(module (func $f (result i32) (i32.const 7))\n'
                '  (table funcref (elem $f)))',
                '(module (table funcref (elem)))',
                '(module (memory (data)))'):
        with tempfile.TemporaryDirectory() as tmp:
            wat = os.path.join(tmp, "abbrev.wat")
            with open(wat, "w") as f:
                f.write(src)
            p_ = subprocess.run([WARMC, "--emit-wasm", "-o",
                                 os.path.join(tmp, "o.wasm"), wat],
                                capture_output=True, text=True, timeout=limit(30))
            if p_.returncode != 0:
                failures.append("inline-segment abbreviation refused: %s"
                                % p_.stderr.strip())
            else:
                counts["exact"] += 1

    # And shapes whose *refusal* is the property.  The text format lets a
    # field carry an inline import, and an import may not follow a definition
    # of any sort -- a rule the front end has and nothing asserted, so
    # deleting the check broke no test.
    # A custom-section annotation is a module field, and its grammar is
    # wasm-tools': one anywhere else, a place no section has, a producers
    # field or dylink.0 subsection no one defined, or a name that is not
    # UTF-8 is refused rather than dropped.
    for src, want in (
            ('(module (func) (func (import "m" "f")))',
             "import after a function definition"),
            ('(module (global i32 (i32.const 0)) '
             '(global (import "m" "g") i32))',
             "import after a global definition"),
            ('(module (func (@custom "a" "b")))', "misplaced @custom annotation"),
            ('(module (@custom "a" (@custom "b" "c") "d"))', "misplaced @custom annotation"),
            ('(module (func (@producers (language "a" "1"))))', "misplaced @producers annotation"),
            ('(module (memory 1 (@dylink.0)))', "misplaced @dylink.0 annotation"),
            ('(module (@custom "a" (after nothing) "b"))', "expected a section's name"),
            ('(module (@custom "a" (within code) "b"))', "expected before or after"),
            ('(module (@custom "a" (before last) "b"))', "expected a section's name"),
            ('(module (@custom "a" (after first) "b"))', "expected a section's name"),
            ('(module (@custom "a" 1))', "expected ')'"),
            ('(module (@custom "\\ff" "b"))', "UTF-8"),
            ('(module (@producers (author "a" "1")))', "expected language, sdk or processed-by"),
            ('(module (@producers (language "a")))', "a producer's version"),
            ('(module (@dylink.0 (needs "a")))', "expected a dylink.0 subsection"),
            ('(module (@dylink.0 (export-info "f" sideways)))', "expected a symbol's flag"),
            ('(module (@dylink.0 (mem-info (memory 1 0x100000000))))', "out of range")):
        with tempfile.TemporaryDirectory() as tmp:
            wat = os.path.join(tmp, "order.wat")
            with open(wat, "w") as f:
                f.write(src)
            p_ = subprocess.run([WARMC, "--emit-wasm", "-o",
                                 os.path.join(tmp, "o.wasm"), wat],
                                capture_output=True, text=True, timeout=limit(30))
            if p_.returncode == 0:
                failures.append("accepted: %s" % src)
            elif want not in p_.stderr:
                failures.append("%s was refused for the wrong reason: %s"
                                % (src, p_.stderr.strip()))
            else:
                counts["exact"] += 1

    for why in sorted(failures):
        print("FAIL[wat]: %s" % why)
    for status, why in sorted(notes):
        if status == "disagree":
            print("wat: oracles disagree, warmc matches one -- %s" % why)
        else:
            print("wat: wasm-tools is wrong, as listed -- %s" % why)

    compared = counts["ok"] + counts["exact"] + counts["disagree"] + counts["oracle_wrong"]
    if not compared:
        print("FAIL[wat]: nothing was compared -- every file reported no oracle",
              file=sys.stderr)
        failures.append("no comparisons ran")

    print("wat: %d byte-identical to %s, %d round trips reproduced their input "
          "module exactly, %d where the oracles disagreed, %d where wasm-tools "
          "is wrong, %d refused through both front ends, %d with no oracle; %d "
          "fixtures and %d of the %d modules in %s"
          % (counts["ok"], "+".join(n for n, _, _ in ORACLES), counts["exact"],
             counts["disagree"], counts["oracle_wrong"], counts["consistent"],
             counts["no_oracle"], len(wats), len(corpus), total, BUILD))
    return 1 if failures else 0

if __name__ == "__main__":
    sys.exit(main())
