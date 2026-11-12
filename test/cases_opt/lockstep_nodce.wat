;; test/cases_opt/lockstep.wat's counters with no pass after to take away
;; what pass_iv_lockstep leaves unread (-fno-dce): the step of a counter
;; taken out is gone with its parameter, and reads nothing that is no more.
;; From test/gen_lockstep.py.
;;
;; flags: -O2 -fno-dce
;;
;; stats -O2: lockstep=6 lockstep_exits=1 lockstep_addrs=1
;;
;; run: same 0 => i32:36
;; run: same 4 => i32:36
;; run: same 9 => i32:36
;; run: same 16 => i32:76
;; run: same 18 => i32:76
;; run: same 48 => i32:220
;; run: oldafter 0 => i32:1219
;; run: oldafter 2 => i32:1235
;; run: oldafter 4 => i32:1251
;; run: oldafter 9 => i32:1259
;; run: threeout 0 => i32:1091919
;; run: threeout 5 => i32:1041414
;; run: threeout 9 => i32:1001010
;; run: threeout 16 => i32:1031323
;; run: threeout 48 => i32:1071763
;; run: outret 0 => i32:64
;; run: outret 2 => i32:66
;; run: outret 4 => i32:68
;; run: outret 9 => i32:69
;; run: outbrif 0 => i32:111
;; run: outbrif 2 => i32:102
;; run: outbrif 4 => i32:111
;; run: outbrif 9 => i32:111
;; run: late 0 => i32:3
;; run: late 4 => i32:110
;; run: late 11 => i32:211
;; run: late 20 => i32:320
;; run: late 45 => i32:845
;; run: addr 100 0 => i32:102
;; run: addr 100 1 => i32:102
;; run: addr 7 5 => i32:497
;; run: addr 4294967294 9 => i32:-7472

(module
  (type $t1 (func (param i32)))
  (memory 1)
  (table 1 funcref)
  (elem (i32.const 0) $thrower)
  (tag $e)
  (data (i32.const 0) "\03\07\01\09\04\06\02\08\05\00\00\00\00\00\00\00\01\01\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\02\03\04\05\06\00\00\00\00\00\00\00\00\00\02\04\06\08\01\03\05\00\00\00\00\00\00\00\00\00")
  ;; throws at 3; called through the table at an index read from memory, which keeps it out of line
  (func $thrower (type $t1) (if (i32.eq (local.get 0) (i32.const 3)) (then (throw $e))))
  (func $same (export "same") (param $a i32) (result i32)
    (local $i i32) (local $j i32)
    (local.set $i (local.get $a))
    (local.set $j (local.get $a))
    (block $out
      (loop $l
        (br_if $out (i32.eqz (i32.load8_u (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $j) (i32.const 3)) (local.get $i)))
  (func $oldafter (export "oldafter") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $o i32)
    (local.set $i (i32.const 38))
    (local.set $j (i32.const 1000))
    (loop $l
      (local.set $o (local.get $j))
      (local.set $j (i32.sub (local.get $j) (i32.const 1)))
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (br_if $l (i32.gt_u (i32.load8_u (local.get $i)) (local.get $n))))
    (i32.add (local.get $o) (i32.mul (local.get $i) (i32.const 7))))
  (func $threeout (export "threeout") (param $p i32) (result i32)
    (local $c1 i32) (local $c2 i32) (local $c3 i32)
    (local.set $c1 (i32.const 1))
    (local.set $c2 (i32.const 10))
    (local.set $c3 (i32.const 100))
    (block $out
      (loop $l
        (br_if $out (i32.eqz (i32.load8_u (local.get $p))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (local.set $c1 (i32.add (local.get $c1) (i32.const 1)))
        (local.set $c2 (i32.add (local.get $c2) (i32.const 1)))
        (local.set $c3 (i32.add (local.get $c3) (i32.const 1)))
        (br $l)))
    (i32.add (i32.add (local.get $c1) (i32.mul (local.get $c2) (i32.const 100)))
             (i32.add (i32.mul (local.get $c3) (i32.const 10000)) (local.get $p))))
  (func $outret (export "outret") (param $n i32) (result i32)
    (local $i i32) (local $j i32)
    (local.set $i (i32.const 38))
    (local.set $j (i32.const 70))
    (loop $l
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (local.set $j (i32.sub (local.get $j) (i32.const 1)))
      (br_if $l (i32.gt_u (i32.load8_u (local.get $i)) (local.get $n))))
    (local.get $j))
  (func $outbrif (export "outbrif") (param $n i32) (result i32)
    (local $i i32) (local $j i32)
    (local.set $i (i32.const 38))
    (local.set $j (i32.const 4))
    (loop $l
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (local.set $j (i32.sub (local.get $j) (i32.const 1)))
      (br_if $l (i32.gt_u (i32.load8_u (local.get $i)) (local.get $n))))
    (if (result i32) (local.get $j)
      (then (i32.store8 (i32.const 210) (local.get $n)) (i32.const 111))
      (else (i32.store8 (i32.const 210) (local.get $i)) (i32.mul (local.get $i) (i32.const 3)))))
  (func $late (export "late") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32)
    (loop $l
      (local.set $o (local.get $i))
      (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (i32.mul (local.get $o) (i32.const 100)) (local.get $s)))
  (func $addr (export "addr") (param $b i32) (param $n i32) (result i32)
    (local $i i32) (local $q i32) (local $s i32)
    (loop $l
      (local.set $q (i32.add (local.get $i) (i32.const 1)))
      (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 3)) (i32.add (i32.add (local.get $b) (local.get $i)) (i32.const 1))))
      (local.set $i (local.get $q))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (i32.add (local.get $s) (local.get $i)))
)
