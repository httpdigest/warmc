"""test/cases_opt/full_adder.wat: limbs of GMP's generic mpn_add_n and
mpn_sub_n, which the arm64 back end makes adcs and sbcs, the carry handed
from limb to limb in the flag (isel.c find_full_adders), in each form a
compare may take, and the near misses it must leave alone.  Answers worked
out here.

  python3 test/gen_fulladder.py test/cases_opt/full_adder.wat
"""
import random
import sys

UP, VP, RP = 0, 1024, 2048
# no limb is this: the loops leave on it too, so none is a counted loop the
# unroller would unroll
STOP = 0x5eed5eed
NS = (1, 2, 3, 7, 16, 32)


def mask(t):
    return (1 << (64 if t == 'i64' else 32)) - 1


def s32(x):
    x &= 0xffffffff
    return x - (1 << 32) if x >> 31 else x


def words():
    """limbs whose sums carry in runs and whose differences borrow in runs"""
    rng = random.Random(7)
    pick = [0, 0xffffffff, 1, 0x80000000, 0x7fffffff, 0xfffffffe]
    u, v = [], []
    for i in range(256):
        k = rng.randrange(6)
        if k == 0:
            u.append(0xffffffff); v.append(rng.choice((0, 1, 0xffffffff)))
        elif k == 1:
            u.append(0); v.append(rng.choice((0, 1, 0xffffffff)))
        elif k == 2:
            u.append(rng.choice(pick)); v.append(rng.choice(pick))
        else:
            u.append(rng.getrandbits(32)); v.append(rng.getrandbits(32))
    return u, v


U32, V32 = words()
assert STOP not in U32


def limbs(t, base):
    w = U32 if base == UP else V32
    if t == 'i32':
        return w
    return [w[2 * i] | w[2 * i + 1] << 32 for i in range(128)]


def lt(t, a, b):
    return 1 if a & mask(t) < b & mask(t) else 0


# a variant: name, type, limbs an iteration, the limb's wat (from its
# offset), the limb's model (u, v, cy, st) -> (r, cy), shapes
def limb_add(t, c1, c2, comb, swap=False):
    ext = (lambda e: '(i64.extend_i32_u %s)' % e) if t == 'i64' else (lambda e: e)
    r = '(%s.add (local.get $cy) (local.get $s))' if swap else '(%s.add (local.get $s) (local.get $cy))'

    def wat(off):
        return '''
      (local.set $u (%(t)s.load offset=%(o)d (local.get $up)))
      (local.set $v (%(t)s.load offset=%(o)d (local.get $vp)))
      (local.set $s (%(t)s.add (local.get $u) (local.get $v)))
      (local.set $r %(r)s)
      (%(t)s.store offset=%(o)d (local.get $rp) (local.get $r))
      (local.set $cy %(cy)s)''' % dict(t=t, o=off, r=r % t, cy=ext('(%s %s %s)' % (comb, c1 % t, c2 % t)))
    return wat


def model_add(t, c1, c2):
    def step(u, v, cy, st):
        m = mask(t)
        s = (u + v) & m
        r = (s + cy) & m
        env = dict(u=u, v=v, s=s, r=r, cy=cy)
        return r, (c1(t, env) | c2(t, env))
    return step


def limb_sub(t, b1, b2, comb):
    ext = (lambda e: '(i64.extend_i32_u %s)' % e) if t == 'i64' else (lambda e: e)

    def wat(off):
        return '''
      (local.set $u (%(t)s.load offset=%(o)d (local.get $up)))
      (local.set $v (%(t)s.load offset=%(o)d (local.get $vp)))
      (local.set $s (%(t)s.sub (local.get $u) (local.get $v)))
      (local.set $r (%(t)s.sub (local.get $s) (local.get $cy)))
      (%(t)s.store offset=%(o)d (local.get $rp) (local.get $r))
      (local.set $cy %(cy)s)''' % dict(t=t, o=off, cy=ext('(%s %s %s)' % (comb, b1 % t, b2 % t)))
    return wat


