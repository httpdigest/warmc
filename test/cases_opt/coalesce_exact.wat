;; The coalescer's test of whether a block parameter and the value an edge
;; hands it are live at once (regalloc.inc web_interferes): by liveness
;; where their intervals overlap.  count's counter j is read on the error
;; exit, laid out after the loop, so its interval spans j + 1's definition,
;; where it is dead: the two share a register and the loop's back edge is
;; its b.lt, no block of a move and a b.  Only on a back edge: sw's switch
;; cases each end in a move to the state the loop goes on with.  And only
;; for a step short beside the parameter's span: early's j + 1, made at the
;; top of the loop and read through the rest, keeps its move, as it does in
;; early2, where the exit's tail before it reads j makes j's span a little
;; over twice the step's.  Live at
;; once, so kept apart: in after, j read by the exit past j + 1's
;; definition; in from, a counter and its first value, read in the loop,
;; and in reset, a value read in the loop that the back edge hands the
;; counter (and tailreset, where a long tail after the loop reads the
;; counter, so that its span is long beside the value's); in fib, two parameters of one block, each handed the other's
;; value, and in fib2 the same tested at the loop's top, its back edge
;; unconditional and so carrying the moves itself (left so without rotation
;; and unrolling, the flags below); in late, j read later in the block that
;; makes j + 1, and taillate with that tail; in late2, j handed on an exit's edge there.  Answers worked
;; out in Python.
;;
;; flags: -O2 -fno-rotate -fno-unroll
;; shape: count \sb\.lt\s=1
;; shape: sw \smov\sx\d+, x\d+=4
;; shape: early \smov\sx\d+, x\d+=3
;; shape: early2 \smov\sx\d+, x\d+=3
;;
;; run: count 1 0 100 => i32:0
;; run: count 2 0 100 => i32:2
;; run: count 10 0 100 => i32:18
;; run: count 40 0 100 => i32:78
;; run: count 10 0 5 => i32:-1006
;; run: count 10 2 100 => i32:-1001
;; run: count 10 -3 100 => i32:36
;; run: count 50 -10 20 => i32:-1021
;; run: early 1 0 100 => i32:0
;; run: early 2 0 100 => i32:126
;; run: early 10 0 100 => i32:-1634578226
;; run: early 40 0 100 => i32:1982800431
;; run: early 10 0 5 => i32:-1006
;; run: early 10 2 100 => i32:-1001
;; run: early 50 -10 20 => i32:-1021
;; run: early2 1 0 100 => i32:0
;; run: early2 10 0 100 => i32:-1634578226
;; run: early2 10 0 5 => i32:-351299646
;; run: early2 10 2 100 => i32:-1
;; run: early2 50 -10 20 => i32:1360533283
;; run: after 1 100 => i32:1
;; run: after 10 100 => i32:285
;; run: after 10 5 => i32:-1005
;; run: after 40 30 => i32:-1030
;; run: after 3 1 => i32:-1001
;; run: from 0 1 => i32:0
;; run: from 3 10 => i32:105
;; run: from -5 20 => i32:1500
;; run: from 7 8 => i32:0
;; run: reset 5 1 => i32:5
;; run: reset 5 4 => i32:65
;; run: reset -7 10 => i32:-259
;; run: reset 100000 3 => i32:900000
;; run: sw 1 1 => i32:1
;; run: sw 7 10 => i32:-136718750
;; run: sw -123456 100 => i32:994020506
;; run: sw 12345 3 => i32:-1
;; run: sw 1000 1000 => i32:-1869321374
;; run: tailreset 5 0 => i32:0
;; run: tailreset 5 1 => i32:-538915586
;; run: tailreset 5 4 => i32:-1589798463
;; run: tailreset -7 10 => i32:1524803616
;; run: taillate 1 => i32:1
;; run: taillate 2 => i32:1392420139
;; run: taillate 10 => i32:599734492
;; run: taillate 30 => i32:1287027814
;; run: fib 1 => i32:8
;; run: fib 2 => i32:9
;; run: fib 10 => i32:474
;; run: fib 40 => i32:881919226
;; run: fib2 0 => i32:1
;; run: fib2 1 => i32:8
;; run: fib2 10 => i32:474
;; run: fib2 40 => i32:881919226
;; run: late 1 => i32:1
;; run: late 2 => i32:1
;; run: late 10 => i32:14757
;; run: late 30 => i32:1894948383
;; run: late2 1 100 => i32:-1998
;; run: late2 10 100 => i32:-53946
;; run: late2 10 5 => i32:5020
;; run: late2 40 30 => i32:30495

(module
  (memory 1)
  (global $g (mut i32) (i32.const 0))
  (data (i32.const 0) "\01\00\00\00\04\00\00\00\07\00\00\00\0a\00\00\00\0d\00\00\00\10\00\00\00\13\00\00\00\16\00\00\00\19\00\00\00\1c\00\00\00\1f\00\00\00\22\00\00\00\25\00\00\00\28\00\00\00\2b\00\00\00\2e\00\00\00\31\00\00\00\34\00\00\00\37\00\00\00\3a\00\00\00\3d\00\00\00\40\00\00\00\43\00\00\00\46\00\00\00\49\00\00\00\4c\00\00\00\4f\00\00\00\52\00\00\00\55\00\00\00\58\00\00\00\5b\00\00\00\5e\00\00\00\61\00\00\00\64\00\00\00\67\00\00\00\6a\00\00\00\6d\00\00\00\70\00\00\00\73\00\00\00\76\00\00\00\79\00\00\00\7c\00\00\00\7f\00\00\00\82\00\00\00\85\00\00\00\88\00\00\00\8b\00\00\00\8e\00\00\00\91\00\00\00\94\00\00\00\97\00\00\00\9a\00\00\00\9d\00\00\00\a0\00\00\00\a3\00\00\00\a6\00\00\00\a9\00\00\00\ac\00\00\00\af\00\00\00\b2\00\00\00\b5\00\00\00\b8\00\00\00\bb\00\00\00\be\00\00\00")
  ;; s += a[j - lo] / (j + 1) for j from 1 while j < n, j checked against
  ;; [lo, hi] as a Haskell array's index is; the error exit returns -1000 - j
  (func $count (export "count") (param $n i32) (param $lo i32) (param $hi i32) (result i32)
    (local $j i32) (local $s i32) (local $k i32)
    (local.set $j (i32.const 1))
    (block $err
      (block $done
        (br_if $done (i32.ge_s (local.get $j) (local.get $n)))
        (loop $l
          (br_if $err (i32.gt_s (local.get $lo) (local.get $j)))
          (br_if $err (i32.gt_s (local.get $j) (local.get $hi)))
          (local.set $k (i32.sub (local.get $j) (local.get $lo)))
          (local.set $s (i32.add (local.get $s)
            (i32.div_u (i32.load (i32.shl (local.get $k) (i32.const 2)))
                       (i32.add (local.get $j) (i32.const 1)))))
          (local.set $j (i32.add (local.get $j) (i32.const 1)))
          (br_if $l (i32.lt_s (local.get $j) (local.get $n)))))
      (return (local.get $s)))
    (i32.sub (i32.const -1000) (local.get $j)))
  ;; s += j * j while j + 1 <= lim and j < n; past lim, -1000 - j, the j
  ;; before the step
  (func $after (export "after") (param $n i32) (param $lim i32) (result i32)
    (local $j i32) (local $s i32) (local $t i32)
    (local.set $j (i32.const 1))
    (block $err
      (loop $l
        (local.set $s (i32.add (local.get $s) (i32.mul (local.get $j) (local.get $j))))
        (local.set $t (i32.add (local.get $j) (i32.const 1)))
        (br_if $err (i32.gt_s (local.get $t) (local.get $lim)))
        (local.set $j (local.get $t))
        (br_if $l (i32.lt_s (local.get $j) (local.get $n))))
      (return (local.get $s)))
    (i32.sub (i32.const -1000) (local.get $j)))
  ;; x stepped n times, at least once, by one of four steps x & 3 picks,
  ;; each storing; -1 on meeting 12345, x then kept in a global
  (func $sw (export "sw") (param $x i32) (param $n i32) (result i32)
    (block $err
      (loop $l
        (br_if $err (i32.eq (local.get $x) (i32.const 12345)))
        (block $join
          (block $c3
            (block $c2
              (block $c1
                (block $c0
                  (br_table $c0 $c1 $c2 $c3 (i32.and (local.get $x) (i32.const 3))))
                (local.set $x (i32.add (local.get $x) (i32.const 0x9e37)))
                (i32.store (i32.const 16) (local.get $n))
                (br $join))
              (local.set $x (i32.xor (local.get $x) (i32.shr_s (local.get $x) (i32.const 3))))
              (i32.store (i32.const 20) (local.get $n))
              (br $join))
            (local.set $x (i32.mul (local.get $x) (i32.const 5)))
            (i32.store (i32.const 24) (local.get $n))
            (br $join))
          (local.set $x (i32.sub (local.get $x) (i32.const 77)))
          (i32.store (i32.const 28) (local.get $n)))
        (br_if $l (local.tee $n (i32.sub (local.get $n) (i32.const 1)))))
      (return (local.get $x)))
    (global.set $g (local.get $x))
    (i32.const -1))
  ;; count's loop with j + 1 made at its top and read through the rest
  (func $early (export "early") (param $n i32) (param $lo i32) (param $hi i32) (result i32)
    (local $j i32) (local $s i32) (local $k i32) (local $t i32)
    (local.set $j (i32.const 1))
    (block $err
      (block $done
        (br_if $done (i32.ge_s (local.get $j) (local.get $n)))
        (loop $l
          (br_if $err (i32.gt_s (local.get $lo) (local.get $j)))
          (br_if $err (i32.gt_s (local.get $j) (local.get $hi)))
          (local.set $k (i32.sub (local.get $j) (local.get $lo)))
          (local.set $t (i32.add (local.get $j) (i32.const 1)))
          (local.set $s (i32.add (local.get $s)
            (i32.div_u (i32.load (i32.shl (local.get $k) (i32.const 2))) (local.get $t))))
          (local.set $s (i32.xor (local.get $s) (i32.mul (local.get $t) (i32.const 5))))
          (local.set $s (i32.add (local.get $s) (i32.shr_u (local.get $s) (local.get $t))))
          (local.set $s (i32.xor (local.get $s) (i32.shl (local.get $t) (i32.const 3))))
          (local.set $s (i32.add (local.get $s) (i32.rotl (local.get $s) (local.get $t))))
          (local.set $s (i32.sub (local.get $s) (i32.mul (local.get $t) (local.get $t))))
          (local.set $j (local.get $t))
          (br_if $l (i32.lt_s (local.get $j) (local.get $n)))))
      (return (local.get $s)))
    (i32.sub (i32.const -1000) (local.get $j)))
  ;; early with three steps of s on the exit before it reads j
  (func $early2 (export "early2") (param $n i32) (param $lo i32) (param $hi i32) (result i32)
    (local $j i32) (local $s i32) (local $k i32) (local $t i32)
    (local.set $j (i32.const 1))
    (block $err
      (block $done
        (br_if $done (i32.ge_s (local.get $j) (local.get $n)))
        (loop $l
          (br_if $err (i32.gt_s (local.get $lo) (local.get $j)))
          (br_if $err (i32.gt_s (local.get $j) (local.get $hi)))
          (local.set $k (i32.sub (local.get $j) (local.get $lo)))
          (local.set $t (i32.add (local.get $j) (i32.const 1)))
          (local.set $s (i32.add (local.get $s)
            (i32.div_u (i32.load (i32.shl (local.get $k) (i32.const 2))) (local.get $t))))
          (local.set $s (i32.xor (local.get $s) (i32.mul (local.get $t) (i32.const 5))))
          (local.set $s (i32.add (local.get $s) (i32.shr_u (local.get $s) (local.get $t))))
          (local.set $s (i32.xor (local.get $s) (i32.shl (local.get $t) (i32.const 3))))
          (local.set $s (i32.add (local.get $s) (i32.rotl (local.get $s) (local.get $t))))
          (local.set $s (i32.sub (local.get $s) (i32.mul (local.get $t) (local.get $t))))
          (local.set $j (local.get $t))
          (br_if $l (i32.lt_s (local.get $j) (local.get $n)))))
      (return (local.get $s)))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 3)) (i32.shr_u (local.get $s) (i32.const 1))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 5)) (i32.shr_u (local.get $s) (i32.const 2))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 7)) (i32.shr_u (local.get $s) (i32.const 3))))
    (i32.sub (local.get $s) (local.get $j)))
  ;; s += 5 (j - lo) for j from lo while j < n: lo, j's first value, is
  ;; read in the loop
  (func $from (export "from") (param $lo i32) (param $n i32) (result i32)
    (local $j i32) (local $s i32)
    (local.set $j (local.get $lo))
    (loop $l
      (local.set $s (i32.add (local.get $s)
        (i32.mul (i32.sub (local.get $j) (local.get $lo)) (i32.const 5))))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br_if $l (i32.lt_s (local.get $j) (local.get $n))))
    (local.get $s))
  ;; s += 3j + lo, j 0 and then lo, n times, at least once: lo, handed j on
  ;; the back edge, is read in the loop
  (func $reset (export "reset") (param $lo i32) (param $n i32) (result i32)
    (local $j i32) (local $s i32) (local $k i32)
    (loop $l
      (local.set $s (i32.add (local.get $s)
        (i32.add (i32.mul (local.get $j) (i32.const 3)) (local.get $lo))))
      (local.set $j (local.get $lo))
      (local.set $k (i32.add (local.get $k) (i32.const 1)))
      (br_if $l (i32.lt_s (local.get $k) (local.get $n))))
    (local.get $s))
  ;; reset tested at the loop's top, n times, and a long tail after the
  ;; loop that reads j at its end: j's span is long, lo's short beside it
  (func $tailreset (export "tailreset") (param $lo i32) (param $n i32) (result i32)
    (local $j i32) (local $s i32) (local $k i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $k) (local.get $n)))
        (local.set $s (i32.add (local.get $s)
          (i32.add (i32.mul (local.get $j) (i32.const 3)) (local.get $lo))))
        (local.set $j (local.get $lo))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br $l)))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 3)) (i32.shr_u (local.get $s) (i32.const 1))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 5)) (i32.shr_u (local.get $s) (i32.const 2))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 7)) (i32.shr_u (local.get $s) (i32.const 3))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 9)) (i32.shr_u (local.get $s) (i32.const 4))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 11)) (i32.shr_u (local.get $s) (i32.const 5))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 13)) (i32.shr_u (local.get $s) (i32.const 6))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 15)) (i32.shr_u (local.get $s) (i32.const 7))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 17)) (i32.shr_u (local.get $s) (i32.const 1))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 19)) (i32.shr_u (local.get $s) (i32.const 2))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 21)) (i32.shr_u (local.get $s) (i32.const 3))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 23)) (i32.shr_u (local.get $s) (i32.const 4))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 25)) (i32.shr_u (local.get $s) (i32.const 5))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 27)) (i32.shr_u (local.get $s) (i32.const 6))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 29)) (i32.shr_u (local.get $s) (i32.const 7))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 31)) (i32.shr_u (local.get $s) (i32.const 1))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 33)) (i32.shr_u (local.get $s) (i32.const 2))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 35)) (i32.shr_u (local.get $s) (i32.const 3))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 37)) (i32.shr_u (local.get $s) (i32.const 4))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 39)) (i32.shr_u (local.get $s) (i32.const 5))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 41)) (i32.shr_u (local.get $s) (i32.const 6))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 43)) (i32.shr_u (local.get $s) (i32.const 7))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 45)) (i32.shr_u (local.get $s) (i32.const 1))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 47)) (i32.shr_u (local.get $s) (i32.const 2))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 49)) (i32.shr_u (local.get $s) (i32.const 3))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 51)) (i32.shr_u (local.get $s) (i32.const 4))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 53)) (i32.shr_u (local.get $s) (i32.const 5))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 55)) (i32.shr_u (local.get $s) (i32.const 6))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 57)) (i32.shr_u (local.get $s) (i32.const 7))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 59)) (i32.shr_u (local.get $s) (i32.const 1))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 61)) (i32.shr_u (local.get $s) (i32.const 2))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 63)) (i32.shr_u (local.get $s) (i32.const 3))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 65)) (i32.shr_u (local.get $s) (i32.const 4))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 67)) (i32.shr_u (local.get $s) (i32.const 5))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 69)) (i32.shr_u (local.get $s) (i32.const 6))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 71)) (i32.shr_u (local.get $s) (i32.const 7))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 73)) (i32.shr_u (local.get $s) (i32.const 1))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 75)) (i32.shr_u (local.get $s) (i32.const 2))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 77)) (i32.shr_u (local.get $s) (i32.const 3))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 79)) (i32.shr_u (local.get $s) (i32.const 4))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 81)) (i32.shr_u (local.get $s) (i32.const 5))))
    (i32.add (local.get $s) (local.get $j)))
  ;; late tested at the top, with that tail
  (func $taillate (export "taillate") (param $n i32) (result i32)
    (local $j i32) (local $s i32) (local $t i32)
    (local.set $j (i32.const 1))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $j) (local.get $n)))
        (local.set $t (i32.add (local.get $j) (i32.const 1)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $j)))
        (local.set $j (local.get $t))
        (br $l)))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 3)) (i32.shr_u (local.get $s) (i32.const 1))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 5)) (i32.shr_u (local.get $s) (i32.const 2))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 7)) (i32.shr_u (local.get $s) (i32.const 3))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 9)) (i32.shr_u (local.get $s) (i32.const 4))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 11)) (i32.shr_u (local.get $s) (i32.const 5))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 13)) (i32.shr_u (local.get $s) (i32.const 6))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 15)) (i32.shr_u (local.get $s) (i32.const 7))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 17)) (i32.shr_u (local.get $s) (i32.const 1))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 19)) (i32.shr_u (local.get $s) (i32.const 2))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 21)) (i32.shr_u (local.get $s) (i32.const 3))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 23)) (i32.shr_u (local.get $s) (i32.const 4))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 25)) (i32.shr_u (local.get $s) (i32.const 5))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 27)) (i32.shr_u (local.get $s) (i32.const 6))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 29)) (i32.shr_u (local.get $s) (i32.const 7))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 31)) (i32.shr_u (local.get $s) (i32.const 1))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 33)) (i32.shr_u (local.get $s) (i32.const 2))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 35)) (i32.shr_u (local.get $s) (i32.const 3))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 37)) (i32.shr_u (local.get $s) (i32.const 4))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 39)) (i32.shr_u (local.get $s) (i32.const 5))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 41)) (i32.shr_u (local.get $s) (i32.const 6))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 43)) (i32.shr_u (local.get $s) (i32.const 7))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 45)) (i32.shr_u (local.get $s) (i32.const 1))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 47)) (i32.shr_u (local.get $s) (i32.const 2))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 49)) (i32.shr_u (local.get $s) (i32.const 3))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 51)) (i32.shr_u (local.get $s) (i32.const 4))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 53)) (i32.shr_u (local.get $s) (i32.const 5))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 55)) (i32.shr_u (local.get $s) (i32.const 6))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 57)) (i32.shr_u (local.get $s) (i32.const 7))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 59)) (i32.shr_u (local.get $s) (i32.const 1))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 61)) (i32.shr_u (local.get $s) (i32.const 2))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 63)) (i32.shr_u (local.get $s) (i32.const 3))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 65)) (i32.shr_u (local.get $s) (i32.const 4))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 67)) (i32.shr_u (local.get $s) (i32.const 5))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 69)) (i32.shr_u (local.get $s) (i32.const 6))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 71)) (i32.shr_u (local.get $s) (i32.const 7))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 73)) (i32.shr_u (local.get $s) (i32.const 1))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 75)) (i32.shr_u (local.get $s) (i32.const 2))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 77)) (i32.shr_u (local.get $s) (i32.const 3))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 79)) (i32.shr_u (local.get $s) (i32.const 4))))
    (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 81)) (i32.shr_u (local.get $s) (i32.const 5))))
    (i32.add (local.get $s) (local.get $j)))
  ;; (a, b) = (b, a + b) n times, at least once: 7a + b
  (func $fib (export "fib") (param $n i32) (result i32)
    (local $a i32) (local $b i32) (local $t i32)
    (local.set $b (i32.const 1))
    (loop $l
      (local.set $t (i32.add (local.get $a) (local.get $b)))
      (local.set $a (local.get $b))
      (local.set $b (local.get $t))
      (br_if $l (i32.gt_s (local.tee $n (i32.sub (local.get $n) (i32.const 1))) (i32.const 0))))
    (i32.add (i32.mul (local.get $a) (i32.const 7)) (local.get $b)))
  ;; fib with its test first: n <= 0 is 7 * 0 + 1
  (func $fib2 (export "fib2") (param $n i32) (result i32)
    (local $a i32) (local $b i32) (local $t i32)
    (local.set $b (i32.const 1))
    (block $done
      (loop $l
        (br_if $done (i32.le_s (local.get $n) (i32.const 0)))
        (local.set $t (i32.add (local.get $a) (local.get $b)))
        (local.set $a (local.get $b))
        (local.set $b (local.get $t))
        (local.set $n (i32.sub (local.get $n) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $a) (i32.const 7)) (local.get $b)))
  ;; s = 3s + j for j from 1 while j < n, j read after j + 1 is made
  (func $late (export "late") (param $n i32) (result i32)
    (local $j i32) (local $s i32) (local $t i32)
    (local.set $j (i32.const 1))
    (loop $l
      (local.set $t (i32.add (local.get $j) (i32.const 1)))
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $j)))
      (local.set $j (local.get $t))
      (br_if $l (i32.lt_s (local.get $j) (local.get $n))))
    (local.get $s))
  ;; s += j + 1 for j from 1 while j < n, then -1000 s + s; past lim,
  ;; 1000 j + s, j handed out on the exit's edge to the block both ways end
  (func $late2 (export "late2") (param $n i32) (param $lim i32) (result i32)
    (local $j i32) (local $s i32) (local $t i32)
    (local.set $j (i32.const 1))
    (i32.add
      (i32.mul
        (block $out (result i32)
          (loop $l
            (local.set $t (i32.add (local.get $j) (i32.const 1)))
            (local.set $s (i32.add (local.get $s) (local.get $t)))
            (drop (br_if $out (local.get $j) (i32.gt_s (local.get $t) (local.get $lim))))
            (local.set $j (local.get $t))
            (br_if $l (i32.lt_s (local.get $j) (local.get $n))))
          (i32.sub (i32.const 0) (local.get $s)))
        (i32.const 1000))
      (local.get $s)))
)
