;; An eq or ne of a value with its width's least integer -- luawasm's mark
;; of a field that holds no number, 0x8000000000000000 -- is a cmp of it
;; with 1, whose subtraction overflows there and nowhere else: VS for eq
;; and VC for ne (isel_ops.inc is_min_cmp, min_cmps), where the constant
;; took a register, made before every test.  In a value (cset), a branch,
;; a select, and a compare chain: as its first compare, and as its ccmp,
;; where both compares are of the least integer (chain3).  Answers worked
;; out in Python.
;;
;; stats -O2 a64: min_cmps=9
;; shape: eq64 cmp\tx\d+, #0x1\b=1
;; shape: eq64 cset\tw\d+, vs=1
;; shape: ne64 cset\tw\d+, vc=1
;; shape: eq32 cmp\tw\d+, #0x1\b=1
;; shape: eq32 cset\tw\d+, vs=1
;; shape: br64 b\.v[sc]\b=1
;; shape: brne32 b\.v[sc]\b=1
;; shape: sel csel\tw\d+, w\d+, w\d+, vs=1
;; shape: chain ccmp\s=1
;; shape: chain3 ccmp\tw\d+, #0x1, =1
;; shape: chain3 #-0x80000000=0
;; shape: br64 #-0x80000000=0
;;
;; run: eq64 -9223372036854775808 => i32:1
;; run: eq64 -9223372036854775807 => i32:0
;; run: eq64 9223372036854775807 => i32:0
;; run: eq64 -1 => i32:0
;; run: eq64 0 => i32:0
;; run: eq64 1 => i32:0
;; run: ne64 -9223372036854775808 => i32:0
;; run: ne64 -9223372036854775807 => i32:1
;; run: ne64 9223372036854775807 => i32:1
;; run: ne64 -1 => i32:1
;; run: ne64 0 => i32:1
;; run: ne64 1 => i32:1
;; run: eq32 -2147483648 => i32:1
;; run: eq32 -2147483647 => i32:0
;; run: eq32 2147483647 => i32:0
;; run: eq32 -1 => i32:0
;; run: eq32 0 => i32:0
;; run: eq32 1 => i32:0
;; run: br64 -9223372036854775808 => i32:3
;; run: br64 -9223372036854775807 => i32:1
;; run: br64 9223372036854775807 => i32:-1
;; run: br64 -1 => i32:-1
;; run: br64 0 => i32:0
;; run: br64 1 => i32:1
;; run: brne32 -2147483648 => i32:-7
;; run: brne32 -2147483647 => i32:-2147483646
;; run: brne32 2147483647 => i32:-2147483648
;; run: brne32 -1 => i32:0
;; run: brne32 0 => i32:1
;; run: brne32 1 => i32:2
;; run: sel -9223372036854775808 5 9 => i32:5
;; run: sel -9223372036854775807 5 9 => i32:9
;; run: sel 9223372036854775807 5 9 => i32:9
;; run: sel -1 5 9 => i32:9
;; run: sel 0 5 9 => i32:9
;; run: sel 1 5 9 => i32:9
;; run: chain -9223372036854775808 3 => i32:22
;; run: chain -9223372036854775808 4 => i32:11
;; run: chain -9223372036854775808 -9223372036854775808 => i32:11
;; run: chain -9223372036854775807 3 => i32:22
;; run: chain -9223372036854775807 4 => i32:22
;; run: chain -9223372036854775807 -9223372036854775808 => i32:22
;; run: chain 0 3 => i32:22
;; run: chain 0 4 => i32:22
;; run: chain 0 -9223372036854775808 => i32:22
;; run: chain2 -9223372036854775808 1 2 => i32:33
;; run: chain2 -9223372036854775808 2 1 => i32:44
;; run: chain2 1 1 2 => i32:33
;; run: chain2 1 2 1 => i32:33
;; run: chain3 -9223372036854775808 -2147483648 => i32:66
;; run: chain3 -9223372036854775808 -2147483647 => i32:55
;; run: chain3 -9223372036854775808 0 => i32:55
;; run: chain3 -9223372036854775807 -2147483648 => i32:66
;; run: chain3 -9223372036854775807 -2147483647 => i32:66
;; run: chain3 -9223372036854775807 0 => i32:66
(module
  (memory 1)
  (func $eq64 (export "eq64") (param $x i64) (result i32)
    (i64.eq (local.get $x) (i64.const 0x8000000000000000)))
  (func $ne64 (export "ne64") (param $x i64) (result i32)
    (i64.ne (local.get $x) (i64.const 0x8000000000000000)))
  (func $eq32 (export "eq32") (param $x i32) (result i32)
    (i32.eq (local.get $x) (i32.const 0x80000000)))
  (func $br64 (export "br64") (param $x i64) (result i32)
    (if (i64.eq (local.get $x) (i64.const 0x8000000000000000))
      (then (i32.store (i32.const 0) (i32.const 17)) (return (i32.const 3))))
    (i32.store (i32.const 4) (i32.const 29))
    (i32.wrap_i64 (local.get $x)))
  (func $brne32 (export "brne32") (param $x i32) (result i32)
    (if (i32.ne (local.get $x) (i32.const 0x80000000))
      (then (i32.store (i32.const 8) (local.get $x)) (return (i32.add (local.get $x) (i32.const 1)))))
    (i32.store (i32.const 12) (i32.const 5))
    (i32.const -7))
  (func $sel (export "sel") (param $x i64) (param $a i32) (param $b i32) (result i32)
    (select (local.get $a) (local.get $b) (i64.eq (local.get $x) (i64.const 0x8000000000000000))))
  (func $chain (export "chain") (param $x i64) (param $y i64) (result i32)
    (if (i32.and (i64.ne (local.get $y) (i64.const 3)) (i64.eq (local.get $x) (i64.const 0x8000000000000000)))
      (then (i32.store (i32.const 16) (i32.const 1)) (return (i32.const 11))))
    (i32.store (i32.const 20) (i32.const 2))
    (i32.const 22))
  (func $chain2 (export "chain2") (param $x i64) (param $a i64) (param $b i64) (result i32)
    (if (i32.or (i64.lt_s (local.get $a) (local.get $b)) (i64.ne (local.get $x) (i64.const 0x8000000000000000)))
      (then (i32.store (i32.const 24) (i32.const 3)) (return (i32.const 33))))
    (i32.store (i32.const 28) (i32.const 4))
    (i32.const 44))
  (func $chain3 (export "chain3") (param $x i64) (param $y i32) (result i32)
    (if (i32.and (i64.eq (local.get $x) (i64.const 0x8000000000000000)) (i32.ne (local.get $y) (i32.const 0x80000000)))
      (then (i32.store (i32.const 32) (i32.const 5)) (return (i32.const 55))))
    (i32.store (i32.const 36) (i32.const 6))
    (i32.const 66))
)
