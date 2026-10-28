"""test/cases_opt/slp.wat: a block's stores to adjacent addresses whose values
are alike lane by lane are one vector computation and one vector store
(opt.c pass_slp).  Answers are worked out here, each function run on a
model of its memory in Python: wasm's wrapping integers and IEEE doubles.

  python3 test/gen_slp.py test/cases_opt/slp.wat
"""
import struct, sys

M32 = (1 << 32) - 1
MEM = 4096


def data():
    """The data segment: doubles at 256.., words at 64.. and 1024.."""
    m = bytearray(MEM)
    for i in range(64):
        struct.pack_into('<i', m, 64 + 4 * i, (i * 2654435761 + 12345) & 0x7fffffff)
    for i in range(160):
        struct.pack_into('<d', m, 256 + 8 * i, (i * 37 % 101) / 8.0 - 3.25)
    for i in range(64):
        struct.pack_into('<i', m, 1792 + 4 * i, (i * 40503 + 7) & 0x7fffffff)
    return m


def ld(m, a): return struct.unpack_from('<d', m, a)[0]
def st(m, a, v): struct.pack_into('<d', m, a, v)
def ld32(m, a): return struct.unpack_from('<I', m, a)[0]
def st32(m, a, v): struct.pack_into('<I', m, a, v & M32)
def s32(v): v &= M32; return v - (1 << 32) if v >> 31 else v


def h32(m, a, n):
    """the readback: words at a.. hashed, as the module's $h does"""
    h = 0
    for j in range(n):
        h = (h * 31 + ld32(m, a + 4 * j)) & M32
    return h


def fsum(m, a, n):
    """doubles at a.., each sum doubled before the next is added, as the
    module's $fs does: no multiply, so as not to count in the shapes"""
    s = 0.0
    for j in range(n):
        s = (s + s) + ld(m, a + 8 * j)
    return s


def wat_f64(v):
    return repr(float(v)) if v == v else 'nan'


def fmt_f64(v):
    """an f64 result as run_optcases.py reads one: its bits"""
    return 'f64:0x%016x' % struct.unpack('<Q', struct.pack('<d', v))[0]


CASES = []


def case(name, params, body, model, runs, shapes):
    CASES.append((name, params, body, model, runs, shapes))


# -- vectorised ------------------------------------------------------------

case('scale2', '(param $p i32) (param $q i32) (param $k f64) (result f64)', '''
    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (f64.load offset=8 (local.get $p))))
    (f64.store offset=16 (local.get $p) (f64.mul (local.get $k) (f64.load offset=16 (local.get $p))))
    (call $fs (i32.add (local.get $q) (i32.const 8)) (i32.const 2))''',
     lambda m, p, q, k: (st(m, p + 8, k * ld(m, p + 8)), st(m, p + 16, k * ld(m, p + 16)),
                         fsum(m, q + 8, 2))[-1],
     [(256, 256, 1.5), (512, 512, -2.0)],
     [(r'\sfmul\.2d\s', 1), (r'\sfmul\s+d\d+', 0), (r'\sldur\s+q\d+', 1)])

case('add4', '(param $p i32) (param $q i32) (result i32)', '''
    (i32.store offset=0 (local.get $p) (i32.add (i32.load offset=1792 (local.get $p)) (i32.load offset=1856 (local.get $p))))
    (i32.store offset=4 (local.get $p) (i32.add (i32.load offset=1796 (local.get $p)) (i32.load offset=1860 (local.get $p))))
    (i32.store offset=8 (local.get $p) (i32.add (i32.load offset=1800 (local.get $p)) (i32.load offset=1864 (local.get $p))))
    (i32.store offset=12 (local.get $p) (i32.add (i32.load offset=1804 (local.get $p)) (i32.load offset=1868 (local.get $p))))
    (call $h (local.get $q) (i32.const 4))''',
     lambda m, p, q: ([st32(m, p + 4 * j, ld32(m, p + 1792 + 4 * j) + ld32(m, p + 1856 + 4 * j)) for j in range(4)],
                      h32(m, q, 4))[-1],
     [(0, 0), (16, 16)], [(r'\sadd\.4s\s', 1)])

case('copy4', '(param $p i32) (param $q i32) (result i32)', '''
    (i32.store offset=0 (local.get $p) (i32.load offset=64 (local.get $p)))
    (i32.store offset=4 (local.get $p) (i32.load offset=68 (local.get $p)))
    (i32.store offset=8 (local.get $p) (i32.load offset=72 (local.get $p)))
    (i32.store offset=12 (local.get $p) (i32.load offset=76 (local.get $p)))
    (call $h (local.get $q) (i32.const 4))''',
     lambda m, p, q: ([st32(m, p + 4 * j, ld32(m, p + 64 + 4 * j)) for j in range(4)], h32(m, q, 4))[-1],
     [(0, 0), (8, 8)], [(r'\sldu?r\s+q\d+', 1), (r'\sstu?r\s+q\d+', 1)])

case('consts4', '(param $p i32) (param $q i32) (result i32)', '''
    (i32.store offset=0 (local.get $p) (i32.const 11))
    (i32.store offset=4 (local.get $p) (i32.const -7))
    (i32.store offset=8 (local.get $p) (i32.const 123456789))
    (i32.store offset=12 (local.get $p) (i32.const 0))
    (call $h (local.get $q) (i32.const 4))''',
     lambda m, p, q: ([st32(m, p + 4 * j, c) for j, c in enumerate((11, -7, 123456789, 0))], h32(m, q, 4))[-1],
     [(0, 0), (4, 4)], [(r'\sstu?r\s+q\d+', 1)])

case('splat4', '(param $p i32) (param $q i32) (param $x i32) (result i32)', '''
    (i32.store offset=0 (local.get $p) (i32.add (local.get $x) (i32.load offset=1792 (local.get $p))))
    (i32.store offset=4 (local.get $p) (i32.add (local.get $x) (i32.load offset=1796 (local.get $p))))
    (i32.store offset=8 (local.get $p) (i32.add (local.get $x) (i32.load offset=1800 (local.get $p))))
    (i32.store offset=12 (local.get $p) (i32.add (local.get $x) (i32.load offset=1804 (local.get $p))))
    (call $h (local.get $q) (i32.const 4))''',
     lambda m, p, q, x: ([st32(m, p + 4 * j, x + ld32(m, p + 1792 + 4 * j)) for j in range(4)], h32(m, q, 4))[-1],
     [(0, 0, 5), (32, 32, -100000)], [(r'\sadd\.4s\s', 1), (r'\sdup\.4s\s', 1)])

