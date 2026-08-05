;; A local read far below where it was written, through a ladder of joins.
;;
;; SSA construction looks a local up by walking back from the block that reads
;; it, giving every join block on the way a parameter.  Nothing else in the
;; corpus makes that walk long.  Here $vN is written once at the top and read
;; in arm N of a ladder, so the walk for $vN crosses every arm above it, and
;; every parameter it leaves behind is trivial -- the same value arrives on
;; both edges.  Construction collapses those as it wires them, and what this
;; file pins is that it collapses *only* those.
;;
;; The mistake that shape invites is to memoise the collapsed value as the
;; block's current definition.  A block's current definition is what its last
;; write left, not what its parameter was, so overwriting it makes a
;; loop-carried local read its pre-loop value instead of the one the back edge
;; brings.  Every function here therefore carries a counter and an accumulator
;; across the same joins the ladder walks through, and `chain_write` has arms
;; that write a local a later arm reads, whose join parameters are real.
;;
;; The ladder is also inside the loop, so the header's parameter for $vN takes
;; the definition on one edge and itself on the other: a parameter is trivial
;; when its only other argument is itself, and one that never notices stays
;; alive for every local in the function.
;;
;; `deadparam.wat` pins the same rule from the register allocator's end.
;; wasm-interp answers every export, so no expected values are written here.

