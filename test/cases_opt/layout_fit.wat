;; A loop too big to be a small one (isel.c small_loop) whose hot way
;; round fits in a 64-byte fetch block is placed so that it does
;; (loop_hot_span, hot_pad): the layout puts a loop's likelier ways first,
;; so its first blocks up to the first jump back are what runs on most
;; trips.  $dead is gobench/utf8scan's byte loop, its arms for the longer
;; characters after the way back: entered by a jump into it, the padding
;; ahead of it never runs, so any amount is free and no branch goes over it
;; (ends_dead; dead_pads); $live16's loop is entered by falling into it and
;; takes the 16 bytes of nops that fit it, and $live32's, 16 bytes further
;; on, would take 32 and a branch over them on every entry -- more than a
;; loop entered often and run a few trips gains (asbench/strswitch, 7%) --
;; so it keeps the plain alignment.  $calls is $dead with a call in an arm:
;; a loop that calls waits on the call, not on fetching itself, and is not
;; placed -- but $before's, whose call comes ahead of the loop, is.
;; $fib2's way round ends with the moves of its back edge and a
;; jump to the block right after them, the next instruction: left out
;; (produce_body; next_jumps); so is the jump of $twoexit's way out to
;; where both ways out meet, past the block between that makes no code.
;; $sw's table ends with two entries for the block right after it: those
;; are no jump, and stay.  $nest's outer loop, too big to be small, goes
;; round through its inner loop, which is placed as a small one: the outer
;; loop's way round is no row of its own blocks, and it is not placed.
;;
;; stats -O2 a64: hot_fits=3 dead_pads=2 next_jumps=2
;; shape: dead \sb\s=5
;;
;; run: dead 16 36 => i32:23930
;; run: dead 16 18 => i32:3
;; run: dead 18 22 => i32:176
;; run: dead 22 31 => i32:23669
;; run: dead 30 30 => i32:0
;; run: calls 16 36 => i32:23663
;; run: calls 16 18 => i32:3
;; run: calls 18 22 => i32:8
;; run: calls 22 31 => i32:23669
;; run: calls 30 30 => i32:0
;; run: live16 16 36 => i32:1109
;; run: live16 16 18 => i32:195
;; run: live16 18 22 => i32:261
;; run: live16 30 30 => i32:0
;; run: live32 16 36 => i32:1109
;; run: live32 16 18 => i32:195
;; run: live32 18 22 => i32:261
;; run: live32 30 30 => i32:0
;; run: fib2 0 => i32:1
;; run: fib2 1 => i32:8
;; run: fib2 5 => i32:43
;; run: fib2 20 => i32:58301
;; run: twoexit 0 => i32:1000
;; run: twoexit 1 => i32:1996
;; run: twoexit 2 => i32:2992
;; run: twoexit 3 => i32:3984
;; run: twoexit 7 => i32:3984
;; run: sw 0 5 => i32:16
;; run: sw 4 100 => i32:729
;; run: sw 9 5 => i32:124
;; run: sw 10 5 => i32:20
;; run: sw 11 5 => i32:20
;; run: sw 12 5 => i32:20
;; run: nest 0 => i32:82
;; run: nest 1 => i32:82
;; run: nest 2 => i32:168
;; run: nest 5 => i32:4002
;; run: nest 30 => i32:-809112936
;; run: before 16 36 => i32:23919
;; run: before 16 18 => i32:4
;; run: before 18 22 => i32:177
;; run: before 22 31 => i32:23670
;; run: before 30 30 => i32:1
;; run: before 0 0 => i32:0
;; Answers worked out in Python.