case('sub2', '(param $p i32) (param $q i32) (result f64)', '''
    (f64.store offset=0 (local.get $p) (f64.sub (f64.load offset=256 (local.get $p)) (f64.load offset=512 (local.get $p))))
    (f64.store offset=8 (local.get $p) (f64.sub (f64.load offset=264 (local.get $p)) (f64.load offset=520 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2))''',
     lambda m, p, q: ([st(m, p + 8 * j, ld(m, p + 256 + 8 * j) - ld(m, p + 512 + 8 * j)) for j in range(2)],
                      fsum(m, q, 2))[-1],
     [(0, 0), (24, 24)], [(r'\sfsub\.2d\s', 1)])

case('swapped', '(param $p i32) (param $q i32) (param $k f64) (result f64)', '''
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))
    (f64.store offset=8 (local.get $p) (f64.mul (f64.load offset=264 (local.get $p)) (local.get $k)))
    (call $fs (local.get $q) (i32.const 2))''',
     lambda m, p, q, k: ([st(m, p + 8 * j, k * ld(m, p + 256 + 8 * j)) for j in range(2)], fsum(m, q, 2))[-1],
     [(0, 0, 0.5), (40, 40, 3.0)], [(r'\sfmul\.2d\s', 1)])

case('chain4', '(param $p i32) (param $q i32) (param $k i32) (result i32)', '''
    (i32.store offset=0 (local.get $p) (i32.mul (local.get $k) (i32.add (i32.load offset=1792 (local.get $p)) (i32.load offset=1856 (local.get $p)))))
    (i32.store offset=4 (local.get $p) (i32.mul (local.get $k) (i32.add (i32.load offset=1796 (local.get $p)) (i32.load offset=1860 (local.get $p)))))
    (i32.store offset=8 (local.get $p) (i32.mul (local.get $k) (i32.add (i32.load offset=1800 (local.get $p)) (i32.load offset=1864 (local.get $p)))))
    (i32.store offset=12 (local.get $p) (i32.mul (local.get $k) (i32.add (i32.load offset=1804 (local.get $p)) (i32.load offset=1868 (local.get $p)))))
    (call $h (local.get $q) (i32.const 4))''',
     lambda m, p, q, k: ([st32(m, p + 4 * j, k * (ld32(m, p + 1792 + 4 * j) + ld32(m, p + 1856 + 4 * j)))
                          for j in range(4)], h32(m, q, 4))[-1],
     [(0, 0, 3), (16, 16, -77)], [(r'\smul\.4s\s', 1), (r'\sadd\.4s\s', 1)])

case('highoff', '(param $p i32) (param $q i32) (result f64)', '''
    (f64.store offset=1272 (local.get $p) (f64.add (f64.load offset=264 (local.get $p)) (f64.load offset=520 (local.get $p))))
    (f64.store offset=1280 (local.get $p) (f64.add (f64.load offset=272 (local.get $p)) (f64.load offset=528 (local.get $p))))
    (call $fs (i32.add (local.get $q) (i32.const 1272)) (i32.const 2))''',
     lambda m, p, q: ([st(m, p + 1272 + 8 * j, ld(m, p + 264 + 8 * j) + ld(m, p + 520 + 8 * j)) for j in range(2)],
                      fsum(m, q + 1272, 2))[-1],
     [(0, 0), (8, 8)], [(r'\sfadd\.2d\s', 1)])

case('revorder', '(param $p i32) (param $q i32) (param $k f64) (result f64)', '''
    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (f64.load offset=304 (local.get $p))))
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (f64.load offset=296 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2))''',
     lambda m, p, q, k: ([st(m, p + 8 * j, k * ld(m, p + 296 + 8 * j)) for j in (1, 0)], fsum(m, q, 2))[-1],
     [(0, 0, 1.25), (16, 16, -0.5)], [(r'\sfmul\.2d\s', 1)])

def deep(n):
    """four words, each the sum of n + 1 loads"""
    lanes = []
    for j in range(4):
        e = '(i32.load offset=%d (local.get $p))' % (1792 + 4 * j)
        for t in range(1, n + 1):
            e = '(i32.add %s (i32.load offset=%d (local.get $p)))' % (e, 1792 + 64 * t + 4 * j)
        lanes.append('    (i32.store offset=%d (local.get $p) %s)' % (4 * j, e))
    return '\n' + '\n'.join(lanes) + '\n    (call $h (local.get $q) (i32.const 4))'


def deep_model(n):
    def f(m, p, q):
        for j in range(4):
            v = sum(ld32(m, p + 1792 + 64 * t + 4 * j) for t in range(n + 1))
            st32(m, p + 4 * j, v)
        return h32(m, q, 4)
    return f


def lanes_of(fmt, n, *offs):
    """one store a lane: fmt filled with the lane's offsets"""
    return ''.join('\n    ' + fmt % tuple(o(j) for o in offs) for j in range(n))


# five adds deep, the limit's depth less one: a vector; six: past it
case('deep5', '(param $p i32) (param $q i32) (result i32)', deep(5), deep_model(5),
     [(0, 0)], [(r'\sadd\.4s\s', 5)])
case('deep6', '(param $p i32) (param $q i32) (result i32)', deep(6), deep_model(6),
     [(0, 0)], [(r'\sadd\.4s\s', 0)])

case('isub4', '(param $p i32) (param $q i32) (result i32)',
     lanes_of('(i32.store offset=%d (local.get $p) (i32.sub (i32.load offset=%d (local.get $p)) '
              '(i32.load offset=%d (local.get $p))))', 4,
              lambda j: 4 * j, lambda j: 1792 + 4 * j, lambda j: 1856 + 4 * j)
     + '\n    (call $h (local.get $q) (i32.const 4))',
     lambda m, p, q: ([st32(m, p + 4 * j, ld32(m, p + 1792 + 4 * j) - ld32(m, p + 1856 + 4 * j))
                       for j in range(4)], h32(m, q, 4))[-1],
     [(0, 0)], [(r'\ssub\.4s\s', 1)])


