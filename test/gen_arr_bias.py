"""test/cases_opt/arr_bias.wat: an array's element read or written at its
array's address plus a value times its size, where its index is that value
less the elements the array's header is long (opt.c pass_array_bias, the
operand it gives the access; isel_ops.inc's array.get and array.set, which
take the value's register where one has it).  The module's header says what
each function is; every answer is worked out here.

  python3 test/gen_arr_bias.py test/cases_opt/arr_bias.wat
"""
import struct
import sys

M32, M64 = 0xffffffff, 0xffffffffffffffff
A64 = [1000 + 7 * i for i in range(8)]
A32 = [2000 + 5 * i for i in range(8)]
A16 = [-3, 40000, 7, -32768, 12, 65535, 1, 300]
A8 = [-1, 200, 3, -128, 5, 127, 255, 9, 10, 11, 12, 13, 14, 15, 16, 17]
AF = [1.5, 2.5, 3.5, 4.5]
AR = [11, 22, 33, 44]
OOB = 'trap: out of bounds array access'


def s8(v): return struct.unpack('<b', struct.pack('<B', v & 0xff))[0]
def s16(v): return struct.unpack('<h', struct.pack('<H', v & 0xffff))[0]
def s32(v): return struct.unpack('<i', struct.pack('<I', v & M32))[0]
def s64(v): return struct.unpack('<q', struct.pack('<Q', v & M64))[0]


class Trap(Exception):
    pass


def at(arr, idx):
    idx &= M32
    if idx >= len(arr):
        raise Trap()
    return idx


def get64(y): return A64[at(A64, y - 1)]
def get64a(y): return A64[at(A64, (y & M32) - 1)] + 100
def wrapy(z): return A64[at(A64, (z & M32) - 1)] ^ 5
def off2(y): return A64[at(A64, y - 2)]
def plain(i): return A64[at(A64, i)]
def get32(y): return A32[at(A32, y - 2)]
def get16s(y): return s16(A16[at(A16, y - 4)])
def get16u(y): return A16[at(A16, y - 4)] & 0xffff
def get8s(y): return s8(A8[at(A8, y - 8)])
def get8u(y): return A8[at(A8, y - 8)] & 0xff
def fget(y): return int(AF[at(AF, y - 1)] * 2)
def rget(y): return AR[at(AR, y - 1)]


def swap(i, j):
    a = A64[:]
    if (i - 1) & M64 >= len(a) or (j - 1) & M64 >= len(a):
        return -1
    a[i - 1], a[j - 1] = a[j - 1], a[i - 1]
    return a[i - 1] * 1000003 + a[j - 1]


def set32(y, v):
    a = A32[:]
    a[at(a, y - 2)] = v
    return s32(a[3] + a[at(a, y - 2)])


def set8(y, v):
    a = A8[:]
    a[at(a, y - 8)] = v
    return (a[at(a, y - 8)] & 0xff) + 1000 * s8(a[0])


def fset(y, v):
    a = AF[:]
    a[at(a, y - 1)] = float(v)
    return int(a[1] + a[at(a, y - 1)])


def rset(y, v):
    a = AR[:]
    a[at(a, y - 1)] = v
    return a[1] + a[at(a, y - 1)]


def kept(y):
    # y an argument of every call: in a register all the way
    s = 0
    for k in range(1, 13):
        s = (s * 3 + k + y) & M32
    return s32(s + A64[at(A64, y - 1)])


def spill(y, z):
    # the calls' arguments are other values, and y is in no register
    s = 0
    for k in range(1, 13):
        s = (s * 3 + k + (z + k + 1) + (z + k + 2)) & M32
    return s32(s + A64[at(A64, y - 1)])


def spill2(y, z):
    # y read by two accesses after the calls: kept in a register for both
    s = 0
    for k in range(1, 13):
        s = (s * 3 + k + (z + k + 1) + (z + k + 2)) & M32
    a = A64[at(A64, y - 1)]
    return s32(s + a + A32[at(A32, y - 2)])


