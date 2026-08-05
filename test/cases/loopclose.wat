;; A loop-carried value that is also read after the loop.
;;
;; `pass_loop_close` gives such a value a second name on the way out: the block
;; the loop exits to takes it as a parameter, and the reads after the loop are
;; rewritten to that.  The point is the register allocator -- with only one name,
;; the value is live until the read after the loop, so it cannot share a register
;; with the update that feeds it and the loop pays a copy every iteration.  With
;; two, its last use is the branch and `s += x` becomes one `fadd`.
;;
;; The transformation is a rewrite of uses, so what it can get wrong is *which*
;; uses.  A use it rewrites must be one the exit edge is the only way to reach;
;; anything else reads a value that was never computed on the path it took.  That
;; is what these exports are about:
;;
;;   lc_*        the shapes it should transform, with the carried value read
;;               after the loop so a wrong entry or exit value shows up.
;;   esc_*       a use outside the loop that the exit edge is *not* the only way
;;               to reach.  `esc_throw` is the case that actually broke: a
;;               `throw` inside the loop body sits in a block whose terminator is
;;               a `trap`, and reading the terminator alone says "this ends the
;;               program, nothing there observes anything" -- which is wrong,
;;               because the exception reaches a handler first, and the handler
;;               reads the payload.
;;   two_*       two ways out of one loop, with the value read after they merge.
;;               Neither edge is the only way to reach that read.
;;
;; Trip counts come from memory so nothing unrolls or folds the loops first.
(module
  (memory 1)
  ;; 5, 3, 0, 9
  (data (i32.const 0) "\05\00\00\00\03\00\00\00\00\00\00\00\09\00\00\00")
  (tag $e (param i32))

  ;; ------------------------------------------------------- what it transforms

  ;; The plain accumulator: 0+1+2+3+4.
  (func (export "lc_sum") (result i32)
    (local $i i32) (local $n i32) (local $s i32)
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $s (i32.add (local.get $s) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))

  ;; Two carried values read afterwards, which share one exit block between them.
  (func (export "lc_two_vals") (result i64)
    (local $i i32) (local $n i32) (local $s i64) (local $p i64)
    (local.set $n (i32.load (i32.const 0)))
    (local.set $p (i64.const 1))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $s (i64.add (local.get $s) (i64.extend_i32_s (local.get $i))))
        (local.set $p (i64.mul (local.get $p) (i64.const 3)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i64.add (local.get $s) (i64.mul (local.get $p) (i64.const 1000))))

  ;; f64, where the copy this removes is an `fmov` on the loop-carried chain --
  ;; the shape the pass was written for.
  (func (export "lc_f64") (result i64)
    (local $i i32) (local $n i32) (local $s f64)
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $s (f64.add (local.get $s)
                               (f64.sqrt (f64.convert_i32_s (local.get $i)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i64.reinterpret_f64 (local.get $s)))

  ;; Nested, both levels carrying, and the outer value read after both.
  (func (export "lc_nested") (result i32)
    (local $i i32) (local $j i32) (local $n i32) (local $m i32) (local $s i32)
    (local.set $n (i32.load (i32.const 0)))
    (local.set $m (i32.load (i32.const 4)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $j (i32.const 0))
        (block $idone
          (loop $il
            (br_if $idone (i32.ge_s (local.get $j) (local.get $m)))
            (local.set $s (i32.add (local.get $s) (i32.const 2)))
            (local.set $j (i32.add (local.get $j) (i32.const 1)))
            (br $il)))
        (local.set $s (i32.add (local.get $s) (i32.const 1)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))

  ;; A loop whose body also traps on a condition that never holds.  A bare trap
  ;; block observes nothing, so it must not count as a way out -- if it did, this
  ;; loop would have two and the pass would decline it.  Only an effectiveness
  ;; rule, so this export cannot fail; it is here so the shape is compiled.
  (func (export "lc_with_trap") (result i32)
    (local $i i32) (local $n i32) (local $s i32)
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $s (i32.add (local.get $s)
                               (i32.div_s (i32.const 100) (local.get $n))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))

  ;; --------------------------------------------- what it must leave alone

  ;; The carried value is the payload of a `throw` taken from inside the body.
  ;; That block is not in the loop -- it has no successor that reaches the header
  ;; -- and its terminator is a trap, so a pass that read the terminator alone
  ;; would treat it as unobservable and rewrite the payload to a value only the
  ;; *normal* exit edge defines.  The handler then reads a register nothing wrote.
  (func $throw_mid (result i32)
    (local $i i32) (local $n i32) (local $s i32)
    (local.set $n (i32.load (i32.const 0)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $s (i32.add (local.get $s) (i32.const 10)))
        (if (i32.eq (local.get $i) (i32.const 2))
          (then (local.get $s) (throw $e)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))

  ;; 30 on the third iteration, and never the 50 the whole loop would give.
  (func (export "esc_throw") (result i32)
    (block $h (result i32)
      (try_table (catch $e $h) (drop (call $throw_mid)))
      (i32.const -1)))

  ;; Two ways out, and the value is read after they merge -- so neither edge is
  ;; the only way to reach that read.
  (func (export "two_exits") (result i32)
    (local $i i32) (local $n i32) (local $s i32)
    (local.set $n (i32.load (i32.const 12)))
    (block $out
      (block $done
        (loop $l
          (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
          (local.set $s (i32.add (local.get $s) (i32.const 10)))
          (br_if $out (i32.eq (local.get $i) (i32.const 4)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $l))))
    (local.get $s))

  ;; The loop's exit target reached from somewhere else too, so the parameter
  ;; cannot be added to it and the edge has to be split.  Memory[8] is 0, so the
  ;; loop runs; the other side is compiled but not taken.
  (func (export "join_after") (result i32)
    (local $i i32) (local $n i32) (local $s i32)
    (local.set $n (i32.load (i32.const 0)))
    (block $join
      (block $go
        (br_if $go (i32.eqz (i32.load (i32.const 8))))
        (local.set $s (i32.const 999))
        (br $join))
      (block $done
        (loop $l
          (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
          (local.set $s (i32.add (local.get $s) (i32.const 3)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $l))))
    (local.get $s))
)
