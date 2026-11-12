;; A select whose condition each way into its block decides is what that
;; way picked: a parameter of the block, handed the one operand or the other
;; on each edge (opt.c pass_bit_facts).  Each function scans bytes and stops
;; several ways, then picks by the byte it stopped at.  From
;; test/gen_way_select.py, which works out every answer.
;;
;;   find      two ways out, at a zero and at the byte looked for: the one
;;             hands on -1, a constant made in the select's block, the other
;;             the place
;;   three     three ways, the operands values made before the loop
;;   open      one way out is taken before the byte is loaded: the select
;;             stays
;;   inblock   the operands are loads made in the select's block, which no
;;             way in has: it stays
;;   inblock1 inblock2
;;             one of the two is
;;   holes     a way out on `c != 7`: the byte is 0 to 6 or over 7 there,
;;             zero among them, and the select stays
;;   wide      a halfword found over 255 before it is tested for zero: not
;;             zero, by its high byte alone
;;   consts    both operands constants of the block, the ways in from two
;;             arms of a branch
;;   agree_in  the ways agree on an operand made in the block: that one
;;   oneway    one way in, an arm of a branch on the value: the operand
;;   agree     both ways find the byte not zero: the operand
;;   ways8     eight ways in, as many as are looked at
;;   ways9     nine: the select stays
;;   made      what one way picks is made in the select's block for it
;;             alone, of a value from above and a constant: made on that
;;             way
;;   madeboth  what each picks is
;;   made2     read once more beside the select: it stays, and the select
;;   madein    made of a byte the block loads: it stays
;;   mademul   a multiplication: it stays
;;   ehconst   a way in ends in a call a handler stands behind: the
;;             constant it picks is made ahead of that call
;;   ehmade    and what it picks, made in the select's block, is too
;;   ehresult  what one way picks is made of a call's result: on the way
;;             that begins after the call
;;
;; stats -O2: way_selects=14 way_select_made=5
;; stats -O2 -fno-fold: way_selects=0
;; shape: find \scs(el|inv|inc|neg)\s=0
;; shape: three \scs(el|inv|inc|neg)\s=0
;; shape: open \scs(el|inv|inc|neg)\s=1
;; shape: inblock \scs(el|inv|inc|neg)\s=1
;; shape: inblock2 \scs(el|inv|inc|neg)\s=1
;; shape: inblock1 \scs(el|inv|inc|neg)\s=1
;; shape: holes \scs(el|inv|inc|neg)\s=1
;; shape: wide \scs(el|inv|inc|neg)\s=0
;; shape: consts \scs(el|inv|inc|neg)\s=0
;; shape: agree_in \scs(el|inv|inc|neg)\s=0
;; shape: oneway \scs(el|inv|inc|neg)\s=0
;; shape: agree \scs(el|inv|inc|neg)\s=0
;; shape: ways8 \scs(el|inv|inc|neg)\s=0
;; shape: ways9 \scs(el|inv|inc|neg)\s=1
;; shape: made \scs(el|inv|inc|neg)\s=0
;; shape: madeboth \scs(el|inv|inc|neg)\s=0
;; shape: made2 \scs(el|inv|inc|neg)\s=1
;; shape: madein \scs(el|inv|inc|neg)\s=1
;; shape: mademul \scs(el|inv|inc|neg)\s=1
;; shape: ehconst \scs(el|inv|inc|neg)\s=0
;; shape: ehmade \scs(el|inv|inc|neg)\s=0
;; shape: ehresult \scs(el|inv|inc|neg)\s=0
;;
;; run: find 0 3 => i32:2
;; run: find 0 200 => i32:3
;; run: find 0 77 => i32:-1
;; run: find 2 5 => i32:5
;; run: find 7 13 => i32:8
;; run: find 7 99 => i32:-1
;; run: find 6 1 => i32:-1
;; run: find 10 2 => i32:11
;; run: three 0 5 9 => i32:18
;; run: three 4 5 9 => i32:89
;; run: three 7 100 3 => i32:98
;; run: three 10 1 2 => i32:13
;; run: three 11 7 255 => i32:180
;; run: open 0 3 => i32:3
;; run: open 0 100 => i32:-1
;; run: open 4 4 => i32:-1
;; run: open 6 0 => i32:-1
;; run: open 7 8 => i32:8
;; run: open 10 30 => i32:-1
;; run: inblock 0 3 => i32:200
;; run: inblock 0 77 => i32:13
;; run: inblock 2 5 => i32:0
;; run: inblock 7 99 => i32:2
;; run: inblock 10 4 => i32:6
;; run: inblock2 0 3 => i32:3
;; run: inblock2 0 77 => i32:13
;; run: inblock2 2 5 => i32:5
;; run: inblock2 7 99 => i32:2
;; run: inblock2 10 4 => i32:4
;; run: inblock1 0 3 => i32:200
;; run: inblock1 0 77 => i32:77
;; run: inblock1 2 5 => i32:0
;; run: inblock1 7 99 => i32:99
;; run: inblock1 10 4 => i32:6
;; run: holes 32 99 => i32:-1
;; run: holes 32 33 => i32:33
;; run: holes 36 99 => i32:38
;; run: holes 36 37 => i32:37
;; run: holes 39 99 => i32:-1
;; run: holes 35 99 => i32:-1
;; run: wide 48 5 => i32:50
;; run: wide 50 6 => i32:50
;; run: wide 52 7 => i32:7
;; run: wide 56 8 => i32:58
;; run: consts 0 => i32:7785
;; run: consts 4 => i32:7785
;; run: consts 5 => i32:7785
;; run: consts 7 => i32:7785
;; run: consts 11 => i32:100014
;; run: consts 12 => i32:100014
;; run: agree_in 0 3 => i32:1
;; run: agree_in 0 9 => i32:1
;; run: agree_in 4 5 => i32:13
;; run: agree_in 7 13 => i32:13
;; run: agree_in 8 2 => i32:2
;; run: oneway 0 5 9 => i32:10
;; run: oneway 1 5 9 => i32:5
;; run: oneway 256 3 4 => i32:3
;; run: oneway 2147483648 1 2 => i32:1
;; run: agree 0 5 => i32:3
;; run: agree 4 9 => i32:7
;; run: agree 7 1 => i32:7
;; run: agree 8 2 => i32:10
;; run: ways8 0 77 => i32:4098
;; run: ways8 4 5 => i32:4100
;; run: ways8 7 9 => i32:4103
;; run: ways8 10 1 => i32:4106
;; run: ways8 11 3 => i32:4099
;; run: ways8 12 8 => i32:4104
;; run: ways9 0 77 => i32:4098
;; run: ways9 4 5 => i32:4100
;; run: ways9 7 9 => i32:4103
;; run: ways9 10 1 => i32:4106
;; run: ways9 11 3 => i32:4107
;; run: ways9 12 8 => i32:4104
;; run: made 0 3 => i32:102
;; run: made 0 77 => i32:77
;; run: made 2 5 => i32:105
;; run: made 7 99 => i32:99
;; run: made 10 4 => i32:112
;; run: madeboth 0 3 => i32:16
;; run: madeboth 0 77 => i32:4729
;; run: madeboth 2 5 => i32:40
;; run: madeboth 7 99 => i32:4695
;; run: madeboth 10 4 => i32:96
;; run: made2 0 3 => i32:1
;; run: made2 0 77 => i32:231
;; run: made2 2 5 => i32:4
;; run: made2 7 99 => i32:594
;; run: made2 10 4 => i32:81
;; run: madein 0 3 => i32:250
;; run: madein 0 77 => i32:77
;; run: madein 2 5 => i32:50
;; run: madein 7 99 => i32:99
;; run: madein 10 4 => i32:56
;; run: mademul 0 3 => i32:2000006
;; run: mademul 0 77 => i32:77
;; run: mademul 2 5 => i32:5000015
;; run: mademul 7 99 => i32:99
;; run: mademul 10 4 => i32:12000036
;; run: ehconst 0 5 => i32:5
;; run: ehconst 1 5 => i32:77
;; run: ehconst 3 5 => i32:-7
;; run: ehconst 2 2147483649 => i32:77
;; run: ehmade 0 5 => i32:5
;; run: ehmade 1 5 => i32:14
;; run: ehmade 3 5 => i32:-7
;; run: ehmade 2 2147483649 => i32:-2147483638
;; run: ehresult 0 5 => i32:34
;; run: ehresult 1 5 => i32:5
;; run: ehresult 0 3 => i32:-7
;; run: ehresult 1 3 => i32:-7
;; run: ehresult 0 2147483649 => i32:-2147483634