def model_sub(t, b1, b2):
    def step(u, v, cy, st):
        m = mask(t)
        s = (u - v) & m
        r = (s - cy) & m
        env = dict(u=u, v=v, s=s, r=r, cy=cy)
        return r, (b1(t, env) | b2(t, env))
    return step


def L(a, b):
    """a <u b, in wat and in the model"""
    return ('(%%s.lt_u (local.get $%s) (local.get $%s))' % (a, b),
            lambda t, e: lt(t, e[a], e[b]))


def G(a, b):
    """b <u a written a >u b"""
    return ('(%%s.gt_u (local.get $%s) (local.get $%s))' % (a, b),
            lambda t, e: lt(t, e[b], e[a]))


def LE(a, b):
    """a <=u b: no carry"""
    return ('(%%s.le_u (local.get $%s) (local.get $%s))' % (a, b),
            lambda t, e: 1 - lt(t, e[b], e[a]))


def add_v(name, t, c1, c2, comb='i32.or', swap=False, nl=1, shapes=None):
    return dict(name=name, t=t, nl=nl, wat=limb_add(t, c1[0], c2[0], comb, swap),
                step=model_add(t, c1[1], c2[1]),
                shapes=shapes if shapes is not None else [(r'\sadcs\s', nl), (r'\sorr\s', 0)])


def sub_v(name, t, b1, b2, comb='i32.or', nl=1, shapes=None):
    return dict(name=name, t=t, nl=nl, wat=limb_sub(t, b1[0], b2[0], comb),
                step=model_sub(t, b1[1], b2[1]),
                shapes=shapes if shapes is not None else [(r'\ssbcs\s', nl), (r'\sorr\s', 0)])


ONE = r'\scmp\s[wx]\d+, #0x1$'
HS, LO = r'\scset\sw\d+, hs$', r'\scset\sw\d+, lo$'
ZERO = r'\scmp\swzr, w\d+$'
VARIANTS = [
    # the carry out of u + v and of s + cin, each compare either way round
    add_v('add_or', 'i32', L('s', 'u'), L('r', 's')),
    add_v('add_gt', 'i32', G('u', 's'), G('s', 'r')),
    add_v('add_vcin', 'i32', L('s', 'v'), L('r', 'cy')),
    add_v('add_xor', 'i32', L('r', 's'), L('s', 'u'), comb='i32.xor'),
    add_v('add_swap', 'i32', L('s', 'u'), L('r', 's'), swap=True),
    add_v('add64', 'i64', L('s', 'u'), L('r', 's')),
    # two limbs an iteration: the second's carry in is in the flag
    add_v('add2', 'i32', L('s', 'u'), L('r', 's'), nl=2,
          shapes=[(r'\sadcs\s', 2), (ONE, 1), (HS, 2)]),
    add_v('add2_64', 'i64', L('s', 'v'), L('r', 's'), nl=2,
          shapes=[(r'\sadcs\s', 2), (ONE, 1)]),
    # borrows: u - v and s - bin
    sub_v('sub_or', 'i32', L('u', 'v'), L('s', 'cy')),
    sub_v('sub_gt', 'i32', G('s', 'u'), G('r', 's')),
    sub_v('sub_mix', 'i32', L('u', 's'), L('s', 'r'), comb='i32.xor'),
    sub_v('sub64', 'i64', L('u', 'v'), L('s', 'cy')),
    sub_v('sub2', 'i32', L('u', 'v'), L('s', 'cy'), nl=2,
          shapes=[(r'\ssbcs\s', 2), (ZERO, 1), (LO, 2)]),
    # not a carry: u + v's compare against r, r's against u
    add_v('add_notc1', 'i32', L('s', 'r'), L('r', 's'), shapes=[(r'\sadcs\s', 0)]),
    add_v('add_notc2', 'i32', L('s', 'u'), L('r', 'u'), shapes=[(r'\sadcs\s', 0)]),
    add_v('add_le', 'i32', L('s', 'u'), LE('r', 's'), shapes=[(r'\sadcs\s', 0)]),
    # a borrow's compare of v and u the wrong way round
    sub_v('sub_notb1', 'i32', L('v', 'u'), L('s', 'cy'), shapes=[(r'\ssbcs\s', 0)]),
    sub_v('sub_notb1y', 'i32', L('u', 'cy'), L('s', 'cy'), shapes=[(r'\ssbcs\s', 0)]),
    # an add's compares on a sub
    sub_v('sub_addcmp', 'i32', L('s', 'u'), L('r', 's'), shapes=[(r'\ssbcs\s', 0)]),
]


