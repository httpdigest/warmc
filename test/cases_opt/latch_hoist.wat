;; A latch whose way round moves values makes those moves ahead of its
;; branch, so that the branch goes round on its own and not out to a block
;; of moves and a jump back (isel_ops.inc T_BRIF; latch_hoists): $one's
;; p + 1 goes to p's register ahead of the test.  Not where a register the
;; moves write is one the way out keeps a value in: $kept's way out passes
;; p as it was on the way in, in the register of its result, which the way
;; round's p + 1 would have overwritten.  A way out with moves of its own
;; keeps them on its edge (latch_both): $both's way out moves m into the
;; register of the merge it shares with the test ahead of the loop, its way
;; round p + 1 into p's -- and $fout's a double, from the register d0 where
;; the way round writes x0: a register of the other file.  Not where the way
;; round writes a register the way out's moves read: $clash's way out moves
;; p as it was to the merge's register, from the one the way round puts
;; p + 1 in (cbench/varargs' sum loop under RA2, a value loaded back on the
;; way out, went out to a move and a branch back on every trip); nor where
;; the way out makes a constant again ($zout's 0).  Only a way back into a
;; loop, not a branch forward ($fwd's to the merge its arm meets); only the
;; likelier way ($cont's inner latch, whose way back into the outer loop is
;; less likely than its way round the inner one); no constant made again
;; ($zero's r, made 0 on every way round); no store to a slot ($many's
;; i + 1, more values round the loop than the registers hold).  The calls through the
;; table keep $g and $gf out of line, their arguments in their registers.
;;
;; stats -O2 a64: latch_hoists=1 latch_both=2
;; shape: one \sb\s=0
;; shape: kept \sb\s=1
;; shape: both \sb\s=0
;; shape: clash \sb\s=1
;;
;; run: one 0 => i32:102
;; run: one 10 => i32:1404
;; run: one 30 => i32:5506
;; run: kept 0 => i32:1
;; run: kept 10 => i32:3
;; run: kept 30 => i32:5
;; run: both 0 5 => i32:-698
;; run: both 10 5 => i32:-40
;; run: both 30 2 => i32:-16
;; run: clash 0 5 => i32:98958
;; run: clash 10 5 => i32:2958
;; run: clash 30 2 => i32:4979
;; run: fwd 4 3 => i32:16
;; run: fwd 4 5 => i32:24
;; run: fwd 7 9 => i32:70
;; run: zero 16 => i32:362
;; run: zero 18 => i32:44
;; run: zero 19 => i32:7
;; run: many 1 => i32:1850057700
;; run: many 3 => i32:2118921950
;; run: many 7 => i32:-140484994
;; run: zout 0 5 => i32:-698
;; run: zout 10 5 => i32:-5
;; run: zout 30 2 => i32:-2
;; run: fout 0 5 => i32:-7
;; run: fout 10 5 => i32:-11
;; run: fout 30 2 => i32:-11
;; run: cont 1 => i32:0
;; run: cont 4 => i32:45
;; run: cont 9 => i32:1360
(module
  (type $t (func (param i32 i32 i32) (result i32)))
  (type $tf (func (param i32 i32 f64) (result i32)))
  (memory 1)
  (table 4 funcref)
  (elem (i32.const 0) $g $h $gf $hf)
  ;; the index of $gf in the table, and $zero's string
  (data (i32.const 4) "\02")
  (data (i32.const 16) "\05\07\09\02")
  ;; g(a, b, c) = 1000a - b - 7c
  (func $g (type $t)
    (i32.sub (i32.mul (local.get 0) (i32.const 1000))
             (i32.add (local.get 1) (i32.mul (local.get 2) (i32.const 7)))))
  (func $h (type $t) (i32.const 0))
  ;; gf(a, b, x) = 1000a - b - trunc x
  (func $gf (type $tf)
    (i32.sub (i32.mul (local.get 0) (i32.const 1000))
             (i32.add (local.get 1) (i32.trunc_f64_s (local.get 2)))))
  (func $hf (type $tf) (i32.const 0))
  ;; s = 0^2 + 1^2 + ... until s > n, p one past the last square: 100s + p
  (func $one (export "one") (param $n i32) (result i32)
    (local $p i32) (local $q i32) (local $s i32)
    (loop $l
      (local.set $q (i32.add (local.get $p) (i32.const 1)))
      (local.set $s (i32.add (local.get $s) (i32.mul (local.get $p) (local.get $p))))
      (local.set $p (local.get $q))
      (br_if $l (i32.le_u (local.get $s) (local.get $n))))
    (i32.add (i32.mul (local.get $s) (i32.const 100)) (local.get $p)))
  ;; the last p squared: the first whose sum passes n
  (func $kept (export "kept") (param $n i32) (result i32)
    (local $p i32) (local $s i32) (local $o i32)
    (block $out (result i32)
      (loop $l
        (local.set $o (local.get $p))
        (local.set $s (i32.add (local.get $s) (i32.mul (local.get $p) (local.get $p))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (drop (br_if $out (local.get $o) (i32.gt_u (local.get $s) (local.get $n))))
        (br $l))
      (unreachable)))
  ;; g(0, m, n ? m : 99): s is read before the loop
  (func $both (export "both") (param $n i32) (param $m i32) (result i32)
    (local $p i32) (local $q i32) (local $s i32)
    (call_indirect (type $t) (local.get $s) (local.get $m)
      (block $out (result i32)
        (drop (br_if $out (i32.const 99) (i32.eqz (local.get $n))))
        (loop $l
          (local.set $q (i32.add (local.get $p) (i32.const 1)))
          (local.set $s (i32.add (local.get $s) (i32.mul (local.get $p) (local.get $p))))
          (local.set $p (local.get $q))
          (drop (br_if $out (local.get $m) (i32.gt_u (local.get $s) (local.get $n))))
          (br $l))
        (unreachable))
      (i32.load (i32.const 0))))
  ;; g(n ? kept(n) : 99, 7, m)
  (func $clash (export "clash") (param $n i32) (param $m i32) (result i32)
    (local $p i32) (local $s i32) (local $o i32)
    (call_indirect (type $t)
      (block $out (result i32)
        (drop (br_if $out (i32.const 99) (i32.eqz (local.get $n))))
        (loop $l
          (local.set $o (local.get $p))
          (local.set $s (i32.add (local.get $s) (i32.mul (local.get $p) (local.get $p))))
          (local.set $p (i32.add (local.get $p) (i32.const 1)))
          (drop (br_if $out (local.get $o) (i32.gt_u (local.get $s) (local.get $n))))
          (br $l))
        (unreachable))
      (i32.const 7) (local.get $m)
      (i32.load (i32.const 0))))
  ;; g(0, m, n ? 0 : 99)
  (func $zout (export "zout") (param $n i32) (param $m i32) (result i32)
    (local $p i32) (local $q i32) (local $s i32)
    (call_indirect (type $t) (local.get $s) (local.get $m)
      (block $out (result i32)
        (drop (br_if $out (i32.const 99) (i32.eqz (local.get $n))))
        (loop $l
          (local.set $q (i32.add (local.get $p) (i32.const 1)))
          (local.set $s (i32.add (local.get $s) (i32.mul (local.get $p) (local.get $p))))
          (local.set $p (local.get $q))
          (drop (br_if $out (i32.const 0) (i32.gt_u (local.get $s) (local.get $n))))
          (br $l))
        (unreachable))
      (i32.load (i32.const 0))))
  ;; gf(0, m, n ? 1.5 times the trips : 2.5)
  (func $fout (export "fout") (param $n i32) (param $m i32) (result i32)
    (local $p i32) (local $q i32) (local $s i32) (local $x f64)
    (call_indirect (type $tf) (local.get $s) (local.get $m)
      (block $out (result f64)
        (drop (br_if $out (f64.const 2.5) (i32.eqz (local.get $n))))
        (loop $l
          (local.set $q (i32.add (local.get $p) (i32.const 1)))
          (local.set $s (i32.add (local.get $s) (i32.mul (local.get $p) (local.get $p))))
          (local.set $x (f64.add (local.get $x) (f64.const 1.5)))
          (local.set $p (local.get $q))
          (drop (br_if $out (local.get $x) (i32.gt_u (local.get $s) (local.get $n))))
          (br $l))
        (unreachable))
      (i32.load (i32.const 4))))
  ;; a + ab: the arm stores b and subtracts what is at 12, nothing
  (func $fwd (export "fwd") (param $a i32) (param $b i32) (result i32)
    (i32.add (local.get $a)
      (i32.mul (local.get $b)
        (block $j (result i32)
          (drop (br_if $j (local.get $a) (i32.ne (local.get $b) (i32.const 3))))
          (i32.store (i32.const 8) (local.get $b))
          (i32.sub (local.get $a) (i32.load (i32.const 12)))))))
  ;; a string's bytes from p, while they are past 1, the first plus 5,
  ;; folded as s = 3s + byte
  (func $zero (export "zero") (param $p i32) (result i32)
    (local $r i32) (local $s i32)
    (local.set $r (i32.const 5))
    (loop $l
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3))
                             (i32.add (local.get $r) (i32.load8_u (local.get $p)))))
      (local.set $r (i32.const 0))
      (local.set $p (i32.add (local.get $p) (i32.const 1)))
      (br_if $l (i32.gt_u (i32.load8_u (local.get $p)) (i32.const 1))))
    (local.get $s))
  ;; for i = 1 .. n-1, j = 1 .. i: a = ji, b = j at the last, each i's ab
  ;; summed, plus the last a
  (func $cont (export "cont") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $t i32) (local $acc i32) (local $a i32) (local $b i32)
    (block $done
      (loop $outer
        (local.set $acc (i32.add (local.get $acc) (i32.mul (local.get $a) (local.get $b))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $j (i32.const 0))
        (loop $inner
          (local.set $j (i32.add (local.get $j) (i32.const 1)))
          (local.set $t (i32.mul (local.get $j) (local.get $i)))
          (local.set $a (local.get $t))
          (local.set $b (local.get $j))
          (br_if $outer (i32.eq (local.get $j) (local.get $i)))
          (br $inner))))
    (i32.add (local.get $acc) (local.get $a)))
  ;; 24 values round the loop, more than the registers hold: i + 1 goes
  ;; to i's slot, from its own, on the way round
  (func $many (export "many") (param $n i32) (result i32)
    (local $i i32) (local $q i32) (local $a0 i32) (local $a1 i32) (local $a2 i32) (local $a3 i32) (local $a4 i32) (local $a5 i32) (local $a6 i32) (local $a7 i32) (local $a8 i32) (local $a9 i32) (local $a10 i32) (local $a11 i32) (local $a12 i32) (local $a13 i32) (local $a14 i32) (local $a15 i32) (local $a16 i32) (local $a17 i32) (local $a18 i32) (local $a19 i32) (local $a20 i32) (local $a21 i32) (local $a22 i32) (local $a23 i32)
    (local.set $a0 (i32.add (local.get $n) (i32.const 0)))
    (local.set $a1 (i32.add (local.get $n) (i32.const 1)))
    (local.set $a2 (i32.add (local.get $n) (i32.const 2)))
    (local.set $a3 (i32.add (local.get $n) (i32.const 3)))
    (local.set $a4 (i32.add (local.get $n) (i32.const 4)))
    (local.set $a5 (i32.add (local.get $n) (i32.const 5)))
    (local.set $a6 (i32.add (local.get $n) (i32.const 6)))
    (local.set $a7 (i32.add (local.get $n) (i32.const 7)))
    (local.set $a8 (i32.add (local.get $n) (i32.const 8)))
    (local.set $a9 (i32.add (local.get $n) (i32.const 9)))
    (local.set $a10 (i32.add (local.get $n) (i32.const 10)))
    (local.set $a11 (i32.add (local.get $n) (i32.const 11)))
    (local.set $a12 (i32.add (local.get $n) (i32.const 12)))
    (local.set $a13 (i32.add (local.get $n) (i32.const 13)))
    (local.set $a14 (i32.add (local.get $n) (i32.const 14)))
    (local.set $a15 (i32.add (local.get $n) (i32.const 15)))
    (local.set $a16 (i32.add (local.get $n) (i32.const 16)))
    (local.set $a17 (i32.add (local.get $n) (i32.const 17)))
    (local.set $a18 (i32.add (local.get $n) (i32.const 18)))
    (local.set $a19 (i32.add (local.get $n) (i32.const 19)))
    (local.set $a20 (i32.add (local.get $n) (i32.const 20)))
    (local.set $a21 (i32.add (local.get $n) (i32.const 21)))
    (local.set $a22 (i32.add (local.get $n) (i32.const 22)))
    (local.set $a23 (i32.add (local.get $n) (i32.const 23)))
    (loop $l
      (local.set $q (i32.add (local.get $i) (i32.const 1)))
      (local.set $a0 (i32.add (i32.mul (local.get $a0) (i32.const 3)) (local.get $a1)))
      (local.set $a1 (i32.add (i32.mul (local.get $a1) (i32.const 3)) (local.get $a2)))
      (local.set $a2 (i32.add (i32.mul (local.get $a2) (i32.const 3)) (local.get $a3)))
      (local.set $a3 (i32.add (i32.mul (local.get $a3) (i32.const 3)) (local.get $a4)))
      (local.set $a4 (i32.add (i32.mul (local.get $a4) (i32.const 3)) (local.get $a5)))
      (local.set $a5 (i32.add (i32.mul (local.get $a5) (i32.const 3)) (local.get $a6)))
      (local.set $a6 (i32.add (i32.mul (local.get $a6) (i32.const 3)) (local.get $a7)))
      (local.set $a7 (i32.add (i32.mul (local.get $a7) (i32.const 3)) (local.get $a8)))
      (local.set $a8 (i32.add (i32.mul (local.get $a8) (i32.const 3)) (local.get $a9)))
      (local.set $a9 (i32.add (i32.mul (local.get $a9) (i32.const 3)) (local.get $a10)))
      (local.set $a10 (i32.add (i32.mul (local.get $a10) (i32.const 3)) (local.get $a11)))
      (local.set $a11 (i32.add (i32.mul (local.get $a11) (i32.const 3)) (local.get $a12)))
      (local.set $a12 (i32.add (i32.mul (local.get $a12) (i32.const 3)) (local.get $a13)))
      (local.set $a13 (i32.add (i32.mul (local.get $a13) (i32.const 3)) (local.get $a14)))
      (local.set $a14 (i32.add (i32.mul (local.get $a14) (i32.const 3)) (local.get $a15)))
      (local.set $a15 (i32.add (i32.mul (local.get $a15) (i32.const 3)) (local.get $a16)))
      (local.set $a16 (i32.add (i32.mul (local.get $a16) (i32.const 3)) (local.get $a17)))
      (local.set $a17 (i32.add (i32.mul (local.get $a17) (i32.const 3)) (local.get $a18)))
      (local.set $a18 (i32.add (i32.mul (local.get $a18) (i32.const 3)) (local.get $a19)))
      (local.set $a19 (i32.add (i32.mul (local.get $a19) (i32.const 3)) (local.get $a20)))
      (local.set $a20 (i32.add (i32.mul (local.get $a20) (i32.const 3)) (local.get $a21)))
      (local.set $a21 (i32.add (i32.mul (local.get $a21) (i32.const 3)) (local.get $a22)))
      (local.set $a22 (i32.add (i32.mul (local.get $a22) (i32.const 3)) (local.get $a23)))
      (local.set $a23 (i32.add (i32.mul (local.get $a23) (i32.const 3)) (local.get $a0)))
      (local.set $a0 (i32.add (local.get $a0) (local.get $i)))
      (local.set $i (local.get $q))
      (br_if $l (i32.lt_u (local.get $q) (local.get $n))))
    (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (local.get $a0) (i32.const 7)) (local.get $a1)) (i32.const 7)) (local.get $a2)) (i32.const 7)) (local.get $a3)) (i32.const 7)) (local.get $a4)) (i32.const 7)) (local.get $a5)) (i32.const 7)) (local.get $a6)) (i32.const 7)) (local.get $a7)) (i32.const 7)) (local.get $a8)) (i32.const 7)) (local.get $a9)) (i32.const 7)) (local.get $a10)) (i32.const 7)) (local.get $a11)) (i32.const 7)) (local.get $a12)) (i32.const 7)) (local.get $a13)) (i32.const 7)) (local.get $a14)) (i32.const 7)) (local.get $a15)) (i32.const 7)) (local.get $a16)) (i32.const 7)) (local.get $a17)) (i32.const 7)) (local.get $a18)) (i32.const 7)) (local.get $a19)) (i32.const 7)) (local.get $a20)) (i32.const 7)) (local.get $a21)) (i32.const 7)) (local.get $a22)) (i32.const 7)) (local.get $a23))))