(module
  (memory 1)
  (data (i32.const 16) "ab\c3\a9cd\e2\82\acef\f0\9f\98\80gh\d0\b4z")
  ;; g(x): x with its bits above 1 halved away, recursive so that it is
  ;; called, not inlined: 0 or 1
  (func $g (param $x i32) (result i32)
    (if (result i32) (i32.gt_u (local.get $x) (i32.const 1))
      (then (call $g (i32.shr_u (local.get $x) (i32.const 1))))
      (else (local.get $x))))
  (func $dead (export "dead") (param $p i32) (param $n i32) (result i32)
    (local $c i32) (local $b i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $p) (local.get $n)))
        (local.set $b (i32.load8_s (local.get $p)))
        (if (i32.lt_s (local.get $b) (i32.const 0))
          (then
            (if (i32.eq (i32.and (local.get $b) (i32.const 0xe0)) (i32.const 0xc0))
              (then (local.set $c (i32.add (local.get $c) (i32.load8_u offset=1 (local.get $p))))
                    (local.set $p (i32.add (local.get $p) (i32.const 2)))
                    (br $l)))
            (local.set $c (i32.xor (local.get $c) (i32.load16_u offset=1 (local.get $p))))
            (local.set $p (i32.add (local.get $p) (i32.const 3)))
            (br $l)))
        (local.set $c (i32.add (local.get $c) (i32.and (local.get $b) (i32.const 7))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.get $c))
  (func $live16 (export "live16") (param $p i32) (param $n i32) (result i32)
    (local $c i32) (local $b i32)
    (i32.store offset=512 (i32.const 0) (local.get $n))
    (i32.store offset=516 (i32.const 0) (local.get $n))
    (i32.store offset=520 (i32.const 0) (local.get $n))
    (i32.store offset=524 (i32.const 0) (local.get $n))
    (i32.store offset=528 (i32.const 0) (local.get $n))
    (i32.store offset=532 (i32.const 0) (local.get $n))
    (i32.store offset=536 (i32.const 0) (local.get $n))
    (i32.store offset=540 (i32.const 0) (local.get $n))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $p) (local.get $n)))
        (local.set $b (i32.load8_s (local.get $p)))
        (if (i32.lt_s (local.get $b) (i32.const 0))
          (then (local.set $c (i32.add (local.get $c)
                  (i32.xor (i32.and (local.get $b) (i32.const 0x3f)) (i32.const 0x55))))))
        (local.set $c (i32.add (local.get $c) (local.get $b)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.get $c))
  (func $live32 (export "live32") (param $p i32) (param $n i32) (result i32)
    (local $c i32) (local $b i32)
    (i32.store offset=512 (i32.const 0) (local.get $n))
    (i32.store offset=516 (i32.const 0) (local.get $n))
    (i32.store offset=520 (i32.const 0) (local.get $n))
    (i32.store offset=524 (i32.const 0) (local.get $n))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $p) (local.get $n)))
        (local.set $b (i32.load8_s (local.get $p)))
        (if (i32.lt_s (local.get $b) (i32.const 0))
          (then (local.set $c (i32.add (local.get $c)
                  (i32.xor (i32.and (local.get $b) (i32.const 0x3f)) (i32.const 0x55))))))
        (local.set $c (i32.add (local.get $c) (local.get $b)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.get $c))
  (func $calls (export "calls") (param $p i32) (param $n i32) (result i32)
    (local $c i32) (local $b i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $p) (local.get $n)))
        (local.set $b (i32.load8_s (local.get $p)))
        (if (i32.lt_s (local.get $b) (i32.const 0))
          (then
            (if (i32.eq (i32.and (local.get $b) (i32.const 0xe0)) (i32.const 0xc0))
              (then (local.set $c (i32.add (local.get $c) (call $g (i32.load8_u offset=1 (local.get $p)))))
                    (local.set $p (i32.add (local.get $p) (i32.const 2)))
                    (br $l)))
            (local.set $c (i32.xor (local.get $c) (i32.load16_u offset=1 (local.get $p))))
            (local.set $p (i32.add (local.get $p) (i32.const 3)))
            (br $l)))
        (local.set $c (i32.add (local.get $c) (i32.and (local.get $b) (i32.const 7))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.get $c))
  (func $fib2 (export "fib2") (param $n i32) (result i32)
    (local $a i32) (local $b i32) (local $t i32)
    (local.set $b (i32.const 1))
    (block $done
      (loop $l
        (br_if $done (i32.le_s (local.get $n) (i32.const 0)))
        (local.set $t (i32.add (local.get $a) (local.get $b)))
        (local.set $a (local.get $b))
        (local.set $b (local.get $t))
        (local.set $n (i32.sub (local.get $n) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $a) (i32.const 7)) (local.get $b)))
  (global $gl (mut i32) (i32.const 0))
  (func $twoexit (export "twoexit") (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (global.set $gl (i32.const 1000))
    (block $out
      (block $done
        (loop $l
          (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
          (global.set $gl (i32.sub (global.get $gl) (i32.const 4)))
          (local.set $s (i32.add (local.get $s) (global.get $gl)))
          (br_if $out (i32.eq (local.get $i) (i32.const 2)))
          (global.set $gl (i32.add (global.get $gl) (i32.const 4)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $l))))
    (i32.add (local.get $s) (global.get $gl)))
  (func $sw (export "sw") (param $x i32) (param $y i32) (result i32)
    (block $d (block $c9 (block $c8 (block $c7 (block $c6 (block $c5 (block $c4 (block $c3 (block $c2 (block $c1 (block $c0 
      (br_table $c0 $c1 $c2 $c3 $c4 $c5 $c6 $c7 $c8 $c9 $d $d $d (local.get $x)))
      (return (i32.add (i32.mul (local.get $y) (i32.const 3)) (i32.const 1))))
      (return (i32.add (i32.mul (local.get $y) (i32.const 4)) (i32.const 8))))
      (return (i32.add (i32.mul (local.get $y) (i32.const 5)) (i32.const 15))))
      (return (i32.add (i32.mul (local.get $y) (i32.const 6)) (i32.const 22))))
      (return (i32.add (i32.mul (local.get $y) (i32.const 7)) (i32.const 29))))
      (return (i32.add (i32.mul (local.get $y) (i32.const 8)) (i32.const 36))))
      (return (i32.add (i32.mul (local.get $y) (i32.const 9)) (i32.const 43))))
      (return (i32.add (i32.mul (local.get $y) (i32.const 10)) (i32.const 50))))
      (return (i32.add (i32.mul (local.get $y) (i32.const 11)) (i32.const 57))))
      (return (i32.add (i32.mul (local.get $y) (i32.const 12)) (i32.const 64))))
    (i32.shl (local.get $y) (i32.const 2)))
  (func $nest (export "nest") (param $n i32) (result i32)
    (local $x i32) (local $i i32) (local $j i32)
    (i32.store offset=512 (i32.const 0) (local.get $n))
    (i32.store offset=520 (i32.const 0) (local.get $n))
    (i32.store offset=528 (i32.const 0) (local.get $n))
    (i32.store offset=536 (i32.const 0) (local.get $n))
    (i32.store offset=544 (i32.const 0) (local.get $n))
    (i32.store offset=552 (i32.const 0) (local.get $n))
    (loop $o
      (local.set $x (i32.add (i32.mul (local.get $x) (i32.const 3)) (i32.const 7)))
      (local.set $j (i32.const 0))
      (loop $in
        (local.set $x (i32.xor (local.get $x) (i32.const 0x55)))
        (br_if $in (i32.lt_u (local.tee $j (i32.add (local.get $j) (i32.const 1))) (i32.const 3))))
      (br_if $o (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (local.get $n))))
    (local.get $x))
  (func $before (export "before") (param $p i32) (param $n i32) (result i32)
    (local $c i32) (local $b i32)
    (local.set $c (call $g (local.get $n)))
    (i32.store offset=512 (i32.const 0) (local.get $n))
    (i32.store offset=520 (i32.const 0) (local.get $n))
    (i32.store offset=528 (i32.const 0) (local.get $n))
    (i32.store offset=536 (i32.const 0) (local.get $n))
    (i32.store offset=544 (i32.const 0) (local.get $n))
    (i32.store offset=552 (i32.const 0) (local.get $n))
    (i32.store offset=560 (i32.const 0) (local.get $n))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $p) (local.get $n)))
        (local.set $b (i32.load8_s (local.get $p)))
        (if (i32.lt_s (local.get $b) (i32.const 0))
          (then
            (if (i32.eq (i32.and (local.get $b) (i32.const 0xe0)) (i32.const 0xc0))
              (then (local.set $c (i32.add (local.get $c) (i32.load8_u offset=1 (local.get $p))))
                    (local.set $p (i32.add (local.get $p) (i32.const 2)))
                    (br $l)))
            (local.set $c (i32.xor (local.get $c) (i32.load16_u offset=1 (local.get $p))))
            (local.set $p (i32.add (local.get $p) (i32.const 3)))
            (br $l)))
        (local.set $c (i32.add (local.get $c) (i32.and (local.get $b) (i32.const 7))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.get $c))
)
