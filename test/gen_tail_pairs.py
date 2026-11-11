"""test/cases_opt/tail_pairs.wat: two functions that tail-call each other,
one loop where each takes the other's body in at its tail call (inline.c
inline_tail_pairs, ahead of opt.c pass_self_tail).  The module's header says
what each function is; every answer is worked out here.

  python3 test/gen_tail_pairs.py test/cases_opt/tail_pairs.wat
"""
import sys

M64 = (1 << 64) - 1
M32 = (1 << 32) - 1
K = 2654435761


def s64(v):
    v &= M64
    return v - (1 << 64) if v >> 63 else v


def rotl(x, k):
    return ((x << k) | (x >> (64 - k))) & M64


def even(n, acc):
    while True:
        if n == 0:
            return acc
        n, acc = n - 1, (acc + n * K) & M64
        if n == 0:
            return acc ^ 0x5a5a5a5a
        n, acc = n - 1, rotl(acc, 1)


def odd(n, acc):
    if n == 0:
        return acc ^ 0x5a5a5a5a
    return even(n - 1, rotl(acc, 1))


def two(n, x, y):          # p2a / p2b: two results, as x * 31 + y of the pair that comes back
    a = True
    while True:
        if a:
            if n == 0:
                return (x * 31 + y) & M64
            n, x, y = n - 1, (x + y) & M64, y ^ n
        else:
            if n == 0:
                return ((y * 31) + x) & M64          # p2b returns (y, x)
            n, x, y = n - 1, x ^ (y << 1) & M64, (y + 3) & M64
        a = not a


def big(n, acc):           # bigA / bigB: bigB's body is too long to take in
    a = True
    while True:
        if n == 0:
            return acc
        if a:
            n, acc = n - 1, (acc + 1) & M64
        else:
            for j in range(40):
                acc = (rotl(acc, 1 + j % 7) + j) & M64
            n = n - 1
        a = not a


def oneway(n, acc):        # ow_f tail-calls ow_g, which calls nothing back
    return (acc * 5 + n) & M64 if n & 1 else (acc + n) & M64


def tri(n, acc):           # t1 -> t2 -> t3 -> t1
    k = 0
    while True:
        if n == 0:
            return (acc + k) & M64
        n, acc = n - 1, (acc * (3 + 2 * k) + 1) & M64
        k = (k + 1) % 3


def handled(n, acc):       # h_f <-> h_g, h_g with a handler of its own
    a = True
    while True:
        if n == 0:
            return acc
        if a:
            n, acc = n - 1, (acc + 2) & M64
        else:
            n, acc = n - 1, (acc + (7 if n % 5 == 0 else 1)) & M64
        a = not a


def sites(n, acc):         # s_f has two tail calls of s_g
    a = True
    while True:
        if n == 0:
            return acc
        if a:
            acc = (acc + 1) & M64 if n & 1 else (acc * 3) & M64
        else:
            acc = acc ^ n
        n -= 1
        a = not a


def selfback(n, acc):      # sb_f -> sb_g; sb_g -> itself or sb_f
    inf = True
    while True:
        if n == 0:
            return acc
        if inf:
            n, acc, inf = n - 1, (acc + 11) & M64, False
        elif n % 3 == 0:
            n, acc = n - 1, (acc + 7) & M64
        else:
            n, acc, inf = n - 1, acc ^ n, True


def arity(n, a, b, c):     # pa_f(n, a, b, c) <-> pa_g(n, a)
    inf = True
    while True:
        if inf:
            if n == 0:
                return (a + b * 3 + c * 5) & M64
            n, a, inf = n - 1, (a + b + c) & M64, False
        else:
            if n == 0:
                return a
            n, a, b, c, inf = n - 1, a, (a >> 3) & M64, (a & 0xff), True


def third(n, acc):         # x_f <-> x_g, x_g tail-calling x_h too
    inf = True
    while True:
        if n == 0:
            return acc
        if inf:
            n, acc, inf = n - 1, (acc + 1) & M64, False
        elif n & 7 == 0:
            return (acc ^ n) * 9 & M64
        else:
            n, acc, inf = n - 1, (acc * 3) & M64, True


def nontail(n, acc):       # nc_f tail-calls nc_g, which calls nc_f and adds to what it gives
    if n == 0:
        return acc
    n, acc = n - 1, (acc + 1) & M64          # nc_g(n - 1, acc + 1)
    if n == 0:
        return acc
    return (nontail(n - 1, 1) + acc) & M64


