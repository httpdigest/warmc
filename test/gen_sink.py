"""test/cases_opt/sink_use.wat: a cheap value read in one later block alone
is made there, where its operand is read too (opt.c pass_sink).  The
module's header says what each function is; every answer is worked out
here on a model of the function in Python.

  python3 test/gen_sink.py test/cases_opt/sink_use.wat
  python3 test/gen_sink.py --each [NAME ...]

--each compiles every function (or the named ones) alone with build/warmc
and prints what --stats counts for it as sunk beside what is expected.
"""
import os, re, subprocess, sys, tempfile

M32 = (1 << 32) - 1
M64 = (1 << 64) - 1
MEM = bytes([3, 7, 1, 9, 4, 6, 2, 8, 5, 0])


def s32(v):
    v &= M32
    return v - (1 << 32) if v >> 31 else v


def s64(v):
    v &= M64
    return v - (1 << 64) if v >> 63 else v


CASES = []


def case(name, what, wat, model, runs, want, shapes=(), ty='i32'):
    CASES.append((name, what, wat, model, runs, want, shapes, ty))


def fib(n):
    return n if n < 2 else fib(n - 1) + fib(n - 2)


case('fibl', 'fib, its second call made a loop as TinyGo leaves it, n - 2 made at the top and read where the '
     'loop goes round: made there, and the function saves two registers, not three', '''
  (func $fibl (export "fibl") (param $n i32) (result i32)
    (local $acc i32) (local $m i32)
    (if (i32.lt_s (local.get $n) (i32.const 2)) (then (return (local.get $n))))
    (block $out
      (loop $l
        (local.set $m (i32.sub (local.get $n) (i32.const 2)))
        (local.set $acc (i32.add (local.get $acc) (call $fibl (i32.sub (local.get $n) (i32.const 1)))))
        (br_if $out (i32.lt_u (local.get $n) (i32.const 4)))
        (local.set $n (local.get $m))
        (br $l)))
    (i32.add (local.get $acc) (local.get $m)))
''', fib, [(0,), (1,), (2,), (5,), (12,), (20,)], 1, [(r'\sstp\s', 2)])

# v = x OP k made ahead of an arm that stores, read after the arms meet, where x is read too
DIAMOND = '''
  (func $%(n)s (export "%(n)s") (param $x %(t)s) (param $c i32) (result %(t)s)
    (local $v %(t)s)%(lo)s
    (local.set $v %(v)s)
    (if (local.get $c) (then (%(t)s.store (i32.const 512) (local.get $x))))%(mid)s
    %(tail)s)
'''
D_RUNS = [(5, 0), (5, 1), (0x7ffffff0, 1), (0xfffffff9, 0)]


def diamond(name, what, want, v, tail, model, t='i32', lo='', mid='', runs=D_RUNS, shapes=()):
    case(name, what, DIAMOND % dict(n=name, t=t, v=v, tail=tail, lo=lo and ' ' + lo, mid=mid and '\n    ' + mid),
         model, runs, want, shapes, t)


X = '(local.get $x)'
V = '(local.get $v)'
STORE_X = '(i32.store (i32.const 516) (local.get $x))'
diamond('toinst', 'read by an instruction after the arms meet, where x is stored too', 1,
        '(i32.add %s (i32.const 7))' % X, '%s (i32.mul %s (i32.const 3))' % (STORE_X, V),
        lambda x, c: (x + 7) * 3)
diamond('toret', 'returned there', 1,
        '(i32.xor %s (i32.const 0x55))' % X, '%s %s' % (STORE_X, V), lambda x, c: x ^ 0x55)
diamond('tobranch', 'branched on there', 1,
        '(i32.and %s (i32.const 6))' % X,
        '%s (if (result i32) %s (then (i32.store (i32.const 520) %s) (i32.const 11)) (else (i32.const 22)))' % (STORE_X, V, X),
        lambda x, c: 11 if x & 6 else 22)
diamond('toedge', 'handed on from an arm that stores x, to where that arm meets the other', 1,
        '(i32.shl %s (i32.const 3))' % X,
        '(if (result i32) (i32.and (local.get $c) (i32.const 2)) (then (i32.store (i32.const 520) %s) %s) '
        '(else (i32.store (i32.const 524) (local.get $c)) (i32.const 9)))' % (X, V),
        lambda x, c: x << 3 if c & 2 else 9, runs=[(5, 0), (5, 2), (5, 3), (0xfffffff9, 2)])
diamond('wide', '64 bits, a shift right', 1,
        '(i64.shr_u %s (i64.const 9))' % X, '(i64.store (i32.const 528) %s) (i64.add %s (i64.const 1))' % (X, V),
        lambda x, c: (x >> 9) + 1, t='i64', runs=[(5, 0), (1 << 40, 1), (M64, 1)])