def special():
    """near misses that need more than a compare changed"""
    out = []
    # the carry in may be 2 or 3
    out.append(dict(name='add_cy3', t='i32', nl=1, pre='''
      (local.set $cy (i32.and (local.get $cy) (i32.const 3)))''',
        wat=limb_add('i32', L('s', 'u')[0], L('r', 's')[0], 'i32.add'),
        step=lambda u, v, cy, st: model_add('i32', L('s', 'u')[1], L('r', 's')[1])(u, v, cy & 3, st),
        init='(local.set $cy (i32.const 6))', cy0=6, shapes=[(r'\sadcs\s', 0)]))
    # the carries added, which known bits cannot see is 0 or 1 round the
    # loop: the carry in is masked to its low bit
    out.append(dict(name='add_add', t='i32', nl=1, pre='''
        (local.set $cy (i32.and (local.get $cy) (i32.const 1)))''',
        wat=limb_add('i32', L('s', 'u')[0], L('r', 's')[0], 'i32.add'),
        step=lambda u, v, cy, st: model_add('i32', L('s', 'u')[1], L('r', 's')[1])(u, v, cy & 1, st),
        shapes=[(r'\sadcs\s', 1), (r'\sorr\s', 0)]))
    # s read besides: the sum is kept
    def s_used(off):
        return limb_add('i32', L('s', 'u')[0], L('r', 's')[0], 'i32.or')(off) + '''
      (local.set $z (i32.xor (local.get $z) (local.get $s)))'''

    def s_used_step(u, v, cy, st):
        r, c = model_add('i32', L('s', 'u')[1], L('r', 's')[1])(u, v, cy, st)
        st['z'] ^= (u + v) & 0xffffffff
        return r, c
    out.append(dict(name='add_sused', t='i32', nl=1, wat=s_used, step=s_used_step,
                    shapes=[(r'\sadcs\s', 0)]))
    # a compare and select between two limbs writes the flag: the second
    # limb's carry in goes back into it from its register
    def sel(off):
        w = limb_add('i32', L('s', 'u')[0], L('r', 's')[0], 'i32.or')(off)
        if off == 0:
            w += '''
      (local.set $z (i32.add (local.get $z) (select (i32.const 3) (i32.const 1)
        (i32.lt_u (local.get $r) (local.get $z)))))'''
        return w

    def sel_step(u, v, cy, st):
        r, c = model_add('i32', L('s', 'u')[1], L('r', 's')[1])(u, v, cy, st)
        if st['limb'] % 2 == 0:
            st['z'] = (st['z'] + (3 if r < st['z'] else 1)) & 0xffffffff
        return r, c
    out.append(dict(name='add2_clobber', t='i32', nl=2, wat=sel, step=sel_step,
                    shapes=[(r'\sadcs\s', 2), (ONE, 2)]))
    plain = limb_add('i32', L('s', 'u')[0], L('r', 's')[0], 'i32.or')
    ma = model_add('i32', L('s', 'u')[1], L('r', 's')[1])

    def before_cy(text):
        """a limb with text put in just before its carry out"""
        return lambda off: plain(off).replace('\n      (local.set $cy', text + '\n      (local.set $cy')
    # a compare of a limb's carry read besides: kept, and the limb too
    out.append(dict(name='add_c1used', t='i32', nl=1,
                    wat=before_cy('''
      (local.set $z (i32.add (local.get $z) (i32.lt_u (local.get $s) (local.get $u))))'''),
                    step=lambda u, v, cy, st: (st.__setitem__('z', st['z'] + lt('i32', (u + v) & 0xffffffff, u)),
                                               ma(u, v, cy, st))[1],
                    shapes=[(r'\sadcs\s', 0)]))
    out.append(dict(name='add_c2used', t='i32', nl=1,
                    wat=before_cy('''
      (local.set $z (i32.add (local.get $z) (i32.lt_u (local.get $r) (local.get $s))))'''),
                    step=lambda u, v, cy, st: (st.__setitem__('z', st['z'] + lt('i32', (u + v + cy) & 0xffffffff,
                                                                                (u + v) & 0xffffffff)),
                                               ma(u, v, cy, st))[1],
                    shapes=[(r'\sadcs\s', 0)]))
    # an operand shifted, the shift folded into the add: no register to read
    def shl(off):
        return plain(off).replace(
            '(local.set $v (i32.load offset=%d (local.get $vp)))' % off,
            '(local.set $v (i32.shl (i32.load offset=%d (local.get $vp)) (i32.const 1)))' % off)
    out.append(dict(name='add_shl', t='i32', nl=1, wat=shl,
                    step=lambda u, v, cy, st: ma(u, (v << 1) & 0xffffffff, cy, st),
                    shapes=[(r'\sadcs\s', 0)]))
    # the carry out in another block than r, past a branch, and well down
    # it: left alone
    def split_step(u, v, cy, st):
        for _ in range(4):
            st['z'] = (st['z'] * 3 + u) & 0xffffffff
        return ma(u, v, cy, st)
    out.append(dict(name='add_split', t='i32', nl=1,
                    wat=before_cy('''
      (if (i32.eq (local.get $u) (i32.const %d)) (then (i32.store (i32.const 4000) (local.get $r))))''' % STOP +
                                  '''
      (local.set $z (i32.add (i32.mul (local.get $z) (i32.const 3)) (local.get $u)))''' * 4),
                    step=split_step, shapes=[(r'\sadcs\s', 0)]))
    # a sum that is a difference: no carry
    def subs(off):
        return plain(off).replace('(local.set $s (i32.add', '(local.set $s (i32.sub')

    def subs_step(u, v, cy, st):
        s_ = (u - v) & 0xffffffff
        r = (s_ + cy) & 0xffffffff
        return r, lt('i32', s_, u) | lt('i32', r, s_)
    out.append(dict(name='add_subs', t='i32', nl=1, wat=subs, step=subs_step,
                    shapes=[(r'\sadcs\s', 0)]))
    # a carry in shifted down from a word's top bit, the shift folded into
    # the add: no register to read
    def shrcin(off):
        return plain(off).replace('(local.set $r (i32.add (local.get $s) (local.get $cy)))',
                                  '(local.set $r (i32.add (local.get $s) (i32.shr_u (local.get $v) (i32.const 31))))')

    def shrcin_step(u, v, cy, st):
        return ma(u, v, v >> 31, st)
    out.append(dict(name='add_shrcin', t='i32', nl=1, wat=shrcin, step=shrcin_step,
                    shapes=[(r'\sadcs\s', 0)]))
    # a call on one way between two limbs, in an arm emitted after the rest
    # (ir.c call_arm): the block after the branch follows the first limb's
    # in the code but not always when run, so the second limb puts its
    # carry in back in the flag
    def call(off):
        w = plain(off)
        if off == 0:
            w += '''
      (if (i32.and (local.get $u) (i32.const 1))
        (then (local.set $z (i32.add (local.get $z)
                (call $grow (i32.or (local.get $z) (i32.const 1)) (i32.const 2))))))'''
        return w

    def call_step(u, v, cy, st):
        r, c = ma(u, v, cy, st)
        if st['limb'] % 2 == 0 and u & 1:
            st['z'] = (st['z'] + ((st['z'] | 1) << 1)) & 0xffffffff
        return r, c
    out.append(dict(name='add2_call', t='i32', nl=2, wat=call, step=call_step,
                    shapes=[(r'\sadcs\s', 2), (ONE, 2)]))
    # a compare and select between r and its carry out, which writes the
    # flag: the carry out is moved up to r
    def between_step(u, v, cy, st):
        r, c = ma(u, v, cy, st)
        st['z'] = (st['z'] + (3 if r < st['z'] else 1)) & 0xffffffff
        return r, c
    out.append(dict(name='add_between', t='i32', nl=1,
                    wat=before_cy('''
      (local.set $z (i32.add (local.get $z) (select (i32.const 3) (i32.const 1)
        (i32.lt_u (local.get $r) (local.get $z)))))'''),
                    step=between_step, shapes=[(r'\sadcs\s', 1), (ONE, 1)]))
    # an add's carry out the borrow in of a sub, and that the next add's
    # carry in: the flag holds a carry, not a borrow, so each is put back
    sub = limb_sub('i32', L('u', 'v')[0], L('s', 'cy')[0], 'i32.or')
    ms = model_sub('i32', L('u', 'v')[1], L('s', 'cy')[1])
    out.append(dict(name='add_then_sub', t='i32', nl=2,
                    wat=lambda off: (plain if off == 0 else sub)(off),
                    step=lambda u, v, cy, st: (ma if st['limb'] % 2 == 0 else ms)(u, v, cy, st),
                    shapes=[(r'\sadcs\s', 1), (r'\ssbcs\s', 1), (ONE, 1), (ZERO, 1)]))
    # the borrow out made, and read, before r: left alone
    def late(off):
        return '''
      (local.set $u (i32.load offset=%(o)d (local.get $up)))
      (local.set $v (i32.load offset=%(o)d (local.get $vp)))
      (local.set $s (i32.sub (local.get $u) (local.get $v)))
      (local.set $v (i32.or (i32.lt_u (local.get $u) (local.get $v)) (i32.lt_u (local.get $s) (local.get $cy))))
      (local.set $z (i32.add (i32.mul (local.get $z) (i32.const 3)) (local.get $v)))
      (i32.store offset=%(o)d (local.get $rp) (i32.sub (local.get $s) (local.get $cy)))
      (local.set $cy (local.get $v))''' % dict(o=off)

    def late_step(u, v, cy, st):
        r, c = model_sub('i32', L('u', 'v')[1], L('s', 'cy')[1])(u, v, cy, st)
        st['z'] = (st['z'] * 3 + c) & 0xffffffff
        return r, c
    out.append(dict(name='sub_late', t='i32', nl=1, wat=late, step=late_step,
                    shapes=[(r'\ssbcs\s', 0)]))
    return out


