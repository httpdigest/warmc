;; The value a loop hands its parameter x on a back edge, x op c1 op c2 ...
;; with op an add, multiply, and, or or xor and each c a constant, is x op
;; c, the constants folded into one, also where each link is read elsewhere
;; (opt.c carried_chain): the next iteration waits on one instruction, not
;; on each link in turn -- gobench/bigstruct's loop unrolled four
;; times.  The links stay for their other readers.  Not where nothing else
;; reads x, which would then be live to the chain's end; not for a value no
;; back edge hands x, nor one handed another parameter, nor a chain from
;; anything but a parameter, nor through a link that adds a variable, nor
;; one of more than 16 links below the value, nor outside a loop.  Constants
;; wrap: an i32's past 2^32, an i64 product's past 2^64.  The loops whose
;; shapes are counted stop where s is 1, which keeps them from being
;; unrolled: the merged step makes unroll's a counted loop.  Answers worked
;; out in Python.
;;
;; shape: unroll \sadd\s+w\d+, w\d+, #0x4$=1
;; shape: unroll \sadd\s+w\d+, w\d+, #0x1$=3
;; shape: baseonly \sadd\s+w\d+, w\d+, #0x4$=0
;; shape: baseonly \sadd\s+w\d+, w\d+, #0x1$=4
;; shape: notback \sadd\s+w\d+, w\d+, #0x[23]$=0
;; shape: straight \sadd\s+w\d+, w\d+, #0x[23]$=0
;; shape: xorchain \seor\s+x\d+, x\d+, #0xf00$=1
;; shape: deep \sadd\s+w\d+, w\d+, #0x11$=1
;; shape: deeper \sadd\s+w\d+, w\d+, #0x12$=0
;; shape: twoedge \sadd\s+w\d+, w\d+, #0x4$=1
;; shape: notparam \sadd\s+w\d+, w\d+, #0x2$=0
;; shape: swap \sadd\s+w\d+, w\d+, #0x3$=0
;;
;; run: unroll 0 => i32:257
;; run: baseonly 0 => i32:257
;; run: notback 0 => i32:257
;; run: addwrap 0 => i32:-2147483647
;; run: deep 0 => i32:16
;; run: deeper 0 => i32:1
;; run: twoedge 0 => i32:257
;; run: notparam 0 => i32:1
;; run: varlink 2 0 => i32:2
;; run: swap 0 => i32:12
;; run: unroll 1 => i32:257
;; run: baseonly 1 => i32:257
;; run: notback 1 => i32:257
;; run: addwrap 1 => i32:-2147483647
;; run: deep 1 => i32:16
;; run: deeper 1 => i32:1
;; run: twoedge 1 => i32:257
;; run: notparam 1 => i32:1
;; run: varlink 2 1 => i32:2
;; run: swap 1 => i32:12
;; run: unroll 4 => i32:257
;; run: baseonly 4 => i32:257
;; run: notback 4 => i32:8042884
;; run: addwrap 4 => i32:266
;; run: deep 4 => i32:16
;; run: deeper 4 => i32:1
;; run: twoedge 4 => i32:8352
;; run: notparam 4 => i32:36
;; run: varlink 2 4 => i32:68
;; run: swap 4 => i32:12
;; run: unroll 5 => i32:8764
;; run: baseonly 5 => i32:8760
;; run: notback 5 => i32:249330201
;; run: addwrap 5 => i32:-2147481781
;; run: deep 5 => i32:16
;; run: deeper 5 => i32:1
;; run: twoedge 5 => i32:8352
;; run: notparam 5 => i32:36
;; run: varlink 2 5 => i32:68
;; run: swap 5 => i32:12
;; run: unroll 37 => i32:-160728564
;; run: baseonly 37 => i32:524830232
;; run: notback 37 => i32:351547193
;; run: addwrap 37 => i32:717173147
;; run: deep 37 => i32:15953
;; run: deeper 37 => i32:993
;; run: twoedge 37 => i32:1853891848
;; run: notparam 37 => i32:35208
;; run: varlink 2 37 => i32:-1373659862
;; run: swap 37 => i32:454295413
;; run: unroll 100 => i32:-382734287
;; run: baseonly 100 => i32:1648994977
;; run: notback 100 => i32:1678549316
;; run: addwrap 100 => i32:-363083270
;; run: deep 100 => i32:475307523
;; run: deeper 100 => i32:29583456
;; run: twoedge 100 => i32:-533579772
;; run: notparam 100 => i32:1091609
;; run: varlink 2 100 => i32:1840071812
;; run: swap 100 => i32:2029575148
;; run: straight 0 => i32:1
;; run: straight 7 => i32:69
;; run: straight -2 => i32:-1
;; run: straight 2147483647 => i32:2147483645
;; run: xorchain 0 1 => i64:255
;; run: mulwrap 0 1 => i64:0
;; run: xorchain 5 2 => i64:41241625960
;; run: mulwrap 5 2 => i64:42949672960
;; run: xorchain -1 9 => i64:9029023975424099727
;; run: mulwrap -1 9 => i64:38654705664
;; run: xorchain 81985529216486895 40 => i64:5761758257860385600
;; run: mulwrap 81985529216486895 40 => i64:-2243424661817786368