def bit_case(opn, sym, fn):
    case('%s4' % opn, '(param $p i32) (param $q i32) (result i32)',
         lanes_of('(i32.store offset=%d (local.get $p) (i32.' + opn + ' (i32.load offset=%d (local.get $p)) '
                  '(i32.load offset=%d (local.get $p))))', 4,
                  lambda j: 4 * j, lambda j: 1792 + 4 * j, lambda j: 1920 + 4 * j)
         + '\n    (call $h (local.get $q) (i32.const 4))',
         lambda m, p, q: ([st32(m, p + 4 * j, fn(ld32(m, p + 1792 + 4 * j), ld32(m, p + 1920 + 4 * j)))
                           for j in range(4)], h32(m, q, 4))[-1],
         [(0, 0)], [(r'\s%s\.16b\s' % sym, 1)])


bit_case('and', 'and', lambda a, b: a & b)
bit_case('or', 'orr', lambda a, b: a | b)
bit_case('xor', 'eor', lambda a, b: a ^ b)


def wasm_min(a, b):
    if a != a or b != b: return float('nan')
    if a == b == 0.0: return a if str(a).startswith('-') else b
    return min(a, b)


def wasm_max(a, b):
    if a != a or b != b: return float('nan')
    if a == b == 0.0: return b if str(a).startswith('-') else a
    return max(a, b)


def float_case(opn, sym, fn):
    case('f%s2' % opn, '(param $p i32) (param $q i32) (result f64)',
         lanes_of('(f64.store offset=%d (local.get $p) (f64.' + opn + ' (f64.load offset=%d (local.get $p)) '
                  '(f64.load offset=%d (local.get $p))))', 2,
                  lambda j: 8 * j, lambda j: 256 + 8 * j, lambda j: 1024 + 8 * j)
         + '\n    (call $fs (local.get $q) (i32.const 2))',
         lambda m, p, q: ([st(m, p + 8 * j, fn(ld(m, p + 256 + 8 * j), ld(m, p + 1024 + 8 * j)))
                           for j in range(2)], fsum(m, q, 2))[-1],
         [(0, 0), (8, 8)], [(r'\s%s\.2d\s' % sym, 1)])


float_case('div', 'fdiv', lambda a, b: a / b)
float_case('min', 'fmin', wasm_min)
float_case('max', 'fmax', wasm_max)

case('bytes4', '(param $p i32) (param $q i32) (result i32)',
     lanes_of('(i32.store offset=%d (local.get $p) (i32.add (i32.load8_u offset=%d (local.get $p)) '
              '(i32.load offset=%d (local.get $p))))', 4,
              lambda j: 4 * j, lambda j: 64 + j, lambda j: 1792 + 4 * j)
     + '\n    (call $h (local.get $q) (i32.const 4))',
     lambda m, p, q: ([st32(m, p + 4 * j, m[p + 64 + j] + ld32(m, p + 1792 + 4 * j)) for j in range(4)],
                      h32(m, q, 4))[-1],
     [(0, 0)], [(r'\sadd\.4s\s', 0)])

case('twoload', '(param $p i32) (param $q i32) (param $r i32) (result i32)',
     '\n    (local $a i32) (local $b i32) (local $c i32) (local $d i32)'
     '\n    (local.set $a (i32.add (i32.load offset=1792 (local.get $q)) (i32.load offset=1856 (local.get $p))))'
     '\n    (local.set $b (i32.add (i32.load offset=1796 (local.get $r)) (i32.load offset=1860 (local.get $p))))'
     '\n    (local.set $c (i32.add (i32.load offset=1800 (local.get $q)) (i32.load offset=1864 (local.get $p))))'
     '\n    (local.set $d (i32.add (i32.load offset=1804 (local.get $r)) (i32.load offset=1868 (local.get $p))))'
     '\n    (i32.store offset=0 (local.get $p) (local.get $a))'
     '\n    (i32.store offset=4 (local.get $p) (local.get $b))'
     '\n    (i32.store offset=8 (local.get $p) (local.get $c))'
     '\n    (i32.store offset=12 (local.get $p) (local.get $d))'
     '\n    (call $h (local.get $p) (i32.const 4))',
     lambda m, p, q, r: (lambda v: ([st32(m, p + 4 * j, v[j]) for j in range(4)], h32(m, p, 4))[-1])(
         [ld32(m, (q if j % 2 == 0 else r) + 1792 + 4 * j) + ld32(m, p + 1856 + 4 * j) for j in range(4)]),
     [(0, 0, 64)], [(r'\sadd\.4s\s', 0)])

case('strided', '(param $p i32) (param $q i32) (result f64)',
     '\n    (f64.store offset=0 (local.get $p) (f64.add (f64.load offset=256 (local.get $p)) '
     '(f64.load offset=512 (local.get $p))))'
     '\n    (f64.store offset=8 (local.get $p) (f64.add (f64.load offset=272 (local.get $p)) '
     '(f64.load offset=520 (local.get $p))))'
     '\n    (call $fs (local.get $q) (i32.const 2))',
     lambda m, p, q: (st(m, p, ld(m, p + 256) + ld(m, p + 512)), st(m, p + 8, ld(m, p + 272) + ld(m, p + 520)),
                      fsum(m, q, 2))[-1],
     [(0, 0)], [(r'\sfadd\.2d\s', 0)])

case('crossblock', '(param $p i32) (param $q i32) (param $c i32) (result f64)',
     '\n    (local $a f64) (local $b f64)'
     '\n    (local.set $a (f64.load offset=256 (local.get $p)))'
     '\n    (local.set $b (f64.load offset=264 (local.get $p)))'
     '\n    (if (local.get $c) (then (f64.store offset=1024 (local.get $q) (f64.const 1))))'
     '\n    (f64.store offset=0 (local.get $p) (f64.add (local.get $a) (f64.load offset=512 (local.get $p))))'
     '\n    (f64.store offset=8 (local.get $p) (f64.add (local.get $b) (f64.load offset=520 (local.get $p))))'
     '\n    (f64.add (f64.load offset=1024 (local.get $q)) (call $fs (local.get $q) (i32.const 2)))',
     lambda m, p, q, c: (lambda a, b: ((st(m, q + 1024, 1.0) if c else None), st(m, p, a + ld(m, p + 512)),
                                       st(m, p + 8, b + ld(m, p + 520)), ld(m, q + 1024) + fsum(m, q, 2))[-1])(
         ld(m, p + 256), ld(m, p + 264)),
     [(0, 0, 0), (0, 0, 1)], [])

