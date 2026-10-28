"""test/cases_opt/key_split.wat: code below an edge that fixes a value,
which opt.c pass_key_split copies with the value a constant so that the
copy's later tests of it are decided -- TinyGo's switch on strings, whose
cases test the length again after the first test fixed it -- and the near
misses it must leave be.  Every function's answers worked out here, from
the same lists the code is made from.

  python3 test/gen_keysplit.py test/cases_opt/key_split.wat
"""
import sys

# the words, each at its own address, and a table of (address, length) at 0
WORDS = ["cat", "lion", "dog", "horse", "cow", "bird", "ox", "eel", "yak", "emu",
         "zebra", "giraffe", "lizard", "elephant", "",
         # near misses: a matching length and other bytes, and lengths no case has
         "cab", "lium", "dot", "horsy", "ow", "a", "abcdefghi", "eek"]
TABLE = 0
TEXT = 1024
LOADS = 4096   # bytes the long regions add up, 64 apart


def addr(i):
    return TEXT + 16 * i


# chain: each case tests the length, from a flag for 3 and 4, then the bytes
CHAIN = [("cat", 1), ("lion", 2), ("dog", 3), ("horse", 4), ("cow", 5), ("bird", 6),
         ("ox", 7), ("eel", 8), ("yak", 9), ("emu", 10)]
# disp: a switch on the length less 2 to the first case of each length,
# 6, 7 and 8 all to one block; a case whose bytes differ goes on to the rest
DISP_FIRST = {2: ("ox", 7), 3: ("cat", 1), 4: ("lion", 2), 5: ("horse", 4)}
DISP_LONG = [("lizard", 12), ("giraffe", 13), ("elephant", 14)]
DISP_REST = [("dog", 3), ("cow", 5), ("bird", 6), ("eel", 8), ("emu", 10), ("zebra", 11)]


def first(cases, w):
    for word, c in cases:
        if word == w:
            return c
    return 0


def disp_model(w):
    n = len(w)
    if n in DISP_FIRST and DISP_FIRST[n][0] == w:
        return DISP_FIRST[n][1]
    if 6 <= n <= 8:
        c = first(DISP_LONG, w)
        if c:
            return c
    return first(DISP_REST, w)


def bytes_match(label, w):
    """branches to label where the word at $p is not w (its length tested
    already)"""
    return ''.join('\n        (br_if %s (i32.ne (i32.load8_u offset=%d (local.get $p)) (i32.const %d)))'
                   % (label, k, ord(ch)) for k, ch in enumerate(w))


def chain_func():
    body = '''  (func $chain (param $p i32) (param $n i32) (result i32)
    (local $f3 i32) (local $f4 i32)
    (local.set $f3 (i32.eq (local.get $n) (i32.const 3)))
    (local.set $f4 (i32.eq (local.get $n) (i32.const 4)))'''
    for i, (w, c) in enumerate(CHAIN):
        g = {3: '(i32.eqz (local.get $f3))', 4: '(i32.eqz (local.get $f4))'}.get(
            len(w), '(i32.ne (local.get $n) (i32.const %d))' % len(w))
        body += '''
    (block $m%d
      (br_if $m%d %s)%s
      (return (i32.add (i32.const %d) (i32.mul (local.get $n) (i32.const 1000)))))''' % (
            i, i, g, bytes_match('$m%d' % i, w), c)
    return body + '\n    (i32.const 0))'


