"""test/cases_opt/lockstep.wat: a loop's counters that step together (opt.c
pass_iv_lockstep) -- the ones taken out of the loop, the ways out handed a
counter's stepped value, the addresses of a counter, a value and the step.
The module's header says what each function is; every answer is worked out
here on a model of the function in Python.

  python3 test/gen_lockstep.py test/cases_opt/lockstep.wat
  python3 test/gen_lockstep.py --each [NAME ...]

The first writes lockstep_nodce.wat beside it too: some of the functions,
compiled with -fno-dce besides.

--each compiles every function (or the named ones) alone with build/warmc
and prints what --stats counts for it beside what is expected of it here.
"""
import os, re, subprocess, sys, tempfile

M32 = (1 << 32) - 1
M64 = (1 << 64) - 1
MEM = bytes([3, 7, 1, 9, 4, 6, 2, 8, 5, 0] + [0] * 6 +          # 0: a string of nine
            [1, 1, 1, 0] + [0] * 12 +                            # 16: of three
            [0, 1, 2, 3, 4, 5, 6] + [0] * 9 +                    # 32: a ramp, read downwards from 38
            [2, 4, 6, 8, 1, 3, 5, 0] + [0] * 8)                  # 48: evens, then odds


def s32(v):
    v &= M32
    return v - (1 << 32) if v >> 31 else v


def s64(v):
    v &= M64
    return v - (1 << 64) if v >> 63 else v


CASES = []


def case(name, what, wat, model, runs, want, shapes=(), rtype='i32'):
    """want: (counters taken out, ways out given a stepped value, addresses)"""
    CASES.append((name, what, wat, model, runs, want, shapes, rtype))


NS = [(0,), (1,), (2,), (5,), (9,)]

# -- counters taken out -----------------------------------------------------------

case('same', 'two counters from one value, the second read only after the loop: taken out', '''
  (func $same (export "same") (param $a i32) (result i32)
    (local $i i32) (local $j i32)
    (local.set $i (local.get $a))
    (local.set $j (local.get $a))
    (block $out
      (loop $l
        (br_if $out (i32.eqz (i32.load8_u (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $j) (i32.const 3)) (local.get $i)))
''', lambda a: 4 * next(i for i in range(a, 99) if MEM[i] == 0),
     [(0,), (4,), (9,), (16,), (18,), (48,)], (1, 0, 0))


def apart(n):
    i, j, s = 2, 9, 0
    while i < n:
        s = (s * 3 + i) & M32
        i += 1
        j += 1
    return j * 1000 + s


case('apart', 'two a constant apart, the way out above the step: the counter plus the constant there', '''
  (func $apart (export "apart") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (local.set $i (i32.const 2))
    (local.set $j (i32.const 9))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $j) (i32.const 1000)) (local.get $s)))
''', apart, NS, (2, 0, 0))


def below(n):
    i, j, s = 20, 3, 0
    while i < n:
        s = (s * 5 + i) & M32
        i += 1
        j += 1
    return j * 65537 + s


case('below', 'the one taken out starts below the one that stays: a constant taken off', '''
  (func $below (export "below") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (local.set $i (i32.const 20))
    (local.set $j (i32.const 3))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 5)) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $j) (i32.const 65537)) (local.get $s)))
''', below, [(0,), (20,), (21,), (25,), (40,)], (2, 0, 0))


def stepped(n):
    i, j = 0, 100
    while True:
        i += 1
        j += 1
        if not i < n:
            break
    return j ^ (i << 16)


case('stepped', 'the way out is below the step and hands on the stepped values', '''
  (func $stepped (export "stepped") (param $n i32) (result i32)
    (local $i i32) (local $j i32)
    (local.set $j (i32.const 100))
    (loop $l
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (i32.xor (local.get $j) (i32.shl (local.get $i) (i32.const 16))))
''', stepped, NS, (2, 0, 0))


def oldafter(n):
    i, j = 38, 1000
    while True:
        o = j
        j -= 1
        i -= 1
        if not MEM[i] > n:
            break
    return o + i * 7


case('oldafter', 'counting down, the way out below the step hands on the value before it', '''
  (func $oldafter (export "oldafter") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $o i32)
    (local.set $i (i32.const 38))
    (local.set $j (i32.const 1000))
    (loop $l
      (local.set $o (local.get $j))
      (local.set $j (i32.sub (local.get $j) (i32.const 1)))
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (br_if $l (i32.gt_u (i32.load8_u (local.get $i)) (local.get $n))))
    (i32.add (local.get $o) (i32.mul (local.get $i) (i32.const 7))))
''', oldafter, [(0,), (2,), (4,), (9,)], (1, 0, 0), [(r'\sb\s', 0), (r'\smov\s', 2)])


def mixed(n):
    i, j = 38, 500
    while MEM[i] > n:
        i -= 1
        j -= 1
    return j * 5 - i


case('mixed', 'one steps by a subtraction of 1, the other by an addition of -1: alike', '''
  (func $mixed (export "mixed") (param $n i32) (result i32)
    (local $i i32) (local $j i32)
    (local.set $i (i32.const 38))
    (local.set $j (i32.const 500))
    (block $out
      (loop $l
        (br_if $out (i32.le_u (i32.load8_u (local.get $i)) (local.get $n)))
        (local.set $i (i32.sub (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const -1)))
        (br $l)))
    (i32.sub (i32.mul (local.get $j) (i32.const 5)) (local.get $i)))
''', mixed, [(0,), (2,), (4,), (9,)], (1, 0, 0))


def wide(n):
    i, j = 5, 0x700000003
    while i < n:
        i += 1
        j += 1
    return j ^ (i << 40)


case('wide', '64-bit counters more than 32 bits apart', '''
  (func $wide (export "wide") (param $n i64) (result i64)
    (local $i i64) (local $j i64)
    (local.set $i (i64.const 5))
    (local.set $j (i64.const 0x700000003))
    (block $out
      (loop $l
        (br_if $out (i64.ge_u (local.get $i) (local.get $n)))
        (local.set $i (i64.add (local.get $i) (i64.const 1)))
        (local.set $j (i64.add (local.get $j) (i64.const 1)))
        (br $l)))
    (i64.xor (local.get $j) (i64.shl (local.get $i) (i64.const 40))))
''', wide, [(0,), (5,), (6,), (40,)], (2, 0, 0), rtype='i64')


def threeout(a):
    p, c1, c2, c3 = a, 1, 10, 100
    while MEM[p]:
        p += 1
        c1 += 1
        c2 += 1
        c3 += 1
    return c1 + c2 * 100 + c3 * 10000 + p


case('threeout', 'three read only after the loop, beside a pointer that starts as no constant: '
     'the first stays for the other two', '''
  (func $threeout (export "threeout") (param $p i32) (result i32)
    (local $c1 i32) (local $c2 i32) (local $c3 i32)
    (local.set $c1 (i32.const 1))
    (local.set $c2 (i32.const 10))
    (local.set $c3 (i32.const 100))
    (block $out
      (loop $l
        (br_if $out (i32.eqz (i32.load8_u (local.get $p))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (local.set $c1 (i32.add (local.get $c1) (i32.const 1)))
        (local.set $c2 (i32.add (local.get $c2) (i32.const 1)))
        (local.set $c3 (i32.add (local.get $c3) (i32.const 1)))
        (br $l)))
    (i32.add (i32.add (local.get $c1) (i32.mul (local.get $c2) (i32.const 100)))
             (i32.add (i32.mul (local.get $c3) (i32.const 10000)) (local.get $p))))
''', threeout, [(0,), (5,), (9,), (16,), (48,)], (2, 0, 0))


def leadconst(a):
    p, b, c, s = a, 0, 5, 0
    while MEM[p]:
        s = (s + b * MEM[p]) & M32
        p += 1
        b += 1
        c += 1
    return s + c * 1000 + p


case('leadconst', 'of two that stay, the one a constant from it is the one it is made from', '''
  (func $leadconst (export "leadconst") (param $p i32) (result i32)
    (local $b i32) (local $c i32) (local $s i32)
    (local.set $c (i32.const 5))
    (block $out
      (loop $l
        (br_if $out (i32.eqz (i32.load8_u (local.get $p))))
        (local.set $s (i32.add (local.get $s) (i32.mul (local.get $b) (i32.load8_u (local.get $p)))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (local.set $b (i32.add (local.get $b) (i32.const 1)))
        (local.set $c (i32.add (local.get $c) (i32.const 1)))
        (br $l)))
    (i32.add (i32.add (local.get $s) (i32.mul (local.get $c) (i32.const 1000))) (local.get $p)))
''', leadconst, [(0,), (5,), (9,), (16,), (48,)], (1, 0, 0))


def ret(n):
    i, j = 0, 40
    while True:
        if MEM[i] == n:
            return j
        if MEM[i] == 0:
            break
        i += 1
        j += 1
    return i - 1000


