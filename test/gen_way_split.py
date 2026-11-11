"""test/cases_opt/way_split.wat: the hot way split where the ways into it fix
a value (opt.c pass_way_split), and the near misses it leaves be.  The
module's header says what each function is; every answer is worked out
here, by the functions as Python has them.

  python3 test/gen_way_split.py test/cases_opt/way_split.wat
"""
import sys

# ---- the functions' answers
def u32(x): return x & 0xffffffff
def s32(x):
    x &= 0xffffffff
    return x - (1 << 32) if x >> 31 else x
BUF = [10, 20, 30, 40]
TAB = [5, 6, 7, 8]   # first fields of the objects in $tab
ISA = [1, 0, 1, 0]   # which of them are $a
def arm(buf, key, i):
    if key == 1: return buf[i & 3] + 1000
    if key == 2: return buf[(i ^ 1) & 3] + 2000
    return buf[(i ^ 2) & 3]
def mid(buf, x, i):
    y = u32(x * 3 + i)
    if y & 1: buf[2] = y
    else: buf[3] = u32(y + 1)
    return y
def tail(buf, key_truth, y):
    if key_truth: buf[0] = y; r = y + 5
    else: buf[1] = y; r = y + 7
    return u32(r + buf[0] + buf[2] + buf[3])
def kind3(k, i):
    buf = BUF[:]; key = k & 3
    x = arm(buf, key, i); y = mid(buf, x, i)
    return tail(buf, key != 0, y)
def two(k, i):
    buf = BUF[:]; key = k & 3
    x = buf[i & 3] + 1000 if key == 1 else buf[(i ^ 2) & 3]
    y = mid(buf, x, i)
    return tail(buf, key == 1, y)
def refval(k, i):
    buf = BUF[:]; key = k & 3
    x = arm(buf, key, i)
    o = TAB[i & 3]
    y = mid(buf, u32(x + o), i)
    r = tail(buf, key != 0, y)
    return u32(r + ISA[i & 3] * 100000)
def rare(k, i):
    return kind3(k, i)       # the same code, behind a type test that fails
def plain(k, i):
    return kind3(k, i)       # the same code in a function with no rule's test
def once(k, i):
    buf = BUF[:]; key = k & 3
    x = buf[i & 3] + 1000 if key == 1 else buf[(i ^ 2) & 3]
    y = mid(buf, x, i)
    return tail(buf, (i & 1) != 0, y)
def slow(c, k): return u32(c + 3 * k)
def called(k, i):
    buf = BUF[:]; key = k & 3
    x = arm(buf, key, i)
    y = mid(buf, u32(x + slow(i, 2)), i)
    return tail(buf, key != 0, y)
def looped(k, n):
    buf = BUF[:]; key = k & 3
    x = arm(buf, key, n); s = 0
    for c in range(n):
        s = u32(s + x)
        if key: buf[0] = u32(buf[0] + 1)
        else: buf[1] = u32(buf[1] + 2)
    return u32(s + buf[0] + buf[1])
def inl(k, i): return u32(kind3(k, i) + 1)
def hinted(k, i): return kind3(k, i)     # kind3 with its first test's way a branch hint calls rare
def rarearm(k, i):
    # the second test in the arm of a type test that fails (the object is no $a)
    buf = BUF[:]; key = k & 3
    x = arm(buf, key, i); y = mid(buf, x, i)
    return tail(buf, key != 0, y)
def fromrare(k, i):
    # behind a type test that fails, the value 1 goes straight on to the code after
    buf = BUF[:]; key = k & 3
    x = 5 if key == 1 else 9
    y = mid(buf, x, i)
    return tail(buf, key != 0, y)
def nullkey(k, i):
    # the two ways are those of a null test: $tabn's entries 1 and 3 are null
    buf = BUF[:]; null = (k & 3) in (1, 3)
    x = buf[i & 3] + 1000 if null else buf[(i ^ 2) & 3]
    y = mid(buf, x, i)
    return tail(buf, null, y)
runs = []
for fn in (kind3, refval, rare, plain, called, inl, hinted, rarearm, fromrare, nullkey):
    for k in (0, 1, 2, 3):
        for i in (1, 2):
            runs.append((fn.__name__, k, i, fn(k, i)))
for fn in (two, once):
    for k in (0, 1):
        for i in (1, 2):
            runs.append((fn.__name__, k, i, fn(k, i)))
for k in (0, 1, 2, 3):
    runs.append(('looped', k, 3, looped(k, 3)))
runs = ''.join(';; run: %s %d %d => i32:%d\n' % (n, k, i, s32(r)) for n, k, i, r in runs)