(module
  (func (export "chain") (result i32)
    (local $i i32) (local $acc i32)
    (local $v0 i32) (local $v1 i32) (local $v2 i32) (local $v3 i32) (local $v4 i32) (local $v5 i32) (local $v6 i32) (local $v7 i32) (local $v8 i32) (local $v9 i32) (local $v10 i32) (local $v11 i32)
    (local.set $v0 (i32.const 3))
    (local.set $v1 (i32.const 4))
    (local.set $v2 (i32.const 7))
    (local.set $v3 (i32.const 12))
    (local.set $v4 (i32.const 19))
    (local.set $v5 (i32.const 28))
    (local.set $v6 (i32.const 39))
    (local.set $v7 (i32.const 52))
    (local.set $v8 (i32.const 67))
    (local.set $v9 (i32.const 84))
    (local.set $v10 (i32.const 103))
    (local.set $v11 (i32.const 124))
    (loop $l
      (if (i32.eq (local.get $i) (i32.const 0))
        (then (local.set $acc (i32.add (local.get $acc) (local.get $v0)))))
      (if (i32.eq (local.get $i) (i32.const 1))
        (then (local.set $acc (i32.add (local.get $acc) (local.get $v1)))))
      (if (i32.eq (local.get $i) (i32.const 2))
        (then (local.set $acc (i32.add (local.get $acc) (local.get $v2)))))
      (if (i32.eq (local.get $i) (i32.const 3))
        (then (local.set $acc (i32.add (local.get $acc) (local.get $v3)))))
      (if (i32.eq (local.get $i) (i32.const 4))
        (then (local.set $acc (i32.add (local.get $acc) (local.get $v4)))))
      (if (i32.eq (local.get $i) (i32.const 5))
        (then (local.set $acc (i32.add (local.get $acc) (local.get $v5)))))
      (if (i32.eq (local.get $i) (i32.const 6))
        (then (local.set $acc (i32.add (local.get $acc) (local.get $v6)))))
      (if (i32.eq (local.get $i) (i32.const 7))
        (then (local.set $acc (i32.add (local.get $acc) (local.get $v7)))))
      (if (i32.eq (local.get $i) (i32.const 8))
        (then (local.set $acc (i32.add (local.get $acc) (local.get $v8)))))
      (if (i32.eq (local.get $i) (i32.const 9))
        (then (local.set $acc (i32.add (local.get $acc) (local.get $v9)))))
      (if (i32.eq (local.get $i) (i32.const 10))
        (then (local.set $acc (i32.add (local.get $acc) (local.get $v10)))))
      (if (i32.eq (local.get $i) (i32.const 11))
        (then (local.set $acc (i32.add (local.get $acc) (local.get $v11)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 12))))
    (local.get $acc))

  ;; $w is written in one arm and read in the next, so the join parameter
  ;; that carries it is not trivial and has to survive.
  (func (export "chain_write") (result i32)
    (local $i i32) (local $acc i32) (local $w i32)
    (local $v0 i32) (local $v1 i32) (local $v2 i32) (local $v3 i32) (local $v4 i32) (local $v5 i32) (local $v6 i32) (local $v7 i32)
    (local.set $v0 (i32.const 1))
    (local.set $v1 (i32.const 3))
    (local.set $v2 (i32.const 5))
    (local.set $v3 (i32.const 7))
    (local.set $v4 (i32.const 9))
    (local.set $v5 (i32.const 11))
    (local.set $v6 (i32.const 13))
    (local.set $v7 (i32.const 15))
    (loop $l
      (if (i32.eq (local.get $i) (i32.const 0))
        (then (local.set $w (local.get $v0))))
      (if (i32.eq (local.get $i) (i32.const 1))
        (then (local.set $w (i32.add (local.get $w) (local.get $v1)))))
      (if (i32.eq (local.get $i) (i32.const 2))
        (then (local.set $w (i32.add (local.get $w) (local.get $v2)))))
      (if (i32.eq (local.get $i) (i32.const 3))
        (then (local.set $w (i32.add (local.get $w) (local.get $v3)))))
      (if (i32.eq (local.get $i) (i32.const 4))
        (then (local.set $w (i32.add (local.get $w) (local.get $v4)))))
      (if (i32.eq (local.get $i) (i32.const 5))
        (then (local.set $w (i32.add (local.get $w) (local.get $v5)))))
      (if (i32.eq (local.get $i) (i32.const 6))
        (then (local.set $w (i32.add (local.get $w) (local.get $v6)))))
      (if (i32.eq (local.get $i) (i32.const 7))
        (then (local.set $w (i32.add (local.get $w) (local.get $v7)))))
      (local.set $acc (i32.add (local.get $acc) (local.get $w)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 8))))
    (i32.add (local.get $acc) (local.get $w)))

  ;; A branch out of the middle of the ladder, so the block after the loop
  ;; is a join of two very differently deep walks.
  (func (export "chain_break") (result i32)
    (local $i i32) (local $acc i32)
    (local $v0 i32) (local $v1 i32) (local $v2 i32) (local $v3 i32) (local $v4 i32) (local $v5 i32) (local $v6 i32) (local $v7 i32) (local $v8 i32) (local $v9 i32)
    (local.set $v0 (i32.const 2))
    (local.set $v1 (i32.const 9))
    (local.set $v2 (i32.const 16))
    (local.set $v3 (i32.const 23))
    (local.set $v4 (i32.const 30))
    (local.set $v5 (i32.const 37))
    (local.set $v6 (i32.const 44))
    (local.set $v7 (i32.const 51))
    (local.set $v8 (i32.const 58))
    (local.set $v9 (i32.const 65))
    (block $out
      (loop $l
        (if (i32.eq (local.get $i) (i32.const 0))
          (then (local.set $acc (i32.add (local.get $acc) (local.get $v0)))))
        (if (i32.eq (local.get $i) (i32.const 1))
          (then (local.set $acc (i32.add (local.get $acc) (local.get $v1)))))
        (if (i32.eq (local.get $i) (i32.const 2))
          (then (local.set $acc (i32.add (local.get $acc) (local.get $v2)))))
        (if (i32.eq (local.get $i) (i32.const 3))
          (then (local.set $acc (i32.add (local.get $acc) (local.get $v3)))))
        (if (i32.eq (local.get $i) (i32.const 4))
          (then (local.set $acc (i32.add (local.get $acc) (local.get $v4)))))
        (if (i32.eq (local.get $i) (i32.const 5))
          (then (local.set $acc (i32.add (local.get $acc) (local.get $v5)))))
        (if (i32.eq (local.get $i) (i32.const 6))
          (then (local.set $acc (i32.add (local.get $acc) (local.get $v6)))
                (br_if $out (i32.gt_u (local.get $acc) (i32.const 90)))))
        (if (i32.eq (local.get $i) (i32.const 7))
          (then (local.set $acc (i32.add (local.get $acc) (local.get $v7)))))
        (if (i32.eq (local.get $i) (i32.const 8))
          (then (local.set $acc (i32.add (local.get $acc) (local.get $v8)))))
        (if (i32.eq (local.get $i) (i32.const 9))
          (then (local.set $acc (i32.add (local.get $acc) (local.get $v9)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (i32.const 10)))))
    (i32.add (local.get $acc) (i32.mul (local.get $i) (i32.const 1000))))

  ;; The same walk for a wider local and a float one, so the collapse is not
  ;; a rule about i32 alone.
  (func (export "chain_wide") (result i32)
    (local $i i32) (local $acc i64) (local $f f64)
    (local $v0 i64) (local $v1 i64) (local $v2 i64) (local $v3 i64) (local $v4 i64) (local $v5 i64) (local $v6 i64) (local $v7 i64)
    (local $g0 f64) (local $g1 f64) (local $g2 f64) (local $g3 f64) (local $g4 f64) (local $g5 f64) (local $g6 f64) (local $g7 f64)
    (local.set $v0 (i64.const 4294967297))
    (local.set $g0 (f64.const 0.5))
    (local.set $v1 (i64.const 8589934594))
    (local.set $g1 (f64.const 1.5))
    (local.set $v2 (i64.const 12884901891))
    (local.set $g2 (f64.const 2.5))
    (local.set $v3 (i64.const 17179869188))
    (local.set $g3 (f64.const 3.5))
    (local.set $v4 (i64.const 21474836485))
    (local.set $g4 (f64.const 4.5))
    (local.set $v5 (i64.const 25769803782))
    (local.set $g5 (f64.const 5.5))
    (local.set $v6 (i64.const 30064771079))
    (local.set $g6 (f64.const 6.5))
    (local.set $v7 (i64.const 34359738376))
    (local.set $g7 (f64.const 7.5))
    (loop $l
      (if (i32.eq (local.get $i) (i32.const 0))
        (then (local.set $acc (i64.add (local.get $acc) (local.get $v0)))
              (local.set $f (f64.add (local.get $f) (local.get $g0)))))
      (if (i32.eq (local.get $i) (i32.const 1))
        (then (local.set $acc (i64.add (local.get $acc) (local.get $v1)))
              (local.set $f (f64.add (local.get $f) (local.get $g1)))))
      (if (i32.eq (local.get $i) (i32.const 2))
        (then (local.set $acc (i64.add (local.get $acc) (local.get $v2)))
              (local.set $f (f64.add (local.get $f) (local.get $g2)))))
      (if (i32.eq (local.get $i) (i32.const 3))
        (then (local.set $acc (i64.add (local.get $acc) (local.get $v3)))
              (local.set $f (f64.add (local.get $f) (local.get $g3)))))
      (if (i32.eq (local.get $i) (i32.const 4))
        (then (local.set $acc (i64.add (local.get $acc) (local.get $v4)))
              (local.set $f (f64.add (local.get $f) (local.get $g4)))))
      (if (i32.eq (local.get $i) (i32.const 5))
        (then (local.set $acc (i64.add (local.get $acc) (local.get $v5)))
              (local.set $f (f64.add (local.get $f) (local.get $g5)))))
      (if (i32.eq (local.get $i) (i32.const 6))
        (then (local.set $acc (i64.add (local.get $acc) (local.get $v6)))
              (local.set $f (f64.add (local.get $f) (local.get $g6)))))
      (if (i32.eq (local.get $i) (i32.const 7))
        (then (local.set $acc (i64.add (local.get $acc) (local.get $v7)))
              (local.set $f (f64.add (local.get $f) (local.get $g7)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 8))))
    (i32.add (i32.wrap_i64 (i64.shr_u (local.get $acc) (i64.const 20)))
             (i32.trunc_f64_s (local.get $f))))

  ;; A ladder inside a ladder: the walk for $vN now crosses the joins of the
  ;; inner loop as well as the outer one.
  (func (export "chain_nested") (result i32)
    (local $i i32) (local $j i32) (local $acc i32)
    (local $v0 i32) (local $v1 i32) (local $v2 i32) (local $v3 i32) (local $v4 i32)
    (local.set $v0 (i32.const 4))
    (local.set $v1 (i32.const 15))
    (local.set $v2 (i32.const 26))
    (local.set $v3 (i32.const 37))
    (local.set $v4 (i32.const 48))
    (loop $o
      (local.set $j (i32.const 0))
      (loop $l
      (if (i32.eq (local.get $i) (i32.const 0))
        (then (local.set $acc (i32.add (local.get $acc) (i32.mul (local.get $v0) (local.get $i))))))
      (if (i32.eq (local.get $i) (i32.const 1))
        (then (local.set $acc (i32.add (local.get $acc) (i32.mul (local.get $v1) (local.get $i))))))
      (if (i32.eq (local.get $i) (i32.const 2))
        (then (local.set $acc (i32.add (local.get $acc) (i32.mul (local.get $v2) (local.get $i))))))
      (if (i32.eq (local.get $i) (i32.const 3))
        (then (local.set $acc (i32.add (local.get $acc) (i32.mul (local.get $v3) (local.get $i))))))
      (if (i32.eq (local.get $i) (i32.const 4))
        (then (local.set $acc (i32.add (local.get $acc) (i32.mul (local.get $v4) (local.get $i))))))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $j) (i32.const 2))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $o (i32.lt_u (local.get $i) (i32.const 5))))
    (local.get $acc))
)
