;; i32 block parameters that edges pass the wraps of i64s, widened to take
;; the i64s (opt.c pass_wrap_params): the join's readers read the low half
;; through one wrap at its head, which is no instruction (isel.c
;; analyse_wrap_alias), and each call's result, tested after its wrap went
;; to the join, gives its register to the join's parameter where the wrap
;; took a register and a move of its own.  Widened: a join of two calls'
;; results and a constant, read by an add (calls); a join of a result, a
;; constant and a shift's wrap, stored as 4 bytes (store).  Not: one read by
;; a division (div); one an i32 parameter reaches, which would need an
;; extend on that edge (param); one whose i64s nothing else reads, no
;; register saved (lone); one returned as the i32 result (ret); one passed
;; on to another join, nor that one (chain, and chaindiv, the second
;; divided); and a loop's parameters an i32 that is no wrap enters, from
;; before the loop (loop, three) or from within it (loopin).
;;
;; stats -O1 a64: wrap_params=2
;; stats -O2 a64: wrap_params=2
;; shape: calls mov\tw\d+, w\d+$=0
;; shape: store mov\tw\d+, w\d+$=0
;;
;; run: calls 4886718345 0 => i64:12
;; run: calls 4886718345 1 => i64:591751054
;; run: calls 1 1 => i64:6
;; run: calls 3 0 => i64:12
;; run: calls 0 1 => i64:5
;; run: store 4886718345 0 => i64:1735514685
;; run: store 4886718345 1 => i64:3703216247
;; run: store 2 1 => i64:4294967294
;; run: store 5 0 => i64:3472419032
;; run: store 4 0 => i64:2147483632
;; run: div 4886718345 0 => i64:2
;; run: div 4886718345 1 => i64:2
;; run: div 0 1 => i64:0
;; run: param 4886718345 0 => i64:5
;; run: param 4886718345 1 => i64:6
;; run: param 0 0 => i64:5
;; run: lone 4886718345 1 => i64:1735514690
;; run: lone 4886718345 0 => i64:3703216252
;; run: chain 4886718345 0 => i64:16
;; run: chain 4886718345 1 => i64:14
;; run: chain 6 1 => i64:14
;; run: ret 4886718345 0 => i32:13
;; run: ret 4886718345 1 => i32:13
;; run: ret 2 0 => i32:-23791574
;; run: chaindiv 4886718345 0 => i64:3
;; run: chaindiv 4886718345 1 => i64:3
;; run: chaindiv 6 1 => i64:3
;; run: loop 4886718345 0 => i64:13785933346
;; run: loop 77 1 => i64:8673820628
;; run: loopin 4886718345 0 => i64:11014605538
;; run: loopin 77 1 => i64:8673820672
;; run: three 4886718345 0 => i64:38743080037
;; run: three 77 1 => i64:25853690087
(module
  (type $v (func (param i64) (result i64)))
  (memory 1)
  (table 2 funcref)
  (elem (i32.const 0) $mix $neg)
  ;; called through the table: results no fold can see into
  (func $mix (type $v)
    (i64.xor (i64.mul (local.get 0) (i64.const 0x9e37_79b9_7f4a_7c15)) (i64.const 0x1234_5678_0000_0000)))
  (func $neg (type $v) (i64.sub (i64.const 0) (local.get 0)))

  ;; a join of two calls' wraps, each result tested after: widened
  (func $calls (export "calls") (param $a i64) (param $f i32) (result i64) (local $r i64) (local $s i64)
    (local.set $r (call_indirect (type $v) (local.get $a) (local.get $f)))
    (i64.extend_i32_u
      (i32.add (i32.const 5)
        (block $j (result i32)
          (br_if $j (i32.wrap_i64 (local.get $r)) (i64.lt_u (local.get $r) (i64.const 0x8000_0000_0000_0000)))
          (drop)
          (local.set $s (call_indirect (type $v) (local.get $r) (local.get $f)))
          (br_if $j (i32.wrap_i64 (local.get $s)) (i64.lt_u (local.get $s) (i64.const 0x4000_0000_0000_0000)))
          (drop)
          (i64.store (i32.const 72) (local.get $s))
          (i32.const 7)))))

  ;; a constant on the third edge, the join stored as 4 bytes: widened
  (func $store (export "store") (param $a i64) (param $f i32) (result i64) (local $r i64)
    (local.set $r (call_indirect (type $v) (local.get $a) (local.get $f)))
    (i32.store (i32.const 64)
      (block $j (result i32)
        (br_if $j (i32.wrap_i64 (local.get $r)) (i64.lt_s (local.get $r) (i64.const 0)))
        (drop)
        (br_if $j (i32.const 0x7fff_fff0) (i64.eqz (i64.and (local.get $r) (i64.const 1))))
        (drop)
        (i64.store (i32.const 72) (local.get $r))
        (i32.wrap_i64 (i64.shr_u (local.get $r) (i64.const 7)))))
    (i64.load (i32.const 64)))

  ;; divided: no reader of the low half only, not widened
  (func $div (export "div") (param $a i64) (param $f i32) (result i64) (local $r i64)
    (local.set $r (call_indirect (type $v) (local.get $a) (local.get $f)))
    (i64.extend_i32_u
      (i32.div_u
        (block $j (result i32)
          (br_if $j (i32.wrap_i64 (local.get $r)) (i64.lt_u (local.get $r) (i64.const 0x8000_0000_0000_0000)))
          (drop)
          (i64.store (i32.const 72) (local.get $r))
          (i32.const 7))
        (i32.const 3))))

  ;; an i32 parameter on the other edge, which would need an extend there:
  ;; not widened
  (func $param (export "param") (param $a i64) (param $f i32) (result i64) (local $r i64)
    (local.set $r (call_indirect (type $v) (local.get $a) (local.get $f)))
    (i64.extend_i32_u
      (i32.add (i32.const 5)
        (block $j (result i32)
          (br_if $j (i32.wrap_i64 (local.get $r)) (i64.lt_u (local.get $r) (i64.const 0x8000_0000_0000_0000)))
          (drop)
          (i64.store (i32.const 72) (local.get $r))
          (local.get $f)))))

  ;; wraps of i64s nothing else reads: no register is saved, not widened
  (func $lone (export "lone") (param $a i64) (param $p i32) (result i64)
    (i64.extend_i32_u
      (i32.add (i32.const 5)
        (block $j (result i32)
          (br_if $j (i32.wrap_i64 (call_indirect (type $v) (local.get $a) (i32.const 0))) (local.get $p))
          (drop)
          (i64.store (i32.const 72) (local.get $a))
          (i32.wrap_i64 (call_indirect (type $v) (local.get $a) (i32.const 1)))))))

  ;; the join passed on to another join: not widened
  (func $chain (export "chain") (param $a i64) (param $f i32) (result i64) (local $r i64)
    (local.set $r (call_indirect (type $v) (local.get $a) (local.get $f)))
    (i64.extend_i32_u
      (i32.add (i32.const 5)
        (block $k (result i32)
          (br_if $k
            (block $j (result i32)
              (br_if $j (i32.wrap_i64 (local.get $r)) (i64.lt_u (local.get $r) (i64.const 0x8000_0000_0000_0000)))
              (drop)
              (i64.store (i32.const 72) (local.get $r))
              (i32.const 9))
            (i32.wrap_i64 (i64.and (local.get $r) (i64.const 2))))
          (drop)
          (i64.store (i32.const 80) (local.get $r))
          (i32.const 11)))))

  ;; returned as it is, the i32 result: not widened
  (func $ret (export "ret") (param $a i64) (param $f i32) (result i32) (local $r i64)
    (local.set $r (call_indirect (type $v) (local.get $a) (local.get $f)))
    (block $j (result i32)
      (br_if $j (i32.wrap_i64 (local.get $r)) (i64.lt_u (local.get $r) (i64.const 0x8000_0000_0000_0000)))
      (drop)
      (i64.store (i32.const 72) (local.get $r))
      (i32.const 13)))

  ;; chain's two joins, the second's divided: neither widened
  (func $chaindiv (export "chaindiv") (param $a i64) (param $f i32) (result i64) (local $r i64)
    (local.set $r (call_indirect (type $v) (local.get $a) (local.get $f)))
    (i64.extend_i32_u
      (i32.div_u
        (block $k (result i32)
          (br_if $k
            (block $j (result i32)
              (br_if $j (i32.wrap_i64 (local.get $r)) (i64.lt_u (local.get $r) (i64.const 0x8000_0000_0000_0000)))
              (drop)
              (i64.store (i32.const 72) (local.get $r))
              (i32.const 9))
            (i32.wrap_i64 (i64.and (local.get $r) (i64.const 2))))
          (drop)
          (i64.store (i32.const 80) (local.get $r))
          (i32.const 11))
        (i32.const 3))))

  ;; a loop's i32 its trips pass the wrap of a call's result, tested after,
  ;; and an i32 that is no wrap enters: widened, extended on the way in
  (func $loop (export "loop") (param $a i64) (param $f i32) (result i64) (local $x i32) (local $r i64) (local $n i32) (local $s i64)
    (local.set $x (i32.add (i32.wrap_i64 (local.get $a)) (i32.const 3)))
    (loop $l
      (i64.store (i32.const 88) (local.get $s))
      (local.set $s (i64.add (local.get $s) (i64.extend_i32_u (i32.xor (local.get $x) (i32.const 0x55)))))
      (local.set $r (call_indirect (type $v) (i64.extend_i32_u (local.get $x)) (local.get $f)))
      (local.set $s (i64.add (local.get $s) (i64.shr_u (local.get $r) (i64.const 40))))
      (local.set $x (i32.wrap_i64 (local.get $r)))
      (local.set $n (i32.add (local.get $n) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $n) (i32.const 5))))
    (local.get $s))

  ;; the same, but the i32 that is no wrap comes from within the loop, on
  ;; every other trip: not widened
  (func $loopin (export "loopin") (param $a i64) (param $f i32) (result i64) (local $x i32) (local $r i64) (local $n i32) (local $s i64)
    (local.set $x (i32.add (i32.wrap_i64 (local.get $a)) (i32.const 3)))
    (loop $l
      (i64.store (i32.const 88) (local.get $s))
      (local.set $s (i64.add (local.get $s) (i64.extend_i32_u (i32.xor (local.get $x) (i32.const 0x55)))))
      (local.set $r (call_indirect (type $v) (i64.extend_i32_u (local.get $x)) (local.get $f)))
      (local.set $s (i64.add (local.get $s) (i64.shr_u (local.get $r) (i64.const 40))))
      (local.set $x (i32.wrap_i64 (local.get $r)))
      (local.set $n (i32.add (local.get $n) (i32.const 1)))
      (if (i32.and (local.get $n) (i32.const 1))
        (then (i64.store (i32.const 96) (local.get $s))
              (local.set $x (i32.add (local.get $x) (i32.const 7)))))
      (br_if $l (i32.lt_u (local.get $n) (i32.const 5))))
    (local.get $s))

  ;; three of a loop's i32s, the same one entering each and the same call's
  ;; wrap each trip: widened, the one entering extended once for all three
  (func $three (export "three") (param $a i64) (param $f i32) (result i64)
    (local $x i32) (local $y i32) (local $z i32) (local $v i32) (local $r i64) (local $n i32) (local $s i64)
    (local.set $v (i32.add (i32.wrap_i64 (local.get $a)) (i32.const 3)))
    (local.set $x (local.get $v)) (local.set $y (local.get $v)) (local.set $z (local.get $v))
    (loop $l
      (i64.store (i32.const 88) (local.get $s))
      (local.set $s (i64.add (local.get $s) (i64.extend_i32_u (i32.xor (local.get $x) (i32.const 0x55)))))
      (local.set $s (i64.add (local.get $s) (i64.extend_i32_u (i32.mul (local.get $y) (i32.const 3)))))
      (local.set $s (i64.add (local.get $s) (i64.extend_i32_u (i32.sub (local.get $z) (i32.const 9)))))
      (local.set $r (call_indirect (type $v) (i64.extend_i32_u (local.get $x)) (local.get $f)))
      (local.set $s (i64.add (local.get $s) (i64.shr_u (local.get $r) (i64.const 40))))
      (local.set $x (i32.wrap_i64 (local.get $r)))
      (local.set $y (i32.wrap_i64 (local.get $r)))
      (local.set $z (i32.wrap_i64 (local.get $r)))
      (local.set $n (i32.add (local.get $n) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $n) (i32.const 5))))
    (local.get $s))
)
