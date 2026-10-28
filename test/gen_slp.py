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
    """The data segment: doubles at 256.., words at 64.. and 1792.."""
    m = bytearray(MEM)
    for i in range(64):
        struct.pack_into('<i', m, 64 + 4 * i, (i * 2654435761 + 12345) & 0x7fffffff)
    for i in range(160):
        struct.pack_into('<d', m, 256 + 8 * i, (i * 37 % 101) / 8.0 - 3.25)
    for i in range(520):
        struct.pack_into('<i', m, 1792 + 4 * i, (i * 40503 + 7) & 0x7fffffff)
    return m


def ld(m, a): return struct.unpack_from('<d', m, a)[0]
def st(m, a, v): struct.pack_into('<d', m, a, v)
def ld32(m, a): return struct.unpack_from('<I', m, a)[0]
def st32(m, a, v): struct.pack_into('<I', m, a, v & M32)
def s32(v): v &= M32; return v - (1 << 32) if v >> 31 else v
def f32(v): return struct.unpack('<f', struct.pack('<f', v))[0]
def ldf(m, a): return struct.unpack_from('<f', m, a)[0]
def stf(m, a, v): struct.pack_into('<f', m, a, v)
def bits_f64(b): return struct.unpack('<d', struct.pack('<Q', b))[0]


class Trap(Exception):
    pass


def ea(x, c, d, n):
    """the address of an n-byte access at (x + c) + d, as wasm makes it: the
    add wraps in 32 bits, the offset does not; past the memory's one page
    it traps"""
    a = ((x + c) & M32) + d
    if a + n > 65536:
        raise Trap
    return a


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


# 31 adds of 32 loads, 63 nodes, within the limit of 64: a vector; 32 adds
# of 33 loads, 65: past it
case('deep31', '(param $p i32) (param $q i32) (result i32)', deep(31), deep_model(31),
     [(0, 0)], [(r'\sadd\.4s\s', 31)])
case('deep32', '(param $p i32) (param $q i32) (result i32)', deep(32), deep_model(32),
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
     [(0, 0, 0), (0, 0, 1)], [(r'\sfadd\.2d\s', 1)])

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

# addresses as x + c, i32 adds the accesses do not fold, which wrap: at
# p = -8 both lanes' adds wrap, to 0 and 8, as the vector's does
case('addrc2', '(param $p i32) (param $q i32) (param $k f64) (result f64)', '''
    (f64.store (i32.add (local.get $p) (i32.const 8)) (f64.mul (local.get $k) (f64.load (i32.add (local.get $p) (i32.const 392)))))
    (f64.store (i32.add (local.get $p) (i32.const 16)) (f64.mul (local.get $k) (f64.load (i32.add (local.get $p) (i32.const 400)))))
    (call $fs (i32.add (local.get $q) (i32.const 8)) (i32.const 2))''',
     lambda m, p, q, k: ([st(m, ea(p, 8 + 8 * j, 0, 8), k * ld(m, ea(p, 392 + 8 * j, 0, 8))) for j in range(2)],
                         fsum(m, ea(q, 8, 0, 8), 2))[-1],
     [(0, 0, 2.5), (40, 40, -1.0), (-8, -8, 0.5)], [(r'\sfmul\.2d\s', 1)])

# lane 0 at p offset 4, the others at adds: (p, 0, 4), (p, 8, 0), ..  At
# p = -8 lane 0 is past the memory, where the vector's address is too, and
# the other lanes' adds wrap to 0..
case('addrmix', '(param $p i32) (param $q i32) (result i32)', '''
    (i32.store offset=4 (local.get $p) (i32.const 21))
    (i32.store (i32.add (local.get $p) (i32.const 8)) (i32.const -3))
    (i32.store (i32.add (local.get $p) (i32.const 12)) (i32.const 77777))
    (i32.store (i32.add (local.get $p) (i32.const 16)) (i32.const 5))
    (call $h (i32.add (local.get $q) (i32.const 4)) (i32.const 4))''',
     lambda m, p, q: ([st32(m, ea(p, c, d, 4), v) for c, d, v in ((0, 4, 21), (8, 0, -3), (12, 0, 77777), (16, 0, 5))],
                      h32(m, q + 4, 4))[-1],
     [(0, 0), (-8, 0)], [(r'\sstu?r\s+q\d+', 1)])

