;; What an exported function can hand back through warmc_invoke.
;;
;; A function returning more than one value writes them through x8, at a
;; sixteen-byte slot each, into a buffer the caller owns.  warmc_invoke then
;; stored x0 and q0 into that same buffer to carry a single result back -- at
;; offsets 128 and 144, which are slot 8 and slot 9.  So a ten-result export
;; came back with its last two values overwritten by the scratch, and the CLI
;; harness printed only the first eight, which is why nothing said so.  Past
;; that the writes simply left the buffer: the eleventh result was the caller's
;; frame.
;;
;; The scratch lives at the front now and the results start after it, so the
;; count below is what the ABI really carries.  Sixteen is the limit, and
;; codegen_prepare_module refuses an export that wants more rather than letting
;; it write past the end.
;;
;; The shapes here are the ones that read a slot differently: past slot 8 for
;; the overwrite, at the limit for the boundary, floats because they come back
;; through a vector register, and a v128 because it is the one result that uses
;; the whole of its slot rather than the low half.
(module
  (func (export "ten_i64") (result i64 i64 i64 i64 i64 i64 i64 i64 i64 i64)
    (i64.const 1000) (i64.const 1001) (i64.const 1002) (i64.const 1003)
    (i64.const 1004) (i64.const 1005) (i64.const 1006) (i64.const 1007)
    (i64.const 1008) (i64.const 1009))

  (func (export "sixteen_i32")
        (result i32 i32 i32 i32 i32 i32 i32 i32
                i32 i32 i32 i32 i32 i32 i32 i32)
    (i32.const 0) (i32.const 1) (i32.const 2) (i32.const 3)
    (i32.const 4) (i32.const 5) (i32.const 6) (i32.const 7)
    (i32.const 8) (i32.const 9) (i32.const 10) (i32.const 11)
    (i32.const 12) (i32.const 13) (i32.const 14) (i32.const 15))

  ;; Floats travel in a vector register, and the scratch that used to land on
  ;; slot 9 was a q register.
  (func (export "mixed_wide")
        (result i32 f64 i64 f32 i32 f64 i64 f32 i32 f64 i64 f32)
    (i32.const 70) (f64.const 0x1.91eb851eb851fp+1) (i64.const 71)
    (f32.const 0x1.6666666666666p+1)
    (i32.const 72) (f64.const 0x1.3333333333333p+2) (i64.const 73)
    (f32.const -0x1.4cccccccccccdp+2)
    (i32.const 74) (f64.const -0x1.8p+0) (i64.const 75)
    (f32.const 0x1.8p+3))

  ;; A v128 fills its slot rather than the low half of it, so it is the shape
  ;; that catches a stride computed for eight bytes.
  (func (export "vec_slots") (result v128 i64 v128 i64 v128)
    (v128.const i32x4 0x11111111 0x22222222 0x33333333 0x44444444)
    (i64.const 55)
    (v128.const i32x4 0x55555555 0x66666666 0x77777777 0x88888888)
    (i64.const 66)
    (v128.const i32x4 0x99999999 0xaaaaaaaa 0xbbbbbbbb 0xcccccccc))

  ;; One result still goes back in x0 or v0, and those moved too.
  (func (export "one_i64") (result i64) (i64.const 0x0123456789abcdef))
  (func (export "one_f64") (result f64) (f64.const 0x1.921fb54442d18p+1))
  (func (export "one_v128") (result v128)
    (v128.const i32x4 0xdeadbeef 0xfeedface 0x0badc0de 0x8badf00d))
  (func (export "none"))
)
