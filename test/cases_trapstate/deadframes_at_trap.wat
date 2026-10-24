;; A store into a frame that nothing reads before the frame is popped is not
;; dead if a trap can come first: the trap leaves the frame in memory, the
;; stack pointer still below it.  -fdead-frames, on at -O3, takes such
;; stores; -fexact-trap turns it off.  f moves C's stack pointer down by 16,
;; stores 42 into its frame, divides by its argument, and pops; after the
;; trap for 0, the frame at 65520 holds 42 and the stack pointer is there.
;; run: f:0 r sp => trap 42 65520
;; run: f:2 r sp => 21 42 65536
(module
  (memory (export "memory") 1)
  (global $sp (mut i32) (i32.const 65536))
  (func (export "f") (param $x i32) (result i32)
    (local $fp i32) (local $r i32)
    (local.set $fp (i32.sub (global.get $sp) (i32.const 16)))
    (global.set $sp (local.get $fp))
    (i32.store (local.get $fp) (i32.const 42))
    (local.set $r (i32.div_u (i32.const 42) (local.get $x)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 16)))
    (local.get $r))
  (func (export "r") (result i32) (i32.load (i32.const 65520)))
  (func (export "sp") (result i32) (global.get $sp))
)
