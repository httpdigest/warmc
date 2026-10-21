;; A value a loop carries round, made by a chain of one associative op, is
;; rebuilt with the carried value added last (pass_reassoc_carried):
;; acc = ((acc + a) + b) + c becomes acc + ((a + b) + c).  Each export runs a
;; loop of that shape over values that wrap, next to the shapes that must be
;; left alone or kept apart:
;;
;;   * sums, products, and, or and xor, at i32 and i64, that overflow;
;;   * the carried value twice in the chain, which is not rebuilt;
;;   * a link computed before the loop, which stays one term;
;;   * a link something else in the loop also reads, which stays one term;
;;   * a counter stepped by several constants, which become one;
;;   * an inner loop whose sum feeds the outer loop's.
;;
;; The values come from an exported mutable global so nothing folds them away
;; first.

(module
  (global $seed (export "seed") (mut i64) (i64.const 0x9e37_79b9_7f4a_7c15))
  (memory 1)

  (func $v (param $k i32) (result i64)
    (i64.mul (i64.add (global.get $seed) (i64.extend_i32_u (local.get $k)))
             (i64.const 0x2545_f491_4f6c_dd1d)))

  (func (export "sum_chain_i32") (result i32)
    (local $i i32) (local $acc i32) (local $x i64)
    (local.set $acc (i32.const 0x7fff_fff0))
    (loop $l
      (local.set $x (call $v (local.get $i)))
      (local.set $acc
        (i32.add
          (i32.add
            (i32.add (local.get $acc) (i32.wrap_i64 (i64.popcnt (local.get $x))))
            (i32.wrap_i64 (i64.clz (local.get $x))))
          (i32.wrap_i64 (i64.shr_u (local.get $x) (i64.const 33)))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 200))))
    (local.get $acc))

  (func (export "chains_of_each_op") (result i64)
    (local $i i32) (local $s i64) (local $p i64) (local $a i64) (local $o i64)
    (local $x i64) (local $y i64) (local $z i64)
    (local.set $p (i64.const 1)) (local.set $a (i64.const -1))
    (loop $l
      (local.set $y (call $v (local.get $i)))
      (local.set $z (call $v (i32.add (local.get $i) (i32.const 1000))))
      (local.set $s (i64.add (i64.add (i64.add (local.get $s) (local.get $y)) (local.get $z))
                             (i64.const 0x7fff_ffff_ffff_ffff)))
      (local.set $p (i64.mul (i64.mul (local.get $p) (i64.or (local.get $y) (i64.const 1)))
                             (i64.const 3)))
      (local.set $a (i64.and (i64.and (local.get $a) (i64.or (local.get $y) (i64.const 0x8000_0000_0000_0001)))
                             (i64.or (local.get $z) (i64.const 0x10))))
      (local.set $o (i64.or (i64.or (local.get $o) (i64.and (local.get $y) (i64.const 0x101)))
                            (i64.and (local.get $z) (i64.const 0x1010))))
      (local.set $x (i64.xor (i64.xor (i64.xor (local.get $x) (local.get $y))
                                      (i64.rotl (local.get $z) (i64.const 7)))
                             (i64.const 0x5555)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 100))))
    (i64.add (i64.add (i64.add (local.get $s) (i64.mul (local.get $p) (i64.const 3)))
                      (i64.add (local.get $a) (i64.rotl (local.get $o) (i64.const 13))))
             (local.get $x)))

  (func (export "carried_twice") (result i32)
    (local $i i32) (local $acc i32)
    (local.set $acc (i32.const 1))
    (loop $l
      (local.set $acc (i32.add (i32.add (local.get $acc) (local.get $acc))
                               (i32.wrap_i64 (call $v (local.get $i)))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 40))))
    (local.get $acc))

  (func (export "invariant_and_shared_links") (result i32)
    (local $i i32) (local $acc i32) (local $inv i32) (local $t i32) (local $w i32)
    (local.set $inv (i32.add (i32.wrap_i64 (global.get $seed)) (i32.const 12345)))
    (loop $l
      (local.set $w (i32.wrap_i64 (call $v (local.get $i))))
      (local.set $t (i32.add (local.get $acc) (local.get $w)))
      ;; the link $t is also stored, so it stays a term
      (i32.store (i32.and (i32.shl (local.get $i) (i32.const 2)) (i32.const 0xffc)) (local.get $t))
      (local.set $acc (i32.add (i32.add (local.get $t) (i32.shr_u (local.get $w) (i32.const 3)))
                               (local.get $inv)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 300))))
    (i32.add (local.get $acc) (i32.load (i32.const 400))))

  (func (export "counter_stepped_by_constants") (result i32)
    (local $i i32) (local $n i32) (local $acc i32)
    (loop $l
      (local.set $acc (i32.xor (local.get $acc) (i32.mul (local.get $i) (i32.const 0x9e37))))
      (local.set $i (i32.add (i32.add (i32.add (local.get $i) (i32.const 3)) (i32.const 5))
                             (i32.const -1)))
      (br_if $l (i32.lt_u (local.tee $n (i32.add (local.get $n) (i32.const 1)))
                          (i32.const 500))))
    (i32.add (local.get $acc) (local.get $i)))

  (func (export "inner_loop_feeds_outer") (result i64)
    (local $i i32) (local $j i32) (local $outer i64) (local $inner i64)
    (loop $o
      (local.set $inner (i64.extend_i32_u (local.get $i)))
      (local.set $j (i32.const 0))
      (loop $in
        (local.set $inner
          (i64.add (i64.add (local.get $inner) (call $v (local.get $j)))
                   (i64.extend_i32_u (local.get $j))))
        (br_if $in (i32.lt_u (local.tee $j (i32.add (local.get $j) (i32.const 1)))
                             (i32.const 17))))
      (local.set $outer (i64.add (i64.add (local.get $outer) (local.get $inner))
                                 (i64.shl (local.get $inner) (i64.const 3))))
      (br_if $o (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 23))))
    (local.get $outer)))