def disp_func():
    body = '''  (func $disp (param $p i32) (param $n i32) (result i32)
    (block $rest
      (block $big (block $l5 (block $l4 (block $l3 (block $l2
        (br_table $l2 $l3 $l4 $l5 $big $big $big $rest
          (i32.sub (local.get $n) (i32.const 2))))'''
    for n, lab in ((2, '$l2'), (3, '$l3'), (4, '$l4'), (5, '$l5')):
        w, c = DISP_FIRST[n]
        body += bytes_match('$rest', w).replace('\n        ', '\n        ') + '''
        (return (i32.const %d)))''' % c
    for i, (w, c) in enumerate(DISP_LONG):
        body += '''
      (block $g%d
        (br_if $g%d (i32.ne (local.get $n) (i32.const %d)))%s
        (return (i32.const %d)))''' % (i, i, len(w), bytes_match('$g%d' % i, w), c)
    body += ')'
    for i, (w, c) in enumerate(DISP_REST):
        body += '''
    (block $r%d
      (br_if $r%d (i32.ne (local.get $n) (i32.const %d)))%s
      (return (i32.const %d)))''' % (i, i, len(w), bytes_match('$r%d' % i, w), c)
    return body + '\n    (i32.const 0))'


def word_export(name):
    return '''  (func (export "%s_w") (param $i i32) (result i32)
    (call $%s (i32.load offset=%d (i32.shl (local.get $i) (i32.const 3)))
              (i32.load offset=%d (i32.shl (local.get $i) (i32.const 3)))))''' % (
        name, name, TABLE, TABLE + 4)


# reenter: below n == 3 the copy goes through a loop it cannot copy, into a
# merge it copies too, which reads what the block above both made
def reenter_model(n, x):
    x &= 0xffffffff
    if n == 3:
        v = (x * 7) & 0xffffffff
        c = 0
        if x & 1:
            y = x
            while True:
                y >>= 1
                c += 1
                if y == 0:
                    break
        if n == 4:
            return (v + 1 + c) & 0xffffffff
        if n == 3:
            return (v + 2 + c) & 0xffffffff
        return v
    if n == 5:
        return 50
    if n == 6:
        return 60
    return 0


REENTER = '''  (func $reenter (export "reenter") (param $n i32) (param $x i32) (result i32)
    (local $v i32) (local $c i32) (local $y i32) (local $w i32)
    (if (i32.eq (local.get $n) (i32.const 3))
      (then
        (local.set $v (i32.mul (local.get $x) (i32.const 7)))
        (if (i32.and (local.get $x) (i32.const 1))
          (then
            (local.set $y (local.get $x))
            (loop $l
              (local.set $y (i32.shr_u (local.get $y) (i32.const 1)))
              (local.set $c (i32.add (local.get $c) (i32.const 1)))
              (br_if $l (local.get $y)))))
        (local.set $w (i32.add (local.get $v) (local.get $c)))
        (if (i32.eq (local.get $n) (i32.const 4))
          (then (return (i32.add (local.get $w) (i32.const 1)))))
        (if (i32.eq (local.get $n) (i32.const 3))
          (then (return (i32.add (local.get $w) (i32.const 2)))))
        (return (local.get $v))))
    (if (i32.eq (local.get $n) (i32.const 5)) (then (return (i32.const 50))))
    (if (i32.eq (local.get $n) (i32.const 6)) (then (return (i32.const 60))))
    (i32.const 0))'''


# refs: below n == 3, a block reads a reference and tests it there, which
# the copy makes again and nothing past the block reads (escaping_values'
# read in a block of the value's own)
REFS = '''  (global $g (mut externref) (ref.null extern))
  (func (export "setg") (param $r externref) (global.set $g (local.get $r)))
  (func $refs (export "refs") (param $n i32) (result i32)
    (if (i32.eq (local.get $n) (i32.const 3))
      (then
        (if (i32.eq (local.get $n) (i32.const 4)) (then (return (i32.const 4))))
        (if (ref.is_null (global.get $g))
          (then (if (i32.eq (local.get $n) (i32.const 3)) (then (return (i32.const 33))))))
        (return (i32.const 3))))
    (if (i32.eq (local.get $n) (i32.const 5)) (then (return (i32.const 5))))
    (if (i32.eq (local.get $n) (i32.const 6)) (then (return (i32.const 6))))
    (i32.const 0))'''


