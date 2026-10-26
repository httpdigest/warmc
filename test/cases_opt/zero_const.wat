;; Constant zeros on arm64 (regalloc.inc ra_zero_const): no register holds
;; one, its readers read the zero register (rd_zr) or make it in place.  No
;; mov of a zero where none is read: neither at the definition (clear), nor
;; for a piece around the stores on either side of a call (around: a piece
;; once made the zero for stores that read the zero register); two adjacent
;; stores of zeros are one stp of the zero register (pair_zr); a call's zero
;; argument is made in place, once a call (args); a zero less a value is a
;; neg of it, shifted too, the subtraction reading the zero register (neg,
;; negsh).
;;
;; shape: clear mov\t[wx]\d+, #0x0=0
;; shape: clear stp\txzr, xzr=1
;; shape: around mov\t[wx]\d+, #0x0=0
;; shape: args mov\t[wx]\d+, #0x0=2
;; shape: neg mov\t[wx]\d+, #0x0=0
;; shape: neg neg\t=1
;; shape: negsh mov\t[wx]\d+, #0x0=0
;; shape: negsh neg\t=1
;;
;; run: clear 64 => i32:65
;; run: clear 200 => i32:0
;; run: around 64 0 => i32:64
;; run: around 64 1 => i32:65
;; run: args 64 0 => i32:0
;; run: args 64 1 => i32:2
;; run: neg 5 => i64:-5
;; run: neg -9223372036854775808 => i64:-9223372036854775808
;; run: negsh 3 => i32:-24
;; run: negsh 536870912 => i32:0
(module
  (type $v (func (param i32) (result i32)))
  (memory 1)
  (table 2 funcref)
  (elem (i32.const 0) $id $inc)
  (func $id (type $v) (local.get 0))
  (func $inc (type $v) (i32.add (local.get 0) (i32.const 1)))

  ;; zeros stored, 8 bytes and 4, a zero returned on one path, no call
  (func $clear (export "clear") (param $p i32) (result i32)
    (i64.store (local.get $p) (i64.const 0))
    (i64.store offset=8 (local.get $p) (i64.const 0))
    (i32.store offset=16 (local.get $p) (i32.const 0))
    (i32.store offset=20 (local.get $p) (i32.add (local.get $p) (i32.const 1)))
    (if (result i32) (i32.gt_u (local.get $p) (i32.const 100))
      (then (i32.const 0))
      (else (i32.load offset=20 (local.get $p)))))

  ;; zeros stored on both sides of a call, apart: no register, and no
  ;; piece of one made for the stores, which read the zero register
  (func $around (export "around") (param $p i32) (param $f i32) (result i32)
    (i64.store (local.get $p) (i64.const 0))
    (i64.store offset=16 (local.get $p) (i64.const 0))
    (i32.store offset=40 (local.get $p)
      (call_indirect (type $v) (local.get $p) (local.get $f)))
    (i64.store offset=24 (local.get $p) (i64.const 0))
    (i64.store offset=8 (local.get $p) (i64.const 0))
    (i32.add (i32.load offset=40 (local.get $p)) (i32.load offset=4 (local.get $p))))

  ;; a zero argument to two calls and a zero stored between them
  (func $args (export "args") (param $p i32) (param $f i32) (result i32)
    (i32.store (local.get $p) (i32.const 0))
    (i32.add
      (call_indirect (type $v) (i32.const 0) (local.get $f))
      (i32.add (i32.load (local.get $p))
               (call_indirect (type $v) (i32.const 0) (local.get $f)))))

  ;; a zero less a value, less a value shifted
  (func $neg (export "neg") (param $x i64) (result i64)
    (i64.sub (i64.const 0) (local.get $x)))
  (func $negsh (export "negsh") (param $x i32) (result i32)
    (i32.sub (i32.const 0) (i32.shl (local.get $x) (i32.const 3))))
)