case('copyshared', '(param $p i32) (param $q i32) (result f64)',
     '\n    (local $a f64) (local $b f64)'
     '\n    (local.set $a (f64.load offset=256 (local.get $p)))'
     '\n    (local.set $b (f64.load offset=264 (local.get $p)))'
     '\n    (f64.store offset=0 (local.get $p) (local.get $a))'
     '\n    (f64.store offset=8 (local.get $p) (local.get $b))'
     '\n    (f64.add (f64.mul (local.get $a) (local.get $b)) (call $fs (local.get $q) (i32.const 2)))',
     lambda m, p, q: (lambda a, b: (st(m, p, a), st(m, p + 8, b), a * b + fsum(m, q, 2))[-1])(
         ld(m, p + 256), ld(m, p + 264)),
     [(0, 0)], [(r'\sstu?r\s+q\d+', 0)])

case('mul2', '(param $p i32) (param $q i32) (param $x i64) (result i32)',
     '\n    (i64.store offset=0 (local.get $p) (i64.mul (local.get $x) (i64.load offset=256 (local.get $p))))'
     '\n    (i64.store offset=8 (local.get $p) (i64.mul (local.get $x) (i64.load offset=264 (local.get $p))))'
     '\n    (call $h (local.get $q) (i32.const 4))',
     lambda m, p, q, x: ([struct.pack_into('<Q', m, p + 8 * j,
                                           (x * struct.unpack_from('<Q', m, p + 256 + 8 * j)[0]) & ((1 << 64) - 1))
                          for j in range(2)], h32(m, q, 4))[-1],
     [(0, 0, 0x100000003)], [(r'\sstu?r\s+q\d+', 0)])

case('subswap', '(param $p i32) (param $q i32) (param $k f64) (result f64)',
     '\n    (f64.store offset=0 (local.get $p) (f64.sub (f64.load offset=256 (local.get $p)) (local.get $k)))'
     '\n    (f64.store offset=8 (local.get $p) (f64.sub (local.get $k) (f64.load offset=264 (local.get $p))))'
     '\n    (call $fs (local.get $q) (i32.const 2))',
     lambda m, p, q, k: (st(m, p, ld(m, p + 256) - k), st(m, p + 8, k - ld(m, p + 264)), fsum(m, q, 2))[-1],
     [(0, 0, 2.25)], [(r'\sfsub\.2d\s', 0)])

case('dupload', '(param $p i32) (param $q i32) (param $k f64) (result f64)',
     '\n    (local $a f64) (local $b f64) (local $t f64)'
     '\n    (local.set $a (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))'
     '\n    (local.set $t (f64.load offset=264 (local.get $q)))'
     '\n    (local.set $b (f64.mul (local.get $k) (f64.load offset=264 (local.get $p))))'
     '\n    (f64.store offset=0 (local.get $p) (local.get $a))'
     '\n    (f64.store offset=8 (local.get $p) (local.get $b))'
     '\n    (f64.add (local.get $t) (call $fs (local.get $q) (i32.const 2)))',
     lambda m, p, q, k: (lambda a, t, b: (st(m, p, a), st(m, p + 8, b), t + fsum(m, q, 2))[-1])(
         k * ld(m, p + 256), ld(m, q + 264), k * ld(m, p + 264)),
     [(0, 0, 1.75)], [(r'\sfmul\.2d\s', 1)])

case('latewrite', '(param $p i32) (param $q i32) (param $k f64) (result f64)',
     '\n    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))'
     '\n    (f64.store offset=256 (local.get $p) (f64.const 9))'
     '\n    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (f64.load offset=264 (local.get $p))))'
     '\n    (f64.add (f64.load offset=256 (local.get $q)) (call $fs (local.get $q) (i32.const 2)))',
     lambda m, p, q, k: (st(m, p, k * ld(m, p + 256)), st(m, p + 256, 9.0), st(m, p + 8, k * ld(m, p + 264)),
                         ld(m, q + 256) + fsum(m, q, 2))[-1],
     [(0, 0, -1.25)], [(r'\sfmul\.2d\s', 1)])

case('axpy2', '(param $p i32) (param $q i32) (param $k f64) (result f64)',
     lanes_of('(f64.store offset=%d (local.get $p) (f64.add (f64.mul (local.get $k) '
              '(f64.load offset=%d (local.get $p))) (f64.const 0.5)))', 2,
              lambda j: 8 * j, lambda j: 256 + 8 * j)
     + '\n    (call $fs (local.get $q) (i32.const 2))',
     lambda m, p, q, k: ([st(m, p + 8 * j, k * ld(m, p + 256 + 8 * j) + 0.5) for j in range(2)],
                         fsum(m, q, 2))[-1],
     [(0, 0, 1.5)], [(r'\sfmul\.2d\s', 1), (r'\sfadd\.2d\s', 1)])

case('swap4', '(param $p i32) (param $q i32) (result i32)',
     lanes_of('(i32.store offset=%d (local.get $p) (i32.add (i32.load offset=%d (local.get $p)) '
              '(i32.load offset=%d (local.get $p))))', 4,
              lambda j: 4 * j, lambda j: (1920 if j == 2 else 1792) + 4 * j,
              lambda j: (1792 if j == 2 else 1920) + 4 * j)
     + '\n    (call $h (local.get $q) (i32.const 4))',
     lambda m, p, q: ([st32(m, p + 4 * j, ld32(m, p + 1792 + 4 * j) + ld32(m, p + 1920 + 4 * j))
                       for j in range(4)], h32(m, q, 4))[-1],
     [(0, 0)], [(r'\sadd\.4s\s', 1)])

case('swapped2', '(param $p i32) (param $q i32) (param $k f64) (result f64)',
     '\n    (f64.store offset=0 (local.get $p) (f64.mul (f64.load offset=312 (local.get $p)) (local.get $k)))'
     '\n    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (f64.load offset=320 (local.get $p))))'
     '\n    (call $fs (local.get $q) (i32.const 2))',
     lambda m, p, q, k: ([st(m, p + 8 * j, k * ld(m, p + 312 + 8 * j)) for j in range(2)], fsum(m, q, 2))[-1],
     [(0, 0, 0.25)], [(r'\sfmul\.2d\s', 1)])

