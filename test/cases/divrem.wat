;; A remainder of a division made just before it is n - q*d (pass_divrem).
;; Each export takes quotients and remainders of pairs at the edges -- signed
;; and unsigned, 32 and 64 bits, negative operands, divisors of 1 and -1, the
;; most negative dividend over a divisor that does not overflow -- next to the
;; shapes that must stay divisions: a remainder before its division, a
;; division of other operands, and a division in another block.

(module
  (global $seed (mut i64) (i64.const 0x243f6a8885a308d3))
  (func $next (result i64)
    (global.set $seed (i64.add (i64.mul (global.get $seed) (i64.const 6364136223846793005))
                               (i64.const 1442695040888963407)))
    (global.get $seed))

  (func $pick32 (param $k i32) (result i32)
    (if (result i32) (i32.lt_u (local.get $k) (i32.const 8))
      (then
        (if (result i32) (i32.lt_u (local.get $k) (i32.const 4))
          (then (select (select (i32.const 1) (i32.const -1) (i32.eq (local.get $k) (i32.const 0)))
                        (select (i32.const 0x80000000) (i32.const 0x7fffffff) (i32.eq (local.get $k) (i32.const 2)))
                        (i32.lt_u (local.get $k) (i32.const 2))))
          (else (select (select (i32.const 3) (i32.const -7) (i32.eq (local.get $k) (i32.const 4)))
                        (select (i32.const 0xffffffff) (i32.const 0x80000001) (i32.eq (local.get $k) (i32.const 6)))
                        (i32.lt_u (local.get $k) (i32.const 6))))))
      (else (i32.wrap_i64 (i64.shr_u (call $next) (i64.extend_i32_u (i32.and (local.get $k) (i32.const 31))))))))

  (func (export "pairs_32") (result i32)
    (local $i i32) (local $j i32) (local $n i32) (local $d i32) (local $h i32)
    (loop $li
      (local.set $j (i32.const 0))
      (loop $lj
        (local.set $n (call $pick32 (local.get $i)))
        (local.set $d (call $pick32 (local.get $j)))
        (if (i32.and (i32.ne (local.get $d) (i32.const 0))
                     (i32.or (i32.ne (local.get $n) (i32.const 0x80000000))
                             (i32.ne (local.get $d) (i32.const -1))))
          (then
            (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
                                   (i32.div_s (local.get $n) (local.get $d))))
            (local.set $h (i32.xor (local.get $h) (i32.rem_s (local.get $n) (local.get $d))))))
        (if (local.get $d)
          (then
            (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 37))
                                   (i32.div_u (local.get $n) (local.get $d))))
            (local.set $h (i32.add (local.get $h) (i32.rem_u (local.get $n) (local.get $d))))
            ;; the remainder first, then its division: stays as it is
            (local.set $h (i32.xor (local.get $h) (i32.rem_u (local.get $d) (i32.or (local.get $n) (i32.const 1)))))
            (local.set $h (i32.add (local.get $h) (i32.div_u (local.get $d) (i32.or (local.get $n) (i32.const 1)))))
            ;; a division of other operands, and the remainder of these
            (local.set $h (i32.add (local.get $h) (i32.div_u (local.get $n) (i32.or (local.get $d) (i32.const 2)))))
            (local.set $h (i32.xor (local.get $h) (i32.rem_u (local.get $n) (local.get $d))))))
        (br_if $lj (i32.lt_u (local.tee $j (i32.add (local.get $j) (i32.const 1))) (i32.const 40))))
      (br_if $li (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 40))))
    (local.get $h))

  (func (export "pairs_64") (result i64)
    (local $i i32) (local $n i64) (local $d i64) (local $h i64) (local $q i64)
    (loop $l
      (local.set $n (call $next))
      (local.set $d (i64.shr_s (call $next) (i64.extend_i32_u (i32.and (local.get $i) (i32.const 63)))))
      (if (i32.eqz (i32.and (local.get $i) (i32.const 15))) (then (local.set $d (i64.const -1))))
      (if (i32.eq (i32.and (local.get $i) (i32.const 15)) (i32.const 1)) (then (local.set $d (i64.const 1))))
      (if (i64.ne (local.get $d) (i64.const 0))
        (then
          (local.set $q (i64.div_s (local.get $n) (local.get $d)))
          (local.set $h (i64.add (i64.mul (local.get $h) (i64.const 1099511628211)) (local.get $q)))
          (local.set $h (i64.xor (local.get $h) (i64.rem_s (local.get $n) (local.get $d))))
          (local.set $h (i64.add (local.get $h) (i64.div_u (local.get $n) (local.get $d))))
          (local.set $h (i64.xor (local.get $h) (i64.rem_u (local.get $n) (local.get $d))))))
      ;; a division in one block and the remainder in the next
      (if (i64.gt_u (local.get $d) (i64.const 1))
        (then
          (local.set $q (i64.div_u (local.get $n) (local.get $d)))
          (if (i64.gt_u (local.get $q) (i64.const 3))
            (then (local.set $h (i64.add (local.get $h) (i64.rem_u (local.get $n) (local.get $d))))))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 3000))))
    (local.get $h)))