# a 2x4 product's rows, in a loop: each row's four entries two vectors,
# a sum of the row's two elements times rows of the matrix, loaded as
# vectors before the loop, where their scalars were, which go; each
# element splatted once for both halves, and that splat read by lane in
# the multiplies (isel.c's vbyel), no dup.  The loop is unrolled by four,
# and the loop for the rest is a fifth copy: ten groups, twenty multiplies
def rowmat_model(m, p, q, n):
    mm = [[ld(m, q + 256 + 32 * k + 8 * j) for j in range(4)] for k in range(2)]
    for r in range(n):
        x0, x1 = ld(m, q + 512 + 16 * r), ld(m, q + 520 + 16 * r)
        for j in range(4):
            st(m, p + 32 * r + 8 * j, x0 * mm[0][j] + x1 * mm[1][j])
    return fsum(m, q, 4 * n)


def rowmat():
    ms = ''.join('(local $m%d%d f64) ' % (k, j) for k in range(2) for j in range(4))
    loads = ''.join('\n    (local.set $m%d%d (f64.load offset=%d (local.get $q)))' % (k, j, 256 + 32 * k + 8 * j)
                    for k in range(2) for j in range(4))
    stores = ''.join('\n        (f64.store %s (f64.add (f64.mul (local.get $x0) (local.get $m0%d)) '
                     '(f64.mul (local.get $x1) (local.get $m1%d))))'
                     % ('(local.get $o)' if j == 0 else '(i32.add (local.get $o) (i32.const %d))' % (8 * j), j, j)
                     for j in range(4))
    return ('\n    ' + ms + '(local $x0 f64) (local $x1 f64) (local $o i32) (local $r i32)' + loads + '''
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $r) (local.get $n)))
        (local.set $x0 (f64.load offset=512 (i32.add (local.get $q) (i32.shl (local.get $r) (i32.const 4)))))
        (local.set $x1 (f64.load offset=520 (i32.add (local.get $q) (i32.shl (local.get $r) (i32.const 4)))))
        (local.set $o (i32.add (local.get $p) (i32.shl (local.get $r) (i32.const 5))))''' + stores + '''
        (local.set $r (i32.add (local.get $r) (i32.const 1)))
        (br $l)))
    (call $fs (local.get $q) (i32.shl (local.get $n) (i32.const 2)))''')


case('rowmat', '(param $p i32) (param $q i32) (param $n i32) (result f64)', rowmat(), rowmat_model,
     [(0, 0, 4), (64, 64, 3)],
     [(r'\sldr\s+q\d+', 4), (r'\sfmul\.2d\s', 20), (r'\sdup\.2d\s', 0), (r'\sfmul\.2d\s+v\d+, v\d+, v\d+\[0\]', 20),
      (r'\sfmul\s+d\d+', 0),
      (r'\sldr\s+d\d+, \[x\d+, #0x1[0-3]', 0)])

# lane 0 through mem_ptr(p), lane 1 through p + 8: one x
case('ptrmix', '(param $p i32) (param $q i32) (param $k f64) (result f64)', '''
    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (f64.load offset=504 (local.get $p))))
    (f64.store (i32.add (local.get $p) (i32.const 8)) (f64.mul (local.get $k) (f64.load offset=512 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2))''',
     lambda m, p, q, k: ([st(m, p + 8 * j, k * ld(m, p + 504 + 8 * j)) for j in range(2)], fsum(m, q, 2))[-1],
     [(0, 0, 1.75)], [(r'\sfmul\.2d\s', 1)])

