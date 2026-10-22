"""Check the canonical ABI's layout and flattening against the specification's
own implementation.

The component model publishes a reference implementation of the canonical ABI
as executable Python -- `design/mvp/canonical-abi/definitions.py`.  Its
`alignment`, `elem_size` and `flatten_*` are the definition of what warmc's
src/canon.c has to compute, so this asks both the same questions and compares
the answers, rather than pinning numbers that only say the code has not
changed.

How the two are given the same type.  `warmc --dump-canon` prints, per type,
both its *structure* and what the rules make of it:

    canon 6 option<record{u32,string}> align=4 size=16 flat=i32,i32,i32,i32

so this parses the structure back and hands it to the reference.  That leaves
one gap worth naming: if warmc printed the structure wrongly the two sides
would agree about the wrong type.  The printing is a dozen lines and the
component suite's golden dumps already cover what was decoded; this covers
what is computed from it -- and the one part of the rendering that is checked
outright is `unknown`, which is what a type index warmc failed to follow now
prints as.  It used to print `own`, which is a real type with a size of its
own, so both sides agreed about it.

The corpus is every component in test/components, plus documents from
`wasm-tools wit-smith`, an independent generator of random valid WIT.

Nested scopes are resolved, which is where a real document keeps everything:
a component or instance type declares types in an index space of its own, and
`2.8` in the output means type 8 of the type at index 2.

Needs the reference.  It is a single file fetched into $BUILD/spec; without it
this skips rather than fails, since a checkout with no network is not a broken
tree.
"""

import os
import re
import subprocess
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.environ.get("BUILD", "build")
WARMC = os.path.join(ROOT, BUILD, "warmc") + (".exe" if os.name == "nt" else "")
WASM_TOOLS = os.path.join(ROOT, BUILD, "tools", "bin",
                          "wasm-tools.exe" if os.name == "nt" else "wasm-tools")
WORK = os.path.join(ROOT, BUILD, "canon")
SPEC_DIR = os.path.join(ROOT, BUILD, "spec")
SPEC = os.path.join(SPEC_DIR, "definitions.py")
SPEC_URL = ("https://raw.githubusercontent.com/WebAssembly/component-model/"
            "main/design/mvp/canonical-abi/definitions.py")

SMITH = int(os.environ.get("CANON_SMITH", "12"))

failures = 0

def fail(what):
    global failures
    failures += 1
    print("FAIL[canon]: %s" % what)

def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr

def ensure_spec():
    if os.path.exists(SPEC):
        return True
    os.makedirs(SPEC_DIR, exist_ok=True)
    try:
        with urllib.request.urlopen(SPEC_URL, timeout=20) as r:
            data = r.read()
        with open(SPEC, "wb") as f:
            f.write(data)
        return True
    except Exception as e:
        print("canon: skipping, could not fetch the reference (%s)" % e)
        return False

class P:
    """A recursive-descent reader for the structure --dump-canon prints."""

    def __init__(self, s):
        self.s = s
        self.i = 0

    def eat(self, tok):
        if self.s.startswith(tok, self.i):
            self.i += len(tok)
            return True
        return False

    def expect(self, tok):
        if not self.eat(tok):
            raise ValueError("expected %r at %d in %r" % (tok, self.i, self.s))

    def number(self):
        j = self.i
        while self.i < len(self.s) and self.s[self.i].isdigit():
            self.i += 1
        return int(self.s[j:self.i])

_RESOURCES = {}

def resource_type(d, i):
    if i not in _RESOURCES:
        _RESOURCES[i] = d.ResourceType(None, None)
    return _RESOURCES[i]

def handle_index(p):
    """The resource index inside `own<...>`, or None when none was printed."""
    if not p.eat("<"):
        return None
    n = p.number()
    p.expect(">")
    return n

