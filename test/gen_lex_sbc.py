"""test/cases_opt/lex_sbc.wat: two pairs compared by their first members and
then by their second, neither pair one value's bits, are a compare of the
seconds and a subtract with carry of the firsts on arm64 (isel.c
find_cmp_chains' cch_lex, isel_ops.inc emit_lex).  The module's header says
what each function is; every answer is worked out here on a model of the
function in Python, which is the test as the source has it.

  python3 test/gen_lex_sbc.py test/cases_opt/lex_sbc.wat
  python3 test/gen_lex_sbc.py --each [NAME ...]

--each compiles every function (or the named ones) alone with build/warmc
and prints what --stats counts for it as lex_sbcs beside what is expected
(arm64's count: on x86-64 it is 0).
"""
import os, re, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_lex_cmp import M32, M64, s32, s64, rel, QUADS

CASES = []


def case(name, what, wat, model, runs, want, shapes=(), ty='i32'):
    CASES.append((name, what, wat, model, runs, want, shapes, ty))


def sel(c, t, e): return '(select %s %s %s)' % (t, e, c)
def op(o, x, y): return '(%s %s %s)' % (o, x, y)


M, N, P, Q = '(local.get $m)', '(local.get $n)', '(local.get $p)', '(local.get $q)'
FN = '''
  (func $%(n)s (export "%(n)s") (param $m %(tm)s) (param $n %(tn)s) (param $p %(tm)s) (param $q %(tn)s) (result i32)%(lo)s
    %(body)s)
'''
# the quads of gen_lex_cmp are (m_a, n_a, m_b, n_b): here the arguments in that order, and wide ones
Q64 = [tuple((v * 0x100000001 ^ (v >> 31) * 0x7fffffff00000000) & M64 for v in q) for q in QUADS] + [
    (1 << 63, 0, 1, 0), (1, 0, 1 << 63, 0), (7, 1 << 63, 7, 1), (7, 1, 7, 1 << 63), (M64, M64, M64, M64 - 1)]


def lex(name, what, want, body, model, runs=QUADS, tm='i32', tn='i32', shapes=(), tag=0, lo=''):
    # (tag: a constant added to the answer, which keeps a function whose code is counted from being
    # merged with another that comes to the same code)
    if tag:
        body = '(i32.add %s (i32.const %d))' % (body, tag)
        test = model
        model = lambda m, n, p, q: test(m, n, p, q) + tag
    case(name, what, FN % dict(n=name, tm=tm, tn=tn, body=body, lo=lo and '\n    ' + lo), model, runs, want, shapes)


def test_of(mo, no, bm=32, bn=32):
    """the model of `m == p ? n NO q : m MO p`"""
    return lambda m, n, p, q: rel(no, n, q, bn) if m == p else rel(mo, m, p, bm)


def body_of(mo, no, tm='i32', tn='i32'):
    return sel(op(tm + '.eq', M, P), op('%s.%s' % (tn, no), N, Q), op('%s.%s' % (tm, mo), M, P))


# ---- every less and greater of the firsts with every one of the seconds ----

lex('four', 'four i32s, the firsts and the seconds less, unsigned: cmp, sbcs and the borrow', 1,
    body_of('lt_u', 'lt_u'), test_of('lt_u', 'lt_u'),
    shapes=[(r'\bsbcs\b', 1), (r'\bcmp\b', 1), (r'\bcset\b', 1), (r'\bcsel\b', 0)], tag=100)
for mo in ('lt_u', 'le_u', 'gt_u', 'ge_u', 'lt_s', 'le_s', 'gt_s', 'ge_s'):
    for no in ('lt_u', 'le_u', 'gt_u', 'ge_u'):
        lex(mo.replace('_', '') + '_' + no.replace('_', ''),
            'the firsts %s, the seconds %s' % (mo.replace('_', ' '), no.replace('_', ' ')), 1,
            body_of(mo, no), test_of(mo, no))
lex('ne', 'on a ne, its arms the other way round', 1,
    sel(op('i32.ne', M, P), op('i32.gt_s', M, P), op('i32.le_u', N, Q)), test_of('gt_s', 'le_u'))
