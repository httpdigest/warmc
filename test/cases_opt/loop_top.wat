;; ir_emit_order lays a loop's latch out above its header (ir.c
;; rotate_loops) where the loop has that one latch and no block falls into
;; it or three or more reach it: the ways round that branched to the latch
;; and from there back fall from it into the header instead.  three's
;; latch, reached from three arms, goes up; two's, one of whose two arms
;; falls into it, stays; mod's other way round returns (musl's fmodf), so
;; nothing falls into its latch and it goes up.  swap carries two values
;; the back edge exchanges, moves on the edge the latch now falls along;
;; nest's inner loop goes up inside an outer one, whose latch, reached from
;; the inner loop's exit alone, stays.  flat's one block is its own latch;
;; twoback has two latches, the second reached from three arms, and neither
;; goes up; jl tests the end at the top, and its latch, which only jumps
;; back, goes up; innerlatch's outer loop has one latch, reached from three
;; arms, which is the inner loop's header, and stays.  The arms store, so that no if-conversion takes them.
;; The bytes at 64 are the text the loops read; the counters are at 0, 4, 8
;; and 12.
;;
;; stats -O2: loop_tops=5
;; stats -O2 a64: small_loops=4
;;
;; run: three 64 11 => i32:1341
;; run: three 64 1 => i32:256
;; run: three 70 5 => i32:453
;; run: two 64 11 => i32:26
;; run: two 66 3 => i32:6
;; run: mod 100 7 30 => i32:2
;; run: mod 99 7 30 => i32:1
;; run: mod 1000 33 12 => i32:7
;; run: mod 7 7 3 => i32:0
;; run: mod 5 3 1 => i32:1
;; run: swap 64 11 => i32:3694
;; run: swap 64 2 => i32:915
;; run: nest 64 11 3 => i32:658
;; run: nest 64 4 1 => i32:132
;; run: flat 1 => i32:0
;; run: flat 7 => i32:154
;; run: flat 100 => i32:343200
;; run: twoback 64 => i32:1045
;; run: twoback 67 => i32:833
;; run: twoback 73 => i32:362
;; run: jl 64 => i32:202
;; run: jl 70 => i32:137
;; run: jl 75 => i32:0
;; run: innerlatch 64 3 => i32:-645244948
;; run: innerlatch 64 0 => i32:0
;; run: innerlatch 70 2 => i32:51682
;; run: innerlatch 74 5 => i32:562

