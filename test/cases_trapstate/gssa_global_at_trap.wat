;; A global made an SSA value (gssa, opt.c) is stored before whatever can
;; trap, a call included.  s's loop sets $g three times a trip and divides by
;; d - i, which is 0 on the fourth: gloop leaves a loop alone that can trap,
;; and gssa takes it for touching $g six times a trip.  c calls a function
;; that cannot see $g but traps on the third call, after $g has been set
;; twice; u sets $g and then, for 0, reaches unreachable, the trap that ends
;; a block.
;; run: s:10,3 r => trap 12
;; run: s:2,3 r => 833 6
;; run: c:3 r => trap 5
;; run: u:0 r => trap 7
;; run: u:1 r => 7 7
(module
  (memory (export "memory") 1 1)
  (global $g (mut i32) (i32.const 0))
  (global $k (mut i32) (i32.const 0))
  (func (export "s") (param $n i32) (param $d i32) (result i32)
    (local $i i32) (local $acc i32)
    (loop $l
      (global.set $g (i32.add (global.get $g) (i32.const 1)))
      (global.set $g (i32.add (global.get $g) (i32.const 1)))
      (global.set $g (i32.add (global.get $g) (i32.const 1)))
      (local.set $acc (i32.add (local.get $acc)
         (i32.div_u (i32.const 1000) (i32.sub (local.get $d) (local.get $i)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $acc))
  ;; traps on its third call; sees $k only
  (func $tick
    (global.set $k (i32.add (global.get $k) (i32.const 1)))
    (if (i32.eq (global.get $k) (i32.const 3)) (then unreachable)))
  (func (export "c") (param $n i32) (result i32)
    (local $i i32)
    (loop $l
      (global.set $g (i32.add (global.get $g) (i32.const 1)))
      (global.set $g (i32.add (global.get $g) (i32.const 1)))
      (global.set $g (i32.add (global.get $g) (i32.const 1)))
      (global.set $g (i32.sub (global.get $g) (i32.const 2)))
      (call $tick)
      (global.set $g (i32.add (global.get $g) (i32.const 1)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (global.get $g))
  (func (export "u") (param $x i32) (result i32)
    (global.set $g (i32.add (global.get $g) (i32.const 3)))
    (global.set $g (i32.add (global.get $g) (i32.const 4)))
    (if (local.get $x) (then (return (global.get $g))))
    unreachable)
  (func (export "r") (result i32) (global.get $g))
)
