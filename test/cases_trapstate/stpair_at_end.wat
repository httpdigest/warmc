;; Two stores side by side into linear memory, which isel_ops.inc pairs as
;; one STP: one whose one half faults on the guard page may leave the other
;; half unwritten, or written where the first store faulted and the other
;; never ran.  With -fexact-trap each store wider than a byte is tested
;; first (ir_build.c's mem_ea), so no pair that is made can fault.  The
;; store at 65536 is past the end of the one page, whose maximum is left
;; open so that no pass knows it: lo writes 65528 and then traps, so 65528
;; holds its first argument; hi traps first, so it holds nothing.  In both
;; the second store is not made.  Without the flag the two are one STP,
;; which on Apple's cores writes neither half.
;; run: lo:7,9 r => trap 7
;; run: hi:7,9 r => trap 0
;; run: lo:7,9 r q => trap 7 0
(module
  (memory (export "memory") 1)
  (func (export "lo") (param $a i64) (param $b i64) (result i32)
    (i64.store (i32.const 65528) (local.get $a))
    (i64.store (i32.const 65536) (local.get $b))
    (i32.const 0))
  (func (export "hi") (param $a i64) (param $b i64) (result i32)
    (i64.store (i32.const 65536) (local.get $b))
    (i64.store (i32.const 65528) (local.get $a))
    (i32.const 0))
  (func (export "r") (result i32) (i32.load (i32.const 65528)))
  (func (export "q") (result i32) (i32.load (i32.const 65532)))
)
