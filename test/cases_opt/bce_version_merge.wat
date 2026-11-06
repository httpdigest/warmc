;; Loops versioned for counters that step up in one arm and stay in the
;; other (opt.c bv_mono), each its own job, the copy entered where every
;; job's tests pass -- Java's merge, while (i < mid && j < hi), a[i] and
;; a[j] -- and for arrays read anew each time round that are the same
;; array every time: the data field of an object the loop does not change
;; (bv_field_array), read past the loop's tests, whose null check keeps
;; LICM from taking the read out.  The field is mutable, as TeaVM's
;; arrays' is, but no struct.set in the module writes it, so it keeps
;; what struct.new put there (compile.c module_field_writes).  The
;; objects come through call_indirect, which the compiler does not see
;; into, as a Java method's arguments do.  Near misses: merge2's wrapper
;; has its field written (swap2), so its reads are no one array, as are
;; merge3's, whose field a subtype's struct.set writes (swapc), and
;; merge2b's, whose type is $W2 by its structure; downup's
;; counter steps down in one arm; jump3's steps up by 1 or 3, tested at
;; the loop's end on its old value -- neither copied for its counter.
;; Every read past the end traps, each test failing sending the run to the
;; original.  The count is of the loops copied; merge's checks left are its
;; original's and its copy's of t[k] (k no counter tested).  Answers worked
;; out in Python.
;; stats -O2: bce_versioned=5
;; shape: merge b\.hs=7
;; run: merge 20 0 10 20 => i32:19790
;; run: merge 20 2 9 17 => i32:14783
;; run: merge 20 0 10 21 => i32:19790
;; run: merge 20 0 5 20 => i32:5576
;; run: merge 20 3 8 30 => i32:9383
;; run: merge 20 0 25 30 => trap: out of bounds array access
;; run: merge 20 15 22 24 => trap: out of bounds array access
;; run: merge 20 -1 5 10 => trap: out of bounds array access
;; run: merge 20 5 5 5 => i32:5005
;; run: merge2 20 0 10 20 => i32:19790
;; run: merge2 20 15 22 24 => trap: out of bounds array access
;; run: merge3 20 0 10 20 => i32:19790
;; run: merge3 20 15 22 24 => trap: out of bounds array access
;; run: merge2b 20 0 10 20 => i32:19790
;; run: merge2b 20 15 22 24 => trap: out of bounds array access
;; run: swapc 5 => i32:6
;; run: downup 20 3 15 30 => i32:11467
;; run: downup 20 0 15 30 => trap: out of bounds array access
;; run: downup 20 1 15 30 => i32:11656
;; run: jump3 6 6 => trap: out of bounds array access
;; run: jump3 6 5 => i32:259
;; run: jump3 11 6 => i32:433
(module
  (type $D (array (mut i32)))
  (type $W (struct (field $data (mut (ref $D)))))
  (type $W2 (struct (field $data (mut (ref $D))) (field $tag i32)))
  ;; a supertype whose data a subtype's struct.set writes, and a twin of
  ;; $W2, the same type by its structure, whose data swap2 writes
  (type $Wp (sub (struct (field $data (mut (ref $D))))))
  (type $Wc (sub $Wp (struct (field $data (mut (ref $D))) (field $x i32))))
  (type $W2b (struct (field $data (mut (ref $D))) (field $tag i32)))
  (type $mkw (func (param i32) (result (ref null $W))))
  (type $mkp (func (param i32) (result (ref null $Wp))))
  (type $mkw2b (func (param i32) (result (ref null $W2b))))
  (type $mkw2 (func (param i32) (result (ref null $W2))))
  (table $t 2 funcref)
  (elem (table $t) (i32.const 0) func $mk $mk)
  (table $t2 2 funcref)
  (elem (table $t2) (i32.const 0) func $mk2 $mk2)
  (table $t3 2 funcref)
  (elem (table $t3) (i32.const 0) func $mkp $mkp)
  (table $t4 2 funcref)
  (elem (table $t4) (i32.const 0) func $mk2b $mk2b)

  ;; n long, element q (q * 37) % 101
  (func $fill (param $n i32) (result (ref $D))
    (local $a (ref $D)) (local $q i32)
    (local.set $a (array.new_default $D (local.get $n)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $q) (local.get $n)))
      (array.set $D (local.get $a) (local.get $q)
        (i32.rem_u (i32.mul (local.get $q) (i32.const 37)) (i32.const 101)))
      (local.set $q (i32.add (local.get $q) (i32.const 1)))
      (br $l)))
    (local.get $a))
  (func $mk (param $n i32) (result (ref null $W))
    (struct.new $W (call $fill (local.get $n))))
  (func $mk2 (param $n i32) (result (ref null $W2))
    (struct.new $W2 (call $fill (local.get $n)) (i32.const 0)))
  (func $mkp (param $n i32) (result (ref null $Wp))
    (struct.new $Wp (call $fill (local.get $n))))
  (func $mk2b (param $n i32) (result (ref null $W2b))
    (struct.new $W2b (call $fill (local.get $n)) (i32.const 0)))
  ;; the one write of a $W2's data, and of a $Wc's
  (func $swap2 (param $w (ref $W2)) (param $n i32)
    (struct.set $W2 $data (local.get $w) (call $fill (local.get $n))))
  (func $swapc (export "swapc") (param $n i32) (result i32)
    (local $c (ref $Wc))
    (local.set $c (struct.new $Wc (call $fill (local.get $n)) (i32.const 1)))
    (struct.set $Wc $data (local.get $c) (call $fill (i32.add (local.get $n) (i32.const 1))))
    (array.len (struct.get $Wc $data (local.get $c))))

  ;; t[k++] = the less of w[i], w[j] (i++ or j++) while i < mid and j <
  ;; hi; the sum of t[q] * (q + 1), and i * 1000 + j
  (func $merge (export "merge") (param $n i32) (param $lo i32) (param $mid i32) (param $hi i32) (result i32)
    (local $w (ref null $W)) (local $t (ref null $W)) (local $i i32) (local $j i32) (local $k i32)
    (local $x i32) (local $y i32) (local $s i32) (local $q i32)
    (local.set $w (call_indirect $t (type $mkw) (local.get $n) (i32.shr_u (local.get $n) (i32.const 30))))
    (local.set $t (call_indirect $t (type $mkw) (local.get $n) (i32.shr_u (local.get $n) (i32.const 30))))
    (array.fill $D (struct.get $W $data (local.get $t)) (i32.const 0) (i32.const 0) (local.get $n))
    (local.set $i (local.get $lo))
    (local.set $j (local.get $mid))
    (local.set $k (local.get $lo))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $mid)))
      (br_if $d (i32.ge_s (local.get $j) (local.get $hi)))
      (local.set $x (array.get $D (struct.get $W $data (local.get $w)) (local.get $i)))
      (local.set $y (array.get $D (struct.get $W $data (local.get $w)) (local.get $j)))
      (if (i32.le_s (local.get $x) (local.get $y))
        (then
          (array.set $D (struct.get $W $data (local.get $t)) (local.get $k) (local.get $x))
          (local.set $i (i32.add (local.get $i) (i32.const 1))))
        (else
          (array.set $D (struct.get $W $data (local.get $t)) (local.get $k) (local.get $y))
          (local.set $j (i32.add (local.get $j) (i32.const 1)))))
      (local.set $k (i32.add (local.get $k) (i32.const 1)))
      (br $l)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $q) (local.get $n)))
      (local.set $s (i32.add (local.get $s)
        (i32.mul (array.get $D (struct.get $W $data (local.get $t)) (local.get $q))
                 (i32.add (local.get $q) (i32.const 1)))))
      (local.set $q (i32.add (local.get $q) (i32.const 1)))
      (br $l)))
    (i32.add (local.get $s) (i32.add (i32.mul (local.get $i) (i32.const 1000)) (local.get $j))))

  ;; the same through $W2, whose data swap2 writes (here with the same
  ;; values, where hi is 0)
  (func $merge2 (export "merge2") (param $n i32) (param $lo i32) (param $mid i32) (param $hi i32) (result i32)
    (local $w (ref null $W2)) (local $t (ref null $W2)) (local $i i32) (local $j i32) (local $k i32)
    (local $x i32) (local $y i32) (local $s i32) (local $q i32)
    (local.set $w (call_indirect $t2 (type $mkw2) (local.get $n) (i32.shr_u (local.get $n) (i32.const 30))))
    (local.set $t (call_indirect $t2 (type $mkw2) (local.get $n) (i32.shr_u (local.get $n) (i32.const 30))))
    (if (i32.eqz (local.get $hi)) (then (call $swap2 (ref.as_non_null (local.get $w)) (local.get $n))))
    (array.fill $D (struct.get $W2 $data (local.get $t)) (i32.const 0) (i32.const 0) (local.get $n))
    (local.set $i (local.get $lo))
    (local.set $j (local.get $mid))
    (local.set $k (local.get $lo))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $mid)))
      (br_if $d (i32.ge_s (local.get $j) (local.get $hi)))
      (local.set $x (array.get $D (struct.get $W2 $data (local.get $w)) (local.get $i)))
      (local.set $y (array.get $D (struct.get $W2 $data (local.get $w)) (local.get $j)))
      (if (i32.le_s (local.get $x) (local.get $y))
        (then
          (array.set $D (struct.get $W2 $data (local.get $t)) (local.get $k) (local.get $x))
          (local.set $i (i32.add (local.get $i) (i32.const 1))))
        (else
          (array.set $D (struct.get $W2 $data (local.get $t)) (local.get $k) (local.get $y))
          (local.set $j (i32.add (local.get $j) (i32.const 1)))))
      (local.set $k (i32.add (local.get $k) (i32.const 1)))
      (br $l)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $q) (local.get $n)))
      (local.set $s (i32.add (local.get $s)
        (i32.mul (array.get $D (struct.get $W2 $data (local.get $t)) (local.get $q))
                 (i32.add (local.get $q) (i32.const 1)))))
      (local.set $q (i32.add (local.get $q) (i32.const 1)))
      (br $l)))
    (i32.add (local.get $s) (i32.add (i32.mul (local.get $i) (i32.const 1000)) (local.get $j))))

  (func $merge3 (export "merge3") (param $n i32) (param $lo i32) (param $mid i32) (param $hi i32) (result i32)
    (local $w (ref null $Wp)) (local $t (ref null $Wp)) (local $i i32) (local $j i32) (local $k i32)
    (local $x i32) (local $y i32) (local $s i32) (local $q i32)
    (local.set $w (call_indirect $t3 (type $mkp) (local.get $n) (i32.shr_u (local.get $n) (i32.const 30))))
    (local.set $t (call_indirect $t3 (type $mkp) (local.get $n) (i32.shr_u (local.get $n) (i32.const 30))))
    (array.fill $D (struct.get $Wp $data (local.get $t)) (i32.const 0) (i32.const 0) (local.get $n))
    (local.set $i (local.get $lo))
    (local.set $j (local.get $mid))
    (local.set $k (local.get $lo))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $mid)))
      (br_if $d (i32.ge_s (local.get $j) (local.get $hi)))
      (local.set $x (array.get $D (struct.get $Wp $data (local.get $w)) (local.get $i)))
      (local.set $y (array.get $D (struct.get $Wp $data (local.get $w)) (local.get $j)))
      (if (i32.le_s (local.get $x) (local.get $y))
        (then
          (array.set $D (struct.get $Wp $data (local.get $t)) (local.get $k) (local.get $x))
          (local.set $i (i32.add (local.get $i) (i32.const 1))))
        (else
          (array.set $D (struct.get $Wp $data (local.get $t)) (local.get $k) (local.get $y))
          (local.set $j (i32.add (local.get $j) (i32.const 1)))))
      (local.set $k (i32.add (local.get $k) (i32.const 1)))
      (br $l)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $q) (local.get $n)))
      (local.set $s (i32.add (local.get $s)
        (i32.mul (array.get $D (struct.get $Wp $data (local.get $t)) (local.get $q))
                 (i32.add (local.get $q) (i32.const 1)))))
      (local.set $q (i32.add (local.get $q) (i32.const 1)))
      (br $l)))
    (i32.add (local.get $s) (i32.add (i32.mul (local.get $i) (i32.const 1000)) (local.get $j))))

  (func $merge2b (export "merge2b") (param $n i32) (param $lo i32) (param $mid i32) (param $hi i32) (result i32)
    (local $w (ref null $W2b)) (local $t (ref null $W2b)) (local $i i32) (local $j i32) (local $k i32)
    (local $x i32) (local $y i32) (local $s i32) (local $q i32)
    (local.set $w (call_indirect $t4 (type $mkw2b) (local.get $n) (i32.shr_u (local.get $n) (i32.const 30))))
    (local.set $t (call_indirect $t4 (type $mkw2b) (local.get $n) (i32.shr_u (local.get $n) (i32.const 30))))
    (array.fill $D (struct.get $W2b $data (local.get $t)) (i32.const 0) (i32.const 0) (local.get $n))
    (local.set $i (local.get $lo))
    (local.set $j (local.get $mid))
    (local.set $k (local.get $lo))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $mid)))
      (br_if $d (i32.ge_s (local.get $j) (local.get $hi)))
      (local.set $x (array.get $D (struct.get $W2b $data (local.get $w)) (local.get $i)))
      (local.set $y (array.get $D (struct.get $W2b $data (local.get $w)) (local.get $j)))
      (if (i32.le_s (local.get $x) (local.get $y))
        (then
          (array.set $D (struct.get $W2b $data (local.get $t)) (local.get $k) (local.get $x))
          (local.set $i (i32.add (local.get $i) (i32.const 1))))
        (else
          (array.set $D (struct.get $W2b $data (local.get $t)) (local.get $k) (local.get $y))
          (local.set $j (i32.add (local.get $j) (i32.const 1)))))
      (local.set $k (i32.add (local.get $k) (i32.const 1)))
      (br $l)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $q) (local.get $n)))
      (local.set $s (i32.add (local.get $s)
        (i32.mul (array.get $D (struct.get $W2b $data (local.get $t)) (local.get $q))
                 (i32.add (local.get $q) (i32.const 1)))))
      (local.set $q (i32.add (local.get $q) (i32.const 1)))
      (br $l)))
    (i32.add (local.get $s) (i32.add (i32.mul (local.get $i) (i32.const 1000)) (local.get $j))))

  ;; the sum of w[i], i stepping up where w[i] is odd and down where it is
  ;; even, while i < m, for steps rounds; the sum * 7 + i
  (func $downup (export "downup") (param $n i32) (param $lo i32) (param $m i32) (param $steps i32) (result i32)
    (local $w (ref null $W)) (local $i i32) (local $c i32) (local $x i32) (local $s i32)
    (local.set $w (call_indirect $t (type $mkw) (local.get $n) (i32.shr_u (local.get $n) (i32.const 30))))
    (local.set $i (local.get $lo))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $c) (local.get $steps)))
      (br_if $d (i32.ge_s (local.get $i) (local.get $m)))
      (local.set $x (array.get $D (struct.get $W $data (local.get $w)) (local.get $i)))
      (local.set $s (i32.add (local.get $s) (local.get $x)))
      (if (i32.and (local.get $x) (i32.const 1))
        (then (local.set $i (i32.add (local.get $i) (i32.const 1))))
        (else (local.set $i (i32.sub (local.get $i) (i32.const 1)))))
      (local.set $c (i32.add (local.get $c) (i32.const 1)))
      (br $l)))
    (i32.add (i32.mul (local.get $s) (i32.const 7)) (local.get $i)))

  ;; the sum of w[i], i stepping by 3 where w[i] is odd and by 1 where it
  ;; is even, while its old value + 1 < m; the sum * 3 + i
  (func $jump3 (export "jump3") (param $n i32) (param $m i32) (result i32)
    (local $w (ref null $W)) (local $i i32) (local $o i32) (local $x i32) (local $s i32)
    (local.set $w (call_indirect $t (type $mkw) (local.get $n) (i32.shr_u (local.get $n) (i32.const 30))))
    (loop $l
      (local.set $x (array.get $D (struct.get $W $data (local.get $w)) (local.get $i)))
      (local.set $s (i32.add (local.get $s) (local.get $x)))
      (local.set $o (local.get $i))
      (if (i32.and (local.get $x) (i32.const 1))
        (then (local.set $i (i32.add (local.get $i) (i32.const 3))))
        (else (local.set $i (i32.add (local.get $i) (i32.const 1)))))
      (br_if $l (i32.lt_s (i32.add (local.get $o) (i32.const 1)) (local.get $m))))
    (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $i)))
)
