"""test/cases_opt/hot_starts.wat: a callee's likely ways taken into its caller
(inline.c pinl_build's hot start), and the callees that have none.  The
module's header says what each function is; every answer is worked out
here, by the functions as Python has them.

  python3 test/gen_hot_starts.py test/cases_opt/hot_starts.wat
"""
import sys

M = 0xffffffff
def u32(x): return x & M
def s32(x):
    x &= M
    return x - (1 << 32) if x >> 31 else x

class St:
    def __init__(self):
        self.cnt = 0; self.g1 = 0
        self.a = {'leaf': True, 'v': 3, 'next': None, 'w': 30}
        self.b = {'leaf': False, 'v': 5, 'next': None}
def slow(st, c, k):
    st.cnt += 1
    return u32(c + 3 * k)
def step(x, j): 
    x = u32(x * 3 + j)
    return x ^ (x >> 5)
def steps(x, lo, n):
    for j in range(lo, lo + n): x = step(x, j)
    return x
def never(st, x): return None      # the three tests of the globals, which never fail
def c_val(st, o, x):
    if o is None: return slow(st, x, 1)
    x = u32(x + o['v']); x = steps(x, 1, 5)
    if not o['leaf']: return slow(st, x, 2)
    x = steps(x, 6, 9)
    return u32(x + o['w'])
def c_link(st, p, q, x):
    if p is None: return slow(st, x, 1)
    p['next'] = q
    x = steps(x, 1, 4)
    if not q['leaf']: return slow(st, x, 2)
    x = steps(x, 5, 10)
    return u32(x + q['w'])
def c_bump(st, p, q, x):
    if p is None: return slow(st, x, 1)
    p['v'] = u32(p['v'] + 1)
    x = steps(x, 1, 4)
    if not q['leaf']: return slow(st, x, 2)
    x = steps(x, 5, 10)
    return u32(x + p['v'])
def c_twice(st, o, x):
    if not o['leaf']: return slow(st, x, 1)
    st.g1 = u32(st.g1 + 1)
    x = steps(x, 1, 6); x = u32(x + o['v'])
    x = steps(x, 7, 8)
    return u32(x + st.g1)
def c_unb(st, o, x, m):
    if o is None: return slow(st, x, 1)
    x = steps(x, 1, 12)
    if m & 1: return slow(st, x, 2)
    x = steps(x, 13, 2)
    return x
def c_alw(st, o, x):
    if o is None: return slow(st, x, 1)
    x = steps(x, 1, 12)
    return u32(x + slow(st, x, 1))
def c_big(st, o, x):
    if o is None: return slow(st, x, 1)
    x = steps(x, 1, 60)
    return x
def c_join(st, o, x):
    if not o['leaf']: x = slow(st, x, 1)
    st.g1 = u32(st.g1 + 1)
    x = steps(x, 1, 6); x = u32(x + o['v'])
    if not o['leaf']: return slow(st, x, 2)
    x = steps(x, 7, 8)
    return u32(x + st.g1)
def c_arm(st, o, x):
    if o is None:
        return steps(x, 100, 50)
    return steps(x, 1, 12)
def c_tail(st, o, x):
    x = steps(x, 1, 12)
    if o is None: return x
    return u32(x + slow(st, x, 1))
def c_short(st, o, x):
    if o is None: return slow(st, x, 1)
    return steps(x, 1, 2)
def c_cross(st, o, x, m):
    if m & 1:
        x = steps(x, 1, 14)
        if o is None: return x
        return slow(st, x, 1)
    return steps(x, 20, 3)
def c_meet(st, o, x, m):
    if m & 1:
        x = steps(x, 1, 14)
        if o is not None: return slow(st, x, 1)
    else:
        x = steps(x, 20, 2)
    return steps(x, 30, 3)
def pick(st, sel): return st.a if sel == 0 else st.b if sel == 1 else None
def d_val(sel, x):
    st = St(); s = 0
    for i in range(3): s = u32(s + c_val(st, pick(st, sel), u32(x + i)))
    return u32(s + st.cnt * 1000003)
