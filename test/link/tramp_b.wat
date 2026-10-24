;; A trampoline in GHC's shape, which tramp.c turns into tail calls, run by
;; the start function, and a global imported from test/link/tramp_a.wat
;; that its functions use more than the register they alone touch.  The
;; imported one is no register: A reads it while $a is inside the call to
;; peek, after $a added 5 to it.  Nothing exported runs the loop, so the
;; host cannot, and the call to peek stores no register.
(module
  (type $stg (func (result i32)))
  (import "A" "g" (global $g (mut i32)))
  (import "A" "peek" (func $peek (result i32)))
  (memory 1)
  (global $r1 (mut i32) (i32.const 0))
  (table 4 funcref)
  (elem (i32.const 1) $a $b)
  (func $a (type $stg)
    (global.set $g (i32.add (global.get $g) (i32.const 2)))
    (global.set $g (i32.add (global.get $g) (i32.const 3)))
    (global.set $r1 (i32.add (global.get $r1) (i32.const 1)))
    (i32.store (i32.const 4) (call $peek))
    (i32.const 2))
  (func $b (type $stg)
    (i32.store (i32.const 0) (global.get $r1))
    (i32.const 0))
  (func $run (param $f i32)
    (loop $l
      (local.set $f (call_indirect (type $stg) (local.get $f)))
      (br_if $l (local.get $f))))
  (func $init (call $run (i32.const 1)))
  (start $init)
  (func (export "seen") (result i32) (i32.load (i32.const 4)))
  (func (export "r1") (result i32) (i32.load (i32.const 0))))
