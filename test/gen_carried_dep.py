"""test/cases_opt/carried_dep.wat: a value a loop carries round by a chain
of adds (or xors, or multiplies) some of whose terms read the value itself
(opt.c pass_reassoc_carried): those come after it in the chain.  The
module's header says what each function is; every answer is worked out
here on a model of the function in Python.

  python3 test/gen_carried_dep.py test/cases_opt/carried_dep.wat
  python3 test/gen_carried_dep.py --each [NAME ...]

--each compiles every function (or the named ones) alone with build/warmc
and prints what --stats counts for it as reassoc beside what is expected.
"""
import os, re, subprocess, sys, tempfile

M32 = (1 << 32) - 1
M64 = (1 << 64) - 1
TEXT = b'the quick brown fox jumps over the lazy dog, 0123456789 times'
MEM = TEXT + bytes(3) + b'ab' + bytes(1) + bytes(range(1, 40)) + bytes(1)
S0, S1, S2 = 0, len(TEXT) + 3, len(TEXT) + 6


def s32(v):
    v &= M32
    return v - (1 << 32) if v >> 31 else v


def s64(v):
    v &= M64
    return v - (1 << 64) if v >> 63 else v


def scan(p):
    while MEM[p]:
        yield MEM[p]
        p += 1


CASES = []


def case(name, what, ty, body, step, runs, want, shapes=(), init=0, params='', locals_='', pre='', more='',
         result=None, final=None):
    """step(h, c, *extra): the value after a byte c; want: what --stats counts as reassoc.  params: more
    parameters after $p, handed to step; more: what else the loop does; result/final: the wasm and the
    model of what is returned, where that is not h"""
    mask = M64 if ty == 'i64' else M32
    wat = '''
  (func $%(n)s (export "%(n)s") (param $p i32)%(pa)s (result %(t)s)
    (local $h %(t)s) (local $c i32)%(lo)s
    (local.set $h (%(t)s.const %(i)d))%(pre)s
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $h %(b)s)%(more)s
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    %(res)s)
''' % dict(n=name, t=ty, i=init, b=body.strip(), pa=params and ' ' + params, lo=locals_ and ' ' + locals_,
           pre=pre and '\n    ' + pre, more=more and '\n        ' + more, res=result or '(local.get $h)')

    def model(p, *extra):
        h = init
        st = {}
        for c in scan(p):
            h = step(h, c, *extra, **({'st': st} if final else {})) & mask
        return final(h, st) & mask if final else h
    CASES.append((name, what, wat, model, runs, want, shapes, ty))


H = '(local.get $h)'
C64 = '(i64.extend_i32_u (local.get $c))'
C32 = '(local.get $c)'
RUNS = [(S0,), (S1,), (S2,), (S0 + 40,), (S1 + 2,)]

case('h33', 'h * 33 + c as LLVM leaves it, (h << 5) + h + c with c extended: the shift is added after h, '
     'which the add takes in', 'i64',
     '(i64.add (i64.add (i64.shl %s (i64.const 5)) %s) %s)' % (H, H, C64),
     lambda h, c: h * 33 + c, RUNS, 1, [(r':\s+lsl\s', 0, 'loop'), (r'\sadd\s', 3, 'loop')], init=5381)
case('first', 'the term that reads h is the last the chain adds', 'i64',
     '(i64.add (i64.add %s %s) (i64.shl %s (i64.const 5)))' % (C64, H, H),
     lambda h, c: h * 33 + c, RUNS, 1, [(r':\s+lsl\s', 0, 'loop')], init=7)
case('two', 'two terms read h, a shift each way', 'i64',
     '(i64.add (i64.add (i64.add (i64.shl %s (i64.const 5)) (i64.shr_u %s (i64.const 7))) %s) %s)' % (H, H, C64, H),
     lambda h, c: (h << 5) + (h >> 7) + c + h, RUNS, 1, [(r':\s+ls[lr]\s', 0, 'loop')], init=99)