# ---- the module
RULE = '''    (if (i32.eqz (ref.test (ref $a) (global.get $obj)))
      (then (local.set $r (call $slow (i32.const 1) (i32.const 2)))))
'''
def at(ix): return '(array.get $ints (global.get $buf) %s)' % ix
I3 = '(i32.and (local.get $i) (i32.const 3))'
def ix(k): return '(i32.and (i32.xor (local.get $i) (i32.const %d)) (i32.const 3))' % k
KEY = '    (local.set $key (i32.and (local.get $k) (i32.const 3)))\n'
ARM3 = '''    (if (i32.eq (local.get $key) (i32.const 1))
      (then (local.set $x (i32.add %s (i32.const 1000))))
      (else
        (if (i32.eq (local.get $key) (i32.const 2))
          (then (local.set $x (i32.add %s (i32.const 2000))))
          (else (local.set $x %s)))))
''' % (at(I3), at(ix(1)), at(ix(2)))
ARM2 = '''    (if (i32.eq (local.get $key) (i32.const 1))
      (then (local.set $x (i32.add %s (i32.const 1000))))
      (else (local.set $x %s)))
''' % (at(I3), at(ix(2)))
def mid(x='(local.get $x)'):
    return '''    (local.set $y (i32.add (i32.mul %s (i32.const 3)) (local.get $i)))
    (if (i32.and (local.get $y) (i32.const 1))
      (then (array.set $ints (global.get $buf) (i32.const 2) (local.get $y)))
      (else (array.set $ints (global.get $buf) (i32.const 3) (i32.add (local.get $y) (i32.const 1)))))
''' % x
def tail(cond):
    return '''    (if %s
      (then (array.set $ints (global.get $buf) (i32.const 0) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 5))))
      (else (array.set $ints (global.get $buf) (i32.const 1) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 7)))))
    (local.set $r
      (i32.add (i32.add (i32.add (local.get $r) %s) %s) %s))
''' % (cond, at('(i32.const 0)'), at('(i32.const 2)'), at('(i32.const 3)'))
LOC = '(local $key i32) (local $x i32) (local $y i32) (local $r i32)'
def fn(name, body, export=True, extra=''):
    ex = ' (export "%s")' % name if export else ''
    return '  (func $%s%s (param $k i32) (param $i i32) (result i32)\n    %s%s\n%s    (local.get $r))\n' % (name, ex, LOC, extra, body)
KIND3 = RULE + KEY + ARM3 + mid() + tail('(local.get $key)')
out = []
out.append(fn('kind3', KIND3))
out.append(fn('two', RULE + KEY + ARM2 + mid() + tail('(i32.eq (local.get $key) (i32.const 1))')))
out.append(fn('refval', RULE + KEY + ARM3 +
    '    (local.set $o (array.get $objs (global.get $tab) %s))\n' % I3 +
    mid('(i32.add (local.get $x) (struct.get $base 0 (local.get $o)))') + tail('(local.get $key)') +
    '    (local.set $r (i32.add (local.get $r) (i32.mul (ref.test (ref $a) (local.get $o)) (i32.const 100000))))\n',
    extra=' (local $o (ref null $base))'))
out.append(fn('rare', '    (if (i32.eqz (ref.test (ref $a) (global.get $objb)))\n      (then\n' +
    KEY + ARM3 + mid() + tail('(local.get $key)') + '      ))\n'))
out.append(fn('plain', KEY + ARM3 + mid() + tail('(local.get $key)')))
out.append(fn('once', RULE + KEY + ARM2 + mid() + tail('(i32.and (local.get $i) (i32.const 1))')))
out.append(fn('called', RULE + KEY + ARM3 +
    mid('(i32.add (local.get $x) (call $slow (local.get $i) (i32.const 2)))') + tail('(local.get $key)')))
out.append('''  (func $looped (export "looped") (param $k i32) (param $i i32) (result i32)
    (local $key i32) (local $x i32) (local $s i32) (local $c i32) (local $r i32)
''' + RULE + KEY + ARM3 + '''    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $c) (local.get $i)))
        (local.set $s (i32.add (local.get $s) (local.get $x)))
        (if (local.get $key)
          (then (array.set $ints (global.get $buf) (i32.const 0)
                  (i32.add %s (i32.const 1))))
          (else (array.set $ints (global.get $buf) (i32.const 1)
                  (i32.add %s (i32.const 2)))))
        (local.set $c (i32.add (local.get $c) (i32.const 1)))
        (br $l)))
    (i32.add (i32.add (local.get $s) %s) %s))
''' % (at('(i32.const 0)'), at('(i32.const 1)'), at('(i32.const 0)'), at('(i32.const 1)')))
NULLQ = '(ref.is_null (local.get $o))'
out.append(fn('nullkey', RULE +
    '    (local.set $o (array.get $objs (global.get $tabn) (i32.and (local.get $k) (i32.const 3))))\n' +
    '    (if %s\n      (then (local.set $x (i32.add %s (i32.const 1000))))\n      (else (local.set $x %s)))\n'
    % (NULLQ, at(I3), at(ix(2))) + mid() + tail(NULLQ), extra=' (local $o (ref null $base))'))
ARM3H = ARM3.replace('    (if (i32.eq (local.get $key) (i32.const 1))',
                     '    (@metadata.code.branch_hint "\\00") (if (i32.eq (local.get $key) (i32.const 1))', 1)
