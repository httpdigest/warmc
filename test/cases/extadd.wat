;; A 64-bit add or subtract of a widened i32 widens it in the instruction (the
;; extended-register form on arm64: add x, x, w, uxtw / sxtw).  That form reads
;; only the low half of its operand, so it must be right whatever the high half
;; of the register holds, and the widening must stay unsigned or signed as
;; written:
;;
;;   * extend_u and extend_s of values with the top bit set and clear;
;;   * an i32 made by wrapping an i64 whose high half is not zero;
;;   * i64.extend32_s of an i64 whose high half is not zero;
;;   * the widened value as the second operand and as the first, of an add
;;     and of a subtract (where only the second may be folded);
;;   * a widened value read twice, which is not folded;
;;   * md5's shape: a rotate whose result is widened and added in a chain.
;;
;; The inputs come from a mutable global so nothing folds them away first.

(module
  (global $seed (mut i64) (i64.const 0xdead_beef_8000_0001))

  (func $w (param $k i32) (result i64)
    (i64.mul (i64.add (global.get $seed) (i64.extend_i32_u (local.get $k)))
             (i64.const 0x2545_f491_4f6c_dd1d)))

  (func (export "add_extend_u") (result i64)
    (local $acc i64) (local $i i32)
    (loop $l
      (local.set $acc
        (i64.add (local.get $acc)
                 (i64.extend_i32_u (i32.wrap_i64 (call $w (local.get $i))))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 50))))
    (local.get $acc))

  (func (export "add_extend_s") (result i64)
    (local $acc i64) (local $i i32)
    (loop $l
      (local.set $acc
        (i64.add (local.get $acc)
                 (i64.extend_i32_s (i32.wrap_i64 (call $w (local.get $i))))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 50))))
    (local.get $acc))

  (func (export "sub_extend_u") (result i64)
    (local $acc i64) (local $i i32)
    (loop $l
      (local.set $acc
        (i64.sub (local.get $acc)
                 (i64.extend_i32_u (i32.wrap_i64 (call $w (local.get $i))))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 50))))
    (local.get $acc))

  (func (export "sub_extend_s") (result i64)
    (local $acc i64) (local $i i32)
    (loop $l
      (local.set $acc
        (i64.sub (local.get $acc)
                 (i64.extend_i32_s (i32.wrap_i64 (call $w (local.get $i))))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 50))))
    (local.get $acc))

  ;; The widened value first: an add may swap it into place, a subtract not.
  (func (export "extend_first") (result i64)
    (local $a i64) (local $b i64) (local $i i32) (local $x i64)
    (loop $l
      (local.set $x (call $w (local.get $i)))
      (local.set $a
        (i64.add (i64.extend_i32_s (i32.wrap_i64 (local.get $x))) (local.get $a)))
      (local.set $b
        (i64.sub (i64.extend_i32_u (i32.wrap_i64 (local.get $x))) (local.get $b)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 50))))
    (i64.xor (local.get $a) (i64.rotl (local.get $b) (i64.const 17))))

  ;; extend32_s reads the low half of an i64 whose high half is set.
  (func (export "add_extend32_s") (result i64)
    (local $acc i64) (local $i i32)
    (loop $l
      (local.set $acc
        (i64.add (local.get $acc) (i64.extend32_s (call $w (local.get $i)))))
      (local.set $acc
        (i64.sub (local.get $acc)
                 (i64.extend32_s (i64.rotl (call $w (local.get $i)) (i64.const 31)))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 50))))
    (local.get $acc))

  ;; A widened value read by two adds stays one value.
  (func (export "extend_twice") (result i64)
    (local $a i64) (local $b i64) (local $i i32) (local $e i64)
    (local.set $b (i64.const -5))
    (loop $l
      (local.set $e (i64.extend_i32_u (i32.wrap_i64 (call $w (local.get $i)))))
      (local.set $a (i64.add (local.get $a) (local.get $e)))
      (local.set $b (i64.add (local.get $b) (i64.mul (local.get $e) (local.get $e))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 50))))
    (i64.xor (local.get $a) (local.get $b)))

  ;; md5's rounds as Go compiles them: values kept in i64, the 32-bit sum
  ;; rotated and widened, then added to the next word.
  (func (export "md5_shape") (result i64)
    (local $a i64) (local $b i64) (local $c i64) (local $d i64) (local $t i64)
    (local $i i32)
    (local.set $a (i64.const 0x67452301)) (local.set $b (i64.const 0xefcdab89))
    (local.set $c (i64.const 0x98badcfe)) (local.set $d (i64.const 0x10325476))
    (loop $l
      (local.set $t
        (i64.add
          (i64.extend_i32_u
            (i32.rotl
              (i32.add
                (i32.wrap_i64
                  (i64.add
                    (i64.add (i64.xor (i64.and (i64.xor (local.get $c) (local.get $d))
                                               (local.get $b))
                                      (local.get $d))
                             (local.get $a))
                    (call $w (local.get $i))))
                (i32.const 0xd76aa478))
              (i32.const 7)))
          (local.get $b)))
      (local.set $a (local.get $d)) (local.set $d (local.get $c))
      (local.set $c (local.get $b)) (local.set $b (local.get $t))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 64))))
    (i64.xor (i64.xor (local.get $a) (i64.shl (local.get $b) (i64.const 7)))
             (i64.xor (i64.shl (local.get $c) (i64.const 19)) (i64.shl (local.get $d) (i64.const 41))))))