def d_link(sel, x):
    st = St(); r = 0
    q = st.a if sel == 0 else st.b
    for i in range(2): r = u32(r + c_link(st, st.a, q, u32(x + i)))
    return u32(r + (100000 if st.a['next'] is q else 0) + st.cnt * 1000003)
def d_bump(sel, x):
    st = St(); r = 0
    q = st.a if sel == 0 else st.b
    for i in range(2): r = u32(r + c_bump(st, st.a, q, u32(x + i)))
    return u32(r + st.a['v'] * 1000 + st.cnt * 1000003)
def d_twice(sel, x):
    st = St(); r = 0
    for i in range(2): r = u32(r + c_twice(st, pick(st, sel), u32(x + i)))
    return u32(r + st.g1 * 1000 + st.cnt * 1000003)
def d_unb(sel, x, m):
    st = St(); r = 0
    for i in range(2): r = u32(r + c_unb(st, pick(st, sel), u32(x + i), m))
    return u32(r + st.cnt * 1000003)
def d_alw(sel, x):
    st = St(); r = 0
    for i in range(2): r = u32(r + c_alw(st, pick(st, sel), u32(x + i)))
    return u32(r + st.cnt * 1000003)
def d_big(sel, x):
    st = St(); r = 0
    for i in range(2): r = u32(r + c_big(st, pick(st, sel), u32(x + i)))
    return u32(r + st.cnt * 1000003)
def d2(callee):
    def d(sel, x):
        st = St(); r = 0
        for i in range(2): r = u32(r + callee(st, pick(st, sel), u32(x + i)))
        return u32(r + st.g1 * 1000 + st.cnt * 1000003)
    return d
def d3(callee):
    def d(sel, x, m):
        st = St(); r = 0
        for i in range(2): r = u32(r + callee(st, pick(st, sel), u32(x + i), m))
        return u32(r + st.cnt * 1000003)
    return d
d_cross = d3(c_cross); d_meet = d3(c_meet)
d_join = d2(c_join); d_arm = d2(c_arm); d_tail = d2(c_tail); d_short = d2(c_short)
def d_many(sel, x):
    st = St(); s = 0
    for i in range(NMANY): s = u32(s + c_val(st, pick(st, sel), u32(x + i)))
    return u32(s + st.cnt * 1000003)
NMANY = 90
runs = []
for name, fn, args in (('val', d_val, [(0, 7), (1, 7), (2, 7)]), ('link', d_link, [(0, 7), (1, 7)]),
                       ('bump', d_bump, [(0, 7), (1, 7)]), ('twice', d_twice, [(0, 7), (1, 7)]),
                       ('unb', d_unb, [(0, 7, 0), (0, 7, 1), (2, 7, 0)]), ('alw', d_alw, [(0, 7), (2, 7)]),
                       ('big', d_big, [(0, 7), (2, 7)]), ('many', d_many, [(0, 7), (1, 7)]),
                       ('join', d_join, [(0, 7), (1, 7)]), ('arm', d_arm, [(0, 7), (2, 7)]),
                       ('tail', d_tail, [(0, 7), (2, 7)]), ('short', d_short, [(0, 7), (2, 7)]),
                       ('cross', d_cross, [(0, 7, 0), (0, 7, 1), (2, 7, 1)]),
                       ('meet', d_meet, [(0, 7, 0), (0, 7, 1), (2, 7, 1)])):
    for a in args:
        runs.append(';; run: %s %s => i32:%d' % (name, ' '.join(map(str, a)), s32(fn(*a))))

# ---- the module
def STEP(j):
    return ('    (local.set $x (i32.add (i32.mul (local.get $x) (i32.const 3)) (i32.const %d)))\n'
            '    (local.set $x (i32.xor (local.get $x) (i32.shr_u (local.get $x) (i32.const 5))))\n' % j)
