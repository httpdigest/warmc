;; x ^= x >> k twice in a row is x again when 2k is at least the width: the
;; second shift only sees bits the first xor did not touch.  The fold that
;; drops the pair must take exactly that shape, so each export runs both the
;; pairs that cancel -- half the width and more, left and right, 32 and 64
;; bits, amounts written modulo the width -- and the near misses that do not:
;; one less than half, an arithmetic shift, two different amounts, a left
;; shift paired with a right one, and an intermediate that is used elsewhere.

(module
  (global $seed (mut i64) (i64.const 0x9e3779b97f4a7c15))
  (func $next (result i64)
    (global.set $seed (i64.add (i64.mul (global.get $seed) (i64.const 6364136223846793005))
                               (i64.const 1442695040888963407)))
    (global.get $seed))

  (func $twice64 (param $x i64) (param $k i64) (result i64)
    (local $y i64)
    (local.set $y (i64.xor (local.get $x) (i64.shr_u (local.get $x) (local.get $k))))
    (i64.xor (local.get $y) (i64.shr_u (local.get $y) (local.get $k))))

  (func (export "cancels_64") (result i64)
    (local $i i32) (local $x i64) (local $h i64) (local $y i64)
    (loop $l
      (local.set $x (call $next))
      ;; fmix64's last step and its first, as an iterated mix meets them
      (local.set $y (i64.xor (local.get $x) (i64.shr_u (local.get $x) (i64.const 33))))
      (local.set $h (i64.add (local.get $h) (i64.xor (local.get $y) (i64.shr_u (local.get $y) (i64.const 33)))))
      (local.set $y (i64.xor (local.get $x) (i64.shr_u (local.get $x) (i64.const 32))))
      (local.set $h (i64.xor (local.get $h) (i64.xor (i64.shr_u (local.get $y) (i64.const 32)) (local.get $y))))
      (local.set $y (i64.xor (i64.shl (local.get $x) (i64.const 40)) (local.get $x)))
      (local.set $h (i64.add (local.get $h) (i64.xor (local.get $y) (i64.shl (local.get $y) (i64.const 40)))))
      ;; 97 is 33 mod 64
      (local.set $y (i64.xor (local.get $x) (i64.shr_u (local.get $x) (i64.const 97))))
      (local.set $h (i64.add (local.get $h) (i64.xor (local.get $y) (i64.shr_u (local.get $y) (i64.const 33)))))
      (local.set $h (i64.rotl (local.get $h) (i64.const 7)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 200))))
    (local.get $h))

  (func (export "cancels_32") (result i32)
    (local $i i32) (local $x i32) (local $h i32) (local $y i32)
    (loop $l
      (local.set $x (i32.wrap_i64 (call $next)))
      (local.set $y (i32.xor (local.get $x) (i32.shr_u (local.get $x) (i32.const 16))))
      (local.set $h (i32.add (local.get $h) (i32.xor (local.get $y) (i32.shr_u (local.get $y) (i32.const 16)))))
      (local.set $y (i32.xor (local.get $x) (i32.shl (local.get $x) (i32.const 31))))
      (local.set $h (i32.xor (local.get $h) (i32.xor (local.get $y) (i32.shl (local.get $y) (i32.const 31)))))
      (local.set $h (i32.rotl (local.get $h) (i32.const 5)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 200))))
    (local.get $h))

  (func (export "near_misses") (result i64)
    (local $i i32) (local $x i64) (local $h i64) (local $y i64) (local $z i32) (local $w i32)
    (loop $l
      (local.set $x (call $next))
      ;; 31 is one short of half
      (local.set $y (i64.xor (local.get $x) (i64.shr_u (local.get $x) (i64.const 31))))
      (local.set $h (i64.add (local.get $h) (i64.xor (local.get $y) (i64.shr_u (local.get $y) (i64.const 31)))))
      ;; arithmetic shifts fill with the sign
      (local.set $y (i64.xor (local.get $x) (i64.shr_s (local.get $x) (i64.const 40))))
      (local.set $h (i64.xor (local.get $h) (i64.xor (local.get $y) (i64.shr_s (local.get $y) (i64.const 40)))))
      ;; two different amounts
      (local.set $y (i64.xor (local.get $x) (i64.shr_u (local.get $x) (i64.const 40))))
      (local.set $h (i64.add (local.get $h) (i64.xor (local.get $y) (i64.shr_u (local.get $y) (i64.const 48)))))
      ;; left then right
      (local.set $y (i64.xor (local.get $x) (i64.shl (local.get $x) (i64.const 40))))
      (local.set $h (i64.xor (local.get $h) (i64.xor (local.get $y) (i64.shr_u (local.get $y) (i64.const 40)))))
      ;; the intermediate is read on its own as well
      (local.set $y (i64.xor (local.get $x) (i64.shr_u (local.get $x) (i64.const 50))))
      (local.set $h (i64.add (local.get $h) (i64.add (local.get $y)
                     (i64.xor (local.get $y) (i64.shr_u (local.get $y) (i64.const 50))))))
      ;; through a call that is not inlined away is still the same value
      (local.set $h (i64.add (local.get $h) (call $twice64 (local.get $x) (i64.const 36))))
      (local.set $h (i64.add (local.get $h) (call $twice64 (local.get $x) (i64.const 20))))
      (local.set $z (i32.wrap_i64 (local.get $x)))
      (local.set $w (i32.xor (local.get $z) (i32.shr_u (local.get $z) (i32.const 15))))
      (local.set $h (i64.add (local.get $h) (i64.extend_i32_u
        (i32.xor (local.get $w) (i32.shr_u (local.get $w) (i32.const 15))))))
      (local.set $h (i64.rotl (local.get $h) (i64.const 9)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 200))))
    (local.get $h)))