case('ret', 'a return in the loop is a way out as any other', '''
  (func $ret (export "ret") (param $n i32) (result i32)
    (local $i i32) (local $j i32)
    (local.set $j (i32.const 40))
    (block $out
      (loop $l
        (if (i32.eq (i32.load8_u (local.get $i)) (local.get $n)) (then (return (local.get $j))))
        (br_if $out (i32.eqz (i32.load8_u (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.sub (local.get $i) (i32.const 1000)))
''', ret, [(3,), (9,), (5,), (0,), (77,)], (1, 0, 0))


def twoback(a):
    i, j, s = a, a, 0
    while MEM[i]:
        c = MEM[i]
        i += 1
        j += 1
        if c & 1:
            continue
        s += c
    return s * 100 + j


case('twoback', 'two ways round, each with the same stepped values', '''
  (func $twoback (export "twoback") (param $a i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32) (local $c i32)
    (local.set $i (local.get $a))
    (local.set $j (local.get $a))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $i)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (if (i32.and (local.get $c) (i32.const 1))
          (then (i32.store8 (i32.const 200) (local.get $c)) (br $l)))
        (local.set $s (i32.add (local.get $s) (local.get $c)))
        (br $l)))
    (i32.add (i32.mul (local.get $s) (i32.const 100)) (local.get $j)))
''', twoback, [(0,), (4,), (16,), (48,), (52,)], (1, 0, 0))


def twowaysrot(a, n):
    i, j = (0, 5) if a else (2, 7)
    s = 0
    while i < n:
        s = (s * 7 + i) & M32
        i += 1
        j += 1
    return s + j * 4096


case('twowaysrot', 'two ways to a test ahead of the loop, the two the same constant apart on both: through the parameters of where they meet', '''
  (func $twowaysrot (export "twowaysrot") (param $a i32) (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (if (local.get $a)
      (then (local.set $j (i32.const 5)) (i32.store8 (i32.const 201) (local.get $a)))
      (else (local.set $i (i32.const 2)) (local.set $j (i32.const 7)) (i32.store8 (i32.const 202) (local.get $n))))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 7)) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 4096))))
''', twowaysrot, [(0, 0), (1, 0), (0, 6), (1, 6), (0, 3), (1, 1)], (2, 0, 0))


def nested(n):
    a, b, t = 0, 30, 0
    while a < n:
        c, d = 0, 9
        while MEM[48 + c] > a:
            c += 1
            d += 1
        t = (t * 11 + d) & M32
        a += 1
        b += 1
    return t + b * 100000


case('nested', 'a loop in a loop, each with one to take out', '''
  (func $nested (export "nested") (param $n i32) (result i32)
    (local $a i32) (local $b i32) (local $c i32) (local $d i32) (local $t i32)
    (local.set $b (i32.const 30))
    (block $out
      (loop $o
        (br_if $out (i32.ge_u (local.get $a) (local.get $n)))
        (local.set $c (i32.const 0))
        (local.set $d (i32.const 9))
        (block $in
          (loop $l
            (br_if $in (i32.le_u (i32.load8_u offset=48 (local.get $c)) (local.get $a)))
            (local.set $c (i32.add (local.get $c) (i32.const 1)))
            (local.set $d (i32.add (local.get $d) (i32.const 1)))
            (br $l)))
        (local.set $t (i32.add (i32.mul (local.get $t) (i32.const 11)) (local.get $d)))
        (local.set $a (i32.add (local.get $a) (i32.const 1)))
        (local.set $b (i32.add (local.get $b) (i32.const 1)))
        (br $o)))
    (i32.add (local.get $t) (i32.mul (local.get $b) (i32.const 100000))))
''', nested, [(0,), (1,), (3,), (7,), (9,)], (2, 0, 0))


def sibling(a):
    i, j = 0, 7
    while i < a:
        i += 1
        j += 1
    k = m = j
    while MEM[k] == 0:
        k += 1
        m += 1
    return m * 3 + k + i * 1000


case('sibling', 'a loop whose way out leads into the next: each has one taken out', '''
  (func $sibling (export "sibling") (param $a i32) (result i32)
    (local $i i32) (local $j i32) (local $k i32) (local $m i32)
    (local.set $j (i32.const 7))
    (block $o1
      (loop $l1
        (br_if $o1 (i32.ge_u (local.get $i) (local.get $a)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l1)))
    (local.set $k (local.get $j))
    (local.set $m (local.get $j))
    (block $o2
      (loop $l2
        (br_if $o2 (i32.load8_u (local.get $k)))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (local.set $m (i32.add (local.get $m) (i32.const 1)))
        (br $l2)))
    (i32.add (i32.add (i32.mul (local.get $m) (i32.const 3)) (local.get $k)) (i32.mul (local.get $i) (i32.const 1000))))
''', sibling, [(0,), (2,), (5,), (9,)], (3, 0, 0))


def many(n):
    c = list(range(1, 18))
    i, s = 0, 0
    while i < n:
        for k in range(15):
            s = (s * 3 + c[k]) & M32
        c = [x + 1 for x in c]
        i += 1
    return (s + c[15] * 7 + c[16] * 1000) & M32


MANY_LOCALS = ' '.join('(local $c%d i32)' % k for k in range(17))
MANY_INIT = '\n'.join('    (local.set $c%d (i32.const %d))' % (k, k + 1) for k in range(17))
MANY_READ = '\n'.join('        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $c%d)))' % k
                      for k in range(15))
MANY_STEP = '\n'.join('        (local.set $c%d (i32.add (local.get $c%d) (i32.const 1)))' % (k, k) for k in range(17))
case('many', 'seventeen counters, the last two read only after the loop: sixteen are looked at, '
     'and the sixteenth taken out', '''
  (func $many (export "many") (param $n i32) (result i32)
    %s (local $i i32) (local $s i32)
%s
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (i32.load (i32.const 208)) (local.get $n)))
%s
%s
        (i32.store (i32.const 208) (i32.add (i32.load (i32.const 208)) (i32.const 1)))
        (br $l)))
    (i32.store (i32.const 208) (i32.const 0))
    (i32.add (local.get $s) (i32.add (i32.mul (local.get $c15) (i32.const 7)) (i32.mul (local.get $c16) (i32.const 1000)))))
''' % (MANY_LOCALS, MANY_INIT, MANY_READ, MANY_STEP), many, [(0,), (1,), (3,), (6,)], (1, 0, 0))



def twoways_model(ia, ja, ib, jb, andi=False):
    def f(a, n):
        i, j = (ia, ja) if a else (ib, jb)
        s = 0
        while True:
            s = (s * 7 + ((i & 15) + 1 if andi else MEM[i])) & M32
            i = (i + 1) & M32
            j = (j + 1) & M32
            if not s < n:
                break
        return (s + j * 4096) & M32
    return f


TWOWAYS = """
  (func $%(name)s (export "%(name)s") (param $a i32) (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (if (local.get $a)
      (then (local.set $i (i32.const %(ia)d)) (local.set $j (i32.const %(ja)d)) (i32.store8 (i32.const %(m)d) (local.get $a)))
      (else (local.set $i (i32.const %(ib)d)) (local.set $j (i32.const %(jb)d)) (i32.store8 (i32.const %(m)d) (local.get $n))))
    (loop $l
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 7)) %(term)s))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 4096))))
"""
TW_RUNS = [(0, 0), (1, 0), (0, 50), (1, 50), (0, 3000), (1, 3000)]
case('twoways', 'two ways into the loop, the two the same constant apart on both',
     TWOWAYS % dict(name='twoways', ia=0, ja=5, ib=2, jb=7, m=201, term='(i32.load8_u (local.get $i))'),
     twoways_model(0, 5, 2, 7), TW_RUNS, (1, 0, 0))
case('twowayswrap', 'the same constant apart only in 32 bits: 20 and 37 one way, -16 and 1 the other',
     TWOWAYS % dict(name='twowayswrap', ia=20, ja=37, ib=-16, jb=1, m=217,
                    term='(i32.add (i32.and (local.get $i) (i32.const 15)) (i32.const 1))'),
     twoways_model(20, 37, -16 & M32, 1, andi=True), TW_RUNS, (1, 0, 0))


def leadlater(n):
    j, i, k, s = 5, 0, 9, 0
    while True:
        j += 1
        if not (i < n and MEM[i]):
            break
        s = (s * 3 + i) & M32
        i += 1
        k += 1
    return j * 100 + k * 10000 + s


case('leadlater', 'the one that stays comes after the first taken out, among the parameters: '
     'the third is made from it too, not from the first', """
  (func $leadlater (export "leadlater") (param $n i32) (result i32)
    (local $j i32) (local $i i32) (local $k i32) (local $s i32)
    (local.set $j (i32.const 5))
    (local.set $k (i32.const 9))
    (block $out
      (loop $l
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (br_if $out (i32.eqz (i32.load8_u (local.get $i))))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br $l)))
    (i32.add (i32.add (i32.mul (local.get $j) (i32.const 100)) (i32.mul (local.get $k) (i32.const 10000))) (local.get $s)))
""", leadlater, [(0,), (1,), (5,), (9,), (30,)], (2, 0, 0))