RUNS = (('get64', get64, 'i64', [(1,), (8,), (0,), (9,), (4294967297,), (4294967296,)]),
        ('get64a', get64a, 'i64', [(3,), (0,), (4294967298,)]),
        ('wrapy', wrapy, 'i64', [(2,), (4294967304,), (0,)]),
        ('swap', swap, 'i64', [(2, 5), (1, 8), (3, 3), (0, 2), (2, 9)]),
        ('get32', get32, 'i32', [(2,), (9,), (1,), (10,)]),
        ('get16s', get16s, 'i32', [(4,), (5,), (7,), (9,), (3,)]),
        ('get16u', get16u, 'i32', [(4,), (5,), (7,), (9,), (12,)]),
        ('get8s', get8s, 'i32', [(8,), (9,), (11,), (14,), (7,)]),
        ('get8u', get8u, 'i32', [(8,), (9,), (11,), (14,), (24,)]),
        ('set32', set32, 'i32', [(5, 77), (2, 9), (1, 5)]),
        ('set8', set8, 'i32', [(8, 300), (12, 65), (7, 1)]),
        ('fget', fget, 'i32', [(1,), (4,), (0,)]),
        ('fset', fset, 'i32', [(2, 9), (4, 3), (5, 1)]),
        ('rget', rget, 'i32', [(1,), (4,), (5,)]),
        ('rset', rset, 'i32', [(2, 9), (3, 5), (0, 1)]),
        ('off2', off2, 'i64', [(2,), (9,), (1,)]),
        ('plain', plain, 'i64', [(0,), (7,), (8,)]),
        ('kept', kept, 'i32', [(1,), (8,), (9,), (0,)]),
        ('spill', spill, 'i32', [(1, 5), (8, -7), (9, 1), (0, 2)]),
        ('spill2', spill2, 'i32', [(2, 5), (8, -7), (9, 1), (1, 2), (0, 3)]))

HEAD = ''';; An element at its array's address plus a value times its size (opt.c
;; pass_array_bias; isel_ops.inc's array.get and array.set).  An array's
;; elements start 8 bytes after its address, so one is at address + 8 +
;; index * size: an add and the load or the store on arm64.  Where the index
;; is a value less the elements those 8 bytes are -- 1 of 8 bytes, 2 of 4, 4
;; of 2, 8 of 1 -- the element is at address + value * size, and the access,
;; given the value as an operand of its own, takes the value's register
;; where one has it: one instruction.  Integer elements alone.
;;
;;   get64   a[y - 1] of 64-bit elements, the index the wrap of a 64-bit
;;           y - 1: the check of its bound is of the index, the load of y.
;;           y of 0 and of 2^32 are the index 0xffffffff, out of bounds; y
;;           of 2^32 + 1 is the index 0
;;   get64a  the index made by adding -1 to the wrap of y, and 100 added to
;;           the element
;;   wrapy   the index a 32-bit difference of the wrap of y: the wrap is no
;;           instruction, the load reads y's low half
;;   swap    Lua's a[i], a[j] = a[j], a[i] behind its own tests of i - 1 and
;;           j - 1 against the length, 64-bit as luawasm's are: two loads
;;           and two stores by i and j, and of the two reads for the answer
;;           a[i]'s, which the store to a[j] may have changed; a[j]'s is
;;           the value stored
;;   get32   a[y - 2] of 32-bit elements
;;   get16s get16u get8s get8u
;;           a[y - 4] of 16 bits and a[y - 8] of 8, sign- and zero-extended
;;   set32 set8
;;           stores, read back by the same index and by a constant one: the
;;           32-bit read is the value stored, the 8-bit one a load
;;   fget fset rget rset
;;           floats and references: as they were, an add and the access
;;   off2    a[y - 2] of 64-bit elements, 8 bytes off: as it was
;;   plain   a[i]: as it was
;;   kept    a[y - 1] read after a dozen calls that each take y: y is in a
;;           register that the calls keep, and the load is by it
;;   spill   the index y - 1 made first and stored to a global, then a dozen
;;           calls that take two each of sixteen values made of z, all alive
;;           to the end; then a[index].  y, which nothing but the load reads
;;           after the calls, is in no register there, and the load is by
;;           the index: the one read brings no reload of y
;;   spill2  the same with two indexes, y - 1 into 64-bit elements and y - 2
;;           into 32-bit ones: y, read by both loads, is worth a register
;;           the calls keep, and both are by it
;;
;; Answers by the functions as Python has them; a run of each out of bounds.
;;
;; stats -O2 a64: arr_bias_args=%(ARGS)s arr_biased=%(MADE)s
;; stats -O2 -fno-arr-bias a64: arr_bias_args=0 arr_biased=0
%(SHAPES)s;;
'''

