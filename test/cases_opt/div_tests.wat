;; Signed division traps on the least value divided by -1; a dividend that
;; cannot be the least value needs no such test (ir.c ir_not_int_min): an or
;; with a bit below the sign, a sign bit known clear, an i32 widened -- not
;; an or with the sign bit only.  A divisor's zero test is the source's own
;; where that tested the value before widening it, or the i64 it wraps
;; (ir_nonzero_with: rsbench/div64's d == 0 on the i32 it narrowed) -- not a
;; test of another value.  Answers worked out in Python.
;;
;; shape: notmin \sccmp\s=0
;; shape: notmin32 \sccmp\s=0
;; shape: positive \sccmp\s=0
;; shape: widened \sccmp\s=0
;; shape: maybemin \sccmp\s=1
;; shape: signonly \sccmp\s=1
;; shape: zext \scbn?z\s=1
;; shape: zne \scbn?z\s=1
;; shape: wrapd \scbn?z\s=1
;; shape: otherd \scbn?z\s=2
;;
;; run: notmin 0 1 => i64:1
;; run: positive 0 1 => i64:0
;; run: maybemin 0 1 => i64:-9223372036854775808
;; run: notmin 7 2 => i64:3
;; run: positive 7 2 => i64:1
;; run: maybemin 7 2 => i64:-4611686018427387900
;; run: notmin -7 2 => i64:-3
;; run: positive -7 2 => i64:4611686018427387902
;; run: maybemin -7 2 => i64:-3
;; run: notmin -9223372036854775808 -1 => i64:9223372036854775807
;; run: positive -9223372036854775808 -1 => i64:-4611686018427387904
;; run: maybemin -9223372036854775808 -1 => trap
;; run: notmin -9223372036854775807 -1 => i64:9223372036854775807
;; run: positive -9223372036854775807 -1 => i64:-4611686018427387904
;; run: maybemin -9223372036854775807 -1 => i64:9223372036854775807
;; run: notmin -1 -1 => i64:1
;; run: positive -1 -1 => i64:-9223372036854775807
;; run: maybemin -1 -1 => i64:1
;; run: notmin 9223372036854775807 -3 => i64:-3074457345618258602
;; run: positive 9223372036854775807 -3 => i64:-1537228672809129301
;; run: maybemin 9223372036854775807 -3 => i64:0
;; run: notmin 12345 -7 => i64:-1763
;; run: positive 12345 -7 => i64:-881
;; run: maybemin 12345 -7 => i64:1317624576693537637
;; run: notmin32 0 1 => i32:64
;; run: signonly 0 1 => i32:-2147483648
;; run: notmin32 7 2 => i32:35
;; run: signonly 7 2 => i32:-1073741820
;; run: notmin32 -7 2 => i32:-3
;; run: signonly -7 2 => i32:-3
;; run: notmin32 -2147483648 -1 => i32:2147483584
;; run: signonly -2147483648 -1 => trap
;; run: notmin32 -1 -1 => i32:1
;; run: signonly -1 -1 => i32:1
;; run: notmin32 2147483647 -3 => i32:-715827882
;; run: signonly 2147483647 -3 => i32:0
;; run: notmin32 -64 -1 => i32:64
;; run: signonly -64 -1 => i32:64
;; run: widened 0 1 => i64:0
;; run: widened -2147483648 -1 => i64:2147483648
;; run: widened 2147483647 -1 => i64:-2147483647
;; run: widened -5 2 => i64:-2
;; run: zext 100 7 => i64:14
;; run: zne 100 7 => i64:2
;; run: otherd 100 7 1 => i64:14
;; run: zext 100 0 => i64:-1
;; run: zne 100 0 => i64:-2
;; run: otherd 100 0 1 => trap integer divide by zero
;; run: zext -1 3 => i64:6148914691236517205
;; run: zne -1 3 => i64:0
;; run: otherd -1 3 1 => i64:6148914691236517205
;; run: zext 5 -1 => i64:0
;; run: zne 5 -1 => i64:5
;; run: otherd 5 -1 1 => i64:0
;; run: wrapd 100 7 => i32:14
;; run: wrapd 100 4294967296 => i32:-3
;; run: wrapd -1 3 => i32:1431655765

(module
  (func $notmin (export "notmin") (param $x i64) (param $y i64) (result i64)
    (i64.div_s (i64.or (local.get $x) (i64.const 1)) (local.get $y)))
  (func $notmin32 (export "notmin32") (param $x i32) (param $y i32) (result i32)
    (i32.div_s (i32.or (local.get $x) (i32.const 0x40)) (local.get $y)))
  (func $positive (export "positive") (param $x i64) (param $y i64) (result i64)
    (i64.div_s (i64.shr_u (local.get $x) (i64.const 1)) (local.get $y)))
  (func $widened (export "widened") (param $x i32) (param $y i64) (result i64)
    (i64.div_s (i64.extend_i32_s (local.get $x)) (local.get $y)))
  (func $maybemin (export "maybemin") (param $x i64) (param $y i64) (result i64)
    (i64.div_s (i64.or (local.get $x) (i64.const 0x8000000000000000)) (local.get $y)))
  (func $signonly (export "signonly") (param $x i32) (param $y i32) (result i32)
    (i32.div_s (i32.or (local.get $x) (i32.const 0x80000000)) (local.get $y)))
  (func $zext (export "zext") (param $x i64) (param $d i32) (result i64)
    (if (i32.eqz (local.get $d)) (then (return (i64.const -1))))
    (i64.div_u (local.get $x) (i64.extend_i32_u (local.get $d))))
  (func $zne (export "zne") (param $x i64) (param $d i32) (result i64)
    (if (result i64) (i32.ne (local.get $d) (i32.const 0))
      (then (i64.rem_u (local.get $x) (i64.extend_i32_s (local.get $d))))
      (else (i64.const -2))))
  (func $wrapd (export "wrapd") (param $x i32) (param $d i64) (result i32)
    (if (i64.eqz (i64.and (local.get $d) (i64.const 0xffffffff))) (then (return (i32.const -3))))
    (i32.div_u (local.get $x) (i32.wrap_i64 (local.get $d))))
  (func $otherd (export "otherd") (param $x i64) (param $d i32) (param $e i32) (result i64)
    (if (i32.eqz (local.get $e)) (then (return (i64.const -1))))
    (i64.div_u (local.get $x) (i64.extend_i32_u (local.get $d))))
)
