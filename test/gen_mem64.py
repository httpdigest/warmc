"""Random programs on a 64-bit memory past 4 GiB, for the optimiser's
address arithmetic.

A 64-bit memory larger than 4 GiB is where a fold, an alias test or a range
that takes an address mod 2^32 -- right for a 32-bit memory -- goes wrong:
while such memories were refused, an address past 2^32 could not get past
its bounds check, and the slip had nothing to show.  Each module here has a
memory of 65537 pages and one export, `run`, that folds every load into a
checksum.  Its accesses are of every width, at addresses either side of
2^32 and at the end of the memory, through offsets past 2^32, through
constants, locals, opaque calls, sums and loops, stored and loaded in pairs
2^32 apart, with copies, fills, inits and segments among them.  An access
past the end is rare and on purpose, and then the answer is the trap.

Each module runs at each level against -O0, and against wasmtime where it
is installed ($WASMTIME, else `wasmtime` on the PATH).  The memory costs
address space only, the pages the program touches all it writes -- but on
Windows, which commits the memory when it is made, 4 GiB of commit charge,
so there the programs run one at a time.

  python3 test/gen_mem64.py --seed 1 --count 100     # make check runs this
"""
import argparse
import concurrent.futures
import os
import random
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_gosp
import hostbuild

PAGES = 65537
SIZE = PAGES * 65536
LINE = 1 << 32

# (load, store, width, value type)
WIDTHS = [
    ("i32.load8_u", "i32.store8", 1, "i32"), ("i32.load16_u", "i32.store16", 2, "i32"),
    ("i32.load", "i32.store", 4, "i32"), ("i64.load", "i64.store", 8, "i64"),
    ("i64.load8_s", "i64.store8", 1, "i64"), ("i64.load32_u", "i64.store32", 4, "i64"),
    ("f64.load", "f64.store", 8, "f64"), ("f32.load", "f32.store", 4, "f32"),
    ("v128.load", "v128.store", 16, "v128"),
]

