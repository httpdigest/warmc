;; Compare and arithmetic folds (pass_fold), each run over values chosen to
;; reach its edges -- zero, one, all ones, the sign bit, a constant that
;; wraps -- next to near misses that must be left alone:
;;
;;   * a test of x + c1, x - c1 or x ^ c1 against c2 is a test of x against
;;     one constant, and of x - y or x ^ y against zero a test of x against
;;     y, also when the arithmetic has other uses in its block;
;;   * x <=u 0 and x <u 1 are x == 0, x >=u 1 and x >u 0 are x != 0;
;;   * a constant on the left of an ordered compare moves to the right;
;;   * two shifts the same way by constants are one, or all of x gone;
;;   * a shift amount masked to the width is the amount;
;;   * x + (0 - y) is x - y and x - (0 - y) is x + y;
;;   * a compare of two i32s widened the same way is the i32 compare, the
;;     unsigned one when they were zero-extended;
;;   * (x * c1) << c2 is one multiply;
;;   * a load sign- or zero-extended is the wider load, except a signed byte
;;     zero-extended, which no load is;
;;   * a narrow unsigned load sign-extended is the signed load.
;;
;; The values come from an exported mutable global so nothing folds them away
;; first.

(module
  (memory 1)
  (global $seed (export "seed") (mut i64) (i64.const 0x8000_0001_ffff_fffe))
  (data (i32.const 64) "\81\82\83\84\85\86\87\88\01\80\ff\7f")

  ;; the k-th edge value: 0, 1, -1, 2^31, 2^31 - 1, 2^31 + 1, then the seed's
  ;; halves and their neighbours
  (func $v32 (param $k i32) (result i32)
    (if (result i32) (i32.lt_u (local.get $k) (i32.const 6))
      (then
        (i32.load (i32.add (i32.const 128) (i32.shl (local.get $k) (i32.const 2)))))
      (else
        (i32.add (i32.wrap_i64 (i64.rotl (global.get $seed) (i64.extend_i32_u (local.get $k))))
                 (i32.sub (local.get $k) (i32.const 8))))))
  (data (i32.const 128) "\00\00\00\00\01\00\00\00\ff\ff\ff\ff\00\00\00\80\ff\ff\ff\7f\01\00\00\80")

  (func $v64 (param $k i32) (result i64)
    (i64.add (i64.rotl (global.get $seed) (i64.extend_i32_u (i32.mul (local.get $k) (i32.const 5))))
             (i64.extend_i32_s (call $v32 (local.get $k)))))

  (func (export "tests_through_arithmetic") (result i32)
    (local $k i32) (local $acc i32) (local $x i32) (local $y i32) (local $s i32) (local $d i32)
    (loop $l
      (local.set $x (call $v32 (local.get $k)))
      (local.set $y (call $v32 (i32.xor (local.get $k) (i32.const 3))))
      (local.set $s (i32.add (local.get $x) (i32.const 0x7fff_ffff)))
      (local.set $d (i32.sub (local.get $x) (local.get $y)))
      (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
        (i32.add
          (i32.add
            (i32.add (i32.eq (local.get $s) (i32.const -2))
                     (i32.ne (i32.sub (local.get $x) (i32.const 1)) (i32.const -1)))
            (i32.add (i32.eq (i32.xor (local.get $x) (i32.const 0x8000_0000)) (i32.const 0))
                     (i32.ne (local.get $d) (i32.const 0))))
          (i32.add
            (i32.add (i32.eqz (local.get $d))
                     (i32.eqz (i32.xor (local.get $x) (local.get $y))))
            (i32.add (i32.eqz (i32.add (local.get $x) (i32.const 1)))
                     ;; the arithmetic's other uses
                     (i32.xor (local.get $s) (local.get $d)))))))
      (br_if $l (i32.lt_u (local.tee $k (i32.add (local.get $k) (i32.const 1)))
                          (i32.const 12))))
    (local.get $acc))

  (func (export "unsigned_zero_tests") (result i32)
    (local $k i32) (local $acc i32) (local $x i32) (local $w i64)
    (loop $l
      (local.set $x (call $v32 (local.get $k)))
      (local.set $w (call $v64 (local.get $k)))
      (local.set $acc (i32.add (i32.shl (local.get $acc) (i32.const 1))
        (i32.add
          (i32.add (i32.add (i32.le_u (local.get $x) (i32.const 0))
                            (i32.ge_u (local.get $x) (i32.const 1)))
                   (i32.add (i32.lt_u (local.get $x) (i32.const 1))
                            (i32.gt_u (local.get $x) (i32.const 0))))
          (i32.add (i32.add (i64.le_u (local.get $w) (i64.const 0))
                            (i64.gt_u (local.get $w) (i64.const 1)))
                   (i32.le_u (local.get $x) (i32.const 1))))))
      (br_if $l (i32.lt_u (local.tee $k (i32.add (local.get $k) (i32.const 1)))
                          (i32.const 12))))
    (local.get $acc))

  (func (export "constant_on_the_left") (result i32)
    (local $k i32) (local $acc i32) (local $x i32) (local $w i64)
    (loop $l
      (local.set $x (call $v32 (local.get $k)))
      (local.set $w (call $v64 (local.get $k)))
      (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 5))
        (i32.add
          (i32.add
            (i32.add (i32.gt_s (i32.const 0) (local.get $x))
                     (i32.lt_s (i32.const -1) (local.get $x)))
            (i32.add (i32.ge_s (i32.const 0x7fff_ffff) (local.get $x))
                     (i32.le_s (i32.const 0x8000_0000) (local.get $x))))
          (i32.add
            (i32.add (i32.gt_u (i32.const 1) (local.get $x))
                     (i32.lt_u (i32.const 0x8000_0000) (local.get $x)))
            (i32.add (i64.gt_s (i64.const 0) (local.get $w))
                     (i64.le_u (i64.const 0x8000_0001_ffff_fffe) (local.get $w)))))))
      (br_if $l (i32.lt_u (local.tee $k (i32.add (local.get $k) (i32.const 1)))
                          (i32.const 12))))
    (local.get $acc))

  (func (export "shift_pairs_and_masks") (result i64)
    (local $k i32) (local $acc i64) (local $x i32) (local $w i64)
    (loop $l
      (local.set $x (call $v32 (local.get $k)))
      (local.set $w (call $v64 (local.get $k)))
      (local.set $acc (i64.add (i64.rotl (local.get $acc) (i64.const 7))
        (i64.add
          (i64.extend_i32_u
            (i32.xor
              (i32.xor (i32.shl (i32.shl (local.get $x) (i32.const 3)) (i32.const 4))
                       (i32.shl (i32.shl (local.get $x) (i32.const 20)) (i32.const 15)))
              (i32.xor (i32.shr_s (i32.shr_s (local.get $x) (i32.const 20)) (i32.const 15))
                       (i32.xor (i32.shr_u (i32.shr_u (local.get $x) (i32.const 33)) (i32.const 2))
                                (i32.shr_u (i32.shr_u (local.get $x) (i32.const 16)) (i32.const 16))))))
          (i64.xor
            (i64.xor (i64.shl (local.get $w) (i64.and (i64.extend_i32_u (local.get $x)) (i64.const 63)))
                     (i64.shr_u (local.get $w) (i64.and (i64.extend_i32_u (local.get $x)) (i64.const 0xff))))
            (i64.xor (i64.shl (local.get $w) (i64.and (i64.extend_i32_u (local.get $x)) (i64.const 31)))
                     (i64.extend_i32_u
                       (i32.rotl (local.get $x) (i32.and (local.get $k) (i32.const 63)))))))))
      (br_if $l (i32.lt_u (local.tee $k (i32.add (local.get $k) (i32.const 1)))
                          (i32.const 12))))
    (local.get $acc))

  (func (export "negations_and_multiplies") (result i64)
    (local $k i32) (local $acc i64) (local $x i32) (local $y i32) (local $w i64)
    (loop $l
      (local.set $x (call $v32 (local.get $k)))
      (local.set $y (call $v32 (i32.add (local.get $k) (i32.const 5))))
      (local.set $w (call $v64 (local.get $k)))
      (local.set $acc (i64.add (i64.mul (local.get $acc) (i64.const 31))
        (i64.add
          (i64.extend_i32_u
            (i32.xor
              (i32.xor (i32.add (local.get $x) (i32.sub (i32.const 0) (local.get $y)))
                       (i32.add (i32.sub (i32.const 0) (local.get $y)) (i32.const 0x1234)))
              (i32.xor (i32.sub (local.get $x) (i32.sub (i32.const 0) (local.get $y)))
                       (i32.shl (i32.mul (local.get $x) (i32.const 0x10001)) (i32.const 20)))))
          (i64.xor (i64.shl (i64.mul (local.get $w) (i64.const 3)) (i64.const 62))
                   (i64.add (local.get $w) (i64.sub (i64.const 0) (i64.extend_i32_s (local.get $y))))))))
      (br_if $l (i32.lt_u (local.tee $k (i32.add (local.get $k) (i32.const 1)))
                          (i32.const 12))))
    (local.get $acc))

  (func (export "widened_compares") (result i32)
    (local $k i32) (local $acc i32) (local $x i32) (local $y i32)
    (loop $l
      (local.set $x (call $v32 (local.get $k)))
      (local.set $y (call $v32 (i32.sub (i32.const 11) (local.get $k))))
      (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
        (i32.add
          (i32.add
            (i32.add (i64.lt_s (i64.extend_i32_u (local.get $x)) (i64.extend_i32_u (local.get $y)))
                     (i64.ge_s (i64.extend_i32_u (local.get $x)) (i64.extend_i32_u (local.get $y))))
            (i32.add (i64.lt_u (i64.extend_i32_s (local.get $x)) (i64.extend_i32_s (local.get $y)))
                     (i64.gt_s (i64.extend_i32_s (local.get $x)) (i64.extend_i32_s (local.get $y)))))
          (i32.add
            (i64.eq (i64.extend_i32_u (local.get $x)) (i64.extend_i32_u (local.get $y)))
            ;; mixed widenings are not the same compare
            (i64.lt_s (i64.extend_i32_u (local.get $x)) (i64.extend_i32_s (local.get $y)))))))
      (br_if $l (i32.lt_u (local.tee $k (i32.add (local.get $k) (i32.const 1)))
                          (i32.const 12))))
    (local.get $acc))

  (func (export "widened_loads") (result i64)
    (local $k i32) (local $acc i64) (local $a i32)
    (loop $l
      (local.set $a (i32.add (i32.const 64) (i32.and (local.get $k) (i32.const 7))))
      (local.set $acc (i64.add (i64.mul (local.get $acc) (i64.const 7))
        (i64.add
          (i64.add
            (i64.add (i64.extend_i32_s (i32.load (local.get $a)))
                     (i64.extend_i32_u (i32.load (local.get $a))))
            (i64.add (i64.extend_i32_s (i32.load8_u (local.get $a)))
                     (i64.extend_i32_s (i32.load8_s (local.get $a)))))
          (i64.add
            (i64.add (i64.extend_i32_u (i32.load16_u (local.get $a)))
                     ;; a signed byte zero-extended: no load does this
                     (i64.extend_i32_u (i32.load8_s (local.get $a))))
            (i64.add
              (i64.extend_i32_s (i32.extend8_s (i32.load8_u (local.get $a))))
              (i64.add (i64.extend_i32_s (i32.extend16_s (i32.load16_u (local.get $a))))
                       (i64.extend32_s (i64.load32_u (local.get $a)))))))))
      (br_if $l (i32.lt_u (local.tee $k (i32.add (local.get $k) (i32.const 1)))
                          (i32.const 8))))
    (local.get $acc)))
