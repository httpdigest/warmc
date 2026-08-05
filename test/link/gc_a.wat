;; The exporting side of the two-module *collection* test.
;;
;; Everything else under test/link is about reaching another instance -- its
;; memory, its globals, its code.  This pair is about the collector reaching
;; another module's *tables*: a cycle that begins inside A has to walk a frame
;; chain that alternates between A and B, and each frame's roots are named by
;; an index into the pool belonging to the module that frame's code came from.
;; One pool per module, so reading a frame's roots out of the wrong module's
;; pool is a root missed or a non-reference scanned -- and nothing else in the
;; suite ever has two modules that both allocate.
;;
;; Assembled with wasm-tools, not wat2wasm: this wabt build has no GC in it.
(module
  (type $box (struct (field $val (mut i32))))

  ;; The `$escape` convention every case under test/cases_gc uses, and for the
  ;; same reason: an allocation whose reference never leaves the function is
  ;; deleted at -O1 and above, and a frame with nothing live across a call
  ;; carries no safepoint at all.  Without this both modules compile to zero
  ;; safepoints and the test measures nothing -- which is exactly what the
  ;; first version of it did.
  (global $sink (mut anyref) (ref.null any))
  (func $escape (param $r anyref)
    (global.set $sink (local.get $r))
    (global.set $sink (ref.null any)))

  ;; Allocates a box and then `n` more that nobody keeps, holding the first
  ;; across every one of those allocations.  So this frame carries a safepoint
  ;; with a root in it, and that root is an index into *A's* pool.
  ;;
  ;; One live reference here against three in B's caller is deliberate: the two
  ;; pools are then different lengths as well as different contents, so a frame
  ;; read against the wrong one indexes past the end rather than landing on
  ;; something that happens to say the same thing.
  (func (export "churn") (param $n i32) (result i32)
    (local $keep (ref null $box))
    (local $i i32)
    (local.set $keep (struct.new $box (i32.const 4242)))
    (call $escape (local.get $keep))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (call $escape (struct.new $box (i32.const 1)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (struct.get $box $val (local.get $keep)))

  ;; `anyref` on both sides of the boundary, not `(ref $box)`: an import is
  ;; matched by the *structural hash* of its signature, and B has a $box of its
  ;; own that is a different type from this one.  So the object crosses as an
  ;; anyref and comes back to be cast here, in the module that knows what it
  ;; is -- which is also what makes B's frame hold a reference to an object
  ;; whose type entry belongs to A.
  (func (export "make") (result anyref)
    (local $b (ref null $box))
    (local.set $b (struct.new $box (i32.const 11)))
    (call $escape (local.get $b))
    (local.get $b))

  (func (export "val") (param $b anyref) (result i32)
    (struct.get $box $val (ref.cast (ref $box) (local.get $b))))
)