# operands in two memories, lane 1's turned round: its first is the second
# memory's, j lanes on from lane 0's first but not in its memory
case('swapmem', '(param $p i32) (param $q i32) (result f64)', '''
    (f64.store offset=0 (local.get $p) (f64.add (f64.load offset=488 (local.get $p)) (f64.load $b offset=488 (local.get $p))))
    (f64.store offset=8 (local.get $p) (f64.add (f64.load $b offset=496 (local.get $p)) (f64.load offset=496 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2))''',
     lambda m, p, q: ([st(m, p + 8 * j, ld(m, p + 488 + 8 * j) + 0.0) for j in range(2)], fsum(m, q, 2))[-1],
     [(0, 0)], [(r'\sfadd\.2d\s', 1)])

# two groups' loads in the block before the stores', a store to the
# second's between: each vector load where its first lane's was, the later
# made first, so that the earlier's place holds
case('outer2', '(param $p i32) (param $q i32) (param $c i32) (param $k f64) (result f64)',
     '\n    (local $a1 f64) (local $b1 f64) (local $a2 f64) (local $b2 f64)'
     '\n    (local.set $a1 (f64.load offset=536 (local.get $p)))'
     '\n    (local.set $b1 (f64.load offset=544 (local.get $p)))'
     '\n    (f64.store offset=552 (local.get $q) (f64.const 11))'
     '\n    (local.set $a2 (f64.load offset=552 (local.get $p)))'
     '\n    (local.set $b2 (f64.load offset=560 (local.get $p)))'
     '\n    (if (local.get $c) (then (f64.store offset=1024 (local.get $q) (f64.const 1))))'
     '\n    (f64.store offset=0 (local.get $p) (f64.add (local.get $a1) (local.get $k)))'
     '\n    (f64.store offset=8 (local.get $p) (f64.add (local.get $b1) (local.get $k)))'
     '\n    (f64.store offset=16 (local.get $p) (f64.mul (local.get $a2) (local.get $k)))'
     '\n    (f64.store offset=24 (local.get $p) (f64.mul (local.get $b2) (local.get $k)))'
     '\n    (f64.add (f64.load offset=1024 (local.get $q)) (call $fs (local.get $q) (i32.const 4)))',
     lambda m, p, q, c, k: (lambda a1, b1: (st(m, q + 552, 11.0), (lambda a2, b2: (
         (st(m, q + 1024, 1.0) if c else None), st(m, p, a1 + k), st(m, p + 8, b1 + k),
         st(m, p + 16, a2 * k), st(m, p + 24, b2 * k), ld(m, q + 1024) + fsum(m, q, 4))[-1])(
         ld(m, p + 552), ld(m, p + 560)))[-1])(ld(m, p + 536), ld(m, p + 544)),
     [(0, 0, 0, 2.0), (0, 0, 1, 2.0)], [(r'\sfadd\.2d\s', 1), (r'\sfmul\.2d\s', 1)])

# two groups of a block adding k: one splat for both
case('sharesplat', '(param $p i32) (param $q i32) (param $k f64) (result f64)',
     lanes_of('(f64.store offset=%d (local.get $p) (f64.add (f64.load offset=%d (local.get $p)) (local.get $k)))',
              4, lambda j: 8 * j, lambda j: 688 + 8 * j)
     + '\n    (call $fs (local.get $q) (i32.const 4))',
     lambda m, p, q, k: ([st(m, p + 8 * j, ld(m, p + 688 + 8 * j) + k) for j in range(4)], fsum(m, q, 4))[-1],
     [(0, 0, 0.25)], [(r'\sfadd\.2d\s', 2), (r'\sdup\.2d\s', 1)])

# -- a splat multiplied in by lane (isel.c's vbyel) ---------------------------