def wrapzero(n):
    j, i, s = M32, 0, 0
    while True:
        j = (j + 1) & M32
        if not (i < n and MEM[i]):
            break
        s = (s * 3 + i) & M32
        i += 1
    return (j * 100 + s) & M32


case('wrapzero', 'the one taken out starts at -1 and is stepped above the way out, the other at 0 and below '
     'it: what leaves is the counter that stays, with nothing added', """
  (func $wrapzero (export "wrapzero") (param $n i32) (result i32)
    (local $j i32) (local $i i32) (local $s i32)
    (local.set $j (i32.const -1))
    (block $out
      (loop $l
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (br_if $out (i32.eqz (i32.load8_u (local.get $i))))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $j) (i32.const 100)) (local.get $s)))
""", wrapzero, [(0,), (1,), (5,), (9,), (30,)], (1, 0, 0), [(r'\sadd\s', 3), (r'\smov\s', 3)])


def strip_model(fi, fj):
    def f(x, n):
        i, j, s = fi(x) & M32, fj(x) & M32, 0
        while True:
            s = (s * 7 + MEM[i]) & M32
            i += 1
            j = (j + 1) & M32
            if not s < n:
                break
        return (s + j * 4096) & M32
    return f


STRIP = """
  (func $%(name)s (export "%(name)s") (param $x i32) (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (local.set $i %(i)s)
    (local.set $j %(j)s)
    (loop $l
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 7)) (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 4096))))
"""
STRIP_RUNS = [(0, 0), (0, 50), (3, 50), (5, 3000), (2, 0)]
case('stripadd', 'the two start as one value plus a constant and the value less another',
     STRIP % dict(name='stripadd', i='(i32.add (local.get $x) (i32.const 3))', j='(i32.sub (local.get $x) (i32.const 2))'),
     strip_model(lambda x: x + 3, lambda x: x - 2), STRIP_RUNS, (1, 0, 0))


def mergeoff(a, n):
    i = n & 3 if a else n & 1
    return strip_model(lambda x: x, lambda x: x + 8)(i, n)


case('mergeoff', 'two ways meet, the two 5 apart on both, and a constant is added to the one after: 8 apart', """
  (func $mergeoff (export "mergeoff") (param $a i32) (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (if (local.get $a)
      (then (local.set $i (i32.and (local.get $n) (i32.const 3)))
            (local.set $j (i32.add (i32.and (local.get $n) (i32.const 3)) (i32.const 5)))
            (i32.store8 (i32.const 223) (local.get $a)))
      (else (local.set $i (i32.and (local.get $n) (i32.const 1)))
            (local.set $j (i32.add (i32.and (local.get $n) (i32.const 1)) (i32.const 5)))
            (i32.store8 (i32.const 224) (local.get $n))))
    (local.set $j (i32.add (local.get $j) (i32.const 3)))
    (loop $l
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 7)) (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 4096))))
""", mergeoff, [(0, 0), (1, 0), (0, 51), (1, 51), (0, 3002), (1, 3002)], (1, 0, 0))


def goneclosed(a):
    i, j = a, a
    while MEM[i]:
        i += 1
        j += 1
    j = (j + 40) & M32
    k, t = 16, 0
    while MEM[k]:
        t = (t * 3 + j) & M32
        k += 1
    return (t + i) & M32


case('goneclosed', 'a loop past the loop reads the one taken out, as the parameter of the block the one way '
     'out leads to: the edge hands on the counter that stays, plus the constant made on the edge', """
  (func $goneclosed (export "goneclosed") (param $a i32) (result i32)
    (local $i i32) (local $j i32) (local $k i32) (local $t i32)
    (local.set $i (local.get $a))
    (local.set $j (i32.add (local.get $a) (i32.const 40)))
    (block $o1
      (loop $l
        (br_if $o1 (i32.eqz (i32.load8_u (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (local.set $k (i32.const 16))
    (block $out
      (loop $m
        (br_if $out (i32.eqz (i32.load8_u (local.get $k))))
        (local.set $t (i32.add (i32.mul (local.get $t) (i32.const 3)) (local.get $j)))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br $m)))
    (i32.add (local.get $t) (local.get $i)))
""", goneclosed, [(0,), (4,), (9,), (16,), (48,)], (1, 0, 0), [(r'\sadd\s', 4, 'loop')])

# -- readers past the loop ----------------------------------------------------------


def ramp(n):
    """where the scan down the ramp from 38 stops, and the trips it took"""
    i = 38
    while True:
        i -= 1
        if not MEM[i] > n:
            return i


case('outret', 'the function returns the stepped value of the one taken out', """
  (func $outret (export "outret") (param $n i32) (result i32)
    (local $i i32) (local $j i32)
    (local.set $i (i32.const 38))
    (local.set $j (i32.const 70))
    (loop $l
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (local.set $j (i32.sub (local.get $j) (i32.const 1)))
      (br_if $l (i32.gt_u (i32.load8_u (local.get $i)) (local.get $n))))
    (local.get $j))
""", lambda n: ramp(n) + 32, [(0,), (2,), (4,), (9,)], (1, 0, 0))

case('outbrif', 'a branch past the loop is on it', """
  (func $outbrif (export "outbrif") (param $n i32) (result i32)
    (local $i i32) (local $j i32)
    (local.set $i (i32.const 38))
    (local.set $j (i32.const 4))
    (loop $l
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (local.set $j (i32.sub (local.get $j) (i32.const 1)))
      (br_if $l (i32.gt_u (i32.load8_u (local.get $i)) (local.get $n))))
    (if (result i32) (local.get $j)
      (then (i32.store8 (i32.const 210) (local.get $n)) (i32.const 111))
      (else (i32.store8 (i32.const 210) (local.get $i)) (i32.mul (local.get $i) (i32.const 3)))))
""", lambda n: 111 if ramp(n) != 34 else 102, [(0,), (2,), (4,), (9,)], (1, 0, 0))

case('outedge', 'an arm past the loop hands it on', """
  (func $outedge (export "outedge") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $x i32)
    (local.set $i (i32.const 38))
    (local.set $j (i32.const 90))
    (local.set $x (i32.const 5))
    (loop $l
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (local.set $j (i32.sub (local.get $j) (i32.const 1)))
      (br_if $l (i32.gt_u (i32.load8_u (local.get $i)) (local.get $n))))
    (if (i32.and (local.get $n) (i32.const 1))
      (then (local.set $x (local.get $j)) (i32.store8 (i32.const 211) (local.get $n))))
    (i32.add (local.get $x) (i32.mul (local.get $i) (i32.const 1000))))
""", lambda n: (ramp(n) + 52 if n & 1 else 5) + ramp(n) * 1000, [(0,), (1,), (2,), (3,), (9,)], (1, 0, 0))


def outloop(n):
    i = ramp(n)
    j = i + 12
    k, t = 16, 0
    while MEM[k]:
        t = (t * 3 + j) & M32
        k += 1
    return t + i


case('outloop', 'a loop past the loop reads it, each time round: it stays', """
  (func $outloop (export "outloop") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $k i32) (local $t i32)
    (local.set $i (i32.const 38))
    (local.set $j (i32.const 50))
    (loop $l
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (local.set $j (i32.sub (local.get $j) (i32.const 1)))
      (br_if $l (i32.gt_u (i32.load8_u (local.get $i)) (local.get $n))))
    (local.set $k (i32.const 16))
    (block $out
      (loop $m
        (br_if $out (i32.eqz (i32.load8_u (local.get $k))))
        (local.set $t (i32.add (i32.mul (local.get $t) (i32.const 3)) (local.get $j)))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br $m)))
    (i32.add (local.get $t) (local.get $i)))
""", outloop, [(0,), (2,), (4,), (9,)], (0, 0, 0))

case('ehread', 'a handler past the loop reads it, come to from a call in the loop', """
  (func $ehread (export "ehread") (param $n i32) (result i32)
    (local $i i32) (local $j i32)
    (local.set $j (i32.const 20))
    (block $h
      (try_table (catch_all $h)
        (block $out
          (loop $l
            (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
            (call_indirect (type $t1) (local.get $i) (i32.load8_u (i32.const 220)))
            (local.set $i (i32.add (local.get $i) (i32.const 1)))
            (local.set $j (i32.add (local.get $j) (i32.const 1)))
            (br $l)))
        (return (i32.add (local.get $j) (i32.const 1000)))))
    (i32.mul (local.get $j) (i32.const 7)))
""", lambda n: 1020 + n if n <= 3 else 161, [(0,), (2,), (3,), (4,), (9,)], (1, 0, 0))

