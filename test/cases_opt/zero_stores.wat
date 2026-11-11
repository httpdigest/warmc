;; A zero stored to a field or an element is the zero register's -- a null
;; as an integer's 0 is (isel.c rd_zr; isel_ops.inc's field.set and
;; array.set, pair_zr): no register is set to 0 for the store, and two
;; zeros to fields side by side are one stp of the zero register.
;;
;;   clear    a number and a reference side by side, a Lua value's two
;;            halves, set to 0 and null: one stp xzr, xzr
;;   half     the number set to n, the reference to null: an stp of n's
;;            register and the zero register
;;   elem     an array's element set to null: one str of the zero register
;;   izero    an i32 field and an i32 element set to 0: strs of wzr
;;
;; Each function reads back what it wrote, and what was there before
;; beside it.  Answers worked out by hand.
;;
;; shape: clear stp\s+xzr, xzr=1
;; shape: clear mov\s+[xw]\d+, #0x0\b=0
;; shape: half stp\s+x\d+, xzr=1
;; shape: half mov\s+[xw]\d+, #0x0\b=0
;; shape: elem str\s+xzr=1
;; shape: elem mov\s+[xw]\d+, #0x0\b=0
;; shape: izero str\s+wzr=2
;; shape: izero mov\s+[xw]\d+, #0x0\b=0
;;
;; run: clear => i32:71
;; run: half 9 => i32:1133
;; run: elem 1 => i32:111
;; run: elem 2 => i32:201
;; run: izero 1 => i32:38
(module
  (type $val (struct (field (mut i32)) (field (mut i64)) (field (mut eqref)) (field (mut i32))))
  (type $refs (array (mut eqref)))
  (type $ints (array (mut i32)))
  (global $v (mut (ref $val))
    (struct.new $val (i32.const 7) (i64.const 5) (ref.i31 (i32.const 3)) (i32.const 64)))
  (global $r (mut (ref $refs))
    (array.new_fixed $refs 3 (ref.i31 (i32.const 1)) (ref.i31 (i32.const 2)) (ref.i31 (i32.const 3))))
  (global $n (mut (ref $ints)) (array.new_fixed $ints 3 (i32.const 11) (i32.const 12) (i32.const 13)))
  ;; 7 + 64, with 1000 for a number left and 100 for a reference left
  (func $sum (result i32)
    (i32.add
      (i32.add (struct.get $val 0 (global.get $v)) (struct.get $val 3 (global.get $v)))
      (i32.add (i32.mul (i32.wrap_i64 (struct.get $val 1 (global.get $v))) (i32.const 1000))
               (select (i32.const 0) (i32.const 100) (ref.is_null (struct.get $val 2 (global.get $v)))))))
  (func $clear (export "clear") (result i32)
    (struct.set $val 1 (global.get $v) (i64.const 0))
    (struct.set $val 2 (global.get $v) (ref.null eq))
    (call $sum))
  (func $half (export "half") (param $x i32) (result i32)
    (struct.set $val 1 (global.get $v) (i64.extend_i32_u (local.get $x)))
    (struct.set $val 2 (global.get $v) (ref.null eq))
    (i32.add (call $sum) (i32.sub (i32.const 0) (i32.mul (local.get $x) (i32.const 882)))))
  ;; 100 for each of the first two elements that is not null, 10 for the third, and 1
  (func $elem (export "elem") (param $i i32) (result i32)
    (array.set $refs (global.get $r) (local.get $i) (ref.null eq))
    (i32.add
      (i32.add (select (i32.const 0) (i32.const 100) (ref.is_null (array.get $refs (global.get $r) (i32.const 0))))
               (select (i32.const 0) (i32.const 100) (ref.is_null (array.get $refs (global.get $r) (i32.const 1)))))
      (i32.add (select (i32.const 0) (i32.const 10) (ref.is_null (array.get $refs (global.get $r) (i32.const 2))))
               (i32.const 1))))
  (func $izero (export "izero") (param $i i32) (result i32)
    (struct.set $val 3 (global.get $v) (i32.const 0))
    (array.set $ints (global.get $n) (local.get $i) (i32.const 0))
    (i32.add
      (i32.add (struct.get $val 0 (global.get $v)) (struct.get $val 3 (global.get $v)))
      (i32.add (i32.add (array.get $ints (global.get $n) (i32.const 0)) (array.get $ints (global.get $n) (i32.const 1)))
               (i32.add (array.get $ints (global.get $n) (i32.const 2)) (struct.get $val 0 (global.get $v)))))))
