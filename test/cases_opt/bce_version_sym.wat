;; Loops versioned for accesses the prover cannot tie to the counter (opt.c
;; bv_loop's symbolic groups, bv_sym_index): an index that is the counter
;; plus a value the loop does not change (shift: a[i + off] = a[i] + 1, as
;; Java's StringBuilder.insert shifts its chars; shift2: a[(off + 2) + i]),
;; or a second counter that steps with it (copy2: dst[k++] = src[i++]).
;; The copy, entered where the least index start + off + cmin is not
;; negative and the greatest, m - 1 + off + cmax (m + off + cmax for <=),
;; is below the length -- a second counter's start in place of start + off
;; -- has no checks on them; the original keeps them and traps where it
;; did, after the stores before.  A counter whose next value would pass
;; 2^31 - 1 runs the original: wrapv's i <= 2147483647 wraps to -2^31,
;; where i + off is 10, past a[9] (and with a bound one lower it runs the
;; copy).  The near misses, none copied for the access off the counter:
;; inloop's offset changes in the loop; qstep's second counter steps by 2
;; where the counter steps by 1; rshift's counter steps down (its range
;; is [m, start], which these tests do not take); cminus tests i - 1 < k,
;; under which i reaches k, not k - 1, at the loop's end, and ctop at its
;; top, its header too long to be moved (rotate_one).  The count is of the accesses the
;; copies prove; shift's two checks left are the original's.  Answers
;; worked out in Python.
;; stats -O2: bce_versioned=5 bce_version_sym=4
;; shape: shift b\.hs=2
;; run: shift 10 5 3 => i32:31
;; run: shift 10 7 3 => i32:21
;; run: shift 10 8 3 => trap: out of bounds array access
;; run: shift 10 3 -1 => trap: out of bounds array access
;; run: shift 10 0 50 => i32:45
;; run: shift2 10 5 1 => i32:38
;; run: shift2 10 7 1 => i32:39
;; run: shift2 10 8 1 => trap: out of bounds array access
;; run: shift2 10 4 -2 => i32:57
;; run: shift2 10 4 -3 => trap: out of bounds array access
;; run: copy2 10 10 2 3 5 => i32:143
;; run: copy2 10 10 2 3 8 => trap: out of bounds array access
;; run: copy2 10 10 5 0 5 => i32:116
;; run: copy2 10 10 6 0 5 => trap: out of bounds array access
;; run: copy2 10 10 -1 0 3 => trap: out of bounds array access
;; run: wrapv 2147483640 2147483647 -2147483638 => trap: out of bounds array access
;; run: wrapv 2147483640 2147483646 -2147483638 => i32:35
;; run: inloop 10 4 => i32:18
;; run: inloop 10 5 => trap: out of bounds array access
;; run: rshift 10 5 3 => i32:35
;; run: rshift 10 6 4 => i32:27
;; run: rshift 10 7 4 => trap: out of bounds array access
;; run: cminus 10 5 3 => i32:33
;; run: cminus 10 6 3 => i32:42
;; run: cminus 10 7 3 => trap: out of bounds array access
;; run: qstep 10 5 => i32:70
;; run: ctop 10 5 3 => i32:36
;; run: ctop 10 6 3 => i32:46
;; run: ctop 10 7 3 => trap: out of bounds array access
;; run: qstep 10 6 => trap: out of bounds array access
(module
  (type $B (array (mut i32)))
  ;; n long, a[i] = i
  (func $iota (param $n i32) (result (ref $B))
    (local $a (ref $B)) (local $i i32)
    (local.set $a (array.new_default $B (local.get $n)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $n)))
      (array.set $B (local.get $a) (local.get $i) (local.get $i))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $a))
  (func $sum (param $a (ref $B)) (result i32)
    (local $i i32) (local $s i32)
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (array.len (local.get $a))))
      (local.set $s (i32.add (local.get $s) (array.get $B (local.get $a) (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $s))

  (func $shift (export "shift") (param $n i32) (param $k i32) (param $off i32) (result i32)
    (local $a (ref $B)) (local $i i32)
    (local.set $a (call $iota (local.get $n)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $k)))
      (array.set $B (local.get $a) (i32.add (local.get $i) (local.get $off))
        (i32.add (array.get $B (local.get $a) (local.get $i)) (i32.const 1)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (call $sum (local.get $a)))

  (func $shift2 (export "shift2") (param $n i32) (param $k i32) (param $off i32) (result i32)
    (local $a (ref $B)) (local $i i32)
    (local.set $a (call $iota (local.get $n)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $k)))
      (array.set $B (local.get $a) (i32.add (i32.add (local.get $off) (i32.const 2)) (local.get $i))
        (i32.mul (array.get $B (local.get $a) (local.get $i)) (i32.const 3)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (call $sum (local.get $a)))

  ;; src[i] = i * 7 % 13; dst[k++] = src[i++] for i from s below s + cnt;
  ;; the sum of dst[j] * (j + 1)
  (func $copy2 (export "copy2") (param $n i32) (param $m i32) (param $s i32) (param $d0 i32) (param $cnt i32) (result i32)
    (local $src (ref $B)) (local $dst (ref $B)) (local $i i32) (local $k i32) (local $e i32) (local $r i32)
    (local.set $src (array.new_default $B (local.get $n)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $n)))
      (array.set $B (local.get $src) (local.get $i)
        (i32.rem_u (i32.mul (local.get $i) (i32.const 7)) (i32.const 13)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.set $dst (array.new_default $B (local.get $m)))
    (local.set $i (local.get $s))
    (local.set $k (local.get $d0))
    (local.set $e (i32.add (local.get $s) (local.get $cnt)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $e)))
      (array.set $B (local.get $dst) (local.get $k) (array.get $B (local.get $src) (local.get $i)))
      (local.set $k (i32.add (local.get $k) (i32.const 1)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.set $i (i32.const 0))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $m)))
      (local.set $r (i32.add (local.get $r)
        (i32.mul (array.get $B (local.get $dst) (local.get $i)) (i32.add (local.get $i) (i32.const 1)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $r))

  ;; the sum of a[i + off] for i from start while i <= m, a 10 long
  (func $wrapv (export "wrapv") (param $start i32) (param $m i32) (param $off i32) (result i32)
    (local $a (ref $B)) (local $i i32) (local $s i32)
    (local.set $a (call $iota (i32.const 10)))
    (local.set $i (local.get $start))
    (block $d (loop $l
      (br_if $d (i32.gt_s (local.get $i) (local.get $m)))
      (local.set $s (i32.add (local.get $s)
        (array.get $B (local.get $a) (i32.add (local.get $i) (local.get $off)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $s))

  ;; a[i + off] = a[i] + 1 for i from k - 1 down to 0
  (func $rshift (export "rshift") (param $n i32) (param $k i32) (param $off i32) (result i32)
    (local $a (ref $B)) (local $i i32)
    (local.set $a (call $iota (local.get $n)))
    (local.set $i (i32.sub (local.get $k) (i32.const 1)))
    (block $d (loop $l
      (br_if $d (i32.lt_s (local.get $i) (i32.const 0)))
      (array.set $B (local.get $a) (i32.add (local.get $i) (local.get $off))
        (i32.add (array.get $B (local.get $a) (local.get $i)) (i32.const 1)))
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (br $l)))
    (call $sum (local.get $a)))

  ;; the sum of a[i + off] for i from 0 while i - 1 < k
  (func $cminus (export "cminus") (param $n i32) (param $k i32) (param $off i32) (result i32)
    (local $a (ref $B)) (local $i i32) (local $s i32)
    (local.set $a (call $iota (local.get $n)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (i32.sub (local.get $i) (i32.const 1)) (local.get $k)))
      (local.set $s (i32.add (local.get $s)
        (array.get $B (local.get $a) (i32.add (local.get $i) (local.get $off)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $s))

  ;; the sum of a[i + off] + (t & 1) while i - 1 < k, t a hash of i the
  ;; header makes before its test
  (func $ctop (export "ctop") (param $n i32) (param $k i32) (param $off i32) (result i32)
    (local $a (ref $B)) (local $i i32) (local $s i32) (local $t i32)
    (local.set $a (call $iota (local.get $n)))
    (block $d (loop $l
      (local.set $t (i32.add (i32.xor (i32.mul (local.get $i) (i32.const 3))
                                      (i32.shr_s (local.get $i) (i32.const 1)))
                             (i32.mul (local.get $i) (i32.const 5))))
      (local.set $t (i32.xor (local.get $t) (i32.shl (local.get $t) (i32.const 2))))
      (local.set $t (i32.mul (i32.add (local.get $t) (i32.const 7)) (i32.const 9)))
      (local.set $t (i32.sub (i32.xor (local.get $t) (i32.shr_s (local.get $t) (i32.const 3))) (local.get $i)))
      (br_if $d (i32.ge_s (i32.sub (local.get $i) (i32.const 1)) (local.get $k)))
      (local.set $s (i32.add (local.get $s)
        (i32.add (array.get $B (local.get $a) (i32.add (local.get $i) (local.get $off)))
                 (i32.and (local.get $t) (i32.const 1)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $s))

  ;; a[q] = i, q stepping by 2, for i < k; the sum of a[j] * (j + 1)
  (func $qstep (export "qstep") (param $n i32) (param $k i32) (result i32)
    (local $a (ref $B)) (local $i i32) (local $q i32) (local $r i32)
    (local.set $a (array.new_default $B (local.get $n)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $k)))
      (array.set $B (local.get $a) (local.get $q) (local.get $i))
      (local.set $q (i32.add (local.get $q) (i32.const 2)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.set $i (i32.const 0))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $n)))
      (local.set $r (i32.add (local.get $r)
        (i32.mul (array.get $B (local.get $a) (local.get $i)) (i32.add (local.get $i) (i32.const 1)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $r))

  ;; the sum of a[i + x] for i < k, x stepping by 2
  (func $inloop (export "inloop") (param $n i32) (param $k i32) (result i32)
    (local $a (ref $B)) (local $i i32) (local $x i32) (local $s i32)
    (local.set $a (call $iota (local.get $n)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $k)))
      (local.set $s (i32.add (local.get $s)
        (array.get $B (local.get $a) (i32.add (local.get $i) (local.get $x)))))
      (local.set $x (i32.add (local.get $x) (i32.const 2)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $s))
)
