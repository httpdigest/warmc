;; A callee that itself calls, inlined -- the population the leaf gate kept
;; out until the allocator learned to price call crossings.
;;
;; The load-bearing case is EH: when a non-leaf callee is spliced into a
;; protected region, the caller's exceptional edges must re-attach to the
;; *copied* call blocks, or a throw from the grand-callee sails past the
;; caller's handler and the module silently stops catching.  Until now that
;; re-attachment path only ever saw dead throws, because a leaf callee had no
;; live calls to protect.  Everything here stores through a witness cell so a
;; wrong path cannot cancel out to the right answer, per inlineeh.wat's
;; discipline; wasm-interp referees every export.
(module
  (memory 1)
  (tag $t)

  ;; grand-callees: one stores and returns, one throws
  (func $grand (param $x i32) (result i32)
    (i32.store (i32.const 128) (local.get $x))
    (i32.add (local.get $x) (i32.const 7)))
  (func $grand_throw (param $x i32) (result i32)
    (i32.store (i32.const 132) (local.get $x))
    (throw $t))

  ;; single block with one live call: the flat path now hosts a call
  (func $mid_flat (param $x i32) (result i32)
    (i32.add (call $grand (i32.add (local.get $x) (i32.const 1)))
             (i32.const 100)))

  ;; multi-block with one live call: the splice path
  (func $mid_split (param $x i32) (result i32)
    (block (br 0))
    (if (result i32) (i32.gt_s (local.get $x) (i32.const 1000))
      (then (i32.const -7))
      (else (i32.add (call $grand (local.get $x)) (i32.const 200)))))

  ;; one live call and two results
  (func $mid_two (param $x i32) (result i32 i32)
    (call $grand (local.get $x))
    (i32.load (i32.const 128)))

  ;; the call that throws, behind a store so nothing deletes the path
  (func $mid_throw (param $x i32) (result i32)
    (i32.store (i32.const 136) (local.get $x))
    (call $grand_throw (i32.add (local.get $x) (i32.const 3))))

  ;; two live calls: over the default retcall budget, must stay a call
  (func $mid_wide (param $x i32) (result i32)
    (i32.add (call $grand (local.get $x))
             (call $grand (i32.add (local.get $x) (i32.const 1)))))

  ;; a tail call disqualifies outright
  (func $mid_tail (param $x i32) (result i32)
    (return_call $grand (local.get $x)))

  ;; mutual recursion: each is the other's single-call candidate, and the
  ;; ancestor guard of the coming transitive stage must never spin here
  (func $mua (param $x i32) (result i32)
    (if (result i32) (i32.le_s (local.get $x) (i32.const 0))
      (then (i32.const 40))
      (else (call $mub (i32.sub (local.get $x) (i32.const 1)))))
  )
  (func $mub (param $x i32) (result i32)
    (if (result i32) (i32.le_s (local.get $x) (i32.const 0))
      (then (i32.const 2))
      (else (call $mua (i32.sub (local.get $x) (i32.const 1)))))
  )

  (func (export "flat_hosted_call") (result i32)
    (i32.add (call $mid_flat (i32.const 5))
             (i32.load (i32.const 128))))

  (func (export "split_hosted_call") (result i32)
    (i32.add (call $mid_split (i32.const 9))
             (i32.load (i32.const 128))))

  (func (export "two_results") (result i32)
    (call $mid_two (i32.const 11))
    (i32.add))

  ;; grand-callee's throw caught by the caller of the inlined middle
  (func (export "caught_through_inline") (result i32)
    (block $out
      (try_table (catch $t $out)
        (drop (call $mid_throw (i32.const 21)))
        (return (i32.const -1))))
    (i32.add (i32.load (i32.const 132)) (i32.load (i32.const 136))))

  ;; same, but the middle is inlined at a site outside any region, so the
  ;; throw must unwind past this caller entirely
  (func $uncaught_helper (result i32)
    (drop (call $mid_throw (i32.const 2)))
    (i32.const -1))
  (func (export "caught_one_level_up") (result i32)
    (block $out
      (try_table (catch $t $out)
        (drop (call $uncaught_helper))
        (return (i32.const -2))))
    (i32.const 55))

  (func (export "wide_stays_call") (result i32)
    (call $mid_wide (i32.const 4)))

  (func (export "tail_stays_call") (result i32)
    (call $mid_tail (i32.const 30)))

  ;; a loop site with a constant argument: the in-loop evidence rule's yes
  (func (export "loop_with_evidence") (result i32)
    (local $i i32) (local $acc i32)
    (loop $l
      (local.set $acc (i32.add (local.get $acc) (call $mid_flat (i32.const 2))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_s (local.get $i) (i32.const 10))))
    (local.get $acc))

  (func (export "mutual_recursion") (result i32)
    (call $mua (i32.const 9)))
)