def parse_type(p, d):
    PRIMS = {
        "bool": d.BoolType, "s8": d.S8Type, "u8": d.U8Type, "s16": d.S16Type,
        "u16": d.U16Type, "s32": d.S32Type, "u32": d.U32Type, "s64": d.S64Type,
        "u64": d.U64Type, "f32": d.F32Type, "f64": d.F64Type,
        "char": d.CharType, "string": d.StringType,
        "error-context": d.ErrorContextType,
    }

    for name in sorted(PRIMS, key=len, reverse=True):

        if p.s.startswith(name, p.i):
            nxt = p.i + len(name)
            if nxt >= len(p.s) or not (p.s[nxt].isalnum() or p.s[nxt] == '-'):
                p.i = nxt
                return PRIMS[name]()
    if p.eat("list<"):
        t = parse_type(p, d)
        if p.eat(","):
            n = p.number()
            p.expect(">")
            return d.ListType(t, n)
        p.expect(">")
        return d.ListType(t)
    if p.eat("record{"):
        fs = []
        if not p.eat("}"):
            while True:
                fs.append(d.FieldType(str(len(fs)), parse_type(p, d)))
                if p.eat("}"):
                    break
                p.expect(",")
        return d.RecordType(fs)
    if p.eat("tuple{"):
        ts = []
        if not p.eat("}"):
            while True:
                ts.append(parse_type(p, d))
                if p.eat("}"):
                    break
                p.expect(",")
        return d.TupleType(ts)
    if p.eat("variant{"):
        cs = []
        if not p.eat("}"):
            while True:
                t = None if p.eat("_") else parse_type(p, d)
                cs.append(d.CaseType(str(len(cs)), t))
                if p.eat("}"):
                    break
                p.expect(",")
        return d.VariantType(cs)
    if p.eat("option<"):
        t = parse_type(p, d)
        p.expect(">")
        return d.OptionType(t)
    if p.eat("result<"):
        ok = None if p.eat("_") else parse_type(p, d)
        p.expect(",")
        er = None if p.eat("_") else parse_type(p, d)
        p.expect(">")
        return d.ResultType(ok, er)
    if p.eat("flags"):
        return d.FlagsType([str(i) for i in range(p.number())])
    if p.eat("enum"):
        return d.EnumType([str(i) for i in range(p.number())])

    if p.eat("own"):
        return d.OwnType(resource_type(d, handle_index(p)))
    if p.eat("borrow"):
        return d.BorrowType(resource_type(d, handle_index(p)))
    if p.eat("stream"):
        return d.StreamType(None)
    if p.eat("future"):
        return d.FutureType(None)
    raise ValueError("cannot parse a type at %d in %r" % (p.i, p.s))

def parse_whole(s, d):
    p = P(s)
    t = parse_type(p, d)
    if p.i != len(s):
        raise ValueError("trailing %r in %r" % (s[p.i:], s))
    return t

class Opts:
    """What flatten_* reads out of the canonical options: the pointer width."""
    def __init__(self, ptr):
        self.memory = type("M", (), {"ptr_type": staticmethod(lambda: ptr)})()
        self.async_ = False
        self.callback = None

def ref_flat(d, t, ptr="i32"):
    return d.flatten_type(t, Opts(ptr))

LINE_T = re.compile(r"^canon ([\d.]+) (.*) align=(\d+) size=(\d+) flat=(.*)$")
LINE_F = re.compile(r"^canon-func ([\d.]+) \((.*)\)->\((.*)\) "
                    r"lift=\((.*)\)->\((.*)\) lower=\((.*)\)->\((.*)\)$")
LINE_D = re.compile(r"^canon-def (\d+) (lift|lower) .*?"
                    r"sig=\((.*)\)->\((.*)\) core=\((.*)\)->\((.*)\) "
                    r"needs-memory=(yes|no)")

def ref_needs_memory(d, ft, ctx):
    """Whether a signature has to reach memory, from the reference's own rules.

    Two ways it can: something in it is behind a pointer, or the flat form is
    over a limit and spills.  warmc computes the same thing in C; this is the
    second implementation, so agreement means something.
    """
    def pointered(t):
        t = d.despecialize(t)
        if isinstance(t, (d.StringType, d.ListType)):
            return True
        return any(pointered(x) for x in
                   [f.t for f in getattr(t, "fields", [])] +
                   [c.t for c in getattr(t, "cases", []) if c.t is not None] +
                   list(getattr(t, "ts", [])))
    ts = [t for _, t in ft.params] + list(ft.result)
    if any(pointered(t) for t in ts):
        return True
    flat_p = sum(len(ref_flat(d, t)) for _, t in ft.params)
    flat_r = sum(len(ref_flat(d, t)) for t in ft.result)
    return flat_p > d.MAX_FLAT_PARAMS or flat_r > d.MAX_FLAT_RESULTS