case('konly', 'beside the shift of h a constant alone: h and the constant, then the shift', 'i64',
     '(i64.add (i64.add (i64.shl %s (i64.const 5)) (i64.const 7)) %s)' % (H, H),
     lambda h, c: h * 33 + 7, RUNS, 1, [(r':\s+lsl\s', 0, 'loop')], init=1)
case('kboth', 'a constant and the byte', 'i64',
     '(i64.add (i64.add (i64.add (i64.shl %s (i64.const 5)) (i64.const 7)) %s) %s)' % (H, C64, H),
     lambda h, c: h * 33 + 7 + c, RUNS, 1, [(r':\s+lsl\s', 0, 'loop')], init=3)
case('xs2', '32 bits, xors: h ^ (h << 5) ^ (c >> 1), two shifts for two xors to take in', 'i32',
     '(i32.xor (i32.xor (i32.shl %s (i32.const 5)) (i32.shr_u %s (i32.const 1))) %s)' % (H, C32, H),
     lambda h, c: (h << 5) ^ (c >> 1) ^ h, RUNS, 1, [(r':\s+ls[lr]\s', 0, 'loop')], init=0x1234)
case('mulc', 'a product: h * (h | 1) * c', 'i32',
     '(i32.mul (i32.mul (i32.or %s (i32.const 1)) %s) %s)' % (H, C32, H),
     lambda h, c: (h | 1) * c * h, RUNS, 1, init=3)
case('plain', 'no term reads h: h is added last, as it was', 'i64',
     '(i64.add (i64.add %s %s) (i64.shl %s (i64.const 3)))' % (H, C64, C64),
     lambda h, c: h + c + (c << 3), RUNS, 1, [(r'add\sx0, x\d+, x0$', 1, 'loop')], init=11)
case('alldep', 'every term reads h: the chain stays as it is', 'i64',
     '(i64.add (i64.add (i64.shl %s (i64.const 5)) %s) (i64.shr_u %s (i64.const 3)))' % (H, H, H),
     lambda h, c: (h << 5) + h + (h >> 3), RUNS, 0, init=77)

case('k2', 'two constants among the terms: one', 'i64',
     '(i64.add (i64.add (i64.add (i64.add (i64.shl %s (i64.const 5)) (i64.const 7)) %s) (i64.const 9)) %s)' % (H, C64, H),
     lambda h, c: h * 33 + 16 + c, RUNS, 1, [(r':\s+lsl\s', 0, 'loop'), (r'\sadd\s', 4, 'loop')], init=4)
case('invar', 'a term that is a sum made before the loop is one term: it is not taken apart into the loop', 'i64',
     '(i64.add (i64.add (i64.shl %s (i64.const 5)) (local.get $k)) %s)' % (H, H),
     lambda h, c, a, b: h * 33 + ((a + b) & M64), [(S0, 3, 4), (S1, 1 << 40, 5), (S2, 7, 7)], 1,
     [(r'\sadd\s', 3, 'loop')], init=2, params='(param $a i64) (param $b i64)', locals_='(local $k i64)',
     pre='(local.set $k (i64.add (local.get $a) (local.get $b)))')
case('mixed', 'a term that is an xor of a shift of h and the byte is no sum: one term, which does not read '
     'h itself, and the chain is left', 'i64',
     '(i64.add (i64.xor (i64.shl %s (i64.const 5)) %s) %s)' % (H, C64, H),
     lambda h, c: (((h << 5) & M64) ^ c) + h, RUNS, 0, init=6)
case('twicep', 'h is a term twice: left', 'i64',
     '(i64.add (i64.add %s %s) (i64.add (i64.shl %s (i64.const 5)) %s))' % (H, C64, H, H),
     lambda h, c: h + c + (h << 5) + h, RUNS, 0, init=8)


def shared_step(h, c, st):
    t = ((h << 5) + (h >> 7)) & M64
    st['g'] = st.get('g', 0) ^ t
    return t + c + h


