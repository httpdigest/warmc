;; A branch on a null test tests the reference itself (isel.c's fusion of a
;; branch's condition, isel_ops.inc's and isel_x64_ops.inc's T_BRIF): the
;; ref.is_null the branch alone reads is a cbz of the reference, and of the
;; eqz of one -- br_on_non_null, luawasm's `if x` -- a cbnz, where a cmp and
;; a cset made a 0 or 1 for a cbz to test again.  The references come out of
;; an array the optimiser knows nothing of: a null at 0, a struct holding 7
;; at 1, one holding 9 at 2.
;;
;;   isnull    if (ref.is_null x): a cbz of x
;;   notnull   if (eqz (ref.is_null x)): a cbnz of x
;;   onnull    br_on_null
;;   onnon     br_on_non_null
;;   twice     the test's 0 or 1 read by the branch and a sum too: it is
;;             made, and the branch tests it (no fusion with two readers)
;;   twice2    the eqz's operand read by the return too: the eqz is a cbz of
;;             the 0 or 1 made
;;   twice3    the eqz's operand read by a sum before the branch too: made,
;;             and not folded into the branch (its sum is not twice's, which
;;             it would otherwise be, code and all, and one symbol for both)
;;   apart     the test made in one block and the branch in another: no fold
;;   pair      two null tests and'ed: the ccmp chain, untouched
;;
;; Answers worked out by hand from the array.
;;
;; shape: isnull cset\s=0
;; shape: isnull cbz\s+x\d+,=1
;; shape: notnull cset\s=0
;; shape: notnull cb(n)?z\s+x\d+,=1
;; shape: onnull cset\s=0
;; shape: onnon cset\s=0
;; shape: twice cset\s=1
;; shape: twice2 cset\s=1
;; shape: twice3 cset\s=1
;; shape: pair ccmp\s=1
;;
;; run: isnull 0 => i32:1
;; run: isnull 1 => i32:7
;; run: isnull 2 => i32:9
;; run: notnull 0 => i32:-1
;; run: notnull 1 => i32:8
;; run: notnull 2 => i32:10
;; run: onnull 0 => i32:100
;; run: onnull 2 => i32:9
;; run: onnon 0 => i32:200
;; run: onnon 1 => i32:14
;; run: twice 0 => i32:100
;; run: twice 1 => i32:8
;; run: twice2 0 => i32:1
;; run: twice2 2 => i32:18
;; run: twice3 0 => i32:200
;; run: twice3 1 => i32:8
;; run: apart 0 => i32:5
;; run: apart 1 => i32:6
;; run: apart 2 => i32:6
;; run: pair 0 => i32:3
;; run: pair 1 => i32:4
;; run: pair 2 => i32:4
(module
  (type $s (struct (field i32)))
  (type $refs (array (mut (ref null $s))))
  (global $tab (ref $refs)
    (array.new_fixed $refs 3 (ref.null $s) (struct.new $s (i32.const 7)) (struct.new $s (i32.const 9))))
  (func $at (param $i i32) (result (ref null $s))
    (array.get $refs (global.get $tab) (local.get $i)))
  (func $isnull (export "isnull") (param $i i32) (result i32)
    (local $x (ref null $s))
    (local.set $x (array.get $refs (global.get $tab) (local.get $i)))
    (if (ref.is_null (local.get $x)) (then (return (i32.const 1))))
    (struct.get $s 0 (local.get $x)))
  (func $notnull (export "notnull") (param $i i32) (result i32)
    (local $x (ref null $s))
    (local.set $x (array.get $refs (global.get $tab) (local.get $i)))
    (if (i32.eqz (ref.is_null (local.get $x)))
      (then (return (i32.add (struct.get $s 0 (local.get $x)) (i32.const 1)))))
    (i32.const -1))
  (func $onnull (export "onnull") (param $i i32) (result i32)
    (block $n
      (return (struct.get $s 0
        (br_on_null $n (array.get $refs (global.get $tab) (local.get $i))))))
    (i32.const 100))
  (func $onnon (export "onnon") (param $i i32) (result i32)
    (block $nn (result (ref $s))
      (br_on_non_null $nn (array.get $refs (global.get $tab) (local.get $i)))
      (return (i32.const 200)))
    (struct.get $s 0)
    (i32.const 2)
    (i32.mul))
  (func $twice (export "twice") (param $i i32) (result i32)
    (local $x (ref null $s)) (local $t i32) (local $u i32)
    (local.set $x (array.get $refs (global.get $tab) (local.get $i)))
    (local.set $t (ref.is_null (local.get $x)))
    (local.set $u (i32.add (i32.mul (local.get $t) (i32.const 100)) (local.get $i)))
    (if (local.get $t) (then (return (local.get $u))))
    (i32.add (struct.get $s 0 (local.get $x)) (local.get $u)))
  (func $twice2 (export "twice2") (param $i i32) (result i32)
    (local $x (ref null $s)) (local $t i32)
    (local.set $x (array.get $refs (global.get $tab) (local.get $i)))
    (local.set $t (ref.is_null (local.get $x)))
    (if (i32.eqz (local.get $t))
      (then (return (i32.add (local.get $t) (i32.mul (struct.get $s 0 (local.get $x)) (i32.const 2))))))
    (local.get $t))
  (func $twice3 (export "twice3") (param $i i32) (result i32)
    (local $x (ref null $s)) (local $t i32) (local $u i32)
    (local.set $x (array.get $refs (global.get $tab) (local.get $i)))
    (local.set $t (ref.is_null (local.get $x)))
    (local.set $u (i32.add (i32.mul (local.get $t) (i32.const 200)) (local.get $i)))
    (if (i32.eqz (local.get $t))
      (then (return (i32.add (struct.get $s 0 (local.get $x)) (local.get $u)))))
    (local.get $u))
  (func $apart (export "apart") (param $i i32) (result i32)
    (local $x (ref null $s)) (local $t i32)
    (local.set $x (array.get $refs (global.get $tab) (local.get $i)))
    (local.set $t (ref.is_null (local.get $x)))
    (if (i32.lt_u (local.get $i) (i32.const 1))
      (then (local.set $i (i32.const 5))))
    (if (local.get $t) (then (return (local.get $i))))
    (i32.const 6))
  (func $pair (export "pair") (param $i i32) (result i32)
    (local $x (ref null $s)) (local $y (ref null $s))
    (local.set $x (array.get $refs (global.get $tab) (local.get $i)))
    (local.set $y (call $at (i32.and (local.get $i) (i32.const 0))))
    (if (i32.and (ref.is_null (local.get $x)) (ref.is_null (local.get $y)))
      (then (return (i32.const 3))))
    (i32.const 4))
)