case('swapsplat', '(param $p i32) (param $q i32) (result f64)',
     '\n    (local $s f64)'
     '\n    (local.set $s (f64.load offset=600 (local.get $p)))'
     '\n    (f64.store offset=0 (local.get $p) (f64.mul (local.get $s) (f64.load offset=328 (local.get $p))))'
     '\n    (f64.store offset=8 (local.get $p) (f64.mul (f64.load offset=336 (local.get $p)) (local.get $s)))'
     '\n    (call $fs (local.get $q) (i32.const 2))',
     lambda m, p, q: (lambda sv: ([st(m, p + 8 * j, sv * ld(m, p + 328 + 8 * j)) for j in range(2)],
                                  fsum(m, q, 2))[-1])(ld(m, p + 600)),
     [(0, 0)], [(r'\sfmul\.2d\s', 1)])

case('swapptr', '(param $p i32) (param $q i32) (result f64)',
     '\n    (local $a f64) (local $b f64)'
     '\n    (local.set $a (f64.add (f64.load offset=344 (local.get $p)) (f64.load offset=344 (local.get $q))))'
     '\n    (local.set $b (f64.add (f64.load offset=352 (local.get $q)) (f64.load offset=352 (local.get $p))))'
     '\n    (f64.store offset=0 (local.get $p) (local.get $a))'
     '\n    (f64.store offset=8 (local.get $p) (local.get $b))'
     '\n    (call $fs (local.get $q) (i32.const 2))',
     lambda m, p, q: (lambda a, b: (st(m, p, a), st(m, p + 8, b), fsum(m, q, 2))[-1])(
         ld(m, p + 344) + ld(m, q + 344), ld(m, q + 352) + ld(m, p + 352)),
     [(0, 0), (0, 96)], [(r'\sfadd\.2d\s', 1)])

case('swapmul4', '(param $p i32) (param $q i32) (param $k i32) (result i32)',
     lanes_of('(i32.store offset=%d (local.get $p) (i32.add %s %s))', 4,
              lambda j: 4 * j,
              lambda j: ('(local.get $k)' if j == 2 else
                         '(i32.mul (i32.load offset=%d (local.get $p)) (i32.load offset=%d (local.get $p)))'
                         % (1792 + 4 * j, 1856 + 4 * j)),
              lambda j: ('(i32.mul (i32.load offset=%d (local.get $p)) (i32.load offset=%d (local.get $p)))'
                         % (1792 + 4 * j, 1856 + 4 * j) if j == 2 else '(local.get $k)'))
     + '\n    (call $h (local.get $q) (i32.const 4))',
     lambda m, p, q, k: ([st32(m, p + 4 * j, ld32(m, p + 1792 + 4 * j) * ld32(m, p + 1856 + 4 * j) + k)
                          for j in range(4)], h32(m, q, 4))[-1],
     [(0, 0, 9)], [(r'\smla\.4s\s', 1)])

def prod(x, y):
    return '(i32.mul (i32.load offset=%d (local.get $p)) (i32.load offset=%d (local.get $p)))' % (x, y)


# a.b + c.d four times; lane 1's product of a and b turned round, lane 2's sum
case('swapdeep4', '(param $p i32) (param $q i32) (result i32)',
     lanes_of('(i32.store offset=%d (local.get $p) (i32.add %s %s))', 4,
              lambda j: 4 * j,
              lambda j: (prod(1920 + 4 * j, 1984 + 4 * j) if j == 2 else
                         prod(1856 + 4 * j, 1792 + 4 * j) if j == 1 else prod(1792 + 4 * j, 1856 + 4 * j)),
              lambda j: prod(1792 + 4 * j, 1856 + 4 * j) if j == 2 else prod(1920 + 4 * j, 1984 + 4 * j))
     + '\n    (call $h (local.get $q) (i32.const 4))',
     lambda m, p, q: ([st32(m, p + 4 * j, ld32(m, p + 1792 + 4 * j) * ld32(m, p + 1856 + 4 * j) +
                            ld32(m, p + 1920 + 4 * j) * ld32(m, p + 1984 + 4 * j)) for j in range(4)],
                      h32(m, q, 4))[-1],
     [(0, 0)], [(r'\sm(ul|la)\.4s\s', 2)])

# s.x four times, s a load, lane 2 turned round
case('swapsplat4', '(param $p i32) (param $q i32) (result i32)',
     '\n    (local $s i32)'
     '\n    (local.set $s (i32.load offset=2000 (local.get $p)))'
     + lanes_of('(i32.store offset=%d (local.get $p) (i32.mul %s %s))', 4,
                lambda j: 4 * j,
                lambda j: ('(i32.load offset=%d (local.get $p))' % (1792 + 4 * j)) if j == 2 else '(local.get $s)',
                lambda j: '(local.get $s)' if j == 2 else ('(i32.load offset=%d (local.get $p))' % (1792 + 4 * j)))
     + '\n    (call $h (local.get $q) (i32.const 4))',
     lambda m, p, q: (lambda sv: ([st32(m, p + 4 * j, sv * ld32(m, p + 1792 + 4 * j)) for j in range(4)],
                                  h32(m, q, 4))[-1])(ld32(m, p + 2000)),
     [(0, 0)], [(r'\smul\.4s\s', 1)])

case('twoptr2', '(param $p i32) (param $q i32) (result i32)', '''
    (i32.store offset=0 (local.get $p) (i32.const 21))
    (i32.store offset=4 (local.get $p) (i32.const 22))
    (i32.store offset=8 (local.get $p) (i32.const 23))
    (i32.store offset=12 (local.get $q) (i32.const 24))
    (i32.store offset=40 (local.get $q) (i32.const 25))
    (call $h (local.get $q) (i32.const 4))''',
     lambda m, p, q: ([st32(m, a, c) for a, c in ((p, 21), (p + 4, 22), (p + 8, 23), (q + 12, 24), (q + 40, 25))],
                      h32(m, q, 4))[-1],
     [(0, 0), (0, 128)], [(r'\sstu?r\s+q\d+', 0)])