case('shared', 'a sum of two shifts of h that something else reads too is one term, which does not read h itself', 'i64',
     '(i64.add (i64.add (local.tee $t (i64.add (i64.shl %s (i64.const 5)) (i64.shr_u %s (i64.const 7)))) %s) %s)'
     % (H, H, C64, H),
     shared_step, RUNS, 0, init=9, locals_='(local $t i64) (local $g i64)',
     more='(local.set $g (i64.xor (local.get $g) (local.get $t)))',
     result='(i64.add (local.get $h) (i64.mul (local.get $g) (i64.const 3)))',
     final=lambda h, st: h + st.get('g', 0) * 3)

case('amount', 'the term reads h as its second operand, the count of a shift', 'i64',
     '(i64.add (i64.add (i64.shl %s %s) %s) %s)' % (C64, H, C64, H),
     lambda h, c: ((c << (h & 63)) & M64) + c + h, RUNS, 1, init=1)

VEC = '''
  (func $vec (export "vec") (param $p i32) (result i32)
    (local $a v128) (local $c i32)
    (local.set $a (v128.const i32x4 1 2 3 4))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $a (i16x8.add (i16x8.add (i16x8.shl (local.get $a) (i32.const 3)) (i16x8.splat (local.get $c)))
                                 (local.get $a)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (i32.xor (i32.xor (i32x4.extract_lane 0 (local.get $a)) (i32x4.extract_lane 1 (local.get $a)))
             (i32.xor (i32x4.extract_lane 2 (local.get $a)) (i32x4.extract_lane 3 (local.get $a)))))
'''


def vec(p):
    lanes = [1, 0, 2, 0, 3, 0, 4, 0]          # i32x4 1 2 3 4 as eight 16-bit lanes
    for c in scan(p):
        lanes = [((x << 3) + c + x) & 0xffff for x in lanes]
    w = [lanes[2 * i] | lanes[2 * i + 1] << 16 for i in range(4)]
    return w[0] ^ w[1] ^ w[2] ^ w[3]


VECLANE = VEC.replace('$vec (export "vec")', '$veclane (export "veclane")').replace(
    '(i16x8.add (i16x8.add (i16x8.shl (local.get $a) (i32.const 3)) (i16x8.splat (local.get $c)))',
    '(i32x4.add (i16x8.add (i16x8.shl (local.get $a) (i32.const 3)) (i16x8.splat (local.get $c)))')


def veclane(p):
    lanes = [1, 0, 2, 0, 3, 0, 4, 0]
    for c in scan(p):
        t = [((x << 3) + c) & 0xffff for x in lanes]
        w = [((t[2 * i] | t[2 * i + 1] << 16) + (lanes[2 * i] | lanes[2 * i + 1] << 16)) & M32 for i in range(4)]
        lanes = [v for x in w for v in (x & 0xffff, x >> 16)]
    w = [lanes[2 * i] | lanes[2 * i + 1] << 16 for i in range(4)]
    return w[0] ^ w[1] ^ w[2] ^ w[3]


CASES.append(('vec', 'lanes of 16 bits in a vector: the terms after it are added in those lanes', VEC, vec, RUNS, 1, (), 'i32'))

VEC2 = VEC.replace('$vec (export "vec")', '$vec2 (export "vec2")').replace(
    '(i16x8.add (i16x8.add (i16x8.shl (local.get $a) (i32.const 3)) (i16x8.splat (local.get $c)))',
    '(i16x8.add (i16x8.add (i16x8.add (i16x8.shl (local.get $a) (i32.const 3))\n'
    '                                                       (i16x8.splat (i32.shl (local.get $c) (i32.const 1))))\n'
    '                                            (v128.load align=1 (local.get $p)))')
VEC3 = VEC.replace('$vec (export "vec")', '$vec3 (export "vec3")').replace(
    '(i16x8.add (i16x8.add (i16x8.shl (local.get $a) (i32.const 3)) (i16x8.splat (local.get $c)))',
    '(i16x8.add (i16x8.add (i16x8.add (i16x8.shl (local.get $a) (i32.const 3)) (i16x8.shr_u (local.get $a) (i32.const 2)))\n'
    '                                            (i16x8.splat (local.get $c)))')