lex('turned', 'the firsts\' compare with its operands the other way round from the test for equality', 1,
    sel(op('i32.eq', M, P), op('i32.lt_u', N, Q), op('i32.gt_u', P, M)), test_of('lt_u', 'lt_u'))

# ---- widths ------------------------------------------------------------------

lex('wide', 'four i64s, two 128-bit numbers in halves, signed: the same in x registers', 1,
    body_of('lt_s', 'lt_u', 'i64', 'i64'), test_of('lt_s', 'lt_u', 64, 64), runs=Q64, tm='i64', tn='i64',
    shapes=[(r'\bsbcs\s+xzr', 1), (r'\bcmp\s+x', 1)], tag=200)
lex('wideu', 'unsigned, and not strict', 1,
    body_of('ge_u', 'ge_u', 'i64', 'i64'), test_of('ge_u', 'ge_u', 64, 64), runs=Q64, tm='i64', tn='i64')
MIX = [(m * 0x100000001 & M64, n, p * 0x100000001 & M64, q) for m, n, p, q in QUADS] + [(1 << 63, 5, 1, 5), (1 << 32, 1, 0, 2)]
lex('m64n32', 'the firsts i64s and the seconds i32s: each compare in its own width', 1,
    body_of('lt_s', 'le_u', 'i64', 'i32'), test_of('lt_s', 'le_u', 64, 32), runs=MIX, tm='i64', tn='i32',
    shapes=[(r'\bsbcs\s+xzr', 1), (r'\bcmp\s+w', 1)], tag=300)
XIM = [(m, n * 0x100000001 & M64, p, q * 0x100000001 & M64) for m, n, p, q in QUADS] + [(5, 1 << 63, 5, 1), (5, 1 << 32, 5, 2)]
lex('m32n64', 'the firsts i32s and the seconds i64s', 1,
    body_of('gt_u', 'gt_u', 'i32', 'i64'), test_of('gt_u', 'gt_u', 32, 64), runs=XIM, tm='i32', tn='i64',
    shapes=[(r'\bsbcs\s+wzr', 1), (r'\bcmp\s+x', 1)], tag=400)

# ---- the seconds' sign -------------------------------------------------------

lex('nsigned', 'the seconds signed i32s that may be negative: no borrow says which is less, and the '
    'select stays', 0, body_of('lt_u', 'lt_s'), test_of('lt_u', 'lt_s'))
lex('npos', 'the seconds signed, each masked to 31 bits: never negative, the unsigned compare', 1,
    sel(op('i32.eq', M, P), op('i32.lt_s', '(i32.and %s (i32.const 0x7fffffff))' % N, '(i32.and %s (i32.const 0x7fffffff))' % Q),
        op('i32.lt_u', M, P)),
    lambda m, n, p, q: rel('lt_u', n & 0x7fffffff, q & 0x7fffffff) if m == p else rel('lt_u', m, p))

# ---- constants ---------------------------------------------------------------

CQ = QUADS + [(5, 99, 5, 0), (5, 100, 5, 0), (5, 101, 5, 0), (7, 0, 9, 0), (7, 0, 7, 0), (8, 0, 7, 0)]
lex('constn', 'the one second a constant, the compare\'s immediate', 1,
    sel(op('i32.eq', M, P), op('i32.lt_u', N, '(i32.const 100)'), op('i32.lt_u', M, P)),
    lambda m, n, p, q: rel('lt_u', n, 100) if m == p else rel('lt_u', m, p), runs=CQ,
    shapes=[(r'\bcmp\s+w\d+, #0x64', 1), (r'\bsbcs\b', 1)], tag=500)
lex('constnle', 'the same not strict: the constant is what the other is taken from, in a register', 1,
    sel(op('i32.eq', M, P), op('i32.le_u', N, '(i32.const 100)'), op('i32.lt_u', M, P)),
    lambda m, n, p, q: rel('le_u', n, 100) if m == p else rel('lt_u', m, p), runs=CQ,
    shapes=[(r'\bsbcs\b', 1)], tag=600)