# two groups whose code interleaves: one goes in a later round
case('interleave', '(param $p i32) (param $q i32) (param $k f64) (result f64)',
     '\n    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))'
     '\n    (f64.store offset=16 (local.get $p) (f64.add (local.get $k) (f64.load offset=272 (local.get $p))))'
     '\n    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (f64.load offset=264 (local.get $p))))'
     '\n    (f64.store offset=24 (local.get $p) (f64.add (local.get $k) (f64.load offset=280 (local.get $p))))'
     '\n    (call $fs (local.get $q) (i32.const 4))',
     lambda m, p, q, k: (st(m, p, k * ld(m, p + 256)), st(m, p + 16, k + ld(m, p + 272)),
                         st(m, p + 8, k * ld(m, p + 264)), st(m, p + 24, k + ld(m, p + 280)),
                         fsum(m, q, 4))[-1],
     [(0, 0, 1.25)], [(r'\sfmul\.2d\s', 1), (r'\sfadd\.2d\s', 1)])

# two groups one after the other, made in one round, the later first; the
# later's factor made between them
case('twogroups', '(param $p i32) (param $q i32) (param $k f64) (result f64)',
     lanes_of('(i32.store offset=%d (local.get $p) (i32.add (i32.load offset=%d (local.get $p)) '
              '(i32.load offset=%d (local.get $p))))', 4,
              lambda j: 512 + 4 * j, lambda j: 1792 + 4 * j, lambda j: 1856 + 4 * j)
     + '\n    (local.set $k (f64.add (local.get $k) (f64.const 1)))'
     + lanes_of('(f64.store offset=%d (local.get $p) (f64.mul (local.get $k) (f64.load offset=%d (local.get $p))))',
                2, lambda j: 8 * j, lambda j: 256 + 8 * j)
     + '\n    (f64.add (f64.convert_i32_s (call $h (i32.add (local.get $q) (i32.const 512)) (i32.const 4)))'
       ' (call $fs (local.get $q) (i32.const 2)))',
     lambda m, p, q, k: ([st32(m, p + 512 + 4 * j, ld32(m, p + 1792 + 4 * j) + ld32(m, p + 1856 + 4 * j))
                          for j in range(4)],
                         [st(m, p + 8 * j, (k + 1.0) * ld(m, p + 256 + 8 * j)) for j in range(2)],
                         float(s32(h32(m, q + 512, 4))) + fsum(m, q, 2))[-1],
     [(0, 0, 0.5)], [(r'\sadd\.4s\s', 1), (r'\sfmul\.2d\s', 1)])

# one group between another's loads and its stores
case('nested', '(param $p i32) (param $q i32) (result i32)',
     '\n    (local $a i32) (local $b i32) (local $c i32) (local $d i32)'
     + ''.join('\n    (local.set $%s (i32.xor (i32.load offset=%d (local.get $p)) (i32.load offset=%d (local.get $p))))'
               % (v, 1792 + 4 * j, 1856 + 4 * j) for j, v in enumerate('abcd'))
     + lanes_of('(i32.store offset=%d (local.get $p) (i32.load offset=%d (local.get $p)))', 3,
                lambda j: 512 + 4 * j, lambda j: 64 + 4 * j)
     + '\n    (i32.store offset=524 (local.get $p) (i32.const 99))'
     + '\n    (i32.store offset=524 (local.get $p) (i32.load offset=76 (local.get $p)))'
     + ''.join('\n    (i32.store offset=%d (local.get $p) (local.get $%s))' % (4 * j, v) for j, v in enumerate('abcd'))
     + '\n    (i32.xor (call $h (local.get $q) (i32.const 4)) (call $h (i32.add (local.get $q) (i32.const 512)) (i32.const 4)))',
     lambda m, p, q: (lambda v: ([st32(m, p + 512 + 4 * j, ld32(m, p + 64 + 4 * j)) for j in range(3)],
                                 st32(m, p + 524, 99), st32(m, p + 524, ld32(m, p + 76)),
                                 [st32(m, p + 4 * j, v[j]) for j in range(4)],
                                 h32(m, q, 4) ^ h32(m, q + 512, 4))[-1])(
         [ld32(m, p + 1792 + 4 * j) ^ ld32(m, p + 1856 + 4 * j) for j in range(4)]),
     [(0, 0)], [(r'\seor\.16b\s', 1), (r'\sstu?r\s+q\d+', 2)])

# a group whose lane 1 reads what another group, whose code it crosses,
# stores: at -O2 -fno-mem, where that load is not forwarded, it must wait
# for the other to be made, and then not go at all (at -O2 the forwarded
# value is read twice, and neither goes)
case('clash', '(param $p i32) (param $q i32) (param $k f64) (result f64)',
     '\n    (local $a f64) (local $b f64) (local $c f64) (local $d f64)'
     '\n    (local.set $a (f64.load offset=256 (local.get $p)))'
     '\n    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (local.get $a)))'
     '\n    (local.set $c (f64.load offset=0 (local.get $p)))'
     '\n    (local.set $b (f64.load offset=264 (local.get $p)))'
     '\n    (f64.store offset=16 (local.get $p) (f64.mul (local.get $k) (local.get $b)))'
     '\n    (local.set $d (f64.load offset=8 (local.get $p)))'
     '\n    (f64.store offset=1024 (local.get $p) (f64.add (local.get $k) (local.get $c)))'
     '\n    (f64.store offset=1032 (local.get $p) (f64.add (local.get $k) (local.get $d)))'
     '\n    (f64.add (f64.load offset=1032 (local.get $q)) (call $fs (i32.add (local.get $q) (i32.const 8)) (i32.const 2)))',
     lambda m, p, q, k: (lambda a: (st(m, p + 8, k * a),
                                    (lambda c, b: (st(m, p + 16, k * b),
                                                   (lambda d: (st(m, p + 1024, k + c), st(m, p + 1032, k + d),
                                                               ld(m, q + 1032) + fsum(m, q + 8, 2))[-1])(
                                                       ld(m, p + 8)))[-1])(ld(m, p), ld(m, p + 264)))[-1])(
         ld(m, p + 256)),
     [(0, 0, 1.5)], [])

# -- left alone --------------------------------------------------------------

