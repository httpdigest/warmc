"""test/cases_opt/ifc_bits.wat: if-conversion's latency rule and the one
place it gives way (opt.c ifc_try, ifc_one_bit): in a loop, a test of one
bit of a value the loop computes takes a select however late the bit is.
Answers worked out here.

  python3 test/gen_ifcbits.py test/cases_opt/ifc_bits.wat
"""
import sys

M32 = (1 << 32) - 1


def s32(v):
    v &= M32
    return v - (1 << 32) if v >> 31 else v


def mix(x):
    """the loops' hash: a multiply, a shift-xor and a multiply, deep enough
    that a bit of it is ready well after the constants a select picks"""
    x = (x * 0x9e3779b1) & M32
    x ^= x >> 15
    return (x * 0x85ebca6b) & M32


MIX = '''(i32.mul (i32.xor (local.get $z) (i32.shr_u (local.get $z) (i32.const 15))) (i32.const 0x85ebca6b))'''


def loop(name, cond, model, src='(local.get $i)'):
    """s += cond(z) ? 12345 : 54321, z = mix(src) for i < n"""
    return name, '''(func $%s (export "%s") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $z i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $z (i32.mul %s (i32.const 0x9e3779b1)))
        (local.set $z %s)
        (local.set $s (i32.add (local.get $s)
          (if (result i32) %s (then (i32.const 12345)) (else (i32.const 54321)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))''' % (name, name, src, MIX, cond), model


def sumof(test):
    def f(n):
        return sum(12345 if test(i) else 54321 for i in range(n))
    return f


CASES = [
    # one bit of the hash, either way round: a select
    loop('hashbit', '(i32.ne (i32.and (local.get $z) (i32.const 0x20000)) (i32.const 0))',
         sumof(lambda i: mix(i) & 0x20000 != 0)) + ([(r'\scsel\s', 1)],),
    loop('hashbit0', '(i32.eqz (i32.and (local.get $z) (i32.const 0x400)))',
         sumof(lambda i: mix(i) & 0x400 == 0)) + ([(r'\scsel\s', 1)],),
    loop('hashbiteq', '(i32.eq (i32.and (local.get $z) (i32.const 0x20000)) (i32.const 0x20000))',
         sumof(lambda i: mix(i) & 0x20000 != 0)) + ([(r'\scsel\s', 1)],),
    loop('hashbitne', '(i32.ne (i32.and (local.get $z) (i32.const 0x8)) (i32.const 0x8))',
         sumof(lambda i: mix(i) & 0x8 == 0)) + ([(r'\scsel\s', 1)],),
    loop('hashbiteq0', '(i32.eq (i32.and (local.get $z) (i32.const 0x80000000)) (i32.const 0))',
         sumof(lambda i: mix(i) & 0x80000000 == 0)) + ([(r'\scsel\s', 1)],),
    # a comparison of it, which a branch may guess; two bits of it
    loop('hashcmp', '(i32.lt_u (local.get $z) (i32.const 0x40000000))',
         sumof(lambda i: mix(i) < 0x40000000)) + ([(r'\scsel\s', 0)],),
    loop('twobits', '(i32.ne (i32.and (local.get $z) (i32.const 0x30000)) (i32.const 0))',
         sumof(lambda i: mix(i) & 0x30000 != 0)) + ([(r'\scsel\s', 0)],),
    # a bit of a loaded word hashed: the load decides it
    loop('loadedbit', '(i32.ne (i32.and (local.get $z) (i32.const 0x20000)) (i32.const 0))',
         sumof(lambda i: mix(i ^ (i % 4 * 7)) & 0x20000 != 0),
         '(i32.xor (local.get $i) (i32.load (i32.shl (i32.and (local.get $i) (i32.const 3)) (i32.const 2))))') +
    ([(r'\scsel\s', 0)],),
]


def loadedlate():
    """a bit of a loaded word picking the hash or a constant: the hash is
    late, the bit is not, and a loaded bit is a flag a branch guesses"""
    body = '''(func $loadedlate (export "loadedlate") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $z i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $z (i32.mul (local.get $i) (i32.const 0x9e3779b1)))
        (local.set $z %s)
        (local.set $s (i32.add (local.get $s)
          (if (result i32)
              (i32.ne (i32.and (i32.load (i32.shl (i32.and (local.get $i) (i32.const 3)) (i32.const 2)))
                               (i32.const 4)) (i32.const 0))
            (then (local.get $z)) (else (i32.const 12345)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))''' % MIX
    return ('loadedlate', body, lambda n: sum(mix(i) if i % 4 * 7 & 4 else 12345 for i in range(n)),
            [(r'\scsel\s', 0)])


def straight():
    """the bit tested once, outside a loop: the rule stands"""
    body = '''(func $straightbit (export "straightbit") (param $n i32) (result i32)
    (local $z i32)
    (local.set $z (i32.mul (local.get $n) (i32.const 0x9e3779b1)))
    (local.set $z %s)
    (if (result i32) (i32.ne (i32.and (local.get $z) (i32.const 0x20000)) (i32.const 0))
      (then (i32.const 12345)) (else (i32.const 54321))))''' % MIX
    return 'straightbit', body, lambda n: 12345 if mix(n) & 0x20000 else 54321, [(r'\scsel\s', 0)]


def main():
    cases = CASES + [loadedlate(), straight()]
    runs, shapes, funcs = [], [], []
    for name, body, model, sh in cases:
        for n in (0, 1, 7, 100, 1000):
            runs.append(';; run: %s %d => i32:%d' % (name, n, s32(model(n))))
        for pat, k in sh:
            shapes.append(';; shape: %s %s=%d' % (name, pat, k))
        funcs.append('  ' + body)
    head = '''\
;; If-conversion's latency rule (opt.c ifc_try): a select must not wait on
;; its condition much longer than on the values it picks, since a guessed
;; branch would not.  In a loop, a test of one bit of a value the loop
;; computes takes the select however late the bit is -- hashed bits, which
;; a branch guesses wrong half the time (hsbench/bits' testBit 17 of a
;; splitmix value), against 0 or the bit, either way round (ifc_one_bit).
;; A comparison of the same value keeps its branch, as do a test of two
;; bits, a bit of a loaded word hashed, a loaded bit picking the hash, and
;; the bit tested once outside a loop.  Made by test/gen_ifcbits.py.
;;
'''
    out = head + '\n'.join(shapes) + '\n;;\n' + '\n'.join(runs) + '\n\n(module\n  (memory 1)\n'
    out += '  (data (i32.const 0) "\\00\\00\\00\\00\\07\\00\\00\\00\\0e\\00\\00\\00\\15\\00\\00\\00")\n'
    out += '\n'.join(funcs) + '\n)\n'
    open(sys.argv[1], 'w').write(out)


main()