# four floats times k: fmul.4s by element, k lane 0 of its register, the
# splat the second operand (rowmat's are the first)
case('byelem4', '(param $p i32) (param $q i32) (param $n i32) (result i32)',
     '\n    (local $k f32)'
     '\n    (local.set $k (f32.convert_i32_s (local.get $n)))'
     + lanes_of('(f32.store offset=%d (local.get $p) (f32.mul (f32.load offset=%d (local.get $p)) (local.get $k)))',
                4, lambda j: 4 * j, lambda j: 1300 + 4 * j)
     + '\n    (call $h (local.get $q) (i32.const 4))',
     lambda m, p, q, n: ([stf(m, p + 4 * j, f32(ldf(m, p + 1300 + 4 * j) * float(n))) for j in range(4)],
                         h32(m, q, 4))[-1],
     [(0, 0, 3), (16, 16, -5)],
     [(r'\sfmul\.4s\s+v\d+, v\d+, v\d+\[0\]', 1), (r'\sdup\.4s\s', 0)])

# k splatted for the product and added too: the add reads the splat, which
# stays
case('byelemadd', '(param $p i32) (param $q i32) (param $k f64) (result f64)',
     lanes_of('(f64.store offset=%d (local.get $p) (f64.add (f64.mul (local.get $k) (f64.load offset=%d (local.get $p))) '
              '(local.get $k)))', 2, lambda j: 8 * j, lambda j: 584 + 8 * j)
     + '\n    (call $fs (local.get $q) (i32.const 2))',
     lambda m, p, q, k: ([st(m, p + 8 * j, k * ld(m, p + 584 + 8 * j) + k) for j in range(2)], fsum(m, q, 2))[-1],
     [(0, 0, 1.25)], [(r'\sdup\.2d\s', 1), (r'\sfmul\.2d\s+v\d+, v\d+, v\d+\[0\]', 0)])

# the splat in the block before the product's: a dup there
case('byelemblock', '(param $p i32) (param $q i32) (param $c i32) (param $k f64) (result f64)',
     '\n    (local $s v128)'
     '\n    (local.set $s (f64x2.splat (local.get $k)))'
     '\n    (if (local.get $c) (then (f64.store offset=1024 (local.get $q) (f64.const 1))))'
     '\n    (v128.store (local.get $p) (f64x2.mul (local.get $s) (v128.load offset=600 (local.get $p))))'
     '\n    (f64.add (f64.load offset=1024 (local.get $q)) (call $fs (local.get $q) (i32.const 2)))',
     lambda m, p, q, c, k: ((st(m, q + 1024, 1.0) if c else None),
                            [st(m, p + 8 * j, k * ld(m, p + 600 + 8 * j)) for j in range(2)],
                            ld(m, q + 1024) + fsum(m, q, 2))[-1],
     [(0, 0, 0, 0.75), (0, 0, 1, 0.75)], [(r'\sdup\.2d\s', 1)])

# a product of two splats: neither read by lane, the other being no vector
case('byelemtwo', '(param $p i32) (param $q i32) (param $x i32) (result f64)', '''
    (v128.store (local.get $p) (f64x2.mul (f64x2.splat (f64.load offset=616 (local.get $p)))
                                          (f64x2.splat (f64.convert_i32_s (local.get $x)))))
    (call $fs (local.get $q) (i32.const 2))''',
     lambda m, p, q, x: (lambda v: ([st(m, p + 8 * j, v) for j in range(2)], fsum(m, q, 2))[-1])(
         ld(m, p + 616) * float(x)),
     [(0, 0, 7)], [])

# a float's four lanes multiplied as two doubles: lane 0 of its register
# is no double
case('byelemlanes', '(param $p i32) (param $q i32) (result f64)', '''
    (v128.store (local.get $p) (f64x2.mul (f32x4.splat (f32.load offset=1300 (local.get $p)))
                                          (v128.load offset=624 (local.get $p))))
    (call $fs (local.get $q) (i32.const 2))''',
     lambda m, p, q: (lambda b: ([st(m, p + 8 * j, bits_f64(b << 32 | b) * ld(m, p + 624 + 8 * j))
                                  for j in range(2)], fsum(m, q, 2))[-1])(ld32(m, p + 1300)),
     [(0, 0)], [])

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