case('raw', '(param $p i32) (param $q i32) (param $k f64) (result f64)', '''
    (f64.store offset=264 (local.get $p) (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))
    (f64.store offset=272 (local.get $p) (f64.mul (local.get $k) (f64.load offset=264 (local.get $p))))
    (call $fs (i32.add (local.get $q) (i32.const 264)) (i32.const 2))''',
     lambda m, p, q, k: (st(m, p + 264, k * ld(m, p + 256)), st(m, p + 272, k * ld(m, p + 264)),
                         fsum(m, q + 264, 2))[-1],
     [(0, 0, 2.0), (8, 8, -1.5)], [(r'\sfmul\.2d\s', 0)])

case('multiuse', '(param $p i32) (param $q i32) (param $k f64) (result f64)', '''
    (local $t f64)
    (local.set $t (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))
    (f64.store offset=0 (local.get $p) (local.get $t))
    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (f64.load offset=264 (local.get $p))))
    (f64.add (local.get $t) (call $fs (local.get $q) (i32.const 2)))''',
     lambda m, p, q, k: (lambda t: (st(m, p, t), st(m, p + 8, k * ld(m, p + 264)), t + fsum(m, q, 2))[-1])(
         k * ld(m, p + 256)),
     [(0, 0, 1.5)], [(r'\sfmul\.2d\s', 0)])

case('fill', '(param $p i32) (param $q i32) (param $k f64) (result f64)', '''
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))
    (memory.fill (i32.add (local.get $q) (i32.const 264)) (i32.const 0) (i32.const 8))
    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (f64.load offset=264 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2))''',
     lambda m, p, q, k: (st(m, p, k * ld(m, p + 256)), m.__setitem__(slice(q + 264, q + 272), bytes(8)),
                         st(m, p + 8, k * ld(m, p + 264)), fsum(m, q, 2))[-1],
     [(0, 0, 3.0)], [(r'\sfmul\.2d\s', 0)])

case('loadpast', '(param $p i32) (param $q i32) (param $k f64) (result f64)', '''
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))
    (f64.store offset=264 (local.get $p) (f64.const 7))
    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (f64.load offset=264 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2))''',
     lambda m, p, q, k: (st(m, p, k * ld(m, p + 256)), st(m, p + 264, 7.0), st(m, p + 8, k * ld(m, p + 264)),
                         fsum(m, q, 2))[-1],
     [(0, 0, 0.75)], [(r'\sfmul\.2d\s', 0)])

case('storeread', '(param $p i32) (param $q i32) (param $k f64) (result f64)', '''
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))
    (f64.store offset=512 (local.get $p) (f64.load offset=0 (local.get $q)))
    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (f64.load offset=264 (local.get $p))))
    (f64.add (f64.load offset=512 (local.get $q)) (call $fs (local.get $q) (i32.const 2)))''',
     lambda m, p, q, k: (st(m, p, k * ld(m, p + 256)), st(m, p + 512, ld(m, q)), st(m, p + 8, k * ld(m, p + 264)),
                         ld(m, q + 512) + fsum(m, q, 2))[-1],
     [(0, 0, 2.5)], [(r'\sfmul\.2d\s', 0)])

case('waw', '(param $p i32) (param $q i32) (param $k f64) (result f64)', '''
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))
    (f64.store offset=0 (local.get $q) (f64.const 5))
    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (f64.load offset=264 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2))''',
     lambda m, p, q, k: (st(m, p, k * ld(m, p + 256)), st(m, q, 5.0), st(m, p + 8, k * ld(m, p + 264)),
                         fsum(m, q, 2))[-1],
     [(0, 0, -3.0)], [(r'\sfmul\.2d\s', 0)])

case('misalign', '(param $p i32) (param $q i32) (result f64)', '''
    (f64.store offset=0 (local.get $p) (f64.load offset=256 (local.get $p)))
    (f64.store offset=12 (local.get $p) (f64.load offset=264 (local.get $p)))
    (call $fs (local.get $q) (i32.const 3))''',
     lambda m, p, q: (st(m, p, ld(m, p + 256)), st(m, p + 12, ld(m, p + 264)), fsum(m, q, 3))[-1],
     [(0, 0)], [(r'\sstu?r\s+q\d+', 0)])

case('mixedsize', '(param $p i32) (param $q i32) (result i32)', '''
    (i32.store offset=0 (local.get $p) (i32.const 5))
    (i32.store offset=4 (local.get $p) (i32.const 6))
    (i32.store offset=8 (local.get $p) (i32.const 7))
    (i64.store offset=12 (local.get $p) (i64.const 0x1122334455667788))
    (call $h (local.get $q) (i32.const 5))''',
     lambda m, p, q: ([st32(m, p + o, c) for o, c in ((0, 5), (4, 6), (8, 7))],
                      struct.pack_into('<Q', m, p + 12, 0x1122334455667788), h32(m, q, 5))[-1],
     [(0, 0)], [(r'\sstu?r\s+q\d+', 0)])

case('gap', '(param $p i32) (param $q i32) (result i32)', '''
    (i32.store offset=0 (local.get $p) (i32.load offset=64 (local.get $p)))
    (i32.store offset=4 (local.get $p) (i32.load offset=68 (local.get $p)))
    (i32.store offset=12 (local.get $p) (i32.load offset=76 (local.get $p)))
    (i32.store offset=16 (local.get $p) (i32.load offset=80 (local.get $p)))
    (call $h (local.get $q) (i32.const 5))''',
     lambda m, p, q: ([st32(m, p + o, ld32(m, p + 64 + o)) for o in (0, 4, 12, 16)], h32(m, q, 5))[-1],
     [(0, 0)], [(r'\sstu?r\s+q\d+', 0)])

case('twoptr', '(param $p i32) (param $q i32) (result i32)', '''
    (i32.store offset=0 (local.get $p) (i32.load offset=64 (local.get $p)))
    (i32.store offset=4 (local.get $q) (i32.load offset=68 (local.get $p)))
    (i32.store offset=8 (local.get $p) (i32.load offset=72 (local.get $p)))
    (i32.store offset=12 (local.get $q) (i32.load offset=76 (local.get $p)))
    (call $h (local.get $q) (i32.const 4))''',
     lambda m, p, q: ([st32(m, (q if o in (4, 12) else p) + o, ld32(m, p + 64 + o)) for o in (0, 4, 8, 12)],
                      h32(m, q, 4))[-1],
     [(0, 0), (0, 128)], [(r'\sstu?r\s+q\d+', 0)])

