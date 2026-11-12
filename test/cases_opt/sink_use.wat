;; A cheap value of one other and a constant, read in one block alone and
;; not the one it is made in, is made there, ahead of its first reader, when
;; its operand is read there too (opt.c pass_sink): it no longer lives from
;; the one block to the other, across a call or through a loop.  From
;; test/gen_sink.py, which works out every answer.
;;
;;   fibl       fib, its second call made a loop as TinyGo leaves it, n - 2
;;             made at the top and read where the loop goes round: made
;;             there, and the function saves two registers, not three
;;   toinst     read by an instruction after the arms meet, where x is stored
;;             too
;;   toret      returned there
;;   tobranch   branched on there
;;   toedge     handed on from an arm that stores x, to where that arm meets
;;             the other
;;   wide       64 bits, a shift right
;;   subk       a subtraction, an or, an arithmetic shift: each made there
;;   alone      x is not read where v is: v stays where it was made
;;   mulk       a multiplication is not one of the cheap ones
;;   twovals    of two values, neither a constant
;;   twoblocks  read in the arm and after it
;;   same       read in the block it is made in
;;   below2     read in two arms below the block where the first arms meet
;;             and x is stored: made there, above them both
;;   xbranch    x is read where v is stored only by the branch there
;;   ehend      read only by the edge out of a block that ends in a call a
;;             handler stands behind: nothing can be made after that call,
;;             and it stays where it was
;;   afterloop  made each time round a loop and read only after it, with its
;;             operand: made after it
;;   inner      made ahead of a loop and read in it: not made each time round
;;   ways       where two ways meet, each handing x to another of the block's
;;             parameters: neither is x
;;
;; stats -O2: sunk=12
;; shape: fibl \sstp\s=2
;;
;; run: fibl 0 => i32:0
;; run: fibl 1 => i32:1
;; run: fibl 2 => i32:1
;; run: fibl 5 => i32:5
;; run: fibl 12 => i32:144
;; run: fibl 20 => i32:6765
;; run: toinst 5 0 => i32:36
;; run: toinst 5 1 => i32:36
;; run: toinst 2147483632 1 => i32:2147483621
;; run: toinst 4294967289 0 => i32:0
;; run: toret 5 0 => i32:80
;; run: toret 5 1 => i32:80
;; run: toret 2147483632 1 => i32:2147483557
;; run: toret 4294967289 0 => i32:-84
;; run: tobranch 5 0 => i32:11
;; run: tobranch 5 1 => i32:11
;; run: tobranch 2147483632 1 => i32:22
;; run: tobranch 4294967289 0 => i32:22
;; run: toedge 5 0 => i32:9
;; run: toedge 5 2 => i32:40
;; run: toedge 5 3 => i32:40
;; run: toedge 4294967289 2 => i32:-56
;; run: wide 5 0 => i64:1
;; run: wide 1099511627776 1 => i64:2147483649
;; run: wide 18446744073709551615 1 => i64:36028797018963968
;; run: subk 5 0 => i32:4007
;; run: subk 5 1 => i32:4007
;; run: subk 2147483632 1 => i32:536870776
;; run: subk 4294967289 0 => i32:-116
;; run: alone 5 0 => i32:36
;; run: alone 5 1 => i32:36
;; run: alone 2147483632 1 => i32:2147483621
;; run: alone 4294967289 0 => i32:0
;; run: mulk 5 0 => i32:5000016
;; run: mulk 5 1 => i32:5000016
;; run: mulk 2147483632 1 => i32:2131483601
;; run: mulk 4294967289 0 => i32:-7000020
;; run: twovals 5 0 => i32:15
;; run: twovals 5 1 => i32:18
;; run: twovals 2147483632 1 => i32:2147483603
;; run: twovals 4294967289 0 => i32:-21
;; run: twoblocks 5 0 => i32:36
;; run: twoblocks 5 1 => i32:36
;; run: twoblocks 2147483632 1 => i32:2147483621
;; run: twoblocks 4294967289 0 => i32:0
;; run: same 5 0 => i32:36
;; run: same 5 1 => i32:36
;; run: same 2147483632 1 => i32:2147483621
;; run: same 4294967289 0 => i32:0
;; run: below2 5 0 => i32:60
;; run: below2 5 2 => i32:36
;; run: below2 5 3 => i32:36
;; run: below2 4294967289 1 => i32:0
;; run: xbranch 5 0 => i32:15
;; run: xbranch 5 1 => i32:15
;; run: xbranch 0 1 => i32:8
;; run: xbranch 4294967289 0 => i32:3
;; run: ehend 5 0 => i32:27
;; run: ehend 5 1 => i32:36
;; run: ehend 5 3 => i32:-7
;; run: ehend 4294967289 2 => i32:0
;; run: afterloop 0 5 => i32:937
;; run: afterloop 4 9 => i32:377
;; run: afterloop 9 1 => i32:151
;; run: inner 5 0 => i32:137169
;; run: inner 5 4 => i32:1575
;; run: inner 100 9 => i32:0
;; run: inner 4294967289 2 => i32:3730
;; run: ways 5 100 0 => i32:1631
;; run: ways 5 100 1 => i32:16
;; run: ways 4294967289 3 0 => i32:55
;; run: ways 4294967289 3 1 => i32:-115

