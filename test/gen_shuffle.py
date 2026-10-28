"""test/cases_opt/shuffle.wat: i8x16.shuffle of every shape a64 has one
instruction for, at every lane size and order of the operands, and of
shapes that take two, lanes put in, and a table lookup (isel.c sh_plan).
Each function shuffles two vectors of distinct bytes and returns a hash of
the result, worked out here.

  python3 test/gen_shuffle.py test/cases_opt/shuffle.wat
"""
import random, sys

M32 = (1 << 32) - 1
SRC = [(0x41 + 7 * i) & 0xff for i in range(32)]   # x's bytes, then y's


def apply(mask):
    return [SRC[m] for m in mask]


def h32(bs):
    """the readback: the 16 bytes as four words, hashed as $h does"""
    h = 0
    for w in range(4):
        h = (h * 31 + int.from_bytes(bytes(bs[4 * w:4 * w + 4]), 'little')) & M32
    return h


def lanes(e):
    return 16 >> e


def zip_(op, e, a, b):
    """a and b: 0 x, 1 y; the byte places of zip/uzp/trn at lanes of 1 << e"""
    E, n = 1 << e, lanes(e)
    out = []
    for L in range(n):
        if op in ('zip1', 'zip2'):
            S = b if L & 1 else a
            s = (n // 2 if op == 'zip2' else 0) + L // 2
        elif op in ('uzp1', 'uzp2'):
            s = 2 * L + (op == 'uzp2')
            S = a if s < n else b
            s %= n
        else:
            S = b if L & 1 else a
            s = (L & ~1) + (op == 'trn2')
        out += [16 * S + s * E + k for k in range(E)]
    return out


def ext(a, b, k):
    src = [16 * a + i for i in range(16)] + [16 * b + i for i in range(16)]
    return src[k:k + 16]


def rev(c, e, a):
    C, E = 1 << c, 1 << e
    out = []
    for i in range(16):
        c0, w = i // C * C, i % C
        out.append(16 * a + c0 + (C // E - 1 - w // E) * E + w % E)
    return out


def dup(e, l, a):
    E = 1 << e
    return [16 * a + l * E + i % E for i in range(16)]


CASES = []   # (name, mask, same, shapes)


def case(name, mask, shapes=(), same=False):
    CASES.append((name, mask, same, list(shapes)))


SZ = ['16b', '8h', '4s', '2d']
NOTBL = (r'\stbl\.16b\s', 0)
# no index vector made: it is built of movk's
NOIDX = (r'\smovk\s', 0)
# one instruction: of all a plan may emit, just one
ONE = (r'\s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s', 1)
ORDERS = [('xy', 0, 1), ('yx', 1, 0), ('xx', 0, 0), ('yy', 1, 1)]

# x with itself first, then x and y at the same places: the plan kept for
# the one is not the other's (compiled in order, -j1)
case('same_first', zip_('zip1', 0, 0, 1), [NOTBL], same=True)
for op in ('zip1', 'zip2', 'uzp1', 'uzp2', 'trn1', 'trn2'):
    for e in range(4):
        for on, a, b in ORDERS:
            m = zip_(op, e, a, b)
            # (one of a vector with itself at 64-bit lanes is a dup)
            case('%s_%d_%s' % (op, e, on), m,
                 [NOTBL, NOIDX, ONE, (r'\s(zip|uzp|trn)[12]\.%s\s|\sdup\.%s\s' % (SZ[e], SZ[e]), 1)])
for k in range(1, 16):
    for on, a, b in ORDERS[:3]:
        case('ext_%d_%s' % (k, on), ext(a, b, k), [NOTBL, NOIDX, ONE, (r'\sext\.16b\s', 1)])
for c in range(1, 4):
    for e in range(c):
        for a in (0, 1):
            case('rev%d_%d_%d' % (8 << c, e, a), rev(c, e, a), [NOTBL, ONE, (r'\srev%d\.' % (8 << c), 1)])
for e in range(4):
    for l in sorted({0, lanes(e) - 1, lanes(e) // 2}):
        for a in (0, 1):
            case('dup_%d_%d_%d' % (e, l, a), dup(e, l, a), [NOTBL, ONE, (r'\sdup\.%s\s' % SZ[e], 1)])
case('idx', list(range(16)), [NOTBL])
case('idy', list(range(16, 32)), [NOTBL])
# lanes put in: x with y's lane 1 at 0, at each size; two lanes; a base made
# by zip with one lane changed
for e in range(4):
    E = 1 << e
    m = list(range(16))
    m[0:E] = [16 + E + i for i in range(E)]
    case('ins1_%d' % e, m, [NOTBL, (r'\smov\.%s\s|\sins\s' % ('b' if e == 0 else 'hsd'[e - 1]), 1)])
m = list(range(16, 32)); m[4:8] = [8, 9, 10, 11]; m[12:16] = [0, 1, 2, 3]
case('ins2', m, [NOTBL])
# y's bytes 17-20 at x's lane 0, which is no aligned word of y; and y's
# word 1 with its middle bytes turned round: each a lane of bytes put in
m = list(range(16)); m[0:4] = [17, 18, 19, 20]
case('ins_unaligned', m)
m = list(range(16)); m[0:4] = [20, 22, 21, 23]
case('ins_perm', m)
m = zip_('zip1', 2, 0, 1); m[12:16] = [28, 29, 30, 31]
case('zipins', m, [NOTBL])
# two steps: four words reversed (rev64 then ext); bytes reversed whole
case('rev4s', [12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3], [NOTBL])
case('rev16b', list(range(15, -1, -1)), [NOTBL])
case('swaphalves_rev', [12, 13, 14, 15, 8, 9, 10, 11, 20, 21, 22, 23, 16, 17, 18, 19], [NOTBL])
# one vector shuffled with itself: x's indices and y's are one register
case('same_zip', [0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23], [NOTBL], same=True)
case('same_mixed', [3, 19, 0, 30, 9, 1, 17, 14, 2, 2, 21, 8, 13, 11, 27, 5], same=True)
# random: a table lookup, of one vector or two
rnd = random.Random(5)
for i in range(6):
    m = [rnd.randrange(16) for _ in range(16)]
    case('rnd1_%d' % i, m, [(r'\stbl\.16b\s', 1)])
for i in range(6):
    m = [rnd.randrange(32) for _ in range(16)]
    # x and y loaded into neighbouring registers: the table as they are
    case('rnd2_%d' % i, m, [(r'\stbl\.16b\s', 1), (r'\smov\.16b\s', 0)])
for i in range(4):
    m = [16 + rnd.randrange(16) for _ in range(16)]
    case('rndy_%d' % i, m, [(r'\stbl\.16b\s', 1)])


def twice():
    """two table lookups of one index vector, made once"""
    m = [3, 29, 7, 0, 18, 11, 25, 4, 9, 30, 2, 16, 13, 6, 21, 1]
    want = apply(m) + apply([(k + 16) % 32 for k in m])
    return m, want


def main():
    runs, shapes, funcs = [], [], []
    seen = {}
    for name, mask, same, sh in CASES:
        key = (tuple(mask), same)
        if key in seen:
            continue
        seen[key] = name
        want = apply([m & 15 if same else m for m in mask])
        runs.append(';; run: %s 0 => i32:%d' % (name, h32(want) - (1 << 32) if h32(want) >> 31 else h32(want)))
        for pat, n in sh:
            shapes.append(';; shape: %s %s=%d' % (name, pat, n))
        y = '(local.get $x)' if same else '(v128.load offset=16 (local.get $p))'
        funcs.append('''  (func $%s (export "%s") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle %s (local.get $x) %s))
    (call $h (i32.add (local.get $p) (i32.const 64))))''' % (name, name, ' '.join(map(str, mask)), y))
    m, want = twice()
    h = 0
    for bs in (want[:16], want[16:]):
        h = (h * 7 + h32(bs)) & M32
    runs.append(';; run: tbl_twice 0 => i32:%d' % (h - (1 << 32) if h >> 31 else h))
    shapes.append(';; shape: tbl_twice \\stbl\\.16b\\s=2')
    shapes.append(';; shape: tbl_twice \\smovk\\s=6')
    funcs.append('''  (func $tbl_twice (export "tbl_twice") (param $p i32) (result i32)
    (local $x v128) (local $y v128)
    (local.set $x (v128.load (local.get $p)))
    (local.set $y (v128.load offset=16 (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle %s (local.get $x) (local.get $y)))
    (v128.store offset=80 (local.get $p) (i8x16.shuffle %s (local.get $y) (local.get $x)))
    (i32.add (i32.mul (call $h (i32.add (local.get $p) (i32.const 64))) (i32.const 7))
             (call $h (i32.add (local.get $p) (i32.const 80)))))''' % (' '.join(map(str, m)), ' '.join(map(str, m))))
    head = '''\
;; i8x16.shuffle of every shape a64 has one instruction for -- zip, uzp and
;; trn at 8-, 16-, 32- and 64-bit lanes, of x and y in either order and of
;; one with itself, ext at every offset, rev in 16-, 32- and 64-bit
;; containers, dup of a lane -- each with no table lookup; x and y as they
;; are; lanes put in, at each size, two of them, and into a base made by
;; zip; shapes of two steps; one vector shuffled with itself; and random
;; shuffles of one vector or two, a table lookup each, and two lookups of
;; one index vector, made once (isel.c sh_plan).
;; Each function shuffles two vectors of distinct bytes and returns a hash
;; of the result.  At -O2 -j1 too, its functions compiled in order.  Made by
;; test/gen_shuffle.py.
;;
'''
    out = head + '\n'.join(shapes) + '\n;;\n;; flags: -O2 -j1\n;;\n' + '\n'.join(runs) + '\n\n(module\n  (memory 1)\n'
    out += '  (data (i32.const 0) "%s")\n' % ''.join('\\%02x' % b for b in SRC)
    out += '''\
  (func $h (param $a i32) (result i32)
    (local $r i32) (local $n i32)
    (local.set $n (i32.const 4))
    (block $done
      (loop $l
        (br_if $done (i32.eqz (local.get $n)))
        (local.set $r (i32.add (i32.mul (local.get $r) (i32.const 31)) (i32.load (local.get $a))))
        (local.set $a (i32.add (local.get $a) (i32.const 4)))
        (local.set $n (i32.sub (local.get $n) (i32.const 1)))
        (br $l)))
    (local.get $r))
'''
    out += '\n'.join(funcs) + '\n)\n'
    open(sys.argv[1], 'w').write(out)


main()
