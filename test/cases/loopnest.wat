;; A loop that runs a constant few times with a loop inside it, laid out that
;; many times over (pass_unroll_nest), and what that leaves for the passes
;; after it: inner loops with constant trips, unrolled by the whole trip so
;; that they run once (pass_once_loops), and constant addresses spelled two
;; ways -- a pointer made for a run of globals before the unroll, and the
;; constants the copies compute -- which forwarding has to see as one slot.
;; Each export is that shape, the way C's nbody has it, around values that
;; leave the loop, guards that fold, counters that run down, step by two or
;; are 64-bit; the last one keeps more floats live than there are registers.
;; The values come from an exported mutable global so nothing folds them away
;; first.

(module
  (memory 2)
  (global $seed (export "seed") (mut i32) (i32.const 0x9e37_79b9))
  (global $fseed (export "fseed") (mut f64) (f64.const 1.25))
  (data (i32.const 68000) "\01\00\00\00\02\00\00\00\03\00\00\00\04\00\00\00\05\00\00\00\06\00\00\00")

  ;; bodies at a high constant address, a pairwise walk inside a walk of 5
  (func $pairs (param $steps i32) (result f64)
    (local $s i32) (local $i i32) (local $j i32) (local $a i32) (local $b i32)
    (local $d f64) (local $last f64)
    (local.set $i (i32.const 0))
    (loop $init
      (f64.store offset=68400 (i32.mul (local.get $i) (i32.const 24))
                 (f64.convert_i32_s (i32.add (local.get $i) (i32.wrap_i64 (i64.extend_i32_u (global.get $seed))))))
      (f64.store offset=68408 (i32.mul (local.get $i) (i32.const 24)) (f64.const 0))
      (f64.store offset=68416 (i32.mul (local.get $i) (i32.const 24))
                 (f64.add (global.get $fseed) (f64.convert_i32_s (local.get $i))))
      (br_if $init (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 5))))
    (loop $step
      (local.set $i (i32.const 0))
      (loop $outer
        (if (i32.le_u (local.get $i) (i32.const 3))
          (then
            (local.set $a (i32.mul (local.get $i) (i32.const 24)))
            (local.set $j (i32.add (local.get $i) (i32.const 1)))
            (loop $inner
              (local.set $b (i32.mul (local.get $j) (i32.const 24)))
              (local.set $d (f64.sub (f64.load offset=68400 (local.get $a))
                                     (f64.load offset=68400 (local.get $b))))
              (local.set $last (f64.div (f64.load offset=68416 (local.get $b))
                                        (f64.add (f64.mul (local.get $d) (local.get $d)) (f64.const 1))))
              (f64.store offset=68408 (local.get $a)
                         (f64.sub (f64.load offset=68408 (local.get $a)) (f64.mul (local.get $d) (local.get $last))))
              (f64.store offset=68408 (local.get $b)
                         (f64.add (f64.load offset=68408 (local.get $b)) (f64.mul (local.get $d) (local.get $last))))
              (br_if $inner (i32.ne (local.tee $j (i32.add (local.get $j) (i32.const 1))) (i32.const 5))))))
        (br_if $outer (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 5))))
      (local.set $i (i32.const 0))
      (loop $move
        (f64.store offset=68400 (i32.mul (local.get $i) (i32.const 24))
                   (f64.add (f64.load offset=68400 (i32.mul (local.get $i) (i32.const 24)))
                            (f64.mul (f64.const 0.01) (f64.load offset=68408 (i32.mul (local.get $i) (i32.const 24))))))
        (br_if $move (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 5))))
      (br_if $step (i32.ne (local.tee $s (i32.add (local.get $s) (i32.const 1))) (local.get $steps))))
    ;; $last escapes the nest: the last pair's value
    (f64.add (local.get $last)
             (f64.add (f64.load offset=68408 (i32.const 0)) (f64.load offset=68400 (i32.const 96)))))

  (func (export "pairwise_walk") (result f64)
    (call $pairs (i32.const 37)))

  ;; counters that run down, step by two, and are 64-bit; a sum leaves the nest
  (func (export "counters_of_each_kind") (result i64)
    (local $r i32) (local $i i32) (local $k i64) (local $j i32) (local $acc i64) (local $x i32)
    (loop $rep
      (local.set $i (i32.const 6))
      (loop $down
        (local.set $j (local.get $i))
        (loop $in
          (local.set $acc (i64.add (i64.mul (local.get $acc) (i64.const 3))
                                   (i64.extend_i32_u (i32.load offset=68000
                                     (i32.shl (i32.rem_u (i32.add (local.get $j) (local.get $r)) (i32.const 6)) (i32.const 2))))))
          (br_if $in (i32.gt_s (local.tee $j (i32.sub (local.get $j) (i32.const 1))) (i32.const 0))))
        (br_if $down (i32.ne (local.tee $i (i32.sub (local.get $i) (i32.const 2))) (i32.const 0))))
      (local.set $k (i64.const 0))
      (loop $wide
        (local.set $x (i32.wrap_i64 (local.get $k)))
        (loop $in2
          (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 5)) (i64.extend_i32_s (local.get $x))))
          (br_if $in2 (i32.lt_u (local.tee $x (i32.add (local.get $x) (i32.const 1))) (i32.const 3))))
        (br_if $wide (i64.lt_u (local.tee $k (i64.add (local.get $k) (i64.const 1))) (i64.const 3))))
      (br_if $rep (i32.ne (local.tee $r (i32.add (local.get $r) (i32.const 1))) (i32.const 9))))
    (local.get $acc))

  ;; a loop that runs once, and one whose trip is the unroll factor
  (func (export "loops_that_run_once") (result i32)
    (local $i i32) (local $acc i32) (local $t i32) (local $r i32)
    (loop $rep
      (local.set $i (i32.const 7))
      (loop $once
        (local.set $t (i32.mul (local.get $i) (global.get $seed)))
        (br_if $once (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 8))))
      (local.set $i (i32.const 0))
      (loop $four
        (local.set $acc (i32.add (i32.rotl (local.get $acc) (i32.const 3)) (i32.xor (local.get $t) (local.get $i))))
        (br_if $four (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 4))))
      (br_if $rep (i32.ne (local.tee $r (i32.add (local.get $r) (i32.const 1))) (i32.const 5))))
    (i32.add (local.get $acc) (local.get $t)))

  ;; one slot through a constant and through a pointer offset
  (func (export "two_spellings_of_a_global") (result i64)
    (local $p i32)
    (local.set $p (i32.const 68000))
    (i64.store offset=200 (local.get $p) (i64.extend_i32_u (global.get $seed)))
    (i64.store (i32.const 68208) (i64.const 7))
    (i64.add (i64.load (i32.const 68200))
             (i64.load offset=208 (local.get $p))))

  ;; more floats live at once than there are registers
  (func (export "float_pressure") (result f64)
    (local $i i32)
    (local $a0 f64) (local $a1 f64) (local $a2 f64) (local $a3 f64) (local $a4 f64)
    (local $a5 f64) (local $a6 f64) (local $a7 f64) (local $a8 f64) (local $a9 f64)
    (local $b0 f64) (local $b1 f64) (local $b2 f64) (local $b3 f64) (local $b4 f64)
    (local $b5 f64) (local $b6 f64) (local $b7 f64) (local $b8 f64) (local $b9 f64)
    (local $c0 f64) (local $c1 f64) (local $c2 f64) (local $c3 f64) (local $c4 f64)
    (local $c5 f64) (local $c6 f64) (local $c7 f64) (local $c8 f64) (local $c9 f64)
    (local.set $a0 (global.get $fseed))
    (local.set $a1 (f64.add (local.get $a0) (f64.const 1)))
    (local.set $a2 (f64.mul (local.get $a1) (f64.const 1.5)))
    (local.set $a3 (f64.sub (local.get $a2) (f64.const 0.25)))
    (local.set $a4 (f64.add (local.get $a3) (local.get $a0)))
    (local.set $a5 (f64.mul (local.get $a4) (f64.const 0.75)))
    (local.set $a6 (f64.add (local.get $a5) (f64.const 3)))
    (local.set $a7 (f64.sub (local.get $a6) (local.get $a1)))
    (local.set $a8 (f64.mul (local.get $a7) (f64.const 1.125)))
    (local.set $a9 (f64.add (local.get $a8) (local.get $a2)))
    (local.set $b0 (f64.sqrt (local.get $a9)))
    (local.set $b1 (f64.add (local.get $b0) (local.get $a3)))
    (local.set $b2 (f64.mul (local.get $b1) (local.get $a4)))
    (local.set $b3 (f64.sub (local.get $b2) (local.get $a5)))
    (local.set $b4 (f64.add (local.get $b3) (local.get $a6)))
    (local.set $b5 (f64.mul (local.get $b4) (f64.const 0.5)))
    (local.set $b6 (f64.add (local.get $b5) (local.get $a7)))
    (local.set $b7 (f64.sub (local.get $b6) (local.get $a8)))
    (local.set $b8 (f64.mul (local.get $b7) (f64.const 0.875)))
    (local.set $b9 (f64.add (local.get $b8) (local.get $a9)))
    (loop $l
      (local.set $c0 (f64.add (local.get $a0) (local.get $b9)))
      (local.set $c1 (f64.mul (local.get $a1) (f64.const 0.5)))
      (local.set $c2 (f64.sub (local.get $a2) (local.get $b7)))
      (local.set $c3 (f64.add (local.get $a3) (local.get $b6)))
      (local.set $c4 (f64.mul (local.get $a4) (f64.const 0.03125)))
      (local.set $c5 (f64.sub (local.get $a5) (local.get $b4)))
      (local.set $c6 (f64.add (local.get $a6) (local.get $b3)))
      (local.set $c7 (f64.mul (local.get $a7) (f64.const 0.0625)))
      (local.set $c8 (f64.sub (local.get $a8) (local.get $b1)))
      (local.set $c9 (f64.div (local.get $a9) (f64.add (local.get $b0) (f64.const 2))))
      (local.set $a0 (f64.mul (local.get $c0) (f64.const 0.5)))
      (local.set $a1 (f64.add (local.get $c1) (local.get $c9)))
      (local.set $a2 (f64.sub (local.get $c2) (local.get $c8)))
      (local.set $a3 (f64.mul (local.get $c3) (f64.const 0.25)))
      (local.set $a4 (f64.add (local.get $c4) (local.get $c7)))
      (local.set $a5 (f64.sub (local.get $c5) (local.get $c6)))
      (local.set $a6 (f64.mul (local.get $c6) (f64.const 0.125)))
      (local.set $a7 (f64.add (local.get $c7) (local.get $c5)))
      (local.set $a8 (f64.sub (local.get $c8) (local.get $c4)))
      (local.set $a9 (f64.sqrt (f64.abs (f64.add (local.get $c9) (local.get $c3)))))
      (local.set $b0 (f64.add (local.get $b0) (local.get $a0)))
      (local.set $b1 (f64.mul (local.get $b1) (f64.const 0.999)))
      (local.set $b2 (f64.sub (local.get $b2) (local.get $a2)))
      (local.set $b3 (f64.add (local.get $b3) (local.get $a3)))
      (local.set $b4 (f64.mul (local.get $b4) (f64.const 1.001)))
      (local.set $b5 (f64.sub (local.get $b5) (local.get $a5)))
      (local.set $b6 (f64.add (local.get $b6) (local.get $a6)))
      (local.set $b7 (f64.mul (local.get $b7) (f64.const 0.5)))
      (local.set $b8 (f64.sub (local.get $b8) (local.get $a8)))
      (local.set $b9 (f64.add (local.get $b9) (local.get $a9)))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 6))))
    (f64.add
      (f64.add (f64.add (f64.add (local.get $a0) (local.get $a1)) (f64.add (local.get $a2) (local.get $a3)))
               (f64.add (f64.add (local.get $a4) (local.get $a5)) (f64.add (local.get $a6) (local.get $a7))))
      (f64.add
        (f64.add (f64.add (local.get $a8) (local.get $a9)) (f64.add (local.get $b0) (local.get $b1)))
        (f64.add (f64.add (f64.add (local.get $b2) (local.get $b3)) (f64.add (local.get $b4) (local.get $b5)))
                 (f64.add (f64.add (local.get $b6) (local.get $b7)) (f64.add (local.get $b8) (local.get $b9))))))))