case('ehblock', 'handed on by the edge out of a block past the loop that ends in a call a handler stands '
     'behind: nothing can be made after that call, and it stays', """
  (func $ehblock (export "ehblock") (param $n i32) (param $c i32) (result i32)
    (local $i i32) (local $j i32) (local $r i32)
    (local.set $i (i32.const 38))
    (local.set $j (i32.const 70))
    (loop $l
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (local.set $j (i32.sub (local.get $j) (i32.const 1)))
      (br_if $l (i32.gt_u (i32.load8_u (local.get $i)) (local.get $n))))
    (block $h
      (try_table (catch_all $h)
        (if (local.get $c)
          (then (call_indirect (type $t1) (local.get $c) (i32.load8_u (i32.const 220)))
                (local.set $r (local.get $j)))
          (else (i32.store8 (i32.const 225) (local.get $n)) (local.set $r (i32.const 5))))
        (return (i32.add (local.get $r) (i32.mul (local.get $i) (i32.const 1000))))))
    (i32.const -7))
""", lambda n, c: -7 if c == 3 else (ramp(n) + 32 if c else 5) + ramp(n) * 1000,
     [(0, 0), (0, 1), (0, 3), (4, 2), (9, 1)], (0, 0, 0))

# -- counters that stay -----------------------------------------------------------


def types(n):
    i, j = 0, 0x700000005
    while i < n:
        i += 1
        j += 1
    return j ^ (i << 20)


case('types', 'a 32-bit counter and a 64-bit one: both stay', '''
  (func $types (export "types") (param $n i32) (result i64)
    (local $i i32) (local $j i64)
    (local.set $j (i64.const 0x700000005))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i64.add (local.get $j) (i64.const 1)))
        (br $l)))
    (i64.xor (local.get $j) (i64.shl (i64.extend_i32_u (local.get $i)) (i64.const 20))))
''', types, NS, (0, 0, 0), rtype='i64')


def steps(n):
    i, j, s = 0, 4, 0
    while i < n:
        s = (s * 3 + i) & M32
        i += 1
        j += 2
    return j * 1000 + s


case('steps', 'steps of 1 and of 2: both stay', '''
  (func $steps (export "steps") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (local.set $j (i32.const 4))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 2)))
        (br $l)))
    (i32.add (i32.mul (local.get $j) (i32.const 1000)) (local.get $s)))
''', steps, NS, (0, 0, 0))


def runtime(a, b):
    i, j = a, b
    while MEM[i]:
        i += 1
        j += 1
    return j * 1000 + i


case('runtime', 'two that start as two values, no constants: both stay', '''
  (func $runtime (export "runtime") (param $i i32) (param $j i32) (result i32)
    (block $out
      (loop $l
        (br_if $out (i32.eqz (i32.load8_u (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $j) (i32.const 1000)) (local.get $i)))
''', runtime, [(0, 0), (0, 50), (5, 2), (9, 9), (16, 1)], (0, 0, 0))


def halfconst(a):
    i, j = a, 6
    while MEM[i]:
        i += 1
        j += 1
    return j * 1000 + i


case('halfconst', 'one starts as a constant, the other as a value: both stay', '''
  (func $halfconst (export "halfconst") (param $i i32) (result i32)
    (local $j i32)
    (local.set $j (i32.const 6))
    (block $out
      (loop $l
        (br_if $out (i32.eqz (i32.load8_u (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $j) (i32.const 1000)) (local.get $i)))
''', halfconst, [(0,), (5,), (9,), (16,)], (0, 0, 0))


def allread(n):
    i, j, s = 0, 11, 0
    while i < n:
        s = (s * 3 + i * j) & M32
        i += 1
        j += 1
    return s + j


case('allread', 'the loop reads both: both stay', '''
  (func $allread (export "allread") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (local.set $j (i32.const 11))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (i32.mul (local.get $i) (local.get $j))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (local.get $j)))
''', allread, NS, (0, 0, 0))


def stepread(n):
    i, j, s = 0, 11, 0
    while i < n:
        i += 1
        j += 1
        s = (s * 3) ^ j
        s &= M32
    return s + j


case('stepread', 'the loop reads the stepped value of the second: it stays', '''
  (func $stepread (export "stepread") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (local.set $j (i32.const 11))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 3)) (local.get $j)))
        (br $l)))
    (i32.add (local.get $s) (local.get $j)))
''', stepread, NS, (0, 0, 0))


def brifon(n):
    i = j = n
    s = 0
    while j:
        s = (s * 3 + MEM[i]) & M32
        j -= 1
        i -= 1
    return s + 7


case('brifon', 'the loop branches on the second, and reads no more of it: it stays', '''
  (func $brifon (export "brifon") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (local.set $i (local.get $n))
    (local.set $j (local.get $n))
    (loop $l
      (if (local.get $j)
        (then
          (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (i32.load8_u (local.get $i))))
          (local.set $j (i32.sub (local.get $j) (i32.const 1)))
          (local.set $i (i32.sub (local.get $i) (i32.const 1)))
          (br $l))))
    (i32.add (local.get $s) (i32.const 7)))
''', brifon, [(0,), (1,), (4,), (8,)], (0, 0, 0))


def brifstep(n):
    i = j = n
    s = 0
    while True:
        s = (s * 5 + MEM[i]) & M32
        j -= 1
        i -= 1
        if not j:
            break
    return s + 9


case('brifstep', 'the loop branches on its stepped value: it stays', '''
  (func $brifstep (export "brifstep") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (local.set $i (local.get $n))
    (local.set $j (local.get $n))
    (loop $l
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 5)) (i32.load8_u (local.get $i))))
      (local.set $j (i32.sub (local.get $j) (i32.const 1)))
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (br_if $l (local.get $j)))
    (i32.add (local.get $s) (i32.const 9)))
''', brifstep, [(1,), (2,), (4,), (8,)], (0, 0, 0))


def inneredge(n):
    i, j, x = 0, 50, 0
    while i < n:
        if MEM[i] & 1:
            x = j
        i += 1
        j += 1
    return x * 100 + j


case('inneredge', 'an arm in the loop hands the second on to where the arms meet: it stays', '''
  (func $inneredge (export "inneredge") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $x i32)
    (local.set $j (i32.const 50))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (if (i32.and (i32.load8_u (local.get $i)) (i32.const 1))
          (then (local.set $x (local.get $j)) (i32.store8 (i32.const 203) (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $x) (i32.const 100)) (local.get $j)))
''', inneredge, NS, (0, 0, 0))


def prev(n):
    c, j, p, t = 0, 7, 0, 0
    while c < n and MEM[c]:
        t = (t * 3 + (c ^ p)) & M32
        c += 1
        j += 1
        p = j
    return t + n


case('prev', 'the way round hands its stepped value to another parameter, which the loop reads: it stays', '''
  (func $prev (export "prev") (param $n i32) (result i32)
    (local $c i32) (local $j i32) (local $p i32) (local $t i32)
    (local.set $j (i32.const 7))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $c) (local.get $n)))
        (br_if $out (i32.eqz (i32.load8_u (local.get $c))))
        (local.set $t (i32.add (i32.mul (local.get $t) (i32.const 3)) (i32.xor (local.get $c) (local.get $p))))
        (local.set $c (i32.add (local.get $c) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (local.set $p (local.get $j))
        (br $l)))
    (i32.add (local.get $t) (local.get $n)))
''', prev, [(0,), (1,), (2,), (5,), (9,), (30,)], (0, 0, 0))


def twobackdiff(a):
    i, j, s = a, a, 0
    while MEM[i]:
        c = MEM[i]
        i += 1
        if c & 1:
            j += 1
            continue
        j += 2
        s += c
    return s * 100 + j


case('twobackdiff', 'two ways round, the second stepped by 1 on one and by 2 on the other: no counter', '''
  (func $twobackdiff (export "twobackdiff") (param $a i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32) (local $c i32)
    (local.set $i (local.get $a))
    (local.set $j (local.get $a))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $i)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (if (i32.and (local.get $c) (i32.const 1))
          (then (local.set $j (i32.add (local.get $j) (i32.const 1)))
                (i32.store8 (i32.const 204) (local.get $c)) (br $l)))
        (local.set $j (i32.add (local.get $j) (i32.const 2)))
        (local.set $s (i32.add (local.get $s) (local.get $c)))
        (br $l)))
    (i32.add (i32.mul (local.get $s) (i32.const 100)) (local.get $j)))
''', twobackdiff, [(0,), (4,), (16,), (48,), (52,)], (0, 0, 0))


def twowaysrotdiff(a, n):
    i, j = (0, 5) if a else (2, 11)
    s = 0
    while i < n:
        s = (s * 7 + i) & M32
        i += 1
        j += 1
    return s + j * 4096