def lastcall(n, acc):      # lc_f branches on what its call of lc_g gives; lc_g tail-calls lc_f
    def g(n, acc):
        return 1 if n == 0 else f(n - 1, acc)

    def f(n, acc):
        if n == 0:
            return acc & 1
        return 7 if g(n - 1, acc) else 0
    return f(n, acc)


RUNS = (('even', even, [(0, 9), (1, 9), (2, 9), (7, 1), (1000, 3), (3000001, 5)]),
        ('odd', odd, [(0, 9), (1, 9), (6, 2), (2000000, 7)]),
        ('two', two, [(0, 2, 3), (1, 2, 3), (2, 2, 3), (9, 5, 1), (100001, 7, 9)]),
        ('big', big, [(0, 4), (1, 4), (2, 4), (9, 1), (2001, 3)]),
        ('oneway', oneway, [(3, 4), (4, 4)]),
        ('tri', tri, [(0, 1), (1, 1), (2, 1), (3, 1), (10, 2), (300001, 5)]),
        ('handled', handled, [(0, 1), (1, 1), (2, 1), (11, 3), (100000, 2)]),
        ('sites', sites, [(0, 5), (1, 5), (2, 5), (3, 5), (8, 2), (1000001, 1)]),
        ('selfback', selfback, [(0, 3), (1, 3), (2, 3), (3, 3), (10, 1), (1000001, 9)]),
        ('arity', arity, [(0, 1, 2, 3), (1, 1, 2, 3), (2, 1, 2, 3), (7, 4, 5, 6), (500001, 3, 1, 4)]),
        ('x_f', third, [(0, 2), (1, 2), (2, 2), (9, 2), (17, 5), (6, 1)]),
        ('nontail', nontail, [(0, 3), (1, 3), (2, 3), (3, 3), (40, 2)]),
        ('lastcall', lastcall, [(0, 2), (0, 3), (1, 2), (2, 2), (2, 3), (3, 3), (4, 3), (9, 1)]))

HEAD = ''';; Two functions that tail-call each other, one loop (inline.c
;; inline_tail_pairs, ahead of opt.c pass_self_tail).  Each tail call is a
;; jump with its arguments moved into place; where the callee tail-calls the
;; caller back, the caller takes the callee's body in at its call, the
;; callee's tail calls kept, and its tail call of itself that this leaves is
;; a loop: the two functions' steps one after the other.  zigbench/tailrec's
;; even and odd took a dozen instructions and a jump each, and take seven
;; instructions a pair.
;;
;;   even odd   the pair: each is a loop, with no jump to the other
;;   two        p2a and p2b, of three parameters and two results, which a
;;              wrapper makes one of
;;   big        bigA and bigB, bigB of more instructions than are taken in:
;;              bigA keeps its jump; bigB takes bigA in
;;   oneway     ow_f tail-calls ow_g, which calls nothing back: as it was
;;   tri        t1 calls t2, t2 calls t3, t3 calls t1: no pair, as they were
;;   handled    h_f and h_g, h_g with a handler of its own: h_f keeps its
;;              jump, and h_g, which has a handler, takes nothing in
;;   sites      s_f tail-calls s_g at two places, and takes it in at both
;;   selfback   sb_g tail-calls itself and sb_f: sb_f's loop has sb_g's tail
;;              call of itself in it, still a call of sb_g
;;   arity      pa_f of four parameters and pa_g of two
;;   nontail    nc_f tail-calls nc_g, which calls nc_f and adds to what that
;;              gives: no tail call back, and nc_f keeps its jump
;;   lastcall   lc_f's call of lc_g ends its block, a branch on what it
;;              gives: a call, not a tail call, whatever lc_g does
;;   x_f        exported itself, with x_g, which tail-calls x_h too: x_f's
;;              loop ends one way in the jump to x_h
;;
;; Each is run through an exported r_NAME.  Answers by the functions as
;; Python has them, some a few million calls deep, in one frame.
;;
;; stats -O2: tail_pairs=%(PAIRS)s self_tail=%(SELF)s
;; stats -O2 -fno-selftail: tail_pairs=0 self_tail=0
;; stats -O2 -fno-inline: tail_pairs=0 self_tail=1
%(SHAPES)s;;
'''