def vec_model(f):
    def g(p):
        lanes = [1, 0, 2, 0, 3, 0, 4, 0]
        for c in scan(p):
            lanes = [f(x, c) & 0xffff for x in lanes]
        w = [lanes[2 * i] | lanes[2 * i + 1] << 16 for i in range(4)]
        return w[0] ^ w[1] ^ w[2] ^ w[3]
    return g


def vec2(p):
    lanes = [1, 0, 2, 0, 3, 0, 4, 0]
    for c in scan(p):
        at = (MEM + bytes(32))[p:p + 16]
        lanes = [((x << 3) + 2 * c + (at[2 * i] | at[2 * i + 1] << 8) + x) & 0xffff for i, x in enumerate(lanes)]
        p += 1
    w = [lanes[2 * i] | lanes[2 * i + 1] << 16 for i in range(4)]
    return w[0] ^ w[1] ^ w[2] ^ w[3]


CASES.append(('vec2', 'two terms of the vector\'s that do not read it, twice the byte in every lane and sixteen '
              'bytes of the string, whose sum carries out of a lane\'s low byte: added in its lanes', VEC2, vec2, RUNS, 1, (), 'i32'))
CASES.append(('vec3', 'two that do', VEC3,
              vec_model(lambda x, c: (x << 3) + (x >> 2) + c + x), RUNS, 1, (), 'i32'))
CASES.append(('veclane', 'a sum in lanes of 16 bits is one term of a sum in lanes of 32', VECLANE, veclane, RUNS, 0, (), 'i32'))

HEAD = """;; A value a loop carries round by a chain of one associative operation is
;; added last (opt.c pass_reassoc_carried), so that an iteration waits on
;; one link -- and after it the terms that read the value themselves, which
;; wait on it as the chain does: h * 33 + c is (c + h) + (h << 5), two adds
;; that each take in an extension or a shift, where (h << 5) + c added
;; first made the shift an instruction of its own.  Each function folds the
;; bytes of a string into h.  From test/gen_carried_dep.py, which works out
;; every answer.
;;
"""


def render(names=None, head=None):
    funcs, shapes, runs, lines = [], [], [], []
    tot = 0
    for name, what, wat, model, rs, want, shp, ty in CASES:
        if names and name not in names:
            continue
        funcs.append(wat.strip('\n') + '\n')
        tot += want
        l = ';;   %-8s %s' % (name, what)
        while len(l) > 78:
            cut = l.rfind(' ', 0, 78)
            lines.append(l[:cut])
            l = ';;' + ' ' * 11 + l[cut + 1:]
        lines.append(l)
        for pat, n, kind in shp:
            shapes.append(';; %s: %s %s=%d' % (kind, name, pat, n))
        for a in rs:
            r = model(*a)
            runs.append(';; run: %s %s => %s:%d' % (name, ' '.join(str(x) for x in a), ty, s64(r) if ty == 'i64' else s32(r)))
    data = ''.join('\\%02x' % b for b in MEM)
    return ((head or HEAD + '\n'.join(lines) + '\n;;\n') + ';; stats -O2: reassoc=%d\n' % tot + '\n'.join(shapes) + '\n;;\n' +
            '\n'.join(runs) + '\n\n(module\n  (memory 1)\n  (data (i32.const 0) "%s")\n' % data + ''.join(funcs) + ')\n')


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
            got = sum(int(x) for x in re.findall(r'\breassoc=(\d+)', r.stdout + r.stderr))
        mark = '' if got == want else '   <-- expected %d' % want
        if r.returncode:
            mark = '   <-- does not compile: ' + (r.stderr.strip().split('\n') or [''])[-1]
        bad += bool(mark)
        print('%-8s %d%s' % (name, got, mark))
    return bad


if __name__ == '__main__':
    if sys.argv[1] == '--each':
        sys.exit(1 if each(sys.argv[2:]) else 0)
    open(sys.argv[1], 'w').write(render())
    print('%s: %d functions' % (sys.argv[1], len(CASES)))