def split_types(s):
    """Split a comma-separated type list at depth zero."""
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "<{":
            depth += 1
        elif ch in ">}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur:
        out.append(cur)
    return out

EXPECT_DEFS = {"canonabi.wat": 5}

EXPECT_DECLARED = {"canonabi.wat": 5}

EXPECT_SIG = {
    "canonabi.wat": {
        "0": ("(u32,u32)->(u64)", "(i32,i32)->(i64)"),
        "1": ("(string)->(tuple{u32,u32,u32})", "(i32,i32,i32)->()"),
        "2": ("(string)->()", "(i32,i32)->()"),
        "3": ("(u32)->(tuple{u32,u32,u32})", "(i32,i32)->()"),
        "4": ("(string)->(tuple{u32,u32,u32})", "(i32,i32)->(i32)"),
    },
}

def check_dump(name, dump, d):
    n = 0
    ndefs = 0
    ndecl = 0
    for line in dump.splitlines():

        if line.startswith("canon-def ") and "MISSING-MEMORY" in line:
            fail("%s: %s" % (name, line))
            continue

        if line.startswith("canon-def ") and "declared=DIFFERS" in line:
            fail("%s: %s" % (name, line))
            continue

        if "unknown<" in line:
            fail("%s: a type index did not resolve: %s" % (name, line))
            continue
        m = LINE_T.match(line)
        if m:
            idx, expr, al, sz, flat = m.groups()
            try:
                t = parse_whole(expr, d)
            except ValueError as e:
                fail("%s: %s" % (name, e))
                continue
            want_al = d.alignment(t, "i32")
            want_sz = d.elem_size(t, "i32")
            want_fl = ",".join(ref_flat(d, t))
            if int(al) != want_al:
                fail("%s type %s (%s): align %s, the reference says %d"
                     % (name, idx, expr, al, want_al))
            if int(sz) != want_sz:
                fail("%s type %s (%s): size %s, the reference says %d"
                     % (name, idx, expr, sz, want_sz))
            if flat != want_fl:
                fail("%s type %s (%s): flat %s, the reference says %s"
                     % (name, idx, expr, flat, want_fl))
            n += 1
            continue
        m = LINE_D.match(line)
        if m:
            idx, ctx, ps, rs, cp, cr, need = m.groups()
            try:
                params = [(str(i), parse_whole(x, d))
                          for i, x in enumerate(split_types(ps)) if x]
                result = [parse_whole(rs, d)] if rs else []
            except ValueError as e:
                fail("%s: %s" % (name, e))
                continue
            pin = EXPECT_SIG.get(name, {}).get(idx)
            if pin is not None:
                got = ("(%s)->(%s)" % (ps, rs), "(%s)->(%s)" % (cp, cr))
                if got != pin:
                    fail("%s def %s: resolved %s core %s, the fixture declares "
                         "%s core %s" % (name, idx, got[0], got[1], pin[0], pin[1]))
            ft = d.FuncType(params, result)
            sig = d.flatten_functype(Opts("i32"), ft, ctx)
            want = (",".join(sig.params), ",".join(sig.results))
            if (cp, cr) != want:
                fail("%s def %s %s: core (%s)->(%s), the reference says (%s)->(%s)"
                     % (name, idx, ctx, cp, cr, want[0], want[1]))
            wn = "yes" if ref_needs_memory(d, ft, ctx) else "no"
            if need != wn:
                fail("%s def %s %s: needs-memory=%s, the reference's rules say %s"
                     % (name, idx, ctx, need, wn))
            n += 1
            ndefs += 1
            if "declared=same" in line:
                ndecl += 1
            continue
        m = LINE_F.match(line)
        if m:
            idx, ps, rs, lp, lr, wp, wr = m.groups()
            try:
                params = [(str(i), parse_whole(x, d))
                          for i, x in enumerate(split_types(ps)) if x]
                result = [parse_whole(rs, d)] if rs else []
            except ValueError as e:
                fail("%s: %s" % (name, e))
                continue
            ft = d.FuncType(params, result)
            for ctx, gp, gr in (("lift", lp, lr), ("lower", wp, wr)):
                sig = d.flatten_functype(Opts("i32"), ft, ctx)
                want = (",".join(sig.params), ",".join(sig.results))
                if (gp, gr) != want:
                    fail("%s func %s %s: (%s)->(%s), the reference says (%s)->(%s)"
                         % (name, idx, ctx, gp, gr, want[0], want[1]))
            n += 1
    want = EXPECT_DEFS.get(name)
    if want is not None and ndefs != want:
        fail("%s: %d canonical definition(s) checked, expected %d"
             % (name, ndefs, want))
    want = EXPECT_DECLARED.get(name)
    if want is not None and ndecl != want:
        fail("%s: %d signature(s) confirmed against the core module, "
             "expected %d" % (name, ndecl, want))
    return n