# a lane's add below lane 0's: lane 0 at p + 8, the others at p offset
# 12..  At p = -8 lane 0's add wraps to 0 and the others' are past the
# memory, where the scalars trap and a vector at 0 would not
case('wrapdown', '(param $p i32) (param $q i32) (result i32)', '''
    (i32.store (i32.add (local.get $p) (i32.const 8)) (i32.const 31))
    (i32.store offset=12 (local.get $p) (i32.const 32))
    (i32.store offset=16 (local.get $p) (i32.const 33))
    (i32.store offset=20 (local.get $p) (i32.const 34))
    (call $h (i32.add (local.get $q) (i32.const 8)) (i32.const 4))''',
     lambda m, p, q: ([st32(m, ea(p, c, d, 4), v) for c, d, v in ((8, 0, 31), (0, 12, 32), (0, 16, 33), (0, 20, 34))],
                      h32(m, q + 8, 4))[-1],
     [(0, 0), (-8, 0)], [(r'\sstu?r\s+q\d+', 0)])

# a store through q, which may be p, between the lanes' loads in the block
# before the stores': lane 1's load may not move up past it
case('outerstore', '(param $p i32) (param $q i32) (param $c i32) (result f64)',
     '\n    (local $a f64) (local $b f64)'
     '\n    (local.set $a (f64.load offset=408 (local.get $p)))'
     '\n    (f64.store offset=416 (local.get $q) (f64.const 5))'
     '\n    (local.set $b (f64.load offset=416 (local.get $p)))'
     '\n    (if (local.get $c) (then (f64.store offset=1024 (local.get $q) (f64.const 1))))'
     '\n    (f64.store offset=0 (local.get $p) (f64.add (local.get $a) (f64.load offset=512 (local.get $p))))'
     '\n    (f64.store offset=8 (local.get $p) (f64.add (local.get $b) (f64.load offset=520 (local.get $p))))'
     '\n    (f64.add (f64.load offset=1024 (local.get $q)) (call $fs (local.get $q) (i32.const 2)))',
     lambda m, p, q, c: (lambda a: (st(m, q + 416, 5.0), (lambda b: (
         (st(m, q + 1024, 1.0) if c else None), st(m, p, a + ld(m, p + 512)), st(m, p + 8, b + ld(m, p + 520)),
         ld(m, q + 1024) + fsum(m, q, 2))[-1])(ld(m, p + 416)))[-1])(ld(m, p + 408)),
     [(0, 0, 0), (0, 0, 1)], [(r'\sfadd\.2d\s', 0)])

# the lanes' products made in the block before the stores': their vector
# would be made where the stores are, which a loop may run more often
case('opblock', '(param $p i32) (param $q i32) (param $c i32) (param $k f64) (result f64)',
     '\n    (local $a f64) (local $b f64)'
     '\n    (local.set $a (f64.mul (local.get $k) (f64.load offset=424 (local.get $p))))'
     '\n    (local.set $b (f64.mul (local.get $k) (f64.load offset=432 (local.get $p))))'
     '\n    (if (local.get $c) (then (f64.store offset=1024 (local.get $q) (f64.const 1))))'
     '\n    (f64.store offset=0 (local.get $p) (f64.add (local.get $a) (f64.load offset=512 (local.get $p))))'
     '\n    (f64.store offset=8 (local.get $p) (f64.add (local.get $b) (f64.load offset=520 (local.get $p))))'
     '\n    (f64.add (f64.load offset=1024 (local.get $q)) (call $fs (local.get $q) (i32.const 2)))',
     lambda m, p, q, c, k: (lambda a, b: ((st(m, q + 1024, 1.0) if c else None), st(m, p, a + ld(m, p + 512)),
                                          st(m, p + 8, b + ld(m, p + 520)), ld(m, q + 1024) + fsum(m, q, 2))[-1])(
         k * ld(m, p + 424), k * ld(m, p + 432)),
     [(0, 0, 1, 1.5)], [(r'\sf(mul|add)\.2d\s', 0)])

