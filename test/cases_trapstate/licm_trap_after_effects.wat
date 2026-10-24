;; An invariant division that can trap stays below what the loop's header
;; does first (LICM, opt.c stops_block): a global set in w, a memory.grow in
;; w2.  Dividing by 0 traps on the first trip, after them.
;; run: w:7,0,5 r => trap 100
;; run: w2:7,0,5 sz => trap 2
;; run: w:7,1,5 r => 35 104
(module
  (tag $t)
  (memory (export "memory") 1)
  (global $g (mut i32) (i32.const 0))
  (func (export "w") (param $x i32) (param $y i32) (param $n i32) (result i32)
    (local $i i32) (local $acc i32)
    (loop $l
      (global.set $g (i32.add (local.get $i) (i32.const 100)))
      (local.set $acc (i32.add (local.get $acc) (i32.div_u (local.get $x) (local.get $y))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $acc))
  (func (export "w2") (param $x i32) (param $y i32) (param $n i32) (result i32)
    (local $i i32) (local $acc i32)
    (loop $l
      (drop (memory.grow (i32.const 1)))
      (local.set $acc (i32.add (local.get $acc) (i32.div_u (local.get $x) (local.get $y))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $acc))
  (func (export "r") (result i32) (global.get $g))
  (func (export "sz") (result i32) (memory.size))
)
