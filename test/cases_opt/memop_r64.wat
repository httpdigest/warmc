;; A 64-bit memory 0's memory.copy and memory.fill stay the runtime's
;; calls, not warmc_memmove_r's or warmc_memset_r's or the inline v26-v31
;; code, which take 32-bit operands (regalloc.inc memop_preserves): an
;; address or a length past 2^32 traps, and is not read as its low half.
;;
;; shape: copy bl\t.*warmc_memory_copy=1
;; shape: copy warmc_memmove_r=0
;; shape: fill bl\t.*warmc_memory_fill=1
;; shape: fill warmc_memset_r=0
;;
;; run: copy 64 0 8 => i64:578437695752307201
;; run: copy 4294967360 0 8 => trap: out of bounds memory access
;; run: copy 64 4294967296 8 => trap: out of bounds memory access
;; run: copy 64 0 4294967304 => trap: out of bounds memory access
;; run: fill 64 90 8 => i64:6510615555426900570
;; run: fill 4294967360 90 8 => trap: out of bounds memory access
;; run: fill 64 90 4294967304 => trap: out of bounds memory access
;; run: c8 64 => i64:578437695752307201
;; run: c8 4294967360 => trap: out of bounds memory access
;; run: f8 64 => i64:6510615555426900570
;; run: f8 4294967360 => trap: out of bounds memory access
(module
  (memory i64 1)
  (data (i64.const 0) "\01\02\03\04\05\06\07\08")
  (func $copy (export "copy") (param $d i64) (param $s i64) (param $n i64) (result i64)
    (memory.copy (local.get $d) (local.get $s) (local.get $n))
    (i64.load (i64.const 64)))
  (func $fill (export "fill") (param $d i64) (param $v i32) (param $n i64) (result i64)
    (memory.fill (local.get $d) (local.get $v) (local.get $n))
    (i64.load (i64.const 64)))
  (func $c8 (export "c8") (param $d i64) (result i64)
    (memory.copy (local.get $d) (i64.const 0) (i64.const 8))
    (i64.load (i64.const 64)))
  (func $f8 (export "f8") (param $d i64) (result i64)
    (memory.fill (local.get $d) (i32.const 90) (i64.const 8))
    (i64.load (i64.const 64)))
)
