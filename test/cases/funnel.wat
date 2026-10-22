;; (x << k) | (y >> (width - k)) is one extr on arm64: the low half of x:y
;; shifted right by width - k.  Each export folds such shapes over values
;; that change every round, next to the shapes that must not become one:
;;
;;   * or and xor, either operand order, at i32 and i64, for every k;
;;   * x == y, which is a rotate;
;;   * amounts that do not add up to the width, or are out of range and
;;     wrap, which stay two shifts;
;;   * an arithmetic right shift, which is not a funnel;
;;   * a shift something else also reads, which stays computed;
;;   * a multi-word shift by one bit, the shape an unrolled bitset makes.
;;
;; The values come from an exported mutable global so nothing folds them away
;; first.

(module
  (global $seed (export "seed") (mut i64) (i64.const 0x9e37_79b9_7f4a_7c15))
  (memory 1)

  (func $v (param $k i32) (result i64)
    (i64.mul (i64.add (global.get $seed) (i64.extend_i32_u (local.get $k)))
             (i64.const 0x2545_f491_4f6c_dd1d)))

  (func $mix (param $h i64) (param $x i64) (result i64)
    (i64.mul (i64.xor (local.get $h) (local.get $x)) (i64.const 0x100_0000_01b3)))

  (func (export "or32_every_k") (result i64)
    (local $k i32) (local $h i64) (local $x i32) (local $y i32)
    (loop $l
      (local.set $x (i32.wrap_i64 (call $v (local.get $k))))
      (local.set $y (i32.wrap_i64 (i64.shr_u (call $v (i32.add (local.get $k) (i32.const 77)))
                                             (i64.const 17))))
      (local.set $h
        (call $mix (local.get $h)
          (i64.extend_i32_u
            (i32.or (i32.shl (local.get $x) (local.get $k))
                    (i32.shr_u (local.get $y) (i32.sub (i32.const 32) (local.get $k)))))))
      (br_if $l (i32.lt_u (local.tee $k (i32.add (local.get $k) (i32.const 1)))
                          (i32.const 32))))
    (local.get $h))

  (func (export "consts32") (result i64)
    (local $i i32) (local $h i64) (local $x i32) (local $y i32)
    (loop $l
      (local.set $x (i32.wrap_i64 (call $v (local.get $i))))
      (local.set $y (i32.wrap_i64 (call $v (i32.add (local.get $i) (i32.const 500)))))
      (local.set $h (call $mix (local.get $h) (i64.extend_i32_u
        (i32.or (i32.shl (local.get $x) (i32.const 31)) (i32.shr_u (local.get $y) (i32.const 1))))))
      (local.set $h (call $mix (local.get $h) (i64.extend_i32_u
        (i32.or (i32.shr_u (local.get $y) (i32.const 7)) (i32.shl (local.get $x) (i32.const 25))))))
      (local.set $h (call $mix (local.get $h) (i64.extend_i32_u
        (i32.xor (i32.shl (local.get $x) (i32.const 1)) (i32.shr_u (local.get $y) (i32.const 31))))))
      (local.set $h (call $mix (local.get $h) (i64.extend_i32_u
        (i32.xor (i32.shr_u (local.get $x) (i32.const 13)) (i32.shl (local.get $x) (i32.const 19))))))
      ;; out of range amounts wrap: 33 is 1, so 33 + 31 is a funnel too
      (local.set $h (call $mix (local.get $h) (i64.extend_i32_u
        (i32.or (i32.shl (local.get $x) (i32.const 33)) (i32.shr_u (local.get $y) (i32.const 31))))))
      ;; amounts that do not add up to 32
      (local.set $h (call $mix (local.get $h) (i64.extend_i32_u
        (i32.or (i32.shl (local.get $x) (i32.const 3)) (i32.shr_u (local.get $y) (i32.const 3))))))
      (local.set $h (call $mix (local.get $h) (i64.extend_i32_u
        (i32.or (i32.shl (local.get $x) (i32.const 0)) (i32.shr_u (local.get $y) (i32.const 32))))))
      ;; an arithmetic right shift is no funnel
      (local.set $h (call $mix (local.get $h) (i64.extend_i32_u
        (i32.or (i32.shl (local.get $x) (i32.const 4)) (i32.shr_s (local.get $y) (i32.const 28))))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 64))))
    (local.get $h))

  (func (export "consts64") (result i64)
    (local $i i32) (local $h i64) (local $x i64) (local $y i64)
    (loop $l
      (local.set $x (call $v (local.get $i)))
      (local.set $y (call $v (i32.add (local.get $i) (i32.const 900))))
      (local.set $h (call $mix (local.get $h)
        (i64.or (i64.shl (local.get $x) (i64.const 63)) (i64.shr_u (local.get $y) (i64.const 1)))))
      (local.set $h (call $mix (local.get $h)
        (i64.or (i64.shr_u (local.get $y) (i64.const 40)) (i64.shl (local.get $x) (i64.const 24)))))
      (local.set $h (call $mix (local.get $h)
        (i64.xor (i64.shl (local.get $y) (i64.const 7)) (i64.shr_u (local.get $y) (i64.const 57)))))
      (local.set $h (call $mix (local.get $h)
        (i64.or (i64.shl (local.get $x) (i64.const 8)) (i64.shr_u (local.get $y) (i64.const 8)))))
      (local.set $h (call $mix (local.get $h)
        (i64.or (i64.shl (local.get $x) (i64.const 96)) (i64.shr_u (local.get $y) (i64.const 32)))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 64))))
    (local.get $h))

  ;; the shifted value is read again, so the shift must stay computed
  (func (export "shared_shift") (result i64)
    (local $i i32) (local $h i64) (local $x i32) (local $y i32) (local $t i32)
    (loop $l
      (local.set $x (i32.wrap_i64 (call $v (local.get $i))))
      (local.set $y (i32.wrap_i64 (call $v (i32.add (local.get $i) (i32.const 3)))))
      (local.set $t (i32.shl (local.get $x) (i32.const 5)))
      (local.set $h (call $mix (local.get $h) (i64.extend_i32_u
        (i32.or (local.get $t) (i32.shr_u (local.get $y) (i32.const 27))))))
      (local.set $h (call $mix (local.get $h) (i64.extend_i32_u (local.get $t))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 50))))
    (local.get $h))

  ;; a 256-bit value as eight words in memory, shifted left by one bit in
  ;; place, word by word from the top, with each word taking the next lower
  ;; word's top bit
  (func (export "multiword_shift") (result i64)
    (local $i i32) (local $r i32) (local $h i64)
    (loop $f
      (i32.store (i32.shl (local.get $i) (i32.const 2))
                 (i32.wrap_i64 (call $v (local.get $i))))
      (br_if $f (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 8))))
    (loop $rounds
      (i32.store offset=28 (i32.const 0)
        (i32.or (i32.shl (i32.load offset=28 (i32.const 0)) (i32.const 1))
                (i32.shr_u (i32.load offset=24 (i32.const 0)) (i32.const 31))))
      (i32.store offset=24 (i32.const 0)
        (i32.or (i32.shl (i32.load offset=24 (i32.const 0)) (i32.const 1))
                (i32.shr_u (i32.load offset=20 (i32.const 0)) (i32.const 31))))
      (i32.store offset=20 (i32.const 0)
        (i32.or (i32.shl (i32.load offset=20 (i32.const 0)) (i32.const 1))
                (i32.shr_u (i32.load offset=16 (i32.const 0)) (i32.const 31))))
      (i32.store offset=16 (i32.const 0)
        (i32.or (i32.shl (i32.load offset=16 (i32.const 0)) (i32.const 1))
                (i32.shr_u (i32.load offset=12 (i32.const 0)) (i32.const 31))))
      (i32.store offset=12 (i32.const 0)
        (i32.or (i32.shl (i32.load offset=12 (i32.const 0)) (i32.const 1))
                (i32.shr_u (i32.load offset=8 (i32.const 0)) (i32.const 31))))
      (i32.store offset=8 (i32.const 0)
        (i32.or (i32.shl (i32.load offset=8 (i32.const 0)) (i32.const 1))
                (i32.shr_u (i32.load offset=4 (i32.const 0)) (i32.const 31))))
      (i32.store offset=4 (i32.const 0)
        (i32.or (i32.shl (i32.load offset=4 (i32.const 0)) (i32.const 1))
                (i32.shr_u (i32.load (i32.const 0)) (i32.const 31))))
      (i32.store (i32.const 0) (i32.shl (i32.load (i32.const 0)) (i32.const 1)))
      (local.set $h (call $mix (local.get $h) (i64.load offset=24 (i32.const 0))))
      (br_if $rounds (i32.lt_u (local.tee $r (i32.add (local.get $r) (i32.const 1)))
                               (i32.const 40))))
    (local.set $i (i32.const 0))
    (loop $g
      (local.set $h (call $mix (local.get $h)
        (i64.load32_u (i32.shl (local.get $i) (i32.const 2)))))
      (br_if $g (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 8))))
    (local.get $h))
)
