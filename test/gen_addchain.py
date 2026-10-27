"""test/cases_opt/addchain.wat: the value a loop hands its parameter on a
back edge, the parameter and a chain of constants, is the parameter and one
constant (opt.c carried_chain).  Answers are worked out here by running each
function's loop in Python, in wrapping 32- and 64-bit arithmetic.

  python3 test/gen_addchain.py test/cases_opt/addchain.wat
"""
import sys

M32, M64 = (1 << 32) - 1, (1 << 64) - 1

def s32(v):
    v &= M32
    return v - (1 << 32) if v >> 31 else v

def s64(v):
    v &= M64
    return v - (1 << 64) if v >> 63 else v

def rotl32(x, k): x &= M32; return ((x << k) | (x >> (32 - k))) & M32
def rotl64(x, k): x &= M64; return ((x << k) | (x >> (64 - k))) & M64

def unroll(n, use_i=True):
    i = s = 0
    while True:
        a = (i + 1) & M32; b = (a + 1) & M32; c = (b + 1) & M32
        t = ((a << 3) & M32) ^ (rotl32(b, 7) ^ (c * c & M32))
        if use_i: t ^= i
        s = (s * 31 + t) & M32
        i = (c + 1) & M32
        if not (i < n and s != 1): return s

def notback(n):
    i = s = 0
    while True:
        a = (i + 1) & M32; b = (a + 1) & M32; c = (b + 1) & M32
        s = (s * 31 + (i ^ ((a << 3) & M32) ^ rotl32(b, 7) ^ (c * c & M32))) & M32
        i = a
        if not i < n: return s

def straight(x):
    a = (x + 1) & M32; b = (a + 1) & M32; c = (b + 1) & M32
    return (a * b ^ c ^ x) & M32

def xorchain(x, n):
    s = 0
    while True:
        a = x ^ 0xff
        s = (s * 1000003 + a + rotl64(x, 13)) & M64
        x = a ^ 0xfff
        n = (n - 1) & M32
        if not (n and s != 1): return s

def mulwrap(x, n):
    s = 0
    while True:
        a = x * 0x100000001 & M64
        s = (s + (a ^ x)) & M64
        x = a * 0x100000001 & M64
        n = (n - 1) & M32
        if not n: return s

def addwrap(n):
    i = s = 0
    while True:
        a = (i + 0x7fffffff) & M32; b = (a + 0x7fffffff) & M32
        s = (s * 7 + (a ^ b ^ i)) & M32
        i = (b + 3) & M32
        if not (i < n and s != 1): return s

def chain(n, k):
    i = s = 0
    while True:
        t = i; links = []
        for _ in range(k - 1):
            t = (t + 1) & M32; links.append(t)
        x = i
        for l in links: x ^= l
        s = (s * 31 + x) & M32
        i = (t + 1) & M32
        if not i < n: return s

def twoedge(n):
    i = s = 0
    while True:
        a = (i + 1) & M32; b = (a + 1) & M32; c = (b + 1) & M32
        s = (s * 31 + (i ^ ((a << 3) & M32) ^ rotl32(b, 7) ^ (c * c & M32))) & M32
        if s & 1:
            i = a
            if i < n: continue
            return s
        i = (c + 1) & M32
        if not i < n: return s

def varlink(d, n):
    i = s = 0
    while True:
        a = (i + d) & M32
        s = (s * 31 + (a ^ i)) & M32
        i = (a + 1) & M32
        if not i < n: return s

def notparam(n):
    i = s = 0
    while True:
        m = i * 3 & M32
        a = (m + 1) & M32
        s = (s * 31 + (a ^ i)) & M32
        i = (a + 1) & M32
        if not i < n: return s

def swap(n):
    i, j, s = 0, 5, 0
    while True:
        a = (i + 1) & M32; b = (a + 1) & M32
        s = (s * 31 + (i ^ a ^ ((b << 2) & M32) ^ j)) & M32
        i, j = j, (b + 1) & M32
        if not i < n: return s

LOOP_UNROLL = '''(func $%s (export "%s") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $a i32) (local $b i32) (local $c i32)
    (loop $l
      (local.set $a (i32.add (local.get $i) (i32.const 1)))
      (local.set $b (i32.add (local.get $a) (i32.const 1)))
      (local.set $c (i32.add (local.get $b) (i32.const 1)))
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
        (i32.xor %s(i32.shl (local.get $a) (i32.const 3))
          (i32.xor (i32.rotl (local.get $b) (i32.const 7))
                   (i32.mul (local.get $c) (local.get $c))))))
      (local.set $i (i32.add (local.get $c) (i32.const 1)))
      (br_if $l (i32.and (i32.lt_u (local.get $i) (local.get $n))
                         (i32.ne (local.get $s) (i32.const 1)))))
    (local.get $s))'''

