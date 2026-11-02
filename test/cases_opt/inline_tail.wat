;; Callees that make tail calls, inlined (inline.c's untail): in the copy
;; spliced into a caller, each tail call is a call and the return of what
;; it returns, so the caller makes the call the callee would have handed
;; on.  One result and two; through call_ref and call_indirect; a tail
;; call on one path of two; a throw from the tail-called function caught
;; by a handler around the inlined site, for one result and for two (whose
;; call ends a block of its own, so that the splice gives it the site's
;; handler edges); and a callee whose own tail calls loop a million deep,
;; which must still run in a bounded stack: inlined, its first step is a
;; call, and the rest loops out of line as before.  Each callee stores
;; through a witness cell, per inlineeh.wat's discipline, so a wrong path
;; cannot cancel out to the right answer.  WARMC_INLINE_NO_TAIL=1 is the
;; old gate, which took none of them (rej_tail).
;;
;; stats -O2: rej_tail=0
;;
;; run: one => i32:19
;; run: two => i32:17
;; run: both_paths => i32:29
;; run: through_ref => i32:25
;; run: through_table => i32:90
;; run: caught_one => i32:21
;; run: caught_two => i32:23
;; run: deep_loop => i32:3000000
;; run: in_a_loop => i32:125
(module
  (memory 1)
  (tag $t)
  (type $ii (func (param i32) (result i32)))
  (type $i2 (func (param i32) (result i32 i32)))
  (table 2 funcref)
  (elem (i32.const 0) func $grand $grand2)

  ;; what the callees call
  (func $grand (type $ii)
    (i32.store (i32.const 128) (local.get 0))
    (i32.add (local.get 0) (i32.const 7)))
  (func $grand2 (type $i2)
    (i32.store (i32.const 132) (local.get 0))
    (i32.mul (local.get 0) (i32.const 3))
    (i32.add (local.get 0) (i32.const 1)))
  (func $grand_throw (param $x i32) (result i32)
    (i32.store (i32.const 136) (local.get $x))
    (throw $t))
  (func $grand2_throw (param $x i32) (result i32 i32)
    (i32.store (i32.const 140) (local.get $x))
    (throw $t))

  ;; the callees
  (func $tail1 (param $x i32) (result i32)
    (return_call $grand (i32.add (local.get $x) (i32.const 1))))
  (func $tail2 (param $x i32) (result i32 i32)
    (return_call $grand2 (i32.add (local.get $x) (i32.const 2))))
  (func $tail_if (param $x i32) (result i32)
    (if (i32.gt_s (local.get $x) (i32.const 10))
      (then (return_call $grand (local.get $x))))
    (i32.sub (local.get $x) (i32.const 1)))
  (func $tail_ref (param $x i32) (result i32)
    (return_call_ref $ii (local.get $x) (ref.func $grand)))
  (func $tail_ind (param $x i32) (result i32 i32)
    (return_call_indirect (type $i2) (local.get $x) (i32.const 1)))
  (func $tail_throw (param $x i32) (result i32)
    (return_call $grand_throw (local.get $x)))
  (func $tail2_throw (param $x i32) (result i32 i32)
    (return_call $grand2_throw (local.get $x)))
  ;; counts down by tail calls
  (func $count (param $n i32) (param $acc i32) (result i32)
    (if (i32.eqz (local.get $n)) (then (return (local.get $acc))))
    (return_call $count (i32.sub (local.get $n) (i32.const 1))
                        (i32.add (local.get $acc) (i32.const 3))))
  (func $count_from (param $n i32) (result i32)
    (return_call $count (local.get $n) (i32.const 0)))

  ;; grand(6) = 13, its witness 6
  (func (export "one") (result i32)
    (i32.add (call $tail1 (i32.const 5)) (i32.load (i32.const 128))))
  ;; grand2(6) = 18 7, its witness 6
  (func (export "two") (result i32)
    (call $tail2 (i32.const 4))
    (i32.sub)
    (i32.load (i32.const 132))
    (i32.add))
  ;; grand(20) = 27, and 3 - 1
  (func (export "both_paths") (result i32)
    (i32.add (call $tail_if (i32.const 20)) (call $tail_if (i32.const 3))))
  ;; grand(9) = 16, its witness 9
  (func (export "through_ref") (result i32)
    (i32.add (call $tail_ref (i32.const 9)) (i32.load (i32.const 128))))
  ;; grand2(5) = 15 6
  (func (export "through_table") (result i32)
    (call $tail_ind (i32.const 5))
    (i32.mul))
  (func (export "caught_one") (result i32)
    (block $out
      (try_table (catch $t $out)
        (drop (call $tail_throw (i32.const 21)))
        (return (i32.const -1))))
    (i32.load (i32.const 136)))
  (func (export "caught_two") (result i32)
    (block $out
      (try_table (catch $t $out)
        (call $tail2_throw (i32.const 23))
        (drop)
        (drop)
        (return (i32.const -1))))
    (i32.load (i32.const 140)))
  (func (export "deep_loop") (result i32)
    (call $count_from (i32.const 1000000)))
  ;; the sum of i + 8 for i below 10
  (func (export "in_a_loop") (result i32)
    (local $i i32) (local $acc i32)
    (loop $l
      (local.set $acc (i32.add (local.get $acc) (call $tail1 (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_s (local.get $i) (i32.const 10))))
    (local.get $acc))
)
