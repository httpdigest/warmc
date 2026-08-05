;; A constant expression's own stack is a root set.
;;
;; The runtime evaluates a global's initialiser itself, on a small stack of its
;; own, and that expression can allocate.  Objects it has already allocated sit
;; on that stack while it allocates the next one -- so unless the collector is
;; told about them, the collection that next allocation triggers frees them, and
;; the aggregate being built is finished with reclaimed memory in its fields.
;;
;; warmc read the fields back as *zero*: a silent wrong answer rather than a
;; crash, which is why this is a case and not a fuzz target.  `warmc_gc_hold`
;; existed for exactly this and had never been called for it.
;;
;; The bug needs a collection to fire between the two inner allocations, so it
;; is invisible at the default pacing and certain under WARMC_GC_STRESS -- which
;; test/run_gc.sh runs.  Nothing else in the tree put a constant expression and
;; a dense collector together: the hand-written cases allocate from compiled
;; code, where the safepoint map covers the stack, and it took a producer that
;; builds its string table in initialisers to reach it.

;; A value that arrives on that stack by `global.get` rather than by being
;; allocated there is the same problem read the other way round.  Only the
;; allocating pushes registered what they pushed, so a reference copied out of
;; another global's cell was invisible to the collector -- and while a global
;; is a strong root, so nothing is freed, a *relocating* collection heals the
;; cell and leaves the copy on the stack pointing at the abandoned object.  The
;; field is then built from the pre-move address, which no later mark repairs
;; because it already carries the colour of the phase.
;;
;; So this half shows up as identity rather than as a zero: one object read as
;; two, and a write through one of them that the other never sees.  It needs
;; relocation to be doing something, which is the reloc rows of
;; test/run_gc_modes.sh rather than the default pacing.

(module
  (type $inner (struct (field i32)))
  (type $outer (struct (field (ref $inner)) (field (ref $inner))))
  (type $arr (array (ref $inner)))
  (type $cell (struct (field (mut i32))))
  (type $box (struct (field (ref $cell))))
  (type $pad (array i64))

  ;; Two allocations feeding a third: the first `inner` is live across the
  ;; second's allocation, and both are live across the `struct.new $outer`.
  (global $g (ref $outer)
    (struct.new $outer
      (struct.new $inner (i32.const 11))
      (struct.new $inner (i32.const 22))))

  ;; The same shape through an array, whose elements are popped rather than
  ;; read in place -- a different path in the evaluator, and one that allocates
  ;; the array *before* it reads the elements off the stack.
  (global $a (ref $arr)
    (array.new_fixed $arr 3
      (struct.new $inner (i32.const 1))
      (struct.new $inner (i32.const 2))
      (struct.new $inner (i32.const 4))))

  (global $src (ref $cell) (struct.new $cell (i32.const 99)))

  ;; Between the two, so $src is not still sitting in whatever page the next
  ;; allocation is bumping through.
  (global $spacer (ref $pad) (array.new $pad (i64.const 0) (i32.const 512)))

  ;; The initialiser reads $src and then allocates, which is the collection
  ;; point that used to leave the copy behind.
  (global $boxed (ref $box) (struct.new $box (global.get $src)))

  (func (export "global_get_identity")
    (if (i32.eqz (ref.eq (struct.get $box 0 (global.get $boxed))
                         (global.get $src)))
      (then unreachable)))

  ;; The same thing observed through a write rather than through ref.eq, in
  ;; case identity is answered by something other than the address.
  (func (export "global_get_shared")
    (struct.set $cell 0 (global.get $src) (i32.const 123))
    (if (i32.ne (struct.get $cell 0 (struct.get $box 0 (global.get $boxed)))
                (i32.const 123))
      (then unreachable))
    (struct.set $cell 0 (struct.get $box 0 (global.get $boxed)) (i32.const 45))
    (if (i32.ne (struct.get $cell 0 (global.get $src)) (i32.const 45))
      (then unreachable)))

  (func (export "struct_chain")
    (if (i32.ne
          (i32.add
            (struct.get $inner 0 (struct.get $outer 0 (global.get $g)))
            (struct.get $inner 0 (struct.get $outer 1 (global.get $g))))
          (i32.const 33))
      (then unreachable)))

  (func (export "array_elems") (local $s i32) (local $i i32)
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (i32.const 3)))
        (local.set $s (i32.add (local.get $s)
          (struct.get $inner 0
            (array.get $arr (global.get $a) (local.get $i)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (if (i32.ne (local.get $s) (i32.const 7)) (then unreachable))))
