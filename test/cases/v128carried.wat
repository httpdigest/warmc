;; pass_reassoc_carried on v128 accumulators: a chain of lane-wise integer
;; adds, multiplies or bit ops onto a loop-carried vector is reassociated so
;; the terms combine first and only the last link waits on the accumulator,
;; as Zig's unrolled vector sum needs.  Only when every link has the same
;; lanes: an i32x4 add and an i64x2 add carry differently, so a chain mixing
;; them must stay as written, and so must one through a saturating add.
(module
  (memory 1)
  (global $n (mut i32) (i32.const 64))
  (data (i32.const 0) "\01\02\03\04\05\06\07\08\09\0a\0b\0c\0d\0e\0f\10\ff\fe\fd\fc\fb\fa\f9\f8\f7\f6\f5\f4\f3\f2\f1\f0")
  (func $init
    (local $i i32)
    (loop $l
      (i32.store (i32.add (i32.const 32) (local.get $i))
                 (i32.mul (i32.add (local.get $i) (i32.const 0x01010101)) (i32.const 0x9e3779b9)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 4))) (i32.const 4096)))))
  (func $v (param $i i32) (result v128)
    (v128.load offset=32 (i32.and (i32.mul (local.get $i) (i32.const 16)) (i32.const 4080))))
  (func $sum (param $x v128) (result i64)
    (i64.add (i64x2.extract_lane 0 (local.get $x)) (i64.rotl (i64x2.extract_lane 1 (local.get $x)) (i64.const 17))))

  (func (export "add32_chain") (result i64)
    (local $i i32) (local $acc v128)
    (call $init)
    (loop $l
      (local.set $acc (i32x4.add (i32x4.add (i32x4.add (i32x4.add (local.get $acc)
        (call $v (local.get $i))) (call $v (i32.add (local.get $i) (i32.const 1))))
        (call $v (i32.add (local.get $i) (i32.const 2)))) (call $v (i32.add (local.get $i) (i32.const 3)))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 4))) (global.get $n))))
    (call $sum (local.get $acc)))
  (func (export "mul16_chain") (result i64)
    (local $i i32) (local $acc v128)
    (call $init)
    (local.set $acc (i16x8.splat (i32.const 1)))
    (loop $l
      (local.set $acc (i16x8.mul (i16x8.mul (i16x8.mul (local.get $acc)
        (call $v (local.get $i))) (call $v (i32.add (local.get $i) (i32.const 5))))
        (v128.or (call $v (i32.add (local.get $i) (i32.const 7))) (i16x8.splat (i32.const 1)))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (global.get $n))))
    (call $sum (local.get $acc)))
  (func (export "xor_chain") (result i64)
    (local $i i32) (local $acc v128)
    (call $init)
    (loop $l
      (local.set $acc (v128.xor (v128.xor (v128.xor (local.get $acc) (call $v (local.get $i)))
        (i8x16.shl (call $v (i32.add (local.get $i) (i32.const 3))) (i32.const 1)))
        (call $v (i32.add (local.get $i) (i32.const 9)))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (global.get $n))))
    (call $sum (local.get $acc)))
  ;; lanes that differ along the chain
  (func (export "mixed_lanes") (result i64)
    (local $i i32) (local $acc v128)
    (call $init)
    (loop $l
      (local.set $acc (i64x2.add (i32x4.add (i64x2.add (local.get $acc)
        (call $v (local.get $i))) (call $v (i32.add (local.get $i) (i32.const 1))))
        (call $v (i32.add (local.get $i) (i32.const 2)))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (global.get $n))))
    (call $sum (local.get $acc)))
  (func (export "mixed_8_16") (result i64)
    (local $i i32) (local $acc v128)
    (call $init)
    (loop $l
      (local.set $acc (i8x16.add (i16x8.add (i8x16.add (local.get $acc)
        (call $v (local.get $i))) (call $v (i32.add (local.get $i) (i32.const 1))))
        (call $v (i32.add (local.get $i) (i32.const 2)))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (global.get $n))))
    (call $sum (local.get $acc)))
  ;; a saturating add in the chain
  (func (export "sat_link") (result i64)
    (local $i i32) (local $acc v128)
    (call $init)
    (loop $l
      (local.set $acc (i8x16.add (i8x16.add_sat_u (i8x16.add (local.get $acc)
        (call $v (local.get $i))) (call $v (i32.add (local.get $i) (i32.const 1))))
        (call $v (i32.add (local.get $i) (i32.const 2)))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (global.get $n))))
    (call $sum (local.get $acc)))
  ;; float lanes are not reassociated
  (func (export "f32_chain") (result i64)
    (local $i i32) (local $acc v128)
    (call $init)
    (loop $l
      (local.set $acc (f32x4.add (f32x4.add (f32x4.add (local.get $acc)
        (f32x4.convert_i32x4_s (call $v (local.get $i))))
        (f32x4.mul (f32x4.convert_i32x4_s (call $v (i32.add (local.get $i) (i32.const 1)))) (f32x4.splat (f32.const 1e-9))))
        (f32x4.splat (f32.const 1e7))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (global.get $n))))
    (call $sum (local.get $acc)))
)
