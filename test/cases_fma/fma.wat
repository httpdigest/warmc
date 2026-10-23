;; -ffma (pass_fma in opt.c), checking its own answers like the relaxed
;; cases do: there is no oracle, since wasm itself never fuses, so each export
;; traps if the answer is not the one it owes.  a = 1 + 2^-27 and
;; b = 1 - 2^-27 multiply to 1 - 2^-54 exactly, which rounds to 1.0 on its
;; own, so a*b - 1 is 0 rounded twice and -2^-54 fused; 1 +- 2^-13 does the
;; same in f32 (2^-26).  `fused` is what -ffma owes at -O1 and -O2, scalar,
;; in the three forms (a*b + c, c - a*b, a*b - c), and through the
;; vectoriser's loop; `unfused` what every build owes without it; `shared` a
;; multiply that is read twice, which stays as it is either way; `exact`
;; operands where both readings agree.  Inputs come in through mutable
;; globals so that nothing folds before instruction selection.
(module
  (memory 1)
  (global $a (mut f64) (f64.const 0))
  (global $b (mut f64) (f64.const 0))
  (global $c (mut f64) (f64.const 0))
  (global $fa (mut f32) (f32.const 0))
  (global $fb (mut f32) (f32.const 0))
  (global $fc (mut f32) (f32.const 0))
  (global $keep (mut f64) (f64.const 0))
  ;; each form multiplies its own a and b, scaled by powers of two so that
  ;; the product is the same exactly: the same operands, inlined, would make
  ;; one product read twice, and -ffma fuses only a multiply with no other
  ;; reader
  (global $a2 (mut f64) (f64.const 0))
  (global $b2 (mut f64) (f64.const 0))
  (global $a3 (mut f64) (f64.const 0))
  (global $b3 (mut f64) (f64.const 0))
  (func $chk (param $got f64) (param $want f64)
    (if (i64.ne (i64.reinterpret_f64 (local.get $got)) (i64.reinterpret_f64 (local.get $want)))
      (then unreachable)))
  (func $chk32 (param $got f32) (param $want f32)
    (if (i32.ne (i32.reinterpret_f32 (local.get $got)) (i32.reinterpret_f32 (local.get $want)))
      (then unreachable)))
  (func $set (param f64 f64 f64)
    (global.set $a (local.get 0)) (global.set $b (local.get 1)) (global.set $c (local.get 2))
    (global.set $a2 (f64.mul (local.get 0) (f64.const 2)))
    (global.set $b2 (f64.mul (local.get 1) (f64.const 0.5)))
    (global.set $a3 (f64.mul (local.get 0) (f64.const 4)))
    (global.set $b3 (f64.mul (local.get 1) (f64.const 0.25)))
    (global.set $fa (f32.const 0x1.0008p+0)) (global.set $fb (f32.const 0x1.fffp-1)))
  ;; the three forms, scalar
  (func $madd (result f64) (f64.add (f64.mul (global.get $a) (global.get $b)) (global.get $c)))
  (func $msub (result f64) (f64.sub (global.get $c) (f64.mul (global.get $a2) (global.get $b2))))
  (func $mrev (result f64) (f64.sub (f64.mul (global.get $a3) (global.get $b3)) (global.get $c)))
  (func $madd32 (result f32) (f32.add (f32.mul (global.get $fa) (global.get $fb)) (global.get $fc)))
  ;; a multiply read twice: its product is kept too
  (func $twice (result f64) (local $m f64)
    (local.set $m (f64.mul (global.get $a) (global.get $b)))
    (global.set $keep (local.get $m))
    (f64.add (local.get $m) (global.get $c)))
  ;; o[i] = x[i] * y[i] + z[i], or z[i] - x[i] * y[i], over 64 f64 the vectoriser takes
  (func $vloop (param $neg i32)
    (local $i i32)
    (local.set $i (i32.const 0))
    (loop $f
      (f64.store offset=1024 (local.get $i) (global.get $a))
      (f64.store offset=2048 (local.get $i) (global.get $b))
      (f64.store offset=3072 (local.get $i) (global.get $c))
      (br_if $f (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 8))) (i32.const 512))))
    (local.set $i (i32.const 0))
    (if (local.get $neg)
      (then
        (loop $l
          (f64.store (local.get $i)
            (f64.sub (f64.load offset=3072 (local.get $i))
                     (f64.mul (f64.load offset=1024 (local.get $i)) (f64.load offset=2048 (local.get $i)))))
          (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 8))) (i32.const 512)))))
      (else
        (loop $l
          (f64.store (local.get $i)
            (f64.add (f64.mul (f64.load offset=1024 (local.get $i)) (f64.load offset=2048 (local.get $i)))
                     (f64.load offset=3072 (local.get $i))))
          (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 8))) (i32.const 512)))))))
  (func $vchk (param $want f64) (local $i i32)
    (loop $l
      (call $chk (f64.load (local.get $i)) (local.get $want))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 8))) (i32.const 512)))))

  (func (export "fused")
    (call $set (f64.const 0x1.0000002p+0) (f64.const 0x1.ffffffcp-1) (f64.const -1))
    (global.set $fc (f32.const -1))
    (call $chk (call $madd) (f64.const -0x1p-54))
    (call $chk32 (call $madd32) (f32.const -0x1p-26))
    (call $vloop (i32.const 0))
    (call $vchk (f64.const -0x1p-54))
    (call $set (f64.const 0x1.0000002p+0) (f64.const 0x1.ffffffcp-1) (f64.const 1))
    (call $chk (call $msub) (f64.const 0x1p-54))
    (call $chk (call $mrev) (f64.const -0x1p-54))
    (call $vloop (i32.const 1))
    (call $vchk (f64.const 0x1p-54)))
  (func (export "unfused")
    (call $set (f64.const 0x1.0000002p+0) (f64.const 0x1.ffffffcp-1) (f64.const -1))
    (global.set $fc (f32.const -1))
    (call $chk (call $madd) (f64.const 0))
    (call $chk32 (call $madd32) (f32.const 0))
    (call $vloop (i32.const 0))
    (call $vchk (f64.const 0))
    (call $set (f64.const 0x1.0000002p+0) (f64.const 0x1.ffffffcp-1) (f64.const 1))
    (call $chk (call $msub) (f64.const 0))
    (call $chk (call $mrev) (f64.const 0))
    (call $vloop (i32.const 1))
    (call $vchk (f64.const 0)))
  (func (export "shared")
    (call $set (f64.const 0x1.0000002p+0) (f64.const 0x1.ffffffcp-1) (f64.const -1))
    (call $chk (call $twice) (f64.const 0))
    (call $chk (global.get $keep) (f64.const 1)))
  (func (export "exact")
    (call $set (f64.const 3) (f64.const 5) (f64.const 2))
    (global.set $fa (f32.const 3)) (global.set $fb (f32.const 5)) (global.set $fc (f32.const 2))
    (call $chk (call $madd) (f64.const 17))
    (call $chk (call $msub) (f64.const -13))
    (call $chk (call $mrev) (f64.const 13))
    (call $chk32 (call $madd32) (f32.const 17))
    (call $vloop (i32.const 0))
    (call $vchk (f64.const 17)))
)
