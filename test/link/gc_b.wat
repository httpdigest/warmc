;; The importing side of the two-module collection test.  See gc_a.wat.
;;
;; Its own struct type, in its own type space -- the runtime interns each
;; module's `gctypes` into one array as the module is first instantiated, so
;; these two boxes have different type ids at run time despite being the same
;; shape, which is the other thing this pair exercises.
(module
  (type $box (struct (field $val (mut i32))))
  (import "A" "churn" (func $churn (param i32) (result i32)))
  (import "A" "make" (func $make (result anyref)))
  (import "A" "val" (func $val (param anyref) (result i32)))

  ;; Same reason as in gc_a.wat: without an escape the optimiser deletes the
  ;; allocations and this frame has no roots to get wrong.
  (global $sink (mut anyref) (ref.null any))
  (func $escape (param $r anyref)
    (global.set $sink (local.get $r))
    (global.set $sink (ref.null any)))

  ;; Three of its own references live across a call into A that allocates --
  ;; and, with a stress collection, collects.  Three against A's one is what
  ;; makes the two pools different lengths.
  ;;
  ;; `$borrowed` is one of A's objects held in a frame of B's, so the root B
  ;; reports names an object whose type entry belongs to the other module.
  (func (export "run") (param $n i32) (result i32)
    (local $x (ref null $box))
    (local $y (ref null $box))
    (local $borrowed anyref)
    (local.set $x (struct.new $box (i32.const 70)))
    (local.set $y (struct.new $box (i32.const 7)))
    (call $escape (local.get $x))
    (call $escape (local.get $y))
    (local.set $borrowed (call $make))
    (i32.add
      (i32.add (call $churn (local.get $n))
               (call $val (local.get $borrowed)))
      (i32.add (struct.get $box $val (local.get $x))
               (struct.get $box $val (local.get $y)))))

  ;; A's object cast *here*, against B's own $box.
  ;;
  ;; Everything above hands A's object back to A to be cast, so no cast in
  ;; this suite ever ran against a type from another module.  WasmGC type
  ;; identity is structural and these two boxes are the same shape, so this
  ;; must succeed -- but the inline sequence isel emits for a concrete cast
  ;; compares the object's header against a slot in *this* module's run-time
  ;; type range, and the runtime hands every module a disjoint range.  So the
  ;; fast path could only ever answer no, and answered it silently: ref.test
  ;; read false, ref.cast trapped.
  ;;
  ;; B has to allocate a $box of its own for the fast path to be emitted at
  ;; all -- `run` above does -- which is why deleting that allocation used to
  ;; make the same source answer correctly.
  (func (export "cast_borrowed") (result i32)
    (local $borrowed anyref)
    (local.set $borrowed (call $make))
    (call $escape (local.get $borrowed))
    (i32.add
      (i32.mul (ref.test (ref $box) (local.get $borrowed)) (i32.const 100))
      (struct.get $box $val (ref.cast (ref $box) (local.get $borrowed)))))
)