def refs_model(n):
    return {3: 33, 5: 5, 6: 6}.get(n, 0)


# few: three tests of n against a constant, one fewer than a split needs,
# and one against x, which no copy can decide
FEW = '''  (func $few (export "few") (param $n i32) (param $x i32) (result i32)
    (if (i32.eq (local.get $n) (local.get $x)) (then (return (i32.const 77))))
    (if (i32.eq (local.get $n) (i32.const 3))
      (then
        (if (i32.eq (i32.and (local.get $x) (i32.const 1)) (i32.const 0))
          (then (if (i32.eq (local.get $n) (i32.const 4)) (then (return (i32.const 41))))))
        (if (i32.eq (local.get $n) (i32.const 3)) (then (return (i32.const 31))))))
    (i32.const 0))'''


def few_model(n, x):
    return 77 if n == x else 31 if n == 3 else 0


# one: four tests of n, but the copy below n == 1 would decide one
ONE = '''  (func $one (export "one") (param $n i32) (param $x i32) (result i32)
    (if (i32.eq (local.get $n) (i32.const 1))
      (then
        (if (i32.eq (i32.and (local.get $x) (i32.const 7)) (i32.const 7))
          (then (return (i32.const 11))))
        (if (i32.eq (local.get $n) (i32.const 9)) (then (return (i32.const 99))))
        (return (i32.const 12))))
    (if (i32.eq (local.get $n) (i32.const 2)) (then (return (i32.const 2))))
    (if (i32.eq (local.get $n) (i32.const 3)) (then (return (i32.const 3))))
    (i32.const 0))'''


def one_model(n, x):
    if n == 1:
        return 11 if x & 7 == 7 else 12
    return {2: 2, 3: 3}.get(n, 0)


# table: four tests that fix n lead to one switch on n - 1, whose cases
# test n again; each case's answer adds n - 1 and n == 3, made at the
# start, which the copies make constants
TABLE_IN = (2, 5, 11, 3)


def table_func():
    cases = ''.join('(block $c%d ' % i for i in reversed(range(10)))
    body = '''  (func $table (export "table") (param $n i32) (result i32)
    (local $d i32) (local $f i32) (local $v i32)
    (local.set $d (i32.sub (local.get $n) (i32.const 1)))
    (local.set $f (i32.eq (local.get $n) (i32.const 3)))
    (block $r'''
    for k in TABLE_IN:
        body += '''
      (br_if $r (i32.eq (local.get $n) (i32.const %d)))''' % k
    body += '''
      (return (i32.const 0)))
    (block $dflt %s
      (br_table %s $dflt (local.get $d)))''' % (cases, ' '.join('$c%d' % i for i in range(10)))
    v = '''
      (local.set $v (i32.add (i32.mul (local.get $d) (i32.const %d))
                             (i32.mul (local.get $f) (i32.const 50000))))'''
    for i in range(10):
        body += v % (1000 + i) + '''
      (if (i32.eq (local.get $n) (i32.const 3)) (then (return (i32.add (i32.const %d) (local.get $v)))))
      (return (i32.add (i32.const %d) (local.get $v))))''' % (200 + i, 100 + i)
    body += v % 1010 + '''
    (if (i32.eq (local.get $n) (i32.const 3)) (then (return (i32.add (i32.const 299) (local.get $v)))))
    (i32.add (i32.const 999) (local.get $v)))'''
    return body


def table_model(n):
    if n not in TABLE_IN:
        return 0
    d, f = n - 1, int(n == 3)
    v = d * (1000 + min(d, 10)) + f * 50000
    if d < 10:
        return (200 + d if n == 3 else 100 + d) + v
    return (299 if n == 3 else 999) + v


