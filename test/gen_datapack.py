"""Random data segments, for the way warmc packs the active ones (src/datapack.c
and rt/lzunpack.inc): a memory the module defines whose every active segment
has a constant offset in bounds is one compressed image of what they make of
it; any other memory's segments are a stream of their own bytes.

Each module has one or two memories, 32- or 64-bit, of 1 to 8 pages, with up
to 300 active segments laid out as Go's linker lays them (two bytes in twenty)
or at random, overlapping, adjacent, either side of the gap that starts a new
span, at offset 0, ending where the memory does, empty at its end, and as
long as 300 KB, across the 256 KB a block holds -- of zeros, random bytes,
repeated patterns, runs of zeros among literals and copies, and text.  Some
memories also have a segment whose offset is an expression (i32.add of two
constants), which sends all of that memory's to the stream.  A few segments
are passive, and taken with memory.init, whole and in part, after data.drop
of another, and from an active segment with nothing to copy, after its
data.drop -- also in a module with no passive segment, which has no table of
segments at all.

Its exports hash each memory, whole and in eighths, then copy each passive
segment into the scratch page the segments leave alone and hash that.  And
what the binary keeps for memory.init is checked by WARMC_TABLE_STATS: the
bytes of the passive segments it reads, and a table of segments only when
there are some.  Each
module runs under wasm-interp, and compiled by warmc at -O2, with the blocks
64 bytes long (WARMC_PACK_BLOCK_LOG2=6) so that their ends fall where a small
module has data, and with -fno-compress-data; every answer must be the
interpreter's.

  python3 test/gen_datapack.py --seed 1 --count 40     # make check runs this
"""
import argparse
import concurrent.futures
import os
import random
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare as cmp_mod

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.environ.get("BUILD", "build")
WARMC = os.path.join(ROOT, BUILD, "warmc")
PAGE = 65536
SETTINGS = [("O2", ["-O2"], {}),
            ("small-blocks", ["-O2"], {"WARMC_PACK_BLOCK_LOG2": "6"}),
            ("no-compress", ["-O2", "-fno-compress-data"], {})]


def host():
    r = subprocess.run('. test/hostenv.sh; echo "$WARMC_TARGET_FLAGS"; echo "$WARMC_RUN";'
                       ' echo "$WABT_FLAGS"', shell=True, capture_output=True, text=True,
                       cwd=ROOT, env=dict(os.environ, BUILD=BUILD), executable="/bin/bash")
    lines = r.stdout.split("\n")
    return lines[0].split(), lines[1].split(), lines[2].split()


