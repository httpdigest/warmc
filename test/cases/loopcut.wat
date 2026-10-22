;; Shapes a merged function leaves the register allocator and the passes
;; around it, each next to the variant they must leave alone:
;;
;;   * a loop's accumulator coalesced with the param that carries its result
;;     past a loop nest that spills, so the plan cuts it at the header and at
;;     the exit merges -- two loops in a row, where the exit merge of the
;;     first is the header param of the second, and a path round both;
;;   * values that cross calls only behind tests at the top level, which may
;;     give up the callee-saved registers they hold for caller-saved ones
;;     saved around those calls, next to values crossing a call every run
;;     makes, which keep theirs;
;;   * a shadow stack pointer pushed and popped round an inner loop that
;;     calls, which global SSA leaves in memory.
;;
;; The values come from an exported mutable global so nothing folds them away
;; first, and the calls go through a table so nothing inlines them.

(module
  (global $seed (export "seed") (mut i64) (i64.const 0x9e37_79b9_7f4a_7c15))
  (global $sp (mut i32) (i32.const 4096))
  (memory 1)
  (type $mix (func (param i64) (result i64)))
  (table 2 funcref)
  (elem (i32.const 0) $mix_a $mix_b)

  (func $mix_a (type $mix) (param $x i64) (result i64)
    (i64.xor (i64.rotl (local.get $x) (i64.const 17))
             (i64.mul (local.get $x) (i64.const 0x2545_f491_4f6c_dd1d))))

  ;; Keeps its scratch on the shadow stack, so a call to it reads and
  ;; writes $sp.
  (func $mix_b (type $mix) (param $x i64) (result i64)
    (local $i i32) (local $a i64) (local $fp i32)
    (local.set $fp (i32.sub (global.get $sp) (i32.const 8)))
    (global.set $sp (local.get $fp))
    (i64.store (local.get $fp) (local.get $x))
    (local.set $a (local.get $x))
    (loop $l
      (local.set $a (i64.add (i64.rotl (local.get $a) (i64.const 5))
                             (i64.load (local.get $fp))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 3))))
    (global.set $sp (i32.add (local.get $fp) (i32.const 8)))
    (local.get $a))

  (func $v (param $k i32) (result i64)
    (i64.mul (i64.add (global.get $seed) (i64.extend_i32_u (local.get $k)))
             (i64.const 0x9e37_79b9_7f4a_7c15)))

  ;; Sixteen values carried round a loop nest, more than the registers left
  ;; once the accumulators of the loops before it are live across it.
  (func $pressure (param $k i32) (result i64)
    (local $i i32) (local $j i32)
    (local $a i64) (local $b i64) (local $c i64) (local $d i64)
    (local $e i64) (local $f i64) (local $g i64) (local $h i64)
    (local $p i64) (local $q i64) (local $r i64) (local $s i64)
    (local $t i64) (local $u i64) (local $w i64) (local $x i64)
    (local.set $a (call $v (local.get $k)))
    (local.set $b (i64.rotl (local.get $a) (i64.const 3)))
    (local.set $c (i64.rotl (local.get $a) (i64.const 7)))
    (local.set $d (i64.rotl (local.get $a) (i64.const 11)))
    (local.set $e (i64.rotl (local.get $a) (i64.const 13)))
    (local.set $f (i64.rotl (local.get $a) (i64.const 17)))
    (local.set $g (i64.rotl (local.get $a) (i64.const 19)))
    (local.set $h (i64.rotl (local.get $a) (i64.const 23)))
    (local.set $p (i64.rotl (local.get $a) (i64.const 29)))
    (local.set $q (i64.rotl (local.get $a) (i64.const 31)))
    (local.set $r (i64.rotl (local.get $a) (i64.const 37)))
    (local.set $s (i64.rotl (local.get $a) (i64.const 41)))
    (local.set $t (i64.rotl (local.get $a) (i64.const 43)))
    (local.set $u (i64.rotl (local.get $a) (i64.const 47)))
    (local.set $w (i64.rotl (local.get $a) (i64.const 53)))
    (local.set $x (i64.rotl (local.get $a) (i64.const 59)))
    (loop $nest
    (local.set $i (i32.const 0))
    (loop $l
      (local.set $a (i64.add (local.get $a) (i64.xor (local.get $b) (local.get $x))))
      (local.set $b (i64.add (local.get $b) (i64.xor (local.get $c) (local.get $a))))
      (local.set $c (i64.add (local.get $c) (i64.xor (local.get $d) (local.get $b))))
      (local.set $d (i64.add (local.get $d) (i64.xor (local.get $e) (local.get $c))))
      (local.set $e (i64.add (local.get $e) (i64.xor (local.get $f) (local.get $d))))
      (local.set $f (i64.add (local.get $f) (i64.xor (local.get $g) (local.get $e))))
      (local.set $g (i64.add (local.get $g) (i64.xor (local.get $h) (local.get $f))))
      (local.set $h (i64.add (local.get $h) (i64.xor (local.get $p) (local.get $g))))
      (local.set $p (i64.add (local.get $p) (i64.xor (local.get $q) (local.get $h))))
      (local.set $q (i64.add (local.get $q) (i64.xor (local.get $r) (local.get $p))))
      (local.set $r (i64.add (local.get $r) (i64.xor (local.get $s) (local.get $q))))
      (local.set $s (i64.add (local.get $s) (i64.xor (local.get $t) (local.get $r))))
      (local.set $t (i64.add (local.get $t) (i64.xor (local.get $u) (local.get $s))))
      (local.set $u (i64.add (local.get $u) (i64.xor (local.get $w) (local.get $t))))
      (local.set $w (i64.add (local.get $w) (i64.xor (local.get $x) (local.get $u))))
      (local.set $x (i64.add (local.get $x) (i64.rotl (local.get $w) (local.get $a))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 9))))
    (br_if $nest (i32.lt_u (local.tee $j (i32.add (local.get $j) (i32.const 1)))
                           (i32.add (i32.and (local.get $k) (i32.const 1)) (i32.const 2)))))
    (i64.xor
      (i64.xor (i64.xor (i64.add (local.get $a) (local.get $b)) (i64.add (local.get $c) (local.get $d)))
               (i64.xor (i64.add (local.get $e) (local.get $f)) (i64.add (local.get $g) (local.get $h))))
      (i64.xor (i64.xor (i64.add (local.get $p) (local.get $q)) (i64.add (local.get $r) (local.get $s)))
               (i64.xor (i64.add (local.get $t) (local.get $u)) (i64.add (local.get $w) (local.get $x))))))

  ;; Two summing loops, the second picking up where the first stopped, a path
  ;; round both, and their sum carried past the register-hungry nest above
  ;; (inlined: it is called once) to the add at the end.
  (func $sum_past (param $r i32) (result i64)
    (local $i i32) (local $n i32) (local $acc i64)
    (local.set $n (i32.and (i32.add (local.get $r) (i32.wrap_i64 (global.get $seed)))
                           (i32.const 31)))
    (block $skip
      (br_if $skip (i32.lt_u (local.get $n) (i32.const 3)))
      (loop $first
        (local.set $acc (i64.add (local.get $acc)
                                 (i64.mul (i64.extend_i32_u (local.get $i))
                                          (global.get $seed))))
        (br_if $first (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                                (i32.shr_u (local.get $n) (i32.const 1)))))
      (loop $second
        (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 9))
                                 (i64.extend_i32_u (local.get $i))))
        (br_if $second (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                                 (local.get $n)))))
    (i64.add (call $pressure (local.get $r)) (local.get $acc)))

  (func (export "sum_past_pressure") (result i64)
    (i64.add (call $sum_past (i32.const 5))
             (i64.rotl (call $sum_past (i32.const 22)) (i64.const 1))))

  ;; Values live across calls made only behind tests at the top level, the
  ;; deepest one run in sixteen, and across one call every run makes.  Each
  ;; is called from two places, so it stays a function of its own.
  (func $rare (param $k i32) (result i64)
    (local $x i64) (local $y i64) (local $z i64) (local $w i64)
    (local.set $x (call $v (local.get $k)))
    (local.set $y (i64.xor (local.get $x) (i64.const 0x5555_aaaa_5555_aaaa)))
    (local.set $z (i64.rotl (local.get $y) (i64.const 13)))
    (local.set $w (i64.add (local.get $x) (local.get $z)))
    (if (i32.eq (i32.and (local.get $k) (i32.const 3)) (i32.const 1))
      (then
        (if (i32.eq (i32.and (local.get $k) (i32.const 15)) (i32.const 5))
          (then
            (local.set $w (i64.add (local.get $w)
              (call_indirect (type $mix) (local.get $x)
                             (i32.and (local.get $k) (i32.const 1)))))
            (local.set $w (i64.xor (local.get $w)
              (call_indirect (type $mix) (local.get $y) (i32.const 0))))))
        (local.set $w (i64.sub (local.get $w) (i64.mul (local.get $x) (local.get $y))))))
    (i64.add (i64.add (local.get $w) (local.get $x)) (i64.add (local.get $y) (local.get $z))))

  (func $always (param $k i32) (result i64)
    (local $x i64) (local $y i64) (local $z i64)
    (local.set $x (call $v (local.get $k)))
    (local.set $y (i64.rotl (local.get $x) (i64.const 7)))
    (local.set $z (call_indirect (type $mix) (local.get $y)
                                 (i32.and (local.get $k) (i32.const 1))))
    (i64.add (i64.xor (local.get $x) (local.get $z)) (local.get $y)))

  (func (export "calls_on_side_paths") (result i64)
    (local $i i32) (local $s i64)
    (loop $l
      (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 3))
                             (call $rare (local.get $i))))
      (local.set $s (i64.xor (local.get $s) (call $always (local.get $i))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 100))))
    (local.set $i (i32.const 0))
    (loop $m
      (local.set $s (i64.sub (local.get $s)
                             (call $rare (i32.mul (local.get $i) (i32.const 7)))))
      (local.set $s (i64.add (local.get $s)
                             (call $always (i32.mul (local.get $i) (i32.const 3)))))
      (br_if $m (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 50))))
    (local.get $s))

  ;; A frame pushed on the shadow stack and popped every time round an
  ;; outer loop, around an inner loop that calls: the push and pop look
  ;; like a call-free way round if the inner loop never ran.
  (func (export "frame_round_calling_loop") (result i64)
    (local $r i32) (local $i i32) (local $fp i32) (local $s i64)
    (loop $outer
      (local.set $fp (i32.sub (global.get $sp) (i32.const 16)))
      (global.set $sp (local.get $fp))
      (i64.store (local.get $fp) (i64.extend_i32_u (local.get $r)))
      (global.set $sp (i32.sub (global.get $sp) (i32.const 16)))
      (local.set $i (i32.const 0))
      (block $done
        (loop $inner
          (br_if $done (i32.ge_u (local.get $i) (i32.and (local.get $r) (i32.const 7))))
          (local.set $s (i64.add (local.get $s)
            (call_indirect (type $mix)
              (i64.add (i64.load (local.get $fp)) (i64.extend_i32_u (local.get $i)))
              (i32.and (local.get $i) (i32.const 1)))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $inner)))
      (global.set $sp (i32.add (global.get $sp) (i32.const 16)))
      (local.set $s (i64.xor (local.get $s) (i64.extend_i32_u (global.get $sp))))
      (local.set $s (i64.add (local.get $s) (i64.extend_i32_u (global.get $sp))))
      (global.set $sp (i32.add (local.get $fp) (i32.const 16)))
      (br_if $outer (i32.lt_u (local.tee $r (i32.add (local.get $r) (i32.const 1)))
                              (i32.const 30))))
    (i64.add (local.get $s) (i64.extend_i32_u (global.get $sp))))
)