# lane 1 loaded first, through an add, and lane 0's add, the vector's
# address, made after it
case('lateaddr', '(param $p i32) (param $q i32) (param $k f64) (result f64)',
     '\n    (local $a f64) (local $b f64)'
     '\n    (local.set $b (f64.load (i32.add (local.get $p) (i32.const 448))))'
     '\n    (local.set $a (f64.load (i32.add (local.get $p) (i32.const 440))))'
     '\n    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (local.get $a)))'
     '\n    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (local.get $b)))'
     '\n    (call $fs (local.get $q) (i32.const 2))',
     lambda m, p, q, k: (lambda b, a: (st(m, p, k * a), st(m, p + 8, k * b), fsum(m, q, 2))[-1])(
         ld(m, p + 448), ld(m, p + 440)),
     [(0, 0, 0.75)], [(r'\sfmul\.2d\s', 0)])

# four words, two in each of two memories, at one x: one vector store would
# write all four to the first
case('twomem', '(param $p i32) (param $q i32) (result i32)', '''
    (i32.store offset=0 (local.get $p) (i32.const 41))
    (i32.store offset=4 (local.get $p) (i32.const 42))
    (i32.store $b offset=8 (local.get $p) (i32.const 43))
    (i32.store $b offset=12 (local.get $p) (i32.const 44))
    (i32.add (i32.mul (call $h (local.get $q) (i32.const 2)) (i32.const 961))
      (i32.add (i32.mul (i32.load $b offset=8 (local.get $q)) (i32.const 31)) (i32.load $b offset=12 (local.get $q))))''',
     lambda m, p, q: (st32(m, p, 41), st32(m, p + 4, 42), h32(m, q, 2) * 961 + 43 * 31 + 44)[-1],
     [(0, 0)], [(r'\sstu?r\s+q\d+', 0)])

# an i64 lane loaded as a sign-extended word, at the next lane's address
case('narrowlane', '(param $p i32) (param $q i32) (param $x i64) (result i32)', '''
    (i64.store offset=0 (local.get $p) (i64.add (local.get $x) (i64.load offset=456 (local.get $p))))
    (i64.store offset=8 (local.get $p) (i64.add (local.get $x) (i64.load32_s offset=464 (local.get $p))))
    (call $h (local.get $q) (i32.const 4))''',
     lambda m, p, q, x: (struct.pack_into('<Q', m, p, (x + struct.unpack_from('<Q', m, p + 456)[0]) & ((1 << 64) - 1)),
                         struct.pack_into('<Q', m, p + 8, (x + struct.unpack_from('<i', m, p + 464)[0]) & ((1 << 64) - 1)),
                         h32(m, q, 4))[-1],
     [(0, 0, 0x100000005)], [(r'\sadd\.2d\s', 0)])

# the lanes' loads in two blocks, lane 1's before a store to its address
# and lane 0's after: one vector load in lane 0's block would read lane 1
# after the store
case('splitload', '(param $p i32) (param $q i32) (param $c i32) (param $k f64) (result f64)',
     '\n    (local $a f64) (local $b f64)'
     '\n    (local.set $b (f64.load offset=480 (local.get $p)))'
     '\n    (if (local.get $c) (then (f64.store offset=480 (local.get $q) (f64.const 7))))'
     '\n    (local.set $a (f64.load offset=472 (local.get $p)))'
     '\n    (f64.store offset=0 (local.get $p) (f64.mul (local.get $k) (local.get $a)))'
     '\n    (f64.store offset=8 (local.get $p) (f64.mul (local.get $k) (local.get $b)))'
     '\n    (call $fs (local.get $q) (i32.const 2))',
     lambda m, p, q, c, k: (lambda b: ((st(m, q + 480, 7.0) if c else None), (lambda a: (
         st(m, p, k * a), st(m, p + 8, k * b), fsum(m, q, 2))[-1])(ld(m, p + 472)))[-1])(ld(m, p + 480)),
     [(0, 0, 0, 1.5), (0, 0, 1, 1.5)], [(r'\sfmul\.2d\s', 0)])