MOD = '''(module
  (tag $e (param i32))
  (func $even (param $n i32) (param $acc i64) (result i64)
    (if (i32.eqz (local.get $n)) (then (return (local.get $acc))))
    (return_call $odd (i32.sub (local.get $n) (i32.const 1))
      (i64.add (local.get $acc) (i64.mul (i64.extend_i32_u (local.get $n)) (i64.const %(K)d)))))
  (func $odd (param $n i32) (param $acc i64) (result i64)
    (if (i32.eqz (local.get $n)) (then (return (i64.xor (local.get $acc) (i64.const 0x5a5a5a5a)))))
    (return_call $even (i32.sub (local.get $n) (i32.const 1)) (i64.rotl (local.get $acc) (i64.const 1))))
  (func $r_even (export "r_even") (param i32 i64) (result i64) (call $even (local.get 0) (local.get 1)))
  (func $r_odd (export "r_odd") (param i32 i64) (result i64) (call $odd (local.get 0) (local.get 1)))

  (func $p2a (param $n i32) (param $x i64) (param $y i64) (result i64 i64)
    (if (i32.eqz (local.get $n)) (then (return (local.get $x) (local.get $y))))
    (return_call $p2b (i32.sub (local.get $n) (i32.const 1)) (i64.add (local.get $x) (local.get $y))
      (i64.xor (local.get $y) (i64.extend_i32_u (local.get $n)))))
  (func $p2b (param $n i32) (param $x i64) (param $y i64) (result i64 i64)
    (if (i32.eqz (local.get $n)) (then (return (local.get $y) (local.get $x))))
    (return_call $p2a (i32.sub (local.get $n) (i32.const 1))
      (i64.xor (local.get $x) (i64.shl (local.get $y) (i64.const 1)))
      (i64.add (local.get $y) (i64.const 3))))
  (func $r_two (export "r_two") (param i32 i64 i64) (result i64) (local $a i64) (local $b i64)
    (call $p2a (local.get 0) (local.get 1) (local.get 2))
    (local.set $b) (local.set $a)
    (i64.add (i64.mul (local.get $a) (i64.const 31)) (local.get $b)))

  (func $bigA (param $n i32) (param $acc i64) (result i64)
    (if (i32.eqz (local.get $n)) (then (return (local.get $acc))))
    (return_call $bigB (i32.sub (local.get $n) (i32.const 1)) (i64.add (local.get $acc) (i64.const 1))))
  (func $bigB (param $n i32) (param $acc i64) (result i64)
    (if (i32.eqz (local.get $n)) (then (return (local.get $acc))))
%(BIG)s    (return_call $bigA (i32.sub (local.get $n) (i32.const 1)) (local.get $acc)))
  (func $r_big (export "r_big") (param i32 i64) (result i64) (call $bigA (local.get 0) (local.get 1)))

  (func $ow_g (param $n i32) (param $acc i64) (result i64)
    (i64.add (local.get $acc) (i64.extend_i32_u (local.get $n))))
  (func $ow_f (param $n i32) (param $acc i64) (result i64)
    (if (i32.and (local.get $n) (i32.const 1))
      (then (return_call $ow_g (local.get $n) (i64.mul (local.get $acc) (i64.const 5)))))
    (return_call $ow_g (local.get $n) (local.get $acc)))
  (func $r_oneway (export "r_oneway") (param i32 i64) (result i64) (call $ow_f (local.get 0) (local.get 1)))

  (func $t1 (param $n i32) (param $acc i64) (result i64)
    (if (i32.eqz (local.get $n)) (then (return (local.get $acc))))
    (return_call $t2 (i32.sub (local.get $n) (i32.const 1)) (i64.add (i64.mul (local.get $acc) (i64.const 3)) (i64.const 1))))
  (func $t2 (param $n i32) (param $acc i64) (result i64)
    (if (i32.eqz (local.get $n)) (then (return (i64.add (local.get $acc) (i64.const 1)))))
    (return_call $t3 (i32.sub (local.get $n) (i32.const 1)) (i64.add (i64.mul (local.get $acc) (i64.const 5)) (i64.const 1))))
  (func $t3 (param $n i32) (param $acc i64) (result i64)
    (if (i32.eqz (local.get $n)) (then (return (i64.add (local.get $acc) (i64.const 2)))))
    (return_call $t1 (i32.sub (local.get $n) (i32.const 1)) (i64.add (i64.mul (local.get $acc) (i64.const 7)) (i64.const 1))))
  (func $r_tri (export "r_tri") (param i32 i64) (result i64) (call $t1 (local.get 0) (local.get 1)))

  (func $thrower (param $n i32) (if (i32.eqz (i32.rem_u (local.get $n) (i32.const 5))) (then (throw $e (i32.const 7)))))
  (func $h_f (param $n i32) (param $acc i64) (result i64)
    (if (i32.eqz (local.get $n)) (then (return (local.get $acc))))
    (return_call $h_g (i32.sub (local.get $n) (i32.const 1)) (i64.add (local.get $acc) (i64.const 2))))
  (func $h_g (param $n i32) (param $acc i64) (result i64) (local $d i32)
    (if (i32.eqz (local.get $n)) (then (return (local.get $acc))))
    (local.set $d (i32.const 1))
    (block $h (result i32)
      (try_table (catch $e $h) (call $thrower (local.get $n)))
      (i32.const 1))
    (local.set $d)
    (return_call $h_f (i32.sub (local.get $n) (i32.const 1)) (i64.add (local.get $acc) (i64.extend_i32_u (local.get $d)))))
  (func $r_handled (export "r_handled") (param i32 i64) (result i64) (call $h_f (local.get 0) (local.get 1)))

  (func $s_f (param $n i32) (param $acc i64) (result i64)
    (if (i32.eqz (local.get $n)) (then (return (local.get $acc))))
    (if (i32.and (local.get $n) (i32.const 1))
      (then (return_call $s_g (i32.sub (local.get $n) (i32.const 1)) (i64.add (local.get $acc) (i64.const 1)))))
    (return_call $s_g (i32.sub (local.get $n) (i32.const 1)) (i64.mul (local.get $acc) (i64.const 3))))
  (func $s_g (param $n i32) (param $acc i64) (result i64)
    (if (i32.eqz (local.get $n)) (then (return (local.get $acc))))
    (return_call $s_f (i32.sub (local.get $n) (i32.const 1)) (i64.xor (local.get $acc) (i64.extend_i32_u (local.get $n)))))
  (func $r_sites (export "r_sites") (param i32 i64) (result i64) (call $s_f (local.get 0) (local.get 1)))

  (func $sb_f (param $n i32) (param $acc i64) (result i64)
    (if (i32.eqz (local.get $n)) (then (return (local.get $acc))))
    (return_call $sb_g (i32.sub (local.get $n) (i32.const 1)) (i64.add (local.get $acc) (i64.const 11))))
  (func $sb_g (param $n i32) (param $acc i64) (result i64)
    (if (i32.eqz (local.get $n)) (then (return (local.get $acc))))
    (if (i32.eqz (i32.rem_u (local.get $n) (i32.const 3)))
      (then (return_call $sb_g (i32.sub (local.get $n) (i32.const 1)) (i64.add (local.get $acc) (i64.const 7)))))
    (return_call $sb_f (i32.sub (local.get $n) (i32.const 1)) (i64.xor (local.get $acc) (i64.extend_i32_u (local.get $n)))))
  (func $r_selfback (export "r_selfback") (param i32 i64) (result i64) (call $sb_f (local.get 0) (local.get 1)))

  (func $pa_f (param $n i32) (param $a i64) (param $b i64) (param $c i64) (result i64)
    (if (i32.eqz (local.get $n))
      (then (return (i64.add (local.get $a) (i64.add (i64.mul (local.get $b) (i64.const 3)) (i64.mul (local.get $c) (i64.const 5)))))))
    (return_call $pa_g (i32.sub (local.get $n) (i32.const 1)) (i64.add (local.get $a) (i64.add (local.get $b) (local.get $c)))))
  (func $pa_g (param $n i32) (param $a i64) (result i64)
    (if (i32.eqz (local.get $n)) (then (return (local.get $a))))
    (return_call $pa_f (i32.sub (local.get $n) (i32.const 1)) (local.get $a)
      (i64.shr_u (local.get $a) (i64.const 3)) (i64.and (local.get $a) (i64.const 0xff))))
  (func $r_arity (export "r_arity") (param i32 i64 i64 i64) (result i64)
    (call $pa_f (local.get 0) (local.get 1) (local.get 2) (local.get 3)))

  (func $nc_f (param $n i32) (param $acc i64) (result i64)
    (if (i32.eqz (local.get $n)) (then (return (local.get $acc))))
    (return_call $nc_g (i32.sub (local.get $n) (i32.const 1)) (i64.add (local.get $acc) (i64.const 1))))
  (func $nc_g (param $n i32) (param $acc i64) (result i64)
    (if (i32.eqz (local.get $n)) (then (return (local.get $acc))))
    (i64.add (call $nc_f (i32.sub (local.get $n) (i32.const 1)) (i64.const 1)) (local.get $acc)))
  (func $r_nontail (export "r_nontail") (param i32 i64) (result i64) (call $nc_f (local.get 0) (local.get 1)))

  (func $lc_f (param $n i32) (param $acc i64) (result i32)
    (if (i32.eqz (local.get $n)) (then (return (i32.and (i32.wrap_i64 (local.get $acc)) (i32.const 1)))))
    (if (result i32) (call $lc_g (i32.sub (local.get $n) (i32.const 1)) (local.get $acc))
      (then (i32.const 7)) (else (i32.const 0))))
  (func $lc_g (param $n i32) (param $acc i64) (result i32)
    (if (i32.eqz (local.get $n)) (then (return (i32.const 1))))
    (return_call $lc_f (i32.sub (local.get $n) (i32.const 1)) (local.get $acc)))
  (func $r_lastcall (export "r_lastcall") (param i32 i64) (result i32) (call $lc_f (local.get 0) (local.get 1)))

  (func $x_h (param $n i32) (param $acc i64) (result i64)
    (i64.mul (i64.xor (local.get $acc) (i64.extend_i32_u (local.get $n))) (i64.const 9)))
  (func $x_f (export "x_f") (param $n i32) (param $acc i64) (result i64)
    (if (i32.eqz (local.get $n)) (then (return (local.get $acc))))
    (return_call $x_g (i32.sub (local.get $n) (i32.const 1)) (i64.add (local.get $acc) (i64.const 1))))
  (func $x_g (param $n i32) (param $acc i64) (result i64)
    (if (i32.eqz (local.get $n)) (then (return (local.get $acc))))
    (if (i32.eqz (i32.and (local.get $n) (i32.const 7)))
      (then (return_call $x_h (local.get $n) (local.get $acc))))
    (return_call $x_f (i32.sub (local.get $n) (i32.const 1)) (i64.mul (local.get $acc) (i64.const 3))))
)
'''

