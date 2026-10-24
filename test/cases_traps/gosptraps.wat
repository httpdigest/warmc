;; The traps of a module in Go's shape, whose functions gosp.c makes take
;; the stack pointer and return it: a call through Go's type, (i32) -> i32,
;; traps as it did -- on a slot holding a function of another type, an
;; empty slot, one past the table -- whether an export makes it or a Go
;; function does; so does a call through (i32, i32) -> i64, the functions'
;; new shape and a type of the module's own here, that lands on one of them;
;; and a Go function's own trap after it moved the pointer is reported as
;; it was.  test/run_traps.sh runs each export.  The slots come from memory,
;; so no pass can fold them.
(module
  (@custom "go:buildid" "warmc trap test")
  (type $go (func (param i32) (result i32)))
  (type $c2 (func (param i32 i32) (result i64)))
  (memory 1)
  (global $sp (mut i32) (i32.const 8192))
  (table 8 funcref)
  (elem (i32.const 1) $leaf $pair $other $via $deep)

  ;; 1: a Go function that pops its return address
  (func $leaf (type $go)
    (global.set $sp (i32.add (global.get $sp) (i32.const 8)))
    (i32.const 0))
  ;; 2: the new shape's type
  (func $pair (type $c2)
    (i64.extend_i32_u (i32.add (local.get 0) (local.get 1))))
  ;; 3: another type
  (func $other (param i32) (result i64) (i64.extend_i32_u (local.get 0)))
  ;; 4: a Go function calling through the table at the slot memory at 4 holds
  (func $via (type $go)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 16)))
    (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))
    (if (call_indirect (type $go) (i32.const 0) (i32.load (i32.const 4)))
      (then (return (i32.const 1))))
    (global.set $sp (i32.add (global.get $sp) (i32.const 24)))
    (i32.const 0))
  ;; 5: a Go function that moves the pointer and then traps
  (func $deep (type $go)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 64)))
    (i32.store (global.get $sp) (i32.const 1))
    (if (i32.load (global.get $sp)) (then unreachable))
    (i32.const 0))

  (func $go (param $slot i32) (param $then i32) (result i32)
    (i32.store (i32.const 4) (local.get $then))
    (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))
    (call_indirect (type $go) (i32.const 0) (local.get $slot)))

  (func (export "gosp_ok")
    (if (call $go (i32.const 4) (i32.const 1)) (then unreachable))
    (if (i32.ne (global.get $sp) (i32.const 8192)) (then unreachable)))
  (func (export "gosp_x_type") (drop (call $go (i32.load (i32.const 8)) (i32.const 1))))
  (func (export "gosp_x_null") (drop (call $go (i32.add (i32.load (i32.const 8)) (i32.const 4)) (i32.const 1))))
  (func (export "gosp_x_oob") (drop (call $go (i32.add (i32.load (i32.const 8)) (i32.const 100)) (i32.const 1))))
  (func (export "gosp_go_x_type") (drop (call $go (i32.const 4) (i32.const 3))))
  (func (export "gosp_go_x_null") (drop (call $go (i32.const 4) (i32.const 7))))
  (func (export "gosp_go_x_oob") (drop (call $go (i32.const 4) (i32.const 8))))
  (func (export "gosp_c2_on_go") (result i64)
    (call_indirect (type $c2) (i32.const 1) (i32.const 2) (i32.add (i32.load (i32.const 8)) (i32.const 3))))
  (func (export "gosp_c2_ok") (result i64)
    (call_indirect (type $c2) (i32.const 1) (i32.const 2) (i32.load (i32.const 8))))
  (func (export "gosp_trap") (drop (call $go (i32.const 4) (i32.const 5))))
  (data (i32.const 8) "\02\00\00\00")
)
