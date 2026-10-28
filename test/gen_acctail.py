"""test/cases_opt/acc_tail.wat: recursions whose result is the call's op
something else, which opt.c pass_acc_tail carries as a loop with an
accumulator at -O3, and the near misses it must leave be.  Answers worked
out here.

  python3 test/gen_acctail.py test/cases_opt/acc_tail.wat
"""
import sys

sys.setrecursionlimit(100000)

M32, M64 = (1 << 32) - 1, (1 << 64) - 1


def s32(x):
    x &= M32
    return x - (1 << 32) if x >> 31 else x


def s64(x):
    x &= M64
    return x - (1 << 64) if x >> 63 else x


def fib(n):
    return n if n < 2 else (fib(n - 1) + fib(n - 2)) & M32


def sumr(x):
    return 0 if x == 0 else (x + sumr(x - 1)) & M32


def fac(n):
    return 1 if n <= 1 else (n * fac(n - 1)) & M64


def xsum(n):
    return 0x55 if n == 0 else ((n * 0x9e37) & M32) ^ xsum(n - 1)


def band(n):
    return M32 if n == 0 else (n | 0xf0) & band(n - 1)


def band64(n):
    return M64 if n == 0 else ((n << 40) | 0xff00ff) & band64(n - 1)


def bor(n):
    return 0 if n == 0 else (1 << (n & 31)) | bor(n - 1)


def ret(n):
    return 7 if n <= 0 else (n * 3 + ret(n - 2)) & M32


def mixn(n, k):
    x = n
    for i in range(k):
        x = ((x * (2 * i + 3)) & M32) ^ (x >> (i % 5 + 1))
    return x


def medium(n):
    return 0 if n == 0 else (mixn(n, 6) + medium(n - 1)) & M32


def big(n):
    return 0 if n == 0 else (mixn(n, 24) + big(n - 1)) & M32


def subr(x):
    return 0 if x == 0 else (x - subr(x - 1)) & M32


def stored(n, mem):
    if n == 0:
        return 1
    r = stored(n - 1, mem)
    mem[0] = n
    return (r + n) & M32


