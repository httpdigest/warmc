;; i32s extended to i64 on arm64 (regalloc.inc w_written, op_tied_arg;
;; isel_ops.inc's I_EXTENDU): one a w-register write made -- an add, a
;; compare -- has a zero high half already, so its extend stays in its
;; register and is no instruction, as every Go function's return of its
;; stack pointer is; one loaded, likewise, though the load's register is
;; not the result's.  A parameter, whose high half a caller of C's leaves
;; unspecified, and the wrap of an i64 read on after it, whose register is
;; the i64's, are extended by a uxtw, into the register the result goes to.
;;
;; shape: add mov\t=0
;; shape: cmp mov\t=0
;; shape: load mov\tw\d+, w\d+$=0
;; shape: param mov\tw\d+, w\d+$=1
;; shape: param mov\tx=0
;; shape: wrap mov\tw\d+, w\d+$=1
;;
;; run: add 5 => i64:13
;; run: add -8 => i64:0
;; run: add 2147483644 => i64:2147483652
;; run: load 0 => i64:4294967294
;; run: load 4 => i64:67305985
;; run: cmp 1 2 => i64:1
;; run: cmp 3 2 => i64:0
;; run: cmp -1 2 => i64:0
;; run: param -1 => i64:4294967295
;; run: param 5 => i64:5
;; run: wrap 1311768467463790320 => i64:-9100393787520900864
;; run: wrap -1 => i64:-4294967295
(module
  (memory 1)
  (data (i32.const 16) "\fe\ff\ff\ff\01\02\03\04")
  ;; an i32 sum, extended as Go's functions return their stack pointer
  (func $add (export "add") (param $p i32) (result i64)
    (i64.extend_i32_u (i32.add (local.get $p) (i32.const 8))))
  ;; an i32 loaded, read again as an i32 first, so no extending load; and a
  ;; compare's 0 or 1
  (func $load (export "load") (param $p i32) (result i64) (local $v i32)
    (local.set $v (i32.load offset=16 (local.get $p)))
    (i64.store (i32.const 64) (i64.extend_i32_s (i32.xor (local.get $v) (i32.const 3))))
    (i64.extend_i32_u (local.get $v)))
  (func $cmp (export "cmp") (param $p i32) (param $q i32) (result i64)
    (i64.extend_i32_u (i32.lt_u (local.get $p) (local.get $q))))
  ;; a parameter, whose high half a caller of C's leaves unspecified: made
  (func $param (export "param") (param $p i32) (result i64)
    (i64.extend_i32_u (local.get $p)))
  ;; the wrap of an i64 read on after it, whose register is the i64's: made
  (func $wrap (export "wrap") (param $x i64) (result i64)
    (i64.mul (local.get $x)
      (i64.extend_i32_u (i32.wrap_i64 (local.get $x)))))
)
