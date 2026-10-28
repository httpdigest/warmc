;; Recursion through an accumulator (opt.c pass_acc_tail, -O3 only): a
;; function whose result is, one way, its own call's result op something
;; else -- an add, multiply, xor, and or or, each with its identity -- the
;; call's result read by op alone and nothing after the call but pure work,
;; loops instead, as LLVM's TailCallElim does: naive fib makes one call a
;; level, not two, sum and fac none.  A path may return through a block
;; that returns its parameter, or return itself (ret).  A body of some
;; dozens of instructions is taken (medium), one past 64 is not (big):
;; TeaVM unrolls its fib into itself, where one call fewer of many saved
;; nothing.  Left be: a difference (subr); after the call, a store
;; (stored), a divide that may trap (trapping), a global set (gset) or read
;; (gget); the call in a block before the op's, a store between (split); a
;; sum the block it reaches doubles (dbl) or counts in memory before
;; returning it (counted); the call's result read by the op's other operand
;; too (twice); a call in a try that catches what a deeper level throws
;; (eh); a call of another function (mutual_a and mutual_b); a second op
;; where the first was another (mixed: the sum's call is taken, the
;; product's stays); and a function with a tail call of another, whose
;; result an accumulator could not meet (viatail).  None at -O2, where a
;; recursion must still run out of stack.  Answers worked out by
;; test/gen_acctail.py.
;;
;; stats -O2: acc_tail=0
;; stats -O3: acc_tail=10
;;
;; run: fib 0 => i32:0
;; run: fib 1 => i32:1
;; run: fib 2 => i32:1
;; run: fib 10 => i32:55
;; run: fib 20 => i32:6765
;; run: sum 0 => i32:0
;; run: sum 1 => i32:1
;; run: sum 10 => i32:55
;; run: sum 100 => i32:5050
;; run: sum 3000 => i32:4501500
;; run: fac 0 => i64:1
;; run: fac 1 => i64:1
;; run: fac 5 => i64:120
;; run: fac 20 => i64:2432902008176640000
;; run: fac 25 => i64:7034535277573963776
;; run: xsum 0 => i32:85
;; run: xsum 1 => i32:40546
;; run: xsum 9 => i32:495610
;; run: xsum 100 => i32:4027061
;; run: band 0 => i32:-1
;; run: band 1 => i32:241
;; run: band 7 => i32:240
;; run: band 40 => i32:240
;; run: band64 0 => i64:-1
;; run: band64 1 => i64:1099528339711
;; run: band64 3 => i64:16711935
;; run: band64 9 => i64:16711935
;; run: bor 0 => i32:0
;; run: bor 1 => i32:2
;; run: bor 5 => i32:62
;; run: bor 31 => i32:-2
;; run: bor 40 => i32:-1
;; run: ret 0 => i32:7
;; run: ret 1 => i32:10
;; run: ret 2 => i32:13
;; run: ret 9 => i32:82
;; run: ret 100 => i32:7657
;; run: medium 0 => i32:0
;; run: medium 1 => i32:137480
;; run: medium 30 => i32:64922449
;; run: big 0 => i32:0
;; run: big 1 => i32:513596351
;; run: big 30 => i32:2110273609
;; run: subr 0 => i32:0
;; run: subr 1 => i32:1
;; run: subr 2 => i32:1
;; run: subr 9 => i32:5
;; run: stored 0 => i32:1
;; run: stored 1 => i32:2
;; run: stored 6 => i32:22
;; run: trapping 0 1 => i32:0
;; run: trapping 5 2 => i32:6
;; run: trapping 9 3 => i32:12
;; run: gset 0 => i32:0
;; run: gset 1 => i32:1
;; run: gset 5 => i32:15
;; run: gget 0 => i32:0
;; run: gget 1 => i32:1
;; run: gget 5 => i32:275
;; run: split 0 => i32:0
;; run: split 1 => i32:1
;; run: split 5 => i32:15
;; run: dbl 0 => i32:2
;; run: dbl 1 => i32:6
;; run: dbl 6 => i32:368
;; run: counted 0 => i32:1005
;; run: counted 1 => i32:2006
;; run: counted 6 => i32:7026
;; run: twice 0 => i32:1
;; run: twice 1 => i32:1
;; run: twice 8 => i32:1
;; run: twice 30 => i32:1
;; run: eh 1 => i32:-1
;; run: eh 2 => i32:1
;; run: eh 5 => i32:13
;; run: mutual_a 0 => i32:0
;; run: mutual_a 1 => i32:2
;; run: mutual_a 2 => i32:4
;; run: mutual_a 11 => i32:97
;; run: mutual_b 0 => i32:1
;; run: mutual_b 5 => i32:24
;; run: mixed 0 => i32:1
;; run: mixed 1 => i32:2
;; run: mixed 2 => i32:4
;; run: mixed 7 => i32:205
;; run: viatail 0 => i32:40
;; run: viatail 1 => i32:41
;; run: viatail 6 => i32:61

(module
  (memory 1)
  (global $g (mut i32) (i32.const 0))
  (tag $e)
  (func $other (param i32) (result i32) (i32.add (local.get 0) (i32.const 40)))
  (func $count1 (param i32) (result i32)
    (local $r i32)
    (local.set $r
      (if (result i32) (i32.eqz (local.get 0))
        (then (i32.const 5))
        (else (i32.add (local.get 0) (call $count1 (i32.sub (local.get 0) (i32.const 1)))))))
    (i32.store (i32.const 128) (i32.add (i32.load (i32.const 128)) (i32.const 1)))
    (local.get $r))
  (func $fib (export "fib") (param i32) (result i32)
    (if (result i32) (i32.lt_u (local.get 0) (i32.const 2))
      (then (local.get 0))
      (else (i32.add (call $fib (i32.sub (local.get 0) (i32.const 1)))
                     (call $fib (i32.sub (local.get 0) (i32.const 2)))))))
  (func $sum (export "sum") (param i32) (result i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 0))
      (else (i32.add (local.get 0) (call $sum (i32.sub (local.get 0) (i32.const 1)))))))
  (func $fac (export "fac") (param i64) (result i64)
    (if (result i64) (i64.le_u (local.get 0) (i64.const 1))
      (then (i64.const 1))
      (else (i64.mul (local.get 0) (call $fac (i64.sub (local.get 0) (i64.const 1)))))))
  (func $xsum (export "xsum") (param i32) (result i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 0x55))
      (else (i32.xor (call $xsum (i32.sub (local.get 0) (i32.const 1)))
                     (i32.mul (local.get 0) (i32.const 0x9e37))))))
  (func $band (export "band") (param i32) (result i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const -1))
      (else (i32.and (i32.or (local.get 0) (i32.const 0xf0))
                     (call $band (i32.sub (local.get 0) (i32.const 1)))))))
  (func $band64 (export "band64") (param i64) (result i64)
    (if (result i64) (i64.eqz (local.get 0))
      (then (i64.const -1))
      (else (i64.and (i64.or (i64.shl (local.get 0) (i64.const 40)) (i64.const 0xff00ff))
                     (call $band64 (i64.sub (local.get 0) (i64.const 1)))))))
  (func $bor (export "bor") (param i32) (result i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 0))
      (else (i32.or (i32.shl (i32.const 1) (local.get 0))
                    (call $bor (i32.sub (local.get 0) (i32.const 1)))))))
  (func $ret (export "ret") (param i32) (result i32)
    (if (i32.le_s (local.get 0) (i32.const 0)) (then (return (i32.const 7))))
    (return (i32.add (i32.mul (local.get 0) (i32.const 3))
                     (call $ret (i32.sub (local.get 0) (i32.const 2))))))
  (func $medium (export "medium") (param i32) (result i32)
    (local $x i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 0))
      (else
        (local.set $x (local.get 0))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 3)) (i32.shr_u (local.get $x) (i32.const 1))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 5)) (i32.shr_u (local.get $x) (i32.const 2))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 7)) (i32.shr_u (local.get $x) (i32.const 3))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 9)) (i32.shr_u (local.get $x) (i32.const 4))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 11)) (i32.shr_u (local.get $x) (i32.const 5))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 13)) (i32.shr_u (local.get $x) (i32.const 1))))
        (i32.add (local.get $x) (call $medium (i32.sub (local.get 0) (i32.const 1)))))))
  (func $big (export "big") (param i32) (result i32)
    (local $x i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 0))
      (else
        (local.set $x (local.get 0))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 3)) (i32.shr_u (local.get $x) (i32.const 1))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 5)) (i32.shr_u (local.get $x) (i32.const 2))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 7)) (i32.shr_u (local.get $x) (i32.const 3))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 9)) (i32.shr_u (local.get $x) (i32.const 4))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 11)) (i32.shr_u (local.get $x) (i32.const 5))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 13)) (i32.shr_u (local.get $x) (i32.const 1))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 15)) (i32.shr_u (local.get $x) (i32.const 2))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 17)) (i32.shr_u (local.get $x) (i32.const 3))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 19)) (i32.shr_u (local.get $x) (i32.const 4))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 21)) (i32.shr_u (local.get $x) (i32.const 5))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 23)) (i32.shr_u (local.get $x) (i32.const 1))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 25)) (i32.shr_u (local.get $x) (i32.const 2))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 27)) (i32.shr_u (local.get $x) (i32.const 3))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 29)) (i32.shr_u (local.get $x) (i32.const 4))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 31)) (i32.shr_u (local.get $x) (i32.const 5))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 33)) (i32.shr_u (local.get $x) (i32.const 1))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 35)) (i32.shr_u (local.get $x) (i32.const 2))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 37)) (i32.shr_u (local.get $x) (i32.const 3))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 39)) (i32.shr_u (local.get $x) (i32.const 4))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 41)) (i32.shr_u (local.get $x) (i32.const 5))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 43)) (i32.shr_u (local.get $x) (i32.const 1))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 45)) (i32.shr_u (local.get $x) (i32.const 2))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 47)) (i32.shr_u (local.get $x) (i32.const 3))))
        (local.set $x (i32.xor (i32.mul (local.get $x) (i32.const 49)) (i32.shr_u (local.get $x) (i32.const 4))))
        (i32.add (local.get $x) (call $big (i32.sub (local.get 0) (i32.const 1)))))))
  (func $subr (export "subr") (param i32) (result i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 0))
      (else (i32.sub (local.get 0) (call $subr (i32.sub (local.get 0) (i32.const 1)))))))
  (func $stored (export "stored") (param i32) (result i32)
    (local $r i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 1))
      (else
        (local.set $r (call $stored (i32.sub (local.get 0) (i32.const 1))))
        (i32.store (i32.const 64) (local.get 0))
        (i32.add (local.get $r) (local.get 0)))))
  (func $trapping (export "trapping") (param i32) (param i32) (result i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 0))
      (else (i32.add (call $trapping (i32.sub (local.get 0) (i32.const 1)) (local.get 1))
                     (i32.div_u (local.get 0) (local.get 1))))))
  (func $gset (export "gset") (param i32) (result i32)
    (local $r i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (global.get $g))
      (else
        (local.set $r (call $gset (i32.sub (local.get 0) (i32.const 1))))
        (global.set $g (i32.add (i32.mul (global.get $g) (i32.const 3)) (local.get 0)))
        (i32.add (local.get $r) (local.get 0)))))
  (func $gget (export "gget") (param i32) (result i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 0))
      (else
        (global.set $g (i32.add (global.get $g) (i32.mul (local.get 0) (local.get 0))))
        (i32.add (call $gget (i32.sub (local.get 0) (i32.const 1))) (global.get $g)))))
  (func $split (export "split") (param i32) (result i32)
    (local $r i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.load (i32.const 64)))
      (else
        (local.set $r (call $split (i32.sub (local.get 0) (i32.const 1))))
        (if (i32.and (local.get 0) (i32.const 1))
          (then (i32.store (i32.const 64) (i32.add (i32.mul (i32.load (i32.const 64)) (i32.const 3))
                                                   (local.get 0)))))
        (i32.add (local.get $r) (local.get 0)))))
  (func $dbl (export "dbl") (param i32) (result i32)
    (i32.mul
      (if (result i32) (i32.eqz (local.get 0))
        (then (i32.const 1))
        (else (i32.add (local.get 0) (call $dbl (i32.sub (local.get 0) (i32.const 1))))))
      (i32.const 2)))
  (func $counted (export "counted") (param i32) (result i32)
    (i32.add (call $count1 (local.get 0))
             (i32.mul (i32.load (i32.const 128)) (i32.const 1000))))
  (func $twice (export "twice") (param i32) (result i32)
    (local $r i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 1))
      (else
        (local.set $r (call $twice (i32.sub (local.get 0) (i32.const 1))))
        (i32.add (local.get $r) (i32.shr_u (local.get $r) (i32.const 1))))))
  (func $eh (export "eh") (param i32) (result i32)
    (if (i32.eqz (local.get 0)) (then (throw $e)))
    (block $h
      (try_table (catch_all $h)
        (return (i32.add (local.get 0) (call $eh (i32.sub (local.get 0) (i32.const 1)))))))
    (i32.const -1))
  (func $mutual_a (export "mutual_a") (param i32) (result i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 0))
      (else (i32.add (local.get 0) (call $mutual_b (i32.sub (local.get 0) (i32.const 1)))))))
  (func $mutual_b (export "mutual_b") (param i32) (result i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 1))
      (else (i32.add (i32.mul (local.get 0) (i32.const 2))
                     (call $mutual_a (i32.sub (local.get 0) (i32.const 1)))))))
  (func $mixed (export "mixed") (param i32) (result i32)
    (if (result i32) (i32.eqz (local.get 0))
      (then (i32.const 1))
      (else
        (if (result i32) (i32.and (local.get 0) (i32.const 1))
          (then (return (i32.add (local.get 0) (call $mixed (i32.sub (local.get 0) (i32.const 1))))))
          (else (return (i32.mul (local.get 0) (call $mixed (i32.sub (local.get 0) (i32.const 1))))))))))
  (func $viatail (export "viatail") (param i32) (result i32)
    (if (i32.eqz (local.get 0)) (then (return_call $other (local.get 0))))
    (i32.add (local.get 0) (call $viatail (i32.sub (local.get 0) (i32.const 1)))))
)