case('twowaysrotdiff', 'two ways to the test, the two 5 apart on one and 9 on the other: both stay', '''
  (func $twowaysrotdiff (export "twowaysrotdiff") (param $a i32) (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (if (local.get $a)
      (then (local.set $j (i32.const 5)) (i32.store8 (i32.const 205) (local.get $a)))
      (else (local.set $i (i32.const 2)) (local.set $j (i32.const 11)) (i32.store8 (i32.const 206) (local.get $n))))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 7)) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 4096))))
''', twowaysrotdiff, [(0, 0), (1, 0), (0, 6), (1, 6), (0, 3), (1, 1)], (0, 0, 0))


case('twowaysdiff', 'two ways in, the two 5 apart on one and 9 on the other: both stay',
     TWOWAYS % dict(name='twowaysdiff', ia=0, ja=5, ib=2, jb=11, m=205, term='(i32.load8_u (local.get $i))'),
     twoways_model(0, 5, 2, 11), TW_RUNS, (0, 0, 0))


def xorstep(n):
    i, q = 38, 6
    while MEM[i] > n:
        i -= 1
        q ^= 1
    return q * 1000 + i


case('xorstep', 'an xor with 1 each time round is no step', """
  (func $xorstep (export "xorstep") (param $n i32) (result i32)
    (local $i i32) (local $q i32)
    (local.set $i (i32.const 38))
    (local.set $q (i32.const 6))
    (block $out
      (loop $l
        (br_if $out (i32.le_u (i32.load8_u (local.get $i)) (local.get $n)))
        (local.set $i (i32.sub (local.get $i) (i32.const 1)))
        (local.set $q (i32.xor (local.get $q) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $q) (i32.const 1000)) (local.get $i)))
""", xorstep, [(0,), (3,), (4,), (5,), (9,)], (0, 0, 0))


def updown(n):
    i, j = 0, 100
    while i < n and MEM[i]:
        i += 1
        j -= 1
    return j * 1000 + i


case('updown', 'one counts up and one down: both stay', """
  (func $updown (export "updown") (param $n i32) (result i32)
    (local $i i32) (local $j i32)
    (local.set $j (i32.const 100))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (br_if $out (i32.eqz (i32.load8_u (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.sub (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $j) (i32.const 1000)) (local.get $i)))
""", updown, [(0,), (1,), (5,), (30,)], (0, 0, 0))


def outloopedge(n):
    i = ramp(n)
    return (i + 12) + i * 100


case('outloopedge', 'an arm in a loop past the loop hands it on: it stays', """
  (func $outloopedge (export "outloopedge") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $k i32) (local $x i32)
    (local.set $i (i32.const 38))
    (local.set $j (i32.const 50))
    (loop $l
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (local.set $j (i32.sub (local.get $j) (i32.const 1)))
      (br_if $l (i32.gt_u (i32.load8_u (local.get $i)) (local.get $n))))
    (local.set $k (i32.const 16))
    (block $out
      (loop $m
        (br_if $out (i32.eqz (i32.load8_u (local.get $k))))
        (if (i32.and (local.get $k) (i32.const 1))
          (then (local.set $x (local.get $j)) (i32.store8 (i32.const 213) (local.get $k))))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br $m)))
    (i32.add (local.get $x) (i32.mul (local.get $i) (i32.const 100))))
""", outloopedge, [(0,), (2,), (4,), (9,)], (0, 0, 0))


case('stripand', 'one starts as the other masked: both stay',
     STRIP % dict(name='stripand', i='(i32.and (local.get $x) (i32.const 15))', j='(local.get $x)'),
     strip_model(lambda x: x & 15, lambda x: x), [(0, 0), (0, 50), (3, 50), (0x102, 3000), (20, 0)], (0, 0, 0))


def swap(n):
    c, q, p = 0, 7, 0
    while c < n and MEM[c]:
        c += 1
        p = q + 1
        q += 2
    return p * 100 + c + q * 10000


case('swap', 'a parameter handed another\'s value plus a constant, not its own: no counter', """
  (func $swap (export "swap") (param $n i32) (result i32)
    (local $c i32) (local $q i32) (local $p i32)
    (local.set $q (i32.const 7))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $c) (local.get $n)))
        (br_if $out (i32.eqz (i32.load8_u (local.get $c))))
        (local.set $c (i32.add (local.get $c) (i32.const 1)))
        (local.set $p (i32.add (local.get $q) (i32.const 1)))
        (local.set $q (i32.add (local.get $q) (i32.const 2)))
        (br $l)))
    (i32.add (i32.add (i32.mul (local.get $p) (i32.const 100)) (local.get $c)) (i32.mul (local.get $q) (i32.const 10000))))
""", swap, [(0,), (1,), (2,), (5,), (30,)], (0, 0, 0))


def condstep_model(odd):
    def f(a):
        i, j, s = a, a, 0
        while MEM[i]:
            c = MEM[i]
            i += 1
            if c & 1:
                if odd:
                    j += 1
                continue
            if not odd:
                j += 1
            s += c
        return s * 100 + j
    return f


CONDSTEP = """
  (func $%(name)s (export "%(name)s") (param $a i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32) (local $c i32)
    (local.set $i (local.get $a))
    (local.set $j (local.get $a))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $i)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (if (i32.and (local.get $c) (i32.const 1))
          (then%(arm)s
                (i32.store8 (i32.const %(m)d) (local.get $c)) (br $l)))%(rest)s
        (local.set $s (i32.add (local.get $s) (local.get $c)))
        (br $l)))
    (i32.add (i32.mul (local.get $s) (i32.const 100)) (local.get $j)))
"""
STEP_J = ' (local.set $j (i32.add (local.get $j) (i32.const 1)))'
case('condstep', 'two ways round, the second stepped on one and handed on as it is on the other: no counter',
     CONDSTEP % dict(name='condstep', arm=STEP_J, rest='', m=219), condstep_model(True),
     [(0,), (4,), (16,), (48,), (52,)], (0, 0, 0))
case('condstep2', 'the same, stepped on the other way round',
     CONDSTEP % dict(name='condstep2', arm='', rest='\n       ' + STEP_J, m=222), condstep_model(False),
     [(0,), (4,), (16,), (48,), (52,)], (0, 0, 0))


def rsub(n):
    i, p = 0, 3
    while i < n:
        p = 1 - p
        i += 1
    return (p * 100 + i) & M32


case('rsub', 'a constant less the parameter is no step', '''
  (func $rsub (export "rsub") (param $n i32) (result i32)
    (local $i i32) (local $p i32)
    (local.set $p (i32.const 3))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $p (i32.sub (i32.const 1) (local.get $p)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $p) (i32.const 100)) (local.get $i)))
''', rsub, NS, (0, 0, 0))


def varstep(n):
    i, p = 0, 0
    while i < n:
        p += n
        i += 1
    return (p * 100 + i) & M32


case('varstep', 'a step by a value that is no constant', '''
  (func $varstep (export "varstep") (param $n i32) (result i32)
    (local $i i32) (local $p i32)
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $p (i32.add (local.get $p) (local.get $n)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $p) (i32.const 100)) (local.get $i)))
''', varstep, NS, (0, 0, 0))

# -- ways out given the stepped value ---------------------------------------------


def late(n):
    i, s = 0, 0
    while True:
        o = i
        s += MEM[i]
        i += 1
        if not s < n:
            break
    return o * 100 + s


case('late', 'the way out below the step hands on the counter as it was: the stepped value less the step', '''
  (func $late (export "late") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32)
    (loop $l
      (local.set $o (local.get $i))
      (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (i32.mul (local.get $o) (i32.const 100)) (local.get $s)))
''', late, [(0,), (4,), (11,), (20,), (45,)], (0, 1, 0), [(r'\sb\s', 0), (r'\ssub\s', 1)])


def late64(n):
    i, s = 38, 0
    while True:
        o = i
        s += MEM[i]
        i -= 1
        if not s < n:
            break
    return (o << 32) + s


case('late64', 'a 64-bit one counting down', '''
  (func $late64 (export "late64") (param $n i64) (result i64)
    (local $i i64) (local $s i64) (local $o i64)
    (local.set $i (i64.const 38))
    (loop $l
      (local.set $o (local.get $i))
      (local.set $s (i64.add (local.get $s) (i64.load8_u (i32.wrap_i64 (local.get $i)))))
      (local.set $i (i64.sub (local.get $i) (i64.const 1)))
      (br_if $l (i64.lt_u (local.get $s) (local.get $n))))
    (i64.add (i64.shl (local.get $o) (i64.const 32)) (local.get $s)))
''', late64, [(0,), (6,), (12,), (20,)], (0, 1, 0), [(r'\sb\s', 0)], rtype='i64')


def lateafter(n):
    i, s = 0, 0
    while True:
        o = i
        q = i + 1
        s += MEM[q] * i
        i = q
        if not s < n:
            break
    return o * 100 + s