def trapping(n, d):
    if n == 0:
        return 0
    r = trapping(n - 1, d)
    return (r + n // d) & M32


def gset(n, g):
    if n == 0:
        return g[0]
    r = gset(n - 1, g)
    g[0] = (g[0] * 3 + n) & M32
    return (r + n) & M32


def gget(n, g):
    if n == 0:
        return 0
    g[0] = (g[0] + n * n) & M32
    r = gget(n - 1, g)
    return (r + g[0]) & M32


def split(n, mem):
    if n == 0:
        return mem[0]
    r = split(n - 1, mem)
    if n & 1:
        mem[0] = (mem[0] * 3 + n) & M32
    return (r + n) & M32


def dbl(n):
    return 2 if n == 0 else ((n + dbl(n - 1)) * 2) & M32


def counted(n, mem):
    # the block both ways reach counts in memory; the caller reads it after
    def f(k):
        r = 5 if k == 0 else (k + f(k - 1)) & M32
        mem[0] += 1
        return r
    r = f(n)
    return (r + mem[0] * 1000) & M32


def twice(n):
    if n == 0:
        return n + 1
    r = twice(n - 1)
    return (r + (r >> 1)) & M32


def eh(n):
    # n == 0 throws, which the level above catches, giving -1
    if n == 1:
        return -1
    return (n + eh(n - 1)) & M32


def mutual_a(n):
    return 0 if n == 0 else (n + mutual_b(n - 1)) & M32


def mutual_b(n):
    return 1 if n == 0 else (2 * n + mutual_a(n - 1)) & M32


def mixed(n):
    if n == 0:
        return 1
    return (n + mixed(n - 1)) & M32 if n & 1 else (n * mixed(n - 1)) & M32


def viatail(n):
    return other(n) if n == 0 else (n + viatail(n - 1)) & M32


def other(n):
    return 40 + n


def MIXBODY(name, k):
    lines = ''.join('''
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const %d)) (i32.shr_u (local.get $x) (i32.const %d))))'''
                    % (2 * i + 3, i % 5 + 1) for i in range(k))
    return '''
    (local $x i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 0))
      (else
        (local.set $x (local.get 0))%s
        (i32.add (local.get $x) (call $%s (i32.sub (local.get 0) (i32.const 1))))))''' % (lines, name)


# name, args, model, result type, body, taken at -O3
CASES = [
    ('fib', [(0,), (1,), (2,), (10,), (20,)], fib, 'i32', '''
    (if (result i32) (i32.lt_u (local.get 0) (i32.const 2))
      (then (local.get 0))
      (else (i32.add (call $fib (i32.sub (local.get 0) (i32.const 1)))
                     (call $fib (i32.sub (local.get 0) (i32.const 2))))))''', True),
    ('sum', [(0,), (1,), (10,), (100,), (3000,)], sumr, 'i32', '''
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 0))
      (else (i32.add (local.get 0) (call $sum (i32.sub (local.get 0) (i32.const 1))))))''', True),
    ('fac', [(0,), (1,), (5,), (20,), (25,)], fac, 'i64', '''
    (if (result i64) (i64.le_u (local.get 0) (i64.const 1))
      (then (i64.const 1))
      (else (i64.mul (local.get 0) (call $fac (i64.sub (local.get 0) (i64.const 1))))))''', True),
    ('xsum', [(0,), (1,), (9,), (100,)], xsum, 'i32', '''
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 0x55))
      (else (i32.xor (call $xsum (i32.sub (local.get 0) (i32.const 1)))
                     (i32.mul (local.get 0) (i32.const 0x9e37)))))''', True),
    ('band', [(0,), (1,), (7,), (40,)], band, 'i32', '''
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const -1))
      (else (i32.and (i32.or (local.get 0) (i32.const 0xf0))
                     (call $band (i32.sub (local.get 0) (i32.const 1))))))''', True),
    ('band64', [(0,), (1,), (3,), (9,)], band64, 'i64', '''
    (if (result i64) (i64.eqz (local.get 0))
      (then (i64.const -1))
      (else (i64.and (i64.or (i64.shl (local.get 0) (i64.const 40)) (i64.const 0xff00ff))
                     (call $band64 (i64.sub (local.get 0) (i64.const 1))))))''', True),
    ('bor', [(0,), (1,), (5,), (31,), (40,)], bor, 'i32', '''
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 0))
      (else (i32.or (i32.shl (i32.const 1) (local.get 0))
                    (call $bor (i32.sub (local.get 0) (i32.const 1))))))''', True),
    # a return of its own, not a jump to a block that returns
    ('ret', [(0,), (1,), (2,), (9,), (100,)], ret, 'i32', '''
    (if (i32.le_s (local.get 0) (i32.const 0)) (then (return (i32.const 7))))
    (return (i32.add (i32.mul (local.get 0) (i32.const 3))
                     (call $ret (i32.sub (local.get 0) (i32.const 2)))))''', True),
    # a body of some dozens of instructions, and one past the limit
    ('medium', [(0,), (1,), (30,)], medium, 'i32', MIXBODY('medium', 6), True),
    ('big', [(0,), (1,), (30,)], big, 'i32', MIXBODY('big', 24), False),
    # a difference, which does not associate
    ('subr', [(0,), (1,), (2,), (9,)], subr, 'i32', '''
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 0))
      (else (i32.sub (local.get 0) (call $subr (i32.sub (local.get 0) (i32.const 1))))))''', False),
    # a store after the call, which the next level's would come before
    ('stored', [(0,), (1,), (6,)], lambda n: stored(n, [0]), 'i32', '''
    (local $r i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 1))
      (else
        (local.set $r (call $stored (i32.sub (local.get 0) (i32.const 1))))
        (i32.store (i32.const 64) (local.get 0))
        (i32.add (local.get $r) (local.get 0))))''', False),
    # a divide after the call, which may trap where the call ran first
    ('trapping', [(0, 1), (5, 2), (9, 3)], trapping, 'i32', '''
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 0))
      (else (i32.add (call $trapping (i32.sub (local.get 0) (i32.const 1)) (local.get 1))
                     (i32.div_u (local.get 0) (local.get 1)))))''', False),
    # a global set after the call, no store and no trap: its last level
    # returns what the global held
    ('gset', [(0,), (1,), (5,)], lambda n: gset(n, [0]), 'i32', '''
    (local $r i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (global.get $g))
      (else
        (local.set $r (call $gset (i32.sub (local.get 0) (i32.const 1))))
        (global.set $g (i32.add (i32.mul (global.get $g) (i32.const 3)) (local.get 0)))
        (i32.add (local.get $r) (local.get 0))))''', False),
    # a global read after the call, which deeper levels write: the next
    # level's writes would come after it
    ('gget', [(0,), (1,), (5,)], lambda n: gget(n, [0]), 'i32', '''
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 0))
      (else
        (global.set $g (i32.add (global.get $g) (i32.mul (local.get 0) (local.get 0))))
        (i32.add (call $gget (i32.sub (local.get 0) (i32.const 1))) (global.get $g))))''', False),
    # the call in a block before the op's, a store between: the next
    # level's would see it first (its last level returns what it stored)
    ('split', [(0,), (1,), (5,)], lambda n: split(n, [0]), 'i32', '''
    (local $r i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.load (i32.const 64)))
      (else
        (local.set $r (call $split (i32.sub (local.get 0) (i32.const 1))))
        (if (i32.and (local.get 0) (i32.const 1))
          (then (i32.store (i32.const 64) (i32.add (i32.mul (i32.load (i32.const 64)) (i32.const 3))
                                                   (local.get 0)))))
        (i32.add (local.get $r) (local.get 0))))''', False),
    # a sum the block both ways reach doubles before it returns
    ('dbl', [(0,), (1,), (6,)], dbl, 'i32', '''
    (i32.mul
      (if (result i32) (i32.eqz (local.get 0))
        (then (i32.const 1))
        (else (i32.add (local.get 0) (call $dbl (i32.sub (local.get 0) (i32.const 1))))))
      (i32.const 2))''', False),
    # the block both ways reach counts each level in memory before it
    # returns, which counted reads back after the call
    ('counted', [(0,), (1,), (6,)], lambda n: counted(n, [0]), 'i32', '''
    (i32.add (call $count1 (local.get 0))
             (i32.mul (i32.load (i32.const 128)) (i32.const 1000)))''', False),
    # the call's result read by the op's other operand too
    ('twice', [(0,), (1,), (8,), (30,)], twice, 'i32', '''
    (local $r i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 1))
      (else
        (local.set $r (call $twice (i32.sub (local.get 0) (i32.const 1))))
        (i32.add (local.get $r) (i32.shr_u (local.get $r) (i32.const 1)))))''', False),
    # the call in a try that catches what its last level throws
    ('eh', [(1,), (2,), (5,)], eh, 'i32', '''
    (if (i32.eqz (local.get 0)) (then (throw $e)))
    (block $h
      (try_table (catch_all $h)
        (return (i32.add (local.get 0) (call $eh (i32.sub (local.get 0) (i32.const 1)))))))
    (i32.const -1)''', False),
    # a call of another function
    ('mutual_a', [(0,), (1,), (2,), (11,)], mutual_a, 'i32', '''
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 0))
      (else (i32.add (local.get 0) (call $mutual_b (i32.sub (local.get 0) (i32.const 1))))))''', False),
    ('mutual_b', [(0,), (5,)], mutual_b, 'i32', '''
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 1))
      (else (i32.add (i32.mul (local.get 0) (i32.const 2))
                     (call $mutual_a (i32.sub (local.get 0) (i32.const 1))))))''', False),
    # a sum one way and a product the other: the sum's call is taken, the
    # product's stays a call
    ('mixed', [(0,), (1,), (2,), (7,)], mixed, 'i32', '''
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 1))
      (else
        (if (result i32) (i32.and (local.get 0) (i32.const 1))
          (then (return (i32.add (local.get 0) (call $mixed (i32.sub (local.get 0) (i32.const 1))))))
          (else (return (i32.mul (local.get 0) (call $mixed (i32.sub (local.get 0) (i32.const 1)))))))))''',
     True),
    # a tail call of another function, whose result no accumulator could
    # meet: left be
    ('viatail', [(0,), (1,), (6,)], viatail, 'i32', '''
    (if (i32.eqz (local.get 0)) (then (return_call $other (local.get 0))))
    (i32.add (local.get 0) (call $viatail (i32.sub (local.get 0) (i32.const 1))))''', False),
]


def main():
    runs, funcs = [], []
    taken = sum(1 for c in CASES if c[5])
    for name, args, model, t, body, _ in CASES:
        for a in args:
            v = model(*a)
            runs.append(';; run: %s %s => %s:%d' % (name, ' '.join(map(str, a)), t,
                                                     s64(v) if t == 'i64' else s32(v)))
        params = ' '.join('(param %s)' % t for _ in args[0])
        funcs.append('  (func $%s (export "%s") %s (result %s)%s)' % (name, name, params, t, body))
    head = '''\
;; Recursion through an accumulator (opt.c pass_acc_tail, -O3 only): a
;; function whose result is, one way, its own call's result op something
;; else -- an add, multiply, xor, and or or, each with its identity -- the
;; call's result read by op alone and nothing after the call but pure work,
;; loops instead, as LLVM's TailCallElim does: naive fib makes one call a
;; level, not two, sum and fac none.  A path may return through a block
;; that returns its parameter, or return itself (ret).  A body of some
;; dozens of instructions is taken (medium), one past 64 is not (big):
;; TeaVM unrolls its fib into itself, where one call fewer of many saved
;; nothing.  Left be: a difference (subr); after the call, a store
;; (stored), a divide that may trap (trapping), a global set (gset) or read
;; (gget); the call in a block before the op's, a store between (split); a
;; sum the block it reaches doubles (dbl) or counts in memory before
;; returning it (counted); the call's result read by the op's other operand
;; too (twice); a call in a try that catches what a deeper level throws
;; (eh); a call of another function (mutual_a and mutual_b); a second op
;; where the first was another (mixed: the sum's call is taken, the
;; product's stays); and a function with a tail call of another, whose
;; result an accumulator could not meet (viatail).  None at -O2, where a
;; recursion must still run out of stack.  Answers worked out by
;; test/gen_acctail.py.
;;
;; stats -O2: acc_tail=0
;; stats -O3: acc_tail=%d
;;
''' % taken
    out = head + '\n'.join(runs) + '\n\n(module\n  (memory 1)\n'
    out += '  (global $g (mut i32) (i32.const 0))\n  (tag $e)\n'
    out += '  (func $other (param i32) (result i32) (i32.add (local.get 0) (i32.const 40)))\n'
    out += '''  (func $count1 (param i32) (result i32)
    (local $r i32)
    (local.set $r
      (if (result i32) (i32.eqz (local.get 0))
        (then (i32.const 5))
        (else (i32.add (local.get 0) (call $count1 (i32.sub (local.get 0) (i32.const 1)))))))
    (i32.store (i32.const 128) (i32.add (i32.load (i32.const 128)) (i32.const 1)))
    (local.get $r))
'''
    out += '\n'.join(funcs) + '\n)\n'
    open(sys.argv[1], 'w').write(out)


main()
