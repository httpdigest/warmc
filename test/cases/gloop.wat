;; A mutable global that a loop leaves exactly as it found it.
;;
;; `pass_global_loop` proves the round trip -- G's value on every back edge is
;; G's value at the header plus zero -- and then deletes every `global.set` in
;; the loop and replaces every `global.get` with the value the original would
;; have read.  That turns AssemblyScript's per-iteration shadow-stack push and
;; pop into nothing, which is worth a third of the executed instructions in a
;; reduction.  It is also a transformation with four ways to be wrong, and this
;; module exists because when each of its rules was broken on purpose, the whole
;; suite stayed green for four of them.
;;
;; What each export pins down, and which rule it is the ablation test for:
;;
;;   rt_*        the round trip itself: `+4` then `-4` in some order, across
;;               straight-line code, a diamond, and a nested loop.  These are
;;               the cases the pass must transform, and the global is read after
;;               the loop so a wrong entry value shows up in the answer.
;;   nz_*        a loop whose net effect is *not* zero, which must be left
;;               alone.  Off by one in either direction, and one where the two
;;               arms of a diamond disagree -- the merge-equality rule.
;;   call_*      a call inside the loop, which may read or write the global
;;               behind the pass's back.  The callee here does both, so a pass
;;               that promoted anyway would feed it a stale value and lose its
;;               write.  This is the no-calls rule.
;;   exit_*      an early exit taken from the middle of an iteration, where the
;;               global is *not* back to its entry value.  Code after the loop
;;               reads it, so dropping the write-back is visible.  This is the
;;               exit net-zero rule, and `exit_ret` is the same thing through a
;;               `return` rather than a branch.
;;   cy_*        a global the loop *changes* and does not put back -- an
;;               accumulator, which is what a module-level `let` in
;;               AssemblyScript is.  These are the cases for the general form:
;;               a header parameter, the value handed on each back edge, and a
;;               write-back on every edge that leaves.  Each one reads the
;;               global *itself* afterwards, because what a missing or misplaced
;;               write-back gets wrong is the copy in memory and not the
;;               expression the loop computed.
;;   cy_call_*   the same with a call in the loop, which is allowed now when the
;;               callee's effects say it cannot observe the global.  One callee
;;               touches nothing, one touches a *different* global -- the case
;;               the per-global mask exists for -- and one reads this very
;;               global, which must still refuse.
;;
;; Every loop is entered with a trip count read from memory rather than a
;; constant, so nothing unrolls it or folds the whole thing away first.
(module
  (memory 1)
  ;; trip counts: 5, 1, 0, 3
  (data (i32.const 0) "\05\00\00\00\01\00\00\00\00\00\00\00\03\00\00\00")

  (global $g (mut i32) (i32.const 1000))
  (global $h (mut i64) (i64.const 77))
  (global $k (mut i32) (i32.const 0))   ;; a callee's scratch, never the accumulator

  ;; ---------------------------------------------------------------- round trip

  ;; push then pop, straight line.  The global must read 1000 afterwards, and
  ;; the accumulator must see the pushed value each time round.
  (func (export "rt_straight") (result i32)
    (local $i i32) (local $n i32) (local $s i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $g (i32.sub (global.get $g) (i32.const 4)))
        (local.set $s (i32.add (local.get $s) (global.get $g)))
        (global.set $g (i32.add (global.get $g) (i32.const 4)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (global.get $g)))

  ;; The same, with the push and pop on opposite sides of a diamond whose two
  ;; arms agree about the global.  Both arms run: the condition is the parity of
  ;; the counter.
  (func (export "rt_diamond") (result i32)
    (local $i i32) (local $n i32) (local $s i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $g (i32.sub (global.get $g) (i32.const 8)))
        (if (i32.and (local.get $i) (i32.const 1))
          (then (local.set $s (i32.add (local.get $s) (global.get $g))))
          (else (local.set $s (i32.sub (local.get $s) (global.get $g)))))
        (global.set $g (i32.add (global.get $g) (i32.const 8)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (global.get $g)))

  ;; A nested loop, each level pushing and popping its own slot.  The inner
  ;; loop's round trip is what makes the outer one's provable.
  (func (export "rt_nested") (result i32)
    (local $i i32) (local $j i32) (local $n i32) (local $m i32) (local $s i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (local.set $m (i32.load (i32.const 12)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $g (i32.sub (global.get $g) (i32.const 4)))
        (local.set $j (i32.const 0))
        (block $idone
          (loop $il
            (br_if $idone (i32.ge_s (local.get $j) (local.get $m)))
            (global.set $g (i32.sub (global.get $g) (i32.const 4)))
            (local.set $s (i32.add (local.get $s) (global.get $g)))
            (global.set $g (i32.add (global.get $g) (i32.const 4)))
            (local.set $j (i32.add (local.get $j) (i32.const 1)))
            (br $il)))
        (global.set $g (i32.add (global.get $g) (i32.const 4)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (global.get $g)))

  ;; An i64 global, so the type comes from the global's declaration rather than
  ;; from whatever happened to be stored into it.
  (func (export "rt_i64") (result i64)
    (local $i i32) (local $n i32) (local $s i64)
    (global.set $h (i64.const 77))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $h (i64.sub (global.get $h) (i64.const 3)))
        (local.set $s (i64.add (local.get $s) (global.get $h)))
        (global.set $h (i64.add (global.get $h) (i64.const 3)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i64.add (local.get $s) (global.get $h)))

  ;; ------------------------------------------------------- not a round trip

  ;; Net -4 per iteration: the global really does march, and the answer depends
  ;; on the trip count.
  (func (export "nz_down") (result i32)
    (local $i i32) (local $n i32) (local $s i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $g (i32.sub (global.get $g) (i32.const 4)))
        (local.set $s (i32.add (local.get $s) (global.get $g)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (global.get $g)))

  ;; Net +4, so an offset the wrong way round is not mistaken for zero either.
  (func (export "nz_up") (result i32)
    (local $i i32) (local $n i32) (local $s i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $g (i32.add (global.get $g) (i32.const 4)))
        (local.set $s (i32.add (local.get $s) (global.get $g)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (global.get $g)))

  ;; The two arms of the diamond leave the global at different values, so there
  ;; is no single value the merge can carry.  Both arms run.
  (func (export "nz_merge") (result i32)
    (local $i i32) (local $n i32) (local $s i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (if (i32.and (local.get $i) (i32.const 1))
          (then (global.set $g (i32.sub (global.get $g) (i32.const 4))))
          (else (global.set $g (i32.add (global.get $g) (i32.const 4)))))
        (local.set $s (i32.add (local.get $s) (global.get $g)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (global.get $g)))

  ;; ------------------------------------------------------------ with a call

  ;; Reads the global and writes it: a pass that promoted the caller's loop
  ;; would hand this the entry value instead of the pushed one, and would then
  ;; lose the write on the way out.
  (func $touch (result i32)
    (global.set $g (i32.add (global.get $g) (i32.const 1)))
    (global.get $g))

  (func (export "call_inside") (result i32)
    (local $i i32) (local $n i32) (local $s i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $g (i32.sub (global.get $g) (i32.const 4)))
        (local.set $s (i32.add (local.get $s) (call $touch)))
        (global.set $g (i32.add (global.get $g) (i32.const 4)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (global.get $g)))

  ;; The same shape through a table, where the callee is not known at all.
  (table 1 funcref)
  (elem (i32.const 0) $touch)
  (type $void_to_i32 (func (result i32)))

  (func (export "call_indirect_inside") (result i32)
    (local $i i32) (local $n i32) (local $s i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $g (i32.sub (global.get $g) (i32.const 4)))
        (local.set $s (i32.add (local.get $s)
                               (call_indirect (type $void_to_i32) (i32.const 0))))
        (global.set $g (i32.add (global.get $g) (i32.const 4)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (global.get $g)))

  ;; ---------------------------------------------------------- leaving early

  ;; The exit is taken from between the push and the pop, so on that path the
  ;; global is 4 below its entry value -- and the code after the loop reads it.
  ;; The round trip holds on the back edge, which is exactly the case that
  ;; would fool a pass checking only back edges.
  (func (export "exit_mid") (result i32)
    (local $i i32) (local $n i32) (local $s i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (block $out
      (block $done
        (loop $l
          (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
          (global.set $g (i32.sub (global.get $g) (i32.const 4)))
          (local.set $s (i32.add (local.get $s) (global.get $g)))
          ;; leave with the global still pushed
          (br_if $out (i32.eq (local.get $i) (i32.const 2)))
          (global.set $g (i32.add (global.get $g) (i32.const 4)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $l))))
    (i32.add (local.get $s) (global.get $g)))

  ;; The same, but the way out is a `return`, which no edge of the loop names.
  ;;
  ;; Split into a callee and a wrapper on purpose.  What a wrongly promoted
  ;; return path gets wrong is the *global* it leaves behind, not the value it
  ;; returns -- the returned expression is rewritten to the value the original
  ;; would have read, and is right either way.  Every other export here starts
  ;; by storing 1000, and the harness may run each in its own instance, so a
  ;; leftover is only observable if one call both leaves it and looks at it.
  ;; Written as a single export it passed with the rule deleted.
  (func $ret_mid (result i32)
    (local $i i32) (local $n i32) (local $s i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $g (i32.sub (global.get $g) (i32.const 4)))
        (local.set $s (i32.add (local.get $s) (global.get $g)))
        (if (i32.eq (local.get $i) (i32.const 2))
          (then (return (i32.add (local.get $s) (global.get $g)))))
        (global.set $g (i32.add (global.get $g) (i32.const 4)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (global.get $g)))

  (func (export "exit_ret") (result i32) (call $ret_mid))

  ;; The global the returning call left, which is what the rule is about.
  (func (export "exit_ret_g") (result i32)
    (drop (call $ret_mid))
    (global.get $g))


  ;; ------------------------------------------------- carried, not round trip

  ;; The plain accumulator.  n=5, so 1000 + 0+1+2+3+4.
  (func (export "cy_sum") (result i32)
    (local $i i32) (local $n i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $g (i32.add (global.get $g) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (global.get $g))

  ;; A 64-bit one, read inside the loop as well as after: a promotion that
  ;; answered the in-loop read from the entry value instead of the carried one
  ;; would still write back the right total.
  (func (export "cy_sum64") (result i64)
    (local $i i32) (local $n i32) (local $s i64)
    (global.set $h (i64.const 77))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $h (i64.mul (global.get $h) (i64.const 3)))
        (local.set $s (i64.add (local.get $s) (global.get $h)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i64.add (local.get $s) (global.get $h)))

  ;; Two ways out, each leaving a different value behind: the write-back belongs
  ;; to the edge and not to the loop.  i=0,1 add 110 each; i=2 adds 10 and goes.
  (func (export "cy_two") (result i32)
    (local $i i32) (local $n i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (block $out
      (block $done
        (loop $l
          (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
          (global.set $g (i32.add (global.get $g) (i32.const 10)))
          (br_if $out (i32.eq (local.get $i) (i32.const 2)))
          (global.set $g (i32.add (global.get $g) (i32.const 100)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $l))))
    (global.get $g))

  ;; A diamond inside the body, so what reaches the back edge comes from a merge
  ;; the two arms have to agree about.
  (func (export "cy_diamond") (result i32)
    (local $i i32) (local $n i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $g (i32.add (global.get $g) (i32.const 1)))
        (if (i32.and (local.get $i) (i32.const 1))
          (then (global.set $g (i32.add (global.get $g) (i32.const 8))))
          (else (global.set $g (i32.add (global.get $g) (i32.const 8)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (global.get $g))

  ;; Nested, both levels changing it.
  (func (export "cy_nested") (result i32)
    (local $i i32) (local $j i32) (local $n i32) (local $m i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (local.set $m (i32.load (i32.const 12)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $j (i32.const 0))
        (block $idone
          (loop $il
            (br_if $idone (i32.ge_s (local.get $j) (local.get $m)))
            (global.set $g (i32.add (global.get $g) (i32.const 2)))
            (local.set $j (i32.add (local.get $j) (i32.const 1)))
            (br $il)))
        (global.set $g (i32.add (global.get $g) (i32.const 1)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (global.get $g))

  ;; Out through a `return`, which no edge of the loop names.  Split the way
  ;; `exit_ret` is and for the same reason: what a missing write-back gets wrong
  ;; is the global left behind, not the value returned.
  (func $cy_ret_body (result i32)
    (local $i i32) (local $n i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $g (i32.add (global.get $g) (i32.const 10)))
        (if (i32.eq (local.get $i) (i32.const 2))
          (then (return (global.get $g))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (global.get $g))

  (func (export "cy_ret") (result i32) (call $cy_ret_body))

  (func (export "cy_ret_g") (result i32)
    (drop (call $cy_ret_body))
    (global.get $g))

  ;; ------------------------------------------- carried, with a call in the way

  ;; Touches no global at all, so it cannot observe one.
  (func $pure (param $x i32) (result i32)
    (i32.add (i32.mul (local.get $x) (i32.const 3)) (i32.const 1)))

  ;; Touches a global, but not the one the caller is accumulating into.  This is
  ;; what a single "writes globals" bit gets wrong: AssemblyScript's `Math` keeps
  ;; scratch in globals of its own, and under one bit that blocked promoting the
  ;; accumulator its caller was adding the result to.
  (func $scratch (param $x i32) (result i32)
    (global.set $k (i32.add (global.get $k) (local.get $x)))
    (global.get $k))

  ;; Reads the very global the loop accumulates into, so promoting across it
  ;; feeds it a stale value -- and what it returns is accumulated, so the
  ;; staleness lands in the answer.
  (func $peek (param $x i32) (result i32)
    (i32.add (global.get $g) (local.get $x)))

  ;; And one that writes it, which loses a write rather than reading a stale
  ;; value.
  (func $bump (result i32)
    (global.set $g (i32.add (global.get $g) (i32.const 7)))
    (i32.const 1))

  (func (export "cy_call_pure") (result i32)
    (local $i i32) (local $n i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $g (i32.add (global.get $g) (call $pure (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (global.get $g))

  (func (export "cy_call_scratch") (result i32)
    (local $i i32) (local $n i32)
    (global.set $g (i32.const 1000))
    (global.set $k (i32.const 0))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $g (i32.add (global.get $g) (call $scratch (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (global.get $g) (i32.mul (global.get $k) (i32.const 1000))))

  (func (export "cy_call_peek") (result i32)
    (local $i i32) (local $n i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $g (i32.add (global.get $g) (call $peek (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (global.get $g))

  (func (export "cy_call_bump") (result i32)
    (local $i i32) (local $n i32)
    (global.set $g (i32.const 1000))
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (global.set $g (i32.add (global.get $g) (call $bump)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (global.get $g))
  ;; And the global on its own, so a test that only ever reads it through an
  ;; accumulator cannot hide a wrong value in an arithmetic coincidence.
  (func (export "g_after") (result i32) (global.get $g))
)
