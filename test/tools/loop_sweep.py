"""Look inside the loops of a compiled module for code that should not be there.

An instrument, not a test.  `test/tools/latency_model.py` answers "how long is
this block's dependency chain"; this one answers the other question -- "is warmc
emitting anything here it did not need to" -- and it answers it where it matters,
inside loops, weighted by how deeply nested they are.

    python3 test/tools/loop_sweep.py BUILD/corpus/kernel.wasm [--top N]

Loops are found from the machine code rather than from the IR, deliberately: the
IR is what warmc believes and the disassembly is what the processor will run, and
the gap between the two is the whole reason for looking.  A backward branch whose
target is inside the same function is a loop; everything from the target to the
branch is its body, and a body inside another body is nested.

What it counts, and why each is worth counting:

  reg-copy      `mov xD, xS` between two general registers.  A copy the register
                allocator could not coalesce away.  Free on some cores and not
                on others, but never useful.
  wmov          the same at 32 bits.  Called out separately because roughly a
                tenth of them come from `i32.wrap_i64`, which needs no
                instruction at all on AArch64 -- the narrow register *is* the
                low half.
  shift-then-op a `lsl`/`lsr` whose result is used once by the very next
                arithmetic instruction, which could have taken it as a shifted
                operand.  warmc fuses these; one appearing here means a fusion
                that did not fire.
  cmp-cset      a condition materialised into a register.  Reasonable when the
                value is wanted; suspicious when a branch follows, which means
                the flags were thrown away and recomputed.
  reload        a load from `[sp, #...]`.  Inside a loop this is a spill slot
                being read every iteration.
  const-build   `mov`/`movk` pairs building a constant in a loop body, which
                could have been hoisted.
  addr-add      an `add` of a base and a scaled index feeding a load or store
                that could have folded it into the addressing mode.

Counts are raw and then weighted by 10 per level of nesting, which is the same
weighting the optimiser's own loop heuristics use.  Nothing here is a defect on
its own -- a `mov` may be a two-address instruction's copy that no allocator
could remove -- so what this produces is a list of places to look, ordered.
"""

import re
import subprocess
import sys
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WARMC = os.path.join(ROOT, os.environ.get("BUILD", "build"), "warmc") + (".exe" if os.name == "nt" else "")

INSN = re.compile(r"^\s+([0-9a-f]+):\s+[0-9a-f]{8}\s+(\S+)\s*(.*)$")
FUNC = re.compile(r"^([0-9a-f]+)\s+<([^>]+)>:")
BRANCH = re.compile(r"^(b|b\.\w+|cbz|cbnz|tbz|tbnz)$")

