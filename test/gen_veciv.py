"""test/cases_opt/vec_iv.wat: loops whose lanes hold their counter (opt.c
vectorize_one's vector of a counter's values), with memory or none.
Answers are worked out here on a model of each loop in Python: wasm's
wrapping integers and IEEE doubles.

  python3 test/gen_veciv.py test/cases_opt/vec_iv.wat
"""
import functools, struct, sys

M32 = (1 << 32) - 1
M64 = (1 << 64) - 1


def s32(v): v &= M32; return v - (1 << 32) if v >> 31 else v
def s64(v): v &= M64; return v - (1 << 64) if v >> 63 else v


def h32(words):
    """the readback: words hashed, as the module's $h does"""
    h = 0
    for w in words:
        h = (h * 31 + (w & M32)) & M32
    return h


CASES = []


def case(name, sig, locals_, body, model, runs, shapes):
    CASES.append((name, sig, locals_, body, model, runs, shapes))


NS = [0, 1, 3, 4, 5, 15, 16, 17, 31, 63, 64, 65, 1000]

# -- a vector of the counter ---------------------------------------------------

# gobench/bigstruct's loop: (i & 3) + 14 summed, no memory, in four copies
# and the loop of one after them; a second counter t counts along and is
# read only after the loop, so its vector goes
case('rsum', '(param $n i32) (result i32)', '(local $i i32) (local $s i32) (local $t i32)', '''
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (i32.add (local.get $s) (i32.add (i32.and (local.get $i) (i32.const 3)) (i32.const 14))))
        (local.set $t (i32.add (local.get $t) (i32.const 1)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (i32.mul (local.get $t) (i32.const 1000)))''',
     lambda n: sum((i & 3) + 14 for i in range(n)) + 1000 * n,
     [(n,) for n in NS], [(r'\sand\.16b\s', 5), (r'\sadd\.4s\s', 20)])

# a counter stepping by 3 from 7, its product xor'd into the sum
case('rxor3', '(param $n i32) (result i32)', '(local $i i32) (local $s i32)', '''
    (local.set $i (i32.const 7))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (i32.xor (local.get $s) (i32.mul (local.get $i) (i32.const 5))))
        (local.set $i (i32.add (local.get $i) (i32.const 3)))
        (br $l)))
    (local.get $s)''',
     lambda n: functools.reduce(lambda s, i: s ^ (i * 5 & M32), range(7, n, 3), 0),
     [(n,) for n in NS + [3007]], [(r'\smul\.4s\s', 5)])

# a counter running down to 0
case('rdown', '(param $n i32) (result i32)', '(local $s i32)', '''
    (block $done
      (loop $l
        (br_if $done (i32.eqz (local.get $n)))
        (local.set $s (i32.add (local.get $s) (i32.or (local.get $n) (i32.const 1))))
        (local.set $n (i32.sub (local.get $n) (i32.const 1)))
        (br $l)))
    (local.get $s)''',
     lambda n: sum(i | 1 for i in range(n, 0, -1)) & M32,
     [(n,) for n in NS], [(r'\sorr\.16b\s', 5)])

# 64-bit lanes, two to a vector
case('rsum64', '(param $n i64) (result i64)', '(local $i i64) (local $s i64)', '''
    (block $done
      (loop $l
        (br_if $done (i64.ge_u (local.get $i) (local.get $n)))
        (local.set $s (i64.add (local.get $s) (i64.xor (local.get $i) (i64.const 0x55))))
        (local.set $i (i64.add (local.get $i) (i64.const 1)))
        (br $l)))
    (local.get $s)''',
     lambda n: sum(i ^ 0x55 for i in range(n)) & M64,
     [(n,) for n in NS], [(r'\seor\.16b\s', 5)])

# two counters in one term, one stepping by 2 from 100
case('rtwo', '(param $n i32) (result i32)', '(local $i i32) (local $j i32) (local $s i32)', '''
    (local.set $j (i32.const 100))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (i32.add (local.get $s) (i32.xor (local.get $i) (local.get $j))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 2)))
        (br $l)))
    (local.get $s)''',
     lambda n: sum(i ^ (100 + 2 * i) for i in range(n)) & M32,
     [(n,) for n in NS], [(r'\seor\.16b\s', 5)])

# a store of the counter's value: 3i + 1 at a[i]
case('fill', '(param $n i32) (result i32)', '(local $i i32)', '''
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (i32.store offset=1024 (i32.shl (local.get $i) (i32.const 2))
                   (i32.add (i32.mul (local.get $i) (i32.const 3)) (i32.const 1)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $h (i32.const 1024) (local.get $n))''',
     lambda n: h32([3 * i + 1 for i in range(n)]),
     [(n,) for n in NS], [(r'\smla\.4s\s', 5)])