def chain_func(name, k):
    body = ['  (func $%s (export "%s") (param $n i32) (result i32)' % (name, name),
            '    (local $i i32) (local $s i32) (local $t i32) (local $x i32)',
            '    (loop $l',
            '      (local.set $t (local.get $i))',
            '      (local.set $x (local.get $i))']
    for _ in range(k - 1):
        body.append('      (local.set $t (i32.add (local.get $t) (i32.const 1)))')
        body.append('      (local.set $x (i32.xor (local.get $x) (local.get $t)))')
    body += ['      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31)) (local.get $x)))',
             '      (local.set $i (i32.add (local.get $t) (i32.const 1)))',
             '      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))',
             '    (local.get $s))']
    return '\n'.join(body)

ADD = r'\sadd\s+w\d+, w\d+, #0x%s$'
head = r''';; The value a loop hands its parameter x on a back edge, x op c1 op c2 ...
;; with op an add, multiply, and, or or xor and each c a constant, is x op
;; c, the constants folded into one, also where each link is read elsewhere
;; (opt.c carried_chain): the next iteration waits on one instruction, not
;; on each link in turn -- gobench/bigstruct's loop unrolled four
;; times.  The links stay for their other readers.  Not where nothing else
;; reads x, which would then be live to the chain's end; not for a value no
;; back edge hands x, nor one handed another parameter, nor a chain from
;; anything but a parameter, nor through a link that adds a variable, nor
;; one of more than 16 links below the value, nor outside a loop.  Constants
;; wrap: an i32's past 2^32, an i64 product's past 2^64.  The loops whose
;; shapes are counted stop where s is 1, which keeps them from being
;; unrolled: the merged step makes unroll's a counted loop.  Answers worked
;; out in Python.
;;
;; shape: unroll %s=1
;; shape: unroll %s=3
;; shape: baseonly %s=0
;; shape: baseonly %s=4
;; shape: notback %s=0
;; shape: straight %s=0
;; shape: addwrap %s=1
;; shape: xorchain \seor\s+x\d+, x\d+, #0xf00$=1
;; shape: deep %s=1
;; shape: deeper %s=0
;; shape: twoedge %s=1
;; shape: notparam %s=0
;; shape: swap %s=0
;;
''' % (ADD % '4', ADD % '1', ADD % '4', ADD % '1', ADD % '[23]', ADD % '[23]',
       ADD % '1', ADD % '11', ADD % '12', ADD % '4', ADD % '2', ADD % '3')

runs = []
for n in (0, 1, 4, 5, 37, 100):
    runs.append('unroll %d => i32:%d' % (n, s32(unroll(n))))
    runs.append('baseonly %d => i32:%d' % (n, s32(unroll(n, False))))
    runs.append('notback %d => i32:%d' % (n, s32(notback(n))))
    runs.append('addwrap %d => i32:%d' % (n, s32(addwrap(n))))
    runs.append('deep %d => i32:%d' % (n, s32(chain(n, 17))))
    runs.append('deeper %d => i32:%d' % (n, s32(chain(n, 18))))
    runs.append('twoedge %d => i32:%d' % (n, s32(twoedge(n))))
    runs.append('notparam %d => i32:%d' % (n, s32(notparam(n))))
    runs.append('varlink 2 %d => i32:%d' % (n, s32(varlink(2, n))))
    runs.append('swap %d => i32:%d' % (n, s32(swap(n))))
for x in (0, 7, 0xfffffffe, 0x7fffffff):
    runs.append('straight %d => i32:%d' % (s32(x), s32(straight(x))))
for x, n in ((0, 1), (5, 2), (-1, 9), (0x123456789abcdef, 40)):
    runs.append('xorchain %d %d => i64:%d' % (x, n, s64(xorchain(x & M64, n))))
    runs.append('mulwrap %d %d => i64:%d' % (x, n, s64(mulwrap(x & M64, n))))