# shared: a switch on n sends 3, 4 and 5 to one block, whose tests of n
# each of them would decide (KS_SAME)
SHARED = '''  (func $shared (export "shared") (param $n i32) (result i32)
    (block $d (block $t (block $c2 (block $c1 (block $c0
      (br_table $c0 $c1 $c2 $t $t $t $c2 $c1 $c0 $d (local.get $n)))
      (return (i32.const 10)))
      (return (i32.const 11)))
      (return (i32.const 12)))
      (if (i32.eq (local.get $n) (i32.const 3)) (then (return (i32.const 33))))
      (if (i32.eq (local.get $n) (i32.const 4)) (then (return (i32.const 44))))
      (if (i32.eq (local.get $n) (i32.const 5)) (then (return (i32.const 56))))
      (return (i32.const 55)))
    (i32.const 99))'''


def shared_model(n):
    return {0: 10, 1: 11, 2: 12, 3: 33, 4: 44, 5: 56, 6: 12, 7: 11, 8: 10}.get(n, 99)


# loopy: every way below n == 3 goes round a loop, which no copy takes, so
# the tests after it are not reached
LOOPY = '''  (func $loopy (export "loopy") (param $n i32) (param $x i32) (result i32)
    (local $y i32) (local $c i32)
    (if (i32.eq (local.get $n) (i32.const 3))
      (then
        (local.set $y (local.get $x))
        (loop $l
          (local.set $y (i32.shr_u (local.get $y) (i32.const 1)))
          (local.set $c (i32.add (local.get $c) (i32.const 1)))
          (br_if $l (local.get $y)))
        (if (i32.eq (local.get $n) (i32.const 4)) (then (return (i32.add (local.get $c) (i32.const 400)))))
        (if (i32.eq (local.get $n) (i32.const 3)) (then (return (i32.add (local.get $c) (i32.const 300)))))
        (return (local.get $c))))
    (if (i32.eq (local.get $n) (i32.const 5)) (then (return (i32.const 5))))
    (if (i32.eq (local.get $n) (i32.const 6)) (then (return (i32.const 6))))
    (i32.const 0))'''


def loopy_model(n, x):
    if n == 3:
        c, y = 0, x
        while True:
            y >>= 1
            c += 1
            if y == 0:
                break
        return c + 300
    return {5: 5, 6: 6}.get(n, 0)


# sized: below n == 3, a sum of `count` bytes 64 apart, then two more tests
# of n -- a copy of about twice count instructions
def sized_func(name, count):
    terms = ''.join('\n        (local.set $s (i32.add (local.get $s) (i32.load8_u offset=%d (local.get $p))))'
                    % (LOADS + 64 * i) for i in range(count))
    return '''  (func $%s (export "%s") (param $n i32) (param $p i32) (result i32)
    (local $s i32)
    (if (i32.eq (local.get $n) (i32.const 3))
      (then%s
        (if (i32.eq (local.get $n) (i32.const 4)) (then (return (i32.const 4))))
        (if (i32.eq (local.get $n) (i32.const 3)) (then (return (local.get $s))))
        (return (i32.const 5))))
    (if (i32.eq (local.get $n) (i32.const 5)) (then (return (i32.const 55))))
    (if (i32.eq (local.get $n) (i32.const 6)) (then (return (i32.const 66))))
    (i32.const 0))''' % (name, name, terms)


def sized_model(n, p, count, mem):
    if n == 3:
        return sum(mem.get(p + LOADS + 64 * i, 0) for i in range(count)) & 0xffffffff
    return {5: 55, 6: 66}.get(n, 0)


MID, BIG = 300, 650