def STEPS(lo, n): return ''.join(STEP(j) for j in range(lo, lo + n))
def RARE_NULL(o, k): return '    (if (ref.is_null %s) (then (return (call $slow (local.get $x) (i32.const %d)))))\n' % (o, k)
def RARE_LEAF(o, k): return '    (if (i32.eqz (ref.test (ref $leaf) %s)) (then (return (call $slow (local.get $x) (i32.const %d)))))\n' % (o, k)
# three tests of the globals that never fail, each with a call of its own: with
# the two a callee makes of its own, more calls than a whole callee is taken in with
NEVER = (RARE_NULL('(global.get $a)', 3) + RARE_LEAF('(global.get $a)', 4) + RARE_NULL('(global.get $b)', 5))
O = '(local.get $o)'; P = '(local.get $p)'; Q = '(local.get $q)'
callees = '''
  (func $c_val (param $o (ref null $rec)) (param $x i32) (result i32)
''' + RARE_NULL(O, 1) + NEVER + '''    (local.set $x (i32.add (local.get $x) (struct.get $rec 0 (local.get $o))))
''' + STEPS(1, 5) + RARE_LEAF(O, 2) + STEPS(6, 9) + '''    (i32.add (local.get $x) (struct.get $leaf 2 (ref.cast (ref $leaf) (local.get $o)))))
  (func $c_link (param $p (ref null $rec)) (param $q (ref null $rec)) (param $x i32) (result i32)
''' + RARE_NULL(P, 1) + '''    (struct.set $rec 1 (local.get $p) (local.get $q))
''' + STEPS(1, 4) + RARE_LEAF(Q, 2) + NEVER + STEPS(5, 10) + '''    (i32.add (local.get $x) (struct.get $leaf 2 (ref.cast (ref $leaf) (local.get $q)))))
  (func $c_bump (param $p (ref null $rec)) (param $q (ref null $rec)) (param $x i32) (result i32)
''' + RARE_NULL(P, 1) + NEVER + '''    (struct.set $rec 0 (local.get $p) (i32.add (struct.get $rec 0 (local.get $p)) (i32.const 1)))
''' + STEPS(1, 4) + RARE_LEAF(Q, 2) + STEPS(5, 10) + '''    (i32.add (local.get $x) (struct.get $rec 0 (local.get $p))))
  (func $c_twice (param $o (ref null $rec)) (param $x i32) (result i32)
''' + RARE_LEAF(O, 1) + NEVER + '''    (global.set $g1 (i32.add (global.get $g1) (i32.const 1)))
''' + STEPS(1, 6) + '''    (local.set $x (i32.add (local.get $x) (struct.get $rec 0 (local.get $o))))
''' + RARE_LEAF(O, 2) + STEPS(7, 8) + '''    (i32.add (local.get $x) (global.get $g1)))
  (func $c_join (param $o (ref null $rec)) (param $x i32) (result i32)
''' + NEVER + '''    (if (i32.eqz (ref.test (ref $leaf) (local.get $o))) (then (local.set $x (call $slow (local.get $x) (i32.const 1)))))
    (global.set $g1 (i32.add (global.get $g1) (i32.const 1)))
''' + STEPS(1, 6) + '''    (local.set $x (i32.add (local.get $x) (struct.get $rec 0 (local.get $o))))
''' + RARE_LEAF(O, 2) + STEPS(7, 8) + '''    (i32.add (local.get $x) (global.get $g1)))
  (func $c_arm (param $o (ref null $rec)) (param $x i32) (result i32)
''' + NEVER + '''    (if (ref.is_null (local.get $o))
      (then
''' + STEPS(100, 50) + '''        (return (local.get $x))))
''' + STEPS(1, 12) + '''    (local.get $x))
  (func $c_tail (param $o (ref null $rec)) (param $x i32) (result i32)
''' + NEVER + STEPS(1, 12) + '''    (if (ref.is_null (local.get $o)) (then (return (local.get $x))))
    (i32.add (local.get $x) (call $slow (local.get $x) (i32.const 1))))
  (func $c_short (param $o (ref null $rec)) (param $x i32) (result i32)
''' + RARE_NULL(O, 1) + NEVER + STEPS(1, 2) + '''    (local.get $x))
  (func $c_cross (param $o (ref null $rec)) (param $x i32) (param $m i32) (result i32)
''' + NEVER + '''    (if (i32.and (local.get $m) (i32.const 1))
      (then
''' + STEPS(1, 14) + '''        (if (ref.is_null (local.get $o)) (then (return (local.get $x))))
        (return (call $slow (local.get $x) (i32.const 1)))))
''' + STEPS(20, 3) + '''    (local.get $x))
  (func $c_meet (param $o (ref null $rec)) (param $x i32) (param $m i32) (result i32)
''' + NEVER + '''    (block $r
      (if (i32.and (local.get $m) (i32.const 1))
        (then
''' + STEPS(1, 14) + '''          (br_if $r (ref.is_null (local.get $o)))
          (return (call $slow (local.get $x) (i32.const 1)))))
''' + STEPS(20, 2) + '''    )
''' + STEPS(30, 3) + '''    (local.get $x))
  (func $c_unb (param $o (ref null $rec)) (param $x i32) (param $m i32) (result i32)
''' + RARE_NULL(O, 1) + NEVER + STEPS(1, 12) + '''    (if (i32.and (local.get $m) (i32.const 1)) (then (return (call $slow (local.get $x) (i32.const 2)))))
''' + STEPS(13, 2) + '''    (local.get $x))
  (func $c_alw (param $o (ref null $rec)) (param $x i32) (result i32)
''' + RARE_NULL(O, 1) + NEVER + STEPS(1, 12) + '''    (i32.add (local.get $x) (call $slow (local.get $x) (i32.const 1))))
  (func $c_big (param $o (ref null $rec)) (param $x i32) (result i32)
''' + RARE_NULL(O, 1) + NEVER + STEPS(1, 60) + '''    (local.get $x))
'''
PICK = '''  (func $pick (param $sel i32) (result (ref null $rec))
    (if (result (ref null $rec)) (i32.eqz (local.get $sel))
      (then (global.get $a))
      (else (if (result (ref null $rec)) (i32.eq (local.get $sel) (i32.const 1))
        (then (global.get $b)) (else (ref.null $rec))))))
'''
def loop(n, body):
    return ('''    (loop $l
''' + body + '''      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const %d))))
''' % n)
CNT = '(i32.mul (global.get $cnt) (i32.const 1000003))'
XI = '(i32.add (local.get $x) (local.get $i))'
drivers = '''
  (func $val (export "val") (param $sel i32) (param $x i32) (result i32)
    (local $i i32) (local $s i32) (local $o (ref null $rec))
    (local.set $o (call $pick (local.get $sel)))
''' + loop(3, '      (local.set $s (i32.add (local.get $s) (call $c_val (local.get $o) %s)))\n' % XI) + '''    (i32.add (local.get $s) %s))
  (func $link (export "link") (param $sel i32) (param $x i32) (result i32)
    (local $i i32) (local $s i32) (local $q (ref null $rec))
    (local.set $q (call $pick (local.get $sel)))
''' % CNT + loop(2, '      (local.set $s (i32.add (local.get $s) (call $c_link (global.get $a) (local.get $q) %s)))\n' % XI) + '''    (i32.add (i32.add (local.get $s)
               (select (i32.const 100000) (i32.const 0)
                 (ref.eq (struct.get $rec 1 (global.get $a)) (local.get $q))))
             %s))
  (func $bump (export "bump") (param $sel i32) (param $x i32) (result i32)
    (local $i i32) (local $s i32) (local $q (ref null $rec))
    (local.set $q (call $pick (local.get $sel)))
''' % CNT + loop(2, '      (local.set $s (i32.add (local.get $s) (call $c_bump (global.get $a) (local.get $q) %s)))\n' % XI) + '''    (i32.add (i32.add (local.get $s) (i32.mul (struct.get $rec 0 (global.get $a)) (i32.const 1000))) %s))
  (func $twice (export "twice") (param $sel i32) (param $x i32) (result i32)
    (local $i i32) (local $s i32) (local $o (ref null $rec))
    (local.set $o (call $pick (local.get $sel)))
''' % CNT + loop(2, '      (local.set $s (i32.add (local.get $s) (call $c_twice (local.get $o) %s)))\n' % XI) + '''    (i32.add (i32.add (local.get $s) (i32.mul (global.get $g1) (i32.const 1000))) %s))
  (func $unb (export "unb") (param $sel i32) (param $x i32) (param $m i32) (result i32)
    (local $i i32) (local $s i32) (local $o (ref null $rec))
    (local.set $o (call $pick (local.get $sel)))
''' % CNT + loop(2, '      (local.set $s (i32.add (local.get $s) (call $c_unb (local.get $o) %s (local.get $m))))\n' % XI) + '''    (i32.add (local.get $s) %s))
  (func $alw (export "alw") (param $sel i32) (param $x i32) (result i32)
    (local $i i32) (local $s i32) (local $o (ref null $rec))
    (local.set $o (call $pick (local.get $sel)))
''' % CNT + loop(2, '      (local.set $s (i32.add (local.get $s) (call $c_alw (local.get $o) %s)))\n' % XI) + '''    (i32.add (local.get $s) %s))
  (func $big (export "big") (param $sel i32) (param $x i32) (result i32)
    (local $i i32) (local $s i32) (local $o (ref null $rec))
    (local.set $o (call $pick (local.get $sel)))
''' % CNT + loop(2, '      (local.set $s (i32.add (local.get $s) (call $c_big (local.get $o) %s)))\n' % XI) + '''    (i32.add (local.get $s) %s))
  (func $many (export "many") (param $sel i32) (param $x i32) (result i32)
    (local $s i32) (local $o (ref null $rec))
    (local.set $o (call $pick (local.get $sel)))
''' % CNT + ''.join('    (local.set $s (i32.add (local.get $s) (call $c_val (local.get $o) (i32.add (local.get $x) (i32.const %d)))))\n' % i for i in range(NMANY)) + '''    (i32.add (local.get $s) %s))
''' % CNT
def DRV(n):
    return ('  (func $%s (export "%s") (param $sel i32) (param $x i32) (result i32)\n'
            '    (local $i i32) (local $s i32) (local $o (ref null $rec))\n'
            '    (local.set $o (call $pick (local.get $sel)))\n' % (n, n) +
            loop(2, '      (local.set $s (i32.add (local.get $s) (call $c_%s (local.get $o) %s)))\n' % (n, XI)) +
            '    (i32.add (i32.add (local.get $s) (i32.mul (global.get $g1) (i32.const 1000))) %s))\n' % CNT)
