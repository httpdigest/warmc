(module
  ;; The table half of the relaxed call barrier.
  ;;
  ;; An indirect call is only as well behaved as the worst thing the table can
  ;; hold, and what it can hold is what the *active* elem segments put there --
  ;; but only while nothing in the module writes the table.  A passive segment
  ;; reaches a table through `table.init`, so its contents count for nothing
  ;; and its existence counts for everything: the `table.init` below is what
  ;; has to make every indirect call in this module opaque.  A separate module
  ;; from frame.wat because that is a fact about the whole module.

  (memory 1)
  (table 4 funcref)
  (elem (i32.const 0) $benign)
  (elem func $hostile)
  (elem declare func $hostile)

  (type $void (func))

  (global $sp (mut i32) (i32.const 65536))

  ;; What the active segment says is in slot 0: a frame below the pointer.
  (func $benign
    (i32.store (i32.sub (global.get $sp) (i32.const 4)) (i32.const 0x5EED)))

  ;; What the passive segment can put there, which writes the caller's frame.
  (func $hostile
    (i32.store (global.get $sp) (i32.const 0x0BAD)))

  ;; Runs first, while slot 0 still holds $benign.  Even so it has to reload:
  ;; the module writes its table somewhere, so no indirect call in it knows
  ;; what it is calling.
  (func (export "unswapped") (result i32)
    (local $fp i32) (local $r i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 32)))
    (local.set $fp (global.get $sp))
    (i32.store (local.get $fp) (i32.const 111))
    (call_indirect (type $void) (i32.const 0))
    (local.set $r (i32.load (local.get $fp)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 32)))
    (local.get $r))

  (func (export "swapped") (result i32)
    (local $fp i32) (local $r i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 32)))
    (local.set $fp (global.get $sp))
    (i32.store (local.get $fp) (i32.const 111))
    (table.init 1 (i32.const 0) (i32.const 0) (i32.const 1))
    (call_indirect (type $void) (i32.const 0))
    (local.set $r (i32.load (local.get $fp)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 32)))
    (local.get $r))

  ;; `table.set` from a `ref.func` puts a function in the table without any
  ;; segment: the entry's type has to be recovered from the code pointer, or
  ;; the call below traps on a type check that can never pass.
  (func (export "set_then_call") (result i32)
    (local $fp i32) (local $r i32)
    (global.set $sp (i32.sub (global.get $sp) (i32.const 32)))
    (local.set $fp (global.get $sp))
    (i32.store (local.get $fp) (i32.const 111))
    (table.set (i32.const 2) (ref.func $benign))
    (call_indirect (type $void) (i32.const 2))
    (local.set $r (i32.load (local.get $fp)))
    (global.set $sp (i32.add (local.get $fp) (i32.const 32)))
    (local.get $r))

  ;; And the same through table.fill and table.grow.
  (func (export "fill_then_call") (result i32)
    (table.fill (i32.const 3) (ref.func $benign) (i32.const 1))
    (call_indirect (type $void) (i32.const 3))
    (i32.const 1))

  (func (export "grow_then_call") (result i32)
    (local $at i32)
    (local.set $at (table.grow (ref.func $benign) (i32.const 1)))
    (call_indirect (type $void) (local.get $at))
    (local.get $at))
)
