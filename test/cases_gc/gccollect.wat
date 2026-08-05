;; The collector, from the outside.
;;
;; test/cases_gc/gc.wat is about what the *instructions* do to an object.  This
;; file is about what happens to that object when the collector runs over it,
;; which is a different instrument entirely: nothing here is interesting unless
;; a collection actually happens in the middle of it, so every case that
;; matters takes an iteration count and allocates enough garbage to force one.
;; test/run_gc.sh drives the same exports at several counts, at every
;; optimisation level, and under every combination of the collector's own
;; knobs -- stress (a full collection at every allocation), incremental and
;; not, and a write barrier buffer small enough to overflow.
;;
;; The shape of a case is always the same three steps: make something, make a
;; great deal of garbage, and then insist the first thing is still there and
;; still says what it said.  A rooting bug shows up as a wrong answer or a
;; fault; `WARMC_GC_VERIFY=1` turns it into a message that names the object.
;;
;; Every export checks its own answer and traps when it is wrong -- there is no
;; oracle, because wabt's interpreter has no garbage collection in it.
;;
;; Assembled with wasm-tools, not wat2wasm -- see test/run_gc.sh.

(module
  (type $node (sub (struct (field $next (mut (ref null $node)))
                           (field $val  (mut i32)))))
  ;; A subtype appends a second reference field.  The collector reaches the
  ;; inherited one through the supertype's offset and the new one through its
  ;; own, which is the same prefix rule the accessors depend on.
  (type $node2 (sub $node (struct (field $next (mut (ref null $node)))
                                  (field $val  (mut i32))
                                  (field $side (mut (ref null $node))))))

  (type $pair (struct (field $a (mut (ref null $pair)))
                      (field $b (mut (ref null $pair)))))

  ;; A field the *type* says is a host reference and that may hold a GC object
  ;; all the same: `extern.convert_any` hands one out under the extern type.
  (type $extbox (struct (field $e (mut externref))))

  (type $refs  (array (mut (ref null $node))))
  (type $anys  (array (mut anyref)))
  (type $bytes (array (mut i8)))
  (type $words (array (mut i64)))
  (type $vecs  (array (mut v128)))
  (type $arrs  (array (mut (ref null $refs))))

  (type $ii (func (param i32) (result i32)))

  (tag $boxed (param (ref $node)))
  (tag $plain (param i32))

  (global $keep (mut (ref null $node)) (ref.null $node))
  (global $keep2 (mut (ref null eq)) (ref.null eq))

  ;; What the optimiser must not reason away goes through here.  Since escape
  ;; analysis, an allocation whose reference never leaves the function is
  ;; deleted at -O1 and above -- which is right for programs and fatal for a
  ;; suite whose garbage and whose kept-in-frame objects are the point.  A
  ;; store to a global is an escape no intraprocedural analysis can see
  ;; through (another export could read it), and nulling the global right
  ;; after leaves the frame as the object's only root again -- exactly the
  ;; state the rooting cases exist to test.  The two stores survive inlining.
  (global $sink (mut anyref) (ref.null any))
  (func $escape (param $r anyref)
    (global.set $sink (local.get $r))
    (global.set $sink (ref.null any)))


  (table $t 8 (ref null $node))
  ;; A table of its own for `mark_writeback_table`, and 256 rather than 8: a
  ;; table is walked by `gc_visit_tables` on the collector's thread while the
  ;; program writes it, and eight entries gave the race too few chances to
  ;; catch a plain write-back at all.
  (table $wb 256 (ref null $node))
  (table $ft 4 funcref)
  (elem (table $ft) (i32.const 0) funcref (ref.func $one) (ref.func $two))

  (func $one (param i32) (result i32) (i32.add (local.get 0) (i32.const 1)))
  (func $two (param i32) (result i32) (i32.add (local.get 0) (i32.const 2)))

  (func $fail (unreachable))
  (func $eq32 (param i32 i32) (if (i32.ne (local.get 0) (local.get 1)) (then (call $fail))))

  ;; ---- the engine of every case below -----------------------------------
  ;;
  ;; `n` objects, each dropped the instant it exists, so the whole run is
  ;; garbage and the heap can only survive it by collecting.  Two allocations
  ;; per turn and two shapes, so the free lists see more than one size class.
  (func $churn (param $n i32)
    (local $i i32)
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (global.set $sink (struct.new $node (ref.null $node) (local.get $i)))
        (global.set $sink (array.new_default $refs (i32.and (local.get $i) (i32.const 7))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n))))))

  ;; A chain of `n` nodes, numbered from the tail up.
  (func $chain (param $n i32) (result (ref null $node))
    (local $i i32) (local $h (ref null $node))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $h (struct.new $node (local.get $h) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (local.get $h))

  (func $chain_sum (param $h (ref null $node)) (result i32)
    (local $s i32)
    (block $done
      (loop $l
        (br_if $done (ref.is_null (local.get $h)))
        (local.set $s (i32.add (local.get $s)
                        (struct.get $node $val (ref.as_non_null (local.get $h)))))
        (local.set $h (struct.get $node $next (ref.as_non_null (local.get $h))))
        (br $l)))
    (local.get $s))

  ;; ---- reclamation ------------------------------------------------------

  ;; Nothing survives, so the heap must be handed back.  Without a collector
  ;; this is the case that runs out of memory, and it is the only one here that
  ;; would: the reservation is a gigabyte and this asks for many times that.
  (func (export "churn") (param $n i32)
    (call $churn (local.get $n)))

  ;; The same, with each object briefly reachable from a global and then not.
  (func (export "churn_global") (param $n i32)
    (local $i i32)
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (global.set $keep (struct.new $node (ref.null $node) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $eq32 (struct.get $node $val (ref.as_non_null (global.get $keep)))
                (i32.sub (local.get $n) (i32.const 1)))
    (global.set $keep (ref.null $node)))

  ;; Two objects that point at each other and at nothing else.  A reference
  ;; count would never let go of these; a trace has no trouble with them, and
  ;; the only way to tell is that the heap does not fill up.
  (func (export "cycles_die") (param $n i32)
    (local $i i32) (local $p (ref $pair))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $p (struct.new $pair (ref.null $pair) (ref.null $pair)))
        (struct.set $pair $a (local.get $p)
          (struct.new $pair (local.get $p) (ref.null $pair)))
        (struct.set $pair $b (local.get $p)
          (struct.get $pair $a (local.get $p)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n))))))

  ;; ---- what has to survive ---------------------------------------------

  ;; A local, which is the case that rests entirely on the safepoint map: the
  ;; only reference to this object is in whatever register or frame slot the
  ;; allocator put it in.
  (func (export "keep_local") (param $n i32)
    (local $o (ref $node))
    (local.set $o (struct.new $node (ref.null $node) (i32.const 4242)))
    (call $escape (local.get $o))
    (call $churn (local.get $n))
    (call $eq32 (struct.get $node $val (local.get $o)) (i32.const 4242)))

  ;; Several at once, so more than one map entry has to be right, and the
  ;; register allocator has to put them somewhere a callee preserves.
  (func (export "keep_many_locals") (param $n i32)
    (local $a (ref $node)) (local $b (ref $node)) (local $c (ref $node))
    (local $d (ref $node)) (local $e (ref $node)) (local $f (ref $node))
    (local $g (ref $node)) (local $h (ref $node))
    (local.set $a (struct.new $node (ref.null $node) (i32.const 1)))
    (local.set $b (struct.new $node (local.get $a) (i32.const 2)))
    (local.set $c (struct.new $node (local.get $b) (i32.const 3)))
    (local.set $d (struct.new $node (local.get $c) (i32.const 4)))
    (local.set $e (struct.new $node (local.get $d) (i32.const 5)))
    (local.set $f (struct.new $node (local.get $e) (i32.const 6)))
    (local.set $g (struct.new $node (local.get $f) (i32.const 7)))
    (local.set $h (struct.new $node (local.get $g) (i32.const 8)))
    (call $churn (local.get $n))
    (call $eq32 (struct.get $node $val (local.get $a)) (i32.const 1))
    (call $eq32 (struct.get $node $val (local.get $b)) (i32.const 2))
    (call $eq32 (struct.get $node $val (local.get $c)) (i32.const 3))
    (call $eq32 (struct.get $node $val (local.get $d)) (i32.const 4))
    (call $eq32 (struct.get $node $val (local.get $e)) (i32.const 5))
    (call $eq32 (struct.get $node $val (local.get $f)) (i32.const 6))
    (call $eq32 (struct.get $node $val (local.get $g)) (i32.const 7))
    (call $eq32 (struct.get $node $val (local.get $h)) (i32.const 8))
    (call $eq32 (call $chain_sum (local.get $h)) (i32.const 36)))

  ;; A parameter, which the callee's own map has to name -- the caller has
  ;; already forgotten it.
  (func $through (param $o (ref $node)) (param $n i32) (result i32)
    (call $churn (local.get $n))
    (struct.get $node $val (local.get $o)))

  (func (export "keep_param") (param $n i32)
    (call $eq32
      (call $through (struct.new $node (ref.null $node) (i32.const 99))
                     (local.get $n))
      (i32.const 99)))

  ;; A global.
  (func (export "keep_global") (param $n i32)
    (global.set $keep (struct.new $node (ref.null $node) (i32.const 7)))
    (call $churn (local.get $n))
    (call $eq32 (struct.get $node $val (ref.as_non_null (global.get $keep)))
                (i32.const 7))
    (global.set $keep (ref.null $node)))

  ;; A table entry.
  (func (export "keep_table") (param $n i32)
    (table.set $t (i32.const 3)
      (struct.new $node (ref.null $node) (i32.const 31337)))
    (call $churn (local.get $n))
    (call $eq32 (struct.get $node $val
                  (ref.as_non_null (table.get $t (i32.const 3))))
                (i32.const 31337))
    (table.set $t (i32.const 3) (ref.null $node)))

  ;; One reference per frame, down a hundred frames, with the whole of the
  ;; churn at the bottom.  Every frame between here and there has to be walked
  ;; and every one of their maps read.
  (func $deep (param $d i32) (param $n i32) (result i32)
    (local $o (ref $node))
    (local.set $o (struct.new $node (ref.null $node) (local.get $d)))
    (call $escape (local.get $o))
    (if (i32.eqz (local.get $d))
      (then (call $churn (local.get $n)))
      (else (call $eq32 (call $deep (i32.sub (local.get $d) (i32.const 1))
                                    (local.get $n))
                        (i32.sub (local.get $d) (i32.const 1)))))
    (struct.get $node $val (local.get $o)))

  (func (export "keep_deep") (param $n i32)
    (call $eq32 (call $deep (i32.const 100) (local.get $n)) (i32.const 100)))

  ;; A list built first and walked after: the trace has to follow the whole
  ;; chain, which is the only path to every node but the head.
  (func (export "list_survives") (param $n i32)
    (local $h (ref null $node))
    (local.set $h (call $chain (i32.const 500)))
    (call $churn (local.get $n))
    ;; 0 + 1 + ... + 499
    (call $eq32 (call $chain_sum (local.get $h)) (i32.const 124750)))

  ;; A chain built and then walked, with **nothing allocated during the walk**.
  ;;
  ;; A round is answered at an allocation and nowhere else, so a walk is a span
  ;; in which the collector's Mark End request cannot be answered -- and every
  ;; reference the walk loads goes through the barrier's slow path and is
  ;; published to the mark queue in the meantime.  Whether that queue is drained
  ;; beside the program or inside the pause is the difference between a
  ;; collector whose pause is bounded and one whose longest pause is the whole
  ;; mark, and `taken at mark end` in the report is which.
  ;;
  ;; **Every other case in this file allocates on every iteration**, so the
  ;; round is answered within microseconds of being asked and the queue never
  ;; grows.  This is the only one that asks, and the whole file passed for as
  ;; long as it did not exist.
  ;;
  ;; The chain is rebuilt each turn rather than walked twice, because a slot is
  ;; healed at most once per phase: a second walk of the same chain under the
  ;; same colour goes nowhere near the barrier and publishes nothing.
  ;;
  ;; The walk's *length* is the caller's too, and the two callers want opposite
  ;; things from it.  The rows want many cheap turns, because what they are
  ;; asking is the ordinary one -- does the chain survive -- and a turn under
  ;; stress is a whole collection per node over a live set of `len`, which is
  ;; quadratic and did not finish at twenty thousand.  The pause check wants
  ;; few long ones, because a walk shorter than the gap between two rounds
  ;; publishes nothing much whatever the collector does with it.
  (func (export "walk_no_alloc") (param $n i32) (param $len i32)
    (local $i i32) (local $h (ref null $node))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $h (call $chain (local.get $len)))
        (call $eq32 (call $chain_sum (local.get $h))
          (i32.wrap_i64 (i64.div_u
            (i64.mul (i64.extend_i32_u (local.get $len))
                     (i64.extend_i32_u (i32.sub (local.get $len) (i32.const 1))))
            (i64.const 2))))
        (local.set $h (ref.null $node))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n))))))

  ;; A live set of whatever size the caller asks for, held across whatever
  ;; amount of allocation it asks for.  This is the shape a pause is worst on:
  ;; every cycle's mark phase has to walk the whole chain, so the stop-the-world
  ;; time grows with `m` while the incremental slice does not -- which is the
  ;; measurement in the report, and a test in its own right, because a chain of
  ;; a million nodes is also what overflows a grey set that is sized in bytes.
  (func (export "live_heap") (param $m i32) (param $n i32)
    (local $h (ref null $node))
    (local.set $h (call $chain (local.get $m)))
    (call $churn (local.get $n))
    (call $eq32 (call $chain_sum (local.get $h))
      (i32.wrap_i64 (i64.div_u
        (i64.mul (i64.extend_i32_u (local.get $m))
                 (i64.extend_i32_u (i32.sub (local.get $m) (i32.const 1))))
        (i64.const 2)))))

  ;; A tree rather than a list, so the trace branches and the mark stack is
  ;; more than a chain.
  (func $tree (param $d i32) (result (ref null $node))
    (local $l (ref null $node))
    (if (i32.eqz (local.get $d)) (then (return (ref.null $node))))
    (local.set $l (call $tree (i32.sub (local.get $d) (i32.const 1))))
    (struct.new $node2 (local.get $l) (i32.const 1)
      (call $tree (i32.sub (local.get $d) (i32.const 1)))))

  (func $tree_count (param $t (ref null $node)) (result i32)
    (if (ref.is_null (local.get $t)) (then (return (i32.const 0))))
    (i32.add (i32.const 1)
      (i32.add
        (call $tree_count (struct.get $node $next (ref.as_non_null (local.get $t))))
        (call $tree_count
          (struct.get $node2 $side
            (ref.cast (ref $node2) (ref.as_non_null (local.get $t))))))))

  (func (export "tree_survives") (param $n i32)
    (local $t (ref null $node))
    (local.set $t (call $tree (i32.const 12)))
    (call $churn (local.get $n))
    (call $eq32 (call $tree_count (local.get $t)) (i32.const 4095)))

  ;; ---- arrays -----------------------------------------------------------

  (func (export "array_refs") (param $n i32)
    (local $a (ref $refs)) (local $i i32) (local $s i32)
    (local.set $a (array.new_default $refs (i32.const 64)))
    (loop $l
      (array.set $refs (local.get $a) (local.get $i)
        (struct.new $node (ref.null $node) (local.get $i)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 64))))
    (call $churn (local.get $n))
    (local.set $i (i32.const 0))
    (loop $c
      (local.set $s (i32.add (local.get $s)
        (struct.get $node $val
          (ref.as_non_null (array.get $refs (local.get $a) (local.get $i))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $c (i32.lt_u (local.get $i) (i32.const 64))))
    (call $eq32 (local.get $s) (i32.const 2016)))

  ;; Longer than the chunk the marker stops an array scan at, so the scan has
  ;; to resume where it left off -- twice.
  (func (export "big_array_refs") (param $n i32)
    (local $a (ref $refs)) (local $i i32) (local $s i32)
    (local.set $a (array.new_default $refs (i32.const 10000)))
    (loop $l
      (array.set $refs (local.get $a) (local.get $i)
        (struct.new $node (ref.null $node) (i32.const 1)))
      (local.set $i (i32.add (local.get $i) (i32.const 3)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 10000))))
    (call $churn (local.get $n))
    (local.set $i (i32.const 0))
    (loop $c
      (local.set $s (i32.add (local.get $s)
        (struct.get $node $val
          (ref.as_non_null (array.get $refs (local.get $a) (local.get $i))))))
      (local.set $i (i32.add (local.get $i) (i32.const 3)))
      (br_if $c (i32.lt_u (local.get $i) (i32.const 10000))))
    (call $eq32 (local.get $s) (i32.const 3334)))

  ;; An array of arrays: two levels, and the inner ones are reachable only
  ;; through the outer.
  (func (export "nested_arrays") (param $n i32)
    (local $o (ref $arrs)) (local $i i32)
    (local.set $o (array.new_default $arrs (i32.const 32)))
    (loop $l
      (array.set $arrs (local.get $o) (local.get $i)
        (array.new $refs (struct.new $node (ref.null $node) (local.get $i))
                          (i32.const 4)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 32))))
    (call $churn (local.get $n))
    (local.set $i (i32.const 0))
    (loop $c
      (call $eq32
        (struct.get $node $val (ref.as_non_null
          (array.get $refs
            (ref.as_non_null (array.get $arrs (local.get $o) (local.get $i)))
            (i32.const 2))))
        (local.get $i))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $c (i32.lt_u (local.get $i) (i32.const 32)))))

  ;; Arrays with no references in them at all, of three different widths, so
  ;; the free lists see sizes that are not the ones everything else uses and
  ;; the walk has to step over payloads it must not read as pointers.  The
  ;; i64 array is deliberately filled with values that *look* like heap
  ;; addresses; a conservative collector would keep something alive here.
  (func (export "opaque_arrays") (param $n i32)
    (local $b (ref $bytes)) (local $w (ref $words)) (local $v (ref $vecs))
    (local $i i32)
    (local.set $b (array.new $bytes (i32.const 0x5a) (i32.const 300)))
    (local.set $w (array.new $words (i64.const 0x1122334455667780) (i32.const 40)))
    (local.set $v (array.new_default $vecs (i32.const 8)))
    (call $churn (local.get $n))
    (call $eq32 (array.get_u $bytes (local.get $b) (i32.const 299)) (i32.const 0x5a))
    (call $eq32 (array.len (local.get $w)) (i32.const 40))
    (if (i64.ne (array.get $words (local.get $w) (i32.const 39))
                (i64.const 0x1122334455667780))
      (then (call $fail)))
    (call $eq32 (i32x4.extract_lane 0 (array.get $vecs (local.get $v) (i32.const 7)))
                (i32.const 0)))

  ;; ---- identity ---------------------------------------------------------

  ;; A collector that moved anything would fail this, and so would one that
  ;; handed the same address out twice.
  (func (export "identity") (param $n i32)
    (local $a (ref $node)) (local $b (ref $node))
    (local.set $a (struct.new $node (ref.null $node) (i32.const 1)))
    (local.set $b (struct.new $node (ref.null $node) (i32.const 1)))
    (if (ref.eq (local.get $a) (local.get $b)) (then (call $fail)))
    (call $churn (local.get $n))
    (if (i32.eqz (ref.eq (local.get $a) (local.get $a))) (then (call $fail)))
    (if (ref.eq (local.get $a) (local.get $b)) (then (call $fail)))
    (global.set $keep2 (local.get $a))
    (call $churn (local.get $n))
    (if (i32.eqz (ref.eq (ref.as_non_null (global.get $keep2)) (local.get $a)))
      (then (call $fail)))
    (global.set $keep2 (ref.null eq)))

  ;; The four things a reference can be, in one array, across a collection.
  ;; Three of them are not heap objects and the trace has to leave all three
  ;; alone -- an i31 and a host reference by their low bits, a null by being
  ;; zero.
  (func (export "mixed_tags") (param $n i32) (param $host externref)
    (local $a (ref $anys)) (local $i i32)
    (local.set $a (array.new_default $anys (i32.const 40)))
    (loop $l
      (array.set $anys (local.get $a) (local.get $i)
        (struct.new $node (ref.null $node) (local.get $i)))
      (array.set $anys (local.get $a) (i32.add (local.get $i) (i32.const 1))
        (ref.i31 (local.get $i)))
      (array.set $anys (local.get $a) (i32.add (local.get $i) (i32.const 2))
        (any.convert_extern (local.get $host)))
      (local.set $i (i32.add (local.get $i) (i32.const 4)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 40))))
    (call $churn (local.get $n))
    (local.set $i (i32.const 0))
    (loop $c
      (call $eq32
        (struct.get $node $val
          (ref.cast (ref $node) (array.get $anys (local.get $a) (local.get $i))))
        (local.get $i))
      (call $eq32
        (i31.get_u (ref.cast (ref i31)
          (array.get $anys (local.get $a) (i32.add (local.get $i) (i32.const 1)))))
        (local.get $i))
      (if (ref.test (ref eq)
            (array.get $anys (local.get $a) (i32.add (local.get $i) (i32.const 2))))
        (then (call $fail)))
      (if (i32.eqz (ref.is_null
            (array.get $anys (local.get $a) (i32.add (local.get $i) (i32.const 3)))))
        (then (call $fail)))
      (local.set $i (i32.add (local.get $i) (i32.const 4)))
      (br_if $c (i32.lt_u (local.get $i) (i32.const 40)))))

  ;; A funcref cell is sixteen-byte aligned and lives in the instance, not the
  ;; heap -- so it looks exactly like an object until the collector asks
  ;; whether the address is inside the reservation.  Getting that wrong reads
  ;; a function pointer as a type index.
  (func (export "funcrefs_untouched") (param $n i32)
    (local $f (ref null func)) (local $o (ref $node))
    (local.set $o (struct.new $node (ref.null $node) (i32.const 5)))
    (local.set $f (ref.func $two))
    (table.set $ft (i32.const 2) (ref.func $one))
    (call $churn (local.get $n))
    (call $eq32 (call_indirect $ft (type $ii) (i32.const 10) (i32.const 0))
                (i32.const 11))
    (call $eq32 (call_indirect $ft (type $ii) (i32.const 10) (i32.const 2))
                (i32.const 11))
    (call $eq32 (call_ref $ii (i32.const 10)
                  (ref.cast (ref $ii) (local.get $f)))
                (i32.const 12))
    (call $eq32 (struct.get $node $val (local.get $o)) (i32.const 5)))

  ;; ---- the write barrier ------------------------------------------------

  ;; The case the barrier exists for.  The only reference to the payload is
  ;; taken out of a heap field into a local, the field is nulled, and then the
  ;; program allocates for a long time.  A collector that had already walked
  ;; past the box, and that only learns about references from the heap, would
  ;; never see the payload again.
  (func (export "hidden_by_store") (param $n i32)
    (local $box (ref $node)) (local $p (ref null $node))
    (local.set $box (struct.new $node
      (struct.new $node (ref.null $node) (i32.const 6060)) (i32.const 0)))
    (call $escape (local.get $box))
    (local.set $p (struct.get $node $next (local.get $box)))
    (struct.set $node $next (local.get $box) (ref.null $node))
    (call $churn (local.get $n))
    (call $eq32 (struct.get $node $val (ref.as_non_null (local.get $p)))
                (i32.const 6060))
    (struct.set $node $next (local.get $box) (local.get $p))
    (call $churn (local.get $n))
    (call $eq32 (struct.get $node $val
                  (ref.as_non_null (struct.get $node $next (local.get $box))))
                (i32.const 6060)))

  ;; ---- what a barrier may be dropped for ---------------------------------

  ;; One slot, read twice, with a collection in between.
  ;;
  ;; `pass_bar_elide` drops the second read's barrier when nothing between the
  ;; two can collect: self-healing means the first read wrote the repaired
  ;; reference back into the slot, so the second one cannot find anything
  ;; wrong with it.  An allocation between them is exactly what can collect,
  ;; and after one the slot holds the colour the *mark* left on it -- the
  ;; pause repairs the roots and nothing else -- so the second read owes a
  ;; barrier it must not be let off.
  ;;
  ;; `$a` is a root across the allocation and so comes back current whatever
  ;; happens; `$b` comes back current only if it was barriered.  Comparing
  ;; them makes the difference a wrong answer rather than a fault, and makes
  ;; it one in every configuration: without relocation the two still differ,
  ;; because the colour does.
  (func (export "reload_across_collect") (param $n i32)
    (local $box (ref $node)) (local $i i32)
    (local $a (ref null $node)) (local $b (ref null $node))
    (local.set $box (struct.new $node
      (struct.new $node (ref.null $node) (i32.const 7070)) (i32.const 0)))
    (call $escape (local.get $box))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $a (struct.get $node $next (local.get $box)))
        ;; A megabyte a turn, so that the pacing starts a cycle every few of
        ;; them and *every* cycle it starts falls between the two reads --
        ;; which is what the case is about, and what a loop of small
        ;; allocations would only manage by luck.  Through `$escape` so that
        ;; nothing may reason it away, and because an opaque call between the
        ;; reads is also what stops the optimiser forwarding the second one
        ;; and leaving nothing here to elide.
        (call $escape (array.new_default $bytes (i32.const 1000000)))
        (local.set $b (struct.get $node $next (local.get $box)))
        (if (i32.eqz (ref.eq (local.get $a) (local.get $b))) (then (call $fail)))
        (call $eq32 (struct.get $node $val (ref.as_non_null (local.get $b)))
                    (i32.const 7070))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n))))))

  ;; The reference family a barrier may *not* be dropped for on type alone.
  ;;
  ;; A funcref is a cell outside the heap and an i31 is its own payload, so
  ;; neither ever moves and neither ever wears a colour -- `vt_ref_collectable`
  ;; says so and no barrier is emitted for either.  An externref looks like the
  ;; same kind of thing and is not: `extern.convert_any` puts a struct into one
  ;; and `any.convert_extern` takes it back out, so the slot holds a reference
  ;; the collector moves and recolours like any other.
  ;;
  ;; `$k` is a root the pause repaired whatever happened; the field is a slot
  ;; only the barrier repairs.  Comparing them makes the difference a wrong
  ;; answer in every configuration, relocating or not -- the colour alone is
  ;; enough to tell them apart.
  (func (export "extern_field_moves") (param $n i32)
    (local $b (ref $extbox)) (local $k (ref eq))
    (local.set $k (struct.new $node (ref.null $node) (i32.const 5150)))
    (local.set $b (struct.new $extbox (extern.convert_any (local.get $k))))
    (call $escape (local.get $b))
    (call $churn (local.get $n))
    (if (i32.eqz (ref.eq (local.get $k)
                   (ref.cast (ref eq)
                     (any.convert_extern (struct.get $extbox $e (local.get $b))))))
      (then (call $fail)))
    (call $eq32 (struct.get $node $val
                  (ref.cast (ref $node)
                    (any.convert_extern (struct.get $extbox $e (local.get $b)))))
                (i32.const 5150)))

  ;; And the one writer that puts a bad reference into a slot without a
  ;; safepoint anywhere near it.
  ;;
  ;; `array.copy` moves elements as bytes and heals none of them, so a slot
  ;; that was barriered a moment ago is not a slot that stayed good: the copy
  ;; can have written the source's stale bits over it.  The pass ends a region
  ;; at every bulk array operation for this and for nothing else.
  ;;
  ;; `$c` reads the source through a barrier of its own, so it is the same
  ;; object as `$b` and current; `$b` is current only if the copy stopped the
  ;; elision.
  (func (export "copy_then_reload") (param $n i32)
    (local $src (ref $refs)) (local $dst (ref $refs))
    (local $b (ref null $node)) (local $c (ref null $node)) (local $i i32)
    (local.set $src (array.new_default $refs (i32.const 4)))
    (local.set $dst (array.new_default $refs (i32.const 4)))
    (array.set $refs (local.get $src) (i32.const 0)
      (struct.new $node (ref.null $node) (i32.const 8080)))
    (array.set $refs (local.get $dst) (i32.const 0)
      (struct.new $node (ref.null $node) (i32.const 9090)))
    (call $escape (local.get $src))
    (call $escape (local.get $dst))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        ;; A cycle a turn, for the same reason and by the same means as above:
        ;; it is what leaves `$src[0]` holding the colour the mark put there.
        (call $escape (array.new_default $bytes (i32.const 1000000)))
        ;; The first read heals `$dst[0]`; the copy then puts `$src[0]`'s bits
        ;; -- which nothing has healed -- straight on top of it.
        (drop (array.get $refs (local.get $dst) (i32.const 0)))
        (array.copy $refs $refs (local.get $dst) (i32.const 0)
                                (local.get $src) (i32.const 0) (i32.const 1))
        (local.set $b (array.get $refs (local.get $dst) (i32.const 0)))
        (local.set $c (array.get $refs (local.get $src) (i32.const 0)))
        (if (i32.eqz (ref.eq (local.get $b) (local.get $c))) (then (call $fail)))
        (call $eq32 (struct.get $node $val (ref.as_non_null (local.get $b)))
                    (i32.const 8080))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n))))))

  ;; The same, over and over, so that a cycle is somewhere in the middle of
  ;; marking for some of them however the pacing falls out -- and so that the
  ;; barrier's buffer is asked to hold a great many entries.
  (func (export "hide_and_restore") (param $n i32)
    (local $box (ref $node)) (local $p (ref null $node)) (local $i i32)
    (local.set $box (struct.new $node
      (struct.new $node (ref.null $node) (i32.const 909)) (i32.const 0)))
    (call $escape (local.get $box))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $p (struct.get $node $next (local.get $box)))
        (struct.set $node $next (local.get $box) (ref.null $node))
        (global.set $sink (struct.new $node (ref.null $node) (i32.const 0)))
        (struct.set $node $next (local.get $box) (local.get $p))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $eq32 (struct.get $node $val
                  (ref.as_non_null (struct.get $node $next (local.get $box))))
                (i32.const 909)))

  ;; The array forms of the same store, which the runtime carries out and
  ;; therefore has to run the barrier for itself.
  (func (export "array_set_barrier") (param $n i32)
    (local $a (ref $refs)) (local $p (ref null $node)) (local $i i32)
    (local.set $a (array.new_default $refs (i32.const 4)))
    (array.set $refs (local.get $a) (i32.const 1)
      (struct.new $node (ref.null $node) (i32.const 1234)))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $p (array.get $refs (local.get $a) (i32.const 1)))
        (array.fill $refs (local.get $a) (i32.const 0) (ref.null $node)
                    (i32.const 4))
        (global.set $sink (struct.new $node (ref.null $node) (i32.const 0)))
        (array.set $refs (local.get $a) (i32.const 1) (local.get $p))
        (array.copy $refs $refs (local.get $a) (i32.const 2)
                    (local.get $a) (i32.const 0) (i32.const 2))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $eq32 (struct.get $node $val
                  (ref.as_non_null (array.get $refs (local.get $a) (i32.const 1))))
                (i32.const 1234))
    (call $eq32 (struct.get $node $val
                  (ref.as_non_null (array.get $refs (local.get $a) (i32.const 3))))
                (i32.const 1234)))

  ;; A loop with no allocation in it, storing into a *different* old object
  ;; every turn.  One object's remembered-set entry says nothing about
  ;; another's, so a barrier lifted out of this loop would record the first
  ;; holder and lose the other sixty-three.  The young nodes are made before
  ;; the storing loop and the array that held them is cleared after it, so
  ;; the only way to each of them is through the holder it was stored into.
  (func (export "loop_many_targets") (param $n i32)
    (local $h (ref $refs)) (local $y (ref $refs)) (local $i i32) (local $k i32)
    (local.set $h (array.new_default $refs (i32.const 64)))
    (local.set $y (array.new_default $refs (i32.const 64)))
    (loop $mk
      (array.set $refs (local.get $h) (local.get $i)
        (struct.new $node (ref.null $node) (i32.const 0)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $mk (i32.lt_u (local.get $i) (i32.const 64))))
    (call $churn (i32.const 4000))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $outer
        (local.set $i (i32.const 0))
        (loop $fill
          (array.set $refs (local.get $y) (local.get $i)
            (struct.new $node (ref.null $node) (i32.const 4321)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br_if $fill (i32.lt_u (local.get $i) (i32.const 64))))
        (local.set $i (i32.const 0))
        (loop $link
          (struct.set $node $next
            (ref.as_non_null (array.get $refs (local.get $h) (local.get $i)))
            (array.get $refs (local.get $y) (local.get $i)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br_if $link (i32.lt_u (local.get $i) (i32.const 64))))
        (local.set $i (i32.const 0))
        (loop $clr
          (array.set $refs (local.get $y) (local.get $i) (ref.null $node))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br_if $clr (i32.lt_u (local.get $i) (i32.const 64))))
        (call $churn (local.get $n))
        (local.set $i (i32.const 0))
        (loop $chk
          (call $eq32 (struct.get $node $val
                        (ref.as_non_null
                          (struct.get $node $next
                            (ref.as_non_null
                              (array.get $refs (local.get $h)
                                              (local.get $i))))))
                      (i32.const 4321))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br_if $chk (i32.lt_u (local.get $i) (i32.const 64))))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br_if $outer (i32.lt_u (local.get $k) (local.get $n))))))

  ;; A table a cycle has already read is not read again until something is
  ;; written into it -- so the *write* is what has to say so.  Churn until the
  ;; table has settled and a cycle has passed over it, then store one fresh
  ;; object into it whose only reference is that slot, then churn again so a
  ;; collection happens between the store and the read.  A collection that
  ;; decides tables by a flag, and a flag the store forgot to set, frees it.
  (func (export "table_late_store") (param $n i32) (param $q i32)
    (local $i i32) (local $s i32)
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (call $churn (local.get $q))
        (table.set $t (i32.const 3)
          (struct.new $node (ref.null $node) (i32.const 777)))
        (call $churn (local.get $q))
        (local.set $s (i32.add (local.get $s)
          (struct.get $node $val
            (ref.as_non_null (table.get $t (i32.const 3))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $eq32 (local.get $s) (i32.mul (local.get $n) (i32.const 777))))

  ;; An object handed to a container while a mark phase is running is *born
  ;; old*: the header base carries the age bit for the whole of a mark.  So
  ;; nothing ever marks it -- it arrives already marked -- and nothing that
  ;; records what a minor's marking found will ever mention it.  A sweep that
  ;; decides which spans to read by what marking found must therefore be told
  ;; about the span some other way, or it reads an empty answer and hands back
  ;; memory holding a live object.
  ;;
  ;; The churn between rounds is what makes the case bite: it fills the rest
  ;; of the span with objects that really are dead, so nothing else in there
  ;; survives to keep the span honest, and it hands the freed memory straight
  ;; back out again so a wrong answer is a wrong *value* and not merely a
  ;; dangling one.  Only reachable under a slice budget small enough that a
  ;; mark phase is still running while the program allocates.
  (func (export "born_old_kept") (param $n i32)
    (local $a (ref $refs)) (local $i i32) (local $k i32)
    (local.set $a (array.new_default $refs (i32.const 64)))
    ;; Old enough that a minor will not look inside it of its own accord.
    (call $churn (i32.const 4000))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $outer
        (local.set $i (i32.const 0))
        (loop $fill
          (array.set $refs (local.get $a) (local.get $i)
            (struct.new $node (ref.null $node) (i32.const 1234)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br_if $fill (i32.lt_u (local.get $i) (i32.const 64))))
        (call $churn (i32.const 500))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br_if $outer (i32.lt_u (local.get $k) (local.get $n)))))
    (local.set $i (i32.const 0))
    (loop $chk
      (call $eq32 (struct.get $node $val
                    (ref.as_non_null
                      (array.get $refs (local.get $a) (local.get $i))))
                  (i32.const 1234))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $chk (i32.lt_u (local.get $i) (i32.const 64)))))

  ;; More *distinct old objects* stored into than the remembered set can hold,
  ;; and then a quiet stretch of pure allocation for the collector to run in.
  ;;
  ;; This is the one shape that tells the truth about a stale dedupe bit.  The
  ;; barrier remembers an old object once, and the once is a bit in its header;
  ;; a buffer with no room for the entry leaves the bit set anyway, and from
  ;; then on that object's stores are skipped by an entry that does not exist.
  ;; The collector's answer is that a dropped entry makes the next cycle a
  ;; major and that major's sweep clear every bit as it walks -- and *this* is
  ;; the case that fails when it does not, because the only reference to each
  ;; fresh node is the field of an old holder, and an old holder is exactly
  ;; what a minor collection declines to look inside.
  ;;
  ;; The holders have to outnumber the buffer, which is why the count is a
  ;; hundred and twenty-eight and why test/run_gc_modes.sh runs it once with
  ;; the buffer pinned smaller than that.  With the default buffer nothing
  ;; overflows and this is an ordinary generational case, which is worth
  ;; running too.
  (func (export "remset_overflow") (param $n i32) (param $q i32)
    (local $a (ref $refs)) (local $i i32) (local $k i32) (local $s i32)
    (local.set $a (array.new_default $refs (i32.const 128)))
    (loop $mk
      (array.set $refs (local.get $a) (local.get $i)
        (struct.new $node (ref.null $node) (local.get $i)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $mk (i32.lt_u (local.get $i) (i32.const 128))))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $round
        ;; One fresh node per holder, reachable through that holder and
        ;; nothing else.  Overwriting the previous round's node is what makes
        ;; every one of these a barriered store rather than an initialising
        ;; one.
        (local.set $i (i32.const 0))
        (loop $fill
          (struct.set $node $next
            (ref.as_non_null (array.get $refs (local.get $a) (local.get $i)))
            (struct.new $node (ref.null $node)
                              (i32.add (local.get $i) (i32.const 1000))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br_if $fill (i32.lt_u (local.get $i) (i32.const 128))))
        ;; Nothing but garbage now, so whatever cycle the pacing starts here
        ;; runs to the end without another reference store to record -- and a
        ;; minor is what the holders' contents have to survive.
        (call $churn (local.get $q))
        (local.set $i (i32.const 0))
        (loop $chk
          (local.set $s (i32.add (local.get $s)
            (struct.get $node $val
              (ref.as_non_null (struct.get $node $next
                (ref.as_non_null (array.get $refs (local.get $a)
                                                  (local.get $i))))))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br_if $chk (i32.lt_u (local.get $i) (i32.const 128))))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br_if $round (i32.lt_u (local.get $k) (local.get $n)))))
    ;; 1000 + ... + 1127, once per round
    (call $eq32 (local.get $s) (i32.mul (local.get $n) (i32.const 136128))))

  ;; Two thousand reference stores between one allocation and the next, which
  ;; is what the write barrier's buffer is sized against.  With the buffer
  ;; small -- which is the only way a test reaches it -- this is what makes it
  ;; overflow, and the overflow is not a dropped entry: the collector takes a
  ;; fresh snapshot and marks to completion with nothing else running.  The
  ;; answer has to come out the same either way.
  (func (export "bulk_ref_stores") (param $n i32)
    (local $a (ref $refs)) (local $b (ref $refs)) (local $i i32) (local $s i32)
    (local.set $a (array.new_default $refs (i32.const 1024)))
    (local.set $b (array.new_default $refs (i32.const 1024)))
    (loop $fill
      (array.set $refs (local.get $a) (local.get $i)
        (struct.new $node (ref.null $node) (local.get $i)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $fill (i32.lt_u (local.get $i) (i32.const 1024))))
    (local.set $i (i32.const 0))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (array.copy $refs $refs (local.get $b) (i32.const 0)
                                (local.get $a) (i32.const 0) (i32.const 1024))
        (array.fill $refs (local.get $b) (i32.const 0) (ref.null $node)
                          (i32.const 1024))
        (global.set $sink (struct.new $node (ref.null $node) (i32.const 0)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (local.set $i (i32.const 0))
    (loop $c
      (local.set $s (i32.add (local.get $s)
        (struct.get $node $val
          (ref.as_non_null (array.get $refs (local.get $a) (local.get $i))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $c (i32.lt_u (local.get $i) (i32.const 1024))))
    ;; 0 + 1 + ... + 1023
    (call $eq32 (local.get $s) (i32.const 523776)))

  ;; The same bulk copy where the two ranges overlap, which is the arm that
  ;; runs backwards.  A reference moves a whole word at a time: the marker
  ;; reads a slot with an eight-byte load and may do so at any instant during
  ;; the copy, so moving them a byte at a time could hand it an address made
  ;; of the low half of one reference and the high half of another -- still
  ;; inside the heap, and naming no object.
  (func (export "copy_refs_overlap") (param $n i32)
    (local $a (ref $refs)) (local $i i32) (local $j i32)
    (local.set $a (array.new_default $refs (i32.const 1024)))
    (call $escape (local.get $a))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $j (i32.const 0))
        (loop $fill
          (array.set $refs (local.get $a) (local.get $j)
            (struct.new $node (ref.null $node) (local.get $j)))
          (local.set $j (i32.add (local.get $j) (i32.const 1)))
          (br_if $fill (i32.lt_u (local.get $j) (i32.const 1024))))

        ;; Destination below source: forwards.
        (array.copy $refs $refs (local.get $a) (i32.const 0)
                                (local.get $a) (i32.const 8) (i32.const 1000))
        (call $eq32 (struct.get $node $val (ref.as_non_null
                      (array.get $refs (local.get $a) (i32.const 0))))
                    (i32.const 8))
        (call $eq32 (struct.get $node $val (ref.as_non_null
                      (array.get $refs (local.get $a) (i32.const 999))))
                    (i32.const 1007))

        (local.set $j (i32.const 0))
        (loop $refill
          (array.set $refs (local.get $a) (local.get $j)
            (struct.new $node (ref.null $node) (local.get $j)))
          (local.set $j (i32.add (local.get $j) (i32.const 1)))
          (br_if $refill (i32.lt_u (local.get $j) (i32.const 1024))))

        ;; Destination above source: backwards.
        (array.copy $refs $refs (local.get $a) (i32.const 8)
                                (local.get $a) (i32.const 0) (i32.const 1000))
        (call $eq32 (struct.get $node $val (ref.as_non_null
                      (array.get $refs (local.get $a) (i32.const 8))))
                    (i32.const 0))
        (call $eq32 (struct.get $node $val (ref.as_non_null
                      (array.get $refs (local.get $a) (i32.const 1007))))
                    (i32.const 999))

        (global.set $sink (struct.new $node (ref.null $node) (i32.const 0)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n))))))

  ;; A `struct.new`'s own field stores write over the zero the allocator left,
  ;; so they carry no barrier at all.  This is what says the two are still
  ;; telling the truth about each other: the value stored is the only reference
  ;; to a fresh object, and it has to survive whatever comes next.
  (func (export "init_stores") (param $n i32)
    (local $a (ref $refs)) (local $i i32) (local $s i32)
    (local.set $a (array.new_fixed $refs 4
      (struct.new $node (ref.null $node) (i32.const 10))
      (struct.new $node (ref.null $node) (i32.const 20))
      (struct.new $node (ref.null $node) (i32.const 30))
      (struct.new $node (ref.null $node) (i32.const 40))))
    (call $churn (local.get $n))
    (loop $c
      (local.set $s (i32.add (local.get $s)
        (struct.get $node $val
          (ref.as_non_null (array.get $refs (local.get $a) (local.get $i))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $c (i32.lt_u (local.get $i) (i32.const 4))))
    (call $eq32 (local.get $s) (i32.const 100)))

  ;; ---- exceptions -------------------------------------------------------

  ;; An exception is a heap object, and its payload is a reference the
  ;; collector reaches only through it.
  (func $raise (param $v i32) (throw $boxed (struct.new $node (ref.null $node) (local.get $v))))

  (func (export "exn_payload") (param $n i32)
    (local $o (ref null $node))
    (block $h (result (ref $node))
      (try_table (catch $boxed $h) (call $raise (i32.const 555)))
      (call $fail)
      (return))
    (local.set $o)
    (call $churn (local.get $n))
    (call $eq32 (struct.get $node $val (ref.as_non_null (local.get $o)))
                (i32.const 555)))

  ;; The exception object itself, kept across a collection and thrown again.
  (func (export "exnref_kept") (param $n i32)
    (local $e (ref null exn)) (local $o (ref null $node))
    (block $h (result (ref $node) exnref)
      (try_table (catch_ref $boxed $h) (call $raise (i32.const 777)))
      (call $fail)
      (return))
    (local.set $e)
    (drop)
    (call $churn (local.get $n))
    (block $h2 (result (ref $node))
      (try_table (catch $boxed $h2) (throw_ref (local.get $e)))
      (call $fail)
      (return))
    (local.set $o)
    (call $churn (local.get $n))
    (call $eq32 (struct.get $node $val (ref.as_non_null (local.get $o)))
                (i32.const 777)))

  ;; Throwing in a loop used to leak one object per throw, because exceptions
  ;; came out of a region that was never freed.  Now they are ordinary objects
  ;; and this is a churn like any other.
  (func (export "many_throws") (param $n i32)
    (local $i i32)
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (block $h (result (ref $node))
          (try_table (catch $boxed $h) (call $raise (local.get $i)))
          (call $fail)
          (return))
        (drop)
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n))))))

  ;; A reference live across the *whole* of a protected region, so it is in the
  ;; safepoint map of a call that is also a protected call site: the two tables
  ;; describe the same return address and neither may confuse the other.
  (func (export "keep_across_try") (param $n i32)
    (local $o (ref $node))
    (local.set $o (struct.new $node (ref.null $node) (i32.const 8888)))
    (call $escape (local.get $o))
    (block $h (result i32)
      (try_table (catch $plain $h)
        (call $churn (local.get $n))
        (throw $plain (i32.const 1)))
      (unreachable))
    (drop)
    (call $churn (local.get $n))
    (call $eq32 (struct.get $node $val (local.get $o)) (i32.const 8888)))

  ;; ---- control flow -----------------------------------------------------

  ;; References carried on block parameters and merged from two paths, which
  ;; is where the register allocator's coalescing decides the location a map
  ;; entry names.
  (func (export "block_params") (param $n i32)
    (local $o (ref null $node))
    (local.set $o
      (block $b (result (ref $node))
        (if (i32.and (local.get $n) (i32.const 1))
          (then (br $b (struct.new $node (ref.null $node) (i32.const 11))))
          (else (br $b (struct.new $node (ref.null $node) (i32.const 22)))))
        (unreachable)))
    (call $churn (local.get $n))
    (if (i32.and (local.get $n) (i32.const 1))
      (then (call $eq32 (struct.get $node $val (ref.as_non_null (local.get $o)))
                        (i32.const 11)))
      (else (call $eq32 (struct.get $node $val (ref.as_non_null (local.get $o)))
                        (i32.const 22)))))

  ;; A reference that survives a cast branch, so the value the map names is the
  ;; one `br_on_cast` carried rather than the one it was given.
  (func (export "cast_paths") (param $n i32)
    (local $o (ref null $node))
    (local.set $o
      (block $b (result (ref $node2))
        (drop (br_on_cast $b anyref (ref $node2)
          (struct.new $node2 (ref.null $node) (i32.const 33) (ref.null $node))))
        (unreachable)))
    (call $churn (local.get $n))
    (call $eq32 (struct.get $node $val (ref.as_non_null (local.get $o)))
                (i32.const 33)))

  ;; The subtype's own reference field, reached through the supertype's
  ;; accessor for the inherited one and its own for the new one.
  (func (export "subtype_fields") (param $n i32)
    (local $o (ref $node2))
    (local.set $o (struct.new $node2
      (struct.new $node (ref.null $node) (i32.const 1))
      (i32.const 2)
      (struct.new $node (ref.null $node) (i32.const 3))))
    (call $escape (local.get $o))
    (call $churn (local.get $n))
    (call $eq32 (struct.get $node $val
                  (ref.as_non_null (struct.get $node $next (local.get $o))))
                (i32.const 1))
    (call $eq32 (struct.get $node2 $val (local.get $o)) (i32.const 2))
    (call $eq32 (struct.get $node $val
                  (ref.as_non_null (struct.get $node2 $side (local.get $o))))
                (i32.const 3)))

  ;; ---- the allocator ----------------------------------------------------

  ;; Sizes all over the free lists' classes, alternately kept and dropped, so
  ;; the sweep leaves holes and the next round has to split and coalesce.  What
  ;; this checks is that nothing is corrupted; `WARMC_GC_VERIFY=1` is what
  ;; turns "nothing is corrupted" into a statement about every block.
  (func (export "fragment") (param $n i32)
    (local $i i32) (local $keepers (ref $anys))
    (local.set $keepers (array.new_default $anys (i32.const 64)))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (array.set $anys (local.get $keepers)
          (i32.and (local.get $i) (i32.const 63))
          (array.new_default $words
            (i32.and (local.get $i) (i32.const 127))))
        (global.set $sink (array.new_default $bytes
          (i32.mul (i32.and (local.get $i) (i32.const 31)) (i32.const 9))))
        (global.set $sink (array.new_default $vecs (i32.and (local.get $i) (i32.const 15))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $eq32 (array.len (local.get $keepers)) (i32.const 64)))

  ;; One object far larger than any size class, so the large buckets are what
  ;; serve it and a split has to leave the remainder usable.
  (func (export "large_objects") (param $n i32)
    (local $i i32) (local $a (ref null $words))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $a (array.new_default $words (i32.const 20000)))
        (array.set $words (ref.as_non_null (local.get $a)) (i32.const 19999)
                   (i64.const 42))
        (call $churn (i32.const 8))
        (if (i64.ne (array.get $words (ref.as_non_null (local.get $a))
                               (i32.const 19999)) (i64.const 42))
          (then (call $fail)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n))))))

  ;; An object far larger than the *page allocator's* scan window, which is a
  ;; different question from the one above and had a different answer.
  ;;
  ;; Granules are handed out first-fit from the bottom, so the free-bitmap scan
  ;; stops a little past the high-water mark -- nothing above it has ever been
  ;; handed out, so a run that does not fit below cannot fit just above.  "A
  ;; little past" was sixty-four granules whatever was asked for, so a run
  ;; reaching more than four megabytes past the mark was refused on a heap with
  ;; a gigabyte free, and the refusal came back as TRAP_HEAP_OOM.  Sixteen
  ;; megabytes is two hundred and fifty-six granules, so this traps for a full
  ;; heap on an empty one until the bound counts the request.
  ;;
  ;; It also asks the other thing a large object owes: it is never relocated,
  ;; so its address survives every cycle it does, and `ref.eq` against a copy
  ;; taken before the churn is what says so.
  (func (export "huge_object") (param $n i32)
    (local $i i32) (local $a (ref $words))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $a (array.new_default $words (i32.const 2000000)))
        (array.set $words (local.get $a) (i32.const 1999999) (i64.const 42))
        (global.set $keep2 (local.get $a))
        (call $churn (i32.const 64))
        (call $eq32 (ref.eq (local.get $a) (global.get $keep2)) (i32.const 1))
        (if (i64.ne (array.get $words (local.get $a) (i32.const 1999999))
                    (i64.const 42))
          (then (call $fail)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (global.set $keep2 (ref.null eq)))

  ;; An object with a v128 field, which is the one shape that needs a wide
  ;; header -- so its payload starts sixteen bytes in rather than eight, and
  ;; the collector has to take that from the type rather than assume it.
  (type $wide (struct (field $v (mut v128)) (field $r (mut (ref null $node)))))

  (func (export "wide_header") (param $n i32)
    (local $o (ref $wide))
    (local.set $o (struct.new $wide
      (v128.const i64x2 0x0f0e0d0c0b0a0908 0x0706050403020100)
      (struct.new $node (ref.null $node) (i32.const 4711))))
    (call $escape (local.get $o))
    (call $churn (local.get $n))
    (call $eq32 (i32x4.extract_lane 0 (struct.get $wide $v (local.get $o)))
                (i32.const 0x0b0a0908))
    (call $eq32 (struct.get $node $val
                  (ref.as_non_null (struct.get $wide $r (local.get $o))))
                (i32.const 4711)))

  ;; ---- allocations the runtime performs on compiled code's behalf --------

  (data $d8 "\01\02\03\04\05\06\07\08")
  (elem $es (ref null $node)
    (item (struct.new $node (ref.null $node) (i32.const 4711)))
    (item (ref.null $node)))
  ;; Made at instantiation, so a reference can exist before the first compiled
  ;; allocation of the run has ever written the collector's frame snapshot.
  (global $seed (mut (ref null $node))
    (struct.new $node (ref.null $node) (i32.const 4242)))

  ;; A reference live across `array.new_data`.  The helper allocates, so a
  ;; collection can begin inside it, and the only way the collector can see
  ;; $o is through this frame's safepoint map -- which it only reads if the
  ;; helper was entered through the trampoline that snapshots the frame.  The
  ;; first probe runs before any other compiled allocation on purpose: a
  ;; helper reusing a stale snapshot would walk from an address that is not
  ;; even code, and $o would simply never be seen.
  (func (export "keep_across_new_data") (param $n i32)
    (local $o (ref null $node)) (local $a (ref null $bytes)) (local $i i32)
    (local.set $o (global.get $seed))
    (global.set $seed (ref.null $node))
    (local.set $a (array.new_data $bytes $d8 (i32.const 0) (i32.const 8)))
    (call $eq32 (struct.get $node $val (ref.as_non_null (local.get $o)))
                (i32.const 4242))
    (call $eq32 (array.get_u $bytes (ref.as_non_null (local.get $a))
                             (i32.const 3))
                (i32.const 4))
    ;; And in earnest, with the pacing free to land a cycle anywhere.
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $o (struct.new $node (ref.null $node) (local.get $i)))
        (call $escape (local.get $o))
        (local.set $a (array.new_data $bytes $d8 (i32.const 0) (i32.const 8)))
        (call $eq32 (struct.get $node $val (ref.as_non_null (local.get $o)))
                    (local.get $i))
        (call $eq32 (array.get_u $bytes (ref.as_non_null (local.get $a))
                                 (i32.const 7))
                    (i32.const 8))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n))))))

  ;; The same, for `array.new_elem`.  The segment's items were evaluated once
  ;; at instantiation, so on top of $o surviving, both arrays have to come
  ;; back holding the same object the segment already holds.
  (func (export "keep_across_new_elem") (param $n i32)
    (local $o (ref null $node)) (local $a (ref null $refs)) (local $i i32)
    (local.set $o (global.get $seed))
    (global.set $seed (ref.null $node))
    (local.set $a (array.new_elem $refs $es (i32.const 0) (i32.const 2)))
    (call $eq32 (struct.get $node $val (ref.as_non_null (local.get $o)))
                (i32.const 4242))
    (call $eq32 (struct.get $node $val (ref.as_non_null
                  (array.get $refs (ref.as_non_null (local.get $a))
                             (i32.const 0))))
                (i32.const 4711))
    (if (i32.eqz (ref.is_null (array.get $refs (ref.as_non_null (local.get $a))
                                         (i32.const 1))))
      (then (call $fail)))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $o (struct.new $node (ref.null $node) (local.get $i)))
        (call $escape (local.get $o))
        (local.set $a (array.new_elem $refs $es (i32.const 0) (i32.const 2)))
        (call $eq32 (struct.get $node $val (ref.as_non_null (local.get $o)))
                    (local.get $i))
        (call $eq32 (struct.get $node $val (ref.as_non_null
                      (array.get $refs (ref.as_non_null (local.get $a))
                                 (i32.const 0))))
                    (i32.const 4711))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n))))))

  ;; ---- roots that are not the stack --------------------------------------
  ;;
  ;; Tables and element segments are scanned by the collector's own cursor, a
  ;; budget's worth per slice, and each of the shapes below takes an object
  ;; whose *only* home is a table slot, removes it while marking may be
  ;; anywhere in its crawl, and puts it back.  Two details make these tests
  ;; able to fail at all, and both were learned by breaking the barrier on
  ;; purpose and watching the suite stay green:
  ;;
  ;;   - the local that briefly holds the object is nulled before the next
  ;;     iteration, because a local that still holds it when a cycle starts is
  ;;     a frame root and the hide never happens;
  ;;   - the slot is near the end of a table big enough that the cursor takes
  ;;     many slices to reach it, because a table that fits in the first slice
  ;;     is scanned before the program can run at all.
  ;;
  ;; With those two in place, a removal the barrier fails to record is an
  ;; object the cycle never sees: the cursor finds the slot empty (or already
  ;; passed it), the frame was scanned while the local was null, and the
  ;; restore happens behind the cursor's back.

  (table $t2 8 (ref null $node))
  (table $tg 2 64 (ref null $node))
  (table $tbig 4096 (ref null $node))
  (elem $enull (ref null $node)
    (item (ref.null $node)) (item (ref.null $node))
    (item (ref.null $node)) (item (ref.null $node)))

  (func $hide_check (param $v i32)
    (call $eq32 (struct.get $node $val
                  (ref.as_non_null (table.get $tbig (i32.const 4090))))
                (local.get $v))
    (table.set $tbig (i32.const 4090) (ref.null $node)))

  (func (export "table_hide_restore") (param $n i32)
    (local $p (ref null $node)) (local $i i32)
    (table.set $tbig (i32.const 4090)
      (struct.new $node (ref.null $node) (i32.const 3131)))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $p (table.get $tbig (i32.const 4090)))
        (table.set $tbig (i32.const 4090) (ref.null $node))
        ;; The window and the closed phase are both wider than the cursor's
        ;; whole crawl (~20 slices to reach slot 4090), so a cycle starting
        ;; anywhere in the second half of the closed phase scans the slot
        ;; while it is empty and the local is no longer named by any map.
        (call $churn (i32.const 32))
        (table.set $tbig (i32.const 4090) (local.get $p))
        (local.set $p (ref.null $node))
        (call $churn (i32.const 32))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $hide_check (i32.const 3131)))

  ;; The same removal, but by `table.fill` -- carried out by the runtime, so
  ;; it is the runtime's own barrier that has to record the overwritten slots.
  (func (export "table_fill_hide") (param $n i32)
    (local $p (ref null $node)) (local $i i32)
    (table.set $tbig (i32.const 4090)
      (struct.new $node (ref.null $node) (i32.const 4646)))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $p (table.get $tbig (i32.const 4090)))
        (table.fill $tbig (i32.const 4086) (ref.null $node) (i32.const 8))
        ;; The window and the closed phase are both wider than the cursor's
        ;; whole crawl (~20 slices to reach slot 4090), so a cycle starting
        ;; anywhere in the second half of the closed phase scans the slot
        ;; while it is empty and the local is no longer named by any map.
        (call $churn (i32.const 32))
        (table.set $tbig (i32.const 4090) (local.get $p))
        (local.set $p (ref.null $node))
        (call $churn (i32.const 32))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $hide_check (i32.const 4646)))

  ;; By `table.copy`, from a table of nulls.
  (func (export "table_copy_hide") (param $n i32)
    (local $p (ref null $node)) (local $i i32)
    (table.set $tbig (i32.const 4090)
      (struct.new $node (ref.null $node) (i32.const 5959)))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $p (table.get $tbig (i32.const 4090)))
        (table.copy $tbig $t2 (i32.const 4086) (i32.const 0) (i32.const 8))
        ;; The window and the closed phase are both wider than the cursor's
        ;; whole crawl (~20 slices to reach slot 4090), so a cycle starting
        ;; anywhere in the second half of the closed phase scans the slot
        ;; while it is empty and the local is no longer named by any map.
        (call $churn (i32.const 32))
        (table.set $tbig (i32.const 4090) (local.get $p))
        (local.set $p (ref.null $node))
        (call $churn (i32.const 32))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $hide_check (i32.const 5959)))

  ;; By `table.init`, from a segment of nulls.  The segment is never dropped,
  ;; so it stays initialisable however many times the loop runs.
  (func (export "table_init_hide") (param $n i32)
    (local $p (ref null $node)) (local $i i32)
    (table.set $tbig (i32.const 4090)
      (struct.new $node (ref.null $node) (i32.const 6767)))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $p (table.get $tbig (i32.const 4090)))
        (table.init $tbig $enull (i32.const 4088) (i32.const 0) (i32.const 4))
        ;; The window and the closed phase are both wider than the cursor's
        ;; whole crawl (~20 slices to reach slot 4090), so a cycle starting
        ;; anywhere in the second half of the closed phase scans the slot
        ;; while it is empty and the local is no longer named by any map.
        (call $churn (i32.const 32))
        (table.set $tbig (i32.const 4090) (local.get $p))
        (local.set $p (ref.null $node))
        (call $churn (i32.const 32))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $hide_check (i32.const 6767)))

  ;; And by `global.set`, which is a plain store into the instance.
  (func (export "global_hide_restore") (param $n i32)
    (local $p (ref null $node)) (local $i i32)
    (global.set $keep (struct.new $node (ref.null $node) (i32.const 8484)))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $p (global.get $keep))
        (global.set $keep (ref.null $node))
        (global.set $sink (struct.new $node (ref.null $node) (i32.const 0)))
        (global.set $keep (local.get $p))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $eq32 (struct.get $node $val (ref.as_non_null (global.get $keep)))
                (i32.const 8484))
    (global.set $keep (ref.null $node)))

  ;; `table.grow` appends entries; nothing is overwritten, but the new slots
  ;; and the initialiser have to be as visible to the scan as the old ones.
  (func (export "table_grow_keeps") (param $n i32)
    (local $i i32) (local $sz i32)
    (table.set $tg (i32.const 0)
      (struct.new $node (ref.null $node) (i32.const 111)))
    (table.set $tg (i32.const 1)
      (struct.new $node (ref.null $node) (i32.const 222)))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $sz (table.size $tg))
        (if (i32.lt_u (local.get $sz) (i32.const 64))
          (then (drop (table.grow $tg
            (struct.new $node (ref.null $node)
                        (i32.add (i32.const 1000) (local.get $sz)))
            (i32.const 1)))))
        (call $churn (i32.const 2))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $eq32 (struct.get $node $val
                  (ref.as_non_null (table.get $tg (i32.const 0))))
                (i32.const 111))
    (call $eq32 (struct.get $node $val
                  (ref.as_non_null (table.get $tg (i32.const 1))))
                (i32.const 222))
    (if (i32.gt_u (table.size $tg) (i32.const 2))
      (then
        (call $eq32 (struct.get $node $val
                      (ref.as_non_null (table.get $tg (i32.const 2))))
                    (i32.const 1002))
        (call $eq32 (struct.get $node $val
                      (ref.as_non_null (table.get $tg
                        (i32.sub (table.size $tg) (i32.const 1)))))
                    (i32.add (i32.const 999) (table.size $tg))))))

  ;; A funcref table under the same churn.  A funcref cell is not a heap
  ;; object -- it is sixteen-byte aligned and otherwise indistinguishable --
  ;; so what this checks is that a scan or a barrier seeing one neither dies
  ;; nor keeps the wrong thing, and that the entries still call.
  (func (export "funcref_table_churn") (param $n i32)
    (local $i i32)
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (table.set $ft (i32.const 2)
          (table.get $ft (i32.and (local.get $i) (i32.const 1))))
        (global.set $sink (struct.new $node (ref.null $node) (local.get $i)))
        (call $eq32
          (call_indirect $ft (type $ii) (i32.const 5) (i32.const 2))
          (i32.add (i32.const 6) (i32.and (local.get $i) (i32.const 1))))
        (table.fill $ft (i32.const 3) (ref.func $two) (i32.const 1))
        (call $eq32
          (call_indirect $ft (type $ii) (i32.const 5) (i32.const 3))
          (i32.const 7))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n))))))

  ;; ---- the array forms of hiding -----------------------------------------
  ;;
  ;; The same discipline as the table cases, but the only home is an array
  ;; element and the removal is one of the runtime's range operations, each of
  ;; which owes the barrier every slot it overwrites.  The delay the table
  ;; cases get from a big table these get from depth: the array hangs off the
  ;; far end of a five-hundred-link chain, and each link is its own object
  ;; with its own budget charge, so the tracer reaches the array's elements
  ;; hundreds of slices into the cycle -- long after the frame scan, and long
  ;; after the loop below has started opening windows.

  (type $link (struct (field $lnext (mut (ref null $link)))
                      (field $larr  (mut (ref null $refs)))))

  (func $mkchain (param $len i32) (param $a (ref null $refs)) (result (ref $link))
    (local $h (ref $link)) (local $i i32)
    (local.set $h (struct.new $link (ref.null $link) (local.get $a)))
    (block $done
      (br_if $done (i32.lt_u (local.get $len) (i32.const 2)))
      (loop $l
        (local.set $h (struct.new $link (local.get $h) (ref.null $refs)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i)
                            (i32.sub (local.get $len) (i32.const 1))))))
    (local.get $h))

  (func $tailarr (param $h (ref null $link)) (result (ref $refs))
    (block $done
      (loop $l
        (br_if $done (ref.is_null
          (struct.get $link $lnext (ref.as_non_null (local.get $h)))))
        (local.set $h (struct.get $link $lnext (ref.as_non_null (local.get $h))))
        (br $l)))
    (ref.as_non_null (struct.get $link $larr (ref.as_non_null (local.get $h)))))

  (func (export "arr_copy_hide") (param $n i32)
    (local $c (ref $link)) (local $aa (ref null $refs)) (local $z (ref $refs))
    (local $p (ref null $node)) (local $i i32)
    (local.set $z (array.new_default $refs (i32.const 8)))
    (local.set $aa (array.new_default $refs (i32.const 8)))
    (array.set $refs (ref.as_non_null (local.get $aa)) (i32.const 5)
      (struct.new $node (ref.null $node) (i32.const 2468)))
    (local.set $c (call $mkchain (i32.const 512) (local.get $aa)))
    (local.set $aa (ref.null $refs))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $aa (call $tailarr (local.get $c)))
        (local.set $p (array.get $refs (ref.as_non_null (local.get $aa))
                                 (i32.const 5)))
        (array.copy $refs $refs (ref.as_non_null (local.get $aa)) (i32.const 0)
                    (local.get $z) (i32.const 0) (i32.const 8))
        (call $churn (i32.add (i32.const 1)
                             (i32.rem_u (local.get $i) (i32.const 3))))
        (array.set $refs (ref.as_non_null (local.get $aa)) (i32.const 5)
                   (local.get $p))
        (local.set $p (ref.null $node))
        (local.set $aa (ref.null $refs))
        (global.set $sink (struct.new $node (ref.null $node) (i32.const 0)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $eq32 (struct.get $node $val
                  (ref.as_non_null (array.get $refs (call $tailarr (local.get $c))
                                              (i32.const 5))))
                (i32.const 2468)))

  (func (export "arr_fill_hide") (param $n i32)
    (local $c (ref $link)) (local $aa (ref null $refs))
    (local $p (ref null $node)) (local $i i32)
    (local.set $aa (array.new_default $refs (i32.const 8)))
    (array.set $refs (ref.as_non_null (local.get $aa)) (i32.const 2)
      (struct.new $node (ref.null $node) (i32.const 1357)))
    (local.set $c (call $mkchain (i32.const 512) (local.get $aa)))
    (local.set $aa (ref.null $refs))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $aa (call $tailarr (local.get $c)))
        (local.set $p (array.get $refs (ref.as_non_null (local.get $aa))
                                 (i32.const 2)))
        (array.fill $refs (ref.as_non_null (local.get $aa)) (i32.const 0)
                    (ref.null $node) (i32.const 8))
        (call $churn (i32.add (i32.const 1)
                             (i32.rem_u (local.get $i) (i32.const 3))))
        (array.set $refs (ref.as_non_null (local.get $aa)) (i32.const 2)
                   (local.get $p))
        (local.set $p (ref.null $node))
        (local.set $aa (ref.null $refs))
        (global.set $sink (struct.new $node (ref.null $node) (i32.const 0)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $eq32 (struct.get $node $val
                  (ref.as_non_null (array.get $refs (call $tailarr (local.get $c))
                                              (i32.const 2))))
                (i32.const 1357)))

  (func (export "arr_init_elem_hide") (param $n i32)
    (local $c (ref $link)) (local $aa (ref null $refs))
    (local $p (ref null $node)) (local $i i32)
    (local.set $aa (array.new_default $refs (i32.const 8)))
    (array.set $refs (ref.as_non_null (local.get $aa)) (i32.const 1)
      (struct.new $node (ref.null $node) (i32.const 9753)))
    (local.set $c (call $mkchain (i32.const 512) (local.get $aa)))
    (local.set $aa (ref.null $refs))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $aa (call $tailarr (local.get $c)))
        (local.set $p (array.get $refs (ref.as_non_null (local.get $aa))
                                 (i32.const 1)))
        (array.init_elem $refs $enull (ref.as_non_null (local.get $aa))
                         (i32.const 0) (i32.const 0) (i32.const 4))
        (call $churn (i32.add (i32.const 1)
                             (i32.rem_u (local.get $i) (i32.const 3))))
        (array.set $refs (ref.as_non_null (local.get $aa)) (i32.const 1)
                   (local.get $p))
        (local.set $p (ref.null $node))
        (local.set $aa (ref.null $refs))
        (global.set $sink (struct.new $node (ref.null $node) (i32.const 0)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $eq32 (struct.get $node $val
                  (ref.as_non_null (array.get $refs (call $tailarr (local.get $c))
                                              (i32.const 1))))
                (i32.const 9753)))

  ;; Every terminator a versioned loop's body can end in, in one loop, because
  ;; pass_gc_barrier_version copies the body and has to reproduce all of them:
  ;; the back edge, two different exits, and a `br_table` that is both at once.
  ;; Nothing else versioned in this file has more than one way out, so this is
  ;; the only case that runs that copy at all; the answer is a number, compared
  ;; against the same arithmetic written without the reference store, which is
  ;; therefore not versioned.
  ;;
  ;; **It is coverage of a shape, not a discriminating test, and that was
  ;; measured rather than assumed**: with the copying deliberately broken --
  ;; operands left naming the original's values, edge arguments not remapped --
  ;; this case still passes, while `version_hide` and `loop_many_targets` fail.
  ;; The reason is that the two copies are structurally identical, so the
  ;; register allocator gives them the same assignment and reading the wrong
  ;; value reads the right register.  Do not treat a green run of this one as
  ;; evidence that the copying is right; treat it as evidence that a loop with
  ;; several ways out survives being copied.  (The churn between the calls is
  ;; still load-bearing: without it no mark is ever in progress and the second
  ;; copy is never entered.)
  (func $vshapes_bar (param $n i32) (result i32)
    (local $a (ref $refs)) (local $b (ref $node)) (local $i i32) (local $s i32)
    (local.set $a (array.new_default $refs (i32.const 8)))
    (local.set $b (struct.new $node (ref.null $node) (i32.const 3)))
    (block $late
      (block $early
        (loop $l
          (array.set $refs (local.get $a)
            (i32.and (local.get $i) (i32.const 7)) (local.get $b))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (local.set $s (i32.add (local.get $s) (local.get $i)))
          (br_if $early (i32.ge_u (local.get $i) (local.get $n)))
          ;; out, back, back -- the way out is the rare index, so the loop runs
          ;; a while before it takes it and both copies need all three edges.
          (br_table $late $l $l
            (i32.rem_u (local.get $s) (i32.const 97)))))
      (return (i32.add (local.get $s) (i32.const 1000))))
    (i32.add (local.get $s) (i32.const 2000)))

  (func $vshapes_plain (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (block $late
      (block $early
        (loop $l
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (local.set $s (i32.add (local.get $s) (local.get $i)))
          (br_if $early (i32.ge_u (local.get $i) (local.get $n)))
          (br_table $late $l $l
            (i32.rem_u (local.get $s) (i32.const 97)))))
      (return (i32.add (local.get $s) (i32.const 1000))))
    (i32.add (local.get $s) (i32.const 2000)))

  (func (export "version_shapes") (param $n i32)
    (local $k i32)
    (loop $l
      (call $churn (i32.add (i32.const 8)
                            (i32.rem_u (local.get $k) (i32.const 7))))
      (call $eq32 (call $vshapes_bar
                    (i32.add (i32.const 40)
                             (i32.rem_u (local.get $k) (i32.const 200))))
                  (call $vshapes_plain
                    (i32.add (i32.const 40)
                             (i32.rem_u (local.get $k) (i32.const 200)))))
      (local.set $k (i32.add (local.get $k) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $k) (local.get $n)))))

  ;; A loop the allocation cursor is cached across, building objects that stay
  ;; *live*, and enough of them to run out of the region many times over.
  ;;
  ;; This is the case for the flush the slow arm owes.  Inside a cached loop
  ;; the cell in memory does not move: the cursor is in a register, and
  ;; everything the loop has built since the last flush lies above what the
  ;; cell still says.  When an allocation finally runs out of room and calls
  ;; the helper, the helper reads the cell -- and if it has not been brought up
  ;; to date first, it hands that memory out for a second time, over objects
  ;; that are still reachable.
  ;;
  ;; Nothing else in this file sees it.  The loop has to be one the pass will
  ;; take -- no call, no trap, and no barriered store -- and it has to *keep*
  ;; what it allocates, and `churn` and its neighbours drop everything but the
  ;; last.  A chain does both: each node is born pointing at the one before, so
  ;; the store is an initialising one and owes no barrier, and the head in a
  ;; local is a frame root the collector finds.  Deleting the flush leaves the
  ;; chain shorter than it should be, or with a node in it twice.
  (func (export "lab_live_chain") (param $n i32) (result i32)
    (local $h (ref null $node)) (local $i i32) (local $s i32) (local $k i32)
    (loop $l
      (local.set $h (struct.new $node (local.get $h) (local.get $i)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (block $done
      (loop $w
        (br_if $done (ref.is_null (local.get $h)))
        (local.set $s (i32.add (local.get $s)
          (struct.get $node $val (ref.as_non_null (local.get $h)))))
        (local.set $h (struct.get $node $next (ref.as_non_null (local.get $h))))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br $w)))
    ;; Every node once, and every value exactly once: 0 + 1 + ... + (n-1).
    (call $eq32 (local.get $k) (local.get $n))
    (call $eq32 (local.get $s)
      (i32.div_u (i32.mul (local.get $n) (i32.sub (local.get $n) (i32.const 1)))
                 (i32.const 2)))
    (local.get $s))

  ;; The *first* copy of a versioned loop -- the one that carries no barrier
  ;; at all -- and the one thing that pays for it: the telling in the
  ;; preheader.  A loop with no safepoint in it cannot let the collector run,
  ;; so the remembered set can be told once, before the loop, that this old
  ;; array may now point at young objects.  Delete that telling and every
  ;; other case in the file passes, because `old_to_new` and its neighbours
  ;; put a `churn` inside the storing loop and a loop with a call in it is
  ;; never versioned.
  ;;
  ;; So: one old array, filled by a loop that allocates (not versioned, and
  ;; not the point), then a second loop with no allocation in it (versioned)
  ;; that moves those young nodes into the old array.  The young array and
  ;; every local that held them are cleared afterwards, so a minor that was
  ;; never told about the old array has nothing left pointing at the nodes and
  ;; frees them under it.
  (func (export "version_remember") (param $n i32)
    (local $h (ref $refs)) (local $y (ref $refs)) (local $i i32) (local $k i32)
    (local.set $h (array.new_default $refs (i32.const 64)))
    (call $escape (local.get $h))
    (call $churn (i32.const 2048))          ;; $h survives a cycle: it is old
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $y (array.new_default $refs (i32.const 64)))
        (local.set $i (i32.const 0))
        (loop $mk
          (array.set $refs (local.get $y) (local.get $i)
            (struct.new $node (ref.null $node)
                        (i32.add (i32.const 5100) (local.get $i))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br_if $mk (i32.lt_u (local.get $i) (i32.const 64))))
        (local.set $i (i32.const 0))
        ;; Versioned: no call, no allocation, both arrays from outside it.
        (loop $move
          (array.set $refs (local.get $h) (local.get $i)
            (array.get $refs (local.get $y) (local.get $i)))
          (array.set $refs (local.get $y) (local.get $i) (ref.null $node))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br_if $move (i32.lt_u (local.get $i) (i32.const 64))))
        (local.set $y (array.new_default $refs (i32.const 0)))
        (call $churn (i32.add (i32.const 2)
                              (i32.rem_u (local.get $k) (i32.const 5))))
        (local.set $i (i32.const 0))
        (loop $chk
          (call $eq32 (struct.get $node $val
                        (ref.as_non_null (array.get $refs (local.get $h)
                                                    (local.get $i))))
                      (i32.add (i32.const 5100) (local.get $i)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br_if $chk (i32.lt_u (local.get $i) (i32.const 64))))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $k) (local.get $n))))))

  ;; And what that second copy is allowed to *decide for itself*.
  ;;
  ;; A store during a mark asks the snapshot whether it wants the value being
  ;; overwritten, and the copy that runs during a mark asks it inline rather
  ;; than in the stub: null, an i31 or host reference, and -- when the mode
  ;; says the mark is a minor's and the filter is on -- a value already beyond
  ;; that minor's reach, which is what the age bit says.  Every one of those is
  ;; a value the drain would have thrown away, so getting one *wrong* is a
  ;; freed object and not a slow cycle.
  ;;
  ;; `version_hide` cannot see any of it: its eight nodes are made once and
  ;; moved back and forth for the whole run, so by the time it matters they are
  ;; old, and an age test that is inverted, missing, or asked during a major
  ;; keeps them anyway.  This is the same shape with the nodes made **fresh
  ;; every round**, which is the only state the filter can be wrong about: a
  ;; young value, overwritten while a minor marks, whose one other home is an
  ;; array a global root put in front of the mark long ago.
  ;;
  ;; **And it still does not discriminate, which is the honest label on it.**
  ;; What it does do is *produce the state*: with a counter in the snapshot
  ;; drain, this case puts thirty-two young values into minors' snapshots in a
  ;; 200-round run, where every other case in this file puts zero -- and it
  ;; only does that because of the churn length below.  Inverting the age test
  ;; is green against it anyway, and two explanations for that were measured
  ;; and refuted (escalation: the ablated build runs the same 80 minors and 3
  ;; majors; the slice budget: four settings, none catch it).  Removing the
  ;; test altogether *is* caught, in five configurations, by version_hide and
  ;; loop_many_targets.  Keep this case for the state it reaches, not for a
  ;; guarantee it gives; see the note in emit_gc_barrier_k.
  (func (export "version_satb") (param $n i32)
    (local $c (ref $link)) (local $aa (ref null $refs)) (local $sh (ref $refs))
    (local $i i32) (local $k i32)
    (local.set $aa (array.new_default $refs (i32.const 8)))
    (local.set $c (call $mkchain (i32.const 512) (local.get $aa)))
    (local.set $aa (ref.null $refs))
    (global.set $vshelf (array.new_default $refs (i32.const 8)))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $aa (call $tailarr (local.get $c)))
        (local.set $i (i32.const 0))
        (loop $mk
          (array.set $refs (ref.as_non_null (local.get $aa)) (local.get $i)
            (struct.new $node (ref.null $node)
                        (i32.add (i32.const 2400) (local.get $i))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br_if $mk (i32.lt_u (local.get $i) (i32.const 8))))
        ;; And now the window, which is the whole difficulty and was set by
        ;; counting rather than by argument.  A young value can only reach a
        ;; minor's snapshot if it was allocated while no mark was running and
        ;; overwritten while one was -- and under the slice budget that arms
        ;; the barrier at all, a mark is running nearly always, so nearly
        ;; everything is *born old* and the filter never has to decide.  The
        ;; churn here has to be long enough to cross a cycle's start, so that
        ;; the nodes above were made before it: at one to three turns the
        ;; runtime counts **zero** young values in any minor's snapshot for
        ;; the whole run, and at sixty-four and up it counts them in every
        ;; configuration that arms the barrier.  Varying the length as well,
        ;; because a fixed rhythm locks the phase.
        (call $churn (i32.add (i32.const 64)
                              (i32.rem_u (local.get $k) (i32.const 97))))
        (local.set $sh (ref.as_non_null (global.get $vshelf)))
        (local.set $i (i32.const 0))
        ;; Versioned.  The shelf is old and *nothing writes to it outside a
        ;; mark* -- which is why the check below only reads it, and why the
        ;; previous round's nodes are left in place to be overwritten here
        ;; rather than cleared afterwards.  A write outside a mark would put
        ;; the shelf in the remembered set, the next minor would scan it, and
        ;; the nodes would be found without the snapshot ever being asked;
        ;; that is what the first version of this case did, and it made every
        ;; ablation of the filter green.  The array being emptied is five
        ;; hundred links away and the mark has not reached it.  So the
        ;; snapshot entry for each young node is the only thing holding it.
        (loop $move
          (array.set $refs (local.get $sh) (local.get $i)
            (array.get $refs (ref.as_non_null (local.get $aa)) (local.get $i)))
          (array.set $refs (ref.as_non_null (local.get $aa)) (local.get $i)
            (ref.null $node))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br_if $move (i32.lt_u (local.get $i) (i32.const 8))))
        (local.set $aa (ref.null $refs))
        (call $churn (i32.add (i32.const 1)
                              (i32.rem_u (local.get $k) (i32.const 3))))
        (local.set $i (i32.const 0))
        (loop $chk
          (call $eq32 (struct.get $node $val
                        (ref.as_non_null (array.get $refs (local.get $sh)
                                                    (local.get $i))))
                      (i32.add (i32.const 2400) (local.get $i)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br_if $chk (i32.lt_u (local.get $i) (i32.const 8))))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $k) (local.get $n))))))

  ;; The same removal again, and this time by a loop the compiler writes
  ;; twice.  A loop with no safepoint in it cannot let the collector run, so
  ;; the barrier's mode is settled before it starts and pass_gc_barrier_version
  ;; branches once and gives each answer its own copy of the body: one with no
  ;; barrier at all, one that calls the stub without asking.  Nothing else in
  ;; this file tests the second copy -- delete its barrier and every other case
  ;; passes -- because every other loop that moves references around has a call
  ;; or an allocation in it and is therefore never versioned.
  ;;
  ;; What makes a lost snapshot entry *visible* is the same trick the cases
  ;; above use, plus one more.  The nodes live at the end of a 512-link chain,
  ;; so a mark reaches them late; `$vshelf` is a global, so a mark reaches it
  ;; at the very start of the cycle and never looks again.  The inner loop --
  ;; safepoint-free, both arrays loop-invariant, so it is versioned -- moves
  ;; every node from the late array to the early one and nulls the late slot.
  ;; A mark that was not told the old value then finds the late array empty
  ;; when it finally arrives, has long since finished with the shelf, and
  ;; frees eight nodes that are still reachable through it.
  (global $vshelf (mut (ref null $refs)) (ref.null $refs))

  (func (export "version_hide") (param $n i32)
    (local $c (ref $link)) (local $aa (ref null $refs)) (local $sh (ref $refs))
    (local $i i32) (local $k i32)
    (local.set $aa (array.new_default $refs (i32.const 8)))
    (local.set $i (i32.const 0))
    (loop $mk
      (array.set $refs (ref.as_non_null (local.get $aa)) (local.get $i)
        (struct.new $node (ref.null $node)
                    (i32.add (i32.const 2400) (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $mk (i32.lt_u (local.get $i) (i32.const 8))))
    (local.set $c (call $mkchain (i32.const 512) (local.get $aa)))
    (local.set $aa (ref.null $refs))
    (global.set $vshelf (array.new_default $refs (i32.const 8)))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $aa (call $tailarr (local.get $c)))
        (local.set $sh (ref.as_non_null (global.get $vshelf)))
        (local.set $i (i32.const 0))
        ;; Versioned: no call, no allocation, and both arrays come from
        ;; outside.  Each turn moves one node out of reach of a mark that has
        ;; not arrived yet and into one it has already passed.
        (loop $move
          (array.set $refs (local.get $sh) (local.get $i)
            (array.get $refs (ref.as_non_null (local.get $aa)) (local.get $i)))
          (array.set $refs (ref.as_non_null (local.get $aa)) (local.get $i)
            (ref.null $node))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br_if $move (i32.lt_u (local.get $i) (i32.const 8))))
        (call $churn (i32.add (i32.const 1)
                             (i32.rem_u (local.get $k) (i32.const 3))))
        (local.set $i (i32.const 0))
        (loop $back
          (array.set $refs (ref.as_non_null (local.get $aa)) (local.get $i)
            (array.get $refs (local.get $sh) (local.get $i)))
          (array.set $refs (local.get $sh) (local.get $i) (ref.null $node))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br_if $back (i32.lt_u (local.get $i) (i32.const 8))))
        (local.set $aa (ref.null $refs))
        (global.set $sink (struct.new $node (ref.null $node) (i32.const 0)))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $k) (local.get $n)))))
    (local.set $aa (call $tailarr (local.get $c)))
    (local.set $i (i32.const 0))
    (loop $chk
      (call $eq32 (struct.get $node $val
                    (ref.as_non_null (array.get $refs
                      (ref.as_non_null (local.get $aa)) (local.get $i))))
                  (i32.add (i32.const 2400) (local.get $i)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $chk (i32.lt_u (local.get $i) (i32.const 8))))
    (global.set $vshelf (ref.null $refs)))

  ;; ---- large arrays, marked in chunks ------------------------------------
  ;;
  ;; The tracer stops scanning an array of references every 4096 elements and
  ;; pushes a continuation, so lengths one either side of that line are where
  ;; an off-by-one loses the last element or scans one past the end.

  (func $fill_same (param $a (ref $refs)) (param $v (ref null $node))
    (array.fill $refs (local.get $a) (i32.const 0) (local.get $v)
                (array.len (local.get $a))))

  (func $sum_vals (param $a (ref $refs)) (result i32)
    (local $i i32) (local $s i32)
    (block $done
      (br_if $done (i32.eqz (array.len (local.get $a))))
      (loop $l
        (local.set $s (i32.add (local.get $s)
          (struct.get $node $val
            (ref.as_non_null (array.get $refs (local.get $a) (local.get $i))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (array.len (local.get $a))))))
    (local.get $s))

  (func $chunk_one (param $len i32) (param $n i32)
    (local $a (ref $refs))
    (local.set $a (array.new_default $refs (local.get $len)))
    (call $fill_same (local.get $a)
      (struct.new $node (ref.null $node) (i32.const 3)))
    ;; Distinct objects astride the chunk line, so the continuation's half
    ;; carries different references from the first half's.  4093 rather than
    ;; 4094 so the two probes stay distinct at every length used below.
    (array.set $refs (local.get $a) (i32.const 4093)
      (struct.new $node (ref.null $node) (i32.const 100)))
    (array.set $refs (local.get $a)
      (i32.sub (local.get $len) (i32.const 1))
      (struct.new $node (ref.null $node) (i32.const 200)))
    (call $churn (local.get $n))
    ;; Everything holds 3 except the two probes: 3*(len-2) + 300.
    (call $eq32 (call $sum_vals (local.get $a))
      (i32.add (i32.mul (i32.sub (local.get $len) (i32.const 2)) (i32.const 3))
               (i32.const 300))))

  (func (export "chunk_edges") (param $n i32)
    (call $chunk_one (i32.const 4095) (local.get $n))
    (call $chunk_one (i32.const 4096) (local.get $n))
    (call $chunk_one (i32.const 4097) (local.get $n)))

  ;; An object moved from a chunk the scan has already finished into one it
  ;; has not (and back), while cycles are running: the classic way to lose one
  ;; to an incremental trace, and exactly what the deletion barrier must
  ;; catch when the high slot is nulled.
  (func (export "chunk_mutate") (param $n i32)
    (local $a (ref $refs)) (local $p (ref null $node)) (local $i i32)
    (local.set $a (array.new_default $refs (i32.const 12288)))
    (array.set $refs (local.get $a) (i32.const 12000)
      (struct.new $node (ref.null $node) (i32.const 7777)))
    (array.set $refs (local.get $a) (i32.const 8191)
      (struct.new $node (ref.null $node) (i32.const 8888)))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $p (array.get $refs (local.get $a) (i32.const 12000)))
        (array.set $refs (local.get $a) (i32.const 12000) (ref.null $node))
        (array.set $refs (local.get $a) (i32.const 3) (local.get $p))
        (global.set $sink (struct.new $node (ref.null $node) (i32.const 0)))
        (array.set $refs (local.get $a) (i32.const 3) (ref.null $node))
        (array.set $refs (local.get $a) (i32.const 12000) (local.get $p))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $eq32 (struct.get $node $val
                  (ref.as_non_null (array.get $refs (local.get $a) (i32.const 12000))))
                (i32.const 7777))
    (call $eq32 (struct.get $node $val
                  (ref.as_non_null (array.get $refs (local.get $a) (i32.const 8191))))
                (i32.const 8888))
    (if (i32.eqz (ref.is_null (array.get $refs (local.get $a) (i32.const 3))))
      (then (call $fail))))

  ;; ---- stacks, deep and replaced -----------------------------------------

  ;; Two references per frame, eight hundred frames down, and the churn at the
  ;; bottom: the walk has to read eight hundred maps and correct the register
  ;; snapshot through every prologue on the way.
  (func $deep2 (param $d i32) (param $n i32) (result i32)
    (local $o (ref $node)) (local $q (ref $node))
    (local.set $o (struct.new $node (ref.null $node) (local.get $d)))
    (call $escape (local.get $o))
    (local.set $q (struct.new $node (ref.null $node)
                              (i32.add (local.get $d) (i32.const 100000))))
    (call $escape (local.get $q))
    (if (i32.eqz (local.get $d))
      (then (call $churn (local.get $n)))
      (else (call $eq32 (call $deep2 (i32.sub (local.get $d) (i32.const 1))
                                     (local.get $n))
                        (i32.sub (local.get $d) (i32.const 1)))))
    (call $eq32 (struct.get $node $val (local.get $q))
                (i32.add (local.get $d) (i32.const 100000)))
    (struct.get $node $val (local.get $o)))

  (func (export "deep_frames") (param $n i32)
    (call $eq32 (call $deep2 (i32.const 800) (local.get $n)) (i32.const 800)))

  ;; A reference carried through `return_call`.  The frame it was live in is
  ;; gone by the time the callee allocates, so the value's only home is the
  ;; argument register or slot of the frame that replaced it -- the callee's
  ;; own map is what names it, and the chain it heads grows one node per hop.
  (func $thop (param $p (ref null $node)) (param $d i32) (result i32)
    (local $o (ref $node))
    (local.set $o (struct.new $node (local.get $p) (local.get $d)))
    (if (i32.eqz (local.get $d))
      (then
        ;; The head holds 0 and its predecessor 1, when there was one.
        (if (i32.eqz (ref.is_null (struct.get $node $next (local.get $o))))
          (then (call $eq32
            (struct.get $node $val
              (ref.as_non_null (struct.get $node $next (local.get $o))))
            (i32.const 1))))
        (return (struct.get $node $val (local.get $o))))
      (else (return_call $thop (local.get $o)
                               (i32.sub (local.get $d) (i32.const 1)))))
    (unreachable))

  (func (export "tail_ref_chain") (param $n i32)
    (call $eq32 (call $thop (ref.null $node) (local.get $n)) (i32.const 0)))

  ;; ---- shapes the tracer walks field by field ----------------------------

  ;; Twelve reference fields in one object, every one of which the map of the
  ;; type has to name and the trace has to follow.
  (type $fat (struct
    (field $r0 (mut (ref null $node))) (field $r1 (mut (ref null $node)))
    (field $r2 (mut (ref null $node))) (field $r3 (mut (ref null $node)))
    (field $r4 (mut (ref null $node))) (field $r5 (mut (ref null $node)))
    (field $k  (mut i64))
    (field $r6 (mut (ref null $node))) (field $r7 (mut (ref null $node)))
    (field $r8 (mut (ref null $node))) (field $r9 (mut (ref null $node)))
    (field $ra (mut (ref null $node))) (field $rb (mut (ref null $node)))))

  (func $mk (param $v i32) (result (ref $node))
    (struct.new $node (ref.null $node) (local.get $v)))

  (func (export "many_ref_fields") (param $n i32)
    (local $o (ref $fat))
    (local.set $o (struct.new $fat
      (call $mk (i32.const 200)) (call $mk (i32.const 201))
      (call $mk (i32.const 202)) (call $mk (i32.const 203))
      (call $mk (i32.const 204)) (call $mk (i32.const 205))
      (i64.const -1)
      (call $mk (i32.const 206)) (call $mk (i32.const 207))
      (call $mk (i32.const 208)) (call $mk (i32.const 209))
      (call $mk (i32.const 210)) (call $mk (i32.const 211))))
    (call $escape (local.get $o))
    (call $churn (local.get $n))
    ;; Take one out mid-run and put it back, so the store path runs on a type
    ;; whose reference offsets skip an i64 in the middle.
    (struct.set $fat $r5 (local.get $o) (ref.null $node))
    (call $churn (local.get $n))
    (struct.set $fat $r5 (local.get $o) (call $mk (i32.const 205)))
    (call $churn (local.get $n))
    (call $eq32 (struct.get $node $val (ref.as_non_null (struct.get $fat $r0 (local.get $o)))) (i32.const 200))
    (call $eq32 (struct.get $node $val (ref.as_non_null (struct.get $fat $r1 (local.get $o)))) (i32.const 201))
    (call $eq32 (struct.get $node $val (ref.as_non_null (struct.get $fat $r2 (local.get $o)))) (i32.const 202))
    (call $eq32 (struct.get $node $val (ref.as_non_null (struct.get $fat $r3 (local.get $o)))) (i32.const 203))
    (call $eq32 (struct.get $node $val (ref.as_non_null (struct.get $fat $r4 (local.get $o)))) (i32.const 204))
    (call $eq32 (struct.get $node $val (ref.as_non_null (struct.get $fat $r5 (local.get $o)))) (i32.const 205))
    (if (i64.ne (struct.get $fat $k (local.get $o)) (i64.const -1)) (then (call $fail)))
    (call $eq32 (struct.get $node $val (ref.as_non_null (struct.get $fat $r6 (local.get $o)))) (i32.const 206))
    (call $eq32 (struct.get $node $val (ref.as_non_null (struct.get $fat $r7 (local.get $o)))) (i32.const 207))
    (call $eq32 (struct.get $node $val (ref.as_non_null (struct.get $fat $r8 (local.get $o)))) (i32.const 208))
    (call $eq32 (struct.get $node $val (ref.as_non_null (struct.get $fat $r9 (local.get $o)))) (i32.const 209))
    (call $eq32 (struct.get $node $val (ref.as_non_null (struct.get $fat $ra (local.get $o)))) (i32.const 210))
    (call $eq32 (struct.get $node $val (ref.as_non_null (struct.get $fat $rb (local.get $o)))) (i32.const 211)))

  ;; Zero-length arrays are the smallest block the heap can hold; thousands of
  ;; them interleaved with drops make the free lists' smallest class earn its
  ;; keep.  Slot k of the keeper deliberately holds length k&1.
  (func (export "zero_len_churn") (param $n i32)
    (local $i i32) (local $k i32) (local $ks (ref $arrs))
    (local.set $ks (array.new_default $arrs (i32.const 8)))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (array.set $arrs (local.get $ks) (i32.and (local.get $i) (i32.const 7))
          (array.new_default $refs (i32.and (local.get $i) (i32.const 1))))
        (global.set $sink (array.new_default $bytes (i32.const 0)))
        (global.set $sink (array.new_default $words (i32.const 0)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (if (i32.lt_u (local.get $n) (i32.const 16)) (then (return)))
    (loop $c
      (call $eq32 (array.len (ref.as_non_null
                    (array.get $arrs (local.get $ks) (local.get $k))))
                  (i32.and (local.get $k) (i32.const 1)))
      (local.set $k (i32.add (local.get $k) (i32.const 1)))
      (br_if $c (i32.lt_u (local.get $k) (i32.const 8)))))

  ;; An exception object stored into a struct field, collected around, taken
  ;; back out and thrown again.  The exnref store is a reference store like
  ;; any other and owes the same barrier.
  (type $ebox (struct (field $ex (mut exnref))))

  (func (export "exn_field_rethrow") (param $n i32)
    (local $b (ref $ebox)) (local $i i32)
    (local $o (ref null $node)) (local $e (ref null exn))
    (local.set $b (struct.new $ebox (ref.null exn)))
    (call $escape (local.get $b))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (block $h (result (ref $node) exnref)
          (try_table (catch_ref $boxed $h) (call $raise (local.get $i)))
          (call $fail)
          (return))
        (local.set $e)
        (drop)
        (struct.set $ebox $ex (local.get $b) (local.get $e))
        (global.set $sink (struct.new $node (ref.null $node) (i32.const 0)))
        (block $h2 (result (ref $node))
          (try_table (catch $boxed $h2)
            (throw_ref (struct.get $ebox $ex (local.get $b))))
          (call $fail)
          (return))
        (local.set $o)
        (call $eq32 (struct.get $node $val (ref.as_non_null (local.get $o)))
                    (local.get $i))
        (struct.set $ebox $ex (local.get $b) (ref.null exn))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n))))))

  ;; Out through externref and back, with the external view living in a
  ;; global across the churn.  The internalised result has to be the same
  ;; object, and an i31 has to make the trip unboxed.
  (global $keepx (mut externref) (ref.null extern))

  (func (export "extern_roundtrip") (param $n i32)
    (local $i i32) (local $o (ref null $node))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (global.set $keepx (extern.convert_any
          (struct.new $node (ref.null $node) (local.get $i))))
        (global.set $sink (struct.new $node (ref.null $node) (i32.const 0)))
        (local.set $o (ref.cast (ref $node)
          (any.convert_extern (global.get $keepx))))
        (call $eq32 (struct.get $node $val (ref.as_non_null (local.get $o)))
                    (local.get $i))
        (global.set $keepx (extern.convert_any (ref.i31 (local.get $i))))
        (global.set $sink (struct.new $node (ref.null $node) (i32.const 0)))
        (call $eq32 (i31.get_u (ref.cast (ref i31)
                      (any.convert_extern (global.get $keepx))))
                    (i32.and (local.get $i) (i32.const 0x7fffffff)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (global.set $keepx (ref.null extern)))

  ;; The inline allocation fast path and the lazy sweep, active at once.  A
  ;; live set a couple of megabytes wide makes the sweep span dozens of
  ;; slow-path entries, and the loop mixes allocations the fast path serves
  ;; (structs) with ones it cannot (dynamic-length arrays), so an open bump
  ;; region and the sweep's walk coexist for most of every cycle.  A region
  ;; that straddled the sweep's start -- the bug this case exists for -- has
  ;; its unused tail misread as dead objects and freed under the fast path's
  ;; feet, which the heap check turns into a named failure.
  (func (export "mixed_during_sweep") (param $n i32)
    (local $keep (ref $arrs)) (local $i i32)
    (local.set $keep (array.new_default $arrs (i32.const 64)))
    (loop $fill
      (array.set $arrs (local.get $keep) (local.get $i)
        (array.new_default $refs (i32.const 4000)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $fill (i32.lt_u (local.get $i) (i32.const 64))))
    (local.set $i (i32.const 0))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (global.set $sink (struct.new $node (ref.null $node) (local.get $i)))
        (global.set $sink (struct.new $node (ref.null $node) (local.get $i)))
        (global.set $sink (array.new_default $refs (i32.and (local.get $i) (i32.const 15))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $eq32 (array.len (ref.as_non_null
                  (array.get $arrs (local.get $keep) (i32.const 63))))
                (i32.const 4000)))

  ;; ---- generations -------------------------------------------------------
  ;;
  ;; The remembered set is the one new obligation generational collection
  ;; added: a store of a young reference into an *old* object, made while no
  ;; mark is running, must put the old object where the next minor will
  ;; re-scan it -- or the young object is swept out from under a perfectly
  ;; reachable field.  The first churn below is what makes the container
  ;; old: it outlives at least one whole cycle before the loop starts.

  (func (export "old_to_new") (param $n i32)
    (local $o (ref $node)) (local $i i32)
    (local.set $o (struct.new $node (ref.null $node) (i32.const 7777)))
    (call $escape (local.get $o))
    (call $churn (i32.const 2048))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        ;; The young object's only home is the old object's field.
        (struct.set $node $next (local.get $o)
          (struct.new $node (ref.null $node) (local.get $i)))
        (call $churn (i32.add (i32.const 2)
                              (i32.rem_u (local.get $i) (i32.const 5))))
        (call $eq32 (struct.get $node $val
                      (ref.as_non_null (struct.get $node $next (local.get $o))))
                    (local.get $i))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $eq32 (struct.get $node $val (local.get $o)) (i32.const 7777)))

  ;; The same debt, paid by the runtime's own barrier: the range operations
  ;; overwrite elements of an *old* array with young references.
  (func (export "old_to_new_arrayops") (param $n i32)
    (local $a (ref $refs)) (local $b (ref $refs)) (local $i i32)
    (local.set $a (array.new_default $refs (i32.const 8)))
    (local.set $b (array.new_default $refs (i32.const 8)))
    (call $escape (local.get $a))
    (call $escape (local.get $b))
    (call $churn (i32.const 2048))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        ;; fill writes young into old $b; copy carries it into old $a.
        (array.fill $refs (local.get $b) (i32.const 0)
          (struct.new $node (ref.null $node) (local.get $i)) (i32.const 8))
        (array.copy $refs $refs (local.get $a) (i32.const 0)
                    (local.get $b) (i32.const 0) (i32.const 4))
        (call $churn (i32.add (i32.const 2)
                              (i32.rem_u (local.get $i) (i32.const 5))))
        (call $eq32 (struct.get $node $val
                      (ref.as_non_null (array.get $refs (local.get $a) (i32.const 3))))
                    (local.get $i))
        (call $eq32 (struct.get $node $val
                      (ref.as_non_null (array.get $refs (local.get $b) (i32.const 7))))
                    (local.get $i))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $eq32 (array.len (local.get $a)) (i32.const 8)))

  ;; ---- the mark's write-back, against the program's stores ---------------
  ;;
  ;; The concurrent mark does not only read a reference field, it *writes the
  ;; healed reference back* -- that is what compaction costs a tracer, and
  ;; without it a slot in an already-visited object would still name from-space
  ;; and the forwarding table could never retire.
  ;;
  ;; So on the collector's thread there is a load, a heal, and a store into a
  ;; field the *program is also storing to*.  The write-back must therefore be
  ;; a compare-exchange that only replaces the exact value it read.  A plain
  ;; store puts back a reference the program has already replaced: a lost
  ;; update, in the program's own object, with no trap and no corrupt heap --
  ;; the field simply goes backwards in time.
  ;;
  ;; This was a real bug, found 2026-08-12 and fixed in `gc_mark_slot`.  It hid
  ;; for so long because the only case that reached it was `old_to_new` above,
  ;; which stores *one* field per iteration and so failed about one run in
  ;; fourteen -- rare enough to read as a flake, and suppressed five-fold by
  ;; machine load, so every sweep built to find races missed it.  The three
  ;; cases here are the same defect with the window opened as wide as it goes:
  ;; hundreds of slots overwritten per turn, so a mark that clobbers even one
  ;; is caught.
  ;;
  ;; Every case stamps each slot with a value that only ever goes up and reads
  ;; every slot back.  A clobber replaces a stamp with an older one, which is
  ;; the exact signature of the bug and is checked for directly rather than
  ;; being left to show up as a fault.  They must pass with the collector
  ;; concurrent, stop-the-world, moving and not.

  ;; A big array of references, every element replaced each turn.  The marker
  ;; walks it slot by slot in one `gc_scan_object`, which is the widest window
  ;; the collector ever has, and the program is writing into the same slots.
  (func (export "mark_writeback_array") (param $n i32)
    (local $a (ref $refs)) (local $i i32) (local $k i32)
    (local.set $a (array.new_default $refs (i32.const 512)))
    (call $escape (local.get $a))
    ;; Old, and with a cycle already turning over.
    (call $churn (i32.const 2048))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $turn
        (local.set $k (i32.const 0))
        (loop $fill
          (array.set $refs (local.get $a) (local.get $k)
            (struct.new $node (ref.null $node) (local.get $i)))
          (local.set $k (i32.add (local.get $k) (i32.const 1)))
          (br_if $fill (i32.lt_u (local.get $k) (i32.const 512))))
        ;; Every slot must hold *this* turn's stamp.  An older one is the
        ;; write-back having put a reference back after the program replaced it.
        (local.set $k (i32.const 0))
        (loop $check
          (call $eq32 (struct.get $node $val (ref.as_non_null
                        (array.get $refs (local.get $a) (local.get $k))))
                      (local.get $i))
          (local.set $k (i32.add (local.get $k) (i32.const 1)))
          (br_if $check (i32.lt_u (local.get $k) (i32.const 512))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $turn (i32.lt_u (local.get $i) (local.get $n))))))

  ;; The same against *struct* fields rather than array elements, which is a
  ;; different loop in `gc_scan_object` -- it walks `refoffs` -- and the shape
  ;; `old_to_new` has, with many objects instead of one so a single clobbered
  ;; field is enough.
  (func (export "mark_writeback_struct") (param $n i32)
    (local $a (ref $refs)) (local $i i32) (local $k i32)
    (local.set $a (array.new_default $refs (i32.const 256)))
    ;; 256 old two-field nodes, held through the array so they stay reachable.
    (local.set $k (i32.const 0))
    (loop $mk
      (array.set $refs (local.get $a) (local.get $k)
        (struct.new $node (ref.null $node) (i32.const 0)))
      (local.set $k (i32.add (local.get $k) (i32.const 1)))
      (br_if $mk (i32.lt_u (local.get $k) (i32.const 256))))
    (call $escape (local.get $a))
    (call $churn (i32.const 2048))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $turn
        (local.set $k (i32.const 0))
        (loop $fill
          (struct.set $node $next
            (ref.as_non_null (array.get $refs (local.get $a) (local.get $k)))
            (struct.new $node (ref.null $node) (local.get $i)))
          (local.set $k (i32.add (local.get $k) (i32.const 1)))
          (br_if $fill (i32.lt_u (local.get $k) (i32.const 256))))
        (local.set $k (i32.const 0))
        (loop $check
          (call $eq32 (struct.get $node $val (ref.as_non_null (struct.get $node $next
                        (ref.as_non_null (array.get $refs (local.get $a) (local.get $k))))))
                      (local.get $i))
          (local.set $k (i32.add (local.get $k) (i32.const 1)))
          (br_if $check (i32.lt_u (local.get $k) (i32.const 256))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $turn (i32.lt_u (local.get $i) (local.get $n))))))

  ;; And a *table*, which is the one slot the program can write with no barrier
  ;; at all.  This is the case the compare-exchange was originally written for,
  ;; and it had no test of its own -- the argument lived only in a comment.
  (func (export "mark_writeback_table") (param $n i32)
    (local $i i32) (local $k i32)
    (local.set $k (i32.const 0))
    (loop $mk
      (table.set $t (local.get $k) (struct.new $node (ref.null $node) (i32.const 0)))
      (local.set $k (i32.add (local.get $k) (i32.const 1)))
      (br_if $mk (i32.lt_u (local.get $k) (i32.const 8))))
    (call $churn (i32.const 2048))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $turn
        (local.set $k (i32.const 0))
        (loop $fill
          (table.set $wb (local.get $k)
            (struct.new $node (ref.null $node) (local.get $i)))
          (local.set $k (i32.add (local.get $k) (i32.const 1)))
          (br_if $fill (i32.lt_u (local.get $k) (i32.const 256))))
        (local.set $k (i32.const 0))
        (loop $check
          (call $eq32 (struct.get $node $val
                        (ref.as_non_null (table.get $wb (local.get $k))))
                      (local.get $i))
          (local.set $k (i32.add (local.get $k) (i32.const 1)))
          (br_if $check (i32.lt_u (local.get $k) (i32.const 256))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $turn (i32.lt_u (local.get $i) (local.get $n))))))

  ;; The remembered set's other extreme, and the one it is *bad* at: not one
  ;; object written to a great many times, but one enormous object written to
  ;; a few times per cycle.  A remembered entry is an object and a minor scans
  ;; it whole, so every cycle re-reads the entire array to find the handful of
  ;; young references put into it since the last one -- work quadratic in the
  ;; array, for a program doing nothing unusual.  `gcbench/press` is the same
  ;; shape at scale: eighteen gigabytes of tracing for a gigabyte of
  ;; allocation, and fourteen times faster with minors switched off.
  ;;
  ;; The collector's answer is `gc_minor_review`'s second rule, and this is the
  ;; case it is measured on: the array is far larger than the nursery, so the
  ;; footprint the remembered set commits each minor to is many times what that
  ;; minor exists to collect, and minors go off.  As a *correctness* case it is
  ;; the ordinary one -- every element written is still there at the end -- and
  ;; that half is what the switch must not break.
  (func (export "remset_wide") (param $n i32)
    (local $a (ref $refs)) (local $i i32) (local $s i32)
    (local.set $a (array.new_default $refs (i32.const 40000)))
    (call $escape (local.get $a))
    ;; Old before the loop starts: the store barrier only owes anything for an
    ;; object that has already survived a cycle.
    (call $churn (i32.const 2048))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        ;; One slot per turn, in order, which is how a program fills a vector.
        (array.set $refs (local.get $a)
          (i32.rem_u (local.get $i) (i32.const 40000))
          (struct.new $node (ref.null $node) (i32.const 1)))
        (call $churn (i32.const 8))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    ;; Every one of them still there, and still saying what it said.
    (local.set $i (i32.const 0))
    (block $vdone
      (br_if $vdone (i32.eqz (local.get $n)))
      (loop $v
        (local.set $s (i32.add (local.get $s)
          (struct.get $node $val
            (ref.as_non_null (array.get $refs (local.get $a)
              (i32.rem_u (local.get $i) (i32.const 40000)))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $v (i32.lt_u (local.get $i) (local.get $n)))))
    (call $eq32 (local.get $s) (local.get $n)))

  ;; One old object stored into over and over: the header bit keeps it in
  ;; the set once per cycle, so the buffer cannot flood however hot the loop.
  (func (export "remset_dedupe") (param $n i32)
    (local $o (ref $node)) (local $i i32)
    (local.set $o (struct.new $node (ref.null $node) (i32.const 4321)))
    (call $escape (local.get $o))
    (call $churn (i32.const 2048))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (struct.set $node $next (local.get $o)
          (struct.new $node (ref.null $node) (local.get $i)))
        (struct.set $node $next (local.get $o)
          (struct.new $node (ref.null $node) (local.get $i)))
        (struct.set $node $next (local.get $o)
          (struct.new $node (ref.null $node) (local.get $i)))
        (drop (struct.new $node (ref.null $node) (i32.const 0)))
        (call $eq32 (struct.get $node $val
                      (ref.as_non_null (struct.get $node $next (local.get $o))))
                    (local.get $i))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $eq32 (struct.get $node $val (local.get $o)) (i32.const 4321)))

  ;; A chain that grows at the young end while its tail ages through minors
  ;; and majors both: every append is a young node pointing at an old one --
  ;; the direction that needs no barrier -- and the sum walks every
  ;; generation the chain has lived through.
  (func (export "promote_chain") (param $n i32)
    (local $h (ref null $node)) (local $i i32)
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $h (struct.new $node (local.get $h) (local.get $i)))
        (call $churn (i32.add (i32.const 1)
                              (i32.rem_u (local.get $i) (i32.const 4))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $eq32 (call $chain_sum (local.get $h))
      (i32.wrap_i64 (i64.div_u
        (i64.mul (i64.extend_i32_u (local.get $n))
                 (i64.extend_i32_u (i32.sub (local.get $n) (i32.const 1))))
        (i64.const 2)))))

  ;; Escape analysis and the collector in one loop: the first box is
  ;; scalar-replaced and never allocated, the second escapes through the sink
  ;; and is real garbage, and the sums have to come out right whatever the
  ;; collector is doing between slices.
  (func (export "ea_mixed_live") (param $n i32)
    (local $i i32) (local $s i32)
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $s (i32.add (local.get $s)
          (struct.get $node $val
            (struct.new $node (ref.null $node) (local.get $i)))))
        (global.set $sink (struct.new $node (ref.null $node) (i32.const 0)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $eq32 (local.get $s)
      (i32.wrap_i64 (i64.div_u
        (i64.mul (i64.extend_i32_u (local.get $n))
                 (i64.extend_i32_u (i32.sub (local.get $n) (i32.const 1))))
        (i64.const 2)))))

  ;; Memory and table growth while cycles run: `grow` republishes bases and
  ;; lengths into every instance, and none of that may disturb -- or be
  ;; disturbed by -- a collection in flight.
  (memory $m 1 4)

  (func (export "grow_during_cycle") (param $n i32)
    (local $i i32)
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (call $churn (i32.const 2))
        (if (i32.eq (i32.and (local.get $i) (i32.const 1023)) (i32.const 7))
          (then (drop (memory.grow $m (i32.const 1)))))
        (i32.store $m (i32.and (i32.mul (local.get $i) (i32.const 4))
                               (i32.const 0xfff))
                      (local.get $i))
        (call $eq32 (i32.load $m (i32.and (i32.mul (local.get $i) (i32.const 4))
                                          (i32.const 0xfff)))
                    (local.get $i))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n))))))

  ;; Vectors held live across a reference store.
  ;;
  ;; A reference store can call out of compiled code twice: the barrier stub,
  ;; which preserves everything by hand, and -- when the stub finds the
  ;; snapshot log's chunk full -- a C function, through the save-everything
  ;; wrapper in rt/start.S.  The register allocator knows about neither, so it
  ;; is entitled to leave a v128 in a q register across the store, and AAPCS64
  ;; preserves only the *low half* of v8-v15.  If the wrapper saved what a C
  ;; ABI says it must rather than what a wasm value needs, the top half of a
  ;; vector would come back as whatever the swap left there.
  ;;
  ;; Eight of them so the allocator has to use a spread of registers rather
  ;; than one, and each is read after the loop so none can be rematerialised.
  ;; Nothing else in the suite holds a vector across a store, and a chunk swap
  ;; is rare unless the chunks are small -- so this case only means anything in
  ;; the satb-chunks configuration, which is where it is aimed.
  ;;
  ;; What this case does NOT do is demonstrate the hazard, and that is worth
  ;; stating where someone can read it rather than leaving the comment above to
  ;; imply otherwise.  Replace every `ldp q..` in the wrapper's restore with a
  ;; `movi v..2d, #0` -- all thirty-two vector registers destroyed at every
  ;; swap -- and this case passes, and so does every row of run_gc_modes.sh.
  ;; The vectors are live *across the loop* but not *used in* it, so the
  ;; allocator spills them on the way in and reloads them on the way out, which
  ;; is what it should do; a version that advanced them every iteration to force
  ;; the issue changed the allocation shape enough that no cycle ran at all.
  ;; So the wrapper's vector half is insurance, not a tested path: it costs one
  ;; instruction per eighty entries appended and nothing in this suite can tell
  ;; whether it is needed.  What the case *does* now test is the swap itself --
  ;; it publishes ~2600 chunks and recycles ~2000 -- which the first version of
  ;; it did not, having published none at all.
  ;;
  ;; The shape matters as much as the vectors, and the first version of this got
  ;; it wrong: it allocated on every iteration and stored once, which drives
  ;; cycles but leaves almost nothing in the log, and the whole case ran to
  ;; completion having published *no chunk at all*.  A wrapper that restored
  ;; nothing would have passed it.  What fills a chunk is stores per unit of
  ;; allocation, so the allocation is one small object per outer iteration and
  ;; the stores are a full pass over a thousand-element array whose entries are
  ;; already real objects -- an overwritten null is not logged, so the array has
  ;; to be filled before the loop that matters starts.  The inner loop holds no
  ;; call but the barrier's own, which is what leaves the vectors in q registers
  ;; across it.
  (func (export "satb_v128_live") (param $n i32)
    (local $i i32) (local $j i32)
    (local $a (ref $refs)) (local $src (ref $refs))
    (local $v0 v128) (local $v1 v128) (local $v2 v128) (local $v3 v128)
    (local $v4 v128) (local $v5 v128) (local $v6 v128) (local $v7 v128)
    (local.set $v0 (v128.const i32x4 0x00010203 0x04050607 0x08090a0b 0x0c0d0e0f))
    (local.set $v1 (v128.const i32x4 0x10111213 0x14151617 0x18191a1b 0x1c1d1e1f))
    (local.set $v2 (v128.const i32x4 0x20212223 0x24252627 0x28292a2b 0x2c2d2e2f))
    (local.set $v3 (v128.const i32x4 0x30313233 0x34353637 0x38393a3b 0x3c3d3e3f))
    (local.set $v4 (v128.const i32x4 0x40414243 0x44454647 0x48494a4b 0x4c4d4e4f))
    (local.set $v5 (v128.const i32x4 0x50515253 0x54555657 0x58595a5b 0x5c5d5e5f))
    (local.set $v6 (v128.const i32x4 0x60616263 0x64656667 0x68696a6b 0x6c6d6e6f))
    (local.set $v7 (v128.const i32x4 0x70717273 0x74757677 0x78797a7b 0x7c7d7e7f))
    (local.set $a (array.new_default $refs (i32.const 1024)))
    (local.set $src (array.new_default $refs (i32.const 1024)))
    (call $escape (local.get $a))
    (call $escape (local.get $src))
    ;; Both arrays filled with real objects, so that every store in the loop
    ;; below overwrites a reference the barrier owes an entry for.
    (loop $fill
      (array.set $refs (local.get $src) (local.get $j)
                 (struct.new $node (ref.null $node) (local.get $j)))
      (array.set $refs (local.get $a) (local.get $j)
                 (struct.new $node (ref.null $node) (local.get $j)))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br_if $fill (i32.lt_u (local.get $j) (i32.const 1024))))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        ;; Enough allocation to keep cycles coming -- a mark only advances at an
        ;; allocation, so a loop that allocates too little never runs *during*
        ;; one and logs nothing however many times it stores.
        (call $churn (i32.const 24))
        ;; And a thousand stores into an array the mark has already been past,
        ;; each overwriting a reference the barrier has to log: this is the loop
        ;; that fills chunks, and it calls nothing but the barrier.
        (local.set $j (i32.const 0))
        (loop $s
          (array.set $refs (local.get $a) (local.get $j)
                     (array.get $refs (local.get $src) (local.get $j)))
          (local.set $j (i32.add (local.get $j) (i32.const 1)))
          (br_if $s (i32.lt_u (local.get $j) (i32.const 1024))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    ;; Both halves of each, because the low half of v8-v15 survives a C call
    ;; whatever the wrapper does and would agree either way.
    (call $eq32 (i32x4.extract_lane 0 (local.get $v0)) (i32.const 0x00010203))
    (call $eq32 (i32x4.extract_lane 3 (local.get $v0)) (i32.const 0x0c0d0e0f))
    (call $eq32 (i32x4.extract_lane 0 (local.get $v1)) (i32.const 0x10111213))
    (call $eq32 (i32x4.extract_lane 3 (local.get $v1)) (i32.const 0x1c1d1e1f))
    (call $eq32 (i32x4.extract_lane 0 (local.get $v2)) (i32.const 0x20212223))
    (call $eq32 (i32x4.extract_lane 3 (local.get $v2)) (i32.const 0x2c2d2e2f))
    (call $eq32 (i32x4.extract_lane 0 (local.get $v3)) (i32.const 0x30313233))
    (call $eq32 (i32x4.extract_lane 3 (local.get $v3)) (i32.const 0x3c3d3e3f))
    (call $eq32 (i32x4.extract_lane 0 (local.get $v4)) (i32.const 0x40414243))
    (call $eq32 (i32x4.extract_lane 3 (local.get $v4)) (i32.const 0x4c4d4e4f))
    (call $eq32 (i32x4.extract_lane 0 (local.get $v5)) (i32.const 0x50515253))
    (call $eq32 (i32x4.extract_lane 3 (local.get $v5)) (i32.const 0x5c5d5e5f))
    (call $eq32 (i32x4.extract_lane 0 (local.get $v6)) (i32.const 0x60616263))
    (call $eq32 (i32x4.extract_lane 3 (local.get $v6)) (i32.const 0x6c6d6e6f))
    (call $eq32 (i32x4.extract_lane 0 (local.get $v7)) (i32.const 0x70717273))
    (call $eq32 (i32x4.extract_lane 3 (local.get $v7)) (i32.const 0x7c7d7e7f)))

  ;; Stores that stop while a mark is still running.
  ;;
  ;; The drain takes published chunks from the ring, and then the chunk the
  ;; barrier is still filling, in place.  That second part looks like an
  ;; optimisation -- the entries in it would be drained anyway once the barrier
  ;; filled the chunk and published it -- and it is not: nothing makes the
  ;; barrier fill a chunk.  A program that stores a handful of references and
  ;; then only allocates leaves entries in a chunk no swap will ever retire, and
  ;; `gc_satb_empty` quite correctly refuses to call marking finished while they
  ;; are there.  The mark then never ends, no sweep ever runs, and nothing is
  ;; ever freed -- which no case that checks its own answer can notice, because
  ;; a collector that frees nothing returns exactly the right answers.
  ;;
  ;; So the shape is: stores interleaved with allocation, so that the store
  ;; phase ends somewhere inside a mark, and then a long tail of pure
  ;; allocation, which is the only thing that can carry the mark forward and now
  ;; never will.  `run_gc.sh` reads the cycle and sweep counts after this rather
  ;; than checking a value.
  (func (export "satb_stall") (param $n i32)
    (local $i i32) (local $j i32) (local $a (ref $refs))
    (local.set $a (array.new_default $refs (i32.const 256)))
    (call $escape (local.get $a))
    (loop $fill
      (array.set $refs (local.get $a) (local.get $j)
                 (struct.new $node (ref.null $node) (local.get $j)))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br_if $fill (i32.lt_u (local.get $j) (i32.const 256))))
    ;; Enough allocation to have a mark under way, with a few logged stores
    ;; scattered through it so the barrier's chunk is partly full when they stop.
    (loop $mix
      (call $churn (i32.const 8))
      (array.set $refs (local.get $a)
                 (i32.and (local.get $i) (i32.const 255))
                 (struct.new $node (ref.null $node) (local.get $i)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $mix (i32.lt_u (local.get $i) (i32.const 64))))
    ;; And now nothing but allocation.
    (call $churn (local.get $n)))

  ;; ---- a heap with nothing left in it -----------------------------------
  ;;
  ;; The three exports below are one sequence, run in one process by the
  ;; runtime's `--catch`: fill the heap until an allocation traps, throw on the
  ;; heap that is now full, and then let go of it all and carry on.  Each step
  ;; is set up by the one before it, and none of them means anything alone.
  ;;
  ;; Reachable at all only because `WARMC_GC_HEAP_KB` makes the reservation a
  ;; megabyte instead of a gigabyte.  What it reaches is everything a full heap
  ;; is the only way to: the out-of-memory trap, the backstop that takes the
  ;; mark back from a running collector because an allocation cannot wait for
  ;; it, and a trap unwinding out of the collector's own state.

  ;; Every object kept, so nothing can be collected and the reservation is the
  ;; only limit.  The count is a bound on how long this may take and not a
  ;; count of anything: the case ends in the trap, and reaching the end of the
  ;; loop means the heap outlasted the count, which is a broken test rather
  ;; than a passing one.
  (func (export "oom_fill") (param $n i32)
    (local $i i32)
    (loop $l
      (global.set $keep (struct.new $node (global.get $keep) (local.get $i)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (unreachable))

  ;; A throw with no room to put the exception object.
  ;;
  ;; An exception is an ordinary heap object with one difference: allocating it
  ;; may not collect, because `throw` has already written its payload into the
  ;; throwing frame's throw area, which belongs to no safepoint map.  So this
  ;; is the one allocation in the language that answers "no room" with a trap
  ;; rather than with a collection -- and the trap unwinds out of the
  ;; suppression, which is a counter that only the frame that raised it can
  ;; lower.  Left raised, it is `oom_recover` below that fails.
  (func (export "oom_throw") (result i32)
    (block $h (result i32)
      (try_table (catch $plain $h) (throw $plain (i32.const 9)))
      (unreachable)))

  ;; And carrying on, which is deliberately an ordinary case: null the global
  ;; and the whole heap is garbage, so what has to notice is a plain
  ;; allocation.  The object made here is then kept across a churn on a heap
  ;; that has just been emptied under it, which is where the free lists and the
  ;; span list are at their least like the ones every other case leaves.
  (func (export "oom_recover") (param $n i32) (result i32)
    (local $o (ref $node))
    (global.set $keep (ref.null $node))
    (local.set $o (struct.new $node (ref.null $node) (i32.const 4242)))
    (call $escape (local.get $o))
    (call $churn (local.get $n))
    (call $eq32 (struct.get $node $val (local.get $o)) (i32.const 4242))
    (i32.const 1))
)
