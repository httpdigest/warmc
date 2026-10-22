;; Unsigned 32-bit division and remainder by a divisor a loop does not change,
;; which becomes a multiply-high by a reciprocal made before the loop
;; (pass_invdiv).  The reciprocal is exact for every 32-bit dividend and every
;; divisor from 2 up, and 1 is a case of its own, so each export runs a loop
;; of dividends that reach the top of the range against divisors at the edges:
;; 1, 2, 3, powers of two, 2^31 and its neighbours, and the largest values.
;; Several divisors in one loop, a divisor from an outer loop, the quotient
;; and the remainder of one divisor, and a division on only one path through
;; the loop.  And the same divisions done in 64 bits on values zero-extended
;; from 32, as Go's compiler writes them, next to a 64-bit dividend that does
;; not fit in 32 bits and must be left a division.  The divisors come from a
;; table in memory so nothing folds them.

(module
  (memory 1)
  (data (i32.const 0)
    "\01\00\00\00" "\02\00\00\00" "\03\00\00\00" "\07\00\00\00"
    "\0a\00\00\00" "\00\01\00\00" "\ff\ff\00\00" "\01\00\01\00"
    "\00\00\00\80" "\01\00\00\80" "\ff\ff\ff\7f" "\fe\ff\ff\ff"
    "\ff\ff\ff\ff" "\9b\ab\72\2b" "\05\00\00\00" "\0b\00\00\00")

  (global $seed (mut i32) (i32.const 0x2545f491))
  (func $next (result i32)
    (global.set $seed (i32.add (i32.mul (global.get $seed) (i32.const 1103515245))
                               (i32.const 12345)))
    (global.get $seed))

  ;; dividend k of a run: the edges first, then pseudo-random words
  (func $dividend (param $k i32) (result i32)
    (if (result i32) (i32.lt_u (local.get $k) (i32.const 8))
      (then (i32.load (i32.add (i32.const 64) (i32.shl (local.get $k) (i32.const 2)))))
      (else (call $next))))

  (func $digest (param $h i32) (param $x i32) (result i32)
    (i32.mul (i32.xor (local.get $h) (local.get $x)) (i32.const 16777619)))

  ;; every divisor in the table, quotient and remainder of each dividend
  (func (export "quotients_and_remainders") (result i32)
    (local $t i32) (local $d i32) (local $k i32) (local $n i32) (local $h i32)
    (i32.store (i32.const 64) (i32.const 0))
    (i32.store (i32.const 68) (i32.const 1))
    (i32.store (i32.const 72) (i32.const 0xffffffff))
    (i32.store (i32.const 76) (i32.const 0xfffffffe))
    (i32.store (i32.const 80) (i32.const 0x80000000))
    (i32.store (i32.const 84) (i32.const 0x7fffffff))
    (i32.store (i32.const 88) (i32.const 0x80000001))
    (i32.store (i32.const 92) (i32.const 0xfffffff0))
    (local.set $h (i32.const -2128831035))
    (loop $divisors
      (local.set $d (i32.load (i32.shl (local.get $t) (i32.const 2))))
      (local.set $k (i32.const 0))
      (loop $dividends
        (local.set $n (call $dividend (local.get $k)))
        (local.set $h (call $digest (local.get $h) (i32.div_u (local.get $n) (local.get $d))))
        (local.set $h (call $digest (local.get $h) (i32.rem_u (local.get $n) (local.get $d))))
        (br_if $dividends (i32.lt_u (local.tee $k (i32.add (local.get $k) (i32.const 1)))
                                    (i32.const 200))))
      (br_if $divisors (i32.lt_u (local.tee $t (i32.add (local.get $t) (i32.const 1)))
                                 (i32.const 16))))
    (local.get $h))

  ;; d and d + 1 in one loop, as gobench/divmod hashes them
  (func (export "two_divisors") (result i32)
    (local $t i32) (local $d i32) (local $i i32) (local $h i32)
    (loop $divisors
      (local.set $d (i32.load (i32.shl (local.get $t) (i32.const 2))))
      (local.set $i (i32.const 0))
      (loop $l
        (local.set $h (i32.add (local.get $h)
          (i32.add (i32.div_u (i32.mul (local.get $i) (i32.const 2654435761)) (local.get $d))
                   (i32.rem_u (i32.mul (local.get $i) (i32.const 40503))
                              (i32.add (local.get $d) (i32.const 1))))))
        (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                            (i32.const 300))))
      (br_if $divisors (i32.lt_u (local.tee $t (i32.add (local.get $t) (i32.const 1)))
                                 (i32.const 12))))
    (local.get $h))

  ;; the divisor changes in the outer loop and not in the inner one, and the
  ;; division runs only on one path of the inner loop
  (func (export "outer_divisor_one_path") (result i32)
    (local $o i32) (local $d i32) (local $i i32) (local $n i32) (local $h i32)
    (loop $outer
      (local.set $d (i32.add (i32.and (call $next) (i32.const 0xffff)) (local.get $o)))
      (local.set $i (i32.const 0))
      (loop $inner
        (local.set $n (call $next))
        (if (i32.and (local.get $n) (i32.const 4))
          (then (local.set $h (call $digest (local.get $h) (i32.rem_u (local.get $n) (local.get $d)))))
          (else (local.set $h (i32.add (local.get $h) (i32.const 3)))))
        (br_if $inner (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                                (i32.const 50))))
      (br_if $outer (i32.lt_u (local.tee $o (i32.add (local.get $o) (i32.const 1)))
                              (i32.const 40))))
    (local.get $h))

  ;; divisor 1 and the largest divisor, which the reciprocal handles at its ends
  (func (export "divisor_one_and_max") (result i64)
    (local $i i32) (local $one i32) (local $max i32) (local $r i64) (local $n i32)
    (local.set $one (i32.load (i32.const 0)))
    (local.set $max (i32.load (i32.const 48)))
    (loop $l
      (local.set $n (call $next))
      (local.set $r (i64.add (local.get $r) (i64.extend_i32_u (i32.div_u (local.get $n) (local.get $one)))))
      (local.set $r (i64.add (local.get $r) (i64.extend_i32_u (i32.rem_u (local.get $n) (local.get $one)))))
      (local.set $r (i64.add (local.get $r) (i64.extend_i32_u (i32.div_u (local.get $n) (local.get $max)))))
      (local.set $r (i64.add (local.get $r) (i64.extend_i32_u (i32.rem_u (local.get $n) (local.get $max)))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 500))))
    (local.get $r))
  ;; i64 division of zero-extended 32-bit values, and one dividend too wide
  (func (export "wide_division_of_narrow_values") (result i64)
    (local $t i32) (local $d i64) (local $k i32) (local $n i64) (local $h i64) (local $big i64)
    (loop $divisors
      (local.set $d (i64.extend_i32_u (i32.load (i32.shl (local.get $t) (i32.const 2)))))
      (local.set $k (i32.const 0))
      (loop $dividends
        (local.set $n (i64.extend_i32_u (call $next)))
        (local.set $big (i64.or (local.get $n) (i64.shl (local.get $n) (i64.const 29))))
        (local.set $h (i64.add (i64.mul (local.get $h) (i64.const 1099511628211))
                               (i64.div_u (local.get $n) (local.get $d))))
        (local.set $h (i64.xor (local.get $h) (i64.rem_u (local.get $n) (local.get $d))))
        (local.set $h (i64.add (local.get $h) (i64.div_u (local.get $big) (local.get $d))))
        (br_if $dividends (i32.lt_u (local.tee $k (i32.add (local.get $k) (i32.const 1)))
                                    (i32.const 150))))
      (br_if $divisors (i32.lt_u (local.tee $t (i32.add (local.get $t) (i32.const 1)))
                                 (i32.const 16))))
    (local.get $h))
)
