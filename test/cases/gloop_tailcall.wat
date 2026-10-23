;; A global the loop pass keeps in a register (pass_global_loop) must be
;; written back on every way out of the loop that something can read it on.
;; An exit into a block that only traps needs no write-back, but a
;; return_call is built as the call followed by a trap, so the callee would
;; read the global as memory still has it: `direct` returned 0 instead of 5,
;; and GHC's code, which tail-calls out of loops that kept Sp in a register,
;; hung.  `indirect` is the same through return_call_indirect, and `plain`
;; the ordinary return that always worked.
(module
  (type $r (func (result i32)))
  (table 1 funcref)
  (elem (i32.const 0) $done)
  (global $g (mut i32) (i32.const 0))
  (global $h (mut i32) (i32.const 100))
  (func $done (result i32) (i32.add (global.get $g) (i32.mul (global.get $h) (i32.const 1000))))

  (func $count (param $n i32)
    (loop $l
      (global.set $g (i32.add (global.get $g) (i32.const 1)))
      (global.set $h (i32.sub (global.get $h) (i32.const 3)))
      (br_if $l (local.tee $n (i32.sub (local.get $n) (i32.const 1))))))

  (func (export "direct") (result i32)
    (local $n i32)
    (global.set $g (i32.const 0))
    (global.set $h (i32.const 100))
    (local.set $n (i32.const 5))
    (loop $l
      (global.set $g (i32.add (global.get $g) (i32.const 1)))
      (global.set $h (i32.sub (global.get $h) (i32.const 3)))
      (br_if $l (local.tee $n (i32.sub (local.get $n) (i32.const 1)))))
    (return_call $done))

  (func (export "indirect") (result i32)
    (local $n i32)
    (global.set $g (i32.const 10))
    (global.set $h (i32.const 7))
    (local.set $n (i32.const 9))
    (loop $l
      (global.set $g (i32.add (global.get $g) (i32.const 2)))
      (global.set $h (i32.add (global.get $h) (global.get $g)))
      (br_if $l (local.tee $n (i32.sub (local.get $n) (i32.const 1)))))
    (return_call_indirect (type $r) (i32.const 0)))

  (func (export "plain") (result i32)
    (global.set $g (i32.const 3))
    (global.set $h (i32.const 1))
    (call $count (i32.const 6))
    (call $done))
)