(module
  (type $t1 (func (param i32)))
  (type $t2 (func (param i32) (result i32)))
  (memory 1)
  (table 2 funcref)
  (elem (i32.const 0) $thrower $five)
  (tag $e)
  (data (i32.const 0) "\07\09\03\c8\01\05\00\0b\0d\00\ff\02\04\06\08\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\07\07\07\00\07\07\09\07\00\00\00\00\00\00\00\00\07\00\00\01\09\00\00\00\01\00\01\02\00\00\00\00")
  (data (i32.const 221) "\01")
  ;; throw at 3; called through the table at an index read from memory, which keeps them out of line
  (func $thrower (type $t1) (if (i32.eq (local.get 0) (i32.const 3)) (then (throw $e))))
  (func $five (type $t2) (if (i32.eq (local.get 0) (i32.const 3)) (then (throw $e)))
    (i32.mul (local.get 0) (i32.const 5)))
  (func $find (export "find") (param $p i32) (param $k i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (br_if $out (i32.eq (local.get $c) (local.get $k)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (select (local.get $p) (i32.const -1) (local.get $c)))
  (func $three (export "three") (param $p i32) (param $a i32) (param $b i32) (result i32)
    (local $c i32) (local $x i32) (local $y i32)
    (local.set $x (i32.mul (local.get $a) (i32.const 3)))
    (local.set $y (i32.xor (local.get $b) (i32.const 0x5a)))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eq (local.get $c) (i32.const 200)))
        (br_if $out (i32.eqz (local.get $c)))
        (br_if $out (i32.gt_u (local.get $c) (i32.const 250)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $p) (select (local.get $x) (local.get $y) (local.get $c))))
  (func $open (export "open") (param $p i32) (param $k i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $p) (local.get $k)))
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (select (local.get $p) (i32.const -1) (local.get $c)))
  (func $inblock (export "inblock") (param $p i32) (param $k i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (br_if $out (i32.eq (local.get $c) (local.get $k)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (select (i32.load8_u offset=1 (local.get $p)) (i32.load8_u offset=2 (local.get $p)) (local.get $c)))
  (func $inblock2 (export "inblock2") (param $p i32) (param $k i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (br_if $out (i32.eq (local.get $c) (local.get $k)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (select (local.get $k) (i32.load8_u offset=2 (local.get $p)) (local.get $c)))
  (func $inblock1 (export "inblock1") (param $p i32) (param $k i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (br_if $out (i32.eq (local.get $c) (local.get $k)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (select (i32.load8_u offset=1 (local.get $p)) (local.get $k) (local.get $c)))
  (func $holes (export "holes") (param $p i32) (param $k i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.ne (local.get $c) (i32.const 7)))
        (br_if $out (i32.ge_u (local.get $p) (local.get $k)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (select (local.get $p) (i32.const -1) (local.get $c)))
  (func $wide (export "wide") (param $p i32) (param $k i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (local.set $c (i32.load16_u (local.get $p)))
        (br_if $out (i32.gt_u (local.get $c) (i32.const 255)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $p (i32.add (local.get $p) (i32.const 2)))
        (br $l)))
    (select (local.get $p) (local.get $k) (local.get $c)))
  (func $consts (export "consts") (param $p i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (if (i32.and (local.get $p) (i32.const 1))
          (then (i32.store8 (i32.const 100) (local.get $c))
                (br_if $out (i32.eqz (local.get $c))))
          (else (i32.store8 (i32.const 101) (local.get $c))
                (br_if $out (i32.gt_u (local.get $c) (i32.const 12)))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $p) (select (i32.const 7777) (i32.const 99999) (local.get $c))))
  (func $agree_in (export "agree_in") (param $p i32) (param $k i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.gt_u (local.get $c) (i32.const 100)))
        (br_if $out (i32.eq (local.get $c) (i32.const 11)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (select (i32.load8_u offset=1 (local.get $p)) (i32.load8_u offset=2 (local.get $p)) (local.get $c)))
  (func $oneway (export "oneway") (param $c i32) (param $a i32) (param $b i32) (result i32)
    (if (result i32) (local.get $c)
      (then (i32.store (i32.const 60) (local.get $a))
            (select (local.get $a) (local.get $b) (local.get $c)))
      (else (i32.store (i32.const 60) (local.get $b))
            (i32.add (select (local.get $a) (local.get $b) (local.get $c)) (i32.const 1)))))
  (func $agree (export "agree") (param $p i32) (param $k i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eq (local.get $c) (i32.const 11)))
        (br_if $out (i32.gt_u (local.get $c) (i32.const 100)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (select (local.get $p) (local.get $k) (local.get $c)))
  (func $ways8 (export "ways8") (param $p i32) (param $a i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (br_if $out (i32.eq (local.get $c) (i32.const 3)))
        (br_if $out (i32.eq (local.get $c) (i32.const 200)))
        (br_if $out (i32.eq (local.get $c) (i32.const 1)))
        (br_if $out (i32.eq (local.get $c) (i32.const 5)))
        (br_if $out (i32.eq (local.get $c) (i32.const 11)))
        (br_if $out (i32.eq (local.get $c) (i32.const 13)))
        (br_if $out (i32.eq (local.get $c) (i32.const 255)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (i32.xor (select (local.get $p) (local.get $a) (local.get $c)) (i32.const 0x1000)))
  (func $ways9 (export "ways9") (param $p i32) (param $a i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (br_if $out (i32.eq (local.get $c) (i32.const 3)))
        (br_if $out (i32.eq (local.get $c) (i32.const 200)))
        (br_if $out (i32.eq (local.get $c) (i32.const 1)))
        (br_if $out (i32.eq (local.get $c) (i32.const 5)))
        (br_if $out (i32.eq (local.get $c) (i32.const 11)))
        (br_if $out (i32.eq (local.get $c) (i32.const 13)))
        (br_if $out (i32.eq (local.get $c) (i32.const 255)))
        (br_if $out (i32.eq (local.get $c) (i32.const 2)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (i32.xor (select (local.get $p) (local.get $a) (local.get $c)) (i32.const 0x1000)))
  (func $made (export "made") (param $p i32) (param $k i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (br_if $out (i32.eq (local.get $c) (local.get $k)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (select (i32.add (local.get $p) (i32.const 100)) (local.get $k) (local.get $c)))
  (func $madeboth (export "madeboth") (param $p i32) (param $k i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (br_if $out (i32.eq (local.get $c) (local.get $k)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (select (i32.shl (local.get $p) (i32.const 3)) (i32.xor (local.get $k) (i32.const 0x1234)) (local.get $c)))
  (func $made2 (export "made2") (param $p i32) (param $k i32) (result i32)
    (local $c i32) (local $x i32)
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (br_if $out (i32.eq (local.get $c) (local.get $k)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.set $x (i32.sub (local.get $p) (i32.const 3)))
    (i32.mul (select (local.get $x) (local.get $k) (local.get $c)) (local.get $x)))
  (func $madein (export "madein") (param $p i32) (param $k i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (br_if $out (i32.eq (local.get $c) (local.get $k)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (select (i32.add (i32.load8_u offset=1 (local.get $p)) (i32.const 50)) (local.get $k) (local.get $c)))
  (func $mademul (export "mademul") (param $p i32) (param $k i32) (result i32)
    (local $c i32)
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (br_if $out (i32.eq (local.get $c) (local.get $k)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (select (i32.mul (local.get $p) (i32.const 1000003)) (local.get $k) (local.get $c)))
  (func $ehconst (export "ehconst") (param $c i32) (param $a i32) (result i32)
    (block $h
      (try_table (catch_all $h)
        (if (local.get $c)
          (then (call_indirect (type $t1) (local.get $c) (i32.load8_u (i32.const 220))))
          (else (i32.store8 (i32.const 230) (local.get $a))))
        (return (select (i32.const 77) (local.get $a) (local.get $c)))))
    (i32.const -7))
  (func $ehmade (export "ehmade") (param $c i32) (param $a i32) (result i32)
    (block $h
      (try_table (catch_all $h)
        (if (local.get $c)
          (then (call_indirect (type $t1) (local.get $c) (i32.load8_u (i32.const 220))))
          (else (i32.store8 (i32.const 230) (local.get $a))))
        (return (select (i32.add (local.get $a) (i32.const 9)) (local.get $a) (local.get $c)))))
    (i32.const -7))
  (func $ehresult (export "ehresult") (param $c i32) (param $a i32) (result i32)
    (local $r i32)
    (block $h
      (try_table (catch_all $h)
        (local.set $r (call_indirect (type $t2) (local.get $a) (i32.load8_u (i32.const 221))))
        (if (local.get $c) (then (i32.store8 (i32.const 231) (local.get $a))))
        (return (select (local.get $a) (i32.add (local.get $r) (i32.const 9)) (local.get $c)))))
    (i32.const -7))
)
