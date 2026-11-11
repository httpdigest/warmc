;; A branch on type tests (opt.c pass_test_chain, pass_gc_nullchk's facts of
;; an and, ir.c's known bits of a test).  The table holds a null, a $base,
;; an $a (second field 60), a $b (second field 70) and a $c, an $a with a
;; third field (80, 800); x, y and z are its elements at i, j and k.
;;
;;   two     if (x is an $a) & (y is a $b), then both cast and read: the
;;           branch is two branches, a test each, and neither cast is made
;;           again -- the and held, so each side did
;;   orr     an or of two tests: two branches, the first's taken way the
;;           or's
;;   mixed   a test and a compare: two branches
;;   three   an and of an and: three branches, three casts gone
;;   nobool  a test and a number that is not 0 or 1: the and of a 1 and a 2
;;           is 0 where neither is, so the and stays an and
;;   orbits  an or of a test and such a number: an or is not 0 just where
;;           either is not, and is two branches
;;   shared  the second side read again after the branch, the first only by
;;           the and: the first is the one made in a block of its own
;;   both    each side read again after: left as it is
;;   castor  if (x is no $a) | (y is no $b) the slow way, else both cast:
;;           the or did not hold, so neither side did, and both tests passed
;;   andval  the and itself stored ahead of the branch: left as it is
;;   effect  an or of a test and a call's result, which only the or reads:
;;           the call stays where it is, made once, and the test goes to
;;           the block of its own
;;   cmps    an and of two compares: a cmp, a ccmp and a branch as it is
;;   orcast  an or of two tests, each read again where it held: one side
;;           holding says nothing of the other (and the tests, read twice,
;;           stay values)
;;   andelse an and of two tests, each read again where it did not hold: nor
;;           does that
;;   three2  three's tests as x & (y & z): the inner and goes to the second
;;           block with its tests, and is two branches there
;;   selbits a select of two tests, and 1: the select is a test's 0 or 1
;;           whichever it takes, and the and of it and 1 is the select
;;   testidx a test the index into an array of two: at most 1, so in the
;;           array, and its bound is not checked
;;
;; (and x 1) of a test is the test: two's `1 & (y is a $b)` is one test.
;; Answers worked out by the model that made the module.
;;
;; (stats: a chain each for two, orr, mixed, orbits, shared, castor and
;; effect, two each for three and three2; the casts gone two's 2, mixed's 1,
;; three's 3, shared's 1, castor's 2, three2's 3.)
;;
;; stats -O2: test_chains=11 checks_removed=13
;; stats -O2 -fno-test-chain: test_chains=0 checks_removed=13
;;
;; shape: two \stst\s+w\d+, w\d+=0
;; shape: three \stst\s+w\d+, w\d+=0
;; shape: nobool \stst\s+w\d+, w\d+=1
;; shape: both \stst\s+w\d+, w\d+=1
;; shape: andval \stst\s+w\d+, w\d+|\sand\s+w\d+, w\d+, w\d+=1
;; shape: cmps \sccmp\s=1
;; shape: selbits \sand\s|\stst\s+w\d+, #0x1\b|\stbn?z\s=0
;;
;; run: two 2 3 => i32:130
;; run: two 4 3 => i32:150
;; run: two 2 2 => i32:1022
;; run: two 1 3 => i32:1013
;; run: two 0 0 => i32:1000
;; run: two 3 3 => i32:1033
;; run: orr 2 2 => i32:101
;; run: orr 1 3 => i32:101
;; run: orr 1 1 => i32:102
;; run: orr 0 0 => i32:102
;; run: orr 4 3 => i32:101
;; run: mixed 2 3 => i32:63
;; run: mixed 2 5 => i32:1005
;; run: mixed 1 3 => i32:1003
;; run: mixed 0 9 => i32:1009
;; run: three 2 3 4 => i32:930
;; run: three 4 3 4 => i32:950
;; run: three 2 3 2 => i32:1007
;; run: three 2 2 4 => i32:1008
;; run: three 1 3 4 => i32:1008
;; run: three 0 0 0 => i32:1000
;; run: nobool 2 1 => i32:1
;; run: nobool 2 2 => i32:2
;; run: nobool 2 3 => i32:1
;; run: nobool 1 1 => i32:2
;; run: nobool 0 2 => i32:2
;; run: orbits 1 0 => i32:2
;; run: orbits 1 2 => i32:1
;; run: orbits 2 0 => i32:1
;; run: orbits 0 0 => i32:2
;; run: shared 2 3 => i32:67
;; run: shared 2 2 => i32:1000
;; run: shared 1 3 => i32:1007
;; run: shared 0 0 => i32:1000
;; run: both 2 3 => i32:115
;; run: both 2 2 => i32:109
;; run: both 1 3 => i32:19
;; run: both 1 1 => i32:9
;; run: castor 2 3 => i32:130
;; run: castor 4 3 => i32:150
;; run: castor 2 2 => i32:1022
;; run: castor 1 3 => i32:1013
;; run: castor 0 0 => i32:1000
;; run: andval 2 3 => i32:15
;; run: andval 2 2 => i32:9
;; run: andval 1 3 => i32:9
;; run: effect 2 => i32:111
;; run: effect 1 => i32:111
;; run: effect 0 => i32:111
;; run: cmps 1 3 => i32:1
;; run: cmps 1 2 => i32:2
;; run: cmps 7 3 => i32:2
;; run: orcast 2 3 => i32:3
;; run: orcast 2 2 => i32:1
;; run: orcast 1 3 => i32:2
;; run: orcast 1 1 => i32:7
;; run: orcast 0 0 => i32:7
;; run: andelse 2 3 => i32:9
;; run: andelse 2 2 => i32:1
;; run: andelse 1 3 => i32:2
;; run: andelse 1 1 => i32:0
;; run: three2 2 3 4 => i32:930
;; run: three2 4 3 4 => i32:950
;; run: three2 2 3 2 => i32:1017
;; run: three2 2 2 4 => i32:1018
;; run: three2 1 3 4 => i32:1018
;; run: three2 0 0 0 => i32:1010
;; run: selbits 2 3 1 => i32:1
;; run: selbits 2 3 0 => i32:1
;; run: selbits 1 3 1 => i32:2
;; run: selbits 2 2 0 => i32:2
;; run: testidx 2 => i32:600
;; run: testidx 4 => i32:600
;; run: testidx 1 => i32:500
;; run: testidx 0 => i32:500
(module
  (type $base (sub (struct (field i32))))
  (type $a (sub $base (struct (field i32) (field i32))))
  (type $b (sub $base (struct (field i32) (field i64))))
  (type $c (sub $a (struct (field i32) (field i32) (field i32))))
  (type $objs (array (mut (ref null $base))))
  (type $ints (array i32))
  (global $pair (ref $ints) (array.new_fixed $ints 2 (i32.const 500) (i32.const 600)))
  (global $tab (mut (ref $objs))
    (array.new_fixed $objs 5 (ref.null $base) (struct.new $base (i32.const 5))
      (struct.new $a (i32.const 6) (i32.const 60)) (struct.new $b (i32.const 7) (i64.const 70))
      (struct.new $c (i32.const 8) (i32.const 80) (i32.const 800))))
  ;; 1000 + 10 i + j, recursively so that it is not inlined
  (func $slow (param $i i32) (param $j i32) (param $d i32) (result i32)
    (if (result i32) (local.get $d)
      (then (call $slow (local.get $i) (local.get $j) (i32.sub (local.get $d) (i32.const 1))))
      (else (i32.add (i32.const 1000) (i32.add (i32.mul (local.get $i) (i32.const 10)) (local.get $j))))))
  (func $two (export "two") (param $i i32) (param $j i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base))
    (local.set $x (array.get $objs (global.get $tab) (local.get $i))) (local.set $y (array.get $objs (global.get $tab) (local.get $j)))
    (if (result i32)
      (i32.and (ref.test (ref $a) (local.get $x))
               (i32.and (i32.const 1) (ref.test (ref $b) (local.get $y))))
      (then (i32.add (struct.get $a 1 (ref.cast (ref $a) (local.get $x)))
                     (i32.wrap_i64 (struct.get $b 1 (ref.cast (ref $b) (local.get $y))))))
      (else (call $slow (local.get $i) (local.get $j) (i32.const 2)))))
  (func $orr (export "orr") (param $i i32) (param $j i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base))
    (local.set $x (array.get $objs (global.get $tab) (local.get $i))) (local.set $y (array.get $objs (global.get $tab) (local.get $j)))
    (if (result i32)
      (i32.or (ref.test (ref $a) (local.get $x)) (ref.test (ref $b) (local.get $y)))
      (then (i32.sub (call $slow (i32.const 0) (i32.const 1) (i32.const 2)) (i32.const 900)))
      (else (i32.sub (call $slow (i32.const 0) (i32.const 2) (i32.const 2)) (i32.const 900)))))
  (func $mixed (export "mixed") (param $i i32) (param $n i32) (result i32)
    (local $x (ref null $base))
    (local.set $x (array.get $objs (global.get $tab) (local.get $i)))
    (if (result i32)
      (i32.and (ref.test (ref $a) (local.get $x)) (i32.lt_u (local.get $n) (i32.const 5)))
      (then (i32.add (struct.get $a 1 (ref.cast (ref $a) (local.get $x))) (local.get $n)))
      (else (call $slow (i32.const 0) (local.get $n) (i32.const 2)))))
  (func $three (export "three") (param $i i32) (param $j i32) (param $k i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base)) (local $z (ref null $base))
    (local.set $x (array.get $objs (global.get $tab) (local.get $i))) (local.set $y (array.get $objs (global.get $tab) (local.get $j))) (local.set $z (array.get $objs (global.get $tab) (local.get $k)))
    (if (result i32)
      (i32.and (i32.and (ref.test (ref $a) (local.get $x)) (ref.test (ref $b) (local.get $y)))
               (ref.test (ref $c) (local.get $z)))
      (then (i32.add
              (i32.add (struct.get $a 1 (ref.cast (ref $a) (local.get $x)))
                       (i32.wrap_i64 (struct.get $b 1 (ref.cast (ref $b) (local.get $y)))))
              (struct.get $c 2 (ref.cast (ref $c) (local.get $z)))))
      (else (call $slow (i32.const 0) (i32.add (i32.add (local.get $i) (local.get $j)) (local.get $k))
              (i32.const 2)))))
  (func $nobool (export "nobool") (param $i i32) (param $m i32) (result i32)
    (local $x (ref null $base))
    (local.set $x (array.get $objs (global.get $tab) (local.get $i)))
    (if (result i32) (i32.and (ref.test (ref $a) (local.get $x)) (local.get $m))
      (then (i32.sub (call $slow (i32.const 0) (i32.const 1) (i32.const 2)) (i32.const 1000)))
      (else (i32.sub (call $slow (i32.const 0) (i32.const 2) (i32.const 2)) (i32.const 1000)))))
  (func $orbits (export "orbits") (param $i i32) (param $m i32) (result i32)
    (local $x (ref null $base))
    (local.set $x (array.get $objs (global.get $tab) (local.get $i)))
    (if (result i32) (i32.or (ref.test (ref $a) (local.get $x)) (local.get $m))
      (then (i32.sub (call $slow (i32.const 0) (i32.const 1) (i32.const 2)) (i32.const 1000)))
      (else (i32.sub (call $slow (i32.const 0) (i32.const 2) (i32.const 2)) (i32.const 1000)))))
  (func $shared (export "shared") (param $i i32) (param $j i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base)) (local $t i32) (local $r i32)
    (local.set $x (array.get $objs (global.get $tab) (local.get $i))) (local.set $y (array.get $objs (global.get $tab) (local.get $j)))
    (local.set $t (ref.test (ref $b) (local.get $y)))
    (if (i32.and (ref.test (ref $a) (local.get $x)) (local.get $t))
      (then (local.set $r (struct.get $a 1 (ref.cast (ref $a) (local.get $x)))))
      (else (local.set $r (call $slow (i32.const 0) (i32.const 0) (i32.const 2)))))
    (i32.add (local.get $r) (i32.mul (local.get $t) (i32.const 7))))
  (func $both (export "both") (param $i i32) (param $j i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base)) (local $t i32) (local $u i32) (local $r i32)
    (local.set $x (array.get $objs (global.get $tab) (local.get $i))) (local.set $y (array.get $objs (global.get $tab) (local.get $j)))
    (local.set $t (ref.test (ref $b) (local.get $y)))
    (local.set $u (ref.test (ref $a) (local.get $x)))
    (if (i32.and (local.get $u) (local.get $t))
      (then (local.set $r (i32.sub (call $slow (i32.const 0) (i32.const 5) (i32.const 2)) (i32.const 1000))))
      (else (local.set $r (i32.sub (call $slow (i32.const 0) (i32.const 9) (i32.const 2)) (i32.const 1000)))))
    (i32.add (i32.add (local.get $r) (i32.mul (local.get $t) (i32.const 10)))
             (i32.mul (local.get $u) (i32.const 100))))
  (global $count (mut i32) (i32.const 99))
  ;; 1, the count one more: recursively so that it is not inlined
  (func $tick (param $d i32) (result i32)
    (if (result i32) (local.get $d)
      (then (call $tick (i32.sub (local.get $d) (i32.const 1))))
      (else (global.set $count (i32.add (global.get $count) (i32.const 1))) (i32.const 1))))
  (func $andval (export "andval") (param $i i32) (param $j i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base)) (local $t i32) (local $r i32)
    (local.set $x (array.get $objs (global.get $tab) (local.get $i))) (local.set $y (array.get $objs (global.get $tab) (local.get $j)))
    (local.set $t (i32.and (ref.test (ref $a) (local.get $x)) (ref.test (ref $b) (local.get $y))))
    (global.set $count (local.get $t))
    (if (local.get $t)
      (then (local.set $r (i32.sub (call $slow (i32.const 0) (i32.const 5) (i32.const 2)) (i32.const 1000))))
      (else (local.set $r (i32.sub (call $slow (i32.const 0) (i32.const 9) (i32.const 2)) (i32.const 1000)))))
    (i32.add (local.get $r) (i32.mul (global.get $count) (i32.const 10))))
  (func $effect (export "effect") (param $i i32) (result i32)
    (local $x (ref null $base)) (local $r i32)
    (local.set $x (array.get $objs (global.get $tab) (local.get $i)))
    (if (i32.or (ref.test (ref $a) (local.get $x)) (call $tick (i32.const 2)))
      (then (local.set $r (i32.sub (call $slow (i32.const 1) (i32.const 1) (i32.const 2)) (i32.const 1000))))
      (else (local.set $r (i32.sub (call $slow (i32.const 2) (i32.const 2) (i32.const 2)) (i32.const 1000)))))
    (i32.add (local.get $r) (global.get $count)))
  (func $cmps (export "cmps") (param $n i32) (param $m i32) (result i32)
    (if (result i32) (i32.and (i32.lt_u (local.get $n) (i32.const 5)) (i32.gt_u (local.get $m) (i32.const 2)))
      (then (i32.sub (call $slow (i32.const 0) (i32.const 1) (i32.const 2)) (i32.const 1000)))
      (else (i32.sub (call $slow (i32.const 0) (i32.const 2) (i32.const 2)) (i32.const 1000)))))
  (func $orcast (export "orcast") (param $i i32) (param $j i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base))
    (local.set $x (array.get $objs (global.get $tab) (local.get $i))) (local.set $y (array.get $objs (global.get $tab) (local.get $j)))
    (if (result i32)
      (i32.or (ref.test (ref $a) (local.get $x)) (ref.test (ref $b) (local.get $y)))
      (then (i32.add (ref.test (ref $a) (local.get $x))
                     (i32.mul (ref.test (ref $b) (local.get $y)) (i32.const 2))))
      (else (i32.sub (call $slow (i32.const 0) (i32.const 7) (i32.const 2)) (i32.const 1000)))))
  (func $andelse (export "andelse") (param $i i32) (param $j i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base))
    (local.set $x (array.get $objs (global.get $tab) (local.get $i))) (local.set $y (array.get $objs (global.get $tab) (local.get $j)))
    (if (result i32)
      (i32.and (ref.test (ref $a) (local.get $x)) (ref.test (ref $b) (local.get $y)))
      (then (i32.sub (call $slow (i32.const 0) (i32.const 9) (i32.const 2)) (i32.const 1000)))
      (else (i32.add (ref.test (ref $a) (local.get $x))
                     (i32.mul (ref.test (ref $b) (local.get $y)) (i32.const 2))))))
  (func $three2 (export "three2") (param $i i32) (param $j i32) (param $k i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base)) (local $z (ref null $base))
    (local.set $x (array.get $objs (global.get $tab) (local.get $i))) (local.set $y (array.get $objs (global.get $tab) (local.get $j))) (local.set $z (array.get $objs (global.get $tab) (local.get $k)))
    (if (result i32)
      (i32.and (ref.test (ref $a) (local.get $x))
               (i32.and (ref.test (ref $b) (local.get $y)) (ref.test (ref $c) (local.get $z))))
      (then (i32.add
              (i32.add (struct.get $a 1 (ref.cast (ref $a) (local.get $x)))
                       (i32.wrap_i64 (struct.get $b 1 (ref.cast (ref $b) (local.get $y)))))
              (struct.get $c 2 (ref.cast (ref $c) (local.get $z)))))
      (else (call $slow (i32.const 1) (i32.add (i32.add (local.get $i) (local.get $j)) (local.get $k))
              (i32.const 2)))))
  (func $selbits (export "selbits") (param $i i32) (param $j i32) (param $c i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base))
    (local.set $x (array.get $objs (global.get $tab) (local.get $i))) (local.set $y (array.get $objs (global.get $tab) (local.get $j)))
    (if (result i32)
      (i32.and (select (ref.test (ref $a) (local.get $x)) (ref.test (ref $b) (local.get $y)) (local.get $c))
               (i32.const 1))
      (then (i32.sub (call $slow (i32.const 0) (i32.const 1) (i32.const 2)) (i32.const 1000)))
      (else (i32.sub (call $slow (i32.const 0) (i32.const 2) (i32.const 2)) (i32.const 1000)))))
  (func $testidx (export "testidx") (param $i i32) (result i32)
    (local $x (ref null $base))
    (local.set $x (array.get $objs (global.get $tab) (local.get $i)))
    (array.get $ints (global.get $pair) (ref.test (ref $a) (local.get $x))))
  (func $castor (export "castor") (param $i i32) (param $j i32) (result i32)
    (local $x (ref null $base)) (local $y (ref null $base))
    (local.set $x (array.get $objs (global.get $tab) (local.get $i))) (local.set $y (array.get $objs (global.get $tab) (local.get $j)))
    (if (result i32)
      (i32.or (i32.eqz (ref.test (ref $a) (local.get $x))) (i32.eqz (ref.test (ref $b) (local.get $y))))
      (then (call $slow (local.get $i) (local.get $j) (i32.const 2)))
      (else (i32.add (struct.get $a 1 (ref.cast (ref $a) (local.get $x)))
                     (i32.wrap_i64 (struct.get $b 1 (ref.cast (ref $b) (local.get $y)))))))))
