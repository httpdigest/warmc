;; A global kept in a register through a loop (gloop, gssa, opt.c) is
;; written back where a trap ends the loop too: w divides by d - i, which is
;; 0 on the fourth trip, wt reaches unreachable there, and each has set the
;; global once a trip by then.
;; run: w:10,3 r => trap 4
;; run: wt:10,3 r => trap 4
;; run: w:2,3 r => 833 2
(module
  (memory (export "memory") 1 1)
  (global $g (mut i32) (i32.const 0))
  (func (export "w") (param $n i32) (param $d i32) (result i32)
    (local $i i32) (local $acc i32)
    (loop $l
      (global.set $g (i32.add (global.get $g) (i32.const 1)))
      (local.set $acc (i32.add (local.get $acc)
         (i32.div_u (i32.const 1000) (i32.sub (local.get $d) (local.get $i)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $acc))
  (func (export "wt") (param $n i32) (param $d i32) (result i32)
    (local $i i32) (local $acc i32)
    (loop $l
      (global.set $g (i32.add (global.get $g) (i32.const 1)))
      (if (i32.eq (local.get $i) (local.get $d)) (then unreachable))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $acc))
  (func (export "r") (result i32) (global.get $g))
)
