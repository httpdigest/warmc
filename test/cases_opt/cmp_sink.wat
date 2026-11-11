;; A branch's compare made where the branch is (opt.c pass_cmp_sink): a
;; compare in another block than the branch on it is a 0 or 1 there, a cset,
;; kept to the branch and tested again; copied to the branch it is one cmp
;; and one b.cond, and the original, unread, goes.  And a test's value
;; handed on along an edge where a branch has decided it is that 0 or 1.
;; No function here keeps a cset.
;;
;;   two     a compare of two parameters branched on in its own block and
;;           again after a loop: the second branch gets a copy
;;   inv     the compare in an outer loop's body, which LICM makes ahead of
;;           the loop (no unswitching: the loop has a loop in it): the body
;;           compares for itself
;;   edge    the test's value handed to the code after the `if` on it, along
;;           the way the `if` is not taken: 0 there
;;   known   the same from inside the arm the test's taken way alone
;;           reaches, on a second `if`'s way round: 1 there
;;
;;   andinv  as inv with an `and` of two compares: both made again at the
;;           branch, one cmp and one ccmp
;;   rare0   a null test's value handed on along its rare way, the null's:
;;           the 0 is made on that edge, after the function's ret, where
;;           one web with the parameter it was made on entry (regalloc.inc:
;;           a constant on a rare way's edge)
;;   tinv    a type test of a value from before an outer loop, which LICM
;;           makes ahead of the loop: eight instructions as a branch, it
;;           stays the value it was made there and is not made again at
;;           the branch (cmps_sunk counts none for it)
;;   truth   a value that is no 0 or 1, branched on and handed on along the
;;           taken way, stays the value there
;;   ktruth  the same from the arm that way alone reaches
;;
;; Answers worked out in Python.
;;
;; stats -O2: cmps_sunk=5 cmp_edge_consts=3
;; shape: two cset=0
;; shape: inv cset=0
;; shape: edge cset=0
;; shape: known cset=0
;; shape: andinv cset=0
;; shape: rare0 cset=0
;; late: rare0 mov\tw[0-9]+, #0x0\b=1
;;
;; run: two 3 1 2 => i32:1012
;; run: two 3 2 1 => i32:9
;; run: two 0 1 2 => i32:1003
;; run: two 0 2 2 => i32:0
;; run: inv 4 1 2 => i32:22
;; run: inv 4 2 1 => i32:34
;; run: inv 0 1 2 => i32:0
;; run: inv 1 1 2 => i32:3
;; run: edge 1 2 => i32:120
;; run: edge 2 1 => i32:10
;; run: edge 5 5 => i32:10
;; run: known 1 2 7 => i32:1105
;; run: known 1 2 0 => i32:1101
;; run: known 2 1 7 => i32:30
;; run: known 2 1 0 => i32:30
;; run: andinv 4 1 2 => i32:22
;; run: andinv 4 2 1 => i32:34
;; run: andinv 2 1 2 => i32:13
;; run: andinv 0 1 2 => i32:0
;; run: rare0 1 => i32:120
;; run: rare0 0 => i32:10
;; run: tinv 4 1 => i32:22
;; run: tinv 4 0 => i32:34
;; run: tinv 0 1 => i32:0
;; run: truth 5 => i32:105
;; run: truth 1 => i32:101
;; run: truth 0 => i32:143
;; run: ktruth 5 0 => i32:1105
;; run: ktruth 5 7 => i32:1108
;; run: ktruth 1 0 => i32:1101
;; run: ktruth 0 7 => i32:30
(module
  (type $t (struct (field i32)))
  (type $objs (array (mut (ref null $t))))
  (global $tab (ref $objs) (array.new_fixed $objs 2 (ref.null $t) (struct.new $t (i32.const 5))))
  (func $at (param $i i32) (result (ref null $t))
    (array.get $objs (global.get $tab) (local.get $i)))
;; c + 3k, recursively so that it is not inlined
  (func $up (param $c i32) (param $k i32) (result i32)
    (if (result i32) (i32.eqz (local.get $k))
      (then (local.get $c))
      (else (call $up (i32.add (local.get $c) (i32.const 3)) (i32.sub (local.get $k) (i32.const 1))))))
  (func $two (export "two") (param $n i32) (param $a i32) (param $b i32) (result i32)
    (local $c i32) (local $j i32) (local $s i32)
    (local.set $c (i32.lt_s (local.get $a) (local.get $b)))
    (if (local.get $c)
      (then (local.set $s (call $up (i32.const 1000) (i32.const 0)))))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $j) (local.get $n)))
        (local.set $s (i32.add (local.get $s) (call $up (i32.const 0) (local.get $j))))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (if (local.get $c)
      (then (local.set $s (call $up (local.get $s) (i32.const 1)))))
    (local.get $s))
  (func $inv (export "inv") (param $n i32) (param $a i32) (param $b i32) (result i32)
    (local $j i32) (local $k i32) (local $s i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $j) (local.get $n)))
        (local.set $k (i32.const 0))
        (loop $in
          (local.set $s (i32.add (local.get $s) (local.get $k)))
          (local.set $k (i32.add (local.get $k) (i32.const 1)))
          (br_if $in (i32.le_u (local.get $k) (i32.and (local.get $j) (i32.const 3)))))
        (if (i32.lt_s (local.get $a) (local.get $b))
          (then (local.set $s (call $up (local.get $s) (i32.const 1))))
          (else (local.set $s (call $up (local.get $s) (i32.const 2)))))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $andinv (export "andinv") (param $n i32) (param $a i32) (param $b i32) (result i32)
    (local $j i32) (local $k i32) (local $s i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $j) (local.get $n)))
        (local.set $k (i32.const 0))
        (loop $in
          (local.set $s (i32.add (local.get $s) (local.get $k)))
          (local.set $k (i32.add (local.get $k) (i32.const 1)))
          (br_if $in (i32.le_u (local.get $k) (i32.and (local.get $j) (i32.const 3)))))
        (if (i32.and (i32.lt_s (local.get $a) (local.get $b)) (i32.lt_s (local.get $b) (local.get $n)))
          (then (local.set $s (call $up (local.get $s) (i32.const 1))))
          (else (local.set $s (call $up (local.get $s) (i32.const 2)))))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $rare0 (export "rare0") (param $i i32) (result i32)
    (local $c i32) (local $r i32)
    (local.set $c (i32.eqz (ref.is_null (call $at (local.get $i)))))
    (local.set $r (local.get $c))
    (if (local.get $c)
      (then (local.set $r (call $up (i32.const 104) (i32.const 2)))))
    (i32.add (local.get $r) (i32.const 10)))
  (func $tinv (export "tinv") (param $n i32) (param $i i32) (result i32)
    (local $j i32) (local $k i32) (local $s i32) (local $x (ref null $t))
    (local.set $x (call $at (local.get $i)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $j) (local.get $n)))
        (local.set $k (i32.const 0))
        (loop $in
          (local.set $s (i32.add (local.get $s) (local.get $k)))
          (local.set $k (i32.add (local.get $k) (i32.const 1)))
          (br_if $in (i32.le_u (local.get $k) (i32.and (local.get $j) (i32.const 3)))))
        (if (ref.test (ref $t) (local.get $x))
          (then (local.set $s (call $up (local.get $s) (i32.const 1))))
          (else (local.set $s (call $up (local.get $s) (i32.const 2)))))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $truth (export "truth") (param $x i32) (result i32)
    (local $r i32)
    (if (local.get $x)
      (then (local.set $r (local.get $x)))
      (else (local.set $r (call $up (i32.const 40) (i32.const 1)))))
    (i32.add (local.get $r) (i32.const 100)))
  (func $ktruth (export "ktruth") (param $x i32) (param $d i32) (result i32)
    (local $q i32) (local $r i32)
    (local.set $r (i32.const 30))
    (if (local.get $x)
      (then
        (local.set $r (call $up (i32.const 1094) (i32.const 2)))
        (local.set $q (local.get $x))
        (if (local.get $d)
          (then (local.set $q (call $up (i32.const 2) (i32.const 2)))))
        (local.set $r (i32.add (local.get $r) (local.get $q)))))
    (local.get $r))
  (func $edge (export "edge") (param $a i32) (param $b i32) (result i32)
    (local $c i32) (local $r i32)
    (local.set $c (i32.lt_s (local.get $a) (local.get $b)))
    (local.set $r (local.get $c))
    (if (local.get $c)
      (then (local.set $r (call $up (i32.const 104) (i32.const 2)))))
    (i32.add (local.get $r) (i32.const 10)))
  (func $known (export "known") (param $a i32) (param $b i32) (param $d i32) (result i32)
    (local $c i32) (local $q i32) (local $r i32)
    (local.set $c (i32.lt_s (local.get $a) (local.get $b)))
    (local.set $r (i32.const 30))
    (if (local.get $c)
      (then
        (local.set $r (call $up (i32.const 1094) (i32.const 2)))
        (local.set $q (local.get $c))
        (if (local.get $d)
          (then (local.set $q (call $up (i32.const 2) (i32.const 1)))))
        (local.set $r (i32.add (local.get $r) (local.get $q)))))
    (local.get $r))
)