mod = '''(module
  ''' + (LOOP_UNROLL % ('unroll', 'unroll', '(i32.xor (local.get $i) ') ).replace('(i32.shl (local.get $a) (i32.const 3))\n          (i32.xor', '(i32.shl (local.get $a) (i32.const 3)))\n          (i32.xor') + '''

  ;; the same, but x is read by the chain alone
  ''' + LOOP_UNROLL % ('baseonly', 'baseonly', '') + '''

  ;; the chain's end is no back edge's: the loop hands x x + 1
  (func $notback (export "notback") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $a i32) (local $b i32) (local $c i32)
    (loop $l
      (local.set $a (i32.add (local.get $i) (i32.const 1)))
      (local.set $b (i32.add (local.get $a) (i32.const 1)))
      (local.set $c (i32.add (local.get $b) (i32.const 1)))
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
        (i32.xor (i32.xor (local.get $i) (i32.shl (local.get $a) (i32.const 3)))
          (i32.xor (i32.rotl (local.get $b) (i32.const 7))
                   (i32.mul (local.get $c) (local.get $c))))))
      (local.set $i (local.get $a))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))

  ;; no loop
  (func $straight (export "straight") (param $x i32) (result i32)
    (local $a i32) (local $b i32) (local $c i32)
    (local.set $a (i32.add (local.get $x) (i32.const 1)))
    (local.set $b (i32.add (local.get $a) (i32.const 1)))
    (local.set $c (i32.add (local.get $b) (i32.const 1)))
    (i32.xor (i32.xor (i32.mul (local.get $a) (local.get $b)) (local.get $c)) (local.get $x)))

  ;; x ^ 0xff ^ 0xfff is x ^ 0xf00, not x + 0x10fe
  (func $xorchain (export "xorchain") (param $x i64) (param $n i32) (result i64)
    (local $s i64) (local $a i64)
    (loop $l
      (local.set $a (i64.xor (local.get $x) (i64.const 0xff)))
      (local.set $s (i64.add (i64.mul (local.get $s) (i64.const 1000003))
        (i64.add (local.get $a) (i64.rotl (local.get $x) (i64.const 13)))))
      (local.set $x (i64.xor (local.get $a) (i64.const 0xfff)))
      (local.set $n (i32.sub (local.get $n) (i32.const 1)))
      (br_if $l (i32.and (i32.ne (local.get $n) (i32.const 0))
                         (i64.ne (local.get $s) (i64.const 1)))))
    (local.get $s))

  ;; x * 0x100000001 * 0x100000001, the product past 2^64
  (func $mulwrap (export "mulwrap") (param $x i64) (param $n i32) (result i64)
    (local $s i64) (local $a i64)
    (loop $l
      (local.set $a (i64.mul (local.get $x) (i64.const 0x100000001)))
      (local.set $s (i64.add (local.get $s) (i64.xor (local.get $a) (local.get $x))))
      (local.set $x (i64.mul (local.get $a) (i64.const 0x100000001)))
      (local.set $n (i32.sub (local.get $n) (i32.const 1)))
      (br_if $l (local.get $n)))
    (local.get $s))

  ;; x + 0x7fffffff + 0x7fffffff + 3 is x + 1, the sum past 2^32
  (func $addwrap (export "addwrap") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $a i32) (local $b i32)
    (loop $l
      (local.set $a (i32.add (local.get $i) (i32.const 0x7fffffff)))
      (local.set $b (i32.add (local.get $a) (i32.const 0x7fffffff)))
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 7))
        (i32.xor (i32.xor (local.get $a) (local.get $b)) (local.get $i))))
      (local.set $i (i32.add (local.get $b) (i32.const 3)))
      (br_if $l (i32.and (i32.lt_u (local.get $i) (local.get $n))
                         (i32.ne (local.get $s) (i32.const 1)))))
    (local.get $s))

  ;; 16 links below the value: x + 17
''' + chain_func('deep', 17) + '''

  ;; 17: left alone
''' + chain_func('deeper', 18) + '''

  ;; two back edges, the chain's end on the second
  (func $twoedge (export "twoedge") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $a i32) (local $b i32) (local $c i32)
    (block $done
      (loop $l
        (local.set $a (i32.add (local.get $i) (i32.const 1)))
        (local.set $b (i32.add (local.get $a) (i32.const 1)))
        (local.set $c (i32.add (local.get $b) (i32.const 1)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
          (i32.xor (i32.xor (local.get $i) (i32.shl (local.get $a) (i32.const 3)))
            (i32.xor (i32.rotl (local.get $b) (i32.const 7))
                     (i32.mul (local.get $c) (local.get $c))))))
        (if (i32.and (local.get $s) (i32.const 1))
          (then
            (local.set $i (local.get $a))
            (br_if $l (i32.lt_u (local.get $i) (local.get $n)))
            (br $done)))
        (local.set $i (i32.add (local.get $c) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (local.get $s))

  ;; a link that adds a variable
  (func $varlink (export "varlink") (param $d i32) (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $a i32)
    (loop $l
      (local.set $a (i32.add (local.get $i) (local.get $d)))
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
        (i32.xor (local.get $a) (local.get $i))))
      (local.set $i (i32.add (local.get $a) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))

  ;; the chain starts at x * 3, no parameter
  (func $notparam (export "notparam") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $a i32)
    (loop $l
      (local.set $a (i32.add (i32.mul (local.get $i) (i32.const 3)) (i32.const 1)))
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
        (i32.xor (local.get $a) (local.get $i))))
      (local.set $i (i32.add (local.get $a) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))

  ;; the chain from i handed j, i handed j's value
  (func $swap (export "swap") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32) (local $a i32) (local $b i32) (local $t i32)
    (local.set $j (i32.const 5))
    (loop $l
      (local.set $a (i32.add (local.get $i) (i32.const 1)))
      (local.set $b (i32.add (local.get $a) (i32.const 1)))
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
        (i32.xor (i32.xor (local.get $i) (local.get $a))
          (i32.xor (i32.shl (local.get $b) (i32.const 2)) (local.get $j)))))
      (local.set $t (i32.add (local.get $b) (i32.const 1)))
      (local.set $i (local.get $j))
      (local.set $j (local.get $t))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))
)
'''
out = head + ''.join(';; run: %s\n' % r for r in runs) + '\n' + mod
open(sys.argv[1], 'w').write(out)
