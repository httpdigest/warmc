;; A constant defined in a loop and read after it (ir_home_consts in ir.c).
;; CSE gives the second loop the first loop's 12345 and 4096; the vectoriser
;; then replaces the first loop whole, its trip count filling every vector,
;; and the scalar loop that defined both constants is no longer reached.
;; Code generation makes a constant where it is defined, so the second loop
;; read garbage for its offset and bound and ran out of memory (at -O2
;; -fno-inline, where the fill is a call and the constants stay in the
;; loops).  A constant read where no reachable block defines it now moves to
;; the entry block.  `unrolled` is the same through a loop a producer
;; unrolled by three, which the vectoriser unrolls again before taking it.
(module
  (memory 1)
  (func $fill (local $k i32)
    (loop $l
      (i32.store (i32.shl (local.get $k) (i32.const 2)) (local.get $k))
      (br_if $l (i32.lt_u (local.tee $k (i32.add (local.get $k) (i32.const 1))) (i32.const 4096)))))
  (func (export "run") (result i32)
    (local $i i32) (local $s i32) (local $k i32)
    (call $fill)
    (loop $l
      (i32.store offset=16384 (local.get $i) (i32.add (i32.load (local.get $i)) (i32.const 12345)))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 4))) (i32.const 4096))))
    (loop $m
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
                             (i32.add (i32.load offset=16384 (local.get $k)) (i32.const 12345))))
      (br_if $m (i32.ne (local.tee $k (i32.add (local.get $k) (i32.const 4))) (i32.const 4096))))
    (local.get $s))
  (func (export "unrolled") (result i32)
    (local $i i32) (local $s i32) (local $k i32)
    (call $fill)
    (loop $l
      (i32.store offset=16384 (local.get $i) (i32.add (i32.load (local.get $i)) (i32.const 777)))
      (i32.store offset=16388 (local.get $i) (i32.add (i32.load offset=4 (local.get $i)) (i32.const 777)))
      (i32.store offset=16392 (local.get $i) (i32.add (i32.load offset=8 (local.get $i)) (i32.const 777)))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 12))) (i32.const 4080))))
    (loop $m
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
                             (i32.add (i32.load offset=16384 (local.get $k)) (i32.const 777))))
      (br_if $m (i32.ne (local.tee $k (i32.add (local.get $k) (i32.const 4))) (i32.const 4080))))
    (local.get $s))
)
