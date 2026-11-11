;; The hot way split where its ways in fix a value (opt.c pass_way_split):
;; a value tested three ways -- 1, 2, anything else -- whose ways meet, some
;; code, and a test of the value again.  The ways that fix it get a copy of
;; the code up to that test, which they go past, and the way of the rest is
;; the one way into the original.  Each function reads an array at an index
;; its way picks (x), makes y = 3x + i, stores y or y + 1 by y's low bit --
;; a branch on something else, three blocks between the merge and the test,
;; more than jump threading copies -- and tests the value again to store y
;; at 0 or at 1.
;;
;;   kind3    the value's two fixed ways each go past the test of it, a
;;            branch on the value itself: two copies
;;   two      a value two branches test, each a compare with 1: one copy
;;   refval   the code between reads a reference a type test after the
;;            second test reads again: the copies make it again, and what
;;            reaches the read is merged -- two copies
;;   rare     kind3's code behind a type test's rare way (which it takes:
;;            the object is no $a): off the hot way, no copy
;;   plain    kind3's code in a function no test of which has a rare way by
;;            a rule: no copy
;;   once     a value one branch tests, the second test of another: no copy
;;   called   a call in the block the ways meet in, which no copy is made
;;            of: no copy
;;   looped   the second test in a loop the ways meet ahead of: no copy
;;   inl      kind3's code in a function inlined into a few instructions:
;;            two copies
;;   hinted   kind3 with its test for 1 a way a branch hint calls rare: that
;;            way is off the hot way, and only the way for 2 has a copy
;;   rarearm  the second test of the value in the arm of a type test's rare
;;            way, which the hot way's walk does not enter: no copy
;;   fromrare a rare way's own test of the value sends 1 straight on to the
;;            hot code that tests it again: a way from off the hot way, no
;;            copy (seen without the tails, which give it code of its own)
;;   nullkey  the two ways are those of a null test, and the test is made
;;            again after they meet: whether a reference is null is no value
;;            to split on -- a tree's missing child is as likely as not, and
;;            javabench/treemap ran 8% longer on such copies -- no copy
;;
;; (stats: kind3 2, two 1, refval 2, hinted 1, inl 2, and 2 in the function
;; inl calls, compiled for itself too.)  Made by a model of the functions
;; that works the answers out.
;;
;; stats -O2: way_splits=10
;; stats -O2 -fno-tails: way_splits=10
;;
;; run: kind3 0 1 => i32:299
;; run: kind3 0 2 => i32:112
;; run: kind3 1 1 => i32:9228
;; run: kind3 1 2 => i32:9312
;; run: kind3 2 1 => i32:18138
;; run: kind3 2 2 => i32:18402
;; run: kind3 3 1 => i32:408
;; run: kind3 3 2 => i32:132
;; run: refval 0 1 => i32:335
;; run: refval 0 2 => i32:100163
;; run: refval 1 1 => i32:9282
;; run: refval 1 2 => i32:109384
;; run: refval 2 1 => i32:18192
;; run: refval 2 2 => i32:118474
;; run: refval 3 1 => i32:462
;; run: refval 3 2 => i32:100204
;; run: rare 0 1 => i32:299
;; run: rare 0 2 => i32:112
;; run: rare 1 1 => i32:9228
;; run: rare 1 2 => i32:9312
;; run: rare 2 1 => i32:18138
;; run: rare 2 2 => i32:18402
;; run: rare 3 1 => i32:408
;; run: rare 3 2 => i32:132
;; run: plain 0 1 => i32:299
;; run: plain 0 2 => i32:112
;; run: plain 1 1 => i32:9228
;; run: plain 1 2 => i32:9312
;; run: plain 2 1 => i32:18138
;; run: plain 2 2 => i32:18402
;; run: plain 3 1 => i32:408
;; run: plain 3 2 => i32:132
;; run: called 0 1 => i32:332
;; run: called 0 2 => i32:160
;; run: called 1 1 => i32:9282
;; run: called 1 2 => i32:9384
;; run: called 2 1 => i32:18192
;; run: called 2 2 => i32:18474
;; run: called 3 1 => i32:462
;; run: called 3 2 => i32:204
;; run: inl 0 1 => i32:300
;; run: inl 0 2 => i32:113
;; run: inl 1 1 => i32:9229
;; run: inl 1 2 => i32:9313
;; run: inl 2 1 => i32:18139
;; run: inl 2 2 => i32:18403
;; run: inl 3 1 => i32:409
;; run: inl 3 2 => i32:133
;; run: hinted 0 1 => i32:299
;; run: hinted 0 2 => i32:112
;; run: hinted 1 1 => i32:9228
;; run: hinted 1 2 => i32:9312
;; run: hinted 2 1 => i32:18138
;; run: hinted 2 2 => i32:18402
;; run: hinted 3 1 => i32:408
;; run: hinted 3 2 => i32:132
;; run: rarearm 0 1 => i32:299
;; run: rarearm 0 2 => i32:112
;; run: rarearm 1 1 => i32:9228
;; run: rarearm 1 2 => i32:9312
;; run: rarearm 2 1 => i32:18138
;; run: rarearm 2 2 => i32:18402
;; run: rarearm 3 1 => i32:408
;; run: rarearm 3 2 => i32:132
;; run: fromrare 0 1 => i32:104
;; run: fromrare 0 2 => i32:115
;; run: fromrare 1 1 => i32:84
;; run: fromrare 1 2 => i32:96
;; run: fromrare 2 1 => i32:120
;; run: fromrare 2 2 => i32:132
;; run: fromrare 3 1 => i32:120
;; run: fromrare 3 2 => i32:132
;; run: nullkey 0 1 => i32:299
;; run: nullkey 0 2 => i32:112
;; run: nullkey 1 1 => i32:9228
;; run: nullkey 1 2 => i32:9312
;; run: nullkey 2 1 => i32:299
;; run: nullkey 2 2 => i32:112
;; run: nullkey 3 1 => i32:9228
;; run: nullkey 3 2 => i32:9312
;; run: two 0 1 => i32:299
;; run: two 0 2 => i32:112
;; run: two 1 1 => i32:9228
;; run: two 1 2 => i32:9312
;; run: once 0 1 => i32:408
;; run: once 0 2 => i32:112
;; run: once 1 1 => i32:9228
;; run: once 1 2 => i32:6232
;; run: looped 0 3 => i32:96
;; run: looped 1 3 => i32:3153
;; run: looped 2 3 => i32:6123
;; run: looped 3 3 => i32:93
(module
  (type $base (sub (struct (field i32))))
  (type $a (sub $base (struct (field i32) (field i32))))
  (type $ints (array (mut i32)))
  (type $objs (array (mut (ref null $base))))
  (global $obj (mut (ref null $base)) (struct.new $a (i32.const 6) (i32.const 60)))
  (global $objb (mut (ref null $base)) (struct.new $base (i32.const 9)))
  (global $buf (mut (ref $ints))
    (array.new_fixed $ints 4 (i32.const 10) (i32.const 20) (i32.const 30) (i32.const 40)))
  (global $tab (mut (ref $objs))
    (array.new_fixed $objs 4 (struct.new $a (i32.const 5) (i32.const 50)) (struct.new $base (i32.const 6))
      (struct.new $a (i32.const 7) (i32.const 70)) (struct.new $base (i32.const 8))))
  (global $tabn (mut (ref $objs))
    (array.new_fixed $objs 4 (struct.new $base (i32.const 1)) (ref.null $base)
      (struct.new $base (i32.const 3)) (ref.null $base)))
  ;; c + 3k, recursively so that it is not inlined
  (func $slow (param $c i32) (param $k i32) (result i32)
    (if (result i32) (i32.eqz (local.get $k))
      (then (local.get $c))
      (else (call $slow (i32.add (local.get $c) (i32.const 3)) (i32.sub (local.get $k) (i32.const 1))))))
  (func $kind3 (export "kind3") (param $k i32) (param $i i32) (result i32)
    (local $key i32) (local $x i32) (local $y i32) (local $r i32)
    (if (i32.eqz (ref.test (ref $a) (global.get $obj)))
      (then (local.set $r (call $slow (i32.const 1) (i32.const 2)))))
    (local.set $key (i32.and (local.get $k) (i32.const 3)))
    (if (i32.eq (local.get $key) (i32.const 1))
      (then (local.set $x (i32.add (array.get $ints (global.get $buf) (i32.and (local.get $i) (i32.const 3))) (i32.const 1000))))
      (else
        (if (i32.eq (local.get $key) (i32.const 2))
          (then (local.set $x (i32.add (array.get $ints (global.get $buf) (i32.and (i32.xor (local.get $i) (i32.const 1)) (i32.const 3))) (i32.const 2000))))
          (else (local.set $x (array.get $ints (global.get $buf) (i32.and (i32.xor (local.get $i) (i32.const 2)) (i32.const 3))))))))
    (local.set $y (i32.add (i32.mul (local.get $x) (i32.const 3)) (local.get $i)))
    (if (i32.and (local.get $y) (i32.const 1))
      (then (array.set $ints (global.get $buf) (i32.const 2) (local.get $y)))
      (else (array.set $ints (global.get $buf) (i32.const 3) (i32.add (local.get $y) (i32.const 1)))))
    (if (local.get $key)
      (then (array.set $ints (global.get $buf) (i32.const 0) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 5))))
      (else (array.set $ints (global.get $buf) (i32.const 1) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 7)))))
    (local.set $r
      (i32.add (i32.add (i32.add (local.get $r) (array.get $ints (global.get $buf) (i32.const 0))) (array.get $ints (global.get $buf) (i32.const 2))) (array.get $ints (global.get $buf) (i32.const 3))))
    (local.get $r))
  (func $two (export "two") (param $k i32) (param $i i32) (result i32)
    (local $key i32) (local $x i32) (local $y i32) (local $r i32)
    (if (i32.eqz (ref.test (ref $a) (global.get $obj)))
      (then (local.set $r (call $slow (i32.const 1) (i32.const 2)))))
    (local.set $key (i32.and (local.get $k) (i32.const 3)))
    (if (i32.eq (local.get $key) (i32.const 1))
      (then (local.set $x (i32.add (array.get $ints (global.get $buf) (i32.and (local.get $i) (i32.const 3))) (i32.const 1000))))
      (else (local.set $x (array.get $ints (global.get $buf) (i32.and (i32.xor (local.get $i) (i32.const 2)) (i32.const 3))))))
    (local.set $y (i32.add (i32.mul (local.get $x) (i32.const 3)) (local.get $i)))
    (if (i32.and (local.get $y) (i32.const 1))
      (then (array.set $ints (global.get $buf) (i32.const 2) (local.get $y)))
      (else (array.set $ints (global.get $buf) (i32.const 3) (i32.add (local.get $y) (i32.const 1)))))
    (if (i32.eq (local.get $key) (i32.const 1))
      (then (array.set $ints (global.get $buf) (i32.const 0) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 5))))
      (else (array.set $ints (global.get $buf) (i32.const 1) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 7)))))
    (local.set $r
      (i32.add (i32.add (i32.add (local.get $r) (array.get $ints (global.get $buf) (i32.const 0))) (array.get $ints (global.get $buf) (i32.const 2))) (array.get $ints (global.get $buf) (i32.const 3))))
    (local.get $r))
  (func $refval (export "refval") (param $k i32) (param $i i32) (result i32)
    (local $key i32) (local $x i32) (local $y i32) (local $r i32) (local $o (ref null $base))
    (if (i32.eqz (ref.test (ref $a) (global.get $obj)))
      (then (local.set $r (call $slow (i32.const 1) (i32.const 2)))))
    (local.set $key (i32.and (local.get $k) (i32.const 3)))
    (if (i32.eq (local.get $key) (i32.const 1))
      (then (local.set $x (i32.add (array.get $ints (global.get $buf) (i32.and (local.get $i) (i32.const 3))) (i32.const 1000))))
      (else
        (if (i32.eq (local.get $key) (i32.const 2))
          (then (local.set $x (i32.add (array.get $ints (global.get $buf) (i32.and (i32.xor (local.get $i) (i32.const 1)) (i32.const 3))) (i32.const 2000))))
          (else (local.set $x (array.get $ints (global.get $buf) (i32.and (i32.xor (local.get $i) (i32.const 2)) (i32.const 3))))))))
    (local.set $o (array.get $objs (global.get $tab) (i32.and (local.get $i) (i32.const 3))))
    (local.set $y (i32.add (i32.mul (i32.add (local.get $x) (struct.get $base 0 (local.get $o))) (i32.const 3)) (local.get $i)))
    (if (i32.and (local.get $y) (i32.const 1))
      (then (array.set $ints (global.get $buf) (i32.const 2) (local.get $y)))
      (else (array.set $ints (global.get $buf) (i32.const 3) (i32.add (local.get $y) (i32.const 1)))))
    (if (local.get $key)
      (then (array.set $ints (global.get $buf) (i32.const 0) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 5))))
      (else (array.set $ints (global.get $buf) (i32.const 1) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 7)))))
    (local.set $r
      (i32.add (i32.add (i32.add (local.get $r) (array.get $ints (global.get $buf) (i32.const 0))) (array.get $ints (global.get $buf) (i32.const 2))) (array.get $ints (global.get $buf) (i32.const 3))))
    (local.set $r (i32.add (local.get $r) (i32.mul (ref.test (ref $a) (local.get $o)) (i32.const 100000))))
    (local.get $r))
  (func $rare (export "rare") (param $k i32) (param $i i32) (result i32)
    (local $key i32) (local $x i32) (local $y i32) (local $r i32)
    (if (i32.eqz (ref.test (ref $a) (global.get $objb)))
      (then
    (local.set $key (i32.and (local.get $k) (i32.const 3)))
    (if (i32.eq (local.get $key) (i32.const 1))
      (then (local.set $x (i32.add (array.get $ints (global.get $buf) (i32.and (local.get $i) (i32.const 3))) (i32.const 1000))))
      (else
        (if (i32.eq (local.get $key) (i32.const 2))
          (then (local.set $x (i32.add (array.get $ints (global.get $buf) (i32.and (i32.xor (local.get $i) (i32.const 1)) (i32.const 3))) (i32.const 2000))))
          (else (local.set $x (array.get $ints (global.get $buf) (i32.and (i32.xor (local.get $i) (i32.const 2)) (i32.const 3))))))))
    (local.set $y (i32.add (i32.mul (local.get $x) (i32.const 3)) (local.get $i)))
    (if (i32.and (local.get $y) (i32.const 1))
      (then (array.set $ints (global.get $buf) (i32.const 2) (local.get $y)))
      (else (array.set $ints (global.get $buf) (i32.const 3) (i32.add (local.get $y) (i32.const 1)))))
    (if (local.get $key)
      (then (array.set $ints (global.get $buf) (i32.const 0) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 5))))
      (else (array.set $ints (global.get $buf) (i32.const 1) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 7)))))
    (local.set $r
      (i32.add (i32.add (i32.add (local.get $r) (array.get $ints (global.get $buf) (i32.const 0))) (array.get $ints (global.get $buf) (i32.const 2))) (array.get $ints (global.get $buf) (i32.const 3))))
      ))
    (local.get $r))
  (func $plain (export "plain") (param $k i32) (param $i i32) (result i32)
    (local $key i32) (local $x i32) (local $y i32) (local $r i32)
    (local.set $key (i32.and (local.get $k) (i32.const 3)))
    (if (i32.eq (local.get $key) (i32.const 1))
      (then (local.set $x (i32.add (array.get $ints (global.get $buf) (i32.and (local.get $i) (i32.const 3))) (i32.const 1000))))
      (else
        (if (i32.eq (local.get $key) (i32.const 2))
          (then (local.set $x (i32.add (array.get $ints (global.get $buf) (i32.and (i32.xor (local.get $i) (i32.const 1)) (i32.const 3))) (i32.const 2000))))
          (else (local.set $x (array.get $ints (global.get $buf) (i32.and (i32.xor (local.get $i) (i32.const 2)) (i32.const 3))))))))
    (local.set $y (i32.add (i32.mul (local.get $x) (i32.const 3)) (local.get $i)))
    (if (i32.and (local.get $y) (i32.const 1))
      (then (array.set $ints (global.get $buf) (i32.const 2) (local.get $y)))
      (else (array.set $ints (global.get $buf) (i32.const 3) (i32.add (local.get $y) (i32.const 1)))))
    (if (local.get $key)
      (then (array.set $ints (global.get $buf) (i32.const 0) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 5))))
      (else (array.set $ints (global.get $buf) (i32.const 1) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 7)))))
    (local.set $r
      (i32.add (i32.add (i32.add (local.get $r) (array.get $ints (global.get $buf) (i32.const 0))) (array.get $ints (global.get $buf) (i32.const 2))) (array.get $ints (global.get $buf) (i32.const 3))))
    (local.get $r))
  (func $once (export "once") (param $k i32) (param $i i32) (result i32)
    (local $key i32) (local $x i32) (local $y i32) (local $r i32)
    (if (i32.eqz (ref.test (ref $a) (global.get $obj)))
      (then (local.set $r (call $slow (i32.const 1) (i32.const 2)))))
    (local.set $key (i32.and (local.get $k) (i32.const 3)))
    (if (i32.eq (local.get $key) (i32.const 1))
      (then (local.set $x (i32.add (array.get $ints (global.get $buf) (i32.and (local.get $i) (i32.const 3))) (i32.const 1000))))
      (else (local.set $x (array.get $ints (global.get $buf) (i32.and (i32.xor (local.get $i) (i32.const 2)) (i32.const 3))))))
    (local.set $y (i32.add (i32.mul (local.get $x) (i32.const 3)) (local.get $i)))
    (if (i32.and (local.get $y) (i32.const 1))
      (then (array.set $ints (global.get $buf) (i32.const 2) (local.get $y)))
      (else (array.set $ints (global.get $buf) (i32.const 3) (i32.add (local.get $y) (i32.const 1)))))
    (if (i32.and (local.get $i) (i32.const 1))
      (then (array.set $ints (global.get $buf) (i32.const 0) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 5))))
      (else (array.set $ints (global.get $buf) (i32.const 1) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 7)))))
    (local.set $r
      (i32.add (i32.add (i32.add (local.get $r) (array.get $ints (global.get $buf) (i32.const 0))) (array.get $ints (global.get $buf) (i32.const 2))) (array.get $ints (global.get $buf) (i32.const 3))))
    (local.get $r))
  (func $called (export "called") (param $k i32) (param $i i32) (result i32)
    (local $key i32) (local $x i32) (local $y i32) (local $r i32)
    (if (i32.eqz (ref.test (ref $a) (global.get $obj)))
      (then (local.set $r (call $slow (i32.const 1) (i32.const 2)))))
    (local.set $key (i32.and (local.get $k) (i32.const 3)))
    (if (i32.eq (local.get $key) (i32.const 1))
      (then (local.set $x (i32.add (array.get $ints (global.get $buf) (i32.and (local.get $i) (i32.const 3))) (i32.const 1000))))
      (else
        (if (i32.eq (local.get $key) (i32.const 2))
          (then (local.set $x (i32.add (array.get $ints (global.get $buf) (i32.and (i32.xor (local.get $i) (i32.const 1)) (i32.const 3))) (i32.const 2000))))
          (else (local.set $x (array.get $ints (global.get $buf) (i32.and (i32.xor (local.get $i) (i32.const 2)) (i32.const 3))))))))
    (local.set $y (i32.add (i32.mul (i32.add (local.get $x) (call $slow (local.get $i) (i32.const 2))) (i32.const 3)) (local.get $i)))
    (if (i32.and (local.get $y) (i32.const 1))
      (then (array.set $ints (global.get $buf) (i32.const 2) (local.get $y)))
      (else (array.set $ints (global.get $buf) (i32.const 3) (i32.add (local.get $y) (i32.const 1)))))
    (if (local.get $key)
      (then (array.set $ints (global.get $buf) (i32.const 0) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 5))))
      (else (array.set $ints (global.get $buf) (i32.const 1) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 7)))))
    (local.set $r
      (i32.add (i32.add (i32.add (local.get $r) (array.get $ints (global.get $buf) (i32.const 0))) (array.get $ints (global.get $buf) (i32.const 2))) (array.get $ints (global.get $buf) (i32.const 3))))
    (local.get $r))
  (func $looped (export "looped") (param $k i32) (param $i i32) (result i32)
    (local $key i32) (local $x i32) (local $s i32) (local $c i32) (local $r i32)
    (if (i32.eqz (ref.test (ref $a) (global.get $obj)))
      (then (local.set $r (call $slow (i32.const 1) (i32.const 2)))))
    (local.set $key (i32.and (local.get $k) (i32.const 3)))
    (if (i32.eq (local.get $key) (i32.const 1))
      (then (local.set $x (i32.add (array.get $ints (global.get $buf) (i32.and (local.get $i) (i32.const 3))) (i32.const 1000))))
      (else
        (if (i32.eq (local.get $key) (i32.const 2))
          (then (local.set $x (i32.add (array.get $ints (global.get $buf) (i32.and (i32.xor (local.get $i) (i32.const 1)) (i32.const 3))) (i32.const 2000))))
          (else (local.set $x (array.get $ints (global.get $buf) (i32.and (i32.xor (local.get $i) (i32.const 2)) (i32.const 3))))))))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $c) (local.get $i)))
        (local.set $s (i32.add (local.get $s) (local.get $x)))
        (if (local.get $key)
          (then (array.set $ints (global.get $buf) (i32.const 0)
                  (i32.add (array.get $ints (global.get $buf) (i32.const 0)) (i32.const 1))))
          (else (array.set $ints (global.get $buf) (i32.const 1)
                  (i32.add (array.get $ints (global.get $buf) (i32.const 1)) (i32.const 2)))))
        (local.set $c (i32.add (local.get $c) (i32.const 1)))
        (br $l)))
    (i32.add (i32.add (local.get $s) (array.get $ints (global.get $buf) (i32.const 0))) (array.get $ints (global.get $buf) (i32.const 1))))
  (func $nullkey (export "nullkey") (param $k i32) (param $i i32) (result i32)
    (local $key i32) (local $x i32) (local $y i32) (local $r i32) (local $o (ref null $base))
    (if (i32.eqz (ref.test (ref $a) (global.get $obj)))
      (then (local.set $r (call $slow (i32.const 1) (i32.const 2)))))
    (local.set $o (array.get $objs (global.get $tabn) (i32.and (local.get $k) (i32.const 3))))
    (if (ref.is_null (local.get $o))
      (then (local.set $x (i32.add (array.get $ints (global.get $buf) (i32.and (local.get $i) (i32.const 3))) (i32.const 1000))))
      (else (local.set $x (array.get $ints (global.get $buf) (i32.and (i32.xor (local.get $i) (i32.const 2)) (i32.const 3))))))
    (local.set $y (i32.add (i32.mul (local.get $x) (i32.const 3)) (local.get $i)))
    (if (i32.and (local.get $y) (i32.const 1))
      (then (array.set $ints (global.get $buf) (i32.const 2) (local.get $y)))
      (else (array.set $ints (global.get $buf) (i32.const 3) (i32.add (local.get $y) (i32.const 1)))))
    (if (ref.is_null (local.get $o))
      (then (array.set $ints (global.get $buf) (i32.const 0) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 5))))
      (else (array.set $ints (global.get $buf) (i32.const 1) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 7)))))
    (local.set $r
      (i32.add (i32.add (i32.add (local.get $r) (array.get $ints (global.get $buf) (i32.const 0))) (array.get $ints (global.get $buf) (i32.const 2))) (array.get $ints (global.get $buf) (i32.const 3))))
    (local.get $r))
  (func $hinted (export "hinted") (param $k i32) (param $i i32) (result i32)
    (local $key i32) (local $x i32) (local $y i32) (local $r i32)
    (if (i32.eqz (ref.test (ref $a) (global.get $obj)))
      (then (local.set $r (call $slow (i32.const 1) (i32.const 2)))))
    (local.set $key (i32.and (local.get $k) (i32.const 3)))
    (@metadata.code.branch_hint "\00") (if (i32.eq (local.get $key) (i32.const 1))
      (then (local.set $x (i32.add (array.get $ints (global.get $buf) (i32.and (local.get $i) (i32.const 3))) (i32.const 1000))))
      (else
        (if (i32.eq (local.get $key) (i32.const 2))
          (then (local.set $x (i32.add (array.get $ints (global.get $buf) (i32.and (i32.xor (local.get $i) (i32.const 1)) (i32.const 3))) (i32.const 2000))))
          (else (local.set $x (array.get $ints (global.get $buf) (i32.and (i32.xor (local.get $i) (i32.const 2)) (i32.const 3))))))))
    (local.set $y (i32.add (i32.mul (local.get $x) (i32.const 3)) (local.get $i)))
    (if (i32.and (local.get $y) (i32.const 1))
      (then (array.set $ints (global.get $buf) (i32.const 2) (local.get $y)))
      (else (array.set $ints (global.get $buf) (i32.const 3) (i32.add (local.get $y) (i32.const 1)))))
    (if (local.get $key)
      (then (array.set $ints (global.get $buf) (i32.const 0) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 5))))
      (else (array.set $ints (global.get $buf) (i32.const 1) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 7)))))
    (local.set $r
      (i32.add (i32.add (i32.add (local.get $r) (array.get $ints (global.get $buf) (i32.const 0))) (array.get $ints (global.get $buf) (i32.const 2))) (array.get $ints (global.get $buf) (i32.const 3))))
    (local.get $r))
  (func $rarearm (export "rarearm") (param $k i32) (param $i i32) (result i32)
    (local $key i32) (local $x i32) (local $y i32) (local $r i32)
    (if (i32.eqz (ref.test (ref $a) (global.get $obj)))
      (then (local.set $r (call $slow (i32.const 1) (i32.const 2)))))
    (local.set $key (i32.and (local.get $k) (i32.const 3)))
    (if (i32.eq (local.get $key) (i32.const 1))
      (then (local.set $x (i32.add (array.get $ints (global.get $buf) (i32.and (local.get $i) (i32.const 3))) (i32.const 1000))))
      (else
        (if (i32.eq (local.get $key) (i32.const 2))
          (then (local.set $x (i32.add (array.get $ints (global.get $buf) (i32.and (i32.xor (local.get $i) (i32.const 1)) (i32.const 3))) (i32.const 2000))))
          (else (local.set $x (array.get $ints (global.get $buf) (i32.and (i32.xor (local.get $i) (i32.const 2)) (i32.const 3))))))))
    (local.set $y (i32.add (i32.mul (local.get $x) (i32.const 3)) (local.get $i)))
    (if (i32.and (local.get $y) (i32.const 1))
      (then (array.set $ints (global.get $buf) (i32.const 2) (local.get $y)))
      (else (array.set $ints (global.get $buf) (i32.const 3) (i32.add (local.get $y) (i32.const 1)))))
    (local.set $r (i32.const 0))
    (if (i32.eqz (ref.test (ref $a) (global.get $objb)))
      (then
    (if (local.get $key)
      (then (array.set $ints (global.get $buf) (i32.const 0) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 5))))
      (else (array.set $ints (global.get $buf) (i32.const 1) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 7)))))
    (local.set $r
      (i32.add (i32.add (i32.add (local.get $r) (array.get $ints (global.get $buf) (i32.const 0))) (array.get $ints (global.get $buf) (i32.const 2))) (array.get $ints (global.get $buf) (i32.const 3))))
      ))
    (local.get $r))
  (func $fromrare (export "fromrare") (param $k i32) (param $i i32) (result i32)
    (local $key i32) (local $x i32) (local $y i32) (local $r i32)
    (local.set $key (i32.and (local.get $k) (i32.const 3)))
    (local.set $x (i32.const 9))
    (block $on
      (br_if $on (ref.test (ref $a) (global.get $objb)))
      (local.set $x (i32.const 5))
      (br_if $on (i32.eq (local.get $key) (i32.const 1)))
      (local.set $x (i32.const 9)))
    (local.set $y (i32.add (i32.mul (local.get $x) (i32.const 3)) (local.get $i)))
    (if (i32.and (local.get $y) (i32.const 1))
      (then (array.set $ints (global.get $buf) (i32.const 2) (local.get $y)))
      (else (array.set $ints (global.get $buf) (i32.const 3) (i32.add (local.get $y) (i32.const 1)))))
    (if (local.get $key)
      (then (array.set $ints (global.get $buf) (i32.const 0) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 5))))
      (else (array.set $ints (global.get $buf) (i32.const 1) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 7)))))
    (local.set $r
      (i32.add (i32.add (i32.add (local.get $r) (array.get $ints (global.get $buf) (i32.const 0))) (array.get $ints (global.get $buf) (i32.const 2))) (array.get $ints (global.get $buf) (i32.const 3))))
    (local.get $r))
  (func $helper (param $k i32) (param $i i32) (result i32)
    (local $key i32) (local $x i32) (local $y i32) (local $r i32)
    (if (i32.eqz (ref.test (ref $a) (global.get $obj)))
      (then (local.set $r (call $slow (i32.const 1) (i32.const 2)))))
    (local.set $key (i32.and (local.get $k) (i32.const 3)))
    (if (i32.eq (local.get $key) (i32.const 1))
      (then (local.set $x (i32.add (array.get $ints (global.get $buf) (i32.and (local.get $i) (i32.const 3))) (i32.const 1000))))
      (else
        (if (i32.eq (local.get $key) (i32.const 2))
          (then (local.set $x (i32.add (array.get $ints (global.get $buf) (i32.and (i32.xor (local.get $i) (i32.const 1)) (i32.const 3))) (i32.const 2000))))
          (else (local.set $x (array.get $ints (global.get $buf) (i32.and (i32.xor (local.get $i) (i32.const 2)) (i32.const 3))))))))
    (local.set $y (i32.add (i32.mul (local.get $x) (i32.const 3)) (local.get $i)))
    (if (i32.and (local.get $y) (i32.const 1))
      (then (array.set $ints (global.get $buf) (i32.const 2) (local.get $y)))
      (else (array.set $ints (global.get $buf) (i32.const 3) (i32.add (local.get $y) (i32.const 1)))))
    (if (local.get $key)
      (then (array.set $ints (global.get $buf) (i32.const 0) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 5))))
      (else (array.set $ints (global.get $buf) (i32.const 1) (local.get $y))
            (local.set $r (i32.add (local.get $y) (i32.const 7)))))
    (local.set $r
      (i32.add (i32.add (i32.add (local.get $r) (array.get $ints (global.get $buf) (i32.const 0))) (array.get $ints (global.get $buf) (i32.const 2))) (array.get $ints (global.get $buf) (i32.const 3))))
    (local.get $r))
  (func $inl (export "inl") (param $k i32) (param $i i32) (result i32)
    (i32.add (call $helper (local.get $k) (local.get $i)) (i32.const 1)))
)
