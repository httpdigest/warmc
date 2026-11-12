"""test/cases_opt/lex_cmp.wat: two pairs compared by their first members and,
where those are equal, by their second, are one compare of two keys (opt.c
lx_match, fold_lex_select, lx_branch).  The module's header says what each
function is; every answer is worked out here on a model of the function in
Python, which is the test as the source has it, not the keys' compare.

  python3 test/gen_lex_cmp.py test/cases_opt/lex_cmp.wat
  python3 test/gen_lex_cmp.py --each [NAME ...]

--each compiles every function (or the named ones) alone with build/warmc
and prints what --stats counts for it as lex_cmps and lex_brs beside what
is expected.
"""
import os, re, subprocess, sys, tempfile

M32 = (1 << 32) - 1
M64 = (1 << 64) - 1


def s32(v):
    v &= M32
    return v - (1 << 32) if v >> 31 else v


def s64(v):
    v &= M64
    return v - (1 << 64) if v >> 63 else v


# the memory's first 16 doublewords: pairs, the first member the low half
PAIRS = [(5, 9), (5, 3), (2, 0xffffffff), (0x80000000, 1), (5, 9), (0, 0), (0xffffffff, 0xffffffff),
         (0x80000000, 0x80000000), (7, 0x7fffffff), (7, 0x80000000), (1, 2), (2, 1), (0x7fffffff, 5),
         (3, 3), (5, 4), (0xfffffffe, 0)]
MEM = b''.join((m | n << 32).to_bytes(8, 'little') for m, n in PAIRS)

CASES = []


def case(name, what, wat, model, runs, cmps, brs, shapes=(), ty='i32'):
    CASES.append((name, what, wat, model, runs, (cmps, brs), shapes, ty))


# ---- the pieces ------------------------------------------------------------

A, B = '(local.get $a)', '(local.get $b)'


def lo(x): return '(i32.wrap_i64 %s)' % x
def hi(x): return '(i64.shr_u %s (i64.const 32))' % x
def hi32(x): return '(i32.wrap_i64 (i64.shr_u %s (i64.const 32)))' % x
def sel(c, t, e): return '(select %s %s %s)' % (t, e, c)
def op(o, x, y): return '(%s %s %s)' % (o, x, y)


def plo(x): return x & M32
def phi(x): return (x >> 32) & M32


def rel(o, x, y, bits=32):
    """a wasm compare by its name's last two parts: lt_u, ge_s ..."""
    if o.endswith('_s'):
        m = 1 << (bits - 1)
        x, y = (x ^ m) - m, (y ^ m) - m
    return int({'lt': x < y, 'le': x <= y, 'gt': x > y, 'ge': x >= y, 'eq': x == y, 'ne': x != y}[o.split('.')[-1][:2]])


# pairs of halves (m_a, n_a, m_b, n_b) that tell the orders apart: equal
# ones, the members pulling opposite ways, the sign bit in either member
QUADS = [(1, 1, 1, 1), (1, 5, 2, 3), (2, 3, 1, 5), (1, 3, 1, 5), (1, 5, 1, 3),
         (0x80000000, 0, 1, 0), (1, 0, 0x80000000, 0), (7, 0x80000000, 7, 1), (7, 1, 7, 0x80000000),
         (M32, M32, M32, M32), (0, M32, 1, 0), (1, 0, 0, M32),
         (0x80000000, 0x80000000, 0x80000000, 0x7fffffff), (0x12345678, 0x9abcdef0, 0x12345678, 0x9abcdef0),
         (0x12345678, 0x9abcdef0, 0x9abcdef0, 0x12345678), (0x9abcdef0, 0x12345678, 0x12345678, 0x9abcdef0)]
# the first member the low half of an i64, and the high half
LOW = [(ma | na << 32, mb | nb << 32) for ma, na, mb, nb in QUADS]
HIGH = [(na | ma << 32, nb | mb << 32) for ma, na, mb, nb in QUADS]

FN2 = '''
  (func $%(n)s (export "%(n)s") (param $a %(t)s) (param $b %(t)s) (result i32)
    %(body)s)
'''


def sel2(name, what, cmps, body, model, runs=LOW, t='i64', shapes=(), tag=0):
    # (tag: a constant added to the answer, which keeps a function whose code is counted from being
    # merged with another that comes to the same code)
    if tag:
        body = '(i32.add %s (i32.const %d))' % (body, tag)
        test = model
        model = lambda a, b: test(a, b) + tag
    case(name, what, FN2 % dict(n=name, t=t, body=body), model, runs, cmps, 0, shapes)


# ---- a select of two compares on a third -----------------------------------

