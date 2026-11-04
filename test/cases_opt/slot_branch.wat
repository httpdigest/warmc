;; A sealed module's type test that a branch alone reads, made by the
;; branch itself (arm64 isel, T_BRIF): its failures -- a null, an i31,
;; another slot -- go the false way, through one stub out of line where
;; that edge moves values, and its pass falls into the true way; of an eqz
;; of the test, the ways swapped.  What must hold: each way gets the values
;; it carries (the loops' counters and sums go round on both), a null and
;; an i31 fail a test that takes no null and pass none but a nullable one,
;; and a test of a type with subtypes takes every one of them.
;; The objects come from $mk by i % 8: an A, a B, a C (both A's), a D,
;; null, an i31, a B, a C (whose field after a is no i32: a type of its own,
;; not B again).
;; stats -O2 a64: slot_brs=4
;; run: count_b 16 => i32:4120
;; run: count_b 0 => i32:0
;; run: count_a 16 => i32:6442
;; run: count_a 3 => i32:111
;; run: skip_c 16 => i32:16016
;; run: nullable 16 => i32:12
;; run: nullable 5 => i32:4
;; run: mixed 16 => i32:-16
;; run: mixed 7 => i32:-7
(module
  (type $A (sub (struct (field $a (mut i32)))))
  (type $B (sub $A (struct (field $a (mut i32)) (field $b (mut i32)))))
  (type $C (sub $A (struct (field $a (mut i32)) (field $c (mut i64)))))
  (type $D (struct (field $d i32)))

  ;; i % 8: A(1), B(10, 1000), C(100, 3), D, null, i31, B(10, 1000), C(100, 3)
  (func $mk (param $i i32) (result eqref)
    (local $k i32)
    (local.set $k (i32.and (local.get $i) (i32.const 7)))
    (if (i32.eq (local.get $k) (i32.const 0)) (then (return (struct.new $A (i32.const 1)))))
    (if (i32.eq (local.get $k) (i32.const 1)) (then (return (struct.new $B (i32.const 10) (i32.const 1000)))))
    (if (i32.eq (local.get $k) (i32.const 2)) (then (return (struct.new $C (i32.const 100) (i64.const 3)))))
    (if (i32.eq (local.get $k) (i32.const 3)) (then (return (struct.new $D (i32.const 7)))))
    (if (i32.eq (local.get $k) (i32.const 4)) (then (return (ref.null eq))))
    (if (i32.eq (local.get $k) (i32.const 5)) (then (return (ref.i31 (local.get $i)))))
    (if (i32.eq (local.get $k) (i32.const 6)) (then (return (struct.new $B (i32.const 10) (i32.const 1000)))))
    (struct.new $C (i32.const 100) (i64.const 3)))

  ;; the B's b summed, the others counted: a not-B goes back round, its
  ;; edge moving the counters
  (func (export "count_b") (param $n i32) (result i32)
    (local $i i32) (local $sum i32) (local $no i32) (local $x eqref)
    (if (i32.eqz (local.get $n)) (then (return (i32.const 0))))
    (loop $l
      (local.set $x (call $mk (local.get $i)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (block $not
        (br_if $not (i32.eqz (ref.test (ref $B) (local.get $x))))
        (local.set $sum (i32.add (local.get $sum) (struct.get $B $b (ref.cast (ref $B) (local.get $x)))))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))
        (return (i32.add (local.get $sum) (local.get $no))))
      (local.set $no (i32.add (local.get $no) (i32.const 10)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (i32.add (local.get $sum) (local.get $no)))

  ;; A, B and C are each an A: their a summed, the rest counted a thousand
  (func (export "count_a") (param $n i32) (result i32)
    (local $i i32) (local $sum i32) (local $x eqref)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $x (call $mk (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (if (ref.test (ref $A) (local.get $x))
          (then (local.set $sum (i32.add (local.get $sum) (struct.get $A $a (ref.cast (ref $A) (local.get $x))))))
          (else (local.set $sum (i32.add (local.get $sum) (i32.const 1000)))))
        (br $l)))
    (local.get $sum))

  ;; a C goes back round at once, its pass the way that moves: the rest
  ;; add their kind's weight
  (func (export "skip_c") (param $n i32) (result i32)
    (local $i i32) (local $sum i32) (local $x eqref)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $x (call $mk (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $sum (i32.add (local.get $sum) (i32.const 1)))
        (br_if $l (ref.test (ref $C) (local.get $x)))
        (local.set $sum (i32.add (local.get $sum) (i32.const 1000)))
        (br_if $l (i32.eqz (ref.test (ref $B) (local.get $x))))
        (local.set $sum (i32.add (local.get $sum) (i32.const 1000)))
        (br $l)))
    (local.get $sum))

  ;; a nullable test: a null passes it, an i31 and a D do not
  (func (export "nullable") (param $n i32) (result i32)
    (local $i i32) (local $k i32) (local $x eqref)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $x (call $mk (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.eqz (ref.test (ref null $A) (local.get $x))))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br $l)))
    (local.get $k))

  ;; a D test and an A test in turn, each way with its own change
  (func (export "mixed") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $x eqref)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $x (call $mk (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (if (ref.test (ref $D) (local.get $x))
          (then (local.set $s (i32.sub (local.get $s) (i32.const 7))))
          (else
            (if (ref.test (ref $A) (local.get $x))
              (then (local.set $s (i32.sub (local.get $s) (i32.const 1))))
              (else (local.set $s (i32.add (local.get $s) (i32.const 2)))))))
        (br $l)))
    (local.get $s))
)