MOD = '''(module
  (type $i64s (array (mut i64)))
  (type $i32s (array (mut i32)))
  (type $i16s (array (mut i16)))
  (type $i8s (array (mut i8)))
  (type $f64s (array (mut f64)))
  (type $refs (array (mut eqref)))
  (global $a64 (mut (ref $i64s)) (array.new_fixed $i64s 8 %(A64)s))
  (global $a32 (mut (ref $i32s)) (array.new_fixed $i32s 8 %(A32)s))
  (global $a16 (mut (ref $i16s)) (array.new_fixed $i16s 8 %(A16)s))
  (global $a8 (mut (ref $i8s)) (array.new_fixed $i8s 16 %(A8)s))
  (global $af (mut (ref $f64s)) (array.new_fixed $f64s 4 %(AF)s))
  (global $ar (mut (ref $refs)) (array.new_fixed $refs 4 %(AR)s))
  (global $g (mut i32) (i32.const 0))
  ;; 3 s + k + y, at the end of a recursion so that it stays a call
  (func $mix (param $s i32) (param $k i32) (param $y i32) (param $d i32) (result i32)
    (if (result i32) (i32.eqz (local.get $d))
      (then (i32.add (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $k)) (local.get $y)))
      (else (call $mix (local.get $s) (local.get $k) (local.get $y) (i32.sub (local.get $d) (i32.const 1))))))
  (func $get64 (export "get64") (param $y i64) (result i64)
    (array.get $i64s (global.get $a64) (i32.wrap_i64 (i64.sub (local.get $y) (i64.const 1)))))
  (func $get64a (export "get64a") (param $y i64) (result i64)
    (i64.add (array.get $i64s (global.get $a64) (i32.add (i32.wrap_i64 (local.get $y)) (i32.const -1)))
             (i64.const 100)))
  (func $wrapy (export "wrapy") (param $z i64) (result i64)
    (i64.xor (array.get $i64s (global.get $a64) (i32.sub (i32.wrap_i64 (local.get $z)) (i32.const 1)))
             (i64.const 5)))
  (func $swap (export "swap") (param $i i64) (param $j i64) (result i64)
    (local $a (ref $i64s)) (local $n i64) (local $t i64)
    (local.set $a (global.get $a64))
    (local.set $n (i64.extend_i32_u (array.len (local.get $a))))
    (if (i64.ge_u (i64.sub (local.get $i) (i64.const 1)) (local.get $n)) (then (return (i64.const -1))))
    (if (i64.ge_u (i64.sub (local.get $j) (i64.const 1)) (local.get $n)) (then (return (i64.const -1))))
    (local.set $t (array.get $i64s (local.get $a) (i32.wrap_i64 (i64.sub (local.get $i) (i64.const 1)))))
    (array.set $i64s (local.get $a) (i32.wrap_i64 (i64.sub (local.get $i) (i64.const 1)))
      (array.get $i64s (local.get $a) (i32.wrap_i64 (i64.sub (local.get $j) (i64.const 1)))))
    (array.set $i64s (local.get $a) (i32.wrap_i64 (i64.sub (local.get $j) (i64.const 1))) (local.get $t))
    (i64.add (i64.mul (array.get $i64s (local.get $a) (i32.wrap_i64 (i64.sub (local.get $i) (i64.const 1))))
                      (i64.const 1000003))
             (array.get $i64s (local.get $a) (i32.wrap_i64 (i64.sub (local.get $j) (i64.const 1))))))
  (func $get32 (export "get32") (param $y i32) (result i32)
    (array.get $i32s (global.get $a32) (i32.sub (local.get $y) (i32.const 2))))
  (func $get16s (export "get16s") (param $y i32) (result i32)
    (array.get_s $i16s (global.get $a16) (i32.sub (local.get $y) (i32.const 4))))
  (func $get16u (export "get16u") (param $y i32) (result i32)
    (array.get_u $i16s (global.get $a16) (i32.sub (local.get $y) (i32.const 4))))
  (func $get8s (export "get8s") (param $y i32) (result i32)
    (array.get_s $i8s (global.get $a8) (i32.sub (local.get $y) (i32.const 8))))
  (func $get8u (export "get8u") (param $y i32) (result i32)
    (array.get_u $i8s (global.get $a8) (i32.sub (local.get $y) (i32.const 8))))
  (func $set32 (export "set32") (param $y i32) (param $v i32) (result i32)
    (array.set $i32s (global.get $a32) (i32.sub (local.get $y) (i32.const 2)) (local.get $v))
    (i32.add (array.get $i32s (global.get $a32) (i32.const 3))
             (array.get $i32s (global.get $a32) (i32.sub (local.get $y) (i32.const 2)))))
  (func $set8 (export "set8") (param $y i32) (param $v i32) (result i32)
    (array.set $i8s (global.get $a8) (i32.sub (local.get $y) (i32.const 8)) (local.get $v))
    (i32.add (array.get_u $i8s (global.get $a8) (i32.sub (local.get $y) (i32.const 8)))
             (i32.mul (array.get_s $i8s (global.get $a8) (i32.const 0)) (i32.const 1000))))
  (func $fget (export "fget") (param $y i32) (result i32)
    (i32.trunc_f64_s (f64.mul (array.get $f64s (global.get $af) (i32.sub (local.get $y) (i32.const 1)))
                              (f64.const 2))))
  (func $fset (export "fset") (param $y i32) (param $v i32) (result i32)
    (array.set $f64s (global.get $af) (i32.sub (local.get $y) (i32.const 1)) (f64.convert_i32_s (local.get $v)))
    (i32.trunc_f64_s (f64.add (array.get $f64s (global.get $af) (i32.const 1))
                              (array.get $f64s (global.get $af) (i32.sub (local.get $y) (i32.const 1))))))
  (func $rget (export "rget") (param $y i32) (result i32)
    (i31.get_s (ref.cast (ref i31) (array.get $refs (global.get $ar) (i32.sub (local.get $y) (i32.const 1))))))
  (func $rset (export "rset") (param $y i32) (param $v i32) (result i32)
    (array.set $refs (global.get $ar) (i32.sub (local.get $y) (i32.const 1)) (ref.i31 (local.get $v)))
    (i32.add (i31.get_s (ref.cast (ref i31) (array.get $refs (global.get $ar) (i32.const 1))))
             (i31.get_s (ref.cast (ref i31) (array.get $refs (global.get $ar) (i32.sub (local.get $y) (i32.const 1)))))))
  (func $off2 (export "off2") (param $y i64) (result i64)
    (array.get $i64s (global.get $a64) (i32.wrap_i64 (i64.sub (local.get $y) (i64.const 2)))))
  (func $plain (export "plain") (param $i i32) (result i64)
    (array.get $i64s (global.get $a64) (local.get $i)))
  (func $kept (export "kept") (param $y i32) (result i32)
    (local $s i32)
%(KEPT_BODY)s    (i32.add (local.get $s)
             (i32.wrap_i64 (array.get $i64s (global.get $a64) (i32.sub (local.get $y) (i32.const 1))))))
  (func $spill (export "spill") (param $y i32) (param $z i32) (result i32)
    (local $s i32) (local $i i32)%(SPILL_LOCALS)s
    (local.set $i (i32.sub (local.get $y) (i32.const 1)))
    (global.set $g (local.get $i))
%(SPILL_BODY)s    (i32.add (local.get $s) (i32.wrap_i64 (array.get $i64s (global.get $a64) (local.get $i)))))
  (func $spill2 (export "spill2") (param $y i32) (param $z i32) (result i32)
    (local $s i32) (local $i i32) (local $j i32)%(SPILL_LOCALS)s
    (local.set $i (i32.sub (local.get $y) (i32.const 1)))
    (local.set $j (i32.sub (local.get $y) (i32.const 2)))
    (global.set $g (i32.mul (local.get $i) (local.get $j)))
%(SPILL_BODY)s    (i32.add (local.get $s)
      (i32.add (i32.wrap_i64 (array.get $i64s (global.get $a64) (local.get $i)))
               (array.get $i32s (global.get $a32) (local.get $j)))))
)
'''


