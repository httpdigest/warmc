"""Code inside one 4 KB page (src/pagefit.c): what the compiler makes of
the rule (src/codegen.c page_fit, by isel.c's stretches, FuncOut.fit) and
the linker (src/link.c place, for the code of others).  The rule by itself,
on stretches and code made to measure, is test/pagefit_test.c's.

What runs over a 4 KB boundary takes a cycle more to fetch each time it
does, so a small loop lies inside one page, and a function of at most 256
bytes does: where one would not, its function starts later, by bytes that
nothing runs.  The module here is made for the rule to have work:

  lp_K   a loop of one block -- a hash of bytes, which no vector does --
         of 6 to 67 instructions, behind straight-line code of a length
         that differs from function to function too
  l2_K   a loop with two ways back to its top, the second after a sum or,
         in every other one, a call
  sm_K   a function of a dozen to sixty instructions and no loop
  sw_K   a function that answers by straight-line code or calls itself,
         placed as a start and its full body (shrink-wrapping), which have
         to land where the start's branches were told the body is; called
         by swc_K, since a function entered through a table has no start
  big_K  functions of more than 256 bytes and no loop, which may lie
         anywhere

each called through a table by a loop that sums what they answer, and
compiled with -fno-unroll, so that a loop's size is its body's.

  rule    WARMC_PAGE_FIT_DEBUG=1 prints each function as it is placed --
          where the text ended, where its code starts, its step, the bytes
          ahead of its symbol, its length, its stretches -- and each start
          is the one the rule gives, worked out here again; a function
          with a loop moves by whole 64-byte blocks, as its loops were
          placed for; the symbols are where that says, and --stats counts
          the functions moved and the bytes left ahead of them
  edges   two functions alone, of sizes made to measure -- a step is one
          instruction: one of 256 bytes that ends where a page does stays
          where it is; 4 bytes later, it starts at the boundary; and one of
          260 bytes over the boundary stays
  loops   a loop is a stretch, from its top to its last branch back and
          the moves a way out may have behind that, where that is 256
          bytes at most, and none where it is more (a stretch of 256 bytes
          is among them, and a loop of 260); and no stretch is anything
          else
  fit     in the object's disassembly no loop of at most 256 bytes -- a
          backward conditional branch and its target -- and no function of
          at most 256 bytes runs over a multiple of 4096
  bites   with WARMC_NO_PAGE_FIT=1 several loops and several functions do
          run over a boundary, and nothing is moved: the rule had work
  runs    the program's sum is what Python's is, with the rule and without,
          and what held of the object holds of the program: its text
          starts on a page's boundary
  links   the runtime's functions are placed by the linker, which reads
          their small loops off their code: in a program's disassembly no
          small loop -- there a backward jump counts too -- and no small
          function runs over a boundary, over programs of several sizes
          that put the runtime at as many offsets in its page; without the
          rule some do; and WARMC_PAGE_FIT_DEBUG=1 prints each atom the
          linker looks at -- where it would have started, where it starts,
          its size, its alignment -- each start the one the rule gives for
          the branches the program's disassembly has there
  x86-64  the rule is arm64's alone: the object is the same bytes with
          WARMC_NO_PAGE_FIT=1

Usage: python3 test/run_pagefit.py
"""
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_gosp

ROOT = os.path.dirname(HERE)
BUILD = os.environ.get("BUILD", "build")
WARMC = os.environ.get("WARMC_BIN") or os.path.join(ROOT, BUILD, "warmc")
OBJDUMP = shutil.which("llvm-objdump") or shutil.which("objdump")
ARM64 = platform.machine() in ("arm64", "aarch64")
M32 = 0xffffffff
PAGE = 4096
LOOP_MAX = 256
FN_MAX = 256
NLP, NL2, NSM, NSW, NBIG = 660, 120, 500, 200, 40
MIN_BITES = 3
LINKS = 10
DEBUG = os.environ.get("PAGEFIT_DEBUG")
OPT = ["-O2", "-fno-unroll"]


