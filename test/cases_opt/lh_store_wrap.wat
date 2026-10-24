;; A store in a loop taken for one before it (loop_hoist, opt.c
;; lh_same_place): (x + -1) at offset 1 is x's place only where the i32 add
;; does not wrap.  For x = 0 the loop's store is at 2^32 and traps, so it is
;; no repeat of the store at 0 before the loop.
;; run: f 0 5 8 => trap: out of bounds memory access
;; run: f 16 5 8 => i32:0
(module
  (memory 1)
  (func (export "f") (param $x i32) (param $v i32) (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (i32.store (local.get $x) (local.get $v))
    (loop $l
      (i32.store offset=1 (i32.add (local.get $x) (i32.const -1)) (local.get $v))
      (local.set $s (i32.add (local.get $s) (i32.load offset=100 (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 4)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))
)