def main():
    mem = {}
    data = []
    table = b''
    for i, w in enumerate(WORDS):
        table += addr(i).to_bytes(4, 'little') + len(w).to_bytes(4, 'little')
        data.append('  (data (i32.const %d) "%s")' % (addr(i), w))
    data.insert(0, '  (data (i32.const %d) "%s")' % (TABLE, ''.join('\\%02x' % b for b in table)))
    # the bytes the sized functions add, each its index's low 7 bits plus 1
    sums = bytes(((i * 37) & 0x7f) + 1 for i in range(BIG))
    for i, b in enumerate(sums):
        mem[LOADS + 64 * i] = b
    for i in range(BIG):
        data.append('  (data (i32.const %d) "\\%02x")' % (LOADS + 64 * i, sums[i]))
    runs = []
    for i, w in enumerate(WORDS):
        runs.append(';; run: chain_w %d => i32:%d' % (i, (first(CHAIN, w) + len(w) * 1000) if first(CHAIN, w) else 0))
        runs.append(';; run: disp_w %d => i32:%d' % (i, disp_model(w)))
    for n in (0, 3, 4, 5, 6, 7):
        for x in (0, 1, 2, 7, 1000, 0xffffffff):
            runs.append(';; run: reenter %d %d => i32:%d' % (n, x, reenter_model(n, x) - (1 << 32 if reenter_model(n, x) >> 31 else 0)))
    for n in (0, 1, 2, 3, 4, 9):
        for x in (0, 1, 7, 15):
            runs.append(';; run: few %d %d => i32:%d' % (n, x, few_model(n, x)))
            runs.append(';; run: one %d %d => i32:%d' % (n, x, one_model(n, x)))
    for n in (0, 1, 2, 3, 4, 5, 6, 9, 10, 11, 12):
        runs.append(';; run: table %d => i32:%d' % (n, table_model(n)))
        runs.append(';; run: shared %d => i32:%d' % (n, shared_model(n)))
    for n in (0, 3, 5, 6):
        for x in (0, 1, 1000):
            runs.append(';; run: loopy %d %d => i32:%d' % (n, x, loopy_model(n, x)))
        runs.append(';; run: refs %d => i32:%d' % (n, refs_model(n)))
    for n in (0, 3, 4, 5, 6):
        runs.append(';; run: mid %d 0 => i32:%d' % (n, sized_model(n, 0, MID, mem)))
        runs.append(';; run: big %d 0 => i32:%d' % (n, sized_model(n, 0, BIG, mem)))
    head = '''\
;; opt.c pass_key_split: below an edge whose way fixes a value -- a test of
;; it, a case of a switch on it -- the code that tests it again gets a copy
;; in which it is the constant, and so its tests there decide.  chain is
;; TinyGo's switch on strings: each case tests the length, from a flag made
;; at the start for 3 and 4, then the bytes, and a case whose bytes differ
;; goes on to the next case's test; on a match it returns its number plus
;; the length times 1000, which the copy makes a constant.  disp switches on
;; the length less 2 to the first case of each length, three lengths of it to
;; one block, and the cases whose bytes differ go on
;; to the rest.  table's four ways in that fix n meet at a switch on n - 1,
;; whose cases test n again, and whose answers add n - 1 and n == 3, made
;; at the start.  reenter's copy goes through a loop it cannot copy into a
;; merge it copied, whose original reads what the block above made
;; (escaping_values' inside); refs' copy makes again a reference a block
;; reads only where it is made.  Left as they are: few, which tests n three
;; times against a constant (KS_TESTS) and once against x; one, whose copy
;; would decide one test (KS_DECIDED); shared, whose switch sends three
;; cases to one block (KS_SAME); loopy, whose tests after a loop no copy
;; reaches; big, whose copy would be over KS_COPY instructions, where mid's
;; is under.
;; Made by test/gen_keysplit.py, which works the answers out.
;;
;; stats -O1: key_splits=25
;; stats -O2: key_splits=44
;;
'''
    funcs = [chain_func(), word_export('chain'), disp_func(), word_export('disp'), table_func(),
             REENTER, REFS, FEW, ONE, SHARED, LOOPY, sized_func('mid', MID), sized_func('big', BIG)]
    out = head + '\n'.join(runs) + '\n\n(module\n  (memory 1)\n' + '\n'.join(data) + '\n' + \
        '\n'.join(funcs) + '\n)\n'
    open(sys.argv[1], 'w').write(out)


main()
