;; A loop's guard moved before it (loop_hoist, opt.c): the header branches
;; on an invariant to a block that ends in a trap -- a return_call is built
;; as the call and then a trap -- and the branch went to the preheader,
;; above the header's global.set, so the callee read the global's value
;; from before it.  A header that does what has an effect keeps its guard.
;; The tag keeps global promotion out of the way.
;; run: f 1 => i32:100
;; run: f 0 => i32:109
(module
  (tag $t)
  (global $g (mut i32) (i32.const 0))
  (func $callee (result i32) (global.get $g))
  (func (export "f") (param $c i32) (result i32)
    (local $i i32)
    (loop $l
      (global.set $g (i32.add (local.get $i) (i32.const 100)))
      (if (local.get $c) (then (return_call $callee)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 10))))
    (global.get $g))
)
