;; eq and ne against a constant with a bit the value can never have are
;; decided whatever the value (opt.c fold_cmp_never, by known bits): a mask
;; that leaves the constant's bit clear, a shift that clears the low bits,
;; a shift that clears the top one, an i32 widened to i64.  A constant inside what the value may
;; hold is compared at run time, as is a value with a bit forced set.
;; (The functions decided apart differ, so none is merged with another.)
;;
;; shape: ne_out \s(tst|cmp|and|cset)\s=0
;; shape: eq_out \s(tst|cmp|and|cset)\s=0
;; shape: eq64_shl \s(tst|cmp|lsl|cset)\s=0
;; shape: eq_hi \s(tst|cmp|lsr|cset)\s=0
;; shape: ne64_ext \s(tst|cmp|cset)\s=0
;; shape: ne_in \scset\s=1
;; shape: eq_in \scset\s=1
;; shape: ne64_shl \scset\s=1
;; shape: eq_or \scset\s=1
;;
;; run: ne_out 0 => i32:1
;; run: ne_out 5 => i32:1
;; run: ne_out -1 => i32:1
;; run: eq_out 0 => i32:0
;; run: eq_out 65792 => i32:0
;; run: eq_out -1 => i32:0
;; run: eq_in 4608 => i32:1
;; run: eq_in 70144 => i32:1
;; run: eq_in 4609 => i32:1
;; run: eq_in 4864 => i32:0
;; run: eq_in 0 => i32:0
;; run: ne_in 4608 => i32:0
;; run: ne_in 0 => i32:1
;; run: ne_in -1 => i32:1
;; run: eq64_shl 0 => i32:30
;; run: eq64_shl 127 => i32:30
;; run: eq64_shl -1 => i32:30
;; run: ne64_shl 127 => i32:0
;; run: ne64_shl 128 => i32:1
;; run: ne64_shl -1 => i32:1
;; run: eq_hi -1 => i32:40
;; run: eq_hi -2147483648 => i32:40
;; run: eq_hi 0 => i32:40
;; run: ne64_ext 0 => i32:51
;; run: ne64_ext -1 => i32:51
;; run: eq_or 0 => i32:1
;; run: eq_or -2147483648 => i32:1
;; run: eq_or 1 => i32:0
;; run: eq_or -1 => i32:0

(module
  (func $ne_out (export "ne_out") (param $x i32) (result i32)
    (i32.ne (i32.and (local.get $x) (i32.const 0xff00)) (i32.const 5)))
  (func $eq_out (export "eq_out") (param $x i32) (result i32)
    (i32.eq (i32.and (local.get $x) (i32.const 0xff00)) (i32.const 0x10100)))
  (func $eq_in (export "eq_in") (param $x i32) (result i32)
    (i32.eq (i32.and (local.get $x) (i32.const 0xff00)) (i32.const 0x1200)))
  (func $ne_in (export "ne_in") (param $x i32) (result i32)
    (i32.ne (i32.and (local.get $x) (i32.const 0xff00)) (i32.const 0x1200)))
  (func $eq64_shl (export "eq64_shl") (param $x i64) (result i32)
    (i32.add (i64.eq (i64.shl (local.get $x) (i64.const 8)) (i64.const 0x7f)) (i32.const 30)))
  (func $ne64_shl (export "ne64_shl") (param $x i64) (result i32)
    (i64.ne (i64.shl (local.get $x) (i64.const 8)) (i64.const 0x7f00)))
  (func $eq_hi (export "eq_hi") (param $x i32) (result i32)
    (i32.add (i32.eq (i32.shr_u (local.get $x) (i32.const 1)) (i32.const 0x80000000)) (i32.const 40)))
  (func $ne64_ext (export "ne64_ext") (param $x i32) (result i32)
    (i32.add (i64.ne (i64.extend_i32_u (local.get $x)) (i64.const 0x100000000)) (i32.const 50)))
  (func $eq_or (export "eq_or") (param $x i32) (result i32)
    (i32.eq (i32.or (local.get $x) (i32.const 0x80000000)) (i32.const 0x80000000)))
)
