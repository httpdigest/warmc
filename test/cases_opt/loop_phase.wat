;; A function placed past the start of its 64-byte block so that the small
;; loop of its deepest nest fits in one block with the least padding run on
;; its entry (isel.c loop_phase; codegen.c places it).  The inner loop below,
;; 28 bytes, is entered once each time round the outer one; from the
;; function's start it came at 44 mod 64, where it took 20 bytes of padding
;; and a branch over them to fit, where starting the function 16 bytes into
;; its block brings it to 60, and a nop fits it at the block's start.  Its
;; first instruction, the load, is at a 64-byte boundary in the program.
;; A phase is for the deepest nest, two deep or more, and only when every
;; loop in it is a small one: $once's inner loop needs no padding where it
;; is, and the branch over its outer loop's is run once a call; $flat's one
;; loop, at depth one, is entered once a call; $mixed's inner nest holds a
;; loop too big to be measured, which a phase could move anywhere -- and
;; none of the three is given one.
;;
;; stats -O2 a64: loop_phases=1
;;
;; shape: scan \tb\t=1
;; shape: scan nop$=1
;; shape: scan [048c]0:\s+ldrb=1
;; shape: once \tb\t=2
;; shape: flat \tb\t=2
;; shape: mixed \tb\t=3
;;
;; run: scan 13 => i32:458
;; run: scan 3 => i32:10
;; run: scan 7 => i32:93
;; run: scan 8 => i32:264
;; run: once 13 => i32:17
;; run: once 3 => i32:5
;; run: flat 13 => i32:10
;; run: mixed 13 => i32:-892258946
;; run: mixed 3 => i32:-901180716
(module
  (memory 1)
  (data (i32.const 0) "\00\00\05\00\00\00\07\03\00\00\00\00\02")
  ;; the nonzero bytes from 0 on, the last at n - 1 or past it, summed with
  ;; their offsets weighted, the zeros skipped over counted
  (func $scan (export "scan") (param $n i32) (result i32)
    (local $j i32) (local $s i32) (local $c i32) (local $d i32)
    (loop $outer
      (local.set $s (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 3))))
      (local.set $s (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 5))))
      (local.set $s (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 7))))
      (local.set $s (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 9))))
      (block $found
        (loop $inner
          (br_if $found (i32.load8_u (local.get $j)))
          (local.set $j (i32.add (local.get $j) (i32.const 1)))
          (local.set $c (i32.add (local.get $c) (i32.const 1)))
          (local.set $d (i32.xor (local.get $d) (local.get $j)))
          (br $inner)))
      (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $j))))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br_if $outer (i32.lt_u (local.get $j) (local.get $n))))
    (i32.add (local.get $s) (i32.add (local.get $c) (local.get $d))))
  (func $once (export "once") (param $n i32) (result i32)
    (local $j i32) (local $s i32)
    (i32.store offset=2048 (i32.const 0) (i32.add (local.get $n) (i32.const 1)))
    (i32.store offset=2052 (i32.const 0) (i32.add (local.get $n) (i32.const 8)))
    (i32.store offset=2056 (i32.const 0) (i32.add (local.get $n) (i32.const 15)))
    (i32.store offset=2060 (i32.const 0) (i32.add (local.get $n) (i32.const 22)))
    (loop $outer
      (block $found
        (loop $inner
          (br_if $found (i32.load8_u (local.get $j)))
          (local.set $j (i32.add (local.get $j) (i32.const 1)))
          (br $inner)))
      (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $j))))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br_if $outer (i32.lt_u (local.get $j) (local.get $n))))
    (local.get $s))
  (func $flat (export "flat") (param $n i32) (result i32)
    (local $j i32) (local $c i32) (local $d i32)
    (i32.store offset=2048 (i32.const 0) (i32.add (local.get $n) (i32.const 1)))
    (i32.store offset=2052 (i32.const 0) (i32.add (local.get $n) (i32.const 8)))
    (i32.store offset=2056 (i32.const 0) (i32.add (local.get $n) (i32.const 15)))
    (block $found
      (loop $skip
        (br_if $found (i32.load8_u (local.get $j)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (local.set $c (i32.add (local.get $c) (i32.const 1)))
        (local.set $d (i32.xor (local.get $d) (local.get $j)))
        (br $skip)))
    (i32.add (i32.load8_u (local.get $j)) (i32.add (local.get $c) (local.get $d))))
  (func $mixed (export "mixed") (param $n i32) (result i32)
    (local $j i32) (local $s i32) (local $c i32) (local $d i32) (local $i i32) (local $t i32)
    (loop $outer
      (local.set $s (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 3))))
      (block $found
        (loop $inner
          (br_if $found (i32.load8_u (local.get $j)))
          (local.set $j (i32.add (local.get $j) (i32.const 1)))
          (local.set $c (i32.add (local.get $c) (i32.const 1)))
          (local.set $d (i32.xor (local.get $d) (local.get $j)))
          (br $inner)))
      (local.set $i (i32.const 0))
      (loop $big
          (local.set $t (i32.add (i32.rotl (local.get $t) (i32.const 1)) (local.get $i)))
          (local.set $t (i32.add (i32.rotl (local.get $t) (i32.const 2)) (local.get $i)))
          (local.set $t (i32.add (i32.rotl (local.get $t) (i32.const 3)) (local.get $i)))
          (local.set $t (i32.add (i32.rotl (local.get $t) (i32.const 4)) (local.get $i)))
          (local.set $t (i32.add (i32.rotl (local.get $t) (i32.const 5)) (local.get $i)))
          (local.set $t (i32.add (i32.rotl (local.get $t) (i32.const 6)) (local.get $i)))
          (local.set $t (i32.add (i32.rotl (local.get $t) (i32.const 7)) (local.get $i)))
          (local.set $t (i32.add (i32.rotl (local.get $t) (i32.const 8)) (local.get $i)))
        (br_if $big (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 3))))
      (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $j))))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br_if $outer (i32.lt_u (local.get $j) (local.get $n))))
    (i32.add (local.get $s) (i32.add (local.get $c) (i32.add (local.get $d) (local.get $t)))))
)