lex('constneg', 'a negative constant, the immediate of a cmn', 1,
    sel(op('i32.eq', M, P), op('i32.lt_u', N, '(i32.const -5)'), op('i32.lt_u', M, P)),
    lambda m, n, p, q: rel('lt_u', n, -5 & M32) if m == p else rel('lt_u', m, p),
    runs=CQ + [(5, -5 & M32, 5, 0), (5, -6 & M32, 5, 0), (5, -4 & M32, 5, 0), (5, 3, 5, 0)],
    shapes=[(r'\bcmn\s+w\d+, #0x5', 1), (r'\bsbcs\b', 1)], tag=700)
lex('constm', 'the one first a constant: in a register for the subtract', 1,
    sel(op('i32.eq', M, '(i32.const 7)'), op('i32.lt_u', N, Q), op('i32.lt_s', M, '(i32.const 7)')),
    lambda m, n, p, q: rel('lt_u', n, q) if m == 7 else rel('lt_s', m, 7),
    runs=CQ + [(7, 1, 0, 2), (7, 2, 0, 1), (-9 & M32, 0, 0, 0)])

# ---- read more than once, or elsewhere ---------------------------------------

USE = '(local $c i32)\n    (local.set $c %s)\n    (i32.add (local.get $c) %s)'
lex('usesm', 'the firsts\' compare read once more: it is made, and the select of it stays', 0,
    USE % (op('i32.lt_u', M, P), sel(op('i32.eq', M, P), op('i32.lt_u', N, Q), '(local.get $c)')),
    lambda m, n, p, q: rel('lt_u', m, p) + (rel('lt_u', n, q) if m == p else rel('lt_u', m, p)))
lex('usesn', 'the seconds\' compare read once more', 0,
    USE % (op('i32.lt_u', N, Q), sel(op('i32.eq', M, P), '(local.get $c)', op('i32.lt_u', M, P))),
    lambda m, n, p, q: rel('lt_u', n, q) + (rel('lt_u', n, q) if m == p else rel('lt_u', m, p)))
lex('usesc', 'the test for equality read once more', 0,
    USE % (op('i32.eq', M, P), sel('(local.get $c)', op('i32.lt_u', N, Q), op('i32.lt_u', M, P))),
    lambda m, n, p, q: int(m == p) + (rel('lt_u', n, q) if m == p else rel('lt_u', m, p)))
lex('twice', 'the select itself read twice, as a value: one cmp and sbcs, and a cset', 1,
    '(local $v i32)\n    (local.set $v %s)\n    (i32.add (i32.shl (local.get $v) (i32.const 4)) (i32.xor (local.get $v) %s))' % (
        body_of('lt_u', 'ge_u'), M),
    lambda m, n, p, q: (lambda v: (v << 4) + (v ^ m))(rel('ge_u', n, q) if m == p else rel('lt_u', m, p)),
    shapes=[(r'\bsbcs\b', 1), (r'\bcset\b', 1), (r'\bcmp\b', 1)])
lex('apart', 'the three compares made ahead of an arm that stores and the select where the arm ends, in '
    'another block: they are values by then, and the select stays', 0,
    '(local $a i32) (local $b i32) (local $c i32)\n    (local.set $a %s) (local.set $b %s) (local.set $c %s)\n'
    '    (if (local.get $n) (then (i32.store (i32.const 512) (local.get $q))))\n'
    '    (select (local.get $b) (local.get $a) (local.get $c))' % (
        op('i32.lt_u', M, P), op('i32.lt_u', N, Q), op('i32.eq', M, P)),
    test_of('lt_u', 'lt_u'))

# ---- what reads it -----------------------------------------------------------

lex('pick', 'read by a select: a csel on the flags, no 0 or 1 made', 1,
    sel(body_of('gt_s', 'lt_u'), N, Q),
    lambda m, n, p, q: n if (rel('lt_u', n, q) if m == p else rel('gt_s', m, p)) else q,
    shapes=[(r'\bsbcs\b', 1), (r'\bcsel\b', 1), (r'\bcset\b', 0), (r'\bcmp\b', 1)])