def content(rng, n):
    kind = rng.randrange(6)
    if kind == 0:
        return bytes(n)
    if kind == 1:
        return bytes(rng.randrange(256) for _ in range(n))
    if kind == 2:
        per = 1 + rng.randrange(24)
        pat = bytes(1 + rng.randrange(255) for _ in range(per))
        return (pat * (n // per + 1))[:n]
    if kind == 3:
        out = bytearray()
        while len(out) < n:
            r, ln = rng.randrange(3), 1 + rng.randrange(40)
            if r == 0:
                out += bytes(rng.randrange(256) for _ in range(ln))
            elif r == 1 or not out:
                out += bytes(ln)
            else:
                off = 1 + rng.randrange(len(out))
                for _ in range(ln):
                    out.append(out[-off])
        return bytes(out[:n])
    if kind == 4:
        return bytes(rng.choice(b"etaoin shrdlu") for _ in range(n))
    out = bytearray(n)
    i = rng.randrange(40)
    while i < n:
        out[i] = 1 + rng.randrange(255)
        i += 1 + rng.randrange(40)
    return bytes(out)


def esc(b):
    return "".join("\\%02x" % c for c in b)


def make(seed):
    rng = random.Random(seed)
    nmem = 2 if rng.random() < 0.25 else 1
    mems = []
    for _ in range(nmem):
        mems.append({"is64": rng.random() < 0.2, "pages": rng.choice([1, 2, 3, 5, 8]),
                     "expr": rng.random() < 0.2})
    segs = []       # (mode, mem, offset, expr, bytes)
    for mi, m in enumerate(mems):
        # the last page is scratch for the passive segments' copies, except
        # in a memory of one page, where it is the last 4 KB
        size = m["pages"] * PAGE
        top = size - (PAGE if m["pages"] > 1 else 4096)
        style = rng.randrange(4)
        n = rng.randrange(1, 300)
        at = rng.randrange(0, 256)
        for k in range(n):
            if style == 0:          # Go's: two bytes in twenty
                ln, off = 2, at
                at += 20
            elif style == 1:        # at random
                ln = rng.choice([0, 1, 2, 3, 5, 8, 16, 17, 31, 64, 100, 300, 1000, 4000])
                off = rng.randrange(0, max(1, top - ln))
            elif style == 2:        # one after another, gaps around SPAN_GAP
                ln = rng.randrange(0, 200)
                off = at
                at += ln + rng.choice([0, 0, 1, 63, 64, 65, 200, 5000])
            else:                   # few and long
                ln = rng.choice([1000, 20000, 70000, 300000]) if k < 3 else rng.randrange(50)
                off = rng.randrange(0, max(1, top - ln))
            if off + ln > top:
                break
            segs.append(("active", mi, off, False, content(rng, ln)))
        if rng.random() < 0.3 and top > 0:
            segs.append(("active", mi, 0, False, content(rng, min(top, 1 + rng.randrange(60)))))
        if rng.random() < 0.3:      # ending where the memory does, and empty there
            ln = 1 + rng.randrange(60)
            segs.append(("active", mi, size - ln, False, content(rng, ln)))
            segs.append(("active", mi, size, False, b""))
        if m["expr"]:
            ln = rng.randrange(1, 300)
            segs.append(("active", mi, rng.randrange(0, top - ln), True, content(rng, ln)))
    rng.shuffle(segs)
    npas = rng.randrange(0, 4)
    for _ in range(npas):
        segs.insert(rng.randrange(len(segs) + 1),
                    ("passive", 0, 0, False, content(rng, rng.randrange(0, 3000))))

    w = ["(module"]
    for m in mems:
        w.append("  (memory %s%d)" % ("i64 " if m["is64"] else "", m["pages"]))
    for i, (mode, mi, off, expr, b) in enumerate(segs):
        if mode == "passive":
            w.append('  (data $d%d "%s")' % (i, esc(b)))
            continue
        t = "i64" if mems[mi]["is64"] else "i32"
        if expr:
            a = rng.randrange(0, off + 1)
            o = "(%s.add (%s.const %d) (%s.const %d))" % (t, t, a, t, off - a)
        else:
            o = "(%s.const %d)" % (t, off)
        w.append('  (data $d%d (memory %d) %s "%s")' % (i, mi, o, esc(b)))
    for mi, m in enumerate(mems):
        t = "i64" if m["is64"] else "i32"
        w.append("""  (func $hash%d (param $lo %s) (param $hi %s) (result i64)
    (local $h i64)
    (local.set $h (i64.const 0xcbf29ce484222325))
    (block $d (loop $l
      (br_if $d (%s.ge_u (local.get $lo) (local.get $hi)))
      (local.set $h (i64.mul (i64.xor (local.get $h) (i64.load8_u %d (local.get $lo)))
                             (i64.const 0x100000001b3)))
      (local.set $lo (%s.add (local.get $lo) (%s.const 1)))
      (br $l)))
    (local.get $h))""" % (mi, t, t, t, mi, t, t))
    exports = []
    for mi, m in enumerate(mems):
        t = "i64" if m["is64"] else "i32"
        size = m["pages"] * PAGE
        w.append('  (func (export "m%d") (result i64) (call $hash%d (%s.const 0) (%s.const %d)))'
                 % (mi, mi, t, t, size))
        for e in range(8):
            w.append('  (func (export "m%d_%d") (result i64) (call $hash%d (%s.const %d) (%s.const %d)))'
                     % (mi, e, mi, t, size * e // 8, t, size * (e + 1) // 8))
    # the passive segments, copied into memory 0's scratch, whole and from
    # the middle; and memory.init of an active segment with nothing to copy
    m0 = mems[0]
    t = "i64" if m0["is64"] else "i32"
    scratch = m0["pages"] * PAGE - (PAGE if m0["pages"] > 1 else 4096)
    span = m0["pages"] * PAGE - scratch
    pas = [i for i, s in enumerate(segs) if s[0] == "passive" and 2 * len(s[4]) <= span]
    for j, i in enumerate(pas):
        b = segs[i][4]
        half = len(b) // 2
        w.append("""  (func (export "p%d") (result i64)
    (memory.fill %d (%s.const %d) (i32.const 0) (%s.const %d))
    (memory.init %d $d%d (%s.const %d) (i32.const 0) (i32.const %d))
    (memory.init %d $d%d (%s.const %d) (i32.const %d) (i32.const %d))
    (call $hash0 (%s.const %d) (%s.const %d)))"""
                 % (j, 0, t, scratch, t, span, 0, i, t, scratch, len(b),
                    0, i, t, scratch + len(b), half, len(b) - half,
                    t, scratch, t, scratch + len(b) + (len(b) - half)))
    act = [i for i, s in enumerate(segs) if s[0] == "active"]
    if act and pas:
        w.append("""  (func (export "q") (result i32)
    (data.drop $d%d)
    (memory.init 0 $d%d (%s.const %d) (i32.const 0) (i32.const 0))
    (i32.const 7))""" % (pas[-1], act[0], t, scratch))
    if act:
        # with no passive segment read, the module has no table of segments
        w.append("""  (func (export "r") (result i32)
    (data.drop $d%d)
    (memory.init 0 $d%d (%s.const %d) (i32.const 0) (i32.const 0))
    (i32.const 9))""" % (act[-1], act[-1], t, scratch))
    w.append(")")
    # the bytes of the passive segments something reads, and a table of the
    # segments only when there are some: the active ones are data_init's
    blob = sum(len(segs[i][4]) for i in pas)
    return "\n".join(w) + "\n", (blob + 7) // 8 * 8, 8 * len(segs) if pas else 0


def run_one(seed, work, target, run, wabt):
    name = "dp%d" % seed
    wat = os.path.join(work, name + ".wat")
    wasm = os.path.join(work, name + ".wasm")
    ref = os.path.join(work, name + ".ref")
    text, want_blob, want_table = make(seed)
    with open(wat, "w") as f:
        f.write(text)
    r = subprocess.run(["wat2wasm"] + wabt + ["-o", wasm, wat], capture_output=True, text=True)
    if r.returncode:
        return ["FAIL[datapack/%s]: wat2wasm: %s" % (name, r.stderr.strip()[:300])]
    r = subprocess.run(["wasm-interp"] + wabt + ["--run-all-exports", wasm],
                       capture_output=True, text=True)
    with open(ref, "w") as f:
        f.write(r.stdout)
    msgs = []
    for label, flags, env in SETTINGS:
        exe = os.path.join(work, "%s.%s" % (name, label))
        r = subprocess.run([WARMC] + target + flags + ["-o", exe, wasm], capture_output=True,
                           text=True, env=dict(os.environ, WARMC_TABLE_STATS="1", **env))
        if r.returncode:
            msgs.append("FAIL[datapack/%s %s]: warmc: %s" % (name, label, r.stderr.strip()[:300]))
            continue
        stats = dict(re.findall(r"^  (?:rodata|data) (blob|datasegs) +(\d+)$", r.stderr, re.M))
        got = (int(stats.get("blob", -1)), int(stats.get("datasegs", -1)))
        if got != (want_blob, want_table):
            msgs.append("FAIL[datapack/%s %s]: passive blob %d bytes and segment table %d, "
                        "want %d and %d" % ((name, label) + got + (want_blob, want_table)))
        n, bad, m = cmp_mod.compare(ref, exe, "datapack/%s %s" % (name, label), run=run)
        if not n:
            msgs.append("FAIL[datapack/%s %s]: no export answered" % (name, label))
        msgs += m if bad else []
    return msgs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--count", type=int, default=40)
    ap.add_argument("--keep", action="store_true", help="leave the modules in build/datapack")
    a = ap.parse_args()
    target, run, wabt = host()
    work = os.path.join(ROOT, BUILD, "datapack") if a.keep else tempfile.mkdtemp(prefix="datapack")
    os.makedirs(work, exist_ok=True)
    fails = []
    seeds = range(a.seed, a.seed + a.count)
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        for msgs in ex.map(lambda s: run_one(s, work, target, run, wabt), seeds):
            for m in msgs:
                print(m)
            fails += msgs
    if fails:
        return 1
    print("datapack: %d modules, each at %d settings, agree with wasm-interp"
          % (a.count, len(SETTINGS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