def func(vr):
    t, nl = vr['t'], vr['nl']
    size = 8 if t == 'i64' else 4
    body = ''.join(vr['wat'](k * size) for k in range(nl)).replace('\n      ', '\n        ')
    step = nl * size
    cy = '(local.get $cy)' if t == 'i32' else '(i32.wrap_i64 (local.get $cy))'
    return '''(func $%(name)s (export "%(name)s") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy %(t)s) (local $u %(t)s) (local $v %(t)s) (local $s %(t)s) (local $r %(t)s)
    (local.set $vp (i32.const %(vp)d))
    (local.set $rp (i32.const %(rp)d))
    (local.set $k (local.get $n))%(init)s
    (block $out
      (loop $l%(pre)s%(body)s
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const %(stop)d)))
        (local.set $up (i32.add (local.get $up) (i32.const %(step)d)))
        (local.set $vp (i32.add (local.get $vp) (i32.const %(step)d)))
        (local.set $rp (i32.add (local.get $rp) (i32.const %(step)d)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const %(words)d)) %(cy)s) (local.get $z))
             (i32.const %(tag)d)))''' % dict(tag=vr['tag'],
        name=vr['name'], t=t, vp=VP, rp=RP, body=body, step=step, cy=cy, words=step // 4,
        init=('\n    ' + vr['init']) if 'init' in vr else '', pre=vr.get('pre', ''), stop=STOP)


