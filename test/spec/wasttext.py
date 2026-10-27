"""The corpus's text modules, through warmc's own text front end.

`wasm-tools json-from-wast` hands the harness a binary for every module a
.wast writes as text, and that binary is what the rest of the harness
validates and runs.  So the text itself -- some five thousand modules, the
best-specified text-format corpus there is -- never reached `src/wat.c`.  This
puts it there: each module is cut out of the .wast, assembled by
`warmc --emit-wasm`, and must come out as wasm-tools' binary did, byte for
byte once the name sections are gone (`run_wat.strip_custom` says why those
go).  Identity is the criterion for the reason run_wat.py gives: anything
weaker needs a comparison function, which can be wrong in the same direction
as the assembler.  And identity with the module the harness then runs means
that every assertion it checks of wasm-tools' binary holds of warmc's too.

Finding the text takes a reader for the .wast's own syntax, which is small:
parentheses, strings, comments, words.  A module is a top-level `(module
...)`, or the first `(module ...)` inside an `assert_*`, or -- with no script
command anywhere -- the whole file.  These are matched to the converter's
commands by order and checked by line, and a file where the two do not line
up is a failure, not a skip.
"""
import os

import run_wat

# Modules on which the oracle is wrong and warmc is not, as run_wat.py keeps
# its fixtures'.  Each is still checked: warmc's binary must get the verdict
# the command wants, and must still differ from wasm-tools' -- an entry the
# two have come to agree on is reported, so that a converter that is fixed
# shows up as a stale entry here rather than as nothing.
ORACLE_WRONG = {
    ("type-subtyping.wast", 344): run_wat.NONFINAL,
    ("type-subtyping.wast", 373): run_wat.NONFINAL,
}

SCRIPT_HEADS = {"module", "register", "invoke", "get", "script", "input",
                "output", "thread", "wait", "either"}