# two groups' loads of one address, a store to it between: two vector loads,
# not one
case('sharestore', '(param $p i32) (param $q i32) (param $k f64) (result f64)',
     '\n    (local $a1 f64) (local $b1 f64) (local $a2 f64) (local $b2 f64)'
     '\n    (local.set $a1 (f64.load offset=520 (local.get $p)))'
     '\n    (local.set $b1 (f64.load offset=528 (local.get $p)))'
     '\n    (f64.store offset=520 (local.get $q) (f64.const 9))'
     '\n    (local.set $a2 (f64.load offset=520 (local.get $p)))'
     '\n    (local.set $b2 (f64.load offset=528 (local.get $p)))'
     '\n    (f64.store offset=0 (local.get $p) (f64.add (local.get $a1) (local.get $k)))'
     '\n    (f64.store offset=8 (local.get $p) (f64.add (local.get $b1) (local.get $k)))'
     '\n    (f64.store offset=16 (local.get $p) (f64.mul (local.get $a2) (local.get $k)))'
     '\n    (f64.store offset=24 (local.get $p) (f64.mul (local.get $b2) (local.get $k)))'
     '\n    (call $fs (local.get $q) (i32.const 4))',
     lambda m, p, q, k: (lambda a1, b1: (st(m, q + 520, 9.0), (lambda a2, b2: (
         st(m, p, a1 + k), st(m, p + 8, b1 + k), st(m, p + 16, a2 * k), st(m, p + 24, b2 * k),
         fsum(m, q, 4))[-1])(ld(m, p + 520), ld(m, p + 528)))[-1])(ld(m, p + 520), ld(m, p + 528)),
     [(0, 0, 2.0), (0, 64, 2.0)], [(r'\sfadd\.2d\s', 1), (r'\sfmul\.2d\s', 1), (r'\sldr\s+q\d+', 2)])

# a 64-bit memory's stores, each after its bounds check, which a store may
# not move past; lane 3's add of a constant past 2^32 is no add in 32 bits
# either, so not 12 bytes on from lane 0 but past the memory, where the
# scalars trap
def traps(*args):
    raise Trap


case('wide64', '(param $x i64) (result i32)', '''
    (i32.store $w offset=0 (local.get $x) (i32.const 51))
    (i32.store $w offset=4 (local.get $x) (i32.const 52))
    (i32.store $w offset=8 (local.get $x) (i32.const 53))
    (i32.store $w (i64.add (local.get $x) (i64.const 0x10000000c)) (i32.const 54))
    (i32.load $w offset=4 (local.get $x))''',
     traps, [(0,)], [(r'\sstu?r\s+q\d+', 0)])

# lanes loaded just after a call that was handed their address, and so
# wrote them one at a time: each scalar load has its store forwarded, where
# a vector load would wait for both stores (rsbench/wide128).  The helper is
# called through the table, by an index the caller is given, so that it
# stays a call: through the address itself, and through p + 640, the lanes
# being at p offset 640
def wr2(m, a, x):
    st(m, a, x)
    st(m, a + 8, x + 1.0)


case('aftercall', '(param $p i32) (param $q i32) (param $s i32) (param $i i32) (param $k f64) (result f64)', '''
    (call_indirect (type $wr) (local.get $s) (local.get $k) (local.get $i))
    (f64.store offset=0 (local.get $p) (f64.load offset=0 (local.get $s)))
    (f64.store offset=8 (local.get $p) (f64.load offset=8 (local.get $s)))
    (call $fs (local.get $q) (i32.const 2))''',
     lambda m, p, q, s, i, k: (wr2(m, s, k), st(m, p, ld(m, s)), st(m, p + 8, ld(m, s + 8)), fsum(m, q, 2))[-1],
     [(0, 0, 640, 0, 1.5)], [(r'\sldu?r\s+q\d+', 0)])

