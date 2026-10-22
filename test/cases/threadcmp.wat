;; Jump threading through a block whose one instruction compares a parameter
;; with a constant and branches on it: an edge that hands the parameter a
;; constant goes straight to the side the compare picks.  This is the shape of
;; `if ((c ? x : 1) == 0)`, which binaryen leaves wherever a select-like if
;; feeds a test.  Each export runs the shape over inputs that take every arm:
;;
;;   * eqz of an i32 merged from a computed arm and a constant arm;
;;   * eq and ne with a non-zero constant, both arms constant;
;;   * an i64 parameter whose constant has bits above the low half;
;;   * a compare whose result is also read after the branch, which is left.
;;
;; The inputs come from a mutable global so nothing folds them away first.

(module
  (global $seed (mut i32) (i32.const 0x2545_f491))

  (func $v (param $k i32) (result i32)
    (i32.mul (i32.xor (global.get $seed) (local.get $k)) (i32.const 0x9e37_79b9)))

  (func $eqz_arm (param $k i32) (result i32)
    (local $x i32)
    (local.set $x (call $v (local.get $k)))
    (if (result i32)
        (i32.eqz
          (if (result i32) (i32.ge_u (i32.and (local.get $x) (i32.const 15)) (i32.const 4))
            (then (i32.and (local.get $x) (i32.const 0x300)))
            (else (i32.const 1))))
      (then (i32.add (local.get $x) (i32.const 1000)))
      (else (i32.sub (local.get $x) (i32.const 77)))))

  (func $eq_arms (param $k i32) (result i32)
    (local $x i32)
    (local.set $x (call $v (local.get $k)))
    (i32.add
      (if (result i32)
          (i32.eq
            (if (result i32) (i32.lt_s (local.get $x) (i32.const 0))
              (then (i32.const 5)) (else (i32.const 7)))
            (i32.const 5))
        (then (i32.rotl (local.get $x) (i32.const 3)))
        (else (i32.xor (local.get $x) (i32.const 0x55))))
      (if (result i32)
          (i32.ne
            (if (result i32) (i32.and (local.get $x) (i32.const 2))
              (then (i32.const 9)) (else (i32.const -9)))
            (i32.const -9))
        (then (i32.const 11)) (else (i32.const 13)))))

  (func $i64_arm (param $k i32) (result i64)
    (local $x i64)
    (local.set $x (i64.extend_i32_u (call $v (local.get $k))))
    (if (result i64)
        (i64.ne
          (if (result i64) (i64.gt_u (local.get $x) (i64.const 0x8000_0000))
            (then (i64.const 0x1_0000_0000))
            (else (i64.and (local.get $x) (i64.const 0xff))))
          (i64.const 0))
      (then (i64.shl (local.get $x) (i64.const 7)))
      (else (i64.const -3))))

  ;; The compare is read again after the branch.
  (func $read_again (param $k i32) (result i32)
    (local $x i32) (local $t i32)
    (local.set $x (call $v (local.get $k)))
    (local.set $t
      (i32.eqz
        (if (result i32) (i32.ge_u (i32.and (local.get $x) (i32.const 7)) (i32.const 3))
          (then (i32.and (local.get $x) (i32.const 0x40)))
          (else (i32.const 1)))))
    (i32.add
      (if (result i32) (local.get $t)
        (then (i32.const 100)) (else (i32.const 200)))
      (i32.mul (local.get $t) (local.get $x))))

  (func (export "eqz_arm") (result i32)
    (local $i i32) (local $s i32)
    (loop $l
      (local.set $s (i32.add (i32.rotl (local.get $s) (i32.const 5)) (call $eqz_arm (local.get $i))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 64))))
    (local.get $s))

  (func (export "eq_arms") (result i32)
    (local $i i32) (local $s i32)
    (loop $l
      (local.set $s (i32.add (i32.rotl (local.get $s) (i32.const 5)) (call $eq_arms (local.get $i))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 64))))
    (local.get $s))

  (func (export "i64_arm") (result i64)
    (local $i i32) (local $s i64)
    (loop $l
      (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 5)) (call $i64_arm (local.get $i))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 64))))
    (local.get $s))

  (func (export "read_again") (result i32)
    (local $i i32) (local $s i32)
    (loop $l
      (local.set $s (i32.add (i32.rotl (local.get $s) (i32.const 5)) (call $read_again (local.get $i))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 64))))
    (local.get $s)))
