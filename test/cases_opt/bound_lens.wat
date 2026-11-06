;; Bounds checks no proof removes, given their array's length as an operand
;; (opt.c pass_bound_lens, GCF_LENARG) where it is known without a load at
;; the check: an array of a length its allocation says compares with the
;; constant (known: r of 8, read and written at (k + i) & 15); an array not
;; null that the loop does not change has one array.len before the loop
;; (hoisted, the outermost loop it is the same in: nest); an array.len
;; made before serves the check after it (dom).  The near miss: an array
;; that may be null is not taken out of the loop -- its length, loaded
;; before the loop, would read a null (nullable).  An immutable global's
;; array has the length its initialiser makes (opt.c array_known_len): a
;; read of pool at 3 needs no check, at k the constant 10; a mutable
;; global's does not keep it -- mglob2 reads one of 2 a call set (an
;; array.new_fixed's length is its count: fixedg).
;; Not an array.len that the access does not come after: one in an arm the
;; access's block is not in, one after it in its block (dom2); nor one
;; before a loop of an array made in the loop around it (inner, whose
;; length is made before the inner loop alone); nor one after a call that
;; can raise, which ends its block (eh: the call's block the loop's way
;; in); nor one in a loop's one way in from before it that is not over the
;; access -- irr's two states jump threading makes one loop entered in
;; either, which luawasm's nbody read an unmade length in: the length
;; goes in a block over the access, the entry.  Every read past the end traps.  The counts are of the checks
;; given a length, and of the array.lens made in a preheader, at -O2.
;; stats -O2: bound_lens=44 bound_lens_hoisted=6
;; run: known 8 0 => i32:1
;; run: known 9 0 => trap: out of bounds array access
;; run: known 5 3 => i32:1
;; run: known 6 3 => trap: out of bounds array access
;; run: known 0 12 => i32:0
;; run: hoisted 16 20 0 => i32:40
;; run: hoisted 8 8 0 => i32:16
;; run: hoisted 8 9 0 => trap: out of bounds array access
;; run: hoisted 16 3 13 => i32:6
;; run: hoisted 15 3 13 => trap: out of bounds array access
;; run: nest 16 4 5 => i32:112
;; run: nest 8 4 1 => i32:20
;; run: nest 4 4 5 => trap: out of bounds array access
;; run: nullable 16 20 0 => i32:40
;; run: nullable 8 9 0 => trap: out of bounds array access
;; run: nullable -1 1 0 => trap: null reference
;; run: nullable -1 0 0 => i32:0
;; run: dom 4 3 => i32:5
;; run: dom 4 4 => trap: out of bounds array access
;; run: dom 0 0 => i32:-1
;; run: glob 9 => i32:14
;; run: glob 10 => trap: out of bounds array access
;; run: mglob 1 => i32:1
;; run: mglob 2 => trap: out of bounds array access
;; run: mglob2 1 => i32:1
;; run: mglob2 2 => trap: out of bounds array access
;; run: fixedg 2 => i32:3
;; run: fixedg 3 => trap: out of bounds array access
;; run: dom2 4 3 0 => i32:9
;; run: dom2 4 3 1 => i32:13
;; run: dom2 4 4 1 => trap: out of bounds array access
;; run: inner 10 3 4 => i32:24
;; run: inner 9 3 4 => trap: out of bounds array access
;; run: inner 4 3 2 => i32:12
;; run: eh 16 4 => i32:12
;; run: eh 6 2 => i32:6
;; run: eh 5 2 => trap: out of bounds array access
;; run: irr 16 5 0 => i32:3030
;; run: irr 16 5 1 => i32:3020
;; run: irr 8 3 0 => i32:3018
;; run: irr 8 4 1 => trap: out of bounds array access
;; run: irr 8 0 1 => i32:3004
(module
  (type $a (array (mut i32)))
  (type $u (func (param i32) (result i32)))
  (table 2 funcref)
  (elem (i32.const 0) $twice $twice)
  (type $v (func))
  (table $sets 1 funcref)
  (elem (table $sets) (i32.const 0) func $set2)
  (global $r (mut i32) (i32.const 0))
  (global $pool (ref $a) (array.new $a (i32.const 7) (i32.const 10)))
  (global $mpool (mut (ref $a)) (array.new $a (i32.const 7) (i32.const 10)))
  (global $mpool2 (mut (ref $a)) (array.new $a (i32.const 7) (i32.const 10)))
  (global $fixed (ref $a) (array.new_fixed $a 3 (i32.const 1) (i32.const 2) (i32.const 3)))

  ;; r[(k + i) & 15] += i for i < n; r[(k + 1) & 7]
  (func $known (export "known") (param $n i32) (param $k i32) (result i32)
    (local $r (ref $a)) (local $i i32) (local $x i32)
    (local.set $r (array.new_default $a (i32.const 8)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $n)))
      (local.set $x (i32.and (i32.add (local.get $k) (local.get $i)) (i32.const 15)))
      (array.set $a (local.get $r) (local.get $x)
        (i32.add (array.get $a (local.get $r) (local.get $x)) (local.get $i)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (array.get $a (local.get $r) (i32.and (i32.add (local.get $k) (i32.const 1)) (i32.const 7))))

  ;; the sum of a[(k + i) & 15] for i < n, a m long of 2s
  (func $hoisted (export "hoisted") (param $m i32) (param $n i32) (param $k i32) (result i32)
    (local $a (ref $a)) (local $i i32) (local $s i32)
    (local.set $a (array.new $a (i32.const 2) (local.get $m)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $n)))
      (local.set $s (i32.add (local.get $s)
        (array.get $a (local.get $a) (i32.and (i32.add (local.get $k) (local.get $i)) (i32.const 15)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $s))

  ;; the same n times over, and a loop of j reads after it: a's one
  ;; array.len before the outer loop, for both
  (func $nest (export "nest") (param $m i32) (param $n i32) (param $k i32) (result i32)
    (local $a (ref $a)) (local $i i32) (local $j i32) (local $s i32)
    (local.set $a (array.new $a (i32.const 2) (local.get $m)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $j) (local.get $n)))
      (local.set $i (i32.const 0))
      (block $d2 (loop $l2
        (br_if $d2 (i32.ge_s (local.get $i) (local.get $k)))
        (local.set $s (i32.add (local.get $s)
          (array.get $a (local.get $a)
            (i32.and (i32.add (i32.mul (local.get $j) (local.get $k)) (local.get $i)) (i32.const 15)))))
        (local.set $s (i32.add (local.get $s) (i32.mul (local.get $i) (local.get $j))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l2)))
      ;; and a[(j + i) & 15] for i < j, with the same length
      (local.set $i (i32.const 0))
      (block $d3 (loop $l3
        (br_if $d3 (i32.ge_s (local.get $i) (local.get $j)))
        (local.set $s (i32.add (local.get $s)
          (array.get $a (local.get $a) (i32.and (i32.add (local.get $j) (local.get $i)) (i32.const 15)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l3)))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br $l)))
    (local.get $s))

  ;; a null where m < 0
  (func $nullable (export "nullable") (param $m i32) (param $n i32) (param $k i32) (result i32)
    (local $a (ref null $a)) (local $i i32) (local $s i32)
    (if (i32.ge_s (local.get $m) (i32.const 0))
      (then (local.set $a (array.new $a (i32.const 2) (local.get $m)))))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $n)))
      (local.set $s (i32.add (local.get $s)
        (array.get $a (local.get $a) (i32.and (i32.add (local.get $k) (local.get $i)) (i32.const 15)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $s))

  ;; -1 for an empty a, else a[k]
  (func $dom (export "dom") (param $m i32) (param $k i32) (result i32)
    (local $a (ref $a))
    (local.set $a (array.new $a (i32.const 5) (local.get $m)))
    (if (i32.eqz (array.len (local.get $a))) (then (return (i32.const -1))))
    (array.get $a (local.get $a) (local.get $k)))

  (func $glob (export "glob") (param $k i32) (result i32)
    (i32.add (array.get $a (global.get $pool) (i32.const 3))
             (array.get $a (global.get $pool) (local.get $k))))

  ;; a[k] + a.length, and + a.length where c, once more
  (func $dom2 (export "dom2") (param $m i32) (param $k i32) (param $c i32) (result i32)
    (local $a (ref $a)) (local $s i32)
    (local.set $a (array.new $a (i32.const 5) (local.get $m)))
    (if (local.get $c) (then (local.set $s (array.len (local.get $a)))))
    (i32.add (local.get $s)
      (i32.add (array.get $a (local.get $a) (local.get $k)) (array.len (local.get $a)))))

  ;; for j < n an a m + j long of 2s, the sum of a[(i * 3) & 15] for i < k
  (func $inner (export "inner") (param $m i32) (param $n i32) (param $k i32) (result i32)
    (local $a (ref $a)) (local $i i32) (local $j i32) (local $s i32)
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $j) (local.get $n)))
      (local.set $a (array.new $a (i32.const 2) (i32.add (local.get $m) (local.get $j))))
      (local.set $i (i32.const 0))
      (block $d2 (loop $l2
        (br_if $d2 (i32.ge_s (local.get $i) (local.get $k)))
        (local.set $s (i32.add (local.get $s)
          (array.get $a (local.get $a) (i32.and (i32.mul (local.get $i) (i32.const 3)) (i32.const 15)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l2)))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br $l)))
    (local.get $s))

  ;; a call that can raise, then a loop: the sum of a[(i * 5) & 15] for
  ;; i < n, a m long of 3s
  (func $twice (param i32) (result i32) (i32.mul (local.get 0) (i32.const 2)))
  (func $eh_body (param $a (ref $a)) (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (drop (call_indirect (type $u) (local.get $n) (i32.and (local.get $n) (i32.const 1))))
    (loop $l
      (local.set $s (i32.add (local.get $s)
        (array.get $a (local.get $a) (i32.and (i32.mul (local.get $i) (i32.const 5)) (i32.const 15)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))
  (func $eh (export "eh") (param $m i32) (param $n i32) (result i32)
    (local $a (ref $a))
    (local.set $a (array.new $a (i32.const 3) (local.get $m)))
    (block $out
      (try_table (catch_all $out)
        (global.set $r (call $eh_body (local.get $a) (local.get $n)))))
    (global.get $r))

  ;; the mutable global set to an array of 2 behind a call
  (func $set2 (global.set $mpool2 (array.new $a (i32.const 1) (i32.const 2))))
  (func $mglob2 (export "mglob2") (param $k i32) (result i32)
    (call_indirect $sets (type $v) (i32.shr_u (local.get $k) (i32.const 8)))
    (array.get $a (global.get $mpool2) (local.get $k)))
  (func $fixedg (export "fixedg") (param $k i32) (result i32)
    (array.get $a (global.get $fixed) (local.get $k)))

  ;; two states round a loop, entered in either: state 0 a loop of reads of
  ;; a, state 1 a count of rounds, to 3
  (func $irr (export "irr") (param $n i32) (param $k i32) (param $st0 i32) (result i32)
    (local $a (ref $a)) (local $st i32) (local $i i32) (local $s i32) (local $r i32)
    (local.set $a (array.new $a (i32.const 2) (local.get $n)))
    (local.set $st (i32.and (local.get $st0) (i32.const 1)))
    (block $done
      (loop $top
        (block $s1
          (block $s0
            (br_table $s0 $s1 (local.get $st)))
          (local.set $i (i32.const 0))
          (loop $in
            (local.set $s (i32.add (local.get $s)
              (array.get $a (local.get $a) (i32.and (i32.mul (local.get $i) (i32.const 3)) (i32.const 15)))))
            (local.set $i (i32.add (local.get $i) (i32.const 1)))
            (br_if $in (i32.lt_s (local.get $i) (local.get $k))))
          (local.set $st (i32.const 1))
          (br $top))
        (local.set $r (i32.add (local.get $r) (i32.const 1)))
        (br_if $done (i32.ge_s (local.get $r) (i32.const 3)))
        (local.set $st (i32.const 0))
        (br $top)))
    (i32.add (local.get $s) (i32.mul (local.get $r) (i32.const 1000))))

  (func $mglob (export "mglob") (param $k i32) (result i32)
    (global.set $mpool (array.new $a (i32.const 1) (i32.const 2)))
    (array.get $a (global.get $mpool) (local.get $k)))
)
