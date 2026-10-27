"""Random expressions for the optimiser's integer and float folds, against
wasm-interp: shifts, masks, rotates, ors, xors and adds of a value's bits --
bit and byte reversals written as swaps, whole, rotated, cut short or with a
mask, a shift or a step changed -- and float computations of converted
integers, constants, sums, differences, products, negations and quotients,
truncated, saturating, or compared with a constant, and TinyGo's select tree
round a truncation, its bounds and its NaN test now and then changed.  The
shapes the folds take apart (opt.c bit_map, fold_trunc_exact, fcmp_exact,
select_trunc_sat), and the near misses they must leave alone, side by side
in the same functions.

Each module's functions take two i64s; its export `all` hashes every
function over a table of inputs (random values, 0, 1, -1, the signed and
unsigned bounds, the float bounds' bits), and `f<k>` hashes one, which only
a disagreement reads.  Each module runs under wasm-interp and compiled by
warmc at -O0 and -O2; every answer must be the interpreter's.

  python3 test/gen_folds.py --seed 1 --count 8     # make check runs this
"""
import argparse
import concurrent.futures
import os
import random
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hostbuild

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.environ.get("BUILD", "build")
WARMC = os.path.join(ROOT, BUILD, "warmc")
LEVELS = ["-O0", "-O2"]
M32, M64 = (1 << 32) - 1, (1 << 64) - 1
SWAP32 = [(1, 0x55555555), (2, 0x33333333), (4, 0x0f0f0f0f), (8, 0x00ff00ff), (16, 0x0000ffff)]
SWAP64 = [(1, 0x5555555555555555), (2, 0x3333333333333333), (4, 0x0f0f0f0f0f0f0f0f),
          (8, 0x00ff00ff00ff00ff), (16, 0x0000ffff0000ffff), (32, 0x00000000ffffffff)]


def bits_f64(x): return struct.unpack('<Q', struct.pack('<d', x))[0]
def bits_f32(x): return struct.unpack('<I', struct.pack('<f', x))[0]