def _tokens(src):
    """(kind, start, end, line) for '(', ')', strings and words; space and
    comments are skipped."""
    out = []
    i, n, line = 0, len(src), 1
    while i < n:
        c = src[i]
        if c == "\n":
            line += 1
            i += 1
        elif c in " \t\r":
            i += 1
        elif src.startswith(";;", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
        elif src.startswith("(;", i):
            depth = 0
            while i < n:
                if src.startswith("(;", i):
                    depth += 1
                    i += 2
                elif src.startswith(";)", i):
                    depth -= 1
                    i += 2
                    if not depth:
                        break
                else:
                    if src[i] == "\n":
                        line += 1
                    i += 1
        elif c in "()":
            out.append((c, i, i + 1, line))
            i += 1
        elif c == '"':
            j, l0 = i + 1, line
            while j < n and src[j] != '"':
                if src[j] == "\\":
                    j += 1
                elif src[j] == "\n":
                    line += 1
                j += 1
            out.append(("str", i, j + 1, l0))
            i = j + 1
        else:
            j = i
            while (j < n and src[j] not in ' \t\r\n()"' and
                   not src.startswith(";;", j) and not src.startswith("(;", j)):
                j += 1
            out.append(("word", i, j, line))
            i = j
    return out

class _Form:
    """One parenthesised form: its span, its line, the words directly in it
    (in order), and the forms directly in it."""
    def __init__(self, start, line):
        self.start, self.end, self.line = start, None, line
        self.words = []
        self.kids = []

def _forms(src):
    top, stack = [], []
    for kind, s, e, line in _tokens(src):
        if kind == "(":
            f = _Form(s, line)
            (stack[-1].kids if stack else top).append(f)
            stack.append(f)
        elif kind == ")":
            if not stack:
                raise ValueError("unbalanced ')' on line %d" % line)
            stack.pop().end = e
        elif kind == "word" and stack:
            stack[-1].words.append((src[s:e], s, e, line))
    if stack:
        raise ValueError("unclosed '(' from line %d" % stack[0].line)
    return top

def modules(src):
    """Every module of a .wast that the converter gives a file of its own, in
    order: (line, kind, text), kind one of text, binary, quote, and line that
    of the word `module`, as the converter has it.  For text,
    `text` is the module to assemble -- `definition`, a script word, cut."""
    top = _forms(src)
    if not any(f.words and (f.words[0][0] in SCRIPT_HEADS or
                            f.words[0][0].startswith("assert_")) for f in top):
        return [(1, "text", src)]
    out = []
    for f in top:
        if not f.words:
            continue
        head = f.words[0][0]
        if head == "module":
            m = f
        elif head.startswith("assert_"):
            m = next((k for k in f.kids if k.words and k.words[0][0] == "module"), None)
            if m is None:
                continue
        else:
            continue
        rest = [w for w in m.words[1:] if not w[0].startswith("$")]
        if rest and rest[0][0] == "instance":
            continue
        definition = rest[0] if rest and rest[0][0] == "definition" else None
        if definition:
            rest = rest[1:]
        if rest and rest[0][0] in ("binary", "quote"):
            out.append((m.words[0][3], rest[0][0], None))
            continue
        text = src[m.start:m.end]
        if definition:
            text = (src[m.start:definition[1]] + src[definition[2]:m.end])
        out.append((m.words[0][3], "text", text))
    return out

def check(wast, spec, outdir, warmc, sh, timeout):
    """Assemble each text module of one .wast with warmc and compare.

    Returns (passed, failed, failures).
    """
    name = os.path.basename(wast)
    # no newline translation either way: warmc gets the .wast's own bytes
    with open(wast, encoding="utf-8", newline="") as f:
        src = f.read()
    try:
        mods = modules(src)
    except ValueError as e:
        return 0, 1, ["text: cannot read the .wast: %s" % e]
    cmds = [c for c in spec["commands"] if "filename" in c]
    if len(cmds) != len(mods) or any(c["line"] != m[0] for c, m in zip(cmds, mods)):
        return 0, 1, ["text: the .wast's %d modules do not line up with the "
                      "converter's %d" % (len(mods), len(cmds))]
    npass = nfail = 0
    failures = []
    texts = []
    for (line, kind, text), cmd in zip(mods, cmds):
        if kind != "text":
            continue
        stem = os.path.splitext(cmd["filename"])[0]
        src_path = os.path.join(outdir, stem + ".text.wat")
        with open(src_path, "w", encoding="utf-8", newline="") as f:
            f.write(text)
        # what a run before wrote is not taken for what this one did
        if os.path.exists(src_path[:-len(".wat")] + ".wasm"):
            os.remove(src_path[:-len(".wat")] + ".wasm")
        texts.append((line, cmd, src_path))
    # one warmc assembles them all, each written beside itself
    srcs = [src for _, _, src in texts]
    said = run_wat.verdicts(sh, [warmc, "--emit-wasm"], srcs, timeout)
    for line, cmd, src_path in texts:
        mine_path = src_path[:-len(".wat")] + ".wasm"
        if src_path not in said:
            r = sh([warmc, "--emit-wasm", "-o", mine_path, src_path], timeout=timeout)
            said[src_path] = (r.returncode, (r.stderr.strip().splitlines() or [""])[0])
        rc, msg = said[src_path]
        if rc != 0:
            nfail += 1
            failures.append("text: line %d: warmc cannot assemble it: %s" % (line, msg))
            continue
        mine = run_wat.read_stripped(mine_path)
        theirs = run_wat.read_stripped(os.path.join(outdir, cmd["filename"]))
        wrong = ORACLE_WRONG.get((name, line))
        if wrong is None:
            if mine is not None and mine == theirs:
                npass += 1
            else:
                nfail += 1
                at = run_wat.first_difference(mine or b"", theirs or b"")
                failures.append("text: line %d: warmc's module differs from "
                                "wasm-tools' at byte %d" % (line, at))
            continue
        if mine == theirs:
            nfail += 1
            failures.append("text: line %d: listed in ORACLE_WRONG, but warmc "
                            "and wasm-tools now agree" % line)
            continue
        want = 3 if cmd["type"] == "assert_invalid" else 0
        r = sh([warmc, "--validate-only", mine_path], timeout=timeout)
        if r.returncode == want:
            npass += 1
        else:
            nfail += 1
            failures.append("text: line %d: warmc's module (%s) exits %d, want %d"
                            % (line, wrong[:40], r.returncode, want))
    return npass, nfail, failures