def disasm(wasm):
    obj = "/tmp/loop_sweep.o"
    r = subprocess.run([WARMC, "-O2", "-c", "-o", obj, wasm],
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("warmc failed: " + r.stderr.strip()[:300])
    return subprocess.run(["llvm-objdump", "-d", obj],
                          capture_output=True, text=True).stdout

def parse(text):
    """[(function name, [(addr, mnemonic, operands)])]"""
    funcs, cur = [], None
    for line in text.splitlines():
        m = FUNC.match(line)
        if m:
            cur = (m.group(2), [])
            funcs.append(cur)
            continue
        m = INSN.match(line)
        if m and cur is not None:
            cur[1].append((int(m.group(1), 16), m.group(2), m.group(3)))
    return funcs

def loops(insts):
    """Loop bodies as (start index, end index), deduplicated.

    One head with several backward branches to it is one loop, not several, so
    the widest span per head is the loop and the rest are its `continue`s.  This
    mattered: counting each backward branch separately made a function with a
    dozen of them look twelve loops deep.
    """
    addr_at = {a: i for i, (a, _, _) in enumerate(insts)}
    widest = {}
    for i, (a, mn, ops) in enumerate(insts):
        if not BRANCH.match(mn):
            continue
        m = re.search(r"0x([0-9a-f]+)", ops)
        if not m:
            continue
        t = int(m.group(1), 16)
        if t in addr_at and addr_at[t] <= i:
            h = addr_at[t]
            if h not in widest or i > widest[h]:
                widest[h] = i
    return sorted(widest.items())

def depth_of(span, all_spans):
    """How many loops properly *contain* this one, itself included.

    Containment, not overlap.  Two loops that share a tail are siblings; only a
    span strictly wrapping another is an outer loop, and reading overlap as
    nesting is what produced the depth of ten that made every function in every
    module look equally hot.
    """
    s, e = span
    d = 1
    for (a, b) in all_spans:
        if (a, b) == (s, e):
            continue
        if a <= s and e <= b:
            d += 1
    return min(d, 4)

REGCOPY = re.compile(r"^(x\d+|sp), (x\d+|sp)$")
WCOPY = re.compile(r"^w\d+, w\d+$")

def classify(insts, lo, hi):
    """Counts of each pattern in insts[lo..hi]."""
    c = {}
    def bump(k):
        c[k] = c.get(k, 0) + 1
    body = insts[lo:hi + 1]
    for i, (a, mn, ops) in enumerate(body):
        nxt = body[i + 1] if i + 1 < len(body) else None
        if mn == "mov":
            if REGCOPY.match(ops):
                bump("reg-copy")
            elif WCOPY.match(ops):
                bump("wmov")
            elif nxt and nxt[1] == "movk":
                bump("const-build")
        elif mn in ("lsl", "lsr", "asr") and nxt:
            dst = ops.split(",")[0].strip()
            if nxt[1] in ("add", "sub", "orr", "eor", "and", "cmp") \
                    and dst in nxt[2]:
                bump("shift-then-op")
        elif mn == "cset":
            if nxt and BRANCH.match(nxt[1]):
                bump("cmp-cset")
        elif mn in ("ldr", "ldrb", "ldrh", "ldp", "ldur") and "[sp" in ops:
            bump("reload")
        elif mn == "add" and nxt and nxt[1].startswith(("ldr", "str")):
            dst = ops.split(",")[0].strip()
            if "[" + dst in nxt[2]:
                bump("addr-add")
    return c

def wasm_names(wasm):
    """Function index -> the name the producer gave it, when there is one.

    warmc names its symbols `wasm_func_N` by index, so the module's own name
    section is what turns a sweep into something a person can act on.
    """
    wt = os.path.join(ROOT, "build", "tools", "bin",
                      "wasm-tools.exe" if os.name == "nt" else "wasm-tools")
    if not os.path.exists(wt):
        wt = "wasm-tools"
    try:
        out = subprocess.run([wt, "print", wasm], capture_output=True,
                             text=True).stdout
    except OSError:
        return {}
    names = {}
    for m in re.finditer(r"\(func \$([^ ]+) \(;(\d+);\)", out):
        names[int(m.group(2))] = m.group(1)
    return names

def main():
    args = sys.argv[1:]
    top = 6
    if "--top" in args:
        i = args.index("--top")
        top = int(args[i + 1])
        del args[i:i + 2]
    want = None
    if "--func" in args:
        i = args.index("--func")
        want = args[i + 1]
        del args[i:i + 2]
    if not args:
        sys.exit(__doc__)

    for wasm in args:
        print("=== %s ===" % os.path.basename(wasm))
        rows = []
        names = wasm_names(wasm)
        for name, insts in parse(disasm(wasm)):
            m = re.match(r"wasm_func_(\d+)$", name)
            if m and int(m.group(1)) in names:
                name = names[int(m.group(1))]
            if want and want not in name:
                continue
            spans = loops(insts)
            for span in spans:
                d = depth_of(span, spans)
                c = classify(insts, *span)
                n = sum(c.values())
                if not n:
                    continue
                weight = n * (10 ** (d - 1))
                rows.append((weight, d, span[1] - span[0] + 1, name, c))
        rows.sort(reverse=True, key=lambda r: r[0])
        for weight, d, size, name, c in rows[:top]:
            short = name if len(name) <= 60 else name[:57] + "..."
            print("  depth=%d body=%-4d weight=%-6d %s" % (d, size, weight, short))
            print("      " + "  ".join("%s=%d" % kv for kv in sorted(c.items())))
        if not rows:
            print("  nothing found in any loop")

if __name__ == "__main__":
    main()