class Gen:
    def __init__(self, rng):
        self.r = rng

    def ival(self, t, depth=0):
        """an integer expression of type t over $x and $y"""
        r = self.r
        if depth > 2 or r.random() < 0.3:
            src = r.choice(['(local.get $x)', '(local.get $y)'])
            return src if t == 'i64' else '(i32.wrap_i64 %s)' % src
        return r.choice([self.swaps, self.mixed])(t, depth + 1)

    def swaps(self, t, depth):
        """a ladder of field swaps: a bit reversal, a byte swap or neither"""
        r = self.r
        w = 64 if t == 'i64' else 32
        steps = list(SWAP64 if w == 64 else SWAP32)[:-1]
        if r.random() < 0.5:
            steps = steps[:r.randint(1, len(steps))]
        if r.random() < 0.3:
            steps.reverse()
        e = self.ival(t, depth)
        for k, m in steps:
            if r.random() < 0.08:
                m ^= 1 << r.randrange(w)            # a mask with a bit changed
            if r.random() < 0.05:
                k = (k + r.choice([-1, 1, w])) % (2 * w)   # a shift changed
            join = r.choice(['or'] * 6 + ['xor', 'add'])
            a = '(%s.and (%s.shr_u %s (%s.const %d)) (%s.const %d))' % (t, t, e, t, k, t, m)
            b = '(%s.shl (%s.and %s (%s.const %d)) (%s.const %d))' % (t, t, e, t, m, t, k)
            if r.random() < 0.5:
                a, b = b, a
            e = '(%s.%s %s %s)' % (t, join, a, b)
        rot = r.choice([0, 0, w // 2, r.randrange(w)])
        if rot:
            e = '(%s.%s %s (%s.const %d))' % (t, r.choice(['rotl', 'rotr']), e, t, rot)
        return e

    def mixed(self, t, depth):
        r = self.r
        w = 64 if t == 'i64' else 32
        op = r.choice(['and', 'or', 'xor', 'add', 'shl', 'shr_u', 'shr_s', 'rotl', 'rotr', 'sub'])
        a = self.ival(t, depth)
        if op in ('shl', 'shr_u', 'shr_s', 'rotl', 'rotr'):
            return '(%s.%s %s (%s.const %d))' % (t, op, a, t, r.randrange(2 * w))
        if r.random() < 0.5:
            c = r.choice([r.getrandbits(w), (1 << r.randrange(w)) - 1, 0xff, 0xff00, 1 << (w - 1)])
            return '(%s.%s %s (%s.const %d))' % (t, op, a, t, c - (1 << w) if c >> (w - 1) else c)
        return '(%s.%s %s %s)' % (t, op, a, self.ival(t, depth))

    def fval(self, ft, depth=0):
        """a float expression of type ft: integers converted, constants, + - * neg /"""
        r = self.r
        if depth > 3 or r.random() < 0.35:
            if r.random() < 0.3:
                c = r.choice([r.randint(-20, 20), r.randint(-20, 20) + 0.5, 2.0 ** r.randint(20, 60),
                              16777216.0, 9007199254740992.0, -2147483648.0])
                return '(%s.const %s)' % (ft, float.hex(float(c)))
            it = r.choice(['i32', 'i64'])
            masks = [3, 0xff, 0xffff, 0xffffff, 0x7fffffff, 0xffffffff, (1 << 52) - 1, (1 << 53) + 1, M64]
            m = r.choice(masks if it == 'i64' else masks[:6])
            v = self.ival(it, 3)
            if m != (M64 if it == 'i64' else M32):
                v = '(%s.and %s (%s.const %d))' % (it, v, it, m - (1 << 64) if m >> 63 else m)
            elif r.random() < 0.5:
                v = '(%s.shr_u %s (%s.const %d))' % (it, v, it, r.randint(1, 40) % (64 if it == 'i64' else 32))
            return '(%s.convert_%s_%s %s)' % (ft, it, r.choice('su'), v)
        op = r.choice(['add', 'add', 'sub', 'mul', 'neg', 'div'])
        if op == 'neg':
            return '(%s.neg %s)' % (ft, self.fval(ft, depth + 1))
        return '(%s.%s %s %s)' % (ft, op, self.fval(ft, depth + 1), self.fval(ft, depth + 1))

    def fresult(self):
        """an i64 from a float computation: truncated, or compared"""
        r = self.r
        ft = r.choice(['f64', 'f64', 'f32'])
        e = self.fval(ft)
        kind = r.random()
        if kind < 0.45:
            it, s = r.choice(['i32', 'i64']), r.choice('su')
            v = '(%s.trunc_sat_%s_%s %s)' % (it, ft, s, e)
            return v if it == 'i64' else '(i64.extend_i32_u %s)' % v
        if kind < 0.75:
            c = r.choice([r.randint(-10, 30), r.randint(-10, 30) + 0.5, 'nan', 2.0 ** 31, -1.0])
            cs = 'nan' if c == 'nan' else float.hex(float(c))
            a, b = '(%s.const %s)' % (ft, cs), e
            if r.random() < 0.5:
                a, b = b, a
            if r.random() < 0.15:
                a = b = e
            op = r.choice(['eq', 'ne', 'lt', 'gt', 'le', 'ge'])
            return '(i64.extend_i32_u (%s.%s %s %s))' % (ft, op, a, b)
        return self.tinygo(ft, e)

    def tinygo(self, ft, e):
        """Go's int(x) as TinyGo writes it, a bound or the NaN test now and
        then changed"""
        r = self.r
        lo = -2.0 ** 31
        hi = float.fromhex('0x1.fffffffcp+30') if ft == 'f64' else 2.0 ** 31
        if r.random() < 0.15:
            lo = r.choice([lo + 1, -2.0 ** 31 - 1, 0.0])
        if r.random() < 0.15:
            hi = r.choice([hi + 1, 2.0 ** 31, 100.0])
        nanv = 0 if r.random() > 0.1 else 7
        k = lambda v: '(%s.const %s)' % (ft, float.hex(v))
        x = '(local.get $f%s)' % ft
        t = '(i32.trunc_sat_%s_s %s)' % (ft, x)
        c1 = '(%s.ge %s %s)' % (ft, x, k(lo))
        nan = '(%s.eq %s %s)' % (ft, x, x)
        if r.random() < 0.2:
            nan = '(i32.eqz (%s.ne %s %s))' % (ft, x, x)
        b = '(select (select (i32.const 2147483647) (i32.const -2147483648) %s) (i32.const %d) %s)' % (
            c1, nanv, nan)
        tree = '(select (select %s %s (%s.le %s %s)) %s %s)' % (t, b, ft, x, k(hi), b, c1)
        return '(block (result i64) (local.set $f%s %s) (i64.extend_i32_s %s))' % (ft, e, tree)


def make(seed, nfuncs=24):
    rng = random.Random(seed)
    g = Gen(rng)
    funcs = []
    for k in range(nfuncs):
        if rng.random() < 0.5:
            t = rng.choice(['i32', 'i64'])
            e = g.ival(t)
            body = e if t == 'i64' else '(i64.extend_i32_u %s)' % e
        else:
            body = g.fresult()
        funcs.append('  (func $g%d (param $x i64) (param $y i64) (result i64)\n'
                     '    (local $ff64 f64) (local $ff32 f32)\n    %s)' % (k, body))
    # the inputs: random values and the edges
    vals = [0, 1, M64, 1 << 63, (1 << 63) - 1, M32, 1 << 31, (1 << 31) - 1, 0xff, 3,
            bits_f64(-2.0 ** 31), bits_f64(2.0 ** 31), bits_f64(float('nan')), bits_f32(16777217.0)]
    vals += [rng.getrandbits(64) for _ in range(18)]
    vals += [rng.getrandbits(rng.randint(1, 60)) for _ in range(16)]
    n = len(vals)
    data = b''.join(struct.pack('<Q', v) for v in vals)
    esc = ''.join('\\%02x' % c for c in data)
    hashes = []
    for k in range(nfuncs):
        hashes.append('''  (func $f%d (export "f%d") (result i64)
    (local $i i32) (local $j i32) (local $h i64)
    (loop $a
      (local.set $j (i32.const 0))
      (loop $b
        (local.set $h (i64.add (i64.mul (local.get $h) (i64.const 1000003))
          (call $g%d (i64.load (i32.shl (local.get $i) (i32.const 3)))
                     (i64.load (i32.shl (local.get $j) (i32.const 3))))))
        (local.set $j (i32.add (local.get $j) (i32.const 3)))
        (br_if $b (i32.lt_u (local.get $j) (i32.const %d))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $a (i32.lt_u (local.get $i) (i32.const %d))))
    (local.get $h))''' % (k, k, k, n, n))
    allf = '  (func (export "all") (result i64)\n    (local $h i64)\n' + ''.join(
        '    (local.set $h (i64.add (i64.mul (local.get $h) (i64.const 31)) (call $f%d)))\n' % k
        for k in range(nfuncs)) + '    (local.get $h))'
    return ('(module\n  (memory 1)\n  (data (i32.const 0) "%s")\n%s\n%s\n%s\n)\n'
            % (esc, '\n'.join(funcs), '\n'.join(hashes), allf)), nfuncs


def host():
    """warmc's target flags, how to run what it makes, and wabt's flags, as
    test/hostenv.sh says for this host"""
    r = subprocess.run('. test/hostenv.sh; echo "$WARMC_TARGET_FLAGS"; echo "$WARMC_RUN";'
                       ' echo "$WABT_FLAGS"', shell=True, capture_output=True, text=True,
                       cwd=ROOT, env=dict(os.environ, BUILD=BUILD), executable="/bin/bash")
    lines = r.stdout.split("\n")
    return lines[0].split(), lines[1].split(), lines[2].split()


def u64(text):
    """an i64 answer as the unsigned number: wasm-interp prints it unsigned
    ("all() => i64:12345"), the harness signed ("i64:-1")"""
    try:
        return str(int(text.strip().rsplit(':', 1)[-1]) & M64)
    except ValueError:
        return 'unreadable: ' + text.strip()[:100]


def interp(wasm, export, wabt):
    r = subprocess.run(['wasm-interp'] + wabt + ['--run-export=' + export, wasm],
                       capture_output=True, text=True)
    return u64(r.stdout) if r.returncode == 0 else 'error: ' + r.stderr.strip()


def native(exe, export, run):
    r = subprocess.run(run + [exe, export], capture_output=True, text=True, timeout=60)
    return u64(r.stdout) if r.returncode == 0 else 'error: ' + r.stderr.strip()[-200:]


def run_one(seed, work, target, run, wabt):
    text, nfuncs = make(seed)
    wat = os.path.join(work, 'folds%d.wat' % seed)
    wasm = wat[:-4] + '.wasm'
    open(wat, 'w').write(text)
    r = subprocess.run(['wat2wasm'] + wabt + ['-o', wasm, wat], capture_output=True, text=True)
    if r.returncode:
        return ['seed %d: wat2wasm: %s' % (seed, r.stderr.strip()[:300])]
    want = interp(wasm, 'all', wabt)
    fails = []
    for level in LEVELS:
        exe = hostbuild.exe(os.path.join(work, 'folds%d%s' % (seed, level)))
        r = subprocess.run([WARMC] + target + [level, '-o', exe, wasm], capture_output=True, text=True)
        if r.returncode:
            fails.append('seed %d %s: warmc: %s' % (seed, level, r.stderr.strip()[:300]))
            continue
        got = native(exe, 'all', run)
        if got != want:
            bad = [k for k in range(nfuncs) if native(exe, 'f%d' % k, run) != interp(wasm, 'f%d' % k, wabt)]
            fails.append('seed %d %s: all gives %s, not %s; differing: %s (%s)'
                         % (seed, level, got, want, ' '.join('g%d' % k for k in bad), wat))
    return fails


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--count', type=int, default=8)
    ap.add_argument('--keep', help='a directory to leave the modules in')
    args = ap.parse_args()
    os.chdir(ROOT)
    target, run, wabt = host()
    work = args.keep or tempfile.mkdtemp(prefix='folds')
    os.makedirs(work, exist_ok=True)
    seeds = range(args.seed, args.seed + args.count)
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as ex:
        fails = [f for fs in ex.map(lambda s: run_one(s, work, target, run, wabt), seeds) for f in fs]
    for f in fails:
        print('FAIL[folds]: ' + f)
    if not fails:
        print('folds: %d modules of random folds and near misses agree with wasm-interp at %s'
              % (args.count, ', '.join(LEVELS)))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