out.append(fn('hinted', RULE + KEY + ARM3H + mid() + tail('(local.get $key)')))
# the second test of the value only in the arm of a type test's rare way
out.append(fn('rarearm', RULE + KEY + ARM3 + mid() +
    '    (local.set $r (i32.const 0))\n    (if (i32.eqz (ref.test (ref $a) (global.get $objb)))\n      (then\n' +
    tail('(local.get $key)') + '      ))\n'))
# a rare way whose own test of the value sends 1 straight on to the hot code
FROM = ('    (local.set $x (i32.const 9))\n'
        '    (block $on\n'
        '      (br_if $on (ref.test (ref $a) (global.get $objb)))\n'
        '      (local.set $x (i32.const 5))\n'
        '      (br_if $on (i32.eq (local.get $key) (i32.const 1)))\n'
        '      (local.set $x (i32.const 9)))\n')
out.append(fn('fromrare', KEY + FROM + mid() + tail('(local.get $key)')))
out.append(fn('helper', KIND3, export=False))
out.append('''  (func $inl (export "inl") (param $k i32) (param $i i32) (result i32)
    (i32.add (call $helper (local.get $k) (local.get $i)) (i32.const 1)))
''')
HEAD = ''';; The hot way split where its ways in fix a value (opt.c pass_way_split):
;; a value tested three ways -- 1, 2, anything else -- whose ways meet, some
;; code, and a test of the value again.  The ways that fix it get a copy of
;; the code up to that test, which they go past, and the way of the rest is
;; the one way into the original.  Each function reads an array at an index
;; its way picks (x), makes y = 3x + i, stores y or y + 1 by y's low bit --
;; a branch on something else, three blocks between the merge and the test,
;; more than jump threading copies -- and tests the value again to store y
;; at 0 or at 1.
;;
;;   kind3    the value's two fixed ways each go past the test of it, a
;;            branch on the value itself: two copies
;;   two      a value two branches test, each a compare with 1: one copy
;;   refval   the code between reads a reference a type test after the
;;            second test reads again: the copies make it again, and what
;;            reaches the read is merged -- two copies
;;   rare     kind3's code behind a type test's rare way (which it takes:
;;            the object is no $a): off the hot way, no copy
;;   plain    kind3's code in a function no test of which has a rare way by
;;            a rule: no copy
;;   once     a value one branch tests, the second test of another: no copy
;;   called   a call in the block the ways meet in, which no copy is made
;;            of: no copy
;;   looped   the second test in a loop the ways meet ahead of: no copy
;;   inl      kind3's code in a function inlined into a few instructions:
;;            two copies
;;   hinted   kind3 with its test for 1 a way a branch hint calls rare: that
;;            way is off the hot way, and only the way for 2 has a copy
;;   rarearm  the second test of the value in the arm of a type test's rare
;;            way, which the hot way's walk does not enter: no copy
;;   fromrare a rare way's own test of the value sends 1 straight on to the
;;            hot code that tests it again: a way from off the hot way, no
;;            copy (seen without the tails, which give it code of its own)
;;   nullkey  the two ways are those of a null test, and the test is made
;;            again after they meet: whether a reference is null is no value
;;            to split on -- a tree's missing child is as likely as not, and
;;            javabench/treemap ran 8% longer on such copies -- no copy
;;
;; (stats: kind3 2, two 1, refval 2, hinted 1, inl 2, and 2 in the function
;; inl calls, compiled for itself too.)  Made by a model of the functions
;; that works the answers out.
;;
;; stats -O2: way_splits=10
;; stats -O2 -fno-tails: way_splits=10
;;
'''
MOD = '''(module
  (type $base (sub (struct (field i32))))
  (type $a (sub $base (struct (field i32) (field i32))))
  (type $ints (array (mut i32)))
  (type $objs (array (mut (ref null $base))))
  (global $obj (mut (ref null $base)) (struct.new $a (i32.const 6) (i32.const 60)))
  (global $objb (mut (ref null $base)) (struct.new $base (i32.const 9)))
  (global $buf (mut (ref $ints))
    (array.new_fixed $ints 4 (i32.const 10) (i32.const 20) (i32.const 30) (i32.const 40)))
  (global $tab (mut (ref $objs))
    (array.new_fixed $objs 4 (struct.new $a (i32.const 5) (i32.const 50)) (struct.new $base (i32.const 6))
      (struct.new $a (i32.const 7) (i32.const 70)) (struct.new $base (i32.const 8))))
  (global $tabn (mut (ref $objs))
    (array.new_fixed $objs 4 (struct.new $base (i32.const 1)) (ref.null $base)
      (struct.new $base (i32.const 3)) (ref.null $base)))
  ;; c + 3k, recursively so that it is not inlined
  (func $slow (param $c i32) (param $k i32) (result i32)
    (if (result i32) (i32.eqz (local.get $k))
      (then (local.get $c))
      (else (call $slow (i32.add (local.get $c) (i32.const 3)) (i32.sub (local.get $k) (i32.const 1))))))
'''
open(sys.argv[1], 'w').write(HEAD + runs + MOD + ''.join(out) + ')\n')