case('lateafter', 'the loop reads the counter after its step: the way out keeps the counter', '''
  (func $lateafter (export "lateafter") (param $n i32) (result i32)
    (local $i i32) (local $q i32) (local $s i32) (local $o i32)
    (loop $l
      (local.set $o (local.get $i))
      (local.set $q (i32.add (local.get $i) (i32.const 1)))
      (local.set $s (i32.add (local.get $s) (i32.mul (i32.load8_u (local.get $q)) (local.get $i))))
      (local.set $i (local.get $q))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (i32.mul (local.get $o) (i32.const 100)) (local.get $s)))
''', lateafter, [(0,), (4,), (30,), (90,)], (0, 0, 0))


def lateabove(n, m):
    i, s = 0, 0
    while True:
        if MEM[i] == m:
            return i + 5000
        o = i
        s += MEM[i]
        i += 1
        if not s < n:
            return o * 100 + s


case('lateabove', 'a way out above the step keeps the counter, one below reads the stepped value', '''
  (func $lateabove (export "lateabove") (param $n i32) (param $m i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32) (local $r i32)
    (local.set $r
      (block $b (result i32)
        (return
          (i32.add (i32.const 5000)
            (block $a (result i32)
              (loop $l
                (drop (br_if $a (local.get $i) (i32.eq (i32.load8_u (local.get $i)) (local.get $m))))
                (local.set $o (local.get $i))
                (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
                (local.set $i (i32.add (local.get $i) (i32.const 1)))
                (drop (br_if $b (local.get $o) (i32.ge_u (local.get $s) (local.get $n))))
                (br $l))
              (unreachable))))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $s)))
''', lateabove, [(0, 77), (11, 77), (11, 1), (45, 4), (45, 0), (3, 3)], (0, 1, 0))


def latebrif(n):
    i, s = n, 0
    while True:
        o = i
        s = (s * 3 + MEM[i]) & M32
        i -= 1
        if not o:
            break
    return o + s


case('latebrif', 'the loop branches on the counter as it was, below the step: the way out keeps it', '''
  (func $latebrif (export "latebrif") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32)
    (local.set $i (local.get $n))
    (loop $l
      (local.set $o (local.get $i))
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (i32.load8_u (local.get $i))))
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (br_if $l (local.get $o)))
    (i32.add (local.get $o) (local.get $s)))
''', latebrif, [(0,), (1,), (4,), (8,)], (0, 0, 0))


def lateinner(n):
    i, s, x = 0, 0, 77
    while True:
        o = i
        s += MEM[i]
        i += 1
        if s & 1:
            x = o
        if not s < n:
            break
    return o * 100 + s + x * 10000


case('lateinner', 'an arm below the step hands the counter as it was on in the loop: the way out keeps it', '''
  (func $lateinner (export "lateinner") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32) (local $x i32)
    (local.set $x (i32.const 77))
    (loop $l
      (local.set $o (local.get $i))
      (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (if (i32.and (local.get $s) (i32.const 1))
        (then (local.set $x (local.get $o)) (i32.store8 (i32.const 207) (local.get $s))))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (i32.add (i32.mul (local.get $o) (i32.const 100)) (local.get $s)) (i32.mul (local.get $x) (i32.const 10000))))
''', lateinner, [(0,), (4,), (11,), (20,), (45,)], (0, 0, 0))


def latebeside(n):
    i, s, x = 0, 0, 0
    while True:
        if MEM[i] & 1:
            x += i
        o = i
        s += MEM[i]
        i += 1
        if not s < n:
            break
    return o * 100 + s + x * 10000


case('latebeside', 'an arm above the step reads the counter, in a block that is not above the step\'s: '
     'read before the step all the same', '''
  (func $latebeside (export "latebeside") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32) (local $x i32)
    (loop $l
      (if (i32.and (i32.load8_u (local.get $i)) (i32.const 1))
        (then (local.set $x (i32.add (local.get $x) (local.get $i))) (i32.store8 (i32.const 209) (local.get $x))))
      (local.set $o (local.get $i))
      (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (i32.add (i32.mul (local.get $o) (i32.const 100)) (local.get $s)) (i32.mul (local.get $x) (i32.const 10000))))
''', latebeside, [(0,), (4,), (11,), (20,), (45,)], (0, 1, 0), [(r'\sb\s', 0)])



def late_o(n):
    i, s = 0, 0
    while True:
        o = i
        s += MEM[i]
        i += 1
        if not s < n:
            return o, s


LATE_LOOP = """
    (loop $l%(top)s
      (local.set $o (local.get $i))
      (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))%(mid)s
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))"""
LATE_NS = [(0,), (4,), (11,), (20,), (45,)]

case('lateret', 'the function returns the counter as it was', """
  (func $lateret (export "lateret") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32)%s
    (local.get $o))
""" % (LATE_LOOP % dict(top='', mid='')), lambda n: late_o(n)[0], LATE_NS, (0, 1, 0))


def lateclosed(n):
    o, s = late_o(n)
    return 13 * o + s


case('lateclosed', 'a loop past the loop reads the counter as it was, as the parameter of the block the one '
     'way out leads to: the edge is handed the stepped value', """
  (func $lateclosed (export "lateclosed") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32) (local $k i32) (local $t i32)%s
    (local.set $k (i32.const 16))
    (block $out
      (loop $m
        (br_if $out (i32.eqz (i32.load8_u (local.get $k))))
        (local.set $t (i32.add (i32.mul (local.get $t) (i32.const 3)) (local.get $o)))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br $m)))
    (i32.add (local.get $t) (local.get $s)))
""" % (LATE_LOOP % dict(top='', mid='')), lateclosed, LATE_NS, (0, 1, 0), [(r'\ssub\s', 0, 'loop')])


def lateoutloop(n):
    i, s = 0, 0
    while True:
        o = i
        s += MEM[i]
        i += 1
        if s >= n or s == 11:
            break
    return 13 * o + s


case('lateoutloop', 'two ways out lead to a loop that reads the counter as it was, each time round: '
     'the counter stays for it', """
  (func $lateoutloop (export "lateoutloop") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32) (local $k i32) (local $t i32) (local $r i32)
    (local.set $r
      (block $x (result i32)
        (loop $l
          (local.set $o (local.get $i))
          (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (drop (br_if $x (local.get $o) (i32.ge_u (local.get $s) (local.get $n))))
          (i32.store8 (i32.const 218) (local.get $s))
          (drop (br_if $x (local.get $o) (i32.eq (local.get $s) (i32.const 11))))
          (br $l))
        (unreachable)))
    (local.set $k (i32.const 16))
    (block $out
      (loop $m
        (br_if $out (i32.eqz (i32.load8_u (local.get $k))))
        (local.set $t (i32.add (i32.mul (local.get $t) (i32.const 3)) (local.get $r)))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br $m)))
    (i32.add (local.get $t) (local.get $s)))
""", lateoutloop, LATE_NS, (0, 0, 0))

case('lateafterblk', 'an arm below the step reads the counter as it was: the way out keeps it', """
  (func $lateafterblk (export "lateafterblk") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32)%s
    (i32.add (i32.mul (local.get $o) (i32.const 100)) (i32.add (local.get $s) (i32.load8_u (i32.const 214)))))
""" % (LATE_LOOP % dict(top='', mid="""
      (if (i32.and (local.get $s) (i32.const 1))
        (then (i32.store8 (i32.const 214) (local.get $o))))""")), lambda n: lateafterblk(n), LATE_NS, (0, 0, 0))


def lateafterblk(n):
    i, s, m = 0, 0, 0
    while True:
        o = i
        s += MEM[i]
        i += 1
        if s & 1:
            m = o
        if not s < n:
            return o * 100 + s + m


def latebrabove(n):
    o, s = late_o(n)
    return o * 100 + s


case('latebrabove', 'the loop branches on the counter above the step: the way out below reads the stepped value', """
  (func $latebrabove (export "latebrabove") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32)%s
    (i32.add (i32.mul (local.get $o) (i32.const 100)) (local.get $s)))
""" % (LATE_LOOP % dict(mid='', top="""
      (if (local.get $i) (then (i32.store8 (i32.const 215) (local.get $s))))""")), latebrabove, LATE_NS, (0, 1, 0))


def lateedgeabove(n):
    i, s, x = 0, 0, 0
    while True:
        if MEM[i] & 1:
            x = i
        o = i
        s += MEM[i]
        i += 1
        if not s < n:
            return o * 100 + s + x * 10000


case('lateedgeabove', 'an arm above the step hands the counter on in the loop: read before the step', """
  (func $lateedgeabove (export "lateedgeabove") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32) (local $x i32)%s
    (i32.add (i32.add (i32.mul (local.get $o) (i32.const 100)) (local.get $s)) (i32.mul (local.get $x) (i32.const 10000))))
""" % (LATE_LOOP % dict(mid='', top="""
      (if (i32.and (i32.load8_u (local.get $i)) (i32.const 1))
        (then (local.set $x (local.get $i)) (i32.store8 (i32.const 216) (local.get $i))))""")), lateedgeabove, LATE_NS, (0, 1, 0))


