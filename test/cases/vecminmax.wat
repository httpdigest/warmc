;; Min and max reductions, which a producer writes as a select over a
;; comparison because wasm has no integer min or max instruction.
;;
;; Hand-written, and it stays hand-written: test/gen_cases.py does not emit a
;; file of this name, and run_cases.py globs the directory afterwards, so this
;; is run against wasm-interp like every generated case is.
;;
;; What it is for.  The vectoriser turns `if (v < lo) lo = v` into SMIN and
;; folds the accumulator with SMINV.  Four things can go wrong and none of
;; them is caught anywhere else:
;;
;;   * the identity.  A min accumulator starts at INT_MAX and a max one at
;;     INT_MIN, in the accumulator's own signedness.  Start a signed min at 0
;;     and every negative answer is lost; start an unsigned one at INT_MAX and
;;     every answer above it is.  Every buffer below therefore contains values
;;     on both sides of zero and at the extremes themselves.
;;   * the sense.  `(x<y) ? x : y` is a min, and swapping either the
;;     comparison's operands or its direction makes it a max.  All four
;;     arrangements appear here, because reading one of them backwards gives
;;     an answer that is still plausible.
;;   * signedness.  -1 is the largest value unsigned and nearly the smallest
;;     signed, so a signed/unsigned mix-up shows up as 4294967295 against -1.
;;   * the tail.  Lengths here are deliberately not multiples of four, and two
;;     are shorter than one vector, so the scalar remainder has to run and has
;;     to start where the vector body stopped.
(module
  (memory 1)

  ;; i32 values at byte 0: the extremes, both signs, and duplicates.
  ;; Deliberately 13 of them, which is three vectors and a one-element tail.
  (func $fill (result i32)
    (i32.store (i32.const 0)  (i32.const 7))
    (i32.store (i32.const 4)  (i32.const -3))
    (i32.store (i32.const 8)  (i32.const 2147483647))
    (i32.store (i32.const 12) (i32.const 0))
    (i32.store (i32.const 16) (i32.const -2147483648))
    (i32.store (i32.const 20) (i32.const 5))
    (i32.store (i32.const 24) (i32.const -1))
    (i32.store (i32.const 28) (i32.const 100))
    (i32.store (i32.const 32) (i32.const -100))
    (i32.store (i32.const 36) (i32.const 2147483647))
    (i32.store (i32.const 40) (i32.const 1))
    (i32.store (i32.const 44) (i32.const -2147483648))
    (i32.store (i32.const 48) (i32.const 42))
    (i32.const 13))

  ;; select(x, y, x<y) -- a signed min written the plain way.
  (func (export "mm_smin") (result i32)
    (local $i i32) (local $n i32) (local $m i32) (local $v i32)
    (local.set $n (call $fill))
    (local.set $m (i32.const 2147483647))
    (loop $L
      (if (i32.lt_s (local.get $i) (local.get $n))
        (then
          (local.set $v (i32.load (i32.mul (local.get $i) (i32.const 4))))
          (local.set $m (select (local.get $v) (local.get $m)
                                (i32.lt_s (local.get $v) (local.get $m))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $m))

  ;; The same value reached with the comparison the other way round, which is
  ;; the arrangement a max is normally written in -- getting the two confused
  ;; is the whole reason both are here.
  (func (export "mm_smax") (result i32)
    (local $i i32) (local $n i32) (local $m i32) (local $v i32)
    (local.set $n (call $fill))
    (local.set $m (i32.const -2147483648))
    (loop $L
      (if (i32.lt_s (local.get $i) (local.get $n))
        (then
          (local.set $v (i32.load (i32.mul (local.get $i) (i32.const 4))))
          (local.set $m (select (local.get $v) (local.get $m)
                                (i32.lt_s (local.get $m) (local.get $v))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $m))

  ;; Unsigned, over the same bytes: -1 is 4294967295 here and INT_MIN is
  ;; 2147483648, so both answers differ from the signed ones above.
  (func (export "mm_umin") (result i32)
    (local $i i32) (local $n i32) (local $m i32) (local $v i32)
    (local.set $n (call $fill))
    (local.set $m (i32.const -1))
    (loop $L
      (if (i32.lt_s (local.get $i) (local.get $n))
        (then
          (local.set $v (i32.load (i32.mul (local.get $i) (i32.const 4))))
          (local.set $m (select (local.get $v) (local.get $m)
                                (i32.gt_u (local.get $m) (local.get $v))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $m))

  (func (export "mm_umax") (result i32)
    (local $i i32) (local $n i32) (local $m i32) (local $v i32)
    (local.set $n (call $fill))
    (loop $L
      (if (i32.lt_s (local.get $i) (local.get $n))
        (then
          (local.set $v (i32.load (i32.mul (local.get $i) (i32.const 4))))
          (local.set $m (select (local.get $v) (local.get $m)
                                (i32.gt_u (local.get $v) (local.get $m))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $m))

  ;; Both accumulators in one loop, which is the shape a minmax() function has
  ;; and the one that made this worth building.  Returned added together so a
  ;; single result carries both.
  (func (export "mm_both") (result i32)
    (local $i i32) (local $n i32) (local $lo i32) (local $hi i32)
    (local $v i32)
    (local.set $n (call $fill))
    (local.set $lo (i32.const 2147483647))
    (local.set $hi (i32.const -2147483648))
    (loop $L
      (if (i32.lt_s (local.get $i) (local.get $n))
        (then
          (local.set $v (i32.load (i32.mul (local.get $i) (i32.const 4))))
          (local.set $lo (select (local.get $v) (local.get $lo)
                                 (i32.lt_s (local.get $v) (local.get $lo))))
          (local.set $hi (select (local.get $v) (local.get $hi)
                                 (i32.lt_s (local.get $hi) (local.get $v))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    ;; -2147483648 + 2147483647 in wrapping i32, which is -1 and tells both
    ;; apart from either being wrong.
    (i32.add (local.get $lo) (local.get $hi)))

  ;; Seeded from the first element rather than from an identity, which is what
  ;; Go's `lo, hi := a[0], a[0]` produces and means the value the loop is
  ;; entered with is not the identity the vector accumulator starts at.
  (func (export "mm_seeded") (result i32)
    (local $i i32) (local $n i32) (local $lo i32) (local $v i32)
    (local.set $n (call $fill))
    (local.set $lo (i32.load (i32.const 0)))
    (loop $L
      (if (i32.lt_s (local.get $i) (local.get $n))
        (then
          (local.set $v (i32.load (i32.mul (local.get $i) (i32.const 4))))
          (local.set $lo (select (local.get $v) (local.get $lo)
                                 (i32.lt_s (local.get $v) (local.get $lo))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $lo))

  ;; Three elements: shorter than one vector, so the vector body must not run
  ;; at all and the accumulator must come back as the identity it went in as.
  (func (export "mm_short") (result i32)
    (local $i i32) (local $m i32) (local $v i32)
    (drop (call $fill))
    (local.set $m (i32.const 2147483647))
    (loop $L
      (if (i32.lt_s (local.get $i) (i32.const 3))
        (then
          (local.set $v (i32.load (i32.mul (local.get $i) (i32.const 4))))
          (local.set $m (select (local.get $v) (local.get $m)
                                (i32.lt_s (local.get $v) (local.get $m))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $m))

  ;; Zero elements: the loop never runs and the answer is the seed untouched.
  (func (export "mm_empty") (result i32)
    (local $i i32) (local $m i32) (local $v i32)
    (drop (call $fill))
    (local.set $m (i32.const 1234))
    (loop $L
      (if (i32.lt_s (local.get $i) (i32.const 0))
        (then
          (local.set $v (i32.load (i32.mul (local.get $i) (i32.const 4))))
          (local.set $m (select (local.get $v) (local.get $m)
                                (i32.lt_s (local.get $v) (local.get $m))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $m))

  ;; 64-bit lanes, which SMINV has no form for -- the vectoriser must refuse
  ;; this and the scalar loop must still be right.  Values chosen so a 32-bit
  ;; answer would differ from the 64-bit one.
  (func (export "mm_i64") (result i64)
    (local $i i32) (local $m i64) (local $v i64)
    (i64.store (i32.const 0) (i64.const 4294967296))
    (i64.store (i32.const 8) (i64.const -8589934592))
    (i64.store (i32.const 16) (i64.const 12884901888))
    (i64.store (i32.const 24) (i64.const -3))
    (i64.store (i32.const 32) (i64.const 9223372036854775807))
    (local.set $m (i64.const 9223372036854775807))
    (loop $L
      (if (i32.lt_s (local.get $i) (i32.const 5))
        (then
          (local.set $v (i64.load (i32.mul (local.get $i) (i32.const 8))))
          (local.set $m (select (local.get $v) (local.get $m)
                                (i64.lt_s (local.get $v) (local.get $m))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $m))

  ;; The accumulator read by something other than the chain.  `t` sums the
  ;; running minimum, so the min is live outside its own update and the loop
  ;; must not be vectorised -- a pass that took it anyway would sum four
  ;; lanes' worth of partial minima instead.
  (func (export "mm_escapes") (result i32)
    (local $i i32) (local $n i32) (local $m i32) (local $v i32) (local $t i32)
    (local.set $n (call $fill))
    (local.set $m (i32.const 2147483647))
    (loop $L
      (if (i32.lt_s (local.get $i) (local.get $n))
        (then
          (local.set $v (i32.load (i32.mul (local.get $i) (i32.const 4))))
          (local.set $m (select (local.get $v) (local.get $m)
                                (i32.lt_s (local.get $v) (local.get $m))))
          (local.set $t (i32.add (local.get $t) (local.get $m)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $t))

  ;; A select that is not a min or a max: the comparison is against a
  ;; constant, not against the two values being chosen between.  Here to be
  ;; refused -- read as a max it returns 9 instead of 6.
  ;;
  ;; The buffer is shaped so the answer survives the scalar tail, which two
  ;; earlier versions of this case did not manage.  This loop's real meaning
  ;; is "the last value greater than zero", so any positive element the tail
  ;; reaches assigns unconditionally and covers up whatever the vector body
  ;; left behind -- and the vector body claims only (n-1)/4 whole vectors, so
  ;; there is always a tail.  Every positive value is therefore inside the
  ;; first vector and everything after it is at or below zero, which makes the
  ;; tail a no-op no matter how much of the loop the vector body took.
  (func $fillsel (result i32)
    (i32.store (i32.const 0)  (i32.const 5))
    (i32.store (i32.const 4)  (i32.const 9))
    (i32.store (i32.const 8)  (i32.const 3))
    (i32.store (i32.const 12) (i32.const 6))
    (i32.store (i32.const 16) (i32.const 0))
    (i32.store (i32.const 20) (i32.const -1))
    (i32.store (i32.const 24) (i32.const -2))
    (i32.store (i32.const 28) (i32.const 0))
    (i32.store (i32.const 32) (i32.const -5))
    (i32.store (i32.const 36) (i32.const 0))
    (i32.store (i32.const 40) (i32.const -7))
    (i32.store (i32.const 44) (i32.const -3))
    (i32.const 12))

  (func (export "mm_notminmax") (result i32)
    (local $i i32) (local $n i32) (local $m i32) (local $v i32)
    (local.set $n (call $fillsel))
    (loop $L
      (if (i32.lt_s (local.get $i) (local.get $n))
        (then
          (local.set $v (i32.load (i32.mul (local.get $i) (i32.const 4))))
          (local.set $m (select (local.get $v) (local.get $m)
                                (i32.gt_s (local.get $v) (i32.const 0))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $m))
)
