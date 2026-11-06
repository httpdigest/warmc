;; Loops versioned inside a versioned loop (opt.c pass_bce_version): of a
;; loop copied behind the tests of its bounds, the loops inside the copy
;; are versioned in turn -- the tests of the outer loop say nothing of an
;; inner loop's own counter -- where the original, run where a test fails,
;; keeps its loops as they are (it is marked cold), and a loop around a
;; versioned one is not copied again.  nested reads row i of rows at j,
;; rows of lengths L + R - 1 - i: the inner copy runs for a row as long as
;; m, the inner original for a shorter one, and traps at it, after the
;; rows before.  mat is Java's matrix multiply: rows of a, b and c read
;; in the i and k loops, a row of c and of b in the j loop, three deep.
;; The count is of the loops copied: nested's two; mat's i loop, the k loop
;; in its copy and the j loop in the k loop's copy; and the loop over a row
;; of c in the sum after (the loop over c's rows needs none: c is made N
;; long).  Answers worked out in Python.
;; stats -O2: bce_versioned=6
;; run: nested 4 5 4 5 => i32:-1904129701
;; run: nested 4 5 4 3 => i32:366059
;; run: nested 4 5 0 9 => i32:0
;; run: nested 4 5 3 6 => i32:263922085
;; run: nested 4 5 4 6 => trap: out of bounds array access
;; run: nested 4 5 2 7 => i32:3257787
;; run: nested 4 5 3 7 => trap: out of bounds array access
;; run: nested 4 5 4 9 => trap: out of bounds array access
;; run: nested 4 5 5 1 => trap: out of bounds array access
;; run: nested 3 1 3 1 => i32:24
;; run: nested 3 1 3 2 => trap: out of bounds array access
;; run: mat 6 6 => i32:-1743341341
;; run: mat 6 4 => i32:-620251816
;; run: mat 6 0 => i32:0
;; run: mat 6 7 => trap: out of bounds array access
(module
  (type $B (array (mut i32)))
  (type $R (array (mut (ref null $B))))

  ;; R rows, row i of length L + R - 1 - i, element j (i * 7 + j * 3) % 11
  (func $rows (param $R i32) (param $L i32) (result (ref $R))
    (local $rs (ref $R)) (local $row (ref $B)) (local $i i32) (local $j i32) (local $len i32)
    (local.set $rs (array.new_default $R (local.get $R)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $R)))
      (local.set $len (i32.add (local.get $L)
        (i32.sub (i32.sub (local.get $R) (i32.const 1)) (local.get $i))))
      (local.set $row (array.new_default $B (local.get $len)))
      (local.set $j (i32.const 0))
      (block $d2 (loop $l2
        (br_if $d2 (i32.ge_s (local.get $j) (local.get $len)))
        (array.set $B (local.get $row) (local.get $j)
          (i32.rem_u (i32.add (i32.mul (local.get $i) (i32.const 7))
                              (i32.mul (local.get $j) (i32.const 3)))
                     (i32.const 11)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l2)))
      (array.set $R (local.get $rs) (local.get $i) (local.get $row))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $rs))

  ;; N x N, element (i, j) (i * a + j * b) % m
  (func $square (param $N i32) (param $a i32) (param $b i32) (param $m i32) (result (ref $R))
    (local $rs (ref $R)) (local $row (ref $B)) (local $i i32) (local $j i32)
    (local.set $rs (array.new_default $R (local.get $N)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $N)))
      (local.set $row (array.new_default $B (local.get $N)))
      (local.set $j (i32.const 0))
      (block $d2 (loop $l2
        (br_if $d2 (i32.ge_s (local.get $j) (local.get $N)))
        (array.set $B (local.get $row) (local.get $j)
          (i32.rem_u (i32.add (i32.mul (local.get $i) (local.get $a))
                              (i32.mul (local.get $j) (local.get $b)))
                     (local.get $m)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l2)))
      (array.set $R (local.get $rs) (local.get $i) (local.get $row))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $rs))

  (func (export "nested") (param $R i32) (param $L i32) (param $n i32) (param $m i32) (result i32)
    (local $rs (ref $R)) (local $row (ref null $B)) (local $i i32) (local $j i32) (local $s i32)
    (local.set $rs (call $rows (local.get $R) (local.get $L)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $n)))
      (local.set $row (array.get $R (local.get $rs) (local.get $i)))
      (local.set $j (i32.const 0))
      (block $d2 (loop $l2
        (br_if $d2 (i32.ge_s (local.get $j) (local.get $m)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3))
                               (array.get $B (local.get $row) (local.get $j))))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l2)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $s))

  (func (export "mat") (param $N i32) (param $n i32) (result i32)
    (local $a (ref $R)) (local $b (ref $R)) (local $c (ref $R))
    (local $ai (ref null $B)) (local $ci (ref null $B)) (local $bk (ref null $B))
    (local $i i32) (local $j i32) (local $k i32) (local $aik i32) (local $s i32)
    (local.set $a (call $square (local.get $N) (i32.const 7) (i32.const 3) (i32.const 11)))
    (local.set $b (call $square (local.get $N) (i32.const 5) (i32.const 2) (i32.const 13)))
    (local.set $c (call $square (local.get $N) (i32.const 0) (i32.const 0) (i32.const 1)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $n)))
      (local.set $ai (array.get $R (local.get $a) (local.get $i)))
      (local.set $ci (array.get $R (local.get $c) (local.get $i)))
      (local.set $k (i32.const 0))
      (block $dk (loop $lk
        (br_if $dk (i32.ge_s (local.get $k) (local.get $n)))
        (local.set $aik (array.get $B (local.get $ai) (local.get $k)))
        (local.set $bk (array.get $R (local.get $b) (local.get $k)))
        (local.set $j (i32.const 0))
        (block $dj (loop $lj
          (br_if $dj (i32.ge_s (local.get $j) (local.get $n)))
          (array.set $B (local.get $ci) (local.get $j)
            (i32.add (array.get $B (local.get $ci) (local.get $j))
                     (i32.mul (local.get $aik) (array.get $B (local.get $bk) (local.get $j)))))
          (local.set $j (i32.add (local.get $j) (i32.const 1)))
          (br $lj)))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br $lk)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    ;; every element of c, row by row
    (local.set $i (i32.const 0))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $N)))
      (local.set $ci (array.get $R (local.get $c) (local.get $i)))
      (local.set $j (i32.const 0))
      (block $d2 (loop $l2
        (br_if $d2 (i32.ge_s (local.get $j) (local.get $N)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
                               (array.get $B (local.get $ci) (local.get $j))))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l2)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $s))
)