class Gen:
    def __init__(self, seed):
        self.r = random.Random(seed)

    def spot(self):
        """an address worth testing: low, either side of 2^32, at the end"""
        r = self.r
        k = r.random()
        if k < 0.3:
            return r.choice([0, 8, 0x100, 0x1000]) + r.randrange(0, 64)
        if k < 0.6:
            return LINE + r.randrange(-64, 64)
        if k < 0.8:
            return SIZE - r.randrange(16, 160)
        return LINE + r.randrange(0, 1 << 16)

    def offset(self):
        return self.r.choice([0, 0, 0, 1, 4, 16, 0x100, LINE, LINE - 16, 0xffff_0000])

    def addr(self, a):
        """a, as an expression the optimiser sees more or less of"""
        r = self.r
        k = r.random()
        if k < 0.3:
            return "(i64.const %d)" % a
        if k < 0.5:
            return "(call $id (i64.const %d))" % a
        if k < 0.7:
            b = r.randrange(0, a + 1)
            return "(i64.add (i64.const %d) (call $id (i64.const %d)))" % (b, a - b)
        if k < 0.85:
            return "(local.tee $p (i64.const %d))" % a
        return ("(i64.or (i64.shl (i64.const %d) (i64.const 32)) (call $id (i64.const %d)))"
                % (a >> 32, a & 0xffffffff))

    def value(self, t):
        v = self.r.getrandbits(64)
        if t == "i32": return "(i32.const %d)" % (v & 0xffffffff)
        if t == "i64": return "(i64.const %d)" % v
        if t == "f32": return "(f32.const %d)" % (v & 0xffff)
        if t == "f64": return "(f64.const %d)" % (v & 0xffffff)
        return "(v128.const i64x2 %d %d)" % (v, self.r.getrandbits(64))

    @staticmethod
    def fold(t, e):
        """the checksum, with the value e of type t mixed in"""
        bits = {"i32": "(i64.extend_i32_u %s)", "i64": "%s",
                "f32": "(i64.extend_i32_u (i32.reinterpret_f32 %s))",
                "f64": "(i64.reinterpret_f64 %s)",
                "v128": "(i64x2.extract_lane 1 %s)"}[t] % e
        return "(local.set $h (i64.xor (i64.rotl (local.get $h) (i64.const 5)) %s))" % bits

    def access(self, load):
        r = self.r
        ld, st, w, t = r.choice(WIDTHS)
        off = self.offset()
        base = self.spot()
        a = base - off if base >= off and r.random() < 0.8 else base
        if a + off + w > SIZE and r.random() < 0.9:
            a = max(0, SIZE - w - off - r.randrange(0, 64))
        e = self.addr(a)
        if load:
            return self.fold(t, "(%s offset=%d %s)" % (ld, off, e))
        return "(%s offset=%d %s %s)" % (st, off, e, self.value(t))

    def aliased(self):
        """two addresses 2^32 apart: each stored, both loaded"""
        r = self.r
        ld, st, w, t = r.choice(WIDTHS)
        lo = r.randrange(0, 1 << 16)
        high = r.choice(["(%s %s %s)" % (st, self.addr(lo + LINE), self.value(t)),
                         "(%s offset=%d %s %s)" % (st, LINE, self.addr(lo), self.value(t))])
        return ["(%s %s %s)" % (st, self.addr(lo), self.value(t)), high,
                self.fold(t, "(%s %s)" % (ld, self.addr(lo))),
                self.fold(t, "(%s %s)" % (ld, self.addr(lo + LINE)))]

    def loop(self, k):
        """a run of accesses at a stride, the shape LICM and the vectoriser take"""
        r = self.r
        base = self.spot()
        cnt, stride = r.randrange(1, 40), r.choice([1, 2, 4, 8])
        base = min(base, SIZE - cnt * stride - 16)
        ld, st, w, t = r.choice([x for x in WIDTHS if x[2] <= stride])
        at = "(i64.add %s (i64.mul (local.get $i) (i64.const %d)))" % (self.addr(base), stride)
        store = "(%s %s %s)" % (st, at, self.value(t)) if r.random() < 0.5 else "(nop)"
        return ("(local.set $i (i64.const 0))\n    (loop $l%d\n      %s\n      %s\n"
                "      (local.set $i (i64.add (local.get $i) (i64.const 1)))\n"
                "      (br_if $l%d (i64.lt_u (local.get $i) (i64.const %d))))"
                % (k, store, self.fold(t, "(%s %s)" % (ld, at)), k, cnt))

    def module(self):
        r = self.r
        body = []
        for _ in range(r.randrange(8, 40)):
            k = r.random()
            if k < 0.35:
                body.append(self.access(False))
            elif k < 0.7:
                body.append(self.access(True))
            elif k < 0.78:
                d, s, n = self.spot(), self.spot(), r.randrange(0, 48)
                body.append("(memory.copy %s %s (i64.const %d))"
                            % (self.addr(min(d, SIZE - n)), self.addr(min(s, SIZE - n)), n))
            elif k < 0.85:
                d, n = self.spot(), r.randrange(0, 48)
                body.append("(memory.fill %s (i32.const %d) (i64.const %d))"
                            % (self.addr(min(d, SIZE - n)), r.randrange(256), n))
            elif k < 0.87:
                body.extend(self.aliased())
            elif k < 0.9:
                body.append("(memory.init $seg %s (i32.const %d) (i32.const %d))"
                            % (self.addr(min(self.spot(), SIZE - 8)), r.randrange(0, 4),
                               r.randrange(0, 5)))
            else:
                body.append(self.loop(len(body)))
        for _ in range(4):
            body.append(self.fold("i64", "(i64.load %s)" % self.addr(min(self.spot(), SIZE - 8))))
        segs = ['(data (i64.const %d) "%s")'
                % (min(self.spot(), SIZE - 8),
                   "".join("\\%02x" % r.randrange(256) for _ in range(r.randrange(1, 8))))
                for _ in range(r.randrange(0, 3))]
        return ('(module\n  (memory i64 %d)\n  %s\n  (data $seg "\\de\\ad\\be\\ef\\01")\n'
                '  (func $id (param i64) (result i64) (local.get 0))\n'
                '  (func (export "run") (result i64)\n'
                '    (local $h i64) (local $p i64) (local $i i64)\n    %s\n'
                '    (local.get $h)))\n' % (PAGES, "\n  ".join(segs), "\n    ".join(body)))

