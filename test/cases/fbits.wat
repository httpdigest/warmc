;; Float constants written as integer bits and a reinterpret, as GHC writes
;; every double it uses, which fold to float constants of the same bits: ones
;; an fmov immediate encodes (1.0, 0.5, -2.0) and ones it does not, -0.0, a
;; denormal, the largest finite value, and NaNs whose payloads must survive.
;; Each is used in a loop the way spectral uses its 1.0, and returned as bits,
;; and an integer constant made from float bits goes the other way.

(module
  (global $seed (mut i32) (i32.const 77))
  (func $next (result i32)
    (global.set $seed (i32.add (i32.mul (global.get $seed) (i32.const 1103515245))
                               (i32.const 12345)))
    (i32.shr_u (global.get $seed) (i32.const 8)))

  (func (export "harmonic_by_bits") (result i64)
    (local $i i32) (local $s f64)
    (loop $l
      (local.set $s (f64.add (local.get $s)
        (f64.div (f64.reinterpret_i64 (i64.const 0x3ff0000000000000))
                 (f64.convert_i32_s (i32.add (local.get $i) (i32.const 1))))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 1000))))
    (i64.reinterpret_f64 (local.get $s)))

  (func (export "f64_constants_by_bits") (result i64)
    (local $i i32) (local $x f64) (local $h i64)
    (loop $l
      (local.set $x (f64.convert_i32_s (call $next)))
      (local.set $h (i64.xor (i64.rotl (local.get $h) (i64.const 5)) (i64.reinterpret_f64
        (f64.add (f64.mul (local.get $x) (f64.reinterpret_i64 (i64.const 0x3fe0000000000000)))
                 (f64.reinterpret_i64 (i64.const 0xc000000000000000))))))
      (local.set $h (i64.add (local.get $h) (i64.reinterpret_f64
        (f64.mul (local.get $x) (f64.reinterpret_i64 (i64.const 0x400921fb54442d18))))))
      (local.set $h (i64.add (local.get $h) (i64.reinterpret_f64
        (f64.copysign (local.get $x) (f64.reinterpret_i64 (i64.const 0x8000000000000000))))))
      (local.set $h (i64.add (local.get $h) (i64.reinterpret_f64
        (f64.max (local.get $x) (f64.reinterpret_i64 (i64.const 0x0000000000000001))))))
      (local.set $h (i64.add (local.get $h) (i64.reinterpret_f64
        (f64.min (local.get $x) (f64.reinterpret_i64 (i64.const 0x7fefffffffffffff))))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 300))))
    (local.get $h))

  (func (export "f32_constants_by_bits") (result i32)
    (local $i i32) (local $x f32) (local $h i32)
    (loop $l
      (local.set $x (f32.convert_i32_s (call $next)))
      (local.set $h (i32.xor (i32.rotl (local.get $h) (i32.const 3)) (i32.reinterpret_f32
        (f32.sub (local.get $x) (f32.reinterpret_i32 (i32.const 0x3f800000))))))
      (local.set $h (i32.add (local.get $h) (i32.reinterpret_f32
        (f32.mul (local.get $x) (f32.reinterpret_i32 (i32.const 0x3dcccccd))))))
      (local.set $h (i32.add (local.get $h) (i32.reinterpret_f32
        (f32.abs (f32.reinterpret_i32 (i32.const 0x80000000))))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 300))))
    (local.get $h))

  ;; a NaN constant passes through a reinterpret and back with its payload
  (func (export "nan_payloads") (result i64)
    (i64.add
      (i64.reinterpret_f64 (f64.reinterpret_i64 (i64.const 0x7ff4000000000123)))
      (i64.extend_i32_u (i32.reinterpret_f32 (f32.reinterpret_i32 (i32.const 0xffa00042))))))

  ;; integer constants from float bits
  (func (export "ints_from_float_bits") (result i64)
    (i64.add (i64.reinterpret_f64 (f64.const -0x1.8p-1022))
             (i64.extend_i32_u (i32.reinterpret_f32 (f32.const 0x1.fffffep+127))))))