# Components that are in the corpus *because* the reference refuses them: a
# composition wasm-tools will not validate has nothing here to compare against,
# and the harness that owns each is named beside it.  Listed by name rather
# than skipped on a failed validation, so a fixture that stops validating by
# accident is still a failure.
NOT_VALID = {
    # A lowered import declared with a type its callee was not lifted with,
    # which is what test/run_clink.py checks the link plan refuses.
    "mismatch.wat",
}

def components():
    """(name, path) for everything worth dumping."""
    os.makedirs(WORK, exist_ok=True)
    out = []
    src = os.path.join(ROOT, "test", "components")
    for f in sorted(os.listdir(src)):
        if not f.endswith(".wat") or f in NOT_VALID:
            continue
        p = os.path.join(WORK, f[:-4] + ".wasm")
        rc, _, se = run([WASM_TOOLS, "parse", os.path.join(src, f), "-o", p])
        if rc != 0:
            continue

        rc, _, se = run([WASM_TOOLS, "validate", "--features", "all", p])
        if rc != 0:
            fail("%s does not validate: %s" % (f, se.strip().splitlines()[0][:160]))
            continue
        out.append((f, p))

    import random
    for seed in range(1, SMITH + 1):
        comp = os.path.join(WORK, "smith%03d.wasm" % seed)
        if not os.path.exists(comp):
            raw = os.path.join(WORK, "smith%03d.bin" % seed)
            rng = random.Random(seed)
            with open(raw, "wb") as f:
                f.write(bytes(rng.randrange(256) for _ in range(600)))
            rc, _, _ = run([WASM_TOOLS, "wit-smith", raw, "-o", comp])
            if rc != 0:
                continue
        out.append(("smith%03d" % seed, comp))
    return out

def main():
    if not os.path.exists(WARMC):
        print("canon: %s not built" % WARMC, file=sys.stderr)
        return 1
    if not os.path.exists(WASM_TOOLS):
        print("canon: wasm-tools not built (run `make wasm-tools`)", file=sys.stderr)
        return 1
    if not ensure_spec():
        return 0
    sys.path.insert(0, SPEC_DIR)
    try:
        import definitions as d
    except Exception as e:
        print("canon: skipping, the reference does not import (%s)" % e)
        return 0

    corpus = components()
    if not corpus:
        print("canon: no components to check", file=sys.stderr)
        return 1
    total, contributed = 0, 0
    for name, path in corpus:
        rc, so, se = run([WARMC, "--dump-canon", path])
        if rc != 0:
            fail("%s: warmc --dump-canon failed: %s" % (name, se.strip()[:200]))
            continue
        n = check_dump(name, so, d)
        total += n
        if n:
            contributed += 1
    print("canon: %d types from %d of %d components agree with the reference, "
          "%d failure(s)" % (total, contributed, len(corpus), failures))
    return 1 if failures else 0

if __name__ == "__main__":
    sys.exit(main())
