;; A function placed past the start of its 64-byte block so that the small
;; loop of its deepest nest fits in one block with the least padding run on
;; its entry (isel.c loop_phase; codegen.c places it).  The inner loop below,
;; 24 bytes, is entered once each time round the outer one, whose latch is
;; laid out above its header (ir.c rotate_loops), the entry branching over
;; it and the nops that align it; from the function's start the inner loop
;; came at 48 mod 64, where it took 16 bytes of padding to fit, where
;; starting the function 16 bytes into its block brings it to the block's
;; start with none.  Its first instruction, the load, is at a 64-byte
;; boundary in the program.
;; A phase is for the deepest nest, two deep or more, and only when every
;; loop in it is a small one: $once's inner loop needs no padding where it
;; is, and the branch over its outer loop's is run once a call; $flat's one
;; loop, at depth one, is entered once a call; $mixed's inner nest holds a
;; loop too big to be measured, which a phase could move anywhere -- and
;; none of the three is given one.  $rot's inner loop, fmodf's (see
;; loop_top.wat), starts at its latch, laid out above its header, and is
;; the small loop its phase places.
;;
;; stats -O2 a64: loop_phases=2
;;
;; shape: scan \tb\t=2
;; shape: scan nop$=3
;; shape: scan [048c]0:\s+ldrb=1
;; shape: once \tb\t=2
;; shape: flat \tb\t=2
;; shape: mixed \tb\t=3
;;
;; run: scan 13 => i32:1466
;; run: scan 3 => i32:10
;; run: scan 7 => i32:261
;; run: scan 8 => i32:824
;; run: once 13 => i32:17
;; run: once 3 => i32:5
;; run: flat 13 => i32:10
;; run: mixed 13 => i32:-892258946
;; run: mixed 3 => i32:-901180716
;; run: rot 1 => i32:2318
;; run: rot 2 => i32:4700
;; run: rot 9 => i32:23166
;; run: rot 30 => i32:97380
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
      (local.set $s (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 11))))
      (local.set $s (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 13))))
      (local.set $s (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 15))))
      (local.set $s (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 17))))
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
  ;; for each r below n, r + 50 run through fmodf's loop against 7 for six
  ;; steps, what is left summed; the sum so far where a step leaves nothing
  (func $rot (export "rot") (param $n i32) (result i32)
    (local $r i32) (local $s i32) (local $x i32) (local $t i32) (local $e i32)
    (loop $outer
      (local.set $x (i32.add (local.get $r) (i32.const 50)))
      (local.set $e (i32.const 6))
      (loop $l
        (local.set $t (i32.sub (local.get $x) (i32.const 7)))
        (if (i32.ge_s (local.get $t) (i32.const 0))
          (then
            (if (i32.eqz (local.get $t)) (then (return (local.get $s))))
            (local.set $x (local.get $t))))
        (local.set $x (i32.shl (local.get $x) (i32.const 1)))
        (local.set $e (i32.sub (local.get $e) (i32.const 1)))
        (br_if $l (i32.gt_s (local.get $e) (i32.const 0))))
      (local.set $s (i32.add (local.get $s) (local.get $x)))
      (local.set $r (i32.add (local.get $r) (i32.const 1)))
      (br_if $outer (i32.lt_u (local.get $r) (local.get $n))))
    (local.get $s))
)