def main():
    runs = []
    for name, fn, ty, argl in RUNS:
        for args in argl:
            try:
                r = fn(*args)
                ans = '%s:%d' % (ty, s64(r) if ty == 'i64' else s32(r))
            except Trap:
                ans = OOB
            runs.append(';; run: %s %s => %s' % (name, ' '.join(map(str, args)), ans))
    kept = ''.join('    (local.set $s (call $mix (local.get $s) (i32.const %d) (local.get $y) (i32.const 1)))\n' % k
                   for k in range(1, 13))
    # spill: sixteen values made of z, v[q] = z + q, two of them an argument of each call (as
    # their sum) and all of them alive to the end, where their xor goes to a global
    nl = 16
    locs = ''.join(' (local $v%d i32)' % q for q in range(nl))
    body = ''.join('    (local.set $v%d (i32.add (local.get $z) (i32.const %d)))\n' % (q, q) for q in range(nl))
    for k in range(1, 13):
        body += ('    (local.set $s (call $mix (local.get $s) (i32.const %d) (i32.add (local.get $v%d) (local.get $v%d))'
                 ' (i32.const 1)))\n' % (k, k + 1, k + 2))
    keep = '(local.get $v0)'
    for q in range(1, nl):
        keep = '(i32.xor %s (local.get $v%d))' % (keep, q)
    body += '    (global.set $g %s)\n' % keep
    stats = dict(l.split('=') for l in (sys.argv[2:] or ['ARGS=?', 'MADE=?']))
    shapes = ''.join(';; shape: %s\n' % l for l in SHAPES)
    mod = MOD % {'A64': ' '.join('(i64.const %d)' % v for v in A64),
                 'A32': ' '.join('(i32.const %d)' % v for v in A32),
                 'A16': ' '.join('(i32.const %d)' % v for v in A16),
                 'A8': ' '.join('(i32.const %d)' % v for v in A8),
                 'AF': ' '.join('(f64.const %s)' % v for v in AF),
                 'AR': ' '.join('(ref.i31 (i32.const %d))' % v for v in AR),
                 'SPILL_LOCALS': locs, 'SPILL_BODY': body, 'KEPT_BODY': kept}
    open(sys.argv[1], 'w').write(HEAD % {'ARGS': stats['ARGS'], 'MADE': stats['MADE'], 'SHAPES': shapes} +
                                 '\n'.join(runs) + '\n' + mod)


