;; A reference known to be no null is not tested for one.  A block param
;; no way in brings a null to -- each way's value of a type that takes
;; none, or one its block knows is none -- is none (opt.c
;; gc_nonnull_params): luawasm's casts of one object, a way a test of each
;; kind, meet at a param its record's read checked for null again.  And in
;; a sealed module a type test of a reference known to be none -- a branch
;; on its null test before, a test null fails or a test that takes none
;; passed, an access of it, a param no way in brings one to -- tests for no
;; null (codegen.c seal_tests, RTK_NONNULL, opt.c gc_nonnull_in): luawasm
;; tests each value for nil before it tests its kind, two or three kinds in
;; a row.
;;
;; The objects by index: 0 null, 1 a $base of 5, 2 an $a of 6, 3 a $b of 7.
;;
;;   kinds       null, then $a, then $b: the two tests after the null branch
;;               test no null (one cbz, the branch's)
;;   access      a read of the object, then a test: the test none (one cbz,
;;               the read's trap)
;;   merge       an $a cast of the object or the $b at 3, met at a param:
;;               the param's read checks no null, nor its test of $a after
;;   tests       after a nullable test failed, and in the arm a test that
;;               takes no null passed: neither the next test tests null
;;   nullside    after a test that takes no null failed, which null fails:
;;               a nullable test there still lets null pass
;;   isnullside  in the null test's arm where it is null: a nullable test
;;               passes it
;;   walk        a list walked, the param round the loop a field read anew:
;;               its read checks null still and traps at the list's end
;;   merge2      the object itself, past its null branch, or the $b at 3 met
;;               at a param: the way in that brings the object brings what
;;               its block knows is no null
;;   mergenull   the object, not tested for null, or the $b at 3: the
;;               param's nullable test lets null pass
;;   held        a test of a field that takes no null: by its type
;;   asnn        a test after a ref.as_non_null: its trap the proof
;;   passnull    in the arm a nullable test passed, which null passes: a
;;               nullable test there still lets null pass
;;   heldmerge   a holder's field, of a type that takes no null, or the
;;               object past its null branch, met at a param: its read
;;               checks no null
;;   kindsn      past the null branch a nullable test: none tested, nor the
;;               null's pass made
;;   heldtest    as heldmerge, the branch on the eqz of the null test, and a
;;               test of the param: none tested
;;
;; Tests that test no null: kinds' two, access' one, merge's two (its test
;; of the object after the null branch too), tests' second, isnullside's
;; past its null arm, held's, asnn's, kindsn's, heldtest's -- 11; the
;; params, merge's, merge2's, heldmerge's two (the first brought the object
;; on the null branch's other way) and heldtest's, each counted in each of
;; the two rounds the pass runs in -- 10.  Answers worked out by hand.
;;
;; stats -O2: gc_nonnull_params=10 slot_nonnull=11
;; shape: heldmerge cb(n)?z\s+x\d+,=1
;; shape: kinds cb(n)?z\s+x\d+,=1
;; shape: access cb(n)?z\s+x\d+,=1
;; shape: merge cb(n)?z\s+x\d+,=2
;; shape: tests cb(n)?z\s+x\d+,=1
;; shape: nullside cb(n)?z\s+x\d+,=2
;;
;; run: kinds 0 => i32:-1
;; run: kinds 1 => i32:3
;; run: kinds 2 => i32:1
;; run: kinds 3 => i32:2
;; run: access 0 => trap: null reference
;; run: access 1 => i32:5
;; run: access 2 => i32:106
;; run: access 3 => i32:7
;; run: merge 0 => i32:-1
;; run: merge 1 => i32:7
;; run: merge 2 => i32:54
;; run: merge 3 => i32:7
;; run: tests 0 => i32:10
;; run: tests 1 => i32:30
;; run: tests 2 => i32:10
;; run: tests 3 => i32:20
;; run: nullside 0 => i32:20
;; run: nullside 1 => i32:30
;; run: nullside 2 => i32:10
;; run: nullside 3 => i32:20
;; run: isnullside 0 => i32:1
;; run: isnullside 1 => i32:100
;; run: isnullside 2 => i32:101
;; run: merge2 0 => i32:-1
;; run: merge2 1 => i32:7
;; run: merge2 2 => i32:6
;; run: merge2 3 => i32:7
;; run: mergenull 0 => i32:1
;; run: mergenull 1 => i32:0
;; run: mergenull 2 => i32:1
;; run: mergenull 3 => i32:0
;; run: held 0 => i32:0
;; run: held 1 => i32:1
;; run: asnn 0 => trap: null reference
;; run: asnn 1 => i32:0
;; run: asnn 2 => i32:1
;; run: passnull 0 => i32:1
;; run: passnull 1 => i32:30
;; run: passnull 2 => i32:0
;; run: passnull 3 => i32:30
;; run: heldmerge 0 => i32:5
;; run: heldmerge 1 => i32:6
;; run: heldmerge 2 => i32:6
;; run: heldmerge 3 => i32:7
;; run: heldtest 0 => i32:7
;; run: heldtest 1 => i32:5
;; run: heldtest 2 => i32:7
;; run: heldtest 3 => i32:7
;; run: kindsn 0 => i32:-1
;; run: kindsn 1 => i32:0
;; run: kindsn 2 => i32:1
;; run: walk 1 => i32:1
;; run: walk 3 => i32:6
;; run: walk 4 => trap: null reference
(module
  (type $base (sub (struct (field i32))))
  (type $a (sub $base (struct (field i32) (field i32))))
  (type $b (sub $base (struct (field i32) (field i64))))
  (type $objs (array (mut (ref null $base))))
  (type $node (struct (field i32) (field (ref null $node))))
  (type $holder (struct (field (ref $base))))
  (type $hs (array (ref $holder)))
  (global $tab (ref $objs)
    (array.new_fixed $objs 4 (ref.null $base) (struct.new $base (i32.const 5))
      (struct.new $a (i32.const 6) (i32.const 60)) (struct.new $b (i32.const 7) (i64.const 70))))
  (global $list (ref $node)
    (struct.new $node (i32.const 1)
      (struct.new $node (i32.const 2) (struct.new $node (i32.const 3) (ref.null $node)))))
  (global $holders (ref $hs)
    (array.new_fixed $hs 2 (struct.new $holder (struct.new $base (i32.const 5)))
      (struct.new $holder (struct.new $a (i32.const 6) (i32.const 60)))))
  (func $at (param $i i32) (result (ref null $base))
    (array.get $objs (global.get $tab) (local.get $i)))
  (func $merge2 (export "merge2") (param $i i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base))
    (local.set $x (call $at (local.get $i)))
    (if (ref.is_null (local.get $x)) (then (return (i32.const -1))))
    (if (i32.and (local.get $i) (i32.const 2))
      (then (local.set $y (local.get $x)))
      (else (local.set $y (ref.cast (ref $b) (call $at (i32.const 3))))))
    (struct.get $base 0 (local.get $y)))
  (func $mergenull (export "mergenull") (param $i i32) (result i32)
    (local $y (ref null $base))
    (if (i32.and (local.get $i) (i32.const 1))
      (then (local.set $y (ref.cast (ref $b) (call $at (i32.const 3)))))
      (else (local.set $y (call $at (local.get $i)))))
    (ref.test (ref null $a) (local.get $y)))
  (func $held (export "held") (param $i i32) (result i32)
    (ref.test (ref $a) (struct.get $holder 0 (array.get $hs (global.get $holders) (local.get $i)))))
  (func $asnn (export "asnn") (param $i i32) (result i32)
    (ref.test (ref $a) (ref.as_non_null (call $at (local.get $i)))))
  (func $heldmerge (export "heldmerge") (param $i i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base))
    (local.set $x (call $at (local.get $i)))
    (if (ref.is_null (local.get $x))
      (then (local.set $y (struct.get $holder 0 (array.get $hs (global.get $holders) (i32.const 0)))))
      (else (local.set $y (local.get $x))))
    (if (i32.eq (local.get $i) (i32.const 1))
      (then (local.set $y (struct.get $holder 0 (array.get $hs (global.get $holders) (i32.const 1))))))
    (struct.get $base 0 (local.get $y)))
  (func $heldtest (export "heldtest") (param $i i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base))
    (local.set $x (call $at (local.get $i)))
    (if (i32.eqz (ref.is_null (local.get $x)))
      (then (local.set $y (local.get $x)))
      (else (local.set $y (struct.get $holder 0 (array.get $hs (global.get $holders) (i32.const 1))))))
    (i32.add (ref.test (ref $a) (local.get $y)) (struct.get $base 0 (local.get $y))))
  (func $kindsn (export "kindsn") (param $i i32) (result i32)
    (local $x (ref null $base))
    (local.set $x (call $at (local.get $i)))
    (if (ref.is_null (local.get $x)) (then (return (i32.const -1))))
    (ref.test (ref null $a) (local.get $x)))
  (func $passnull (export "passnull") (param $i i32) (result i32)
    (local $x (ref null $base))
    (local.set $x (call $at (local.get $i)))
    (if (ref.test (ref null $a) (local.get $x))
      (then (return (ref.test (ref null $b) (local.get $x)))))
    (i32.const 30))
  (func $kinds (export "kinds") (param $i i32) (result i32)
    (local $x (ref null $base))
    (local.set $x (call $at (local.get $i)))
    (if (ref.is_null (local.get $x)) (then (return (i32.const -1))))
    (if (ref.test (ref $a) (local.get $x)) (then (return (i32.const 1))))
    (if (ref.test (ref $b) (local.get $x)) (then (return (i32.const 2))))
    (i32.const 3))
  (func $access (export "access") (param $i i32) (result i32)
    (local $x (ref null $base)) (local $v i32)
    (local.set $x (call $at (local.get $i)))
    (local.set $v (struct.get $base 0 (local.get $x)))
    (if (ref.test (ref $a) (local.get $x))
      (then (return (i32.add (local.get $v) (i32.const 100)))))
    (local.get $v))
  (func $merge (export "merge") (param $i i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base))
    (local.set $x (call $at (local.get $i)))
    (if (ref.is_null (local.get $x)) (then (return (i32.const -1))))
    (if (ref.test (ref $a) (local.get $x))
      (then (local.set $y (ref.cast (ref $a) (local.get $x))))
      (else (local.set $y (ref.cast (ref $b) (call $at (i32.const 3))))))
    (if (ref.test (ref $a) (local.get $y))
      (then (return (i32.mul (struct.get $base 0 (local.get $y)) (i32.const 9)))))
    (struct.get $base 0 (local.get $y)))
  (func $tests (export "tests") (param $i i32) (result i32)
    (local $x (ref null $base))
    (local.set $x (call $at (local.get $i)))
    (if (ref.test (ref null $a) (local.get $x)) (then (return (i32.const 10))))
    (if (ref.test (ref $b) (local.get $x)) (then (return (i32.const 20))))
    (i32.const 30))
  (func $nullside (export "nullside") (param $i i32) (result i32)
    (local $x (ref null $base))
    (local.set $x (call $at (local.get $i)))
    (if (ref.test (ref $a) (local.get $x)) (then (return (i32.const 10))))
    (if (ref.test (ref null $b) (local.get $x)) (then (return (i32.const 20))))
    (i32.const 30))
  (func $isnullside (export "isnullside") (param $i i32) (result i32)
    (local $x (ref null $base))
    (local.set $x (call $at (local.get $i)))
    (if (ref.is_null (local.get $x))
      (then (return (ref.test (ref null $a) (local.get $x)))))
    (i32.add (i32.const 100) (ref.test (ref $a) (local.get $x))))
  (func $walk (export "walk") (param $n i32) (result i32)
    (local $p (ref null $node)) (local $s i32)
    (local.set $p (global.get $list))
    (loop $l
      (local.set $s (i32.add (local.get $s) (struct.get $node 0 (local.get $p))))
      (local.set $p (struct.get $node 1 (local.get $p)))
      (local.set $n (i32.sub (local.get $n) (i32.const 1)))
      (br_if $l (local.get $n)))
    (local.get $s))
)