diamond('subk', 'a subtraction, an or, an arithmetic shift: each made there', 3,
        '(i32.sub %s (i32.const 100))' % X,
        '%s (i32.add (i32.add %s (local.get $u)) (local.get $w))' % (STORE_X, V),
        lambda x, c: (x - 100) + (x | 0x1000) + (s32(x) >> 2),
        lo='(local $u i32) (local $w i32)',
        mid='(local.set $u (i32.or (local.get $x) (i32.const 0x1000)))\n    (local.set $w (i32.shr_s (local.get $x) (i32.const 2)))')
# the three of subk are made before the arms as v is: move them there
CASES[-1] = (CASES[-1][0], CASES[-1][1],
             CASES[-1][2].replace('    (if (local.get $c) (then (i32.store (i32.const 512) (local.get $x))))\n'
                                  '    (local.set $u (i32.or (local.get $x) (i32.const 0x1000)))\n'
                                  '    (local.set $w (i32.shr_s (local.get $x) (i32.const 2)))',
                                  '    (local.set $u (i32.or (local.get $x) (i32.const 0x1000)))\n'
                                  '    (local.set $w (i32.shr_s (local.get $x) (i32.const 2)))\n'
                                  '    (if (local.get $c) (then (i32.store (i32.const 512) (local.get $x))))'),) + CASES[-1][3:]
diamond('alone', 'x is not read where v is: v stays where it was made', 0,
        '(i32.add %s (i32.const 7))' % X, '(i32.mul %s (i32.const 3))' % V, lambda x, c: (x + 7) * 3)
diamond('mulk', 'a multiplication is not one of the cheap ones', 0,
        '(i32.mul %s (i32.const 1000003))' % X, '%s (i32.add %s (i32.const 1))' % (STORE_X, V),
        lambda x, c: x * 1000003 + 1)
diamond('twovals', 'of two values, neither a constant', 0,
        '(i32.add %s (local.get $c))' % X, '%s (i32.mul %s (i32.const 3))' % (STORE_X, V),
        lambda x, c: (x + c) * 3)
diamond('twoblocks', 'read in the arm and after it', 0,
        '(i32.add %s (i32.const 7))' % X, '%s (i32.mul %s (i32.const 3))' % (STORE_X, V),
        lambda x, c: (x + 7) * 3)
CASES[-1] = (CASES[-1][0], CASES[-1][1],
             CASES[-1][2].replace('(then (i32.store (i32.const 512) (local.get $x)))',
                                  '(then (i32.store (i32.const 512) (local.get $v)))'),) + CASES[-1][3:]
diamond('same', 'read in the block it is made in', 0,
        '(i32.add %s (i32.const 7))' % X, '%s (i32.mul %s (i32.const 3))' % (STORE_X, V),
        lambda x, c: (x + 7) * 3)
CASES[-1] = (CASES[-1][0], CASES[-1][1],
             CASES[-1][2].replace('    (if (local.get $c) (then (i32.store (i32.const 512) (local.get $x))))\n', ''),) + CASES[-1][3:]


diamond('below2', 'read in two arms below the block where the first arms meet and x is stored: made there, '
        'above them both', 1,
        '(i32.add %s (i32.const 7))' % X,
        '%s (if (result i32) (i32.and (local.get $c) (i32.const 2)) '
        '(then (i32.store (i32.const 520) (local.get $c)) (i32.mul %s (i32.const 3))) '
        '(else (i32.store (i32.const 524) (local.get $c)) (i32.mul %s (i32.const 5))))' % (STORE_X, V, V),
        lambda x, c: (x + 7) * (3 if c & 2 else 5), runs=[(5, 0), (5, 2), (5, 3), (0xfffffff9, 1)])
diamond('xbranch', 'x is read where v is stored only by the branch there', 1,
        '(i32.add %s (i32.const 7))' % X,
        '(i32.store (i32.const 516) %s) '
        '(i32.add (if (result i32) %s (then (i32.store (i32.const 520) (local.get $c)) (i32.const 3)) '
        '(else (i32.store (i32.const 524) (local.get $c)) (i32.const 1))) (i32.load (i32.const 516)))' % (V, X),
        lambda x, c: (3 if x else 1) + x + 7, runs=[(5, 0), (5, 1), (0, 1), (0xfffffff9, 0)])


case('ehend', 'read only by the edge out of a block that ends in a call a handler stands behind: nothing can '
     'be made after that call, and it stays where it was', '''
  (func $ehend (export "ehend") (param $x i32) (param $c i32) (result i32)
    (local $v i32) (local $r i32)
    (local.set $v (i32.add (local.get $x) (i32.const 7)))
    (block $h
      (try_table (catch_all $h)
        (if (local.get $c)
          (then (i32.store (i32.const 512) (local.get $x))
                (call_indirect (type $t1) (local.get $c) (i32.load8_u (i32.const 220)))
                (local.set $r (local.get $v)))
          (else (i32.store (i32.const 516) (local.get $c)) (local.set $r (i32.const 9))))
        (return (i32.mul (local.get $r) (i32.const 3)))))
    (i32.const -7))
''', lambda x, c: -7 if c == 3 else (x + 7) * 3 if c else 27, [(5, 0), (5, 1), (5, 3), (0xfffffff9, 2)], 0)