sel2('rot', 'the first members the low halves of two i64s, the second the high: the test Zig\'s priority '
     'queue makes of two entries, as one compare of the two rotated', 1,
     sel(op('i32.eq', lo(A), lo(B)), op('i64.lt_u', hi(A), hi(B)), op('i32.lt_u', lo(A), lo(B))),
     lambda a, b: rel('lt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('lt_u', plo(a), plo(b)),
     shapes=[(r'\bror\b', 2), (r'\bcsel\b', 0), (r'\bcmp\b', 1)], tag=100)
sel2('rotne', 'the same on a ne, its arms the other way round', 1,
     sel(op('i32.ne', lo(A), lo(B)), op('i32.lt_u', lo(A), lo(B)), op('i64.lt_u', hi(A), hi(B))),
     lambda a, b: rel('lt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('lt_u', plo(a), plo(b)))
sel2('rotge', 'greater or equal, both compares: the keys\' the other way round and not strict', 1,
     sel(op('i32.eq', lo(A), lo(B)), op('i64.ge_u', hi(A), hi(B)), op('i32.ge_u', lo(A), lo(B))),
     lambda a, b: rel('ge_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('ge_u', plo(a), plo(b)))
sel2('strict', 'the firsts\' compare not strict and the seconds\' strict: as strict as the seconds\'', 1,
     sel(op('i32.eq', lo(A), lo(B)), op('i64.gt_u', hi(A), hi(B)), op('i32.ge_u', lo(A), lo(B))),
     lambda a, b: rel('gt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('ge_u', plo(a), plo(b)))
sel2('loose', 'the firsts\' strict and the seconds\' not', 1,
     sel(op('i32.eq', lo(A), lo(B)), op('i64.le_u', hi(A), hi(B)), op('i32.lt_u', lo(A), lo(B))),
     lambda a, b: rel('le_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('lt_u', plo(a), plo(b)))
sel2('swapped', 'the firsts\' compare with its operands the other way round from the test of whether they '
     'are equal', 1,
     sel(op('i32.eq', lo(A), lo(B)), op('i64.lt_u', hi(A), hi(B)), op('i32.gt_u', lo(B), lo(A))),
     lambda a, b: rel('lt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('gt_u', plo(b), plo(a)))
sel2('hi32', 'the second members as i32s, the high halves wrapped', 1,
     sel(op('i32.eq', lo(A), lo(B)), op('i32.lt_u', hi32(A), hi32(B)), op('i32.lt_u', lo(A), lo(B))),
     lambda a, b: rel('lt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('lt_u', plo(a), plo(b)))
sel2('masked', 'the first members as i64s, the low halves masked', 1,
     sel(op('i64.eq', '(i64.and %s (i64.const 0xffffffff))' % A, '(i64.and %s (i64.const 0xffffffff))' % B),
         op('i64.lt_u', hi(A), hi(B)),
         op('i64.lt_u', '(i64.and %s (i64.const 0xffffffff))' % A, '(i64.and %s (i64.const 0xffffffff))' % B)),
     lambda a, b: rel('lt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('lt_u', plo(a), plo(b)))
sel2('cross', 'the seconds\' compare the other way from the firsts\': each key would be one value\'s first '
     'member and the other\'s second, which no rotation is, and the test stays', 0,
     sel(op('i32.eq', lo(A), lo(B)), op('i64.gt_u', hi(A), hi(B)), op('i32.lt_u', lo(A), lo(B))),
     lambda a, b: rel('gt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('lt_u', plo(a), plo(b)))
sel2('same', 'the first members the high halves: the compare of the two values themselves, no rotation', 1,
     sel(op('i64.eq', hi(A), hi(B)), op('i32.lt_u', lo(A), lo(B)), op('i64.lt_u', hi(A), hi(B))),
     lambda a, b: rel('lt_u', plo(a), plo(b)) if phi(a) == phi(b) else rel('lt_u', phi(a), phi(b)),
     runs=HIGH, shapes=[(r'\bror\b', 0), (r'\bcmp\b', 1)])
sel2('samesg', 'the first members the high halves as signed i32s: the values\' signed compare', 1,
     sel(op('i32.eq', hi32(A), hi32(B)), op('i32.le_u', lo(A), lo(B)), op('i32.lt_s', hi32(A), hi32(B))),
     lambda a, b: rel('le_u', plo(a), plo(b)) if phi(a) == phi(b) else rel('lt_s', phi(a), phi(b)),
     runs=HIGH, shapes=[(r'\bror\b', 0)])
sel2('rotsg', 'the first members the low halves, signed: the rotated values\' signed compare', 1,
     sel(op('i32.eq', lo(A), lo(B)), op('i64.ge_u', hi(A), hi(B)), op('i32.gt_s', lo(A), lo(B))),
     lambda a, b: rel('ge_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('gt_s', plo(a), plo(b)))
sel2('nsigned', 'the second members signed i32s that may be negative: no unsigned compare of the keys\' '
     'low bits is theirs, and the test stays', 0,
     sel(op('i32.eq', lo(A), lo(B)), op('i32.lt_s', hi32(A), hi32(B)), op('i32.lt_u', lo(A), lo(B))),
     lambda a, b: rel('lt_s', phi(a), phi(b)) if plo(a) == plo(b) else rel('lt_u', plo(a), plo(b)))
sel2('npos', 'the second members compared signed as i64s, which a high half is never negative as', 1,
     sel(op('i32.eq', lo(A), lo(B)), op('i64.lt_s', hi(A), hi(B)), op('i32.lt_u', lo(A), lo(B))),
     lambda a, b: rel('lt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('lt_u', plo(a), plo(b)))
sel2('mpos', 'the first members compared signed, and neither negative, 31 bits each: the unsigned compare', 1,
     sel(op('i64.eq', '(i64.shr_u %s (i64.const 33))' % A, '(i64.shr_u %s (i64.const 33))' % B),
         op('i64.lt_u', '(i64.and %s (i64.const 0x1ffffffff))' % A, '(i64.and %s (i64.const 0x1ffffffff))' % B),
         op('i64.lt_s', '(i64.shr_u %s (i64.const 33))' % A, '(i64.shr_u %s (i64.const 33))' % B)),
     lambda a, b: int(a < b), runs=HIGH + [(1 << 63, 1), (1, 1 << 63), (1 << 33, (1 << 33) - 1)],
     shapes=[(r'\bror\b', 0)], tag=200)
sel2('uses', 'the firsts\' compare read once more, by the result: it stays, and so do the others', 0,
     '(local $c i32)\n    (local.set $c %s)\n    (i32.add (local.get $c) %s)' % (
         op('i32.lt_u', lo(A), lo(B)),
         sel(op('i32.eq', lo(A), lo(B)), op('i64.lt_u', hi(A), hi(B)), '(local.get $c)')),
     lambda a, b: rel('lt_u', plo(a), plo(b)) + (rel('lt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('lt_u', plo(a), plo(b))))
sel2('other', 'the firsts\' compare of one of them and a third value: no compare of what is tested for '
     'equality', 0,
     sel(op('i32.eq', lo(A), lo(B)), op('i64.lt_u', hi(A), hi(B)), op('i32.lt_u', lo(A), hi32(B))),
     lambda a, b: rel('lt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('lt_u', plo(a), phi(b)))
sel2('eqarm', 'an arm that is no less or greater but another test for equality', 0,
     sel(op('i32.eq', lo(A), lo(B)), op('i64.eq', hi(A), hi(B)), op('i32.lt_u', lo(A), lo(B))),
     lambda a, b: int(phi(a) == phi(b)) if plo(a) == plo(b) else rel('lt_u', plo(a), plo(b)))

# i32s in two parts
Q16 = [(ma & 0xffff, na & 0xffff, mb & 0xffff, nb & 0xffff) for ma, na, mb, nb in QUADS] + [
    (0x8000, 0, 1, 0), (1, 0, 0x8000, 0), (7, 0x8000, 7, 1), (7, 1, 7, 0x8000)]
LOW16 = [(ma | na << 16, mb | nb << 16) for ma, na, mb, nb in Q16]
A16 = '(i32.and %s (i32.const 0xffff))' % A
B16 = '(i32.and %s (i32.const 0xffff))' % B
sel2('half16', 'an i32\'s low 16 bits first and its high 16 second: the two rotated by 16, compared as i32s', 1,
     sel(op('i32.eq', A16, B16), op('i32.lt_u', '(i32.shr_u %s (i32.const 16))' % A, '(i32.shr_u %s (i32.const 16))' % B),
         op('i32.lt_u', A16, B16)),
     lambda a, b: rel('lt_u', a >> 16, b >> 16) if a & 0xffff == b & 0xffff else rel('lt_u', a & 0xffff, b & 0xffff),
     runs=LOW16, t='i32', shapes=[(r'\bror\s+w', 2)])
sel2('part8', 'the low 8 bits first and the other 24 second: rotated by 8', 1,
     sel(op('i32.eq', '(i32.and %s (i32.const 255))' % A, '(i32.and %s (i32.const 255))' % B),
         op('i32.le_u', '(i32.shr_u %s (i32.const 8))' % A, '(i32.shr_u %s (i32.const 8))' % B),
         op('i32.lt_u', '(i32.and %s (i32.const 255))' % A, '(i32.and %s (i32.const 255))' % B)),
     lambda a, b: rel('le_u', a >> 8, b >> 8) if a & 255 == b & 255 else rel('lt_u', a & 255, b & 255),
     runs=LOW16 + [(0x100, 0x1ff), (0x1ff, 0x100), (0xff000001, 0x00ffff01), (0x00ffff01, 0xff000001)], t='i32')
sel2('gap', 'the low 8 bits first and the high 16 second, the 8 between in neither: no rotation has '
     'just those, and the test stays', 0,
     sel(op('i32.eq', '(i32.and %s (i32.const 255))' % A, '(i32.and %s (i32.const 255))' % B),
         op('i32.lt_u', '(i32.shr_u %s (i32.const 16))' % A, '(i32.shr_u %s (i32.const 16))' % B),
         op('i32.lt_u', '(i32.and %s (i32.const 255))' % A, '(i32.and %s (i32.const 255))' % B)),
     lambda a, b: rel('lt_u', a >> 16, b >> 16) if a & 255 == b & 255 else rel('lt_u', a & 255, b & 255),
     runs=LOW16 + [(0x10100, 0x1ff00), (0x1ff00, 0x10100)], t='i32')
sel2('overlap', 'the low 16 bits first and all above the low 8 second, 8 bits in both: the test stays', 0,
     sel(op('i32.eq', A16, B16),
         op('i32.lt_u', '(i32.shr_u %s (i32.const 8))' % A, '(i32.shr_u %s (i32.const 8))' % B),
         op('i32.lt_u', A16, B16)),
     lambda a, b: rel('lt_u', a >> 8, b >> 8) if a & 0xffff == b & 0xffff else rel('lt_u', a & 0xffff, b & 0xffff),
     runs=LOW16, t='i32')
sel2('notmask', 'the first members an and with a constant that is no mask of low bits', 0,
     sel(op('i32.eq', '(i32.and %s (i32.const 0xfffe))' % A, '(i32.and %s (i32.const 0xfffe))' % B),
         op('i32.lt_u', '(i32.shr_u %s (i32.const 16))' % A, '(i32.shr_u %s (i32.const 16))' % B),
         op('i32.lt_u', '(i32.and %s (i32.const 0xfffe))' % A, '(i32.and %s (i32.const 0xfffe))' % B)),
     lambda a, b: rel('lt_u', a >> 16, b >> 16) if a & 0xfffe == b & 0xfffe else rel('lt_u', a & 0xfffe, b & 0xfffe),
     runs=LOW16 + [(0x10001, 0x20000), (0x20000, 0x10001)], t='i32')
sel2('shrs', 'the second members an arithmetic shift, the sign in all the bits above: not the bits alone', 0,
     sel(op('i32.eq', A16, B16),
         op('i32.lt_u', '(i32.shr_s %s (i32.const 16))' % A, '(i32.shr_s %s (i32.const 16))' % B),
         op('i32.lt_u', A16, B16)),
     lambda a, b: rel('lt_u', (s32(a) >> 16) & M32, (s32(b) >> 16) & M32) if a & 0xffff == b & 0xffff else rel('lt_u', a & 0xffff, b & 0xffff),
     runs=LOW16, t='i32')

def sh(x, k): return '(i32.shr_u %s (i32.const %d))' % (x, k)
def mk(x, k): return '(i32.and %s (i32.const %d))' % (x, k)
def ext(x): return '(i64.extend_i32_u %s)' % x


sel2('ext16', 'the two 16-bit parts widened and compared as i64s, which are the i32s\' compares: the i32 rotated', 1,
     sel(op('i64.eq', ext(A16), ext(B16)), op('i64.lt_u', ext(sh(A, 16)), ext(sh(B, 16))), op('i64.lt_u', ext(A16), ext(B16))),
     lambda a, b: rel('lt_u', a >> 16, b >> 16) if a & 0xffff == b & 0xffff else rel('lt_u', a & 0xffff, b & 0xffff),
     runs=LOW16, t='i32', tag=300)
sel2('shmod', 'the high part shifted down by 48, which an i32 takes as 16', 1,
     sel(op('i32.eq', A16, B16), op('i32.lt_u', sh(A, 48), sh(B, 48)), op('i32.lt_u', A16, B16)),
     lambda a, b: rel('lt_u', a >> 16, b >> 16) if a & 0xffff == b & 0xffff else rel('lt_u', a & 0xffff, b & 0xffff),
     runs=LOW16, t='i32', tag=400)
PARTS = LOW16 + [(0x100, 0x1ff), (0x1ff, 0x100), (0xff000001, 0x00ffff01), (0x00ffff01, 0xff000001),
                 (0x12345678, 0x12345679), (0x12345678, 0x12355678), (0x12345678, 0x13345678), (0x1234ff78, 0x123400ff)]


def parts(name, what, m, n):
    """a.m == b.m ? a.n < b.n : a.m < b.m of an i32's parts m and n, each (wat of x, value of x)"""
    sel2(name, what, 0, sel(op('i32.eq', m[0](A), m[0](B)), op('i32.lt_u', n[0](A), n[0](B)), op('i32.lt_u', m[0](A), m[0](B))),
         lambda a, b: rel('lt_u', n[1](a), n[1](b)) if m[1](a) == m[1](b) else rel('lt_u', m[1](a), m[1](b)),
         runs=PARTS, t='i32')


parts('upper', 'the bits from 8 up first and those from 24 up second: the first do not start at the '
      'bottom, nor the second', (lambda x: sh(x, 8), lambda v: v >> 8), (lambda x: sh(x, 24), lambda v: v >> 24))
parts('short', 'the low 8 bits first and the next 8 second, which do not reach the top',
      (lambda x: mk(x, 255), lambda v: v & 255), (lambda x: mk(sh(x, 8), 255), lambda v: v >> 8 & 255))
parts('nshift', 'the high 16 bits first and 16 from bit 8 up second: the second do not start at the bottom',
      (lambda x: sh(x, 16), lambda v: v >> 16), (lambda x: mk(sh(x, 8), 0xffff), lambda v: v >> 8 & 0xffff))
parts('gaphi', 'the high 16 bits first and the low 8 second, the 8 between in neither',
      (lambda x: sh(x, 16), lambda v: v >> 16), (lambda x: mk(x, 255), lambda v: v & 255))
parts('shorthi', 'the low 8 bits second and the next 8 first, which do not reach the top',
      (lambda x: mk(sh(x, 8), 255), lambda v: v >> 8 & 255), (lambda x: mk(x, 255), lambda v: v & 255))
sel2('sgle', 'the firsts signed and not strict, the seconds not strict: signed and not strict', 1,
     sel(op('i32.eq', lo(A), lo(B)), op('i64.le_u', hi(A), hi(B)), op('i32.le_s', lo(A), lo(B))),
     lambda a, b: rel('le_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('le_s', plo(a), plo(b)))
sel2('sgge', 'the firsts signed, greater or equal, the seconds greater', 1,
     sel(op('i32.eq', lo(A), lo(B)), op('i64.gt_u', hi(A), hi(B)), op('i32.ge_s', lo(A), lo(B))),
     lambda a, b: rel('gt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('ge_s', plo(a), plo(b)))
sel2('usescond', 'the test of whether the firsts are equal read once more', 0,
     '(local $c i32)\n    (local.set $c %s)\n    (i32.add (local.get $c) %s)' % (
         op('i32.eq', lo(A), lo(B)),
         sel('(local.get $c)', op('i64.lt_u', hi(A), hi(B)), op('i32.lt_u', lo(A), lo(B)))),
     lambda a, b: int(plo(a) == plo(b)) + (rel('lt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('lt_u', plo(a), plo(b))))
sel2('usesn', 'the seconds\' compare read once more', 0,
     '(local $c i32)\n    (local.set $c %s)\n    (i32.add (local.get $c) %s)' % (
         op('i64.lt_u', hi(A), hi(B)),
         sel(op('i32.eq', lo(A), lo(B)), '(local.get $c)', op('i32.lt_u', lo(A), lo(B)))),
     lambda a, b: rel('lt_u', phi(a), phi(b)) + (rel('lt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('lt_u', plo(a), plo(b))))

# a pair that is no one value's bits, beside one that is
FN4 = '''
  (func $%(n)s (export "%(n)s") (param $a i64) (param $m %(tm)s) (param $n %(tn)s) (result i32)
    %(body)s)
'''
MV, NV = '(local.get $m)', '(local.get $n)'
GEN = [(ma | na << 32, mb, nb) for ma, na, mb, nb in QUADS]


def sel3(name, what, cmps, body, model, runs=GEN, tm='i32', tn='i32', shapes=(), tag=0):
    if tag:
        body = '(i32.add %s (i32.const %d))' % (body, tag)
        test = model
        model = lambda a, m, n: test(a, m, n) + tag
    case(name, what, FN4 % dict(n=name, tm=tm, tn=tn, body=body), model, runs, cmps, 0, shapes)


sel3('gen', 'one pair an i64\'s halves, the other two i32s: their key put together with a shift and an or', 1,
     sel(op('i32.eq', lo(A), MV), op('i32.lt_u', hi32(A), NV), op('i32.lt_u', lo(A), MV)),
     lambda a, m, n: rel('lt_u', phi(a), n) if plo(a) == m else rel('lt_u', plo(a), m),
     shapes=[(r'\bror\b', 1), (r'\borr\b', 1)], tag=500)
sel3('gensg', 'the same with the first members signed', 1,
     sel(op('i32.eq', lo(A), MV), op('i32.gt_u', hi32(A), NV), op('i32.gt_s', lo(A), MV)),
     lambda a, m, n: rel('gt_u', phi(a), n) if plo(a) == m else rel('gt_s', plo(a), m))
sel3('genwide', 'the other pair\'s second member an i64 that may have more than 32 bits: it has no place '
     'in the key, and the test stays', 0,
     sel(op('i32.eq', lo(A), MV), op('i64.lt_u', hi(A), NV), op('i32.lt_u', lo(A), MV)),
     lambda a, m, n: rel('lt_u', phi(a), n, 64) if plo(a) == m else rel('lt_u', plo(a), m),
     runs=[(a, m, n) for a, m, n in GEN] + [(5 | 3 << 32, 5, 1 << 32), (5 | 3 << 32, 5, (1 << 32) + 2)], tn='i64')
sel3('genfits', 'that i64 masked to 32 bits: it has', 1,
     sel(op('i32.eq', lo(A), MV), op('i64.lt_u', hi(A), '(i64.and %s (i64.const 0xffffffff))' % NV), op('i32.lt_u', lo(A), MV)),
     lambda a, m, n: rel('lt_u', phi(a), n & M32) if plo(a) == m else rel('lt_u', plo(a), m),
     runs=[(a, m, n) for a, m, n in GEN] + [(5 | 3 << 32, 5, 1 << 32), (5 | 3 << 32, 5, (1 << 32) + 2)], tn='i64')
sel3('genmwide', 'the other pair\'s first member an i64 that may have more than 32 bits, the firsts '
     'compared as i64s: the test stays', 0,
     sel(op('i64.eq', '(i64.and %s (i64.const 0xffffffff))' % A, MV), op('i32.lt_u', hi32(A), NV),
         op('i64.lt_u', '(i64.and %s (i64.const 0xffffffff))' % A, MV)),
     lambda a, m, n: rel('lt_u', phi(a), n) if plo(a) == m else rel('lt_u', plo(a), m, 64),
     runs=[(a, m, n) for a, m, n in GEN] + [(5 | 3 << 32, (1 << 32) + 5, 9), (5 | 3 << 32, 1 << 40, 0)], tm='i64')
sel3('genconst', 'the other pair\'s second member a constant', 1,
     sel(op('i32.eq', lo(A), MV), op('i32.lt_u', hi32(A), '(i32.const 3)'), op('i32.lt_u', lo(A), MV)),
     lambda a, m, n: rel('lt_u', phi(a), 3) if plo(a) == m else rel('lt_u', plo(a), m))
sel3('both', 'two constants: the key a constant', 1,
     sel(op('i32.eq', lo(A), '(i32.const 5)'), op('i32.le_u', hi32(A), '(i32.const 0x80000003)'),
         op('i32.lt_u', lo(A), '(i32.const 5)')),
     lambda a, m, n: rel('le_u', phi(a), 0x80000003) if plo(a) == 5 else rel('lt_u', plo(a), 5),
     runs=[(a, 0, 0) for a, m, n in GEN] + [(5 | 0x80000003 << 32, 0, 0), (5 | 0x80000004 << 32, 0, 0), (5 | 3 << 32, 0, 0)],
     shapes=[(r'\bror\b', 1)])
sel3('bothsg', 'two constants beside a signed first member, the constant negative', 1,
     sel(op('i32.eq', lo(A), '(i32.const -5)'), op('i32.lt_u', hi32(A), '(i32.const 9)'),
         op('i32.lt_s', lo(A), '(i32.const -5)')),
     lambda a, m, n: rel('lt_u', phi(a), 9) if plo(a) == (-5 & M32) else rel('lt_s', plo(a), -5 & M32),
     runs=[(a, 0, 0) for a, m, n in GEN] + [((-5 & M32) | 8 << 32, 0, 0), ((-5 & M32) | 9 << 32, 0, 0),
                                            ((-6 & M32) | 99 << 32, 0, 0), ((-4 & M32), 0, 0)])
sel3('mixed', 'the other pair an i32\'s two 16-bit parts, whose own key would be 32 bits: put together '
     'as the i64\'s is laid out', 1,
     sel(op('i32.eq', lo(A), '(i32.and %s (i32.const 0xffff))' % MV),
         op('i32.lt_u', hi32(A), '(i32.shr_u %s (i32.const 16))' % MV),
         op('i32.lt_u', lo(A), '(i32.and %s (i32.const 0xffff))' % MV)),
     lambda a, m, n: rel('lt_u', phi(a), m >> 16) if plo(a) == m & 0xffff else rel('lt_u', plo(a), m & 0xffff),
     runs=[(ma | na << 32, (mb & 0xffff) | (nb & 0xffff) << 16, 0) for ma, na, mb, nb in Q16])
sel3('mixedrev', 'the same with the i32\'s parts the lesser side: laid out as the i64\'s all the same, '
     'the one layout both fit', 1,
     sel(op('i32.eq', lo(A), '(i32.and %s (i32.const 0xffff))' % MV),
         op('i32.gt_u', hi32(A), '(i32.shr_u %s (i32.const 16))' % MV),
         op('i32.gt_u', lo(A), '(i32.and %s (i32.const 0xffff))' % MV)),
     lambda a, m, n: rel('gt_u', phi(a), m >> 16) if plo(a) == m & 0xffff else rel('gt_u', plo(a), m & 0xffff),
     runs=[(ma | na << 32, (mb & 0xffff) | (nb & 0xffff) << 16, 0) for ma, na, mb, nb in Q16],
     shapes=[(r'\bror\b', 1)], tag=600)
FN32 = '''
  (func $%(n)s (export "%(n)s") (param $a i32) (param $m %(tm)s) (param $n %(tn)s) (result i32)
    %(body)s)
'''
FA32 = '(local.get $a)'
G16 = [(ma | na << 16, mb, nb) for ma, na, mb, nb in Q16]
case('w32m', 'an i32\'s two parts, the first widened and compared with an i64: the two are no one '
     'value\'s bits as they are compared, and the test stays', FN32 % dict(n='w32m', tm='i64', tn='i32', body=sel(
         op('i64.eq', ext(A16), '(i64.and %s (i64.const 0xffff))' % MV),
         op('i32.lt_u', sh(A, 16), mk(NV, 0xffff)),
         op('i64.lt_u', ext(A16), '(i64.and %s (i64.const 0xffff))' % MV))),
     lambda a, m, n: rel('lt_u', a >> 16, n & 0xffff) if a & 0xffff == m & 0xffff else rel('lt_u', a & 0xffff, m & 0xffff),
     [(a, m | 7 << 16 | 9 << 40, n | 5 << 16) for a, m, n in G16], 0, 0)
case('w32n', 'or the second widened', FN32 % dict(n='w32n', tm='i32', tn='i64', body=sel(
         op('i32.eq', A16, mk(MV, 0xffff)),
         op('i64.lt_u', ext(sh(A, 16)), '(i64.and %s (i64.const 0xffff))' % NV),
         op('i32.lt_u', A16, mk(MV, 0xffff)))),
     lambda a, m, n: rel('lt_u', a >> 16, n & 0xffff) if a & 0xffff == m & 0xffff else rel('lt_u', a & 0xffff, m & 0xffff),
     [(a, m | 7 << 16, n | 5 << 16 | 9 << 40) for a, m, n in G16], 0, 0)
case('same8', 'an i32\'s high 24 bits first and its low 8 second, the value itself, beside two i32s '
     'masked to as many: put together with a shift by 8', FN32 % dict(n='same8', tm='i32', tn='i32', body=sel(
         op('i32.eq', sh(A, 8), mk(MV, 0xffffff)), op('i32.lt_u', mk(A, 255), mk(NV, 255)),
         op('i32.lt_u', sh(A, 8), mk(MV, 0xffffff)))),
     lambda a, m, n: rel('lt_u', a & 255, n & 255) if a >> 8 == m & 0xffffff else rel('lt_u', a >> 8, m & 0xffffff),
     [((na & 255) | (ma & 0xffffff) << 8, (mb & 0xffffff) | 0x55000000, (nb & 255) | 0xaa00) for ma, na, mb, nb in QUADS], 1, 0)
case('sg16', 'an i32\'s low 16 bits first, compared signed with an i32 that may be negative: a signed '
     'first member has to be the key\'s top bits and as wide as its type, and the test stays',
     FN32 % dict(n='sg16', tm='i32', tn='i32', body=sel(
         op('i32.eq', A16, MV), op('i32.lt_u', sh(A, 16), mk(NV, 0xffff)), op('i32.lt_s', A16, MV))),
     lambda a, m, n: rel('lt_u', a >> 16, n & 0xffff) if a & 0xffff == m else rel('lt_s', a & 0xffff, m),
     G16 + [(5 | 3 << 16, -7 & M32, 9), (5 | 3 << 16, 0x80000005, 1), (0xffff, M32, 0)], 0, 0)
AM = '(i64.and %s (i64.const 0xffffffff7fffffff))' % A
PAM = lambda a: a & 0xffffffff7fffffff
sel3('sgmix', 'the firsts signed, the one known not to be negative and the other an i32 that may be: '
     'the signed compare still', 1,
     sel(op('i32.eq', lo(AM), MV), op('i32.lt_u', hi32(AM), NV), op('i32.lt_s', lo(AM), MV)),
     lambda a, m, n: rel('lt_u', phi(PAM(a)), n) if plo(PAM(a)) == m else rel('lt_s', plo(PAM(a)), m),
     runs=GEN + [(5 | 3 << 32, -5 & M32, 0), (0x7fffffff | 1 << 32, 0x80000000, 0), (0, M32, 9)])
sel3('nsgmix', 'the seconds signed, the one known not to be negative and the other not: the test stays', 0,
     sel(op('i32.eq', MV, lo(A)), op('i32.lt_s', mk(NV, 0x7fffffff), hi32(A)), op('i32.lt_u', MV, lo(A))),
     lambda a, m, n: rel('lt_s', n & 0x7fffffff, phi(a)) if m == plo(a) else rel('lt_u', m, plo(a)),
     runs=[(a, m, n) for a, m, n in GEN] + [(5 | 0x80000000 << 32, 5, 7), (5 | 0xfffffff0 << 32, 5, 0x7fffffff)])
sel3('sgmixrev', 'the same, the firsts\' compare the other way round', 1,
     sel(op('i32.eq', lo(AM), MV), op('i32.gt_u', hi32(AM), NV), op('i32.lt_s', MV, lo(AM))),
     lambda a, m, n: rel('gt_u', phi(PAM(a)), n) if plo(PAM(a)) == m else rel('lt_s', m, plo(PAM(a))),
     runs=GEN + [(5 | 3 << 32, -5 & M32, 0), (0x7fffffff | 1 << 32, 0x80000000, 0), (0, M32, 9)])
sel3('nsgmixrev', 'the same, the seconds\' compare the other way round', 0,
     sel(op('i32.eq', MV, lo(A)), op('i32.lt_s', hi32(A), mk(NV, 0x7fffffff)), op('i32.gt_u', MV, lo(A))),
     lambda a, m, n: rel('lt_s', phi(a), n & 0x7fffffff) if m == plo(a) else rel('gt_u', m, plo(a)),
     runs=[(a, m, n) for a, m, n in GEN] + [(5 | 0x80000000 << 32, 5, 7), (5 | 0xfffffff0 << 32, 5, 0x7fffffff)])
M48 = (1 << 48) - 1
A48 = '(i64.and %s (i64.const 0xffffffffffff))' % A
sel3('mixedw', 'an i64\'s low 48 bits first and its high 16 second, beside an i32\'s two 16-bit parts '
     'widened: put together with a shift by 16', 1,
     sel(op('i64.eq', A48, ext(mk(MV, 0xffff))), op('i64.lt_u', '(i64.shr_u %s (i64.const 48))' % A, ext(sh(MV, 16))),
         op('i64.lt_u', A48, ext(mk(MV, 0xffff)))),
     lambda a, m, n: rel('lt_u', a >> 48, m >> 16) if a & M48 == m & 0xffff else rel('lt_u', a & M48, m & 0xffff, 64),
     runs=[((ma & 0xffff) | (na & 0xffff) << 48, (mb & 0xffff) | (nb & 0xffff) << 16, 0) for ma, na, mb, nb in Q16] +
          [(1 << 40 | 5 << 48, 9 | 9 << 16, 0), (7 | 5 << 48, 7 | 6 << 16, 0)])
case('mixedc', 'two i64s, the one\'s halves and the other\'s low 16 bits and high 48: neither fits the '
     'other\'s layout, and the test stays', FN2 % dict(n='mixedc', t='i64', body=sel(
         op('i32.eq', lo(A), mk(lo(B), 0xffff)), op('i64.lt_u', hi(A), '(i64.shr_u %s (i64.const 16))' % B),
         op('i32.lt_u', lo(A), mk(lo(B), 0xffff)))),
     lambda a, b: rel('lt_u', phi(a), b >> 16, 64) if plo(a) == b & 0xffff else rel('lt_u', plo(a), b & 0xffff),
     LOW + [(5 | 3 << 32, 5 | 3 << 16), (5 | 3 << 32, 5 | 4 << 16), (5 | 3 << 32, 5 | 1 << 60)], 0, 0)
sel3('genwrap', 'the other pair two i64s wrapped: each widened from its low half where the key is put '
     'together, whatever the i64 has above', 1,
     sel(op('i32.eq', lo(A), lo(MV)), op('i32.lt_u', hi32(A), lo(NV)), op('i32.lt_u', lo(A), lo(MV))),
     lambda a, m, n: rel('lt_u', phi(a), plo(n)) if plo(a) == plo(m) else rel('lt_u', plo(a), plo(m)),
     runs=[(a, m | 0xdead << 32, n | 0xbeef << 32) for a, m, n in GEN], tm='i64', tn='i64')
case('two', 'two pairs neither of which is one value\'s bits: two keys put together are no less work, '
     'and the test stays', '''
  (func $two (export "two") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    %s)
''' % sel(op('i32.eq', MV, '(local.get $p)'), op('i32.lt_u', NV, '(local.get $q)'), op('i32.lt_u', MV, '(local.get $p)')),
     lambda m, n, p, q: rel('lt_u', n, q) if m == p else rel('lt_u', m, p), QUADS, 0, 0)

# ---- the same as three branches --------------------------------------------

BR = '''
  (func $%(n)s (export "%(n)s") (param $a %(t)s) (param $b %(t)s) (result i32)%(lo)s
    (block $f
      (block $t
        (if %(c)s
          (then %(e)s)
          (else %(d)s)))
      (i32.store (i32.const 512) (i32.const 1))
      (return %(rt)s))
    (i32.store (i32.const 516) (i32.const 2))
    %(rf)s)
'''


def br2(name, what, brs, c, e, d, model, runs=LOW, lo_='', rt='(i32.const 11)', rf='(i32.const 22)', shapes=(), cmps=0, t='i64'):
    case(name, what, BR % dict(n=name, c=c, e=e, d=d, lo=lo_ and '\n    ' + lo_, rt=rt, rf=rf, t=t), model, runs, cmps, brs, shapes)


def to(t, c): return '(br_if $t %s) (br $f)' % c if t else '(br_if $f %s) (br $t)' % c


br2('br', 'the test as branches, the firsts the low halves: one branch on the rotated values\' compare', 1,
    op('i32.eq', lo(A), lo(B)), to(1, op('i64.ge_u', hi(A), hi(B))), to(1, op('i32.ge_u', lo(A), lo(B))),
    lambda a, b: 11 if (rel('ge_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('ge_u', plo(a), plo(b))) else 22,
    shapes=[(r'\bror\b', 2), (r'\bb\.(eq|ne)\b', 0)])
br2('brne', 'on a ne', 1,
    op('i32.ne', lo(A), lo(B)), to(1, op('i32.lt_u', lo(A), lo(B))), to(1, op('i64.lt_u', hi(A), hi(B))),
    lambda a, b: 11 if (rel('lt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('lt_u', plo(a), plo(b))) else 22)
br2('brswap', 'the seconds\' branch going the other way round to the same two places: the inverse of its '
    'compare', 1,
    op('i32.eq', lo(A), lo(B)), to(0, op('i64.lt_u', hi(A), hi(B))), to(1, op('i32.ge_u', lo(A), lo(B))),
    lambda a, b: 13 if (rel('ge_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('ge_u', plo(a), plo(b))) else 24,
    rt='(i32.const 13)', rf='(i32.const 24)')
br2('brsame', 'the firsts the high halves: a branch on the values\' own compare', 1,
    op('i64.eq', hi(A), hi(B)), to(1, op('i32.lt_u', lo(A), lo(B))), to(1, op('i64.lt_u', hi(A), hi(B))),
    lambda a, b: 11 if a < b else 22, runs=HIGH, shapes=[(r'\bror\b', 0), (r'\bb\.(eq|ne)\b', 0)])
br2('brvals', 'the two places handed the same values on either way', 1,
    op('i32.eq', lo(A), lo(B)), to(1, op('i64.lt_u', hi(A), hi(B))), to(1, op('i32.lt_u', lo(A), lo(B))),
    lambda a, b: (plo(a) + 3 if (rel('lt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('lt_u', plo(a), plo(b))) else phi(b) ^ 7) & M32,
    lo_='(local $x i32) (local $y i32)\n    (local.set $x (i32.add %s (i32.const 3)))\n    (local.set $y (i32.xor %s (i32.const 7)))' % (lo(A), hi32(B)),
    rt='(local.get $x)', rf='(local.get $y)')
br2('brconst', 'handed equal constants, each way its own: one made where the branch now is', 1,
    op('i32.eq', lo(A), lo(B)),
    '(local.set $x (i32.const 77)) ' + to(1, op('i64.lt_u', hi(A), hi(B))),
    '(local.set $x (i32.const 77)) ' + to(1, op('i32.lt_u', lo(A), lo(B))),
    lambda a, b: 78 if (rel('lt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('lt_u', plo(a), plo(b))) else 79,
    lo_='(local $x i32)', rt='(i32.add (local.get $x) (i32.const 1))', rf='(i32.add (local.get $x) (i32.const 2))')
br2('brdiffer', 'handed different constants by the two ways: they are not the same two places with the '
    'same values, and the branches stay', 0,
    op('i32.eq', lo(A), lo(B)),
    '(local.set $x (i32.const 77)) ' + to(1, op('i64.lt_u', hi(A), hi(B))),
    '(local.set $x (i32.const 55)) ' + to(1, op('i32.lt_u', lo(A), lo(B))),
    lambda a, b: ((77 if plo(a) == plo(b) else 55) + (1 if (rel('lt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('lt_u', plo(a), plo(b))) else 2)),
    lo_='(local $x i32)', rt='(i32.add (local.get $x) (i32.const 1))', rf='(i32.add (local.get $x) (i32.const 2))')
DYN = '(i32.store (i32.add (i32.const 520) (i32.and %s (i32.const 4))) (i32.const 3))' % lo(A)
SEEN = '(i32.add (i32.load (i32.const 520)) (i32.shl (i32.load (i32.const 524)) (i32.const 4)))'
ZERO = '(i64.store (i32.const 520) (i64.const 0))'


def seen(a): return 3 << (4 if plo(a) & 4 else 0)


br2('brstore', 'the seconds\' block stores before it compares, at a place worked out there: more than a '
    'compare, and the branches stay', 0,
    op('i32.eq', lo(A), lo(B)), DYN + ' ' + to(1, op('i64.lt_u', hi(A), hi(B))), to(1, op('i32.lt_u', lo(A), lo(B))),
    lambda a, b: (11 if (rel('lt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('lt_u', plo(a), plo(b))) else 22) + (seen(a) if plo(a) == plo(b) else 0),
    lo_=ZERO, rt='(i32.add (i32.const 11) %s)' % SEEN, rf='(i32.add (i32.const 22) %s)' % SEEN)
br2('brstoren', 'the firsts\' block stores before it compares', 0,
    op('i32.eq', lo(A), lo(B)), to(1, op('i64.lt_u', hi(A), hi(B))), DYN + ' ' + to(1, op('i32.lt_u', lo(A), lo(B))),
    lambda a, b: (11 if (rel('lt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('lt_u', plo(a), plo(b))) else 22) + (0 if plo(a) == plo(b) else seen(a)),
    lo_=ZERO, rt='(i32.add (i32.const 11) %s)' % SEEN, rf='(i32.add (i32.const 22) %s)' % SEEN)
br2('brhi32', 'the seconds\' block wraps the high halves before it compares them', 1,
    op('i32.eq', lo(A), lo(B)), to(1, op('i32.gt_u', hi32(A), hi32(B))), to(1, op('i32.gt_u', lo(A), lo(B))),
    lambda a, b: 15 if (rel('gt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('gt_u', plo(a), plo(b))) else 26,
    rt='(i32.const 15)', rf='(i32.const 26)')
br2('brmask16', 'an i32\'s high 16 bits first: the seconds\' block masks the low 16', 1,
    op('i32.eq', sh(A, 16), sh(B, 16)), to(1, op('i32.le_u', A16, B16)), to(1, op('i32.lt_u', sh(A, 16), sh(B, 16))),
    lambda a, b: 17 if a <= b else 28, runs=PARTS, rt='(i32.const 17)', rf='(i32.const 28)', t='i32',
    shapes=[(r'\bror\b', 0), (r'\bb\.(eq|ne)\b', 0)])
# the halves made once at the top, so that every block reads the same values
HALVES = '''(local $la i32) (local $lb i32) (local $ha i32) (local $hb i32)
    (local.set $la (i32.wrap_i64 (local.get $a)))
    (local.set $lb (i32.wrap_i64 (local.get $b)))
    (local.set $ha (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))))
    (local.set $hb (i32.wrap_i64 (i64.shr_u (local.get $b) (i64.const 32))))'''
JOIN = '''
  (func $%(n)s (export "%(n)s") (param $a i64) (param $b i64) (param $g i32) (result i32)
    %(h)s
    (block $f
      (block $t
        (block $e
          (block $n
            %(pre)s
            (br_if $e (i32.eq (local.get $la) (local.get $lb))))
          (br_if $t (i32.lt_u (local.get $la) (local.get $lb)))
          (br $f))
        (br_if $t (i32.lt_u (local.get $ha) (local.get $hb)))
        (br $f))
      (i32.store (i32.const 512) (local.get $g))
      (return (i32.const 11)))
    (i32.store (i32.const 516) (local.get $g))
    (i32.const 22))
'''
JRUNS = [(a, b, g) for a, b in LOW for g in (0, 1)]
case('brjoin', 'the seconds\' block entered another way too, where a third value says so: it stays for '
     'that way, and the branch on whether the firsts are equal goes past it, on the keys\' compare',
     JOIN % dict(n='brjoin', h=HALVES, pre='(br_if $e (local.get $g))'),
     lambda a, b, g: 11 if (rel('lt_u', phi(a), phi(b)) if g or plo(a) == plo(b) else rel('lt_u', plo(a), plo(b))) else 22,
     JRUNS, 0, 1)
case('brjoinn', 'the firsts\' block entered another way too: the same',
     JOIN % dict(n='brjoinn', h=HALVES, pre='(br_if $n (local.get $g))'),
     lambda a, b, g: 11 if (rel('lt_u', phi(a), phi(b)) if not g and plo(a) == plo(b) else rel('lt_u', plo(a), plo(b))) else 22,
     JRUNS, 0, 1)
br2('brload', 'the firsts the high half of one value and the low of the other, whose second member is '
    'loaded only where the firsts are equal, as C reads a struct\'s next field: a load is not made where '
    'it was not, and the branches stay', 0,
    op('i32.eq', hi32(A), lo(B)),
    to(1, op('i32.lt_u', lo(A), '(i32.load (i32.const 4))')),
    to(1, op('i32.lt_u', hi32(A), lo(B))),
    lambda a, b: 11 if (rel('lt_u', plo(a), PAIRS[0][1]) if phi(a) == plo(b) else rel('lt_u', phi(a), plo(b))) else 22,
    runs=HIGH + [(3 | 5 << 32, 5), (9 | 5 << 32, 5), (10 | 5 << 32, 5)])
br2('brthird', 'the seconds\' block going on to a third place: not the same two', 0,
    op('i32.eq', lo(A), lo(B)),
    '(br_if $t %s) (return (i32.const 33))' % op('i64.ge_u', hi(A), hi(B)),
    to(1, op('i32.lt_u', lo(A), lo(B))),
    lambda a, b: (11 if rel('ge_u', phi(a), phi(b)) else 33) if plo(a) == plo(b) else (11 if rel('lt_u', plo(a), plo(b)) else 22))
br2('brcross', 'the seconds\' compare the other way from the firsts\', as in cross', 0,
    op('i32.eq', lo(A), lo(B)), to(1, op('i64.gt_u', hi(A), hi(B))), to(1, op('i32.lt_u', lo(A), lo(B))),
    lambda a, b: 11 if (rel('gt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('lt_u', plo(a), plo(b))) else 22)

case('breh', 'the firsts tested for equality ahead of a call a handler stands behind and branched on '
     'after it: the branch is in a block of its own behind the call\'s, and the keys are made there', '''
  (func $breh (export "breh") (param $a i64) (param $b i64) (param $g i32) (result i32)
    (local $c i32)
    (block $h
      (try_table (catch_all $h)
        (block $f
          (block $t
            (block $e
              (local.set $c (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))))
              (call_indirect (type $t1) (local.get $g) (i32.load8_u (i32.const 220)))
              (br_if $e (local.get $c))
              (br_if $t (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))))
              (br $f))
            (br_if $t (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32))))
            (br $f))
          (i32.store (i32.const 512) (local.get $g))
          (return (i32.const 11)))
        (i32.store (i32.const 516) (local.get $g))
        (return (i32.const 22))))
    (i32.const -7))
''', lambda a, b, g: -7 if g == 3 else 11 if (rel('lt_u', phi(a), phi(b)) if plo(a) == plo(b) else rel('lt_u', plo(a), plo(b))) else 22,
     [(a, b, g) for a, b in LOW[:8] for g in (0, 3)] + [(a, b, 1) for a, b in LOW[8:]], 0, 1)
def joinp_model(a, b, g):
    if g == 1:
        return 211 if rel('lt_u', plo(a), 5) else 222
    if g == 2:
        return 311 if rel('lt_u', phi(a), plo(b)) else 322
    return 111 if (rel('lt_u', plo(a), phi(b)) if phi(a) == plo(b) else rel('lt_u', phi(a), plo(b))) else 122


case('brjoinp', 'each of the two blocks entered another way with values of its own -- the seconds\' with '
     'another second member -- so that the blocks have parameters, one handed on to the two places: the '
     'keys are made of what the branch on the firsts hands them, and the branch hands on the same', '''
  (func $brjoinp (export "brjoinp") (param $a i64) (param $b i64) (param $g i32) (result i32)
    (local $y i32) (local $r i32)
    %s
    (local.set $y (local.get $hb))
    (local.set $r (i32.const 100))
    (block $f
      (block $t
        (block $e
          (block $n
            (if (i32.eq (local.get $g) (i32.const 1))
              (then (local.set $y (i32.const 5)) (local.set $r (i32.const 200)) (br $e)))
            (if (i32.eq (local.get $g) (i32.const 2))
              (then (local.set $r (i32.const 300)) (br $n)))
            (br_if $e (i32.eq (local.get $ha) (local.get $lb))))
          (br_if $t (i32.lt_u (local.get $ha) (local.get $lb)))
          (br $f))
        (br_if $t (i32.lt_u (local.get $la) (local.get $y)))
        (br $f))
      (i32.store (i32.const 512) (local.get $g))
      (return (i32.add (local.get $r) (i32.const 11))))
    (i32.store (i32.const 516) (local.get $g))
    (i32.add (local.get $r) (i32.const 22)))
''' % HALVES, joinp_model, [(na | ma << 32, mb | nb << 32, g) for ma, na, mb, nb in QUADS for g in (0, 1, 2)], 0, 1)
BR3 = '''
  (func $%(n)s (export "%(n)s") (param $a i64) (param $m i32) (param $n i32) (result i32)
    (block $f
      (block $t
        (if %(c)s
          (then %(e)s)
          (else %(d)s)))
      (i32.store (i32.const 512) (i32.const 1))
      (return (i32.const 11)))
    (i32.store (i32.const 516) (i32.const 2))
    (i32.const 22))
'''
case('brgen', 'outside any loop, one pair an i64\'s halves and the other two i32s: a rotation, a shift '
     'and an or are more than the first compare and its branch were, and the branches stay',
     BR3 % dict(n='brgen', c=op('i32.eq', lo(A), MV), e=to(1, op('i32.lt_u', hi32(A), NV)), d=to(1, op('i32.lt_u', lo(A), MV))),
     lambda a, m, n: 11 if (rel('lt_u', phi(a), n) if plo(a) == m else rel('lt_u', plo(a), m)) else 22, GEN, 0, 0)
case('brgensame', 'the same with the i64\'s first member its high half, a key that is the value itself: '
     'one branch', BR3 % dict(n='brgensame', c=op('i32.eq', hi32(A), MV), e=to(1, op('i32.lt_u', lo(A), NV)),
                              d=to(1, op('i32.lt_u', hi32(A), MV))),
     lambda a, m, n: 11 if (rel('lt_u', plo(a), n) if phi(a) == m else rel('lt_u', phi(a), m)) else 22,
     [(na | ma << 32, mb, nb) for ma, na, mb, nb in QUADS], 0, 1)

# loops over the memory's pairs
LOOP = '''
  (func $%(n)s (export "%(n)s") (param $m i32) (param $n i32) (param $cnt i32) (result i32)
    (local $i i32) (local $x i64) (local $s i32)
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $cnt)))
        (local.set $x (i64.load (i32.shl (local.get $i) (i32.const 3))))%(step)s
        (block $next
          (block $f
            (block $t
              (if %(c)s
                (then %(e)s)
                (else %(d)s)))
            (i32.store8 (i32.add (i32.const 1024) (local.get $i)) (i32.const 1))
            (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (i32.const 1)))
            (br $next))
          (i32.store8 (i32.add (i32.const 1100) (local.get $i)) (i32.const 1))
          (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 5)) (i32.const 2))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
'''
XV = '(local.get $x)'


def loop_model(test, step=None):
    def f(m, n, cnt):
        s = 0
        for i in range(cnt):
            x = PAIRS[i][0] | PAIRS[i][1] << 32
            if step:
                m, n = step(m, n, i)
            s = (s * 3 + 1 if test(m, n, x) else s * 5 + 2) & M32
        return s
    return f


LRUNS = [(5, 9, 16), (5, 4, 16), (0x80000000, 1, 16), (0, 0, 16), (M32, M32, 16), (7, 0x80000000, 12), (2, 0, 3), (9, 9, 0)]
case('brloop', 'in a loop, a pair of two i32s the loop does not change against each of the memory\'s '
     'i64s -- a new entry sifted up Zig\'s queue: its key is put together once ahead of the loop, and a '
     'time round is a rotation, a compare and a branch',
     LOOP % dict(n='brloop', step='', c=op('i32.eq', MV, lo(XV)), e=to(1, op('i64.gt_u', hi(XV), '(i64.extend_i32_u %s)' % NV)),
                 d=to(1, op('i32.lt_u', MV, lo(XV)))),
     loop_model(lambda m, n, x: rel('gt_u', phi(x), n) if m == plo(x) else rel('lt_u', m, plo(x))), LRUNS, 0, 1,
     [(r'\bror\b', 1, 'loop'), (r'\borr\b', 0, 'loop'), (r'\bb\.(eq|ne)\b', 0, 'loop')])
case('brloopvar', 'the same with the pair changing each time round: put together in the loop it is more '
     'than the branches, which stay',
     LOOP % dict(n='brloopvar', step='\n        (local.set $m (i32.add (local.get $m) (local.get $i)))'
                 '\n        (local.set $n (i32.xor (local.get $n) (local.get $i)))',
                 c=op('i32.eq', MV, lo(XV)), e=to(1, op('i64.gt_u', hi(XV), '(i64.extend_i32_u %s)' % NV)),
                 d=to(1, op('i32.lt_u', MV, lo(XV)))),
     loop_model(lambda m, n, x: rel('gt_u', phi(x), n) if m == plo(x) else rel('lt_u', m, plo(x)),
                lambda m, n, i: ((m + i) & M32, n ^ i)), LRUNS, 0, 0)

case('brloopc', 'the pair in the loop an i32 the loop does not change and a constant', LOOP % dict(
    n='brloopc', step='', c=op('i32.eq', MV, lo(XV)), e=to(1, op('i64.gt_u', hi(XV), '(i64.const 4)')),
    d=to(1, op('i32.lt_u', MV, lo(XV)))),
     loop_model(lambda m, n, x: rel('gt_u', phi(x), 4) if m == plo(x) else rel('lt_u', m, plo(x))), LRUNS, 0, 1,
     [(r'\bror\b', 1, 'loop'), (r'\bb\.(eq|ne)\b', 0, 'loop')])
case('brloopk', 'the pair in the loop a constant the test of the firsts reads there and an i32 the loop '
     'does not change', LOOP % dict(
    n='brloopk', step='', c=op('i32.eq', lo(XV), '(i32.const 7)'), e=to(1, op('i64.lt_u', hi(XV), '(i64.extend_i32_u %s)' % NV)),
    d=to(1, op('i32.lt_u', lo(XV), '(i32.const 7)'))),
     loop_model(lambda m, n, x: rel('lt_u', phi(x), n) if plo(x) == 7 else rel('lt_u', plo(x), 7)), LRUNS + [(0, 0x7fffffff, 16), (0, 0x80000001, 16)], 0, 1,
     [(r'\bror\b', 1, 'loop'), (r'\bb\.(eq|ne)\b', 0, 'loop')])
case('brloopvarn', 'only the pair\'s second member changing each time round, widened where the seconds '
     'are compared: put together in the loop all the same, and the branches stay',
     LOOP % dict(n='brloopvarn', step='\n        (local.set $n (i32.xor (local.get $n) (local.get $i)))',
                 c=op('i32.eq', MV, lo(XV)), e=to(1, op('i64.gt_u', hi(XV), '(i64.extend_i32_u %s)' % NV)),
                 d=to(1, op('i32.lt_u', MV, lo(XV)))),
     loop_model(lambda m, n, x: rel('gt_u', phi(x), n) if m == plo(x) else rel('lt_u', m, plo(x)),
                lambda m, n, i: (m, n ^ i)), LRUNS, 0, 0)


def rot_model(a, cnt):
    s = 0
    for i in range(cnt):
        m, n = PAIRS[i]
        t = rel('gt_u', phi(a), n) if plo(a) == m else rel('gt_u', plo(a), m)
        s = (s * 3 + 1 if t else s * 5 + 2) & M32
    return s


case('brlooprot', 'an i64 the loop does not change against a pair of two i32s loaded each time round: '
     'the i64 is rotated once ahead of the loop, and the pair\'s key put together in it is no more than '
     'the first compare and its branch were', '''
  (func $brlooprot (export "brlooprot") (param $a i64) (param $cnt i32) (result i32)
    (local $i i32) (local $m i32) (local $n i32) (local $s i32)
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $cnt)))
        (local.set $m (i32.load (i32.shl (local.get $i) (i32.const 3))))
        (local.set $n (i32.load offset=4 (i32.shl (local.get $i) (i32.const 3))))
        (block $next
          (block $f
            (block $t
              (if (i32.eq (i32.wrap_i64 (local.get $a)) (local.get $m))
                (then (br_if $t (i32.gt_u (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (local.get $n))) (br $f))
                (else (br_if $t (i32.gt_u (i32.wrap_i64 (local.get $a)) (local.get $m))) (br $f))))
            (i32.store8 (i32.add (i32.const 1024) (local.get $i)) (i32.const 1))
            (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (i32.const 1)))
            (br $next))
          (i32.store8 (i32.add (i32.const 1100) (local.get $i)) (i32.const 1))
          (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 5)) (i32.const 2))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
''', rot_model, [(5 | 9 << 32, 16), (5 | 3 << 32, 16), (0, 16), (2 | M32 << 32, 16), (M32 | M32 << 32, 16), (7 | 0x80000000 << 32, 12), (6, 0)], 0, 1,
     [(r'\bror\b', 0, 'loop'), (r'\bb\.(eq|ne)\b', 0, 'loop')])

def loopp_model(m, cnt, g):
    s = 0
    for i in range(cnt):
        x = PAIRS[i][0] | PAIRS[i][1] << 32
        y = PAIRS[(i + 1) & 15][1]
        if g == i:
            t = rel('lt_u', 5, phi(x))
        else:
            t = rel('lt_u', y, phi(x)) if m == plo(x) else rel('lt_u', m, plo(x))
        s = (s * 3 + 1 if t else s * 5 + 2) & M32
    return s


case('brloopp', 'in a loop, the seconds\' block entered another way too, its second member a parameter: '
     'the branch on the firsts hands it one loaded each time round, and a key put together of that in '
     'the loop is more than the branches, which stay', '''
  (func $brloopp (export "brloopp") (param $m i32) (param $cnt i32) (param $g i32) (result i32)
    (local $i i32) (local $x i64) (local $s i32) (local $y i32)
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $cnt)))
        (local.set $x (i64.load (i32.shl (local.get $i) (i32.const 3))))
        (local.set $y (i32.load offset=4 (i32.shl (i32.and (i32.add (local.get $i) (i32.const 1)) (i32.const 15)) (i32.const 3))))
        (block $next
          (block $f
            (block $t
              (block $e
                (if (i32.eq (local.get $g) (local.get $i)) (then (local.set $y (i32.const 5)) (br $e)))
                (br_if $e (i32.eq (local.get $m) (i32.wrap_i64 (local.get $x))))
                (br_if $t (i32.lt_u (local.get $m) (i32.wrap_i64 (local.get $x))))
                (br $f))
              (br_if $t (i32.lt_u (local.get $y) (i32.wrap_i64 (i64.shr_u (local.get $x) (i64.const 32)))))
              (br $f))
            (i32.store8 (i32.add (i32.const 1024) (local.get $i)) (i32.const 1))
            (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (i32.const 1)))
            (br $next))
          (i32.store8 (i32.add (i32.const 1100) (local.get $i)) (i32.const 1))
          (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 5)) (i32.const 2))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
''', loopp_model, [(5, 16, 99), (5, 16, 3), (7, 16, 8), (0x80000000, 16, 0), (2, 5, 2), (9, 0, 0)], 0, 0)

# the branches' two places a loop's top and its way out, each handed values
def back_model(a, cnt):
    i = 0
    while i < cnt:
        x = PAIRS[i][0] | PAIRS[i][1] << 32
        if not (rel('ge_u', phi(x), phi(a)) if plo(x) == plo(a) else rel('ge_u', plo(x), plo(a))):
            return i * 16 + 3
        i += 1
    return i * 16 + 5


case('brback', 'the two places the top of the loop the branches are in, handed the next index, and its '
     'way out, handed another value: the branch hands them the same', '''
  (func $brback (export "brback") (param $a i64) (param $cnt i32) (result i32)
    (local $i i32) (local $x i64) (local $r i32)
    (local.set $r (i32.const 5))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $cnt)))
        (local.set $x (i64.load (i32.shl (local.get $i) (i32.const 3))))
        (local.set $r (i32.const 3))
        (if (i32.eq (i32.wrap_i64 (local.get $x)) (i32.wrap_i64 (local.get $a)))
          (then (br_if $out (i64.lt_u (i64.shr_u (local.get $x) (i64.const 32)) (i64.shr_u (local.get $a) (i64.const 32)))))
          (else (br_if $out (i32.lt_u (i32.wrap_i64 (local.get $x)) (i32.wrap_i64 (local.get $a))))))
        (local.set $r (i32.const 5))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (i32.shl (local.get $i) (i32.const 4)) (local.get $r)))
''', back_model, [(5 | 9 << 32, 16), (5 | 3 << 32, 16), (0, 16), (2 | M32 << 32, 16), (5 | 10 << 32, 16), (M32 | M32 << 32, 16), (0, 0), (6, 2)], 0, 1,
     [(r'\bror\b', 1, 'loop'), (r'\bb\.(eq|ne)\b', 0, 'loop')])

# the queue itself: the lesser of two entries by a select, an entry against
# another by branches
def sift_model(cnt):
    h = [m | n << 32 for m, n in PAIRS[:cnt]]
    key = lambda x: (plo(x), phi(x))
    s = 0
    # a selection sort by the same two tests: the least of what is left, by the select, moved down past
    # the entries that are no less, by the branches
    for i in range(cnt):
        b = i
        for j in range(i + 1, cnt):
            if key(h[j]) < key(h[b]):
                b = j
        h[i], h[b] = h[b], h[i]
        s = (s * 31 + plo(h[i]) + 7 * phi(h[i])) & M32
    # and how many entries each is no less than
    for i in range(cnt):
        for j in range(cnt):
            if key(h[i]) >= key(h[j]):
                s = (s + i + 1) & M32
    return s


case('queue', 'both in one function, over the memory\'s entries in a copy: the least of those left '
     'picked by the select and moved down, then each entry counted against every other by the branches',
     '''
  (func $queue (export "queue") (param $cnt i32) (result i32)
    (local $i i32) (local $j i32) (local $b i32) (local $s i32) (local $x i64) (local $y i64)
    (memory.copy (i32.const 2048) (i32.const 0) (i32.shl (local.get $cnt) (i32.const 3)))
    (block $done
      (loop $outer
        (br_if $done (i32.ge_u (local.get $i) (local.get $cnt)))
        (local.set $b (local.get $i))
        (local.set $j (i32.add (local.get $i) (i32.const 1)))
        (block $picked
          (loop $pick
            (br_if $picked (i32.ge_u (local.get $j) (local.get $cnt)))
            (local.set $x (i64.load (i32.add (i32.const 2048) (i32.shl (local.get $j) (i32.const 3)))))
            (local.set $y (i64.load (i32.add (i32.const 2048) (i32.shl (local.get $b) (i32.const 3)))))
            (local.set $b (select (local.get $j) (local.get $b)
              (select (i64.lt_u (i64.shr_u (local.get $x) (i64.const 32)) (i64.shr_u (local.get $y) (i64.const 32)))
                      (i32.lt_u (i32.wrap_i64 (local.get $x)) (i32.wrap_i64 (local.get $y)))
                      (i32.eq (i32.wrap_i64 (local.get $x)) (i32.wrap_i64 (local.get $y))))))
            (local.set $j (i32.add (local.get $j) (i32.const 1)))
            (br $pick)))
        (local.set $x (i64.load (i32.add (i32.const 2048) (i32.shl (local.get $b) (i32.const 3)))))
        (i64.store (i32.add (i32.const 2048) (i32.shl (local.get $b) (i32.const 3)))
                   (i64.load (i32.add (i32.const 2048) (i32.shl (local.get $i) (i32.const 3)))))
        (i64.store (i32.add (i32.const 2048) (i32.shl (local.get $i) (i32.const 3))) (local.get $x))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
          (i32.add (i32.wrap_i64 (local.get $x)) (i32.mul (i32.wrap_i64 (i64.shr_u (local.get $x) (i64.const 32))) (i32.const 7)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $outer)))
    (local.set $i (i32.const 0))
    (block $done2
      (loop $outer2
        (br_if $done2 (i32.ge_u (local.get $i) (local.get $cnt)))
        (local.set $x (i64.load (i32.add (i32.const 2048) (i32.shl (local.get $i) (i32.const 3)))))
        (local.set $j (i32.const 0))
        (block $counted
          (loop $count
            (br_if $counted (i32.ge_u (local.get $j) (local.get $cnt)))
            (local.set $y (i64.load (i32.add (i32.const 2048) (i32.shl (local.get $j) (i32.const 3)))))
            (block $no
              (block $yes
                (if (i32.eq (i32.wrap_i64 (local.get $x)) (i32.wrap_i64 (local.get $y)))
                  (then (br_if $yes (i64.ge_u (i64.shr_u (local.get $x) (i64.const 32)) (i64.shr_u (local.get $y) (i64.const 32))))
                        (br $no))
                  (else (br_if $yes (i32.ge_u (i32.wrap_i64 (local.get $x)) (i32.wrap_i64 (local.get $y))))
                        (br $no))))
              (i32.store8 (i32.add (i32.const 3072) (local.get $j)) (i32.const 1))
              (local.set $s (i32.add (local.get $s) (i32.add (local.get $i) (i32.const 1)))))
            (local.set $j (i32.add (local.get $j) (i32.const 1)))
            (br $count)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $outer2)))
    (local.get $s))
''', sift_model, [(0,), (1,), (2,), (5,), (9,), (16,)], 1, 1,
     [(r'\bcset\b', 0, 'loop'), (r'\bccmp\b', 1, 'loop')])

HEAD = """;; Two pairs compared by their first members and, where those are equal, by
;; their second -- `a.m == b.m ? a.n < b.n : a.m < b.m`, a select of two
;; compares on a third or three branches -- are one compare of two keys,
;; each the first member above the second: a value whose low bits are the
;; first and the rest the second, rotated; one whose high bits are the first,
;; as it is; two constants, one; anything else put together with a shift and
;; an or, where the other key is one of those (opt.c lx_match,
;; fold_lex_select, lx_branch).  From test/gen_lex_cmp.py, which works out
;; every answer from the test as it is written.
;;
"""


def render(names=None):
    funcs, shapes, runs, lines = [], [], [], []
    tot = [0, 0]
    for name, what, wat, model, rs, want, shp, ty in CASES:
        if names and name not in names:
            continue
        funcs.append(wat.strip('\n') + '\n')
        tot[0] += want[0]
        tot[1] += want[1]
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
    return (HEAD + '\n'.join(lines) + '\n;;\n;; stats -O2: lex_cmps=%d lex_brs=%d\n' % tuple(tot) +
            ('\n'.join(shapes) + '\n' if shapes else '') +
            ';;\n' + '\n'.join(runs) + '\n\n(module\n  (type $t1 (func (param i32)))\n  (memory 1)\n  (table 1 funcref)\n'
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
    for name, what, wat, model, rs, want, shp, ty in CASES:
        if names and name not in names:
            continue
        with tempfile.TemporaryDirectory() as d:
            w = os.path.join(d, 'one.wat')
            open(w, 'w').write(render([name]))
            r = subprocess.run([os.path.join(root, 'build', 'warmc'), '-O2'] + target +
                               ['--stats', '-o', os.path.join(d, 'one'), w], capture_output=True, text=True)
            got = tuple(sum(int(x) for x in re.findall(r'\b%s=(\d+)' % k, r.stdout + r.stderr)) for k in ('lex_cmps', 'lex_brs'))
        mark = '' if got == want else '   <-- expected %d %d' % want
        if r.returncode:
            mark = '   <-- does not compile: ' + (r.stderr.strip().split('\n') or [''])[-1]
        bad += bool(mark)
        print('%-10s %d %d%s' % (name, got[0], got[1], mark))
    return bad


if __name__ == '__main__':
    if sys.argv[1] == '--each':
        sys.exit(1 if each(sys.argv[2:]) else 0)
    open(sys.argv[1], 'w').write(render())
    print('%s: %d functions' % (sys.argv[1], len(CASES)))
