;; An element at its array's address plus a value times its size (opt.c
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
;; stats -O2 a64: arr_bias_args=20 arr_biased=19
;; stats -O2 -fno-arr-bias a64: arr_bias_args=0 arr_biased=0
;; shape: get64 \[x\d+, w\d+, uxtw #3\]=1
;; shape: get64 add\s+x16, x\d+, w\d+, uxtw=0
;; shape: get64a \[x\d+, w\d+, uxtw #3\]=1
;; shape: wrapy \[x\d+, w\d+, uxtw #3\]=1
;; shape: wrapy mov\s+w\d+, w\d+$=0
;; shape: swap \[x\d+, w\d+, uxtw #3\]=5
;; shape: swap add\s+x16, x\d+, w\d+, uxtw=0
;; shape: get32 \[x\d+, w\d+, uxtw #2\]=1
;; shape: get16s ldrsh\s+w\d+, \[x\d+, w\d+, uxtw #1\]=1
;; shape: get16u ldrh\s+w\d+, \[x\d+, w\d+, uxtw #1\]=1
;; shape: get8s ldrsb\s+w\d+, \[x\d+, w\d+, uxtw\]=1
;; shape: get8u ldrb\s+w\d+, \[x\d+, w\d+, uxtw\]=1
;; shape: set32 str\s+w\d+, \[x\d+, w\d+, uxtw #2\]=1
;; shape: set32 \[x\d+, w\d+, uxtw #2\]=1
;; shape: set8 strb\s+w\d+, \[x\d+, w\d+, uxtw\]=1
;; shape: fget uxtw #3\]=0
;; shape: fget add\s+x16, x\d+, w\d+, uxtw=1
;; shape: fset uxtw #3\]=0
;; shape: rget uxtw #3\]=0
;; shape: rset uxtw #3\]=0
;; shape: off2 uxtw #3\]=0
;; shape: off2 add\s+x16, x\d+, w\d+, uxtw=1
;; shape: plain uxtw #3\]=0
;; shape: kept \[x\d+, w\d+, uxtw #3\]=1
;; shape: kept add\s+x16, x\d+, w\d+, uxtw=0
;; shape: spill uxtw #3\]=0
;; shape: spill add\s+x16, x\d+, w\d+, uxtw=1
;; shape: spill2 \[x\d+, w\d+, uxtw #3\]=1
;; shape: spill2 \[x\d+, w\d+, uxtw #2\]=1
;; shape: spill2 add\s+x16, x\d+, w\d+, uxtw=0
;;
;; run: get64 1 => i64:1000
;; run: get64 8 => i64:1049
;; run: get64 0 => trap: out of bounds array access
;; run: get64 9 => trap: out of bounds array access
;; run: get64 4294967297 => i64:1000
;; run: get64 4294967296 => trap: out of bounds array access
;; run: get64a 3 => i64:1114
;; run: get64a 0 => trap: out of bounds array access
;; run: get64a 4294967298 => i64:1107
;; run: wrapy 2 => i64:1002
;; run: wrapy 4294967304 => i64:1052
;; run: wrapy 0 => trap: out of bounds array access
;; run: swap 2 5 => i64:1028004091
;; run: swap 1 8 => i64:1049004147
;; run: swap 3 3 => i64:1014004056
;; run: swap 0 2 => i64:-1
;; run: swap 2 9 => i64:-1
;; run: get32 2 => i32:2000
;; run: get32 9 => i32:2035
;; run: get32 1 => trap: out of bounds array access
;; run: get32 10 => trap: out of bounds array access
;; run: get16s 4 => i32:-3
;; run: get16s 5 => i32:-25536
;; run: get16s 7 => i32:-32768
;; run: get16s 9 => i32:-1
;; run: get16s 3 => trap: out of bounds array access
;; run: get16u 4 => i32:65533
;; run: get16u 5 => i32:40000
;; run: get16u 7 => i32:32768
;; run: get16u 9 => i32:65535
;; run: get16u 12 => trap: out of bounds array access
;; run: get8s 8 => i32:-1
;; run: get8s 9 => i32:-56
;; run: get8s 11 => i32:-128
;; run: get8s 14 => i32:-1
;; run: get8s 7 => trap: out of bounds array access
;; run: get8u 8 => i32:255
;; run: get8u 9 => i32:200
;; run: get8u 11 => i32:128
;; run: get8u 14 => i32:255
;; run: get8u 24 => trap: out of bounds array access
;; run: set32 5 77 => i32:154
;; run: set32 2 9 => i32:2024
;; run: set32 1 5 => trap: out of bounds array access
;; run: set8 8 300 => i32:44044
;; run: set8 12 65 => i32:-935
;; run: set8 7 1 => trap: out of bounds array access
;; run: fget 1 => i32:3
;; run: fget 4 => i32:9
;; run: fget 0 => trap: out of bounds array access
;; run: fset 2 9 => i32:18
;; run: fset 4 3 => i32:5
;; run: fset 5 1 => trap: out of bounds array access
;; run: rget 1 => i32:11
;; run: rget 4 => i32:44
;; run: rget 5 => trap: out of bounds array access
;; run: rset 2 9 => i32:18
;; run: rset 3 5 => i32:27
;; run: rset 0 1 => trap: out of bounds array access
;; run: off2 2 => i64:1000
;; run: off2 9 => i64:1049
;; run: off2 1 => trap: out of bounds array access
;; run: plain 0 => i64:1000
;; run: plain 7 => i64:1049
;; run: plain 8 => trap: out of bounds array access
;; run: kept 1 => i32:665294
;; run: kept 8 => i32:2525383
;; run: kept 9 => trap: out of bounds array access
;; run: kept 0 => trap: out of bounds array access
;; run: spill 1 5 => i32:4651082
;; run: spill 8 -7 => i32:-1726149
;; run: spill 9 1 => trap: out of bounds array access
;; run: spill 0 2 => trap: out of bounds array access
;; run: spill2 2 5 => i32:4653089
;; run: spill2 8 -7 => i32:-1724119
;; run: spill2 9 1 => trap: out of bounds array access
;; run: spill2 1 2 => trap: out of bounds array access
;; run: spill2 0 3 => trap: out of bounds array access
(module
  (type $i64s (array (mut i64)))
  (type $i32s (array (mut i32)))
  (type $i16s (array (mut i16)))
  (type $i8s (array (mut i8)))
  (type $f64s (array (mut f64)))
  (type $refs (array (mut eqref)))
  (global $a64 (mut (ref $i64s)) (array.new_fixed $i64s 8 (i64.const 1000) (i64.const 1007) (i64.const 1014) (i64.const 1021) (i64.const 1028) (i64.const 1035) (i64.const 1042) (i64.const 1049)))
  (global $a32 (mut (ref $i32s)) (array.new_fixed $i32s 8 (i32.const 2000) (i32.const 2005) (i32.const 2010) (i32.const 2015) (i32.const 2020) (i32.const 2025) (i32.const 2030) (i32.const 2035)))
  (global $a16 (mut (ref $i16s)) (array.new_fixed $i16s 8 (i32.const -3) (i32.const 40000) (i32.const 7) (i32.const -32768) (i32.const 12) (i32.const 65535) (i32.const 1) (i32.const 300)))
  (global $a8 (mut (ref $i8s)) (array.new_fixed $i8s 16 (i32.const -1) (i32.const 200) (i32.const 3) (i32.const -128) (i32.const 5) (i32.const 127) (i32.const 255) (i32.const 9) (i32.const 10) (i32.const 11) (i32.const 12) (i32.const 13) (i32.const 14) (i32.const 15) (i32.const 16) (i32.const 17)))
  (global $af (mut (ref $f64s)) (array.new_fixed $f64s 4 (f64.const 1.5) (f64.const 2.5) (f64.const 3.5) (f64.const 4.5)))
  (global $ar (mut (ref $refs)) (array.new_fixed $refs 4 (ref.i31 (i32.const 11)) (ref.i31 (i32.const 22)) (ref.i31 (i32.const 33)) (ref.i31 (i32.const 44))))
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
    (local.set $s (call $mix (local.get $s) (i32.const 1) (local.get $y) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 2) (local.get $y) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 3) (local.get $y) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 4) (local.get $y) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 5) (local.get $y) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 6) (local.get $y) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 7) (local.get $y) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 8) (local.get $y) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 9) (local.get $y) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 10) (local.get $y) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 11) (local.get $y) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 12) (local.get $y) (i32.const 1)))
    (i32.add (local.get $s)
             (i32.wrap_i64 (array.get $i64s (global.get $a64) (i32.sub (local.get $y) (i32.const 1))))))
  (func $spill (export "spill") (param $y i32) (param $z i32) (result i32)
    (local $s i32) (local $i i32) (local $v0 i32) (local $v1 i32) (local $v2 i32) (local $v3 i32) (local $v4 i32) (local $v5 i32) (local $v6 i32) (local $v7 i32) (local $v8 i32) (local $v9 i32) (local $v10 i32) (local $v11 i32) (local $v12 i32) (local $v13 i32) (local $v14 i32) (local $v15 i32)
    (local.set $i (i32.sub (local.get $y) (i32.const 1)))
    (global.set $g (local.get $i))
    (local.set $v0 (i32.add (local.get $z) (i32.const 0)))
    (local.set $v1 (i32.add (local.get $z) (i32.const 1)))
    (local.set $v2 (i32.add (local.get $z) (i32.const 2)))
    (local.set $v3 (i32.add (local.get $z) (i32.const 3)))
    (local.set $v4 (i32.add (local.get $z) (i32.const 4)))
    (local.set $v5 (i32.add (local.get $z) (i32.const 5)))
    (local.set $v6 (i32.add (local.get $z) (i32.const 6)))
    (local.set $v7 (i32.add (local.get $z) (i32.const 7)))
    (local.set $v8 (i32.add (local.get $z) (i32.const 8)))
    (local.set $v9 (i32.add (local.get $z) (i32.const 9)))
    (local.set $v10 (i32.add (local.get $z) (i32.const 10)))
    (local.set $v11 (i32.add (local.get $z) (i32.const 11)))
    (local.set $v12 (i32.add (local.get $z) (i32.const 12)))
    (local.set $v13 (i32.add (local.get $z) (i32.const 13)))
    (local.set $v14 (i32.add (local.get $z) (i32.const 14)))
    (local.set $v15 (i32.add (local.get $z) (i32.const 15)))
    (local.set $s (call $mix (local.get $s) (i32.const 1) (i32.add (local.get $v2) (local.get $v3)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 2) (i32.add (local.get $v3) (local.get $v4)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 3) (i32.add (local.get $v4) (local.get $v5)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 4) (i32.add (local.get $v5) (local.get $v6)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 5) (i32.add (local.get $v6) (local.get $v7)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 6) (i32.add (local.get $v7) (local.get $v8)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 7) (i32.add (local.get $v8) (local.get $v9)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 8) (i32.add (local.get $v9) (local.get $v10)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 9) (i32.add (local.get $v10) (local.get $v11)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 10) (i32.add (local.get $v11) (local.get $v12)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 11) (i32.add (local.get $v12) (local.get $v13)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 12) (i32.add (local.get $v13) (local.get $v14)) (i32.const 1)))
    (global.set $g (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (local.get $v0) (local.get $v1)) (local.get $v2)) (local.get $v3)) (local.get $v4)) (local.get $v5)) (local.get $v6)) (local.get $v7)) (local.get $v8)) (local.get $v9)) (local.get $v10)) (local.get $v11)) (local.get $v12)) (local.get $v13)) (local.get $v14)) (local.get $v15)))
    (i32.add (local.get $s) (i32.wrap_i64 (array.get $i64s (global.get $a64) (local.get $i)))))
  (func $spill2 (export "spill2") (param $y i32) (param $z i32) (result i32)
    (local $s i32) (local $i i32) (local $j i32) (local $v0 i32) (local $v1 i32) (local $v2 i32) (local $v3 i32) (local $v4 i32) (local $v5 i32) (local $v6 i32) (local $v7 i32) (local $v8 i32) (local $v9 i32) (local $v10 i32) (local $v11 i32) (local $v12 i32) (local $v13 i32) (local $v14 i32) (local $v15 i32)
    (local.set $i (i32.sub (local.get $y) (i32.const 1)))
    (local.set $j (i32.sub (local.get $y) (i32.const 2)))
    (global.set $g (i32.mul (local.get $i) (local.get $j)))
    (local.set $v0 (i32.add (local.get $z) (i32.const 0)))
    (local.set $v1 (i32.add (local.get $z) (i32.const 1)))
    (local.set $v2 (i32.add (local.get $z) (i32.const 2)))
    (local.set $v3 (i32.add (local.get $z) (i32.const 3)))
    (local.set $v4 (i32.add (local.get $z) (i32.const 4)))
    (local.set $v5 (i32.add (local.get $z) (i32.const 5)))
    (local.set $v6 (i32.add (local.get $z) (i32.const 6)))
    (local.set $v7 (i32.add (local.get $z) (i32.const 7)))
    (local.set $v8 (i32.add (local.get $z) (i32.const 8)))
    (local.set $v9 (i32.add (local.get $z) (i32.const 9)))
    (local.set $v10 (i32.add (local.get $z) (i32.const 10)))
    (local.set $v11 (i32.add (local.get $z) (i32.const 11)))
    (local.set $v12 (i32.add (local.get $z) (i32.const 12)))
    (local.set $v13 (i32.add (local.get $z) (i32.const 13)))
    (local.set $v14 (i32.add (local.get $z) (i32.const 14)))
    (local.set $v15 (i32.add (local.get $z) (i32.const 15)))
    (local.set $s (call $mix (local.get $s) (i32.const 1) (i32.add (local.get $v2) (local.get $v3)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 2) (i32.add (local.get $v3) (local.get $v4)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 3) (i32.add (local.get $v4) (local.get $v5)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 4) (i32.add (local.get $v5) (local.get $v6)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 5) (i32.add (local.get $v6) (local.get $v7)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 6) (i32.add (local.get $v7) (local.get $v8)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 7) (i32.add (local.get $v8) (local.get $v9)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 8) (i32.add (local.get $v9) (local.get $v10)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 9) (i32.add (local.get $v10) (local.get $v11)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 10) (i32.add (local.get $v11) (local.get $v12)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 11) (i32.add (local.get $v12) (local.get $v13)) (i32.const 1)))
    (local.set $s (call $mix (local.get $s) (i32.const 12) (i32.add (local.get $v13) (local.get $v14)) (i32.const 1)))
    (global.set $g (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (i32.xor (local.get $v0) (local.get $v1)) (local.get $v2)) (local.get $v3)) (local.get $v4)) (local.get $v5)) (local.get $v6)) (local.get $v7)) (local.get $v8)) (local.get $v9)) (local.get $v10)) (local.get $v11)) (local.get $v12)) (local.get $v13)) (local.get $v14)) (local.get $v15)))
    (i32.add (local.get $s)
      (i32.add (i32.wrap_i64 (array.get $i64s (global.get $a64) (local.get $i)))
               (array.get $i32s (global.get $a32) (local.get $j)))))
)