def answer(vr, n):
    t, nl = vr['t'], vr['nl']
    us, vs = limbs(t, UP), limbs(t, VP)
    st = dict(z=0, limb=0)
    cy = vr.get('cy0', 0)
    out = []
    for i in range(n * nl):
        st['limb'] = i
        r, cy = vr['step'](us[i], vs[i], cy, st)
        out.append(r)
    ws = []
    for r in out:
        ws += [r & 0xffffffff] if t == 'i32' else [r & 0xffffffff, r >> 32]
    h = cy & 0xffffffff
    for w in ws:
        h = (h * 31 + w) & 0xffffffff
    return s32((h ^ st['z']) + vr['tag'])


def data(base, w):
    return '  (data (i32.const %d) "%s")' % (base, ''.join(
        '\\%02x' % b for x in w for b in x.to_bytes(4, 'little')))


def main():
    vs = VARIANTS + special()
    # each its own answers, so no two compile the same and are merged
    for k, vr in enumerate(vs):
        vr['tag'] = 0x1000 * (k + 1)
    shapes, runs, funcs = [], [], []
    for vr in vs:
        for pat, k in vr['shapes']:
            shapes.append(';; shape: %s %s=%d' % (vr['name'], pat, k))
        for n in NS:
            runs.append(';; run: %s %d => i32:%d' % (vr['name'], n, answer(vr, n)))
        funcs.append('  ' + func(vr))
    head = '''\
;; Limbs of GMP's generic mpn_add_n, s = u + v, r = s + cin, cout =
;; (s <u u) | (r <u s), and mpn_sub_n, s = u - v, r = s - bin, bout =
;; (u <u v) | (s <u bin), cin and bin 0 or 1: the arm64 back end makes each
;; an adcs or sbcs and a cset, and a limb whose carry in the one before put
;; in the flag reads it there (isel.c find_full_adders) -- each compare in
;; its other forms, the or as an add or a xor, the carry in the add's
;; first operand, i64 limbs whose carry is widened, two limbs an iteration.
;; Left alone: compares that are no carry, a carry in that may be 2, a sum
;; or a compare read besides, an operand or carry in shifted into the add,
;; a sum that is a difference, a carry out past a branch, a borrow made and
;; read before its difference.  A compare between two limbs that writes
;; the flag makes the second read its carry in from its register, as do a
;; sub after an add and a limb after a call's arm emitted elsewhere; one
;; between r and its carry out is passed by moving the carry out up.  One
;; limb an iteration unless written two; the loops leave on a limb no array
;; holds too, so the unroller leaves them be.  Made by test/gen_fulladder.py.
;;
'''
    out = head + '\n'.join(shapes) + '\n;;\n' + '\n'.join(runs) + '\n\n(module\n  (memory 1)\n'
    out += data(UP, U32) + '\n' + data(VP, V32) + '\n'
    out += '''  ;; doubles c, recursively so that it is not inlined
  (func $grow (param $c i32) (param $k i32) (result i32)
    (if (result i32) (i32.eqz (local.get $k))
      (then (i32.shl (local.get $c) (i32.const 1)))
      (else (call $grow (local.get $c) (i32.sub (local.get $k) (i32.const 1))))))
'''
    out += '''  (func $hash (param $w i32) (param $h i32) (result i32)
    (local $p i32)
    (local.set $p (i32.const %d))
    (block $done
      (loop $l
        (br_if $done (i32.eqz (local.get $w)))
        (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31)) (i32.load (local.get $p))))
        (local.set $p (i32.add (local.get $p) (i32.const 4)))
        (local.set $w (i32.sub (local.get $w) (i32.const 1)))
        (br $l)))
    (local.get $h))
''' % RP
    out += '\n'.join(funcs) + '\n)\n'
    open(sys.argv[1], 'w').write(out)


main()