def lateaboveret(n, m):
    i, s = 0, 0
    while True:
        if MEM[i] == m:
            return i
        o = i
        s += MEM[i]
        i += 1
        if not s < n:
            return o * 100 + s


case('lateaboveret', 'a return above the step hands on the counter, the way out below reads the stepped value', """
  (func $lateaboveret (export "lateaboveret") (param $n i32) (param $m i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32)%s
    (i32.add (i32.mul (local.get $o) (i32.const 100)) (local.get $s)))
""" % (LATE_LOOP % dict(mid='', top="""
      (if (i32.eq (i32.load8_u (local.get $i)) (local.get $m)) (then (return (local.get $i))))""")),
     lateaboveret, [(0, 77), (11, 77), (11, 1), (45, 4), (45, 0), (3, 3)], (0, 1, 0))


def latemerge(n, m):
    i, s = 0, 0
    while True:
        if MEM[i] == m:
            return m + i
        o = i
        s += MEM[i]
        i += 1
        if s >= n:
            return m + o
        if s == 11:
            return 7000 + o


case('latemerge', 'a way out above the step and one below lead to one reader of the counter as it was: '
     'it stays for that reader, and a third way out keeps it too', """
  (func $latemerge (export "latemerge") (param $n i32) (param $m i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32)
    (i32.add (i32.const 7000)
      (block $y (result i32)
        (return
          (i32.add (local.get $m)
            (block $x (result i32)
              (loop $l
                (drop (br_if $x (local.get $i) (i32.eq (i32.load8_u (local.get $i)) (local.get $m))))
                (local.set $o (local.get $i))
                (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
                (local.set $i (i32.add (local.get $i) (i32.const 1)))
                (drop (br_if $x (local.get $o) (i32.ge_u (local.get $s) (local.get $n))))
                (drop (br_if $y (local.get $o) (i32.eq (local.get $s) (i32.const 11))))
                (br $l))
              (unreachable)))))))
""", latemerge, [(5, 77), (50, 77), (50, 7), (11, 77), (50, 1)], (0, 0, 0))


def latemergeret(n, m):
    i, s = 0, 0
    while True:
        if MEM[i] == m:
            return i
        o = i
        s += MEM[i]
        i += 1
        if s >= n:
            return o
        if s == 11:
            return 7000 + o


case('latemergeret', 'the same, the one reader the function\'s return', """
  (func $latemergeret (export "latemergeret") (param $n i32) (param $m i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32)
    (i32.add (i32.const 7000)
      (block $y (result i32)
        (return
          (block $x (result i32)
            (loop $l
              (drop (br_if $x (local.get $i) (i32.eq (i32.load8_u (local.get $i)) (local.get $m))))
              (local.set $o (local.get $i))
              (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
              (local.set $i (i32.add (local.get $i) (i32.const 1)))
              (drop (br_if $x (local.get $o) (i32.ge_u (local.get $s) (local.get $n))))
              (drop (br_if $y (local.get $o) (i32.eq (local.get $s) (i32.const 11))))
              (br $l))
            (unreachable))))))
""", latemergeret, [(5, 77), (50, 77), (50, 7), (11, 77), (50, 1)], (0, 0, 0))

# -- addresses -----------------------------------------------------------------------


def addr_model(k, two=False, self_=False):
    def f(b, n):
        i, s = 0, 0
        while True:
            q = i + 1
            x = ((i + i if self_ else b + i) + k) & M32
            s = (s * 3) ^ x
            if two:
                s ^= (b + i) << 8
            s &= M32
            i = q
            if not i < n:
                break
        return (s + i) & M32
    return f


ADDR = '''
  (func $%(name)s (export "%(name)s") (param $b i32) (param $n i32) (result i32)
    (local $i i32) (local $q i32) (local $s i32)
    (loop $l
      (local.set $q (i32.add (local.get $i) (i32.const 1)))
      (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 3)) %(addr)s))%(more)s
      (local.set $i (local.get $q))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (i32.add (local.get $s) (local.get $i)))
'''
ADDR_RUNS = [(100, 0), (100, 1), (7, 5), (0xfffffffe, 9)]
case('addr', 'an address of a value, the counter and the step, made after the step: of the stepped value',
     ADDR % dict(name='addr', more='',
                 addr='(i32.add (i32.add (local.get $b) (local.get $i)) (i32.const 1))'),
     addr_model(1), ADDR_RUNS, (0, 0, 1))
case('addrswap', 'the counter first',
     ADDR % dict(name='addrswap', more='',
                 addr='(i32.add (i32.add (local.get $i) (i32.mul (local.get $b) (i32.const 3))) (i32.const 1))'),
     lambda b, n: addr_model(1)((b * 3) & M32, n), ADDR_RUNS, (0, 0, 1))
case('addrk', 'the constant is not the step: it stays',
     ADDR % dict(name='addrk', more='',
                 addr='(i32.add (i32.add (local.get $b) (local.get $i)) (i32.const 2))'),
     addr_model(2), ADDR_RUNS, (0, 0, 0))
def addrtwo(b, n):
    i, s = 0, 0
    while True:
        s = ((s * 3) ^ (((b + i) << 8) & M32) ^ ((b + i + 1) & M32)) & M32
        i += 1
        if not i < n:
            break
    return (s + i) & M32


case('addrtwo', 'the sum of value and counter is read twice: it stays', """
  (func $addrtwo (export "addrtwo") (param $b i32) (param $n i32) (result i32)
    (local $i i32) (local $q i32) (local $s i32)
    (loop $l
      (local.set $q (i32.add (local.get $i) (i32.const 1)))
      (local.set $s (i32.xor (i32.xor (i32.mul (local.get $s) (i32.const 3))
                                      (i32.shl (i32.add (local.get $b) (local.get $i)) (i32.const 8)))
                             (i32.add (i32.add (local.get $b) (local.get $i)) (i32.const 1))))
      (local.set $i (local.get $q))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (i32.add (local.get $s) (local.get $i)))
""", addrtwo, ADDR_RUNS, (0, 0, 0))
case('addrself', 'the counter added to itself: it stays',
     ADDR % dict(name='addrself', more='',
                 addr='(i32.add (i32.add (local.get $i) (local.get $i)) (i32.const 1))'),
     addr_model(1, self_=True), ADDR_RUNS, (0, 0, 0))


case('addrxor', 'an xor with the step, not an addition of it: it stays',
     ADDR % dict(name='addrxor', more='',
                 addr='(i32.xor (i32.add (local.get $b) (local.get $i)) (i32.const 1))'),
     lambda b, n: addr_gen(lambda b, i: (b + i) ^ 1)(b, n), ADDR_RUNS, (0, 0, 0))
case('addrsubr', 'the value less the counter, not their sum: it stays',
     ADDR % dict(name='addrsubr', more='',
                 addr='(i32.add (i32.sub (local.get $b) (local.get $i)) (i32.const 1))'),
     lambda b, n: addr_gen(lambda b, i: b - i + 1)(b, n), ADDR_RUNS, (0, 0, 0))


def addr_gen(term):
    def f(b, n):
        i, s = 0, 0
        while True:
            s = ((s * 3) ^ (term(b, i) & M32)) & M32
            i += 1
            if not i < n:
                break
        return (s + i) & M32
    return f


def addrgone(n, x):
    i, j = 38, 600
    while True:
        o = j
        j -= 1
        i -= 1
        if MEM[i] <= n or i == 35:
            break
    return (o + x - 1 + i * 100000) & M32


case('addrgone', 'an address past the loop of a counter taken out: made from the one that stays', """
  (func $addrgone (export "addrgone") (param $n i32) (param $x i32) (result i32)
    (local $i i32) (local $j i32) (local $o i32) (local $r i32)
    (local.set $i (i32.const 38))
    (local.set $j (i32.const 600))
    (local.set $r
      (block $out (result i32)
        (loop $l
          (local.set $o (local.get $j))
          (local.set $j (i32.sub (local.get $j) (i32.const 1)))
          (local.set $i (i32.sub (local.get $i) (i32.const 1)))
          (drop (br_if $out (local.get $o) (i32.le_u (i32.load8_u (local.get $i)) (local.get $n))))
          (i32.store8 (i32.const 221) (local.get $i))
          (drop (br_if $out (local.get $o) (i32.eq (local.get $i) (i32.const 35))))
          (br $l))
        (unreachable)))
    (i32.add (i32.add (i32.add (local.get $r) (local.get $x)) (i32.const -1))
             (i32.mul (local.get $i) (i32.const 100000))))
""", addrgone, [(0, 5), (2, 100), (4, 7), (9, 0)], (1, 0, 0))