def afterloop(p, k):
    v, s = 0, 0
    while True:
        v = p ^ k
        if MEM[p] == 0:
            break
        s += MEM[p]
        p += 1
    return v * 3 + s + k


case('afterloop', 'made each time round a loop and read only after it, with its operand: made after it', '''
  (func $afterloop (export "afterloop") (param $p i32) (param $k i32) (result i32)
    (local $v i32) (local $s i32)
    (block $out
      (loop $l
        (local.set $v (i32.xor (local.get $k) (i32.const 0x33)))
        (br_if $out (i32.eqz (i32.load8_u (local.get $p))))
        (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $p))))
        (local.set $k (i32.add (local.get $k) (local.get $s)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $v) (i32.const 3)) (local.get $k)))
''', None, [(0, 5), (4, 9), (9, 1)], 1)


def afterloop_model(p, k):
    s = 0
    while True:
        v = k ^ 0x33
        if MEM[p] == 0:
            break
        s += MEM[p]
        k = (k + s) & M32
        p += 1
    return v * 3 + k


def inner(x, p):
    v = (x + 7) & M32
    s = 0
    while MEM[p]:
        s = (s * 3 + (v ^ MEM[p]) + (x & p)) & M32
        p += 1
    return s


case('inner', 'made ahead of a loop and read in it: not made each time round', '''
  (func $inner (export "inner") (param $x i32) (param $p i32) (result i32)
    (local $v i32) (local $s i32)
    (local.set $v (i32.add (local.get $x) (i32.const 7)))
    (i32.store (i32.const 512) (local.get $p))
    (block $out
      (loop $l
        (br_if $out (i32.eqz (i32.load8_u (local.get $p))))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3))
                               (i32.add (i32.xor (local.get $v) (i32.load8_u (local.get $p)))
                                        (i32.and (local.get $x) (local.get $p)))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.get $s))
''', inner, [(5, 0), (5, 4), (100, 9), (0xfffffff9, 2)], 0)

case('ways', 'where two ways meet, each handing x to another of the block\'s parameters: neither is x', '''
  (func $ways (export "ways") (param $x i32) (param $y i32) (param $c i32) (result i32)
    (local $v i32) (local $q i32) (local $r i32)
    (local.set $v (i32.add (local.get $x) (i32.const 7)))
    (if (local.get $c)
      (then (local.set $q (local.get $x)) (local.set $r (local.get $y)) (i32.store (i32.const 512) (local.get $c)))
      (else (local.set $q (local.get $y)) (local.set $r (local.get $x)) (i32.store (i32.const 516) (local.get $y))))
    (i32.add (i32.mul (local.get $v) (i32.const 3)) (i32.sub (i32.shl (local.get $q) (i32.const 4)) (local.get $r))))
''', lambda x, y, c: (x + 7) * 3 + (((x if c else y) << 4) - (y if c else x)),
     [(5, 100, 0), (5, 100, 1), (0xfffffff9, 3, 0), (0xfffffff9, 3, 1)], 0)

HEAD = """;; A cheap value of one other and a constant, read in one block alone and
;; not the one it is made in, is made there, ahead of its first reader, when
;; its operand is read there too (opt.c pass_sink): it no longer lives from
;; the one block to the other, across a call or through a loop.  From
;; test/gen_sink.py, which works out every answer.
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
        if name == 'afterloop':
            model = afterloop_model
        for a in rs:
            r = model(*a)
            runs.append(';; run: %s %s => %s:%d' % (name, ' '.join(str(x) for x in a), ty, s64(r) if ty == 'i64' else s32(r)))
    data = ''.join('\\%02x' % b for b in MEM)
    return (HEAD + '\n'.join(lines) + '\n;;\n;; stats -O2: sunk=%d\n' % tot + ('\n'.join(shapes) + '\n' if shapes else '') +
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
            got = sum(int(x) for x in re.findall(r'\bsunk=(\d+)', r.stdout + r.stderr))
        mark = '' if got == want else '   <-- expected %d' % want
        if r.returncode:
            mark = '   <-- does not compile: ' + (r.stderr.strip().split('\n') or [''])[-1]
        bad += bool(mark)
        print('%-10s %d%s' % (name, got, mark))
    return bad


if __name__ == '__main__':
    if sys.argv[1] == '--each':
        sys.exit(1 if each(sys.argv[2:]) else 0)
    open(sys.argv[1], 'w').write(render())
    print('%s: %d functions' % (sys.argv[1], len(CASES)))
