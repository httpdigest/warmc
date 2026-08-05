;; The importing side.  Every one of these has to reach the *other* instance:
;; its memory, its table, its mutable global, and its code entered with its
;; own instance pointer rather than ours.
(module
  (type $unary (func (param i32) (result i32)))
  (import "A" "mem" (memory 1))
  (import "A" "tab" (table 4 funcref))
  (import "A" "counter" (global $counter (mut i32)))
  (import "A" "answer" (global $answer i32))
  (import "A" "twice" (func $twice (param i32) (result i32)))
  (import "A" "bump" (func $bump (result i32)))
  (import "A" "install" (func $install))
  (import "A" "read_secret" (func $read_secret (result i32)))
  (import "A" "tick" (func $tick))
  ;; An exception tag A defines, and two ways of raising it.  A tag is an
  ;; object rather than a signature: this name and A's reach the same one only
  ;; because the import made them, which is what a `catch` here compares.
  (import "A" "oops" (tag $oops (param i32)))
  (import "A" "raise" (func $raise (param i32)))
  (import "A" "raise_deep" (func $raise_deep (param i32)))
  (import "A" "raise_shielded" (func $raise_shielded (param i32)))
  (import "A" "raise_r" (func $raise_r (param i32) (result i32)))
  (import "A" "rethrow" (func $rethrow (param exnref)))
  (import "A" "call_slot1" (func $call_slot1 (param i32) (result i32)))

  ;; An export naming a function this module did not define.  There is no code
  ;; here to point at, so the descriptor carries the index and the runtime
  ;; reads the funcref cell the import filled -- which is also what makes the
  ;; call enter A's instance and read A's private global.
  (export "reexported_secret" (func $read_secret))

  ;; A start function that is an import: it runs at instantiation, in A.
  (start $tick)

  (global $mine (mut i32) (i32.const 7))
  ;; A constant expression that reads an imported global: not knowable until
  ;; the imports are resolved, so it is resolved at instantiation.
  (global $derived i32 (global.get $answer))

  ;; And a data segment whose offset is one, writing into A's memory.
  (data (global.get $answer) "X")

  ;; The imported function, called directly.
  (func (export "call_import") (param i32) (result i32)
    local.get 0
    call $twice)

  ;; The imported function, tail-called.  A tail call to an import has to load
  ;; the callee's code *and* its instance out of this instance before it tears
  ;; the frame down, because x19 is gone afterwards -- and the callee then
  ;; returns straight to whoever called us.
  (func (export "tail_call_import") (param i32) (result i32)
    local.get 0
    return_call $twice)

  ;; The same across the table, so the funcref's instance rather than ours is
  ;; what the callee is entered with.
  (func (export "tail_call_indirect_import") (param i32) (result i32)
    call $install
    local.get 0
    i32.const 0
    return_call_indirect (type $unary))

  ;; A tail call to a function that reads A's private global: right only if
  ;; the branch entered A's instance.
  (func (export "tail_secret") (result i32) return_call $read_secret)

  ;; The imported table, called indirectly: the slot holds A's function and
  ;; entering it with our instance would read our memory, not A's.
  (func (export "call_indirect_import") (param i32) (result i32)
    call $install
    local.get 0
    i32.const 0
    call_indirect (type $unary))

  ;; The exporter's memory: A's data segment wrote "hello" at 0.
  (func (export "read_shared_mem") (result i32)
    i32.const 0
    i32.load8_u)

  ;; Writing here has to be visible to A, and vice versa.
  (func (export "write_shared_mem") (param i32)
    i32.const 8
    local.get 0
    i32.store)

  ;; The imported mutable global, read and written through the shared cell.
  (func (export "read_counter") (result i32) global.get $counter)
  (func (export "bump_twice") (result i32)
    call $bump
    drop
    call $bump)
  (func (export "set_counter") (param i32) local.get 0 global.set $counter)

  ;; An immutable imported global, and one of our own, to be sure the two
  ;; kinds of slot are not confused.
  (func (export "read_answer") (result i32) global.get $answer)
  (func (export "read_mine") (result i32) global.get $mine)
  (func (export "read_derived") (result i32) global.get $derived)
  (func (export "read_at_answer") (result i32)
    global.get $answer
    i32.load8_u)

  ;; The exporter's memory grown from here is the same memory A sees.
  (func (export "grow") (param i32) (result i32) local.get 0 memory.grow)
  (func (export "size") (result i32) memory.size)

  ;; An exception raised in A's frame and caught in ours.  The clause names
  ;; *our* tag index; the object it resolves to is A's.  A tag of our own with
  ;; the identical type stands beside it and must not take the exception.
  (tag $mine_oops (param i32))

  (func (export "catch_across") (param i32) (result i32)
    (block $h (result i32)
      (try_table (catch $oops $h) (call $raise (local.get 0)))
      (return (i32.const -1)))
    (i32.add (i32.const 1000)))

  (func (export "catch_across_deep") (param i32) (result i32)
    (block $h (result i32)
      (try_table (catch $oops $h) (call $raise_deep (local.get 0)))
      (return (i32.const -1)))
    (i32.add (i32.const 2000)))

  ;; The inner clause names a tag of ours with the same type; only the outer
  ;; one, which names the imported object, may take it.
  (func (export "catch_across_wrong_tag") (param i32) (result i32)
    (block $outer (result i32)
      (try_table (catch $oops $outer)
        (block $inner (result i32)
          (try_table (catch $mine_oops $inner) (call $raise (local.get 0)))
          (return (i32.const -1)))
        (return (i32.add (i32.const 3000))))
      (return (i32.const -2)))
    (i32.add (i32.const 4000)))

  ;; And one we raise ourselves, so the tag object is this instance's.
  (func (export "catch_own") (param i32) (result i32)
    (block $h (result i32)
      (try_table (catch $mine_oops $h)
        (local.get 0) (throw $mine_oops))
      (return (i32.const -1)))
    (i32.add (i32.const 5000)))

  ;; `catch_all` names no tag at all, so it takes an exception from the other
  ;; instance without any object to compare -- the one clause whose answer
  ;; cannot depend on which module raised.
  (func (export "catch_across_all") (param i32) (result i32)
    (block $h
      (try_table (catch_all $h) (call $raise (local.get 0)))
      (return (i32.const -1)))
    (i32.add (local.get 0) (i32.const 6000)))

  ;; Caught here by reference, raised again from a frame of A's, and caught
  ;; here a second time.  The object crosses the boundary in both directions
  ;; and outlives every frame that existed when it was made.
  (func (export "catch_across_ref") (param i32) (result i32) (local $x exnref)
    (block $c (result exnref)
      (try_table (result exnref) (catch_all_ref $c) (call $raise (local.get 0))
        (unreachable)))
    (local.set $x)
    (block $h (result i32)
      (try_table (catch $oops $h) (call $rethrow (local.get $x)))
      (return (i32.const -2)))
    (i32.add (i32.const 7000)))

  ;; Past a frame of A's that has a handler of its own for a tag of A's own.
  ;; The walk has to reject that clause and keep going out of the module.
  (func (export "catch_across_shielded") (param i32) (result i32)
    (block $h (result i32)
      (try_table (catch $oops $h) (call $raise_shielded (local.get 0)))
      (return (i32.const -1)))
    (i32.add (i32.const 8000)))

  ;; A tail call into A out of a protected region: that region's frame is gone
  ;; before the exception exists, so it cannot be the one that catches, and
  ;; the caller's region has to.  If the inner one took it the answer would be
  ;; the payload with nothing added.
  (func $tail_raise (param i32) (result i32)
    (block $h (result i32)
      (try_table (result i32) (catch $oops $h) (return_call $raise_r (local.get 0)))
      (return)))
  (func (export "catch_across_tail") (param i32) (result i32)
    (block $h (result i32)
      (try_table (result i32) (catch $oops $h) (call $tail_raise (local.get 0)))
      (return))
    (i32.add (i32.const 9000)))

  ;; One of our functions in A's table, so a call can go from A to us as well
  ;; as the other way.  With it the chain alternates between the instances all
  ;; the way down: `$b_hop` calls A, A calls `$b_hop` through the slot, and so
  ;; on, so an exception raised at the bottom walks out through fourteen
  ;; frames belonging to two different modules before it is caught here.  The
  ;; tag is A's, and the region that takes it is ours.
  (func $b_hop (param $n i32) (result i32)
    (if (i32.eqz (local.get $n))
      (then (i32.const 77) (throw $oops)))
    (call $call_slot1 (i32.sub (local.get $n) (i32.const 1))))
  (elem declare func $b_hop)
  (func $install_b (i32.const 1) (ref.func $b_hop) (table.set))
  (func (export "catch_across_alt") (param i32) (result i32)
    (call $install_b)
    (block $h (result i32)
      (try_table (catch $oops $h) (drop (call $b_hop (local.get 0))))
      (return (i32.const -1)))
    (i32.add (i32.const 11000)))
)