lex('picks', 'read by two selects, one after the other: both on the same flags', 1,
    '(local $v i32)\n    (local.set $v %s)\n    (i32.sub (select %s %s (local.get $v)) (select %s %s (local.get $v)))' % (
        body_of('le_u', 'le_u'), N, Q, P, M),
    lambda m, n, p, q: (lambda v: ((n if v else q) - (p if v else m)) & M32)(rel('le_u', n, q) if m == p else rel('le_u', m, p)),
    shapes=[(r'\bsbcs\b', 1), (r'\bcsel\b', 2), (r'\bcset\b', 0), (r'\bcmp\b', 1)])
lex('branch', 'read by a branch: a b.hs on the flags', 1,
    '(if %s (then (i32.store (i32.const 512) (i32.const 1)) (return (i32.const 7))))\n'
    '    (i32.store (i32.const 516) (i32.const 2))\n    (i32.const 9)' % body_of('ge_u', 'ge_u'),
    lambda m, n, p, q: 7 if (rel('ge_u', n, q) if m == p else rel('ge_u', m, p)) else 9,
    shapes=[(r'\bsbcs\b', 1), (r'\bb\.hs\b', 1), (r'\bcset\b', 0), (r'\bcmp\b', 1)])
lex('andcmp', 'anded with another compare for a select: cmp, sbcs and a ccmp', 1,
    sel('(i32.and %s %s)' % (body_of('lt_u', 'lt_u'), op('i32.ne', N, P)), M, Q),
    lambda m, n, p, q: m if (rel('lt_u', n, q) if m == p else rel('lt_u', m, p)) and n != p else q,
    shapes=[(r'\bsbcs\b', 1), (r'\bccmp\b', 1), (r'\bcset\b', 0), (r'\bcsel\b', 1)])
lex('orcmp', 'ored with another compare for a branch', 1,
    '(if (i32.or %s %s) (then (i32.store (i32.const 512) (i32.const 1)) (return (i32.const 7))))\n'
    '    (i32.store (i32.const 516) (i32.const 2))\n    (i32.const 9)' % (body_of('lt_s', 'le_u'), op('i32.eq', N, P)),
    lambda m, n, p, q: 7 if (rel('le_u', n, q) if m == p else rel('lt_s', m, p)) or n == p else 9,
    shapes=[(r'\bsbcs\b', 1), (r'\bccmp\b', 1), (r'\bcset\b', 0)])

# ---- one value's bits are the fold's, and branches stay branches ---------------

A, B = '(local.get $a)', '(local.get $b)'
case('rot', 'the pairs each an i64\'s halves: the fold\'s one compare of the two rotated, no sbcs', '''
  (func $rot (export "rot") (param $a i64) (param $b i64) (result i32)
    %s)
''' % sel('(i32.eq (i32.wrap_i64 %s) (i32.wrap_i64 %s))' % (A, B),
          '(i64.lt_u (i64.shr_u %s (i64.const 32)) (i64.shr_u %s (i64.const 32)))' % (A, B),
          '(i32.lt_u (i32.wrap_i64 %s) (i32.wrap_i64 %s))' % (A, B)),
     lambda a, b: rel('lt_u', a >> 32, b >> 32) if a & M32 == b & M32 else rel('lt_u', a & M32, b & M32),
     [(ma | na << 32, mb | nb << 32) for ma, na, mb, nb in QUADS], 0, [(r'\bsbcs\b', 0), (r'\bror\b', 2)])
lex('brs', 'the test as three branches: they stay', 0, '''(block $f
      (block $t
        (if (i32.eq (local.get $m) (local.get $p))
          (then (br_if $t (i32.lt_u (local.get $n) (local.get $q))) (br $f))
          (else (br_if $t (i32.lt_u (local.get $m) (local.get $p))) (br $f))))
      (i32.store (i32.const 512) (i32.const 1))
      (return (i32.const 11)))
    (i32.store (i32.const 516) (i32.const 2))
    (i32.const 22)''', lambda m, n, p, q: 11 if (rel('lt_u', n, q) if m == p else rel('lt_u', m, p)) else 22)

# ---- records in memory: a sort's comparator ------------------------------------

