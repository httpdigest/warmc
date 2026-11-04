;; Loops copied behind a test of their bounds (opt.c pass_bce_version): a
;; counter from start tested against an m the loop does not change, and
;; arrays it does not change read and written at the counter plus a
;; constant.  The copy, entered where each array is no null, m is at most
;; its length less the greatest constant, start plus the least is not
;; negative (and start passes the loop's test, of a test at the loop's
;; end), has no checks; the original, entered where a test fails, keeps
;; them and traps where it did -- after the iterations before, whose
;; stores are made: copy's destination is read back after a trap no more,
;; but its sum is, so each m past the end must trap and each m within must
;; not.  A do-while entered with start past m runs once, through the
;; original.  The count is of the loops copied.
;; stats -O2: bce_versioned=10
;; run: sum 10 10 => i32:45
;; run: sum 10 4 => i32:6
;; run: sum 10 0 => i32:0
;; run: sum 10 11 => trap: out of bounds array access
;; run: sumnull 3 => trap: null reference
;; run: sumnull 0 => i32:0
;; run: copy 10 10 => i32:45
;; run: copy 10 3 => i32:3
;; run: copy 10 12 => trap: out of bounds array access
;; run: window 10 9 => i32:81
;; run: window 10 10 => trap: out of bounds array access
;; run: from 10 2 => i32:44
;; run: from 10 -1 => trap: out of bounds array access
;; run: dowhile 10 0 5 => i32:10
;; run: dowhile 10 7 3 => i32:7
;; run: dowhile 10 10 3 => trap: out of bounds array access
;; run: unsigned 10 10 => i32:45
;; run: unsigned 10 -1 => trap: out of bounds array access
;; run: wide 10 10 => i64:45
;; run: wide 10 11 => trap: out of bounds array access
(module
  (type $B (array (mut i32)))
  (func $mk (param $n i32) (result (ref $B))
    (local $a (ref $B)) (local $i i32)
    (local.set $a (array.new_default $B (local.get $n)))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (array.set $B (local.get $a) (local.get $i) (local.get $i))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $a))

  ;; a[0] + .. + a[m - 1]
  (func $sum (param $a (ref null $B)) (param $m i32) (result i32)
    (local $i i32) (local $s i32)
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $m)))
      (local.set $s (i32.add (local.get $s) (array.get $B (local.get $a) (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $s))
  (func (export "sum") (param $n i32) (param $m i32) (result i32)
    (call $sum (call $mk (local.get $n)) (local.get $m)))
  ;; a null array: the original, which traps once it reads
  (func (export "sumnull") (param $m i32) (result i32)
    (call $sum (ref.null $B) (local.get $m)))

  ;; d[i] = s[i] for i < m
  (func $copy (param $d (ref null $B)) (param $s (ref null $B)) (param $m i32)
    (local $i i32)
    (block $x (loop $l
      (br_if $x (i32.ge_s (local.get $i) (local.get $m)))
      (array.set $B (local.get $d) (local.get $i) (array.get $B (local.get $s) (local.get $i)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
  )
  (func (export "copy") (param $n i32) (param $m i32) (result i32)
    (local $d (ref $B))
    (local.set $d (array.new_default $B (local.get $n)))
    (call $copy (local.get $d) (call $mk (local.get $n)) (local.get $m))
    (call $sum (local.get $d) (local.get $m)))

  ;; a[i] + a[i + 1] for i < m: m at most the length less 1
  (func (export "window") (param $n i32) (param $m i32) (result i32)
    (local $a (ref $B)) (local $i i32) (local $s i32)
    (local.set $a (call $mk (local.get $n)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $m)))
      (local.set $s (i32.add (local.get $s)
        (i32.add (array.get $B (local.get $a) (local.get $i))
                 (array.get $B (local.get $a) (i32.add (local.get $i) (i32.const 1))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $s))

  ;; from start (of any sign) to the length
  (func (export "from") (param $n i32) (param $start i32) (result i32)
    (local $a (ref $B)) (local $i i32) (local $s i32) (local $m i32)
    (local.set $a (call $mk (local.get $n)))
    (local.set $m (local.get $n))
    (local.set $i (local.get $start))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $m)))
      (local.set $s (i32.add (local.get $s) (array.get $B (local.get $a) (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $s))

  ;; do { s += a[i]; i++ } while (i < m), from start
  (func (export "dowhile") (param $n i32) (param $start i32) (param $m i32) (result i32)
    (local $a (ref $B)) (local $i i32) (local $s i32)
    (local.set $a (call $mk (local.get $n)))
    (local.set $i (local.get $start))
    (loop $l
      (local.set $s (i32.add (local.get $s) (array.get $B (local.get $a) (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_s (local.get $i) (local.get $m))))
    (local.get $s))

  ;; i <u m: an m that is negative is past every length
  (func (export "unsigned") (param $n i32) (param $m i32) (result i32)
    (local $a (ref $B)) (local $i i32) (local $s i32)
    (local.set $a (call $mk (local.get $n)))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $m)))
      (local.set $s (i32.add (local.get $s) (array.get $B (local.get $a) (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $s))

  ;; an i64 counter, the index its wrap, as luawasm's are
  (func (export "wide") (param $n i32) (param $m i32) (result i64)
    (local $a (ref $B)) (local $i i64) (local $s i64) (local $mm i64)
    (local.set $a (call $mk (local.get $n)))
    (local.set $mm (i64.extend_i32_s (local.get $m)))
    (block $d (loop $l
      (br_if $d (i64.ge_s (local.get $i) (local.get $mm)))
      (local.set $s (i64.add (local.get $s)
        (i64.extend_i32_s (array.get $B (local.get $a) (i32.wrap_i64 (local.get $i))))))
      (local.set $i (i64.add (local.get $i) (i64.const 1)))
      (br $l)))
    (local.get $s))
)