(module
  (memory 1)
  (data (i32.const 64) "Ab9 zZ!q0~m")
  ;; per byte: below '0' the sum at 0, below 'a' the count at 4, else the
  ;; xor at 8; what the counters come to, mixed
  (func $three (export "three") (param $p i32) (param $n i32) (result i32)
    (local $i i32) (local $x i32)
    (block $done
      (loop $l
        (local.set $x (i32.load8_u (i32.add (local.get $p) (local.get $i))))
        (if (i32.lt_u (local.get $x) (i32.const 48))
          (then (i32.store (i32.const 0) (i32.add (i32.load (i32.const 0)) (local.get $x))))
          (else
            (if (i32.lt_u (local.get $x) (i32.const 97))
              (then (i32.store (i32.const 4) (i32.add (i32.load (i32.const 4)) (i32.const 1))))
              (else (i32.store (i32.const 8) (i32.xor (i32.load (i32.const 8)) (local.get $x)))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (i32.add (i32.mul (i32.load (i32.const 0)) (i32.const 3))
             (i32.add (i32.shl (i32.load (i32.const 4)) (i32.const 8)) (i32.load (i32.const 8)))))
  ;; per byte: below 'a' the count at 0, else the count at 4
  (func $two (export "two") (param $p i32) (param $n i32) (result i32)
    (local $i i32)
    (block $done
      (loop $l
        (if (i32.lt_u (i32.load8_u (i32.add (local.get $p) (local.get $i))) (i32.const 97))
          (then (i32.store (i32.const 0) (i32.add (i32.load (i32.const 0)) (i32.const 1))))
          (else (i32.store (i32.const 4) (i32.add (i32.load (i32.const 4)) (i32.const 2)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (i32.add (i32.load (i32.const 0)) (i32.mul (i32.load (i32.const 4)) (i32.const 2))))
  ;; fmodf's loop: e steps of subtracting y where that leaves x positive
  ;; and doubling, 0 at once where it leaves nothing; then x mod y.  Small,
  ;; it is aligned where it now starts, at its latch
  (func $mod (export "mod") (param $x i32) (param $y i32) (param $e i32) (result i32)
    (local $t i32)
    (loop $l
      (local.set $t (i32.sub (local.get $x) (local.get $y)))
      (if (i32.ge_s (local.get $t) (i32.const 0))
        (then
          (if (i32.eqz (local.get $t)) (then (return (i32.const 0))))
          (local.set $x (local.get $t))))
      (local.set $x (i32.shl (local.get $x) (i32.const 1)))
      (local.set $e (i32.sub (local.get $e) (i32.const 1)))
      (br_if $l (i32.gt_s (local.get $e) (i32.const 0))))
    (i32.rem_u (local.get $x) (local.get $y)))
  ;; each step stores the byte by its class, then b + x and a become a and
  ;; b: the back edge exchanges them, moves on the edge the latch now falls
  ;; along into the header
  (func $swap (export "swap") (param $p i32) (param $n i32) (result i32)
    (local $i i32) (local $a i32) (local $b i32) (local $x i32) (local $t i32)
    (local.set $b (i32.const 1))
    (block $done
      (loop $l
        (local.set $x (i32.load8_u (i32.add (local.get $p) (local.get $i))))
        (if (i32.lt_u (local.get $x) (i32.const 48))
          (then (i32.store (i32.const 0) (local.get $x)))
          (else
            (if (i32.lt_u (local.get $x) (i32.const 97))
              (then (i32.store (i32.const 4) (local.get $x)))
              (else (i32.store (i32.const 8) (local.get $x))))))
        (local.set $t (local.get $a))
        (local.set $a (i32.add (local.get $b) (local.get $x)))
        (local.set $b (local.get $t))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (i32.add (i32.add (i32.mul (local.get $a) (i32.const 7)) (local.get $b))
             (i32.add (i32.load (i32.const 0)) (i32.add (i32.load (i32.const 4)) (i32.load (i32.const 8))))))
  ;; three's loop r times, the counters added up after each
  (func $nest (export "nest") (param $p i32) (param $n i32) (param $r i32) (result i32)
    (local $i i32) (local $x i32) (local $s i32)
    (loop $outer
      (local.set $i (i32.const 0))
      (block $done
        (loop $l
          (local.set $x (i32.load8_u (i32.add (local.get $p) (local.get $i))))
          (if (i32.lt_u (local.get $x) (i32.const 48))
            (then (i32.store (i32.const 0) (i32.add (i32.load (i32.const 0)) (local.get $x))))
            (else
              (if (i32.lt_u (local.get $x) (i32.const 97))
                (then (i32.store (i32.const 4) (i32.add (i32.load (i32.const 4)) (i32.const 1))))
                (else (i32.store (i32.const 8) (i32.xor (i32.load (i32.const 8)) (local.get $x)))))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
      (local.set $s (i32.add (local.get $s)
                             (i32.add (i32.load (i32.const 0))
                                      (i32.add (i32.load (i32.const 4)) (i32.load (i32.const 8))))))
      (local.set $r (i32.sub (local.get $r) (i32.const 1)))
      (br_if $outer (i32.gt_s (local.get $r) (i32.const 0))))
    (local.get $s))
  ;; one block round: its own latch, which stays
  (func $flat (export "flat") (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (loop $l
      (local.set $s (i32.add (local.get $s) (i32.mul (local.get $i) (i32.add (local.get $i) (i32.const 3)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))
  ;; two ways back to the header, which stores before it tests the end: a
  ;; byte below '0' goes straight back, the rest by three arms to a second
  ;; latch, which steps over the next byte after an odd one; no one latch
  ;; to lift
  (func $twoback (export "twoback") (param $p i32) (result i32)
    (local $i i32) (local $x i32)
    (block $done
      (loop $l
        (local.set $x (i32.load8_u (i32.add (local.get $p) (local.get $i))))
        (i32.store (i32.const 12) (i32.add (i32.load (i32.const 12)) (local.get $x)))
        (br_if $done (i32.eqz (local.get $x)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $x) (i32.const 48)))
        (if (i32.lt_u (local.get $x) (i32.const 97))
          (then (i32.store (i32.const 4) (i32.add (i32.load (i32.const 4)) (local.get $x))))
          (else
            (if (i32.lt_u (local.get $x) (i32.const 110))
              (then (i32.store (i32.const 8) (i32.add (i32.load (i32.const 8)) (i32.const 1))))
              (else (i32.store (i32.const 0) (i32.xor (i32.load (i32.const 0)) (local.get $x)))))))
        (local.set $i (i32.add (local.get $i) (i32.and (local.get $x) (i32.const 1))))
        (br $l)))
    (i32.add (i32.load (i32.const 0))
             (i32.add (i32.load (i32.const 4)) (i32.add (i32.load (i32.const 8)) (i32.load (i32.const 12))))))
  ;; the end tested at the top, three arms back to a latch that only jumps:
  ;; it goes up
  (func $jl (export "jl") (param $p i32) (result i32)
    (local $x i32) (local $i i32)
    (block $done
      (loop $l
        (local.set $x (i32.load8_u (i32.add (local.get $p) (local.get $i))))
        (br_if $done (i32.eqz (local.get $x)))
        (if (i32.lt_u (local.get $x) (i32.const 48))
          (then (i32.store (i32.const 0) (i32.add (i32.load (i32.const 0)) (local.get $x))))
          (else
            (if (i32.lt_u (local.get $x) (i32.const 97))
              (then (i32.store (i32.const 4) (i32.add (i32.load (i32.const 4)) (i32.const 1))))
              (else (i32.store (i32.const 8) (i32.xor (i32.load (i32.const 8)) (local.get $x)))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $i)
             (i32.add (i32.load (i32.const 0)) (i32.add (i32.load (i32.const 4)) (i32.load (i32.const 8))))))
  ;; the inner loop's header, reached from three arms, leaves for the outer
  ;; loop's: the outer loop's one latch is a loop's header, which stays
  (func $innerlatch (export "innerlatch") (param $p i32) (param $m i32) (result i32)
    (local $i i32) (local $x i32) (local $j i32) (local $s i32)
    (block $done
      (loop $outer
        (local.set $x (i32.load8_u (i32.add (local.get $p) (local.get $i))))
        (br_if $done (i32.eqz (local.get $x)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (local.get $m))
        (if (i32.lt_u (local.get $x) (i32.const 48))
          (then (i32.store (i32.const 0) (local.get $x)))
          (else
            (if (i32.lt_u (local.get $x) (i32.const 97))
              (then (i32.store (i32.const 4) (local.get $x)))
              (else (i32.store (i32.const 8) (local.get $x))))))
        (loop $in
          (i32.store (i32.const 16) (i32.add (i32.load (i32.const 16)) (local.get $j)))
          (br_if $outer (i32.le_s (local.get $j) (i32.const 0)))
          (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $j)))
          (local.set $j (i32.sub (local.get $j) (i32.const 1)))
          (br $in))))
    (i32.add (local.get $s) (i32.load (i32.const 16))))
)