drivers += ''.join(DRV(n) for n in ('join', 'arm', 'tail', 'short'))
def DRV3(n):
    return ('  (func $%s (export "%s") (param $sel i32) (param $x i32) (param $m i32) (result i32)\n'
            '    (local $i i32) (local $s i32) (local $o (ref null $rec))\n'
            '    (local.set $o (call $pick (local.get $sel)))\n' % (n, n) +
            loop(2, '      (local.set $s (i32.add (local.get $s) (call $c_%s (local.get $o) %s (local.get $m))))\n' % (n, XI)) +
            '    (i32.add (local.get $s) %s))\n' % CNT)
drivers += ''.join(DRV3(n) for n in ('cross', 'meet'))
HEAD = ''';; A callee's likely ways taken into its caller (inline.c pinl_build's hot
;; start): where a callee too big to take in whole has tests with rare ways
;; by the rules, its start is all the likely ways reach that goes on, by
;; likely ways, to a return without a call -- past the 32 instructions any
;; start may have, up to 160 -- and each way out of it calls the callee from
;; its beginning.  Every callee here tests its object for null, three
;; globals that are what they should be, and its object's type, each with a
;; call on the way it fails (six calls, more than a callee taken in whole
;; makes), and mixes x through 14 steps or more of three instructions.
;;
;;   val     the plain case: a start of some sixty instructions, the object's
;;           type tested in the middle of it; run with an object of the type
;;           (no call), of another (the callee called from the test on, its
;;           start run again there) and null
;;   link    p.next = q written ahead of q's type test: a field nothing in
;;           the start reads before, written again by the call a failed test
;;           makes -- the same value, of values the write did not change --
;;           so the write is in the start
;;   bump    p.v = p.v + 1 ahead of that test: it reads what it writes, and
;;           made twice would add 2 -- it is made past the test, on the way
;;           that returns, and p.v is 5 after two calls whichever way they go
;;   twice   a global counted up ahead of a second test of the type the first
;;           test has decided, a field's read between: on the callee as its
;;           wasm has it the count would be made twice by a call from the
;;           second test, and the read keeps it from going past; made ready
;;           as its own compile makes it (opt.c ir_pinl_prep), the second
;;           test is gone, and the start is the whole likely way
;;   unb     a way out by a test no rule calls rare, 38 instructions in: a
;;           start all the same -- the way that calls is the one cut
;;   alw     the likely way itself calls: no start
;;   big     a likely way of 180 instructions: no start
;;   many    ninety calls of val's callee in one function: starts until
;;           their share of the caller's growth, 4000 instructions, is spent
;;   join    twice's callee with the first test's rare way a call that
;;           comes back: the second test is decided on the likely way only
;;           once that way has code of its own past where the rare one
;;           rejoined (the tails, which the callee is made ready with)
;;   arm     a null test's rare way 150 instructions that return without a
;;           call: no way the start takes, which is the likely way alone
;;   tail    a likely way that ends in a call, past 36 instructions, with a
;;           rare way out to a return: no start -- a block goes into one
;;           only where a likely way goes on from it to a return
;;   short   a start of under 32 instructions: taken in as any callee's is,
;;           from the caller's growth and not from the starts' share
;;   cross   a way of 42 instructions, picked by a test no rule calls rare,
;;           whose likely way on is a call and whose rare way returns: it
;;           is on a likely way but none goes on from it to a return, so
;;           the start is the other way's nine instructions -- one any
;;           callee may have -- and the long way is cut where it begins
;;   meet    the same with the rare way out joining the other way's last
;;           steps: the block they share is on a likely way to a return,
;;           and the long way reaches it by its rare way out alone, which
;;           is no likely way on (pinned without the tails as well, which
;;           give the likely way an end of its own: there the shared block
;;           is the one the start has)
;;
;; Answers by the model that made the module: each driver's sum, with the
;; number of slow calls made times 1000003, and link's 100000 where p.next
;; is q, bump's p.v and twice's count times 1000.
;;
;; (stats: a start each in val, link, bump, twice, unb, join and arm, 47 in
;; many; cross's and meet's are no likely ways' starts.  Without the tails
;; join has none.  The blocks are those the callers were given, a start's
;; own and no others: a block for each of a callee's, the ones a start
;; leaves out too, was 1,276.)
;;
;; stats -O2: inl_hot_starts=54 inl_blocks=703
;; stats -O2 -fno-tails: inl_hot_starts=53 inl_blocks=698
;;
'''
MOD = '''(module
  (type $rec (sub (struct (field (mut i32)) (field (mut (ref null $rec))))))
  (type $leaf (sub $rec (struct (field (mut i32)) (field (mut (ref null $rec))) (field (mut i32)))))
  (global $a (mut (ref null $rec)) (struct.new $leaf (i32.const 3) (ref.null $rec) (i32.const 30)))
  (global $b (mut (ref null $rec)) (struct.new $rec (i32.const 5) (ref.null $rec)))
  (global $cnt (mut i32) (i32.const 0))
  (global $g1 (mut i32) (i32.const 0))
  ;; c + 3k, counted, recursively so that it is not inlined
  (func $slow (param $c i32) (param $k i32) (result i32)
    (call $slow2 (local.get $c) (local.get $k) (local.get $k)))
  (func $slow2 (param $c i32) (param $k i32) (param $d i32) (result i32)
    (if (result i32) (i32.eqz (local.get $d))
      (then (global.set $cnt (i32.add (global.get $cnt) (i32.const 1)))
            (i32.add (local.get $c) (i32.mul (local.get $k) (i32.const 3))))
      (else (call $slow2 (local.get $c) (local.get $k) (i32.sub (local.get $d) (i32.const 1))))))
'''
open(sys.argv[1], 'w').write(
    HEAD + '\n'.join(runs) + '\n' + MOD + PICK + callees + drivers + ')\n')