def rotl(x, k):
    k &= 31
    return ((x << k) | (x >> (32 - k))) & M32 if k else x


def mix_steps(seed, n):
    """n steps of straight-line code on $x and $y: (wat lines, the Python of them)"""
    steps = []
    s = seed
    for _ in range(n):
        s = (s * 1103515245 + 12345) & 0x7fffffff
        steps.append((s >> 8 & 3, (s >> 3) % 31 + 1, s | 1))

    def wat():
        out = []
        for kind, k, c in steps:
            if kind == 0:
                out.append("(local.set $x (i32.add (i32.rotl (local.get $x) (i32.const %d)) (local.get $y)))" % k)
            elif kind == 1:
                out.append("(local.set $y (i32.xor (local.get $y) (i32.mul (local.get $x) (i32.const %d))))" % (c & 0xffff))
            elif kind == 2:
                out.append("(local.set $x (i32.sub (local.get $x) (i32.shr_u (local.get $y) (i32.const %d))))" % k)
            else:
                out.append("(local.set $y (i32.add (local.get $y) (i32.xor (local.get $x) (i32.const %d))))" % (c & 0xfffff))
        return out

    def run(x, y):
        for kind, k, c in steps:
            if kind == 0:
                x = (rotl(x, k) + y) & M32
            elif kind == 1:
                y = y ^ ((x * (c & 0xffff)) & M32)
            elif kind == 2:
                x = (x - (y >> k)) & M32
            else:
                y = (y + (x ^ (c & 0xfffff))) & M32
        return x, y
    return wat, run


DATA = bytes((i * 37 + 11) & 0xff for i in range(256))
TWOS = (0, 1, 2, 3, 4, 5, 7, 10, 14, 20, 26, 28, 29, 30)
HASH = "(local.set $y (i32.rotl (i32.xor (local.get $y) (i32.load8_u (local.get $p))) (i32.const 3)))"
NEXT = "(local.set $p (i32.add (local.get $p) (i32.const 1)))"
BACK = "(br_if $l (i32.lt_u (local.get $p) (local.get $e)))"