case('narrow', '(param $p i32) (param $q i32) (param $x i64) (result i32)', '''
    (i64.store32 offset=0 (local.get $p) (i64.add (local.get $x) (i64.const 1)))
    (i64.store32 offset=4 (local.get $p) (i64.add (local.get $x) (i64.const 2)))
    (i64.store32 offset=8 (local.get $p) (i64.add (local.get $x) (i64.const 3)))
    (i64.store32 offset=12 (local.get $p) (i64.add (local.get $x) (i64.const 4)))
    (call $h (local.get $q) (i32.const 4))''',
     lambda m, p, q, x: ([st32(m, p + 4 * j, x + j + 1) for j in range(4)], h32(m, q, 4))[-1],
     [(0, 0, 0x123456789)], [(r'\sstu?r\s+q\d+', 0)])

case('consts2', '(param $p i32) (param $q i32) (result i32)', '''
    (i64.store offset=0 (local.get $p) (i64.const 3))
    (i64.store offset=8 (local.get $p) (i64.const 9))
    (call $h (local.get $q) (i32.const 4))''',
     lambda m, p, q: (struct.pack_into('<qq', m, p, 3, 9), h32(m, q, 4))[-1],
     [(0, 0)], [(r'\sstu?r\s+q\d+', 0)])

case('mixed', '(param $p i32) (param $q i32) (param $k f64) (result f64)', '''
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (f64.load offset=256 (local.get $p))))
    (f64.store offset=8 (local.get $p) (f64.add (local.get $k) (f64.load offset=264 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2))''',
     lambda m, p, q, k: (st(m, p, k * ld(m, p + 256)), st(m, p + 8, k + ld(m, p + 264)), fsum(m, q, 2))[-1],
     [(0, 0, 1.5)], [(r'\sf(mul|add)\.2d\s', 0)])


def main():
    m0 = data()
    seg = ''.join('\\%02x' % b for b in m0)
    runs, shapes, funcs = [], [], []
    for name, params, body, model, rs, sh in CASES:
        res = 'f64' if params.endswith('(result f64)') else 'i32'
        quarter = '(param $k f64)' in params
        if quarter:
            # a factor k as an i32 of quarters: the runner passes integers
            params = params.replace('(param $k f64)', '(param $ki i32)')
            lines = body.split('\n')
            n = 1
            while n < len(lines) and lines[n].strip().startswith('(local '):
                n += 1
            body = '\n'.join(lines[:n] + ['    (local $k f64)',
                              '    (local.set $k (f64.div (f64.convert_i32_s (local.get $ki)) (f64.const 4)))']
                             + lines[n:])
        for args in rs:
            m = bytearray(m0)
            if quarter:
                args = args[:-1] + (int(args[-1] * 4),)
                v = model(m, *(args[:-1] + (args[-1] / 4.0,)))
            else:
                v = model(m, *args)
            out = fmt_f64(v) if res == 'f64' else 'i32:%d' % s32(v)
            a = ' '.join(repr(float(x)) if isinstance(x, float) else str(x) for x in args)
            runs.append(';; run: %s %s => %s' % (name, a, out))
        for pat, n in sh:
            shapes.append(';; shape: %s %s=%d' % (name, pat, n))
        funcs.append('  (func $%s (export "%s") %s%s)' % (name, name, params, body))
    head = '''\
;; A block's stores to adjacent addresses, as many as fill a vector, whose
;; values are alike lane by lane -- one operation of operands themselves
;; alike: loads of adjacent addresses, one value in every lane, constants --
;; are one vector computation and one vector store (opt.c pass_slp): a pair
;; of doubles scaled by k, four words added, subtracted, and'ed, or'd,
;; xor'd, copied or set to constants, a scalar added to four words, a
;; difference, quotient, minimum and maximum of doubles, a sum times k, k
;; times x plus a constant, a sum five adds deep, offsets past what ldr q
;; encodes, stores in either order, a load of a lane's address between, a
;; store into a lane's source after it is read; and a commutative
;; operation's operands turned round in a lane, found by which of them fits
;; lane 0's first -- a factor, a loaded scalar, a load through either of two
;; pointers, a product, a sum of products.  Not a sum six adds deep, nor
;; where a lane reads what another lane stored, nor a lane's value or load
;; is read elsewhere, nor where a memory.fill, a store or a load that may
;; touch a lane stands between, nor across a gap, a misaligned offset or two
;; pointers, nor for loads narrower than their lanes, from two pointers, a
;; stride apart or in another block, nor for stores narrower than their
;; values or of two widths, nor two constants a store pair writes as
;; cheaply, nor lanes of different operations or a difference turned round
;; in one, nor a 64-bit multiply, which has no vector instruction.  At -O2
;; -fno-mem too, where a lane's load of what another lane stored is not
;; forwarded the value but read, after the store.  Each function stores
;; through $p and reads back through $q, the same address, which forwarding
;; cannot see.  Made by test/gen_slp.py, which works the answers out on a
;; model of the memory.
;;
'''
    out = (head + '\n'.join(shapes) + '\n;;\n;; flags: -O2 -fno-mem\n;;\n' + '\n'.join(runs) +
           '\n\n(module\n')
    out += '  (memory 1)\n  (data (i32.const 0) "%s")\n' % seg
    out += '''\
  (func $h (param $a i32) (param $n i32) (result i32)
    (local $r i32)
    (block $done
      (loop $l
        (br_if $done (i32.eqz (local.get $n)))
        (local.set $r (i32.add (i32.mul (local.get $r) (i32.const 31)) (i32.load (local.get $a))))
        (local.set $a (i32.add (local.get $a) (i32.const 4)))
        (local.set $n (i32.sub (local.get $n) (i32.const 1)))
        (br $l)))
    (local.get $r))
  (func $fs (param $a i32) (param $n i32) (result f64)
    (local $r f64) (local $j i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $j) (local.get $n)))
        (local.set $r (f64.add (f64.add (local.get $r) (local.get $r))
          (f64.load (i32.add (local.get $a) (i32.shl (local.get $j) (i32.const 3))))))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (local.get $r))
'''
    out += '\n'.join(funcs) + '\n)\n'
    open(sys.argv[1], 'w').write(out)


main()