(module
  (type $t1 (func (param i32)))
  (memory 1)
  (table 1 funcref)
  (elem (i32.const 0) $thrower)
  (tag $e)
  (data (i32.const 0) "\03\07\01\09\04\06\02\08\05\00")
  ;; throws at 3; called through the table at an index read from memory, which keeps it out of line
  (func $thrower (type $t1) (if (i32.eq (local.get 0) (i32.const 3)) (then (throw $e))))
  (func $fibl (export "fibl") (param $n i32) (result i32)
    (local $acc i32) (local $m i32)
    (if (i32.lt_s (local.get $n) (i32.const 2)) (then (return (local.get $n))))
    (block $out
      (loop $l
        (local.set $m (i32.sub (local.get $n) (i32.const 2)))
        (local.set $acc (i32.add (local.get $acc) (call $fibl (i32.sub (local.get $n) (i32.const 1)))))
        (br_if $out (i32.lt_u (local.get $n) (i32.const 4)))
        (local.set $n (local.get $m))
        (br $l)))
    (i32.add (local.get $acc) (local.get $m)))
  (func $toinst (export "toinst") (param $x i32) (param $c i32) (result i32)
    (local $v i32)
    (local.set $v (i32.add (local.get $x) (i32.const 7)))
    (if (local.get $c) (then (i32.store (i32.const 512) (local.get $x))))
    (i32.store (i32.const 516) (local.get $x)) (i32.mul (local.get $v) (i32.const 3)))
  (func $toret (export "toret") (param $x i32) (param $c i32) (result i32)
    (local $v i32)
    (local.set $v (i32.xor (local.get $x) (i32.const 0x55)))
    (if (local.get $c) (then (i32.store (i32.const 512) (local.get $x))))
    (i32.store (i32.const 516) (local.get $x)) (local.get $v))
  (func $tobranch (export "tobranch") (param $x i32) (param $c i32) (result i32)
    (local $v i32)
    (local.set $v (i32.and (local.get $x) (i32.const 6)))
    (if (local.get $c) (then (i32.store (i32.const 512) (local.get $x))))
    (i32.store (i32.const 516) (local.get $x)) (if (result i32) (local.get $v) (then (i32.store (i32.const 520) (local.get $x)) (i32.const 11)) (else (i32.const 22))))
  (func $toedge (export "toedge") (param $x i32) (param $c i32) (result i32)
    (local $v i32)
    (local.set $v (i32.shl (local.get $x) (i32.const 3)))
    (if (local.get $c) (then (i32.store (i32.const 512) (local.get $x))))
    (if (result i32) (i32.and (local.get $c) (i32.const 2)) (then (i32.store (i32.const 520) (local.get $x)) (local.get $v)) (else (i32.store (i32.const 524) (local.get $c)) (i32.const 9))))
  (func $wide (export "wide") (param $x i64) (param $c i32) (result i64)
    (local $v i64)
    (local.set $v (i64.shr_u (local.get $x) (i64.const 9)))
    (if (local.get $c) (then (i64.store (i32.const 512) (local.get $x))))
    (i64.store (i32.const 528) (local.get $x)) (i64.add (local.get $v) (i64.const 1)))
  (func $subk (export "subk") (param $x i32) (param $c i32) (result i32)
    (local $v i32) (local $u i32) (local $w i32)
    (local.set $v (i32.sub (local.get $x) (i32.const 100)))
    (local.set $u (i32.or (local.get $x) (i32.const 0x1000)))
    (local.set $w (i32.shr_s (local.get $x) (i32.const 2)))
    (if (local.get $c) (then (i32.store (i32.const 512) (local.get $x))))
    (i32.store (i32.const 516) (local.get $x)) (i32.add (i32.add (local.get $v) (local.get $u)) (local.get $w)))
  (func $alone (export "alone") (param $x i32) (param $c i32) (result i32)
    (local $v i32)
    (local.set $v (i32.add (local.get $x) (i32.const 7)))
    (if (local.get $c) (then (i32.store (i32.const 512) (local.get $x))))
    (i32.mul (local.get $v) (i32.const 3)))
  (func $mulk (export "mulk") (param $x i32) (param $c i32) (result i32)
    (local $v i32)
    (local.set $v (i32.mul (local.get $x) (i32.const 1000003)))
    (if (local.get $c) (then (i32.store (i32.const 512) (local.get $x))))
    (i32.store (i32.const 516) (local.get $x)) (i32.add (local.get $v) (i32.const 1)))
  (func $twovals (export "twovals") (param $x i32) (param $c i32) (result i32)
    (local $v i32)
    (local.set $v (i32.add (local.get $x) (local.get $c)))
    (if (local.get $c) (then (i32.store (i32.const 512) (local.get $x))))
    (i32.store (i32.const 516) (local.get $x)) (i32.mul (local.get $v) (i32.const 3)))
  (func $twoblocks (export "twoblocks") (param $x i32) (param $c i32) (result i32)
    (local $v i32)
    (local.set $v (i32.add (local.get $x) (i32.const 7)))
    (if (local.get $c) (then (i32.store (i32.const 512) (local.get $v))))
    (i32.store (i32.const 516) (local.get $x)) (i32.mul (local.get $v) (i32.const 3)))
  (func $same (export "same") (param $x i32) (param $c i32) (result i32)
    (local $v i32)
    (local.set $v (i32.add (local.get $x) (i32.const 7)))
    (i32.store (i32.const 516) (local.get $x)) (i32.mul (local.get $v) (i32.const 3)))
  (func $below2 (export "below2") (param $x i32) (param $c i32) (result i32)
    (local $v i32)
    (local.set $v (i32.add (local.get $x) (i32.const 7)))
    (if (local.get $c) (then (i32.store (i32.const 512) (local.get $x))))
    (i32.store (i32.const 516) (local.get $x)) (if (result i32) (i32.and (local.get $c) (i32.const 2)) (then (i32.store (i32.const 520) (local.get $c)) (i32.mul (local.get $v) (i32.const 3))) (else (i32.store (i32.const 524) (local.get $c)) (i32.mul (local.get $v) (i32.const 5)))))
  (func $xbranch (export "xbranch") (param $x i32) (param $c i32) (result i32)
    (local $v i32)
    (local.set $v (i32.add (local.get $x) (i32.const 7)))
    (if (local.get $c) (then (i32.store (i32.const 512) (local.get $x))))
    (i32.store (i32.const 516) (local.get $v)) (i32.add (if (result i32) (local.get $x) (then (i32.store (i32.const 520) (local.get $c)) (i32.const 3)) (else (i32.store (i32.const 524) (local.get $c)) (i32.const 1))) (i32.load (i32.const 516))))
  (func $ehend (export "ehend") (param $x i32) (param $c i32) (result i32)
    (local $v i32) (local $r i32)
    (local.set $v (i32.add (local.get $x) (i32.const 7)))
    (block $h
      (try_table (catch_all $h)
        (if (local.get $c)
          (then (i32.store (i32.const 512) (local.get $x))
                (call_indirect (type $t1) (local.get $c) (i32.load8_u (i32.const 220)))
                (local.set $r (local.get $v)))
          (else (i32.store (i32.const 516) (local.get $c)) (local.set $r (i32.const 9))))
        (return (i32.mul (local.get $r) (i32.const 3)))))
    (i32.const -7))
  (func $afterloop (export "afterloop") (param $p i32) (param $k i32) (result i32)
    (local $v i32) (local $s i32)
    (block $out
      (loop $l
        (local.set $v (i32.xor (local.get $k) (i32.const 0x33)))
        (br_if $out (i32.eqz (i32.load8_u (local.get $p))))
        (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $p))))
        (local.set $k (i32.add (local.get $k) (local.get $s)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $v) (i32.const 3)) (local.get $k)))
  (func $inner (export "inner") (param $x i32) (param $p i32) (result i32)
    (local $v i32) (local $s i32)
    (local.set $v (i32.add (local.get $x) (i32.const 7)))
    (i32.store (i32.const 512) (local.get $p))
    (block $out
      (loop $l
        (br_if $out (i32.eqz (i32.load8_u (local.get $p))))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3))
                               (i32.add (i32.xor (local.get $v) (i32.load8_u (local.get $p)))
                                        (i32.and (local.get $x) (local.get $p)))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $ways (export "ways") (param $x i32) (param $y i32) (param $c i32) (result i32)
    (local $v i32) (local $q i32) (local $r i32)
    (local.set $v (i32.add (local.get $x) (i32.const 7)))
    (if (local.get $c)
      (then (local.set $q (local.get $x)) (local.set $r (local.get $y)) (i32.store (i32.const 512) (local.get $c)))
      (else (local.set $q (local.get $y)) (local.set $r (local.get $x)) (i32.store (i32.const 516) (local.get $y))))
    (i32.add (i32.mul (local.get $v) (i32.const 3)) (i32.sub (i32.shl (local.get $q) (i32.const 4)) (local.get $r))))
)