def functions(nlp=NLP, nl2=NL2, nsm=NSM, nsw=NSW, nbig=NBIG):
    """((name, locals, wat lines, Python f(x, y) -> i32) of every function of the table, in the
    module's order; the functions ahead of them, which they call)"""
    fs = []
    extra = []
    kinds = (["lp"] * nlp) + (["l2"] * nl2) + (["sm"] * nsm) + (["sw"] * nsw) + (["big"] * nbig)
    # interleaved, so that the sizes ahead of any one are of every kind
    order = sorted(range(len(kinds)), key=lambda i: (i * 2654435761) & M32)
    count = {}
    for i in order:
        kind = kinds[i]
        k = count.get(kind, 0)
        count[kind] = k + 1
        name = "%s_%d" % (kind, k)
        seed = i * 7919 + 13
        decl = ""
        if kind in ("lp", "l2"):
            wat, run = mix_steps(seed, 1 + (i * 5) % 23)
            lo, n = (i * 3) % 64, 1 + i % 29
            head = wat() + [
                "(local.set $p (i32.const %d))" % lo,
                "(local.set $e (i32.add (i32.const %d) (i32.and (local.get $x) (i32.const 31))))" % (lo + n)]
            decl = "(local $p i32) (local $e i32)"
        if kind == "lp":
            # the loop's body longer by a rotate and an add `two` times, and by a subtraction once:
            # 24 bytes and 8 for each of the first, 4 for the other
            two, one = TWOS[(k // 2) % len(TWOS)], k % 2
            more = [(1 + (k + j) % 30) for j in range(two)]
            body = (head + ["(loop $l", "  " + HASH] +
                    ["  (local.set $y (i32.add (i32.rotl (local.get $y) (i32.const %d)) (local.get $x)))" % r for r in more] +
                    (["  (local.set $y (i32.sub (local.get $y) (local.get $x)))"] if one else []) +
                    ["  " + NEXT, "  " + BACK + ")", "(i32.xor (local.get $x) (local.get $y))"])

            def f(x, y, run=run, lo=lo, n=n, more=more, one=one):
                x, y = run(x, y)
                p, e = lo, lo + n + (x & 31)
                while True:
                    y = rotl(y ^ DATA[p], 3)
                    for r in more:
                        y = (rotl(y, r) + x) & M32
                    if one:
                        y = (y - x) & M32
                    p += 1
                    if not p < e:
                        break
                return x ^ y
        elif kind == "l2":
            call = k % 2 == 1
            mid = ("(local.set $x (call $leaf (local.get $x) (local.get $y) (global.get $deep)))" if call
                   else "(local.set $x (i32.add (local.get $x) (local.get $y)))")
            body = (head + ["(block $out (loop $l", "  " + HASH, "  " + NEXT,
                            "  (if (i32.and (local.get $y) (i32.const 1)) (then " + BACK + " (br $out)))",
                            "  " + mid, "  " + BACK + "))", "(i32.xor (local.get $x) (local.get $y))"])

            def f(x, y, run=run, lo=lo, n=n, call=call):
                x, y = run(x, y)
                p, e = lo, lo + n + (x & 31)
                while True:
                    y = rotl(y ^ DATA[p], 3)
                    p += 1
                    if y & 1:
                        if p < e:
                            continue
                        break
                    x = (x * 3 + y) & M32 if call else (x + y) & M32
                    if not p < e:
                        break
                return x ^ y
        elif kind == "sm":
            wat, run = mix_steps(seed, 2 + (i * 3) % 17)
            body = wat() + ["(i32.add (local.get $x) (local.get $y))"]

            def f(x, y, run=run):
                x, y = run(x, y)
                return (x + y) & M32
        elif kind == "sw":
            wat, run = mix_steps(seed, 1 + (i * 3) % 5)
            c = i * 97 + 5
            # sw_K, which the table's swc_K calls: its answer where d is 0, itself with d - 1 otherwise
            extra.append((name,
                          "  (func $%s (param $x i32) (param $y i32) (param $d i32) (result i32)\n"
                          "    (if (result i32) (i32.eqz (local.get $d))\n      (then\n%s\n        (i32.add (local.get $x) (local.get $y)))\n"
                          "      (else (call $%s (i32.add (local.get $x) (i32.const %d)) (local.get $y)"
                          " (i32.sub (local.get $d) (i32.const 1))))))"
                          % (name, "\n".join("        " + w for w in wat()), name, c)))
            body = ["(call $%s (local.get $x) (local.get $y) (i32.add (i32.and (local.get $y) (i32.const 1)) (i32.const 1)))" % name]

            def f(x, y, run=run, c=c):
                x = (x + c * ((y & 1) + 1)) & M32
                x, y = run(x, y)
                return (x + y) & M32
            name = "swc_%d" % k
        else:
            wat, run = mix_steps(seed, 24 + (i * 7) % 60)
            body = wat() + ["(i32.sub (local.get $y) (local.get $x))"]

            def f(x, y, run=run):
                x, y = run(x, y)
                return (y - x) & M32
        fs.append((name, decl, body, f))
    return fs, extra


def steps(n, v="x"):
    """n instructions that nothing folds or pairs: stores of v to n addresses 16 bytes apart"""
    return ["(i32.store (i32.const %d) (local.get $%s))" % (1024 + 16 * i, v) for i in range(n)]


def two(n, m):
    """a module of two functions of n and m instructions and their ends"""
    f = lambda name, k, v: ['  (func $%s (export "%s") (param $x i32) (param $y i32) (result i32)' % (name, name)] + \
        ["    " + l for l in steps(k, v)] + ["    (i32.sub (local.get $x) (local.get $y)))"]
    return "\n".join(['(module', '  (memory (export "memory") 1)'] + f("pad", n, "x") + f("tgt", m, "y") + [")"]) + "\n"


def module(fs, extra):
    L = ['(module',
         '  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))',
         '  (type $F (func (param i32 i32) (result i32)))',
         '  (memory (export "memory") 1)',
         '  (data (i32.const 0) "%s")' % "".join("\\%02x" % b for b in DATA),
         '  (table $t %d funcref)' % len(fs),
         '  (elem (i32.const 0) %s)' % " ".join("$" + n for n, _, _, _ in fs),
         '  ;; how deep $leaf goes where a loop calls it: 0, which only a run knows',
         '  (global $deep (export "deep") (mut i32) (i32.const 0))',
         '  ;; 3 x + y, at the end of a recursion: a call, and a frame in its callers',
         '  (func $leaf (param $x i32) (param $y i32) (param $d i32) (result i32)',
         '    (if (result i32) (i32.eqz (local.get $d))',
         '      (then (i32.add (i32.mul (local.get $x) (i32.const 3)) (local.get $y)))',
         '      (else (call $leaf (local.get $x) (local.get $y) (i32.sub (local.get $d) (i32.const 1))))))']
    L += [text for _, text in extra]
    for name, decl, body, _ in fs:
        L.append('  (func $%s (type $F) (param $x i32) (param $y i32) (result i32) %s' % (name, decl))
        L += ['    ' + b for b in body]
        L[-1] += ')'
    L += ['  (func $start (export "_start") (local $i i32) (local $s i32)',
          '    (loop $l',
          '      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))',
          '        (call_indirect $t (type $F) (i32.add (local.get $s) (i32.const 7)) (local.get $i) (local.get $i))))',
          '      (local.set $i (i32.add (local.get $i) (i32.const 1)))',
          '      (br_if $l (i32.lt_u (local.get $i) (i32.const %d))))' % len(fs),
          '    (call $exit (i32.and (local.get $s) (i32.const 127))))',
          ')']
    return "\n".join(L) + "\n"


def names(fs, extra):
    """the module's functions by index: one import, $leaf, the ones the table's call, the table's, $start"""
    return ["exit", "leaf"] + [n for n, _ in extra] + [n for n, _, _, _ in fs] + ["start"]


def want(fs):
    s = 0
    for i, (_, _, _, f) in enumerate(fs):
        s = (s * 31 + f((s + 7) & M32, i)) & M32
    return s & 127


def over(a, b):
    return b > a and a // PAGE != (b - 1) // PAGE


def fit_model(at, step, entry, length, fit):
    """where codegen.c page_fit starts a function's code"""
    whole = length - entry <= FN_MAX
    if not whole and not fit:
        return at
    p = at
    while p - at < PAGE:
        frm = None
        if whole and over(p + entry, p + length):
            frm = p + entry
        if frm is None:
            for s, e in fit:
                if over(p + s, p + e):
                    frm = p + s
                    break
        if frm is None:
            return p
        need = PAGE - frm % PAGE
        p += -(-need // step) * step
    return at


class Code:
    """A disassembly: each symbol's start, its end (past its last instruction that is no padding), and
    its backward branches as (target, end of the branch, conditional)"""

    def __init__(self, path):
        d = subprocess.run([OBJDUMP, "-d", "--no-show-raw-insn", path], capture_output=True, text=True).stdout
        self.start, self.end, self.back, self.order = {}, {}, {}, []
        self.branches = []      # every backward branch, in the order of the code: (address, target)
        sym = None
        for line in d.splitlines():
            m = re.match(r"^([0-9a-f]+) <(.*)>:$", line)
            if m:
                sym = m.group(2)
                sym = sym[1:] if sym.startswith("_") else sym
                self.start[sym] = int(m.group(1), 16)
                self.back[sym] = []
                self.order.append(sym)
                continue
            m = re.match(r"^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$", line)
            if not m or sym is None:
                continue
            at, op, args = int(m.group(1), 16), m.group(2), m.group(3)
            # (a linker's bytes between two functions are no instructions of the first)
            if op not in ("nop", "udf", "<unknown>") and not op.startswith("."):
                self.end[sym] = at + 4
            cond = op.startswith("b.") or op in ("cbz", "cbnz", "tbz", "tbnz")
            if cond or op == "b":
                t = re.search(r"(?:0x)?([0-9a-f]+)(?: <|$)", args.split(",")[-1].strip())
                if t and int(t.group(1), 16) < at:
                    self.branches.append((at, int(t.group(1), 16)))
                if t and self.start[sym] <= int(t.group(1), 16) <= at:
                    self.back[sym].append((int(t.group(1), 16), at + 4, cond))

    def loops(self, sym, jumps=False):
        """the small loops of sym: (top, end) of each backward branch at most LOOP_MAX from its target"""
        return [(t, e) for t, e, cond in self.back[sym] if e - t <= LOOP_MAX and (cond or jumps)]

    def straddlers(self, jumps=lambda sym: False, shift=None):
        """(what runs over a boundary, small loops seen, small functions seen); with shift, of a
        symbol alone and as if it started that many bytes earlier"""
        bad, nloops, nfns = [], 0, 0
        for sym, a in self.start.items():
            if sym.endswith("$entry") or sym not in self.end or (shift and sym != shift[0]):
                continue
            d = shift[1] if shift else 0
            for t, e in self.loops(sym, jumps(sym)):
                nloops += 1
                if over(t - d, e - d):
                    bad.append("%s's loop at %#x..%#x" % (sym, t, e))
            if self.end[sym] - a <= FN_MAX:
                nfns += 1
                if over(a - d, self.end[sym] - d):
                    bad.append("%s at %#x..%#x" % (sym, a, self.end[sym]))
        return bad, nloops, nfns


def link_model(at, size, align, backs):
    """where link.c text_fit starts an atom; backs: its backward branches as (offset, distance)"""
    step = max(align, 4)
    p = at
    while p - at < PAGE:
        frm = None
        if size <= FN_MAX and over(p, p + size):
            frm = p
        if frm is None:
            for o, d in backs:
                if over(p + o - d, p + o + 4):
                    frm = p + o - d
                    break
        if frm is None:
            return p
        need = PAGE - frm % PAGE
        p += -(-need // step) * step
    return at


def stat(err, key):
    m = re.search(r"\b%s=(\d+)" % key, err)
    return int(m.group(1)) if m else None


def check_rule(err, code, fnames, fails):
    """The debug lines against the rule, the symbols, the loops and the counts"""
    lines = [l for l in err.splitlines() if l.startswith("pagefit ")]
    moved = left = 0
    seen = set()
    sizes, longer = set(), set()      # the stretches' sizes; the loops that are none
    for l in lines:
        m = re.match(r"pagefit wasm_func_(\d+)(\$full)? at=(\d+) to=(\d+) step=(\d+) entry=(\d+) len=(\d+) fit=(\S*)$", l)
        if not m:
            fails.append("rule: cannot read `%s`" % l)
            continue
        idx, at, to, step, entry, length = (int(m.group(g)) for g in (1, 3, 4, 5, 6, 7))
        fit = [tuple(map(int, s.split("-"))) for s in m.group(8).split(",") if s]
        name = fnames[idx] + (m.group(2) or "")
        seen.add(name)
        w = fit_model(at, step, entry, length, fit)
        if to != w:
            fails.append("rule: %s, where the text ended at %d, starts at %d, not %d" % (name, at, to, w))
        if to != at:
            moved += 1
            left += to - at
        if step != (64 if name.split("_")[0] in ("lp", "l2", "start") else 4):
            fails.append("rule: %s moves by steps of %d" % (name, step))
        if code.start.get(name) != to + entry:
            fails.append("rule: %s is at %s, not %d" % (name, code.start.get(name), to + entry))
            continue
        for s, e in fit:
            if not 0 < e - s <= LOOP_MAX:
                fails.append("loops: %s has a stretch of %d bytes" % (name, e - s))
        # its loops by their tops: the ends of the branches back
        tops = {}
        for t, e, cond in code.back.get(name, []):
            if cond:
                tops.setdefault(t, []).append(e)
        have = {to + s: to + e for s, e in fit}
        for t in have:
            if t not in tops:
                fails.append("loops: %s has a stretch at %#x, where no loop starts" % (name, t))
        for t, ends in tops.items():
            last = max(ends)
            # (the loop's last block may have a way out's moves, or its jump, behind the branch back)
            if t in have:
                sizes.add(have[t] - t)
                if not last <= have[t] <= last + 8:
                    fails.append("loops: %s's loop at %#x..%#x has a stretch that ends at %#x"
                                 % (name, t, last, have[t]))
            elif last - t <= LOOP_MAX - 8:
                fails.append("loops: %s's loop of %d bytes at %#x is no stretch" % (name, last - t, t))
            else:
                longer.add(last - t)
    if LOOP_MAX not in sizes or not any(LOOP_MAX < n <= LOOP_MAX + 4 for n in longer):
        fails.append("loops: no stretch of %d bytes and loop of %d to try the limit with (stretches up to %s, loops left out from %s)"
                     % (LOOP_MAX, LOOP_MAX + 4, max(sizes or [0]), min(longer or [0])))
    for n in fnames[1:]:
        if n not in seen:
            fails.append("rule: no line for %s" % n)
    if stat(err, "page_fits") != moved or stat(err, "page_fit_bytes") != left or not moved:
        fails.append("rule: --stats says page_fits=%s page_fit_bytes=%s, the lines %d and %d"
                     % (stat(err, "page_fits"), stat(err, "page_fit_bytes"), moved, left))


def check_edges(tmp, obj_flags, on, fails):
    """Two functions alone, the second placed by the first's size"""
    def place(n, m):
        wat = os.path.join(tmp, "two.wat")
        open(wat, "w").write(two(n, m))
        r = subprocess.run([WARMC] + obj_flags + OPT + ["-c", "-o", os.path.join(tmp, "two.o"), wat],
                           capture_output=True, text=True, env=on)
        got = re.findall(r"^pagefit wasm_func_(\d) at=(\d+) to=(\d+) step=\d+ entry=(\d+) len=(\d+) fit=$", r.stderr, re.M)
        if r.returncode or len(got) != 2:
            fails.append("edges: two functions of %d and %d instructions: %s" % (n, m, (r.stderr.strip() or "no lines")[:200]))
            return None
        return [tuple(map(int, g[1:])) for g in sorted(got)]
    a, b = place(8, 8), place(12, 12)
    if not a or not b:
        return
    if b[0][3] - a[0][3] != 16 or b[1][3] - a[1][3] != 16:
        fails.append("edges: four instructions more are %d and %d bytes more" % (b[0][3] - a[0][3], b[1][3] - a[1][3]))
        return
    entry = a[1][2]
    m = 8 + (FN_MAX + entry - a[1][3]) // 4                 # the second of FN_MAX bytes behind its lead
    n = 8 + (PAGE - (FN_MAX + entry) - a[0][3]) // 4        # and its end on the boundary
    for dn, dm, what in ((0, 0, "ends where the page does"), (1, 0, "is 4 bytes over the boundary"),
                         (2, 1, "is %d bytes long and over the boundary" % (FN_MAX + 4))):
        got = place(n + dn, m + dm)
        if not got:
            return
        at, to, entry, length = got[1]
        if length - entry != FN_MAX + 4 * dm or at + length != PAGE + 4 * dn + 4 * dm:
            fails.append("edges: the function made to measure is of %d bytes and ends at %d" % (length - entry, at + length))
        elif to != (PAGE - entry if (dn, dm) == (1, 0) else at):
            fails.append("edges: a function of %d bytes that %s, from %d, starts at %d" % (length - entry, what, at, to))


def check_links(tmp, flags, on, off, fails):
    """Programs whose own code grows by a function of some 600 bytes each: the runtime after it starts
    that much later in its page"""
    bites = moved = 0
    for k in range(LINKS):
        gs, gextra = functions(12, 4, 12, 4, k)
        mine = set(names(gs, gextra))
        # (the module's symbols, with their starts' full bodies and the leads ahead of them)
        theirs = lambda sym: sym.split("$")[0] not in mine
        wk = os.path.join(tmp, "l%d.wat" % k)
        open(wk, "w").write(module(gs, gextra))
        for name, env in (("on", on), ("off", off)):
            exe = os.path.join(tmp, "l%d.%s" % (k, name))
            r = subprocess.run([WARMC] + flags + ["--wasi"] + OPT + ["-o", exe, wk], capture_output=True, text=True, env=env)
            if r.returncode:
                fails.append("links (%d, %s): the compile failed: %s" % (k, name, r.stderr.strip()[:300]))
                continue
            code = Code(exe)
            bad, _, _ = code.straddlers(theirs)
            if name == "off":
                bites += len([b for b in bad if theirs(b.split("'")[0].split(" ")[0])])
                if DEBUG:
                    print(k, bad[:8])
                continue
            for what in bad:
                fails.append("links (%d): %s runs over a page boundary" % (k, what))
            p = subprocess.run([exe], capture_output=True)
            if p.returncode != want(gs):
                fails.append("links (%d): the sum is %d, not %d" % (k, p.returncode, want(gs)))
            # each atom's start by the rule, from the branches the disassembly has in it
            lines = re.findall(r"^linkfit at=(\d+) to=(\d+) size=(\d+) align=(\d+)$", r.stderr, re.M)
            if len(lines) < 50:
                fails.append("links (%d): the linker lists %d atoms" % (k, len(lines)))
            moved += len([l for l in lines if l[0] != l[1]])
            bi = 0
            for at, to, size, align in (tuple(map(int, l)) for l in lines):
                while bi < len(code.branches) and code.branches[bi][0] < to:
                    bi += 1
                backs, x = [], bi
                while x < len(code.branches) and code.branches[x][0] < to + size:
                    a, t = code.branches[x]
                    if a - t <= LOOP_MAX - 4 and t >= to:
                        backs.append((a - to, a - t))
                    x += 1
                w = link_model(at, size, align, backs)
                if w != to:
                    fails.append("links (%d): the atom of %d bytes that would start at %#x starts at %#x, not %#x"
                                 % (k, size, at, to, w))
                    break
    if DEBUG:
        print("links: %d of the runtime's loops and functions over a boundary without the rule, %d atoms moved with it" % (bites, moved))
    if bites < MIN_BITES or moved < MIN_BITES:
        fails.append("links: without the rule only %d of the runtime's loops and functions run over a boundary, and with it %d atoms are moved"
                     % (bites, moved))


def main():
    os.chdir(ROOT)
    fs, extra = functions()
    fnames = names(fs, extra)
    flags = gen_gosp.target_flags(BUILD)
    obj_flags, skip = [], False
    for a in flags:          # an object needs no runtime
        if skip:
            skip = False
        elif a == "--rt":
            skip = True
        else:
            obj_flags.append(a)
    on = dict(os.environ, WARMC_PAGE_FIT_DEBUG="1")
    off = dict(os.environ, WARMC_NO_PAGE_FIT="1", WARMC_PAGE_FIT_DEBUG="1")
    fails = []
    with tempfile.TemporaryDirectory() as tmp:
        wat = os.path.join(tmp, "pagefit.wat")
        open(wat, "w").write(module(fs, extra))
        objs = {}
        for name, env in (("on", on), ("off", off)):
            objs[name] = os.path.join(tmp, name + ".o")
            r = subprocess.run([WARMC] + obj_flags + OPT + ["--stats", "-c", "-o", objs[name], wat],
                               capture_output=True, text=True, env=env)
            if r.returncode:
                fails.append("%s: the compile failed: %s" % (name, r.stderr.strip()[:300]))
            objs[name + ".err"] = r.stderr
        if not fails and not ARM64:
            if open(objs["on"], "rb").read() != open(objs["off"], "rb").read():
                fails.append("x86-64: the object differs with WARMC_NO_PAGE_FIT=1")
            if "pagefit " in objs["on.err"]:
                fails.append("x86-64: functions are placed by the rule")
        elif not fails and not OBJDUMP:
            fails.append("no llvm-objdump or objdump to read the object with")
        elif not fails:
            code = Code(objs["on"])
            check_rule(objs["on.err"], code, fnames, fails)
            check_edges(tmp, obj_flags, on, fails)
            bad, nloops, nfns = code.straddlers()
            for what in bad:
                fails.append("fit: %s runs over a page boundary" % what)
            if nloops < NLP // 2 or nfns < NSM:
                fails.append("fit: %d small loops and %d small functions found, of %d and %d made"
                             % (nloops, nfns, NLP, NSM))
            wrapped = stat(objs["on.err"], "shrink_wrapped")
            # (not every one: a start as long as what it saves is not worth its copy)
            if not wrapped or wrapped < NSW // 2:
                fails.append("fit: %s functions placed as a start and a full body, of %d made for it" % (wrapped, NSW))
            if stat(objs["off.err"], "page_fits") or "pagefit " in objs["off.err"]:
                fails.append("bites: WARMC_NO_PAGE_FIT=1 still places functions by the rule")
            bad0, _, _ = Code(objs["off"]).straddlers()
            nl0, nf0 = len([b for b in bad0 if "'s loop" in b]), len([b for b in bad0 if "'s loop" not in b])
            if DEBUG:
                print("object: %d loops and %d functions over a boundary without the rule; %d small loops, %d small functions"
                      % (nl0, nf0, nloops, nfns))
            if nl0 < MIN_BITES or nf0 < MIN_BITES:
                fails.append("bites: without the rule only %d loops and %d functions run over a boundary" % (nl0, nf0))
        if not fails:
            w = want(fs)
            for name, env in (("on", None), ("off", off)):
                exe = os.path.join(tmp, name)
                r = subprocess.run([WARMC] + flags + ["--wasi"] + OPT + ["-o", exe, wat], capture_output=True, text=True, env=env)
                if r.returncode:
                    fails.append("runs (%s): the compile failed: %s" % (name, r.stderr.strip()[:300]))
                    continue
                p = subprocess.run([exe], capture_output=True)
                if p.returncode != w:
                    fails.append("runs (%s): the sum is %d, not %d" % (name, p.returncode, w))
                if name == "on" and ARM64 and OBJDUMP:
                    mine = set(fnames)
                    for what in Code(exe).straddlers(lambda sym: sym.split("$")[0] not in mine)[0]:
                        fails.append("runs: in the program %s runs over a page boundary" % what)
        if not fails and ARM64 and OBJDUMP:
            check_links(tmp, flags, on, off, fails)
    for f in fails[:20]:
        print("FAIL[pagefit]: " + f)
    print("pagefit: %d loops, %d small functions and %d wrapped ones, and the runtime behind %d programs: %s"
          % (NLP + NL2, NSM, NSW, LINKS, "inside their pages" if not fails else "%d failure(s)" % len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