# -- the counter not in vectors ------------------------------------------------

# backward: a[i] = i from the top down, the lanes against the iterations
case('fillback', '(param $n i32) (result i32)', '(local $i i32)', '''
    (local.set $i (local.get $n))
    (block $done
      (loop $l
        (br_if $done (i32.eqz (local.get $i)))
        (local.set $i (i32.sub (local.get $i) (i32.const 1)))
        (i32.store offset=1024 (i32.shl (local.get $i) (i32.const 2)) (local.get $i))
        (br $l)))
    (call $h (i32.const 1024) (local.get $n))''',
     lambda n: h32(list(range(n))),
     [(n,) for n in NS], [(r'\\s[a-z0-9]+\\.(16b|8h|4s|2d)\\s', 0)])

# two elements an iteration: a[2i] and a[2i+1] both i, not i and i + 1
case('fillpairs', '(param $n i32) (result i32)', '(local $i i32)', '''
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (i32.store offset=1024 (i32.shl (local.get $i) (i32.const 3)) (local.get $i))
        (i32.store offset=1028 (i32.shl (local.get $i) (i32.const 3)) (local.get $i))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $h (i32.const 1024) (i32.shl (local.get $n) (i32.const 1)))''',
     lambda n: h32([i for i in range(n) for _ in (0, 1)]),
     [(n,) for n in NS], [(r'\\s[a-z0-9]+\\.(16b|8h|4s|2d)\\s', 0)])

# bytes of a 32-bit counter: lanes narrower than it
case('fillbytes', '(param $n i32) (result i32)', '(local $i i32)', '''
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (i32.store8 offset=1024 (local.get $i) (i32.add (local.get $i) (i32.const 7)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $h (i32.const 1024) (i32.shr_u (local.get $n) (i32.const 2)))''',
     lambda n: h32([int.from_bytes(bytes((i + 7) & 255 for i in range(4 * w, 4 * w + 4)), 'little')
                    for w in range(n >> 2)]),
     [(n,) for n in NS], [(r'\\s[a-z0-9]+\\.(16b|8h|4s|2d)\\s', 0)])

# a float sum, which is not reassociable
case('fsum', '(param $n i32) (result f64)', '(local $i i32) (local $s f64)', '''
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (f64.add (local.get $s) (f64.div (f64.convert_i32_u (local.get $i)) (f64.const 3))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s)''',
     None, [(n,) for n in NS], [(r'\\s[a-z0-9]+\\.(16b|8h|4s|2d)\\s', 0)])


def fsum_model(n):
    s = 0.0
    for i in range(n):
        s += i / 3.0
    return s


def main():
    runs, shapes, funcs = [], [], []
    for name, sig, locals_, body, model, rs, sh in CASES:
        res = sig.split('(result ')[1].rstrip(')')
        for args in rs:
            v = (model or fsum_model)(*args)
            if res == 'f64':
                out = 'f64:0x%016x' % struct.unpack('<Q', struct.pack('<d', v))[0]
            elif res == 'i64':
                out = 'i64:%d' % s64(v)
            else:
                out = 'i32:%d' % s32(v)
            runs.append(';; run: %s %s => %s' % (name, ' '.join(str(a) for a in args), out))
        for pat, n in sh:
            shapes.append(';; shape: %s %s=%d' % (name, pat, n))
        funcs.append('  (func $%s (export "%s") %s\n    %s%s)' % (name, name, sig, locals_, body))
    head = '''\
;; Loops whose lanes hold their counter (opt.c vectorize_one): a vector of
;; the counter's values in the iterations the lanes stand for, made on entry
;; a lane a step apart and stepped a copy at a time.  A loop that touches no
;; memory vectorises for what it carries round, one element an iteration in
;; its lanes: gobench/bigstruct's sum of (i & 3) + 14, a counter that counts
;; along unread in the loop, whose vector goes; a product xor'd in, of a
;; counter stepping by 3 from 7; a counter running down; 64-bit lanes; two
;; counters in one term; and with memory, a store of 3i + 1 at a[i].  Not a
;; counter where the lanes run against the iterations (a store loop running
;; down), two elements an iteration, lanes narrower than the counter, nor a
;; float sum, which is not reassociable.  Each at trip counts around the
;; vector's and its copies'.  Made by test/gen_veciv.py.
;;
'''
    out = head + '\n'.join(shapes) + ('\n;;\n' if shapes else '') + '\n'.join(runs) + '\n\n(module\n'
    out += '''\
  (memory 1)
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
'''
    out += '\n'.join(funcs) + '\n)\n'
    open(sys.argv[1], 'w').write(out)


main()
