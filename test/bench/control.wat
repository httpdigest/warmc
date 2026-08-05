;; Calls, tail calls, typed references and exceptions, as loops.
;;
;; `test/cases/exceptions.wat`, `tailcall.wat` and `typedref.wat` establish
;; that these compute the right answer.  What none of them shows is the cost,
;; and each of these features has a cost that only a loop makes visible:
;;
;;   * `call_ref` is supposed to be cheaper than `call_indirect` -- no table
;;     load, no bound, no run-time type compare -- and `c_callref` against
;;     `c_indirect` is the number that says whether it is.
;;   * a tail call has to take the frame apart before it jumps, so
;;     `c_tail` against `c_call` is what that costs.
;;   * an exception handler is meant to cost *nothing at all* when nothing
;;     throws.  `c_guard` is a loop inside a `try_table` whose handler never
;;     runs, and its instruction count should equal the same loop without one.
;;
;; Everything is parameterised by an iteration count so no trip count is known,
;; and every callee has enough live state that inlining it away is not the
;; whole answer.
(module
  (type $ii (func (param i32) (result i32)))
  (type $acc (func (param i32 i32) (result i32)))

  (tag $ei (param i32))

  ;; An extended constant expression.  The feature is an instantiation-time
  ;; one -- the initialiser is folded before any code runs -- so there is
  ;; nothing here to time; it is in the module so that the module is one a
  ;; wasm 3.0 producer could actually have emitted.
  (global $bias i32 (i32.add (i32.mul (i32.const 3) (i32.const 5)) (i32.const 2)))

  (table $t 4 funcref)
  (elem (i32.const 0) $sq $neg $sq $neg)
  (elem declare func $sq $neg $step $tstep)

  (func $sq (type $ii) (i32.add (i32.mul (local.get 0) (local.get 0)) (global.get $bias)))
  (func $neg (type $ii) (i32.sub (global.get $bias) (local.get 0)))

  ;; ---- calls ---------------------------------------------------------

  ;; The three differ in one line each: how the callee is named.  Which callee
  ;; it is depends on the argument, so nothing can be devirtualised, and it is
  ;; settled *before* the loop, so the loop body holds the call and nothing
  ;; else -- otherwise the comparison would be measuring the selection.

  ;; The floor: a direct call, which the inliner is free to remove entirely.
  ;; It is here to be subtracted from the two below, not as a result of its own.
  (func (export "c_call") (param $n i32) (result i32)
    (local $i i32) (local $a i32)
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $a (i32.add (local.get $a) (call $sq (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (local.get $a))

  ;; Through a table: a bound, a null check and a run-time type compare on top
  ;; of the call.
  (func (export "c_indirect") (param $n i32) (result i32)
    (local $i i32) (local $a i32) (local $k i32)
    (local.set $k (i32.and (local.get $n) (i32.const 1)))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $a (i32.add (local.get $a)
          (call_indirect $t (type $ii) (local.get $i) (local.get $k))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (local.get $a))

  ;; Through a typed reference: the type system has already checked it, so
  ;; only the null remains.
  (func (export "c_callref") (param $n i32) (result i32)
    (local $i i32) (local $a i32) (local $f (ref null $ii))
    (local.set $f (ref.func $neg))
    (if (i32.and (local.get $n) (i32.const 1))
      (then (local.set $f (ref.func $sq))))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $a (i32.add (local.get $a)
          (call_ref $ii (local.get $i) (local.get $f))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (local.get $a))

  ;; ---- tail calls ----------------------------------------------------

  ;; The same accumulation written as self tail recursion, which is what a
  ;; functional producer emits for a loop.  A tail call takes the frame apart
  ;; before it jumps, so this is that teardown, once per iteration.
  (func $step (type $acc)
    (if (result i32) (i32.eqz (local.get 0))
      (then (local.get 1))
      (else (return_call $step
              (i32.sub (local.get 0) (i32.const 1))
              (i32.add (local.get 1) (i32.mul (local.get 0) (i32.const 3)))))))
  (func (export "c_tail") (param $n i32) (result i32)
    (call $step (local.get $n) (global.get $bias)))

  ;; And through the table, where the teardown has to happen after the lookup
  ;; rather than before it.
  (func $tstep (type $acc)
    (if (result i32) (i32.eqz (local.get 0))
      (then (local.get 1))
      (else (return_call_ref $acc
              (i32.sub (local.get 0) (i32.const 1))
              (i32.add (local.get 1) (i32.mul (local.get 0) (i32.const 5)))
              (ref.func $tstep)))))
  (func (export "c_tailref") (param $n i32) (result i32)
    (call $tstep (local.get $n) (global.get $bias)))

  ;; ---- exceptions ----------------------------------------------------

  ;; Throws only for a negative argument, which the callers below never pass;
  ;; the branch is here so the callee is not a constant and the handler is not
  ;; provably dead.
  (func $maybe (param $x i32) (result i32)
    (if (i32.lt_s (local.get $x) (i32.const 0))
      (then (local.get $x) (throw $ei)))
    (i32.add (i32.mul (local.get $x) (local.get $x)) (global.get $bias)))

  ;; The loop with no handler at all.  Everything below is measured against
  ;; this: an exception handler is supposed to be free until it catches
  ;; something, and free means these two counts are equal.
  (func (export "c_plain") (param $n i32) (result i32)
    (local $i i32) (local $a i32)
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $a (i32.add (local.get $a) (call $maybe (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (local.get $a))

  ;; The same loop, wholly inside one region.
  (func (export "c_guard") (param $n i32) (result i32)
    (local $i i32) (local $a i32)
    (block $h (result i32)
      (try_table (catch $ei $h)
        (block $done
          (br_if $done (i32.eqz (local.get $n)))
          (loop $l
            (local.set $a (i32.add (local.get $a) (call $maybe (local.get $i))))
            (local.set $i (i32.add (local.get $i) (i32.const 1)))
            (br_if $l (i32.lt_u (local.get $i) (local.get $n))))))
      ;; The only way out of the block is the catch edge, so what it carries is
      ;; the tag's payload and the fallthrough below never runs.
      (return (local.get $a)))
    (drop)
    (i32.const -1))

  ;; A region entered and left on every iteration rather than once, which is
  ;; what a `try` inside a loop looks like.
  (func (export "c_region") (param $n i32) (result i32)
    (local $i i32) (local $a i32)
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $a (i32.add (local.get $a)
          (block $h (result i32)
            (try_table (result i32) (catch $ei $h)
              (call $maybe (local.get $i))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (local.get $a))

  ;; Now one that actually throws, once per iteration, caught one frame up.
  (func (export "c_throw") (param $n i32) (result i32)
    (local $i i32) (local $a i32)
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $a (i32.add (local.get $a)
          (block $h (result i32)
            (try_table (result i32) (catch $ei $h)
              (call $maybe (i32.sub (i32.const -1) (local.get $i)))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (local.get $a))

  ;; Four frames of live state between the throw and the handler, so the walk
  ;; has something to restore rather than a chain of empty frames.
  (func $d0 (param $x i32) (result i32)
    (if (i32.lt_s (local.get $x) (i32.const 0))
      (then (local.get $x) (throw $ei)))
    (local.get $x))
  (func $d1 (param $x i32) (result i32) (local $k i32) (local $j i32)
    (local.set $k (i32.mul (local.get $x) (i32.const 3)))
    (local.set $j (i32.xor (local.get $x) (i32.const 0x5a5a)))
    (i32.add (call $d0 (local.get $x)) (i32.add (local.get $k) (local.get $j))))
  (func $d2 (param $x i32) (result i32) (local $k i32) (local $j i32)
    (local.set $k (i32.mul (local.get $x) (i32.const 5)))
    (local.set $j (i32.xor (local.get $x) (i32.const 0x3c3c)))
    (i32.add (call $d1 (local.get $x)) (i32.add (local.get $k) (local.get $j))))
  (func $d3 (param $x i32) (result i32) (local $k i32) (local $j i32)
    (local.set $k (i32.mul (local.get $x) (i32.const 7)))
    (local.set $j (i32.xor (local.get $x) (i32.const 0x0f0f)))
    (i32.add (call $d2 (local.get $x)) (i32.add (local.get $k) (local.get $j))))
  (func (export "c_deep") (param $n i32) (result i32)
    (local $i i32) (local $a i32)
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $a (i32.add (local.get $a)
          (block $h (result i32)
            (try_table (result i32) (catch $ei $h)
              (call $d3 (i32.sub (i32.const -1) (local.get $i)))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (local.get $a))

  (func (export "startup") (param i32) (result i32) (i32.const 0))
)