# what the code is on arm64, by function: the element's access with the
# value's register and its size's shift, and the add that is gone or stays
RX = r'\[x\d+, w\d+, uxtw #%d\]'
ADD = r'add\s+x16, x\d+, w\d+, uxtw'
SHAPES = ['get64 ' + RX % 3 + '=1', 'get64 ' + ADD + '=0',
          'get64a ' + RX % 3 + '=1',
          'wrapy ' + RX % 3 + '=1', r'wrapy mov\s+w\d+, w\d+$=0',
          'swap ' + RX % 3 + '=5', 'swap ' + ADD + '=0',
          'get32 ' + RX % 2 + '=1',
          r'get16s ldrsh\s+w\d+, \[x\d+, w\d+, uxtw #1\]=1', r'get16u ldrh\s+w\d+, \[x\d+, w\d+, uxtw #1\]=1',
          r'get8s ldrsb\s+w\d+, \[x\d+, w\d+, uxtw\]=1', r'get8u ldrb\s+w\d+, \[x\d+, w\d+, uxtw\]=1',
          r'set32 str\s+w\d+, \[x\d+, w\d+, uxtw #2\]=1', 'set32 ' + RX % 2 + '=1',
          r'set8 strb\s+w\d+, \[x\d+, w\d+, uxtw\]=1',
          r'fget uxtw #3\]=0', 'fget ' + ADD + '=1', r'fset uxtw #3\]=0',
          r'rget uxtw #3\]=0', r'rset uxtw #3\]=0',
          r'off2 uxtw #3\]=0', 'off2 ' + ADD + '=1', r'plain uxtw #3\]=0',
          'kept ' + RX % 3 + '=1', 'kept ' + ADD + '=0', r'spill uxtw #3\]=0', 'spill ' + ADD + '=1',
          'spill2 ' + RX % 3 + '=1', 'spill2 ' + RX % 2 + '=1', 'spill2 ' + ADD + '=0']

if __name__ == '__main__':
    main()
