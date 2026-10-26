;; memory.copy and memory.fill of memory 0 on AArch64 save nothing round
;; them (regalloc.inc memop_preserves): the values a loop carries round one
;; stay in caller-saved registers, and no callee-saved register is saved
;; for them ($copyloop, $fillloop).  A constant length up to 96 bytes is
;; made inline in v26-v31 and is no call at all, so a function with no other
;; is a leaf, with no frame and no stack check ($c96, $f96); a longer one is
;; a bl of the routine, its length made in the routine's register in place
;; ($c97, $f97).  Another memory's ops are the runtime's calls as before
;; ($copy1, $copy01, $fill1).
;;
;; Every op checks both ranges against the memory's size before a byte
;; moves: one ending at the last byte works and one byte further traps, an
;; empty one at the end works and one past it traps, and operands whose sum
;; passes 2^32 trap.  That for each way the op is made: a constant length
;; inline, a variable one up to 16 inline, the routines' shapes up to 64,
;; their loops, and on Darwin libSystem's from 65536 bytes (a copy) or 1024
;; (a fill).  Each answers the byte at the destination, 99 for an empty op.
;;
;; The command line hands an i32 parameter its argument's whole 64 bits, as
;; a C caller may (AAPCS64 leaves the high half unspecified).  $spilled's
;; operands are spilled as they came, and its length reloaded whole into
;; x14, where a mov makes its high half zero before the bl (isel.c
;; memop_r_operand), as the routines take it.  It is $copyloop's loop with
;; thirty more values, and answers what it would for the low halves.
;;
;; shape: copyloop warmc_memory_copy=0
;; shape: copyloop \b[xw](19|2[0-8])\b, \[sp=0
;; shape: copyloop \b[xw]2[1-8]\b=0
;; shape: fillloop warmc_memory_fill=0
;; shape: fillloop \b[xw]2[1-8]\b=0
;; shape: c96 x29=0
;; shape: c96 ldp\tq26, q27=1
;; shape: c96 ldp\tq30, q31=1
;; shape: c96 \tbl\t=0
;; shape: f96 x29=0
;; shape: f96 dup.*v26=1
;; shape: f96 \tbl\t=0
;; shape: c97 bl\t.*warmc_memmove_r=1
;; shape: c97 mov\tw14, #0x61=1
;; shape: c97 mov\tw\d+, #0x61=1
;; shape: f97 bl\t.*warmc_memset_r=1
;; shape: f97 mov\tw14, #0x61=1
;; shape: copy1 bl\t.*warmc_memory_copy=1
;; shape: copy1 warmc_memmove_r=0
;; shape: copy01 bl\t.*warmc_memory_copy=1
;; shape: copy01 warmc_memmove_r=0
;; shape: fill1 bl\t.*warmc_memory_fill=1
;; shape: fill1 warmc_memset_r=0
;; shape: spilled mov\tw14, w14=2
;;
;; run: copyloop 1000 2000 100 7 => i32:362
;; run: fillloop 1000 90 100 7 => i32:992
;; run: copy 131071 0 1 => i32:17
;; run: copy 131072 0 1 => trap: out of bounds memory access
;; run: copy 0 131071 1 => i32:224
;; run: copy 0 131072 1 => trap: out of bounds memory access
;; run: fill 131071 90 1 => i32:90
;; run: fill 131072 90 1 => trap: out of bounds memory access
;; run: copy 131056 0 16 => i32:17
;; run: copy 131057 0 16 => trap: out of bounds memory access
;; run: copy 0 131056 16 => i32:239
;; run: copy 0 131057 16 => trap: out of bounds memory access
;; run: fill 131056 90 16 => i32:90
;; run: fill 131057 90 16 => trap: out of bounds memory access
;; run: copy 131055 0 17 => i32:17
;; run: copy 131056 0 17 => trap: out of bounds memory access
;; run: copy 0 131055 17 => i32:0
;; run: copy 0 131056 17 => trap: out of bounds memory access
;; run: fill 131055 90 17 => i32:90
;; run: fill 131056 90 17 => trap: out of bounds memory access
;; run: copy 131008 0 64 => i32:17
;; run: copy 131009 0 64 => trap: out of bounds memory access
;; run: copy 0 131008 64 => i32:0
;; run: copy 0 131009 64 => trap: out of bounds memory access
;; run: fill 131008 90 64 => i32:90
;; run: fill 131009 90 64 => trap: out of bounds memory access
;; run: copy 131007 0 65 => i32:17
;; run: copy 131008 0 65 => trap: out of bounds memory access
;; run: copy 0 131007 65 => i32:0
;; run: copy 0 131008 65 => trap: out of bounds memory access
;; run: fill 131007 90 65 => i32:90
;; run: fill 131008 90 65 => trap: out of bounds memory access
;; run: copy 130072 0 1000 => i32:17
;; run: copy 130073 0 1000 => trap: out of bounds memory access
;; run: copy 0 130072 1000 => i32:0
;; run: copy 0 130073 1000 => trap: out of bounds memory access
;; run: fill 130072 90 1000 => i32:90
;; run: fill 130073 90 1000 => trap: out of bounds memory access
;; run: copy 65536 0 65536 => i32:17
;; run: copy 65537 0 65536 => trap: out of bounds memory access
;; run: copy 0 65536 65536 => i32:0
;; run: copy 0 65537 65536 => trap: out of bounds memory access
;; run: fill 65536 90 65536 => i32:90
;; run: fill 65537 90 65536 => trap: out of bounds memory access
;; run: copy 61072 0 70000 => i32:17
;; run: copy 61073 0 70000 => trap: out of bounds memory access
;; run: copy 0 61072 70000 => i32:0
;; run: copy 0 61073 70000 => trap: out of bounds memory access
;; run: fill 61072 90 70000 => i32:90
;; run: fill 61073 90 70000 => trap: out of bounds memory access
;; run: c96 130976 0 => i32:17
;; run: c96 130977 0 => trap: out of bounds memory access
;; run: c96 0 130976 => i32:0
;; run: c96 0 130977 => trap: out of bounds memory access
;; run: c97 130975 0 => i32:17
;; run: c97 130976 0 => trap: out of bounds memory access
;; run: c97 0 130975 => i32:0
;; run: c97 0 130976 => trap: out of bounds memory access
;; run: f96 130976 90 => i32:90
;; run: f96 130977 90 => trap: out of bounds memory access
;; run: f97 130975 90 => i32:90
;; run: f97 130976 90 => trap: out of bounds memory access
;; run: copy 131072 131072 0 => i32:99
;; run: copy 131073 0 0 => trap: out of bounds memory access
;; run: copy 0 131073 0 => trap: out of bounds memory access
;; run: fill 131072 90 0 => i32:99
;; run: fill 131073 90 0 => trap: out of bounds memory access
;; run: copy 4294967295 0 2 => trap: out of bounds memory access
;; run: copy 0 4294967280 32 => trap: out of bounds memory access
;; run: copy 0 0 4294967295 => trap: out of bounds memory access
;; run: fill 4294967295 90 1 => trap: out of bounds memory access
;; run: fill 1 90 4294967295 => trap: out of bounds memory access
;; run: copy1 65000 0 536 => i32:119
;; run: copy1 65001 0 536 => trap: out of bounds memory access
;; run: copy01 5 0 1 => i32:119
;; run: copy01 5 65535 2 => trap: out of bounds memory access
;; run: fill1 65535 90 1 => i32:90
;; run: fill1 65535 90 2 => trap: out of bounds memory access
;; run: spilled 4294967552 8589934592 12884901952 5 3 => i64:2990
(module
  (memory 2)
  (memory $m1 1)
  (data (i32.const 0) "\11\22\33\44\55\66\77\88\99\aa\bb\cc\dd\ee\ff\01")
  (data (i32.const 131056) "\ef\ee\ed\ec\eb\ea\e9\e8\e7\e6\e5\e4\e3\e2\e1\e0")
  (data (memory $m1) (i32.const 0) "\77")
  (func $copyloop (export "copyloop") (param $d i32) (param $s i32) (param $n i32) (param $k i32) (result i32)
    (local $a i32) (local $b i32) (local $c i32)
    (loop $l
      (local.set $a (i32.add (local.get $a) (local.get $k)))
      (local.set $b (i32.xor (local.get $b) (local.get $a)))
      (local.set $c (i32.add (local.get $c) (i32.mul (local.get $b) (i32.const 3))))
      (memory.copy (local.get $d) (local.get $s) (local.get $n))
      (local.set $c (i32.add (local.get $c) (i32.load8_u (local.get $d))))
      (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1)))))
    (i32.add (i32.add (local.get $a) (local.get $b)) (local.get $c)))
  (func $fillloop (export "fillloop") (param $d i32) (param $v i32) (param $n i32) (param $k i32) (result i32)
    (local $a i32) (local $b i32) (local $c i32)
    (loop $l
      (local.set $a (i32.add (local.get $a) (local.get $k)))
      (local.set $b (i32.xor (local.get $b) (local.get $a)))
      (local.set $c (i32.add (local.get $c) (i32.mul (local.get $b) (i32.const 3))))
      (memory.fill (local.get $d) (local.get $v) (local.get $n))
      (local.set $c (i32.add (local.get $c) (i32.load8_u (local.get $d))))
      (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1)))))
    (i32.add (i32.add (local.get $a) (local.get $b)) (local.get $c)))
  (func $copy (export "copy") (param $d i32) (param $s i32) (param $n i32) (result i32)
    (memory.copy (local.get $d) (local.get $s) (local.get $n))
    (if (result i32) (local.get $n)
      (then (i32.load8_u (local.get $d))) (else (i32.const 99))))
  (func $fill (export "fill") (param $d i32) (param $v i32) (param $n i32) (result i32)
    (memory.fill (local.get $d) (local.get $v) (local.get $n))
    (if (result i32) (local.get $n)
      (then (i32.load8_u (local.get $d))) (else (i32.const 99))))
  (func $c96 (export "c96") (param $d i32) (param $s i32) (result i32)
    (memory.copy (local.get $d) (local.get $s) (i32.const 96))
    (i32.load8_u (local.get $d)))
  (func $c97 (export "c97") (param $d i32) (param $s i32) (result i32)
    (memory.copy (local.get $d) (local.get $s) (i32.const 97))
    (i32.load8_u (local.get $d)))
  (func $f96 (export "f96") (param $d i32) (param $v i32) (result i32)
    (memory.fill (local.get $d) (local.get $v) (i32.const 96))
    (i32.load8_u (local.get $d)))
  (func $f97 (export "f97") (param $d i32) (param $v i32) (result i32)
    (memory.fill (local.get $d) (local.get $v) (i32.const 97))
    (i32.load8_u (local.get $d)))
  (func $copy1 (export "copy1") (param $d i32) (param $s i32) (param $n i32) (result i32)
    (memory.copy $m1 $m1 (local.get $d) (local.get $s) (local.get $n))
    (i32.load8_u $m1 (local.get $d)))
  (func $copy01 (export "copy01") (param $d i32) (param $s i32) (param $n i32) (result i32)
    (memory.copy 0 $m1 (local.get $d) (local.get $s) (local.get $n))
    (i32.load8_u (local.get $d)))
  (func $fill1 (export "fill1") (param $d i32) (param $v i32) (param $n i32) (result i32)
    (memory.fill $m1 (local.get $d) (local.get $v) (local.get $n))
    (i32.load8_u $m1 (local.get $d)))
  (func $spilled (export "spilled") (param $d i32) (param $s i32) (param $n i32) (param $x i64) (param $k i32) (result i64)
    (local $a0 i64) (local $a1 i64) (local $a2 i64) (local $a3 i64) (local $a4 i64) (local $a5 i64) (local $a6 i64) (local $a7 i64) (local $a8 i64) (local $a9 i64) (local $a10 i64) (local $a11 i64) (local $a12 i64) (local $a13 i64) (local $a14 i64) (local $a15 i64) (local $a16 i64) (local $a17 i64) (local $a18 i64) (local $a19 i64) (local $a20 i64) (local $a21 i64) (local $a22 i64) (local $a23 i64) (local $a24 i64) (local $a25 i64) (local $a26 i64) (local $a27 i64) (local $a28 i64) (local $a29 i64)
    (local.set $a0 (i64.mul (local.get $x) (i64.const 3)))
    (local.set $a1 (i64.mul (local.get $x) (i64.const 5)))
    (local.set $a2 (i64.mul (local.get $x) (i64.const 7)))
    (local.set $a3 (i64.mul (local.get $x) (i64.const 9)))
    (local.set $a4 (i64.mul (local.get $x) (i64.const 11)))
    (local.set $a5 (i64.mul (local.get $x) (i64.const 13)))
    (local.set $a6 (i64.mul (local.get $x) (i64.const 15)))
    (local.set $a7 (i64.mul (local.get $x) (i64.const 17)))
    (local.set $a8 (i64.mul (local.get $x) (i64.const 19)))
    (local.set $a9 (i64.mul (local.get $x) (i64.const 21)))
    (local.set $a10 (i64.mul (local.get $x) (i64.const 23)))
    (local.set $a11 (i64.mul (local.get $x) (i64.const 25)))
    (local.set $a12 (i64.mul (local.get $x) (i64.const 27)))
    (local.set $a13 (i64.mul (local.get $x) (i64.const 29)))
    (local.set $a14 (i64.mul (local.get $x) (i64.const 31)))
    (local.set $a15 (i64.mul (local.get $x) (i64.const 33)))
    (local.set $a16 (i64.mul (local.get $x) (i64.const 35)))
    (local.set $a17 (i64.mul (local.get $x) (i64.const 37)))
    (local.set $a18 (i64.mul (local.get $x) (i64.const 39)))
    (local.set $a19 (i64.mul (local.get $x) (i64.const 41)))
    (local.set $a20 (i64.mul (local.get $x) (i64.const 43)))
    (local.set $a21 (i64.mul (local.get $x) (i64.const 45)))
    (local.set $a22 (i64.mul (local.get $x) (i64.const 47)))
    (local.set $a23 (i64.mul (local.get $x) (i64.const 49)))
    (local.set $a24 (i64.mul (local.get $x) (i64.const 51)))
    (local.set $a25 (i64.mul (local.get $x) (i64.const 53)))
    (local.set $a26 (i64.mul (local.get $x) (i64.const 55)))
    (local.set $a27 (i64.mul (local.get $x) (i64.const 57)))
    (local.set $a28 (i64.mul (local.get $x) (i64.const 59)))
    (local.set $a29 (i64.mul (local.get $x) (i64.const 61)))
    (loop $l
      (memory.copy (local.get $d) (local.get $s) (local.get $n))
      (memory.fill (local.get $s) (local.get $d) (local.get $n))
      (local.set $a0 (i64.add (local.get $a0) (local.get $a1)))
      (local.set $a1 (i64.add (local.get $a1) (local.get $a2)))
      (local.set $a2 (i64.add (local.get $a2) (local.get $a3)))
      (local.set $a3 (i64.add (local.get $a3) (local.get $a4)))
      (local.set $a4 (i64.add (local.get $a4) (local.get $a5)))
      (local.set $a5 (i64.add (local.get $a5) (local.get $a6)))
      (local.set $a6 (i64.add (local.get $a6) (local.get $a7)))
      (local.set $a7 (i64.add (local.get $a7) (local.get $a8)))
      (local.set $a8 (i64.add (local.get $a8) (local.get $a9)))
      (local.set $a9 (i64.add (local.get $a9) (local.get $a10)))
      (local.set $a10 (i64.add (local.get $a10) (local.get $a11)))
      (local.set $a11 (i64.add (local.get $a11) (local.get $a12)))
      (local.set $a12 (i64.add (local.get $a12) (local.get $a13)))
      (local.set $a13 (i64.add (local.get $a13) (local.get $a14)))
      (local.set $a14 (i64.add (local.get $a14) (local.get $a15)))
      (local.set $a15 (i64.add (local.get $a15) (local.get $a16)))
      (local.set $a16 (i64.add (local.get $a16) (local.get $a17)))
      (local.set $a17 (i64.add (local.get $a17) (local.get $a18)))
      (local.set $a18 (i64.add (local.get $a18) (local.get $a19)))
      (local.set $a19 (i64.add (local.get $a19) (local.get $a20)))
      (local.set $a20 (i64.add (local.get $a20) (local.get $a21)))
      (local.set $a21 (i64.add (local.get $a21) (local.get $a22)))
      (local.set $a22 (i64.add (local.get $a22) (local.get $a23)))
      (local.set $a23 (i64.add (local.get $a23) (local.get $a24)))
      (local.set $a24 (i64.add (local.get $a24) (local.get $a25)))
      (local.set $a25 (i64.add (local.get $a25) (local.get $a26)))
      (local.set $a26 (i64.add (local.get $a26) (local.get $a27)))
      (local.set $a27 (i64.add (local.get $a27) (local.get $a28)))
      (local.set $a28 (i64.add (local.get $a28) (local.get $a29)))
      (local.set $a29 (i64.add (local.get $a29) (local.get $a0)))
      (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1)))))
    (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.xor (i64.load8_u (local.get $d)) (local.get $a0)) (local.get $a1)) (local.get $a2)) (local.get $a3)) (local.get $a4)) (local.get $a5)) (local.get $a6)) (local.get $a7)) (local.get $a8)) (local.get $a9)) (local.get $a10)) (local.get $a11)) (local.get $a12)) (local.get $a13)) (local.get $a14)) (local.get $a15)) (local.get $a16)) (local.get $a17)) (local.get $a18)) (local.get $a19)) (local.get $a20)) (local.get $a21)) (local.get $a22)) (local.get $a23)) (local.get $a24)) (local.get $a25)) (local.get $a26)) (local.get $a27)) (local.get $a28)) (local.get $a29)))
)