def answer(r):
    """a warmc run's answer: the checksum, or that it trapped"""
    if r.returncode == 1 and "trap:" in r.stderr:
        return "trap"
    if r.returncode == 0 and r.stdout.startswith("i64:"):
        return "%d" % (int(r.stdout.strip()[4:]) & (2**64 - 1))
    return "exit %d: %s" % (r.returncode, (r.stderr.strip().splitlines() or [""])[0][:80])

def check(seed, levels, tmp, build, target, wasmtime, run):
    sh = lambda c: subprocess.run(c, capture_output=True, text=True, timeout=120)
    d = os.path.join(tmp, "s%d" % seed)
    os.makedirs(d, exist_ok=True)
    wat, wasm = os.path.join(d, "m.wat"), os.path.join(d, "m.wasm")
    with open(wat, "w") as f:
        f.write(Gen(seed).module())
    r = sh([os.path.join(build, "tools", "bin", "wasm-tools"), "parse", wat, "-o", wasm])
    if r.returncode:
        return "seed %d: not assembled: %s" % (seed, r.stderr.strip()[:200])
    got = {}
    for lv in levels:
        exe = os.path.join(d, "m%s%s" % (lv.replace("-", "_"), ".exe" if hostbuild.WINDOWS else ""))
        r = sh([os.path.join(build, "warmc")] + lv.split() + target + ["-o", exe, wasm])
        got[lv] = answer(sh(run + [exe, "run"])) if r.returncode == 0 else \
            "not compiled: " + (r.stderr.strip().splitlines() or [""])[0][:100]
    want = {}
    if wasmtime:
        r = sh([wasmtime, "run", "-W", "memory64=y", "--invoke", "run", wasm])
        want["wasmtime"] = ("trap" if r.returncode else
                            "%d" % (int(r.stdout.strip()) & (2**64 - 1)))
    want[levels[0]] = got[levels[0]]
    bad = ["%s %s, %s %s" % (lv, g, w, v) for lv, g in got.items()
           for w, v in want.items() if g != v and lv != w]
    if bad:
        return "seed %d: %s (--seed %d --count 1 --keep DIR keeps it)" % (
            seed, "; ".join(sorted(set(bad))[:4]), seed)
    return None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--count", type=int, default=100)
    ap.add_argument("--levels", default="-O0,-O1,-O2,-O3")
    # Windows commits a memory's pages when it is made: each program is 4 GiB
    # of commit charge while it runs, so there they run one at a time
    ap.add_argument("--jobs", type=int,
                    default=1 if hostbuild.WINDOWS else min(4, os.cpu_count() or 1))
    ap.add_argument("--keep", help="a directory to leave the modules in")
    a = ap.parse_args()
    os.chdir(os.path.dirname(HERE))
    build = os.environ.get("BUILD", "build")
    target = gen_gosp.target_flags(build)
    run = os.environ.get("WARMC_RUN", "").split()
    wasmtime = os.environ.get("WASMTIME") or shutil.which("wasmtime")
    levels = [lv for lv in a.levels.split(",") if lv]
    tmp = a.keep or tempfile.mkdtemp(prefix="mem64-")
    try:
        with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
            fails = [x for x in ex.map(lambda s: check(s, levels, tmp, build, target,
                                                         wasmtime, run),
                                       range(a.seed, a.seed + a.count)) if x]
    finally:
        if not a.keep:
            shutil.rmtree(tmp, ignore_errors=True)
    for f in fails:
        print("FAIL[mem64] " + f)
    print("mem64: %d programs on a 64-bit memory past 4 GiB at %s, against %s; %d failed"
          % (a.count, " ".join(levels), "wasmtime and " + levels[0] if wasmtime else levels[0],
             len(fails)))
    return 1 if fails else 0

if __name__ == "__main__":
    sys.exit(main())
