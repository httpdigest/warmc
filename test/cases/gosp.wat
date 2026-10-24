;; A module in the shape of Go's wasm port, whose stack pointer gosp.c
;; passes to its functions and has them return: a go:buildid section, the
;; pointer in global 0, every Go function of type (i32) -> i32, its
;; argument above the return address the caller pushed and its result above
;; that, and a 1 returned to unwind.  The functions of other types reach the
;; global the ways Go's do: $wb reads it to push a return address and calls
;; a Go function as the write barrier does, $wb1 reaches it only through $wb,
;; directly and through the table, and $grow and $shrink move it -- each
;; called where the pointer has moved since the global last heard of it.  The Go
;; functions return every way a body can -- the value at the end, a return
;; from inside a loop, a branch to the function's label, a br_table one --
;; and $switch moves to another stack and returns 1, so the export sees the
;; pointer it left.  Each export checks the global where it should be.  And
;; (i32, i32) -> i64, the rewritten functions' new shape, is also a type
;; here; that a call through it that lands on one of them still traps is
;; test/cases_traps/gosptraps.wat's.

(module
  (@custom "go:buildid" "warmc test")
  (type $go (func (param i32) (result i32)))
  (type $wbt (func (param i64) (result i64)))
  (type $c2 (func (param i32 i32) (result i64)))
  (memory 1)
  (global $sp (mut i32) (i32.const 8192))
  (global $seen (mut i64) (i64.const 0))
  (table 8 funcref)
  (elem (i32.const 1) $sq $sum $wb $twice $pair $writer)

  ;; outside Go's type and away from the global
  (func $mix (param $x i64) (result i64)
    (i64.xor (i64.mul (local.get $x) (i64.const 0x9e3779b97f4a7c15))
             (i64.shr_u (local.get $x) (i64.const 29))))

  ;; 1: x*x + 1, with no frame
  (func $sq (type $go)
    (i32.store offset=12 (global.get $sp)
      (i32.add (i32.mul (i32.load offset=8 (global.get $sp)) (i32.load offset=8 (global.get $sp)))
               (i32.const 1)))
    (global.set $sp (i32.add (global.get $sp) (i32.const 8)))
    (i32.const 0))

  ;; 2: sq(n) + sq(n-1) + ... + sq(1), recursively, through a frame of 16;
  ;; returns from inside its loop and by br_table to its own label
  (func $sum (type $go)
    (local $n i32) (local $acc i64) (local $i i32)
    (local.set $n (i32.load offset=8 (global.get $sp)))
    (global.set $sp (i32.sub (global.get $sp) (i32.const 16)))
    (if (i32.eqz (local.get $n))
      (then
        (i32.store offset=28 (global.get $sp) (i32.const 0))
        (global.set $sp (i32.add (global.get $sp) (i32.const 24)))
        (return (i32.const 0))))
    (i32.store (global.get $sp) (local.get $n))
    (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))
    (i64.store (global.get $sp) (i64.const 0x20001))
    (if (call $sq (i32.const 0)) (then (return (i32.const 1))))
    (local.set $acc (i64.extend_i32_u (i32.load offset=4 (global.get $sp))))
    (i32.store (global.get $sp) (i32.sub (local.get $n) (i32.const 1)))
    (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))
    (i64.store (global.get $sp) (i64.const 0x20002))
    (if (call $sum (i32.const 0)) (then (return (i32.const 1))))
    (local.set $acc (i64.add (local.get $acc) (i64.extend_i32_u (i32.load offset=4 (global.get $sp)))))
    (i32.store offset=28 (global.get $sp) (i32.wrap_i64 (local.get $acc)))
    (global.set $sp (i32.add (global.get $sp) (i32.const 24)))
    (loop $l
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (if (i32.ge_u (local.get $i) (i32.const 3)) (then (return (i32.const 0))))
      (br $l))
    (i32.const 7))

  ;; records where it was entered, as the write barrier's flush would see it
  (func $flush (type $go)
    (global.set $seen (i64.add (i64.mul (global.get $seen) (i64.const 31))
                               (i64.extend_i32_u (global.get $sp))))
    (global.set $sp (i32.add (global.get $sp) (i32.const 8)))
    (i32.const 0))

  ;; 3: the write barrier: pushes a return address where the global says
  ;; and calls a Go function, which pops it
  (func $wb (type $wbt)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))
    (i64.store (global.get $sp) (i64.const 0x30001))
    (if (call $flush (i32.const 0)) (then unreachable))
    (call $mix (local.get 0)))

  ;; reaches the global only through $wb
  (func $wb1 (result i64) (call $wb (i64.const 8)))

  ;; move the pointer, as no Go function would, for the caller to see
  (func $grow (global.set $sp (i32.sub (global.get $sp) (i32.const 32))))
  (func $shrink (global.set $sp (i32.add (global.get $sp) (i32.const 32))))

  ;; 4: sq(sq(x)), each call through the table
  (func $twice (type $go)
    (local $x i32)
    (local.set $x (i32.load offset=8 (global.get $sp)))
    (global.set $sp (i32.sub (global.get $sp) (i32.const 16)))
    (i32.store (global.get $sp) (local.get $x))
    (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))
    (i64.store (global.get $sp) (i64.const 0x40001))
    (if (call_indirect (type $go) (i32.const 0) (i32.const 1)) (then (return (i32.const 1))))
    (i32.store (global.get $sp) (i32.load offset=4 (global.get $sp)))
    (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))
    (i64.store (global.get $sp) (i64.const 0x40002))
    (if (call_indirect (type $go) (i32.const 0) (i32.const 1)) (then (return (i32.const 1))))
    (i32.store offset=28 (global.get $sp) (i32.load offset=4 (global.get $sp)))
    (global.set $sp (i32.add (global.get $sp) (i32.const 24)))
    (br 0 (i32.const 0)))

  ;; 5: (i32, i32) -> i64, a function of the new shape's type
  (func $pair (type $c2)
    (i64.or (i64.shl (i64.extend_i32_u (local.get 0)) (i64.const 32)) (i64.extend_i32_u (local.get 1))))

  ;; 6: calls into what reaches the global, each after the pointer moved
  ;; where the global has not seen it
  (func $writer (type $go)
    (local $v i64) (local $k i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 16)))
    (i32.store (global.get $sp) (i32.const 7))
    (local.set $v (call $wb1))
    (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))
    (local.set $v (i64.add (local.get $v) (call_indirect (type $wbt) (i64.const 3) (i32.const 3))))
    (global.set $sp (i32.add (global.get $sp) (i32.const 8)))
    (call $grow)
    (global.set $seen (i64.add (global.get $seen) (i64.extend_i32_u (global.get $sp))))
    (i32.store offset=4 (global.get $sp) (i32.wrap_i64 (local.get $v)))
    (local.set $k (i32.load offset=4 (global.get $sp)))
    (call $shrink)
    (i32.store offset=28 (global.get $sp) (i32.add (local.get $k) (i32.load (global.get $sp))))
    (global.set $sp (i32.add (global.get $sp) (i32.const 24)))
    (i32.const 0))

  ;; moves to another stack, as morestack does, and unwinds: by a return,
  ;; a branch to its label, or br_table's default to it
  (func $switch (type $go)
    (local $to i32)
    (local.set $to (i32.load offset=8 (global.get $sp)))
    (global.set $sp (i32.add (local.get $to) (i32.const 64)))
    (drop (block $a (result i32)
      (drop (block $b (result i32)
        (br_table $a $b 2 (i32.const 1) (i32.and (local.get $to) (i32.const 3)))))
      (global.set $sp (local.get $to))
      (return (i32.const 1))))
    (global.set $sp (i32.add (local.get $to) (i32.const 32)))
    (br 0 (i32.const 1)))

  ;; an export calls a Go function as Go's do: the argument at its stack
  ;; pointer, the return address below it; the result is above the argument
  (func $call (param $f i32) (param $arg i32) (result i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 16)))
    (i32.store (global.get $sp) (local.get $arg))
    (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))
    (i64.store (global.get $sp) (i64.const 0x90001))
    (if (call_indirect (type $go) (i32.const 0) (local.get $f)) (then unreachable))
    (local.set $arg (i32.load offset=4 (global.get $sp)))
    (global.set $sp (i32.add (global.get $sp) (i32.const 16)))
    (local.get $arg))

  (func $at_start (result i64)
    (if (result i64) (i32.eq (global.get $sp) (i32.const 8192))
      (then (i64.const 0)) (else (i64.const 0x100000000))))

  (func (export "sum") (result i64)
    (i64.add (i64.extend_i32_u (call $call (i32.const 2) (i32.const 20))) (call $at_start)))

  (func (export "twice") (result i64)
    (i64.add (i64.extend_i32_u (call $call (i32.const 4) (i32.const 6))) (call $at_start)))

  (func (export "direct") (result i64)
    (local $r i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 16)))
    (i32.store (global.get $sp) (i32.const 9))
    (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))
    (i64.store (global.get $sp) (i64.const 0x90002))
    (if (call $sum (i32.const 0)) (then unreachable))
    (local.set $r (i32.load offset=4 (global.get $sp)))
    (global.set $sp (i32.add (global.get $sp) (i32.const 16)))
    (i64.add (i64.extend_i32_u (local.get $r)) (call $at_start)))

  (func (export "writer") (result i64)
    (global.set $seen (i64.const 0))
    (i64.add (i64.add (i64.extend_i32_u (call $call (i32.const 6) (i32.const 0)))
                      (i64.mul (global.get $seen) (i64.const 1000003)))
             (call $at_start)))

  (func (export "switch_a") (result i32)
    (local $f i32)
    (global.set $sp (i32.const 8192))
    (global.set $sp (i32.sub (global.get $sp) (i32.const 16)))
    (i32.store (global.get $sp) (i32.const 4096))
    (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))
    (i64.store (global.get $sp) (i64.const 0x90003))
    (local.set $f (call $switch (i32.const 0)))
    (i32.add (i32.mul (local.get $f) (i32.const 100000)) (global.get $sp)))

  (func (export "switch_b") (result i32)
    (local $f i32)
    (global.set $sp (i32.const 8192))
    (global.set $sp (i32.sub (global.get $sp) (i32.const 16)))
    (i32.store (global.get $sp) (i32.const 4097))
    (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))
    (i64.store (global.get $sp) (i64.const 0x90004))
    (local.set $f (call $switch (i32.const 0)))
    (i32.add (i32.mul (local.get $f) (i32.const 100000)) (global.get $sp)))

  (func (export "switch_c") (result i32)
    (local $f i32)
    (global.set $sp (i32.const 8192))
    (global.set $sp (i32.sub (global.get $sp) (i32.const 16)))
    (i32.store (global.get $sp) (i32.const 4098))
    (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))
    (i64.store (global.get $sp) (i64.const 0x90005))
    (local.set $f (call $switch (i32.const 0)))
    (i32.add (i32.mul (local.get $f) (i32.const 100000)) (global.get $sp)))

  (func (export "new_shape") (result i64)
    (call_indirect (type $c2) (i32.const 3) (i32.const 4) (i32.const 5)))

  (func (export "sp_after") (result i32) (global.get $sp))
)