(module
  (func $unroll (export "unroll") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $a i32) (local $b i32) (local $c i32)
    (loop $l
      (local.set $a (i32.add (local.get $i) (i32.const 1)))
      (local.set $b (i32.add (local.get $a) (i32.const 1)))
      (local.set $c (i32.add (local.get $b) (i32.const 1)))
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
        (i32.xor (i32.xor (local.get $i) (i32.shl (local.get $a) (i32.const 3)))
          (i32.xor (i32.rotl (local.get $b) (i32.const 7))
                   (i32.mul (local.get $c) (local.get $c))))))
      (local.set $i (i32.add (local.get $c) (i32.const 1)))
      (br_if $l (i32.and (i32.lt_u (local.get $i) (local.get $n))
                         (i32.ne (local.get $s) (i32.const 1)))))
    (local.get $s))

  ;; the same, but x is read by the chain alone
  (func $baseonly (export "baseonly") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $a i32) (local $b i32) (local $c i32)
    (loop $l
      (local.set $a (i32.add (local.get $i) (i32.const 1)))
      (local.set $b (i32.add (local.get $a) (i32.const 1)))
      (local.set $c (i32.add (local.get $b) (i32.const 1)))
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
        (i32.xor (i32.shl (local.get $a) (i32.const 3))
          (i32.xor (i32.rotl (local.get $b) (i32.const 7))
                   (i32.mul (local.get $c) (local.get $c))))))
      (local.set $i (i32.add (local.get $c) (i32.const 1)))
      (br_if $l (i32.and (i32.lt_u (local.get $i) (local.get $n))
                         (i32.ne (local.get $s) (i32.const 1)))))
    (local.get $s))

  ;; the chain's end is no back edge's: the loop hands x x + 1
  (func $notback (export "notback") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $a i32) (local $b i32) (local $c i32)
    (loop $l
      (local.set $a (i32.add (local.get $i) (i32.const 1)))
      (local.set $b (i32.add (local.get $a) (i32.const 1)))
      (local.set $c (i32.add (local.get $b) (i32.const 1)))
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
        (i32.xor (i32.xor (local.get $i) (i32.shl (local.get $a) (i32.const 3)))
          (i32.xor (i32.rotl (local.get $b) (i32.const 7))
                   (i32.mul (local.get $c) (local.get $c))))))
      (local.set $i (local.get $a))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))

  ;; no loop
  (func $straight (export "straight") (param $x i32) (result i32)
    (local $a i32) (local $b i32) (local $c i32)
    (local.set $a (i32.add (local.get $x) (i32.const 1)))
    (local.set $b (i32.add (local.get $a) (i32.const 1)))
    (local.set $c (i32.add (local.get $b) (i32.const 1)))
    (i32.xor (i32.xor (i32.mul (local.get $a) (local.get $b)) (local.get $c)) (local.get $x)))

  ;; x ^ 0xff ^ 0xfff is x ^ 0xf00, not x + 0x10fe
  (func $xorchain (export "xorchain") (param $x i64) (param $n i32) (result i64)
    (local $s i64) (local $a i64)
    (loop $l
      (local.set $a (i64.xor (local.get $x) (i64.const 0xff)))
      (local.set $s (i64.add (i64.mul (local.get $s) (i64.const 1000003))
        (i64.add (local.get $a) (i64.rotl (local.get $x) (i64.const 13)))))
      (local.set $x (i64.xor (local.get $a) (i64.const 0xfff)))
      (local.set $n (i32.sub (local.get $n) (i32.const 1)))
      (br_if $l (i32.and (i32.ne (local.get $n) (i32.const 0))
                         (i64.ne (local.get $s) (i64.const 1)))))
    (local.get $s))

  ;; x * 0x100000001 * 0x100000001, the product past 2^64
  (func $mulwrap (export "mulwrap") (param $x i64) (param $n i32) (result i64)
    (local $s i64) (local $a i64)
    (loop $l
      (local.set $a (i64.mul (local.get $x) (i64.const 0x100000001)))
      (local.set $s (i64.add (local.get $s) (i64.xor (local.get $a) (local.get $x))))
      (local.set $x (i64.mul (local.get $a) (i64.const 0x100000001)))
      (local.set $n (i32.sub (local.get $n) (i32.const 1)))
      (br_if $l (local.get $n)))
    (local.get $s))

  ;; x + 0x7fffffff + 0x7fffffff + 3 is x + 1, the sum past 2^32
  (func $addwrap (export "addwrap") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $a i32) (local $b i32)
    (loop $l
      (local.set $a (i32.add (local.get $i) (i32.const 0x7fffffff)))
      (local.set $b (i32.add (local.get $a) (i32.const 0x7fffffff)))
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 7))
        (i32.xor (i32.xor (local.get $a) (local.get $b)) (local.get $i))))
      (local.set $i (i32.add (local.get $b) (i32.const 3)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))

  ;; 16 links below the value: x + 17
  (func $deep (export "deep") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $t i32) (local $x i32)
    (loop $l
      (local.set $t (local.get $i))
      (local.set $x (local.get $i))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31)) (local.get $x)))
      (local.set $i (i32.add (local.get $t) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))

  ;; 17: left alone
  (func $deeper (export "deeper") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $t i32) (local $x i32)
    (loop $l
      (local.set $t (local.get $i))
      (local.set $x (local.get $i))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $t (i32.add (local.get $t) (i32.const 1)))
      (local.set $x (i32.xor (local.get $x) (local.get $t)))
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31)) (local.get $x)))
      (local.set $i (i32.add (local.get $t) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))

  ;; two back edges, the chain's end on the second
  (func $twoedge (export "twoedge") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $a i32) (local $b i32) (local $c i32)
    (block $done
      (loop $l
        (local.set $a (i32.add (local.get $i) (i32.const 1)))
        (local.set $b (i32.add (local.get $a) (i32.const 1)))
        (local.set $c (i32.add (local.get $b) (i32.const 1)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
          (i32.xor (i32.xor (local.get $i) (i32.shl (local.get $a) (i32.const 3)))
            (i32.xor (i32.rotl (local.get $b) (i32.const 7))
                     (i32.mul (local.get $c) (local.get $c))))))
        (if (i32.and (local.get $s) (i32.const 1))
          (then
            (local.set $i (local.get $a))
            (br_if $l (i32.lt_u (local.get $i) (local.get $n)))
            (br $done)))
        (local.set $i (i32.add (local.get $c) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (local.get $s))

  ;; a link that adds a variable
  (func $varlink (export "varlink") (param $d i32) (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $a i32)
    (loop $l
      (local.set $a (i32.add (local.get $i) (local.get $d)))
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
        (i32.xor (local.get $a) (local.get $i))))
      (local.set $i (i32.add (local.get $a) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))

  ;; the chain starts at x * 3, no parameter
  (func $notparam (export "notparam") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $a i32)
    (loop $l
      (local.set $a (i32.add (i32.mul (local.get $i) (i32.const 3)) (i32.const 1)))
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
        (i32.xor (local.get $a) (local.get $i))))
      (local.set $i (i32.add (local.get $a) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))

  ;; the chain from i handed j, i handed j's value
  (func $swap (export "swap") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32) (local $a i32) (local $b i32) (local $t i32)
    (local.set $j (i32.const 5))
    (loop $l
      (local.set $a (i32.add (local.get $i) (i32.const 1)))
      (local.set $b (i32.add (local.get $a) (i32.const 1)))
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
        (i32.xor (i32.xor (local.get $i) (local.get $a))
          (i32.xor (i32.shl (local.get $b) (i32.const 2)) (local.get $j)))))
      (local.set $t (i32.add (local.get $b) (i32.const 1)))
      (local.set $i (local.get $j))
      (local.set $j (local.get $t))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))
)