RECS = [(5, 9), (5, 3), (2, 0xffffffff), (0x80000000, 1), (5, 9), (0, 0), (0xffffffff, 0xffffffff),
        (0x80000000, 0x80000000), (7, 0x7fffffff), (7, 0x80000000), (1, 2), (2, 1), (0x7fffffff, 5),
        (3, 3), (5, 4), (0xfffffffe, 0)]
MEM = b''.join(k.to_bytes(4, 'little') + i.to_bytes(4, 'little') for k, i in RECS)


def key_s(r): return (s32(r[0]), r[1])


case('recs', 'two records in memory, a signed key and an unsigned number each, loaded and compared', '''
  (func $recs (export "recs") (param $x i32) (param $y i32) (result i32)
    (local $p i32) (local $q i32)
    (local.set $p (i32.shl (local.get $x) (i32.const 3)))
    (local.set $q (i32.shl (local.get $y) (i32.const 3)))
    (select (i32.lt_u (i32.load offset=4 (local.get $p)) (i32.load offset=4 (local.get $q)))
            (i32.lt_s (i32.load (local.get $p)) (i32.load (local.get $q)))
            (i32.eq (i32.load (local.get $p)) (i32.load (local.get $q)))))
''', lambda x, y: int(key_s(RECS[x]) < key_s(RECS[y])),
     [(x, y) for x in range(16) for y in (0, 3, 7, 9, 15)], 1, [(r'\bsbcs\b', 1), (r'\bcset\b', 1)])


def sort_model(cnt):
    r = sorted(RECS[:cnt], key=key_s)
    s = 0
    for k, i in r:
        s = (s * 31 + k + 7 * i) & M32
    return s


case('sortrec', 'an insertion sort of the records in a copy, by that order: the comparator in its inner '
     'loop is a cmp, an sbcs and a branch', '''
  (func $sortrec (export "sortrec") (param $cnt i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32) (local $k i32) (local $id i32) (local $pk i32) (local $pid i32)
    (memory.copy (i32.const 2048) (i32.const 0) (i32.shl (local.get $cnt) (i32.const 3)))
    (local.set $i (i32.const 1))
    (block $done
      (loop $outer
        (br_if $done (i32.ge_u (local.get $i) (local.get $cnt)))
        (local.set $k (i32.load (i32.add (i32.const 2048) (i32.shl (local.get $i) (i32.const 3)))))
        (local.set $id (i32.load (i32.add (i32.const 2052) (i32.shl (local.get $i) (i32.const 3)))))
        (local.set $j (local.get $i))
        (block $placed
          (loop $inner
            (br_if $placed (i32.eqz (local.get $j)))
            (local.set $pk (i32.load (i32.add (i32.const 2040) (i32.shl (local.get $j) (i32.const 3)))))
            (local.set $pid (i32.load (i32.add (i32.const 2044) (i32.shl (local.get $j) (i32.const 3)))))
            (br_if $placed (select (i32.ge_u (local.get $id) (local.get $pid))
                                   (i32.ge_s (local.get $k) (local.get $pk))
                                   (i32.eq (local.get $k) (local.get $pk))))
            (i32.store (i32.add (i32.const 2048) (i32.shl (local.get $j) (i32.const 3))) (local.get $pk))
            (i32.store (i32.add (i32.const 2052) (i32.shl (local.get $j) (i32.const 3))) (local.get $pid))
            (local.set $j (i32.sub (local.get $j) (i32.const 1)))
            (br $inner)))
        (i32.store (i32.add (i32.const 2048) (i32.shl (local.get $j) (i32.const 3))) (local.get $k))
        (i32.store (i32.add (i32.const 2052) (i32.shl (local.get $j) (i32.const 3))) (local.get $id))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $outer)))
    (local.set $i (i32.const 0))
    (block $summed
      (loop $sum
        (br_if $summed (i32.ge_u (local.get $i) (local.get $cnt)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
          (i32.add (i32.load (i32.add (i32.const 2048) (i32.shl (local.get $i) (i32.const 3))))
                   (i32.mul (i32.load (i32.add (i32.const 2052) (i32.shl (local.get $i) (i32.const 3)))) (i32.const 7)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $sum)))
    (local.get $s))
''', sort_model, [(0,), (1,), (2,), (5,), (9,), (16,)], 1, [(r'\bsbcs\b', 1, 'loop'), (r'\bcset\b', 0, 'loop')])