# what the code is on arm64: a jump to the other function, or none
JUMP = r'b\s+0x[0-9a-f]+ <.*%s'
SHAPES = ['even ' + JUMP % 'odd' + '=0', 'odd ' + JUMP % 'even' + '=0',
          'p2a ' + JUMP % 'p2b' + '=0', 'bigA ' + JUMP % 'bigB' + '=1', 'bigB ' + JUMP % 'bigA' + '=0',
          't1 ' + JUMP % 't2' + '=1', 'h_f ' + JUMP % 'h_g' + '=1',
          's_f ' + JUMP % 's_g' + '=0', 'pa_f ' + JUMP % 'pa_g' + '=0',
          # (sb_f, in the function that runs it: sb_g's tail call of itself, a call there)
          r'r_selfback bl\s+0x[0-9a-f]+ <.*sb_g>=1',
          'x_f ' + JUMP % 'x_h' + '=1', 'x_f ' + JUMP % 'x_g' + '=0',
          'nc_f ' + JUMP % 'nc_g' + '=1']


def main():
    runs = []
    for name, fn, argl in RUNS:
        for args in argl:
            got = fn(*args)
            runs.append(';; run: %s%s %s => %s' % ('' if name == 'x_f' else 'r_', name, ' '.join(map(str, args)),
                                                   'i32:%d' % got if name == 'lastcall' else 'i64:%d' % s64(got)))
    big = ''.join('    (local.set $acc (i64.add (i64.rotl (local.get $acc) (i64.const %d)) (i64.const %d)))\n'
                  % (1 + j % 7, j) for j in range(40))
    stats = dict(l.split('=') for l in (sys.argv[2:] or ['PAIRS=?', 'SELF=?']))
    shapes = ''.join(';; shape: %s\n' % l for l in SHAPES)
    open(sys.argv[1], 'w').write(HEAD % {'PAIRS': stats['PAIRS'], 'SELF': stats['SELF'], 'SHAPES': shapes} +
                                 '\n'.join(runs) + '\n' + MOD % {'K': K, 'BIG': big})


if __name__ == '__main__':
    main()
