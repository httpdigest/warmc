"""test/cases_opt/float_reassoc.wat: float sums and products a loop carries
round, which -O3's float-reassoc keeps in vector lanes (opt.c
vectorize_one) and whose chains it rebalances (pass_reassoc,
pass_reassoc_carried); -O2 keeps wasm's order.  The values are small
integers and powers of two, whose sums and products are exact in any
order, so every level gives the one answer worked out here.

  python3 test/gen_floatreassoc.py test/cases_opt/float_reassoc.wat
"""
import struct
import sys

N = 64
A = [float((i * 7) % 13 - 6) for i in range(N)]       # -6 .. 6
B = [float((i * 5) % 11 - 5) for i in range(N)]       # -5 .. 5
P = [(0.5, 1.0, 2.0, 1.0)[i % 4] for i in range(N)]   # products stay powers of two
AB, BB, PB = 0, 1024, 2048    # f64 arrays
AF, BF = 3072, 3584           # f32 copies of A and B
ZB = 4096                     # -0.0s


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def data(base, xs, fmt):
    return '  (data (i32.const %d) "%s")' % (base, ''.join('\\%02x' % c for x in xs for c in struct.pack(fmt, x)))


def loop(name, t, init, body, extra_locals=''):
    """for i < n: body, s the carried value"""
    return '''(func $%s (export "%s") (param $n i32) (result %s)
    (local $s %s) (local $i i32)%s
    (local.set $s (%s.const %s))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))%s
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $s))''' % (name, name, t, t, extra_locals, t, init, body)


def ld(t, base, size):
    return '(%s.load offset=%d (i32.shl (local.get $i) (i32.const %d)))' % (t, base, size)


CASES = []


def case(name, t, init, body, model, ns=(0, 1, 5, 17, 64), extra=''):
    CASES.append((name, t, loop(name, t, init, body, extra), model, ns))


# sums and a product of one array, a dot product, at both widths
case('sum64', 'f64', '0', '''
      (local.set $s (f64.add (local.get $s) %s))''' % ld('f64', AB, 3),
     lambda n: sum(A[:n]))
case('sum32', 'f32', '0', '''
      (local.set $s (f32.add %s (local.get $s)))''' % ld('f32', AF, 2),
     lambda n: f32(sum(A[:n])))
case('dot64', 'f64', '0', '''
      (local.set $s (f64.add (local.get $s) (f64.mul %s %s)))''' % (ld('f64', AB, 3), ld('f64', BB, 3)),
     lambda n: sum(a * b for a, b in zip(A[:n], B[:n])))
case('dot32', 'f32', '0', '''
      (local.set $s (f32.add (local.get $s) (f32.mul %s %s)))''' % (ld('f32', AF, 2), ld('f32', BF, 2)),
     lambda n: f32(sum(a * b for a, b in zip(A[:n], B[:n]))))
case('prod64', 'f64', '1', '''
      (local.set $s (f64.mul (local.get $s) %s))''' % ld('f64', PB, 3),
     lambda n: __import__('math').prod(P[:n]))
# three terms an iteration onto the sum: the sum added last, the terms
# first among themselves (pass_reassoc_carried)
case('three64', 'f64', '0', '''
      (local.set $s (f64.add (f64.add (f64.add (local.get $s) %s) %s) (f64.const 3)))''' % (ld('f64', AB, 3), ld('f64', BB, 3)),
     lambda n: sum(a + b + 3 for a, b in zip(A[:n], B[:n])))
# negative zeros from -0: -0, which a sum's lanes must start at to keep
case('negzero', 'f64', '-0', '''
      (local.set $s (f64.add (local.get $s) %s))''' % ld('f64', ZB, 3),
     lambda n: -0.0)
# a difference, which -O3 leaves in wasm's order
case('diff64', 'f64', '100', '''
      (local.set $s (f64.sub (local.get $s) %s))''' % ld('f64', AB, 3),
     lambda n: 100 - sum(A[:n]))


def f64bits(x):
    """an f64 argument as the runner takes it, its bits"""
    return struct.unpack('<Q', struct.pack('<d', float(x)))[0]


STRAIGHT = []


def straight(name, op, model, deep):
    """(((deep op y) op 3) op 5): the deep term first, which pass_reassoc
    moves last when op associates"""
    body = '(func $%s (export "%s") (param $x f64) (param $y f64) (result f64)\n' % (name, name)
    body += '    (f64.%s (f64.%s (f64.%s %s (local.get $y)) (f64.const 3)) (f64.const 5)))' % (op, op, op, deep)
    STRAIGHT.append((name, body, model))


# a product as the sum's deep term, and a sum as the product's: a term of
# the chain's own op would be a link of it
X4 = '(f64.mul (f64.mul (f64.mul (local.get $x) (local.get $x)) (local.get $x)) (local.get $x))'
XS = '(f64.add (f64.add (f64.add (local.get $x) (f64.const 1)) (local.get $x)) (f64.const 2))'
straight('chainadd', 'add', lambda x, y: x ** 4 + y + 3 + 5, X4)
straight('chainmul', 'mul', lambda x, y: (2 * x + 3) * y * 3 * 5, XS)
straight('chainsub', 'sub', lambda x, y: x ** 4 - y - 3 - 5, X4)


def main():
    runs, funcs = [], []
    for name, t, body, model, ns in CASES:
        for n in ns:
            v = model(n)
            bits = struct.unpack('<Q', struct.pack('<d', v))[0] if t == 'f64' \
                else struct.unpack('<I', struct.pack('<f', v))[0]
            runs.append(';; run: %s %d => %s:0x%0*x' % (name, n, t, 16 if t == 'f64' else 8, bits))
        funcs.append('  ' + body)
    for name, body, model in STRAIGHT:
        for x, y in ((0, 0), (2, 3), (-3, 7), (5, -2)):
            bits = struct.unpack('<Q', struct.pack('<d', float(model(x, y))))[0]
            runs.append(';; run: %s 0x%016x 0x%016x => f64:0x%016x' % (name, f64bits(x), f64bits(y), bits))
        funcs.append('  ' + body)
    head = '''\
;; Float sums and products a loop carries round, which -O3's float-reassoc
;; takes in another order than wasm's: in vector lanes, each a partial
;; result added (multiplied) together at the loop's end, -0 (1) their
;; start -- sum64, sum32, dot64, dot32, prod64, and negzero, whose -0s
;; from -0 sum to -0 -- and a chain of three terms onto the sum made the
;; terms' sum first, then the sum's add (three64, which the vectoriser,
;; before that, does not take).  Straight-line sums and products take their
;; deepest term last (chainadd, chainmul).  A difference keeps wasm's order
;; (diff64, chainsub).  -O2 does none of it.  The values
;; are small integers and powers of two, so every order gives one answer,
;; the one worked out by test/gen_floatreassoc.py; a float answer, and an
;; argument, is given as its bits.
;;
;; stats -O2: vectorised=0 reassoc=0
;; stats -O3: vectorised=6 reassoc=5
;;
'''
    out = head + '\n'.join(runs) + '\n\n(module\n  (memory 1)\n'
    out += '\n'.join([data(AB, A, '<d'), data(BB, B, '<d'), data(PB, P, '<d'),
                      data(AF, A, '<f'), data(BF, B, '<f'), data(ZB, [-0.0] * N, '<d')]) + '\n'
    out += '\n'.join(funcs) + '\n)\n'
    open(sys.argv[1], 'w').write(out)


main()