def addrbrif(b):
    i, s = 0, 0
    while True:
        r = (b + i) & M32
        s = ((s * 3) ^ ((r + 1) & M32)) & M32
        i += 1
        if not r:
            break
    return (s + i) & M32


case('addrbrif', 'the loop branches on the sum: it stays', """
  (func $addrbrif (export "addrbrif") (param $b i32) (result i32)
    (local $i i32) (local $q i32) (local $s i32) (local $r i32)
    (loop $l
      (local.set $q (i32.add (local.get $i) (i32.const 1)))
      (local.set $r (i32.add (local.get $b) (local.get $i)))
      (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 3)) (i32.add (local.get $r) (i32.const 1))))
      (local.set $i (local.get $q))
      (br_if $l (local.get $r)))
    (i32.add (local.get $s) (local.get $i)))
""", addrbrif, [(0,), (0xffffffff,), (0xfffffffb,), (0xfffffff7,)], (0, 0, 0))


def addrnest(n):
    i, t = 0, 0
    while True:
        for j in range(7):
            t = ((t * 3) ^ (i + j + 1)) & M32
        i += 1
        if not i < n:
            break
    return (t + i) & M32


case('addrnest', 'an address in an inner loop of the outer counter and the inner one: the inner loop, '
     'whose counter it now reads, waits a run and keeps the counter', """
  (func $addrnest (export "addrnest") (param $n i32) (result i32)
    (local $i i32) (local $q i32) (local $j i32) (local $k i32) (local $t i32)
    (loop $o
      (local.set $q (i32.add (local.get $i) (i32.const 1)))
      (local.set $j (i32.const 0))
      (local.set $k (i32.const 48))
      (loop $l
        (local.set $t (i32.xor (i32.mul (local.get $t) (i32.const 3))
                               (i32.add (i32.add (local.get $i) (local.get $j)) (i32.const 1))))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br_if $l (i32.load8_u (local.get $k))))
      (local.set $i (local.get $q))
      (br_if $o (i32.lt_u (local.get $i) (local.get $n))))
    (i32.add (local.get $t) (local.get $i)))
""", addrnest, [(0,), (1,), (3,), (6,)], (0, 0, 1))


def addrearly(b, n):
    i, s = 0, 0
    while True:
        x = (b + i + 1) & M32
        s = ((s * 3) ^ x) & M32
        i += 1
        if not i < n:
            break
    return (s + i) & M32


case('addrearly', 'the address is made above the step: it stays', '''
  (func $addrearly (export "addrearly") (param $b i32) (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (loop $l
      (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 3))
                             (i32.add (i32.add (local.get $b) (local.get $i)) (i32.const 1))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (i32.add (local.get $s) (local.get $i)))
''', addrearly, ADDR_RUNS, (0, 0, 0))


def addresc(b, n):
    i, s, r = 0, 0, 0
    while True:
        q = i + 1
        r = (b + i) & M32
        s = ((s * 3) ^ (r + 1)) & M32
        i = q
        if not i < n:
            break
    return (s + i + r * 1000) & M32


case('addresc', 'the sum leaves the loop too: it stays', '''
  (func $addresc (export "addresc") (param $b i32) (param $n i32) (result i32)
    (local $i i32) (local $q i32) (local $s i32) (local $r i32)
    (loop $l
      (local.set $q (i32.add (local.get $i) (i32.const 1)))
      (local.set $r (i32.add (local.get $b) (local.get $i)))
      (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 3)) (i32.add (local.get $r) (i32.const 1))))
      (local.set $i (local.get $q))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (i32.add (i32.add (local.get $s) (local.get $i)) (i32.mul (local.get $r) (i32.const 1000))))
''', addresc, ADDR_RUNS, (0, 0, 0))


HEAD = """;; A loop's counters that step together (opt.c pass_iv_lockstep): parameters
;; of its header that every way round hands their own value plus one
;; constant.  One the loop reads nothing of is taken out where it starts a
;; constant from one that stays on every way in -- two constants, one value,
;; each of those plus constants, the parameters of a block whose ways in
;; agree -- and what read it after the loop reads the one that stays plus
;; that constant: an edge leaving the loop in a block of the edge's own, a
;; reader past the loop, in no other loop, where it reads.  A counter that
;; stays, read past its step by nothing but the ways out below the step and
;; what comes after the loop below it, is read there as the stepped value
;; less the step; and (x + i) + 1 made after the step i + 1 is x + (i + 1).
;; From test/gen_lockstep.py, which works out every answer and says, with
;; --each, what --stats counts for each function alone.  Beside each
;; function below, what is done in it: out, a counter taken out; exit, a
;; reader handed the stepped value; addr, an address remade.
;;
"""


NODCE = ['same', 'oldafter', 'threeout', 'outret', 'outbrif', 'late', 'addr']
HEAD_NODCE = """;; test/cases_opt/lockstep.wat's counters with no pass after to take away
;; what pass_iv_lockstep leaves unread (-fno-dce): the step of a counter
;; taken out is gone with its parameter, and reads nothing that is no more.
;; From test/gen_lockstep.py.
;;
;; flags: -O2 -fno-dce
;;
"""


def render(names=None, head=None):
    funcs, shapes, runs, lines = [], [], [], []
    tot = [0, 0, 0]
    for name, what, wat, model, rs, want, shp, rtype in CASES:
        if names and name not in names:
            continue
        funcs.append(wat.strip('\n') + '\n')
        tot = [a + b for a, b in zip(tot, want)]
        tag = ' '.join(t for t, w in zip(('out', 'exit', 'addr'), want) if w) or '-'
        lines.append(';;   %-12s %-9s %s' % (name, tag, what))
        for sh in shp:
            shapes.append(';; %s: %s %s=%d' % (sh[2] if len(sh) > 2 else 'shape', name, sh[0], sh[1]))
        pairs = [(a, model(*a)) for a in rs]
        for a, r in pairs:
            r = s64(r) if rtype == 'i64' else s32(r)
            runs.append(';; run: %s %s => %s:%d' % (name, ' '.join(str(x) for x in a), rtype, r))
    data = ''.join('\\%02x' % b for b in MEM)
    wrapped = []
    for l in lines:
        while len(l) > 78:
            cut = l.rfind(' ', 0, 78)
            wrapped.append(l[:cut])
            l = ';;' + ' ' * 25 + l[cut + 1:]
        wrapped.append(l)
    if head is None:
        head = HEAD + '\n'.join(wrapped) + '\n;;\n'
    else:
        shapes = []
    head += ';; stats -O2: lockstep=%d lockstep_exits=%d lockstep_addrs=%d\n' % tuple(tot)
    return (head + ('\n'.join(shapes) + '\n' if shapes else '') + ';;\n' + '\n'.join(runs) +
            '\n\n(module\n  (type $t1 (func (param i32)))\n  (memory 1)\n  (table 1 funcref)\n'
            '  (elem (i32.const 0) $thrower)\n  (tag $e)\n  (data (i32.const 0) "%s")\n' % data +
            '  ;; throws at 3; called through the table at an index read from memory, which keeps it out of line\n'
            '  (func $thrower (type $t1) (if (i32.eq (local.get 0) (i32.const 3)) (then (throw $e))))\n' +
            ''.join(funcs) + ')\n')


def each(names):
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    sys.path.insert(0, os.path.join(root, 'test'))
    import gen_gosp
    target = gen_gosp.target_flags(os.path.join(root, 'build'))
    bad = 0
    for name, what, wat, model, rs, want, shp, rtype in CASES:
        if names and name not in names:
            continue
        with tempfile.TemporaryDirectory() as d:
            w = os.path.join(d, 'one.wat')
            open(w, 'w').write(render([name]))
            r = subprocess.run([os.path.join(root, 'build', 'warmc'), '-O2'] + target +
                               ['--stats', '-o', os.path.join(d, 'one'), w], capture_output=True, text=True)
            t = r.stdout + r.stderr
            got = tuple(sum(int(x) for x in re.findall(r'\b%s=(\d+)' % k, t))
                        for k in ('lockstep', 'lockstep_exits', 'lockstep_addrs'))
        mark = '' if got == tuple(want) else '   <-- expected %s' % (want,)
        if r.returncode:
            mark = '   <-- does not compile: ' + (r.stderr.strip().split('\n') or [''])[-1]
        bad += bool(mark)
        print('%-12s %s%s' % (name, got, mark))
    return bad


if __name__ == '__main__':
    if sys.argv[1] == '--each':
        sys.exit(1 if each(sys.argv[2:]) else 0)
    open(sys.argv[1], 'w').write(render())
    open(sys.argv[1].replace('.wat', '_nodce.wat'), 'w').write(render(NODCE, HEAD_NODCE))
    print('%s: %d functions, and %d in its _nodce.wat' % (sys.argv[1], len(CASES), len(NODCE)))