HEAD = """;; Two pairs compared by their first members and, where those are equal, by
;; their second -- `a.m == b.m ? a.n < b.n : a.m < b.m`, a select of two
;; compares on a third -- where neither pair is the bits of one value: on
;; arm64 a compare of the seconds and a subtract with carry of the firsts,
;; whose flags are the whole difference's (isel.c find_cmp_chains' cch_lex,
;; isel_ops.inc emit_lex).  From test/gen_lex_sbc.py, which works out every
;; answer from the test as it is written.
;;
"""


def render(names=None):
    funcs, shapes, runs, lines = [], [], [], []
    tot = 0
    for name, what, wat, model, rs, want, shp, ty in CASES:
        if names and name not in names:
            continue
        funcs.append(wat.strip('\n') + '\n')
        tot += want
        l = ';;   %-10s %s' % (name, what)
        while len(l) > 78:
            cut = l.rfind(' ', 0, 78)
            lines.append(l[:cut])
            l = ';;' + ' ' * 13 + l[cut + 1:]
        lines.append(l)
        for sh in shp:
            shapes.append(';; %s: %s %s=%d' % (sh[2] if len(sh) > 2 else 'shape', name, sh[0], sh[1]))
        for a in rs:
            r = model(*a)
            runs.append(';; run: %s %s => %s:%d' % (name, ' '.join(str(x) for x in a), ty, s64(r) if ty == 'i64' else s32(r)))
    data = ''.join('\\%02x' % b for b in MEM)
    # (andcmp and orcmp each a chain of a compare behind the two pairs', and sortrec's loops two of
    # their own tests: those are counted as chains of compares, the pairs alone are not)
    ccmp = sum(k for n, k in (('andcmp', 1), ('orcmp', 1), ('sortrec', 2)) if not names or n in names)
    return (HEAD + '\n'.join(lines) + '\n;;\n;; stats -O2 a64: lex_sbcs=%d ccmp_chains=%d\n' % (tot, ccmp) +
            ('\n'.join(shapes) + '\n' if shapes else '') +
            ';;\n' + '\n'.join(runs) + '\n\n(module\n  (type $t1 (func (param i32)))\n  (memory 1)\n  (table 1 funcref)\n'
            '  (elem (i32.const 0) $thrower)\n  (tag $e)\n  (data (i32.const 0) "%s")\n' % data +
            '  ;; called through the table at an index read from memory, which keeps it out of line\n'
            '  (func $thrower (type $t1) (if (i32.eq (local.get 0) (i32.const 0x7fffff03)) (then (throw $e))))\n' +
            ''.join(funcs) + ')\n')


def each(names):
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    import gen_gosp
    target = gen_gosp.target_flags(os.path.join(root, 'build'))
    bad = 0
    for name, what, wat, model, rs, want, shp, ty in CASES:
        if names and name not in names:
            continue
        with tempfile.TemporaryDirectory() as d:
            w = os.path.join(d, 'one.wat')
            open(w, 'w').write(render([name]))
            r = subprocess.run([os.path.join(root, 'build', 'warmc'), '-O2'] + target +
                               ['--stats', '-o', os.path.join(d, 'one'), w], capture_output=True, text=True)
            got = sum(int(x) for x in re.findall(r'\blex_sbcs=(\d+)', r.stdout + r.stderr))
        mark = '' if got == want else '   <-- expected %d' % want
        if r.returncode:
            mark = '   <-- does not compile: ' + (r.stderr.strip().split('\n') or [''])[-1]
        bad += bool(mark)
        print('%-12s %d%s' % (name, got, mark))
    return bad


if __name__ == '__main__':
    if sys.argv[1] == '--each':
        sys.exit(1 if each(sys.argv[2:]) else 0)
    open(sys.argv[1], 'w').write(render())
    print('%s: %d functions' % (sys.argv[1], len(CASES)))
