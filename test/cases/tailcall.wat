;; Tail calls.
;;
;; The upstream return_call.wast and return_call_indirect.wast cannot run yet:
;; their first module uses a typed function reference, so the whole file --
;; including the million-deep recursions that are the point of the feature --
;; is gated on M3.  This covers the same ground against wasm-interp, plus the
;; shapes that are about *our* frame rather than about the instruction: stack
;; arguments, a multi-value result area passed through, a saved link register,
;; and a frame too large for an `add` immediate.
;;
;; Every recursion here is a million deep.  A tail call that quietly compiled
;; to a call and a return would run out of stack long before that, so these
;; assert the frame arrangement and not just the answer.
(module
  (type $i64_i64 (func (param i64) (result i64)))
  (table 2 funcref)
  (elem (i32.const 0) $count_ind)

  ;; ---- direct self recursion ----
  (func $count (param $n i64) (result i64)
    (if (result i64) (i64.eqz (local.get $n))
      (then (i64.const 0))
      (else (return_call $count (i64.sub (local.get $n) (i64.const 1))))))
  (func (export "count") (result i64) (call $count (i64.const 1000000)))

  ;; ---- mutual recursion, so the branch is to another function ----
  (func $even (param $n i64) (result i32)
    (if (result i32) (i64.eqz (local.get $n))
      (then (i32.const 44))
      (else (return_call $odd (i64.sub (local.get $n) (i64.const 1))))))
  (func $odd (param $n i64) (result i32)
    (if (result i32) (i64.eqz (local.get $n))
      (then (i32.const 99))
      (else (return_call $even (i64.sub (local.get $n) (i64.const 1))))))
  (func (export "even_1m") (result i32) (call $even (i64.const 1000000)))
  (func (export "odd_1m") (result i32) (call $odd (i64.const 1000000)))

  ;; ---- through a table ----
  (func $count_ind (type $i64_i64)
    (if (result i64) (i64.eqz (local.get 0))
      (then (i64.const 7))
      (else (return_call_indirect (type $i64_i64)
              (i64.sub (local.get 0) (i64.const 1)) (i32.const 0)))))
  (func (export "count_ind") (result i64) (call $count_ind (i64.const 1000000)))

  ;; ---- stack arguments ----
  ;;
  ;; Eleven integers: x0 carries the instance, so four of them travel through
  ;; the caller's outgoing area -- and for an exported function the caller is
  ;; warmc_invoke, which is not compiled code and reserves that area only
  ;; because a tail call may need it.  A million deep as well, because the
  ;; region is written over on every hop.
  (func $many (param i64 i64 i64 i64 i64 i64 i64 i64 i64 i64 i64) (result i64)
    (if (result i64) (i64.eqz (local.get 0))
      (then (i64.add (local.get 7)
              (i64.add (local.get 8) (i64.add (local.get 9) (local.get 10)))))
      (else (return_call $many
              (i64.sub (local.get 0) (i64.const 1))
              (local.get 1) (local.get 2) (local.get 3) (local.get 4)
              (local.get 5) (local.get 6) (local.get 7) (local.get 8)
              (local.get 9) (local.get 10)))))
  (func (export "tail_many") (result i64)
    (return_call $many (i64.const 1000000) (i64.const 2) (i64.const 3)
                 (i64.const 4) (i64.const 5) (i64.const 6) (i64.const 7)
                 (i64.const 8) (i64.const 9) (i64.const 10) (i64.const 11)))

  ;; Ten f64s: two go through the outgoing area on the vector side.
  (func $fmany (param f64 f64 f64 f64 f64 f64 f64 f64 f64 f64) (result f64)
    (f64.add (local.get 0) (f64.add (local.get 8) (local.get 9))))
  (func (export "tail_fmany") (result f64)
    (return_call $fmany (f64.const 1.5) (f64.const 2) (f64.const 3)
                 (f64.const 4) (f64.const 5) (f64.const 6) (f64.const 7)
                 (f64.const 8) (f64.const 9.25) (f64.const 10.5)))

  ;; ---- several results ----
  ;;
  ;; The callee writes through the pointer *our* caller gave us, so the
  ;; incoming x8 has to be handed on rather than pointed at this frame.
  (func $pair (result i64 i64) (i64.const 11) (i64.const 220))
  (func $fwd (result i64 i64) (return_call $pair))
  (func (export "pair_sum") (result i64) (i64.add (call $fwd)))

  ;; ---- a saved link register ----
  ;;
  ;; An ordinary call first, so x29/x30 are pushed; the tail call has to put
  ;; them back before it branches or the callee returns into this frame.
  (func $side (param i64) (result i64) (i64.mul (local.get 0) (i64.const 3)))
  (func $mixed (param $n i64) (result i64)
    (return_call $count (call $side (local.get $n))))
  (func (export "mixed") (result i64) (call $mixed (i64.const 333333)))

  ;; ---- a frame past four kilobytes ----
  ;;
  ;; Enough live values to spill heavily, so that the caller's outgoing area is
  ;; further from the stack pointer than an `add` immediate reaches and the
  ;; address has to be materialised.  Reachable only when the values really do
  ;; get slots, which is what the register pressure is for.
  (func $bigtail (param $n i64) (result i64)
    (local $a i64) (local $b i64) (local $c i64) (local $d i64)
    (local $e i64) (local $f i64) (local $g i64) (local $h i64)
    (local $i i64) (local $j i64) (local $k i64) (local $l i64)
    (local $m i64) (local $o i64) (local $p i64) (local $q i64)
    (local $r i64) (local $s i64) (local $t i64) (local $u i64)
    (local.set $a (i64.add (local.get $n) (i64.const 1)))
    (local.set $b (i64.add (local.get $n) (i64.const 2)))
    (local.set $c (i64.add (local.get $n) (i64.const 3)))
    (local.set $d (i64.add (local.get $n) (i64.const 4)))
    (local.set $e (i64.add (local.get $n) (i64.const 5)))
    (local.set $f (i64.add (local.get $n) (i64.const 6)))
    (local.set $g (i64.add (local.get $n) (i64.const 7)))
    (local.set $h (i64.add (local.get $n) (i64.const 8)))
    (local.set $i (i64.add (local.get $n) (i64.const 9)))
    (local.set $j (i64.add (local.get $n) (i64.const 10)))
    (local.set $k (i64.add (local.get $n) (i64.const 11)))
    (local.set $l (i64.add (local.get $n) (i64.const 12)))
    (local.set $m (i64.add (local.get $n) (i64.const 13)))
    (local.set $o (i64.add (local.get $n) (i64.const 14)))
    (local.set $p (i64.add (local.get $n) (i64.const 15)))
    (local.set $q (i64.add (local.get $n) (i64.const 16)))
    (local.set $r (i64.add (local.get $n) (i64.const 17)))
    (local.set $s (i64.add (local.get $n) (i64.const 18)))
    (local.set $t (i64.add (local.get $n) (i64.const 19)))
    (local.set $u (i64.add (local.get $n) (i64.const 20)))
    (return_call $many
      (i64.const 0)
      (i64.add (local.get $a) (local.get $b))
      (i64.add (local.get $c) (local.get $d))
      (i64.add (local.get $e) (local.get $f))
      (i64.add (local.get $g) (local.get $h))
      (i64.add (local.get $i) (local.get $j))
      (i64.add (local.get $k) (local.get $l))
      (i64.add (local.get $m) (local.get $o))
      (i64.add (local.get $p) (local.get $q))
      (i64.add (local.get $r) (local.get $s))
      (i64.add (local.get $t) (local.get $u))))
  (func (export "bigtail") (result i64) (call $bigtail (i64.const 100)))

  ;; ---- a tail call that is not recursive at all ----
  (func $leaf (param i32) (result i32) (i32.add (local.get 0) (i32.const 5)))
  (func (export "plain") (result i32) (return_call $leaf (i32.const 37)))
)
