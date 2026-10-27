;; In a loop, a block that only a conditional branch reaches, which calls
;; and goes on, through blocks only it reaches, to where the branch's other
;; edge goes, is emitted after the function's other blocks (ir.c call_arm;
;; rsbench/vecgrow's push growing its vector): the way round the loop past
;; it takes one branch, not two -- one b, the arm's back.  Not outside a
;; loop, for an arm that calls nothing, or for a diamond's arms.  And a loop
;; that calls is not aligned to 64 bytes as small (isel.c small_loop), one
;; that does not is.  Answers worked out in Python.
;;
;; shape: push \sb\s=1
;; shape: noloop \sb\s=0
;; shape: nocall \sb\s=0
;; shape: diamond \sb\s=1
;; stats -O2 a64: small_loops=1
;;
;; run: push 0 => i32:100
;; run: callloop 0 => i32:0
;; run: small 0 => i32:0
;; run: diamond 0 => i32:0
;; run: push 1 => i32:100
;; run: callloop 1 => i32:0
;; run: small 1 => i32:0
;; run: diamond 1 => i32:0
;; run: push 4 => i32:400
;; run: callloop 4 => i32:12
;; run: small 4 => i32:14
;; run: diamond 4 => i32:4
;; run: push 5 => i32:501
;; run: callloop 5 => i32:20
;; run: small 5 => i32:30
;; run: diamond 5 => i32:-4
;; run: push 9 => i32:902
;; run: callloop 9 => i32:72
;; run: small 9 => i32:204
;; run: diamond 9 => i32:-8
;; run: push 100 => i32:10005
;; run: callloop 100 => i32:9900
;; run: small 100 => i32:328350
;; run: diamond 100 => i32:100
;; run: noloop 4 4 => i32:12
;; run: noloop 4 5 => i32:9
;; run: noloop 0 0 => i32:0
;; run: nocall 1 3 => i32:1000000
;; run: nocall 20 3 => i32:37100
;; run: nocall 100 7 => i32:223

(module
;; doubles c, recursively so that it is not inlined: the push's rare call
  (func $grow (param $c i32) (param $k i32) (result i32)
    (if (result i32) (i32.eqz (local.get $k))
      (then (i32.shl (local.get $c) (i32.const 1)))
      (else (call $grow (local.get $c) (i32.sub (local.get $k) (i32.const 1))))))
  (func $push (export "push") (param $n i32) (result i32)
    (local $i i32) (local $len i32) (local $cap i32) (local $grows i32)
    (local.set $cap (i32.const 4))
    (loop $l
      (if (i32.eq (local.get $len) (local.get $cap))
        (then (local.set $cap (call $grow (local.get $cap) (i32.const 3)))
              (local.set $grows (i32.add (local.get $grows) (i32.const 1)))))
      (local.set $len (i32.add (local.get $len) (i32.const 1)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (i32.add (i32.mul (local.get $len) (i32.const 100)) (local.get $grows)))
  (func $noloop (export "noloop") (param $c i32) (param $n i32) (result i32)
    (if (i32.eq (local.get $n) (local.get $c))
      (then (local.set $c (call $grow (local.get $c) (i32.const 3)))))
    (i32.add (local.get $c) (local.get $n)))
  (func $nocall (export "nocall") (param $n i32) (param $d i32) (result i32)
    (local $i i32) (local $x i32)
    (local.set $x (i32.const 1000000))
    (loop $l
      (if (i32.eq (i32.and (local.get $i) (i32.const 7)) (i32.const 3))
        (then (local.set $x (i32.div_u (local.get $x) (local.get $d)))))
      (local.set $x (i32.add (local.get $x) (local.get $i)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $x))
  (func $diamond (export "diamond") (param $n i32) (result i32)
    (local $i i32) (local $x i32)
    (loop $l
      (if (i32.and (local.get $i) (i32.const 1))
        (then (local.set $x (i32.add (local.get $x) (call $grow (local.get $i) (i32.const 3)))))
        (else (local.set $x (i32.sub (local.get $x) (call $grow (local.get $i) (i32.const 4))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $x))
  (func $callloop (export "callloop") (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (loop $l
      (local.set $s (i32.add (local.get $s) (call $grow (local.get $i) (i32.const 2))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))
  (func $small (export "small") (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (loop $l
      (local.set $s (i32.add (local.get $s) (i32.mul (local.get $i) (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))
)