case('aftercall2', '(param $p i32) (param $q i32) (param $i i32) (param $k f64) (result f64)', '''
    (call_indirect (type $wr) (i32.add (local.get $p) (i32.const 640)) (local.get $k) (local.get $i))
    (f64.store offset=0 (local.get $p) (f64.load offset=640 (local.get $p)))
    (f64.store offset=8 (local.get $p) (f64.load offset=648 (local.get $p)))
    (call $fs (local.get $q) (i32.const 2))''',
     lambda m, p, q, i, k: (wr2(m, p + 640, k), st(m, p, ld(m, p + 640)), st(m, p + 8, ld(m, p + 648)),
                            fsum(m, q, 2))[-1],
     [(0, 0, 0, -2.5)], [(r'\sldu?r\s+q\d+', 0)])


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
            try:
                v = model(m, *args)
            except Trap:
                v = None
            if quarter:
                args = args[:-1] + (int(args[-1] * 4),)
            out = ('trap: out of bounds memory access' if v is None else
                   fmt_f64(v) if res == 'f64' else 'i32:%d' % s32(v))
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
;; times x plus a constant, a sum 31 adds deep, offsets past what ldr q
;; encodes, stores in either order, a load of a lane's address between, a
;; store into a lane's source after it is read; addresses as i32 adds, which
;; may wrap, and an offset, a pointer made once and adds in one group, past
;; the memory or wrapping to 0; loads in the block before the stores', also
;; two groups' with a store between, and a 2x4 product's rows in a loop, the
;; matrix loaded as vectors before it and each row's elements splatted once;
;; and a commutative operation's operands turned round in a lane, found by
;; which of them fits lane 0's first -- a factor, a loaded scalar, a load
;; through either of two pointers or from either of two memories, a product,
;; a sum of products.  A float splat only multiplies read, in its block and
;; beside a vector, is read by lane, an fmul by element (isel.c's vbyel);
;; not one an add reads too, one in another block, one beside another splat,
;; nor a float's four lanes taken as two doubles.  Not a sum 32 adds deep,
;; nor where a lane reads what another lane stored, nor a lane's value or
;; load is read elsewhere, nor where a memory.fill, a store or a load that
;; may touch a lane stands between, in the stores' block or the loads', nor
;; for lanes loaded in two blocks, nor one vector load for two loads of an
;; address with a store to it between, nor across a gap, a misaligned
;; offset, two pointers or two memories, nor a 64-bit add of a constant past
;; 2^32, nor a lane's add below lane 0's, which only the scalars trap at
;; where lane 0's wraps, nor where lane 0's address is made after another
;; lane's load, nor lanes loaded just after a call handed their address, or
;; it plus a constant, which wrote them one at a time, nor for loads
;; narrower than their lanes, one lane's alone, from two pointers or a
;; stride apart, nor products made in the block before, nor for stores
;; narrower than their values or of two widths, nor two constants a store
;; pair writes as cheaply, nor lanes of different operations or a difference
;; turned round in one, nor a 64-bit multiply, which has no vector
;; instruction.  At -O2 -fno-mem too, where a lane's load of what another
;; lane stored is not forwarded the value but read, after the store.  Each
;; function stores through $p and reads back through $q, the same address,
;; which forwarding cannot see.  Made by test/gen_slp.py, which works the
;; answers out on a model of the memory.
;;
'''
    out = (head + '\n'.join(shapes) + '\n;;\n;; flags: -O2 -fno-mem\n;;\n' + '\n'.join(runs) +
           '\n\n(module\n')
    out += '  (memory 1)\n  (memory $b 1)\n  (memory $w i64 1)\n  (data (i32.const 0) "%s")\n' % seg
    out += '''\
  (type $wr (func (param i32 f64)))
  (table 1 funcref)
  (elem (i32.const 0) $wr2)
  (func $wr2 (param $a i32) (param $x f64)
    (f64.store offset=0 (local.get $a) (local.get $x))
    (f64.store offset=8 (local.get $a) (f64.add (local.get $x) (f64.const 1))))
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
