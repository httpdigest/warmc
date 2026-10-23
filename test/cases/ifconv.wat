;; If-conversion (pass_ifconvert in src/opt.c): a branch whose arms are a few
;; pure instructions that meet again becomes both arms and a select per value
;; the join takes.  Each helper below is one shape, called by an export with
;; inputs that walk both sides of every branch, and every input comes from a
;; mutable global so that nothing folds the branch away first.  The exports
;; hash what came back into one i32, so a select that picks the wrong side, or
;; the wrong value for one of several join parameters, changes the answer.
;;
;; What there is to get wrong, and the helper that would show it:
;;   * the shapes: a triangle with its arm on the taken edge ($tri_t) and on
;;     the other ($tri_e), a diamond with three join parameters ($dia3), and
;;     both edges to the join with different arguments ($edges);
;;   * the types: i64 ($dia64), f32 and f64 on a float compare ($diaf32,
;;     $diaf64), v128 ($diav);
;;   * nesting: a diamond in each arm of a diamond ($nest4), and an `if` with
;;     a result inside another, whose inner join does more work before the
;;     outer one ($nestres), and one whose inner join only branches again on
;;     what it was passed ($joinbr), which is jump threading's to take apart
;;     rather than a select's;
;;   * a diamond carrying values round a loop ($loopd), and one whose
;;     condition is the value it carries (collatz);
;;   * br_table: eight cases on x >> 29, which cannot reach the default
;;     ($brt8, cbench/fnptr's shape); four cases on x & 3 and a default of its
;;     own the index cannot reach ($brt4d); the same on an index that can
;;     reach it ($brt4u); three cases on x >> 30, which can still reach the
;;     default with 3 ($brt3); two cases that are one edge on x & 1 ($brt1);
;;   * the arms that must stay branches, each driven with the input for which
;;     running it anyway would trap or be seen: a division by a variable
;;     ($sdiv), a signed one by -1 of INT_MIN ($sdivm1), an unsigned remainder
;;     by zero ($remz), a load past the end of memory ($ld, and $ldk at a
;;     constant address, ready early enough that only the rule against loads
;;     keeps it), a store ($st), a call with an effect ($callc), a global.set
;;     ($gset) and a trapping float truncation ($trunc) -- each on an export
;;     of its own, so that one trapping does not hide the next; at -O2 the
;;     inliner and global promotion turn $callc's and $gset's arms into
;;     arithmetic on the global's value, which may then be selected -- and
;;     the ones that may be speculated: division by a constant ($div7) and a
;;     saturating truncation ($truncsat).
;; The selects the back end makes a csinc, csneg or csinv of ($cs32, $cs64)
;; are here too: an arm that is x + 1, 0 - x or x ^ -1 of a value of its own
;; against any other value, either way round, and 1 or -1 against a register,
;; and one where the negated value is a shift the subtract could take in.
;; test/cases_traps/traps.wat has the same arms taken where they trap.
(module
  (memory 1)
  (global $seed (mut i32) (i32.const 0x2545f491))
  (global $zero (mut i32) (i32.const 0))
  (global $one (mut i32) (i32.const 1))
  (global $m1 (mut i32) (i32.const -1))
  (global $min (mut i32) (i32.const 0x80000000))
  (global $far (mut i32) (i32.const 0xfffffff0))
  (global $cnt (mut i32) (i32.const 0))
  (global $gv (mut i32) (i32.const 77))
  (global $fbig (mut f32) (f32.const 3e9))
  (global $fnan (mut f32) (f32.const nan))
  (global $fok (mut f32) (f32.const -1234.75))

  (func $lcg (param $x i32) (result i32)
    (i32.add (i32.mul (local.get $x) (i32.const 1664525)) (i32.const 1013904223)))

  ;; ---- shapes ----
  (func $tri_t (param $c i32) (param $x i32) (param $y i32) (result i32)
    (local $r i32)
    (local.set $r (local.get $x))
    (if (local.get $c)
      (then (local.set $r (i32.add (i32.shl (local.get $x) (i32.const 3)) (local.get $y)))))
    (local.get $r))
  (func $tri_e (param $c i32) (param $x i32) (param $y i32) (result i32)
    (local $r i32)
    (local.set $r (local.get $y))
    (if (i32.lt_u (local.get $x) (local.get $y))
      (then)
      (else (local.set $r (i32.xor (i32.sub (local.get $x) (local.get $y)) (local.get $c)))))
    (local.get $r))
  (func $dia3 (param $c i32) (param $x i32) (param $y i32) (result i32)
    (local $a i32) (local $b i32) (local $d i32)
    (local.set $d (local.get $x))
    (if (i32.gt_s (local.get $c) (local.get $y))
      (then
        (local.set $a (i32.add (local.get $x) (i32.const 7)))
        (local.set $b (i32.mul (local.get $y) (i32.const 3)))
        (local.set $d (i32.rotl (local.get $x) (local.get $y))))
      (else
        (local.set $a (i32.sub (local.get $x) (local.get $y)))
        (local.set $b (i32.xor (local.get $y) (i32.const 0x55)))))
    (i32.add (i32.add (i32.mul (local.get $a) (i32.const 31)) (local.get $b))
             (i32.shl (local.get $d) (i32.const 1))))
  (func $edges (param $c i32) (param $x i32) (param $y i32) (result i32)
    (block $j (result i32)
      (drop (br_if $j (local.get $x) (i32.and (local.get $c) (i32.const 4))))
      (local.get $y)))

  (func (export "shapes") (result i32)
    (local $i i32) (local $x i32) (local $y i32) (local $h i32)
    (local.set $x (global.get $seed))
    (loop $l
      (local.set $x (call $lcg (local.get $x)))
      (local.set $y (call $lcg (local.get $x)))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
        (call $tri_t (i32.and (local.get $x) (i32.const 0x100)) (local.get $x) (local.get $y))))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
        (call $tri_e (local.get $i) (local.get $x) (local.get $y))))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
        (call $dia3 (i32.shr_s (local.get $x) (i32.const 3)) (local.get $y) (local.get $i))))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
        (call $edges (local.get $y) (local.get $x) (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 64))))
    (local.get $h))

  ;; ---- types ----
  (func $dia64 (param $c i32) (param $x i64) (param $y i64) (result i64)
    (if (result i64) (local.get $c)
      (then (i64.add (i64.mul (local.get $x) (local.get $y)) (i64.const 1)))
      (else (i64.xor (i64.shr_u (local.get $x) (i64.const 3)) (local.get $y)))))
  (func $diaf32 (param $x f32) (param $y f32) (result f32)
    (if (result f32) (f32.lt (local.get $x) (local.get $y))
      (then (f32.add (f32.mul (local.get $x) (f32.const 2)) (local.get $y)))
      (else (f32.neg (f32.sub (local.get $y) (local.get $x))))))
  (func $diaf64 (param $x f64) (param $y f64) (result f64)
    (if (result f64) (f64.ge (local.get $x) (local.get $y))
      (then (f64.min (f64.abs (local.get $x)) (local.get $y)))
      (else (f64.max (f64.mul (local.get $x) (local.get $y)) (f64.const -0.5)))))
  (func $diav (param $c i32) (param $x i32) (param $y i32) (result i32)
    (local $v v128)
    (local.set $v
      (if (result v128) (i32.and (local.get $c) (i32.const 2))
        (then (i32x4.add (i32x4.splat (local.get $x)) (v128.const i32x4 1 2 3 4)))
        (else (i32x4.mul (i32x4.splat (local.get $y)) (v128.const i32x4 5 6 7 8)))))
    (i32.add (i32x4.extract_lane 0 (local.get $v))
             (i32.mul (i32x4.extract_lane 3 (local.get $v)) (i32.const 3))))

  (func (export "types") (result i32)
    (local $i i32) (local $x i32) (local $y i32) (local $h i32) (local $w i64)
    (local.set $x (global.get $seed))
    (loop $l
      (local.set $x (call $lcg (local.get $x)))
      (local.set $y (call $lcg (local.get $x)))
      (local.set $w (call $dia64 (i32.and (local.get $x) (i32.const 64))
        (i64.extend_i32_u (local.get $x)) (i64.extend_i32_s (local.get $y))))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
        (i32.xor (i32.wrap_i64 (local.get $w)) (i32.wrap_i64 (i64.shr_u (local.get $w) (i64.const 32))))))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
        (i32.reinterpret_f32 (call $diaf32
          (f32.convert_i32_s (i32.shr_s (local.get $x) (i32.const 20)))
          (f32.convert_i32_s (i32.shr_s (local.get $y) (i32.const 20)))))))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
        (i32.trunc_sat_f64_s (f64.mul (call $diaf64
          (f64.convert_i32_s (i32.shr_s (local.get $x) (i32.const 16)))
          (f64.convert_i32_s (i32.shr_s (local.get $y) (i32.const 16))))
          (f64.const 3.25)))))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
        (call $diav (local.get $y) (local.get $x) (local.get $y))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 64))))
    ;; a NaN fails both float compares, so the else arm is picked: which
    ;; side came back shows in whether it is a NaN and in its sign, never in
    ;; its payload, which the hardware may make canonical
    (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
      (i32.add
        (f32.ne (call $diaf32 (global.get $fnan) (f32.const 1)) (call $diaf32 (global.get $fnan) (f32.const 1)))
        (i32.shl (f64.lt (call $diaf64 (f64.promote_f32 (global.get $fnan)) (f64.const 2)) (f64.const 0))
                 (i32.const 1)))))
    (local.get $h))

  ;; ---- nesting ----
  (func $nest4 (param $a i32) (param $x i32) (result i32)
    (local $r i32)
    (if (i32.and (local.get $a) (i32.const 1))
      (then
        (if (i32.and (local.get $a) (i32.const 2))
          (then (local.set $r (i32.add (local.get $x) (i32.const 1))))
          (else (local.set $r (i32.mul (local.get $x) (i32.const 5))))))
      (else
        (if (i32.and (local.get $a) (i32.const 4))
          (then (local.set $r (i32.xor (local.get $x) (i32.const 9))))
          (else (local.set $r (i32.sub (local.get $x) (i32.const 3)))))))
    (local.get $r))
  (func $nestres (param $a i32) (param $x i32) (param $y i32) (result i32)
    (if (result i32) (i32.lt_s (local.get $a) (i32.const 0))
      (then
        (i32.add
          (if (result i32) (i32.and (local.get $a) (i32.const 8))
            (then (i32.add (local.get $x) (local.get $y)))
            (else (i32.sub (local.get $x) (local.get $y))))
          (i32.shl (local.get $y) (i32.const 2))))
      (else (i32.and (local.get $x) (i32.const 0xffff)))))

  ;; the inner join only branches again on what its two edges passed; the
  ;; outer branch's arm is a call, so it stays a branch
  (func $joinbr (param $c i32) (param $x i32) (result i32)
    (if (result i32)
      (if (result i32) (local.get $c)
        (then (i32.ge_s (local.get $x) (i32.const 0)))
        (else (i32.const 1)))
      (then (call $lcg (local.get $x)))
      (else (i32.const -7))))

  (func (export "nesting") (result i32)
    (local $i i32) (local $x i32) (local $y i32) (local $h i32)
    (local.set $x (global.get $seed))
    (loop $l
      (local.set $x (call $lcg (local.get $x)))
      (local.set $y (call $lcg (local.get $x)))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
        (call $nest4 (i32.shr_u (local.get $x) (i32.const 7)) (local.get $y))))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
        (call $nestres (local.get $x) (local.get $y) (local.get $i))))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
        (call $joinbr (i32.and (local.get $y) (i32.const 0x20)) (local.get $x))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 64))))
    (local.get $h))

  ;; ---- a diamond carrying two values round a loop ----
  (func (export "loopd") (result i32)
    (local $i i32) (local $x i32) (local $s i32) (local $t i32)
    (local.set $x (global.get $seed))
    (local.set $t (global.get $one))
    (loop $l
      (local.set $x (call $lcg (local.get $x)))
      (if (i32.and (local.get $x) (i32.const 0x10000))
        (then
          (local.set $s (i32.add (local.get $s) (i32.mul (local.get $x) (i32.const 3))))
          (local.set $t (i32.add (local.get $t) (i32.const 1))))
        (else
          (local.set $s (i32.xor (local.get $s) (i32.shr_u (local.get $x) (i32.const 2))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 1000))))
    (i32.add (local.get $s) (i32.mul (local.get $t) (i32.const 1000003))))

  ;; ---- a diamond whose condition is what it hands round the loop ----
  ;; Collatz: n even ? n / 2 : 3n + 1, the parity test -- a signed
  ;; remainder, as GHC writes it, four cycles where the arms take three --
  ;; on the n the last step chose, which keeps its branch
  (func (export "collatz") (result i32)
    (local $m i32) (local $n i32) (local $steps i32)
    (local.set $m (global.get $one))
    (loop $each
      (local.set $n (local.get $m))
      (block $done (loop $step
        (br_if $done (i32.le_u (local.get $n) (i32.const 1)))
        (if (i32.rem_s (local.get $n) (i32.const 2))
          (then (local.set $n (i32.add (i32.mul (local.get $n) (i32.const 3)) (i32.const 1))))
          (else (local.set $n (i32.shr_u (local.get $n) (i32.const 1)))))
        (local.set $steps (i32.add (local.get $steps) (i32.const 1)))
        (br $step)))
      (local.set $m (i32.add (local.get $m) (i32.const 1)))
      (br_if $each (i32.lt_u (local.get $m) (i32.const 300))))
    (local.get $steps))

  ;; ---- br_table ----
  (func $brt8 (param $s i32) (param $x i32) (result i32)
    (block $j (result i32)
      (block $c7 (block $c6 (block $c5 (block $c4
      (block $c3 (block $c2 (block $c1 (block $c0
        (br_table $c0 $c1 $c2 $c3 $c4 $c5 $c6 $c7 $c0
          (i32.shr_u (local.get $x) (i32.const 29))))
        (br $j (i32.add (local.get $s) (local.get $x))))
        (br $j (i32.mul (local.get $s) (i32.or (local.get $x) (i32.const 1)))))
        (br $j (i32.xor (local.get $s) (i32.shl (local.get $x) (i32.const 3)))))
        (br $j (i32.rotl (local.get $s) (local.get $x))))
        (br $j (i32.sub (local.get $s) (i32.shr_u (local.get $x) (i32.const 1)))))
        (br $j (i32.add (i32.xor (local.get $s) (i32.shr_u (local.get $s) (i32.const 13)))
                        (i32.mul (local.get $x) (i32.const 0x9e3779b9)))))
        (br $j (select (i32.add (local.get $s) (i32.const 1)) (i32.sub (local.get $s) (local.get $x))
                       (i32.lt_u (local.get $s) (local.get $x)))))
      (i32.add (i32.xor (local.get $s) (i32.const -1)) (local.get $x))))
  ;; x & 3 cannot reach the default, which is an edge of its own
  (func $brt4d (param $s i32) (param $x i32) (result i32)
    (block $j (result i32)
      (block $d (block $c3 (block $c2 (block $c1 (block $c0
        (br_table $c0 $c1 $c2 $c3 $d (i32.and (local.get $x) (i32.const 3))))
        (br $j (i32.add (local.get $s) (i32.const 11))))
        (br $j (i32.xor (local.get $s) (local.get $x))))
        (br $j (i32.sub (local.get $x) (local.get $s))))
        (br $j (i32.shl (local.get $s) (i32.const 1))))
      (i32.const 0xdead)))
  ;; the same cases on an index that does reach the default
  (func $brt4u (param $s i32) (param $x i32) (result i32)
    (block $j (result i32)
      (block $d (block $c3 (block $c2 (block $c1 (block $c0
        (br_table $c0 $c1 $c2 $c3 $d (local.get $x)))
        (br $j (i32.add (local.get $s) (i32.const 11))))
        (br $j (i32.xor (local.get $s) (local.get $x))))
        (br $j (i32.sub (local.get $x) (local.get $s))))
        (br $j (i32.shl (local.get $s) (i32.const 1))))
      (i32.add (local.get $s) (i32.const 0xdead))))
  ;; x >> 30 is at most 3, which three cases do not cover
  (func $brt3 (param $s i32) (param $x i32) (result i32)
    (block $j (result i32)
      (block $d (block $c2 (block $c1 (block $c0
        (br_table $c0 $c1 $c2 $d (i32.shr_u (local.get $x) (i32.const 30))))
        (br $j (i32.add (local.get $s) (i32.const 5))))
        (br $j (i32.mul (local.get $s) (i32.const 7))))
        (br $j (i32.xor (local.get $s) (i32.const 0x77))))
      (i32.sub (local.get $s) (i32.const 0x1000))))

  ;; x & 1 picks one of two cases, both the same edge: the default is left
  ;; with nowhere the index reaches, and the table is one jump
  (func $brt1 (param $s i32) (param $x i32) (result i32)
    (block $j (result i32)
      (block $d (block $c
        (br_table $c $c $d (i32.and (local.get $x) (i32.const 1))))
        (br $j (i32.add (local.get $s) (local.get $x))))
      (i32.const 0xbad)))

  (func (export "brtable") (result i32)
    (local $i i32) (local $x i32) (local $s i32) (local $h i32)
    (local.set $x (global.get $seed))
    (local.set $s (global.get $one))
    (loop $l
      (local.set $x (call $lcg (local.get $x)))
      (local.set $s (call $brt8 (local.get $s) (local.get $x)))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
        (call $brt4d (local.get $s) (local.get $x))))
      ;; 0..5, then the top of the range
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
        (call $brt4u (local.get $s)
          (select (i32.rem_u (local.get $i) (i32.const 6))
                  (i32.sub (global.get $zero) (i32.and (local.get $i) (i32.const 3)))
                  (i32.lt_u (local.get $i) (i32.const 500))))))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
        (call $brt3 (local.get $s) (local.get $x))))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
        (call $brt1 (local.get $h) (local.get $x))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 1000))))
    (i32.add (local.get $h) (local.get $s)))
  (func (export "brtable_default") (result i32)
    (i32.add
      (call $brt4u (global.get $one) (global.get $m1))
      (i32.mul (call $brt4u (global.get $one) (global.get $min)) (i32.const 3))))

  ;; ---- arms that must stay branches ----
  (func $sdiv (param $c i32) (param $x i32) (param $d i32) (result i32)
    (if (result i32) (local.get $c)
      (then (i32.div_s (local.get $x) (local.get $d)))
      (else (i32.add (local.get $x) (i32.const 1)))))
  (func $sdivm1 (param $c i32) (param $x i32) (result i32)
    (if (result i32) (local.get $c)
      (then (i32.div_s (local.get $x) (i32.const -1)))
      (else (local.get $x))))
  (func $remz (param $c i32) (param $x i32) (result i32)
    (if (result i32) (local.get $c)
      (then (i32.rem_u (local.get $x) (i32.const 0)))
      (else (i32.sub (local.get $x) (i32.const 2)))))
  (func $ld (param $c i32) (param $p i32) (result i32)
    (if (result i32) (local.get $c)
      (then (i32.load (local.get $p)))
      (else (i32.const -1))))
  ;; past the end at a constant address, which is ready as soon as the
  ;; select would want it
  (func $ldk (param $c i32) (result i32)
    (if (result i32) (local.get $c)
      (then (i32.load offset=16 (i32.const 65528)))
      (else (i32.const -1))))
  (func $st (param $c i32) (param $p i32) (param $v i32) (result i32)
    (if (local.get $c) (then (i32.store (local.get $p) (local.get $v))))
    (i32.load (local.get $p)))
  (func $bump (param $x i32) (result i32)
    (global.set $cnt (i32.add (global.get $cnt) (i32.const 1)))
    (i32.add (local.get $x) (i32.const 1)))
  (func $callc (param $c i32) (param $x i32) (result i32)
    (if (result i32) (local.get $c)
      (then (call $bump (local.get $x)))
      (else (local.get $x))))
  (func $gset (param $c i32) (param $v i32) (result i32)
    (if (local.get $c) (then (global.set $gv (local.get $v))))
    (global.get $gv))
  (func $trunc (param $c i32) (param $f f32) (result i32)
    (if (result i32) (local.get $c)
      (then (i32.trunc_f32_s (local.get $f)))
      (else (i32.const 0))))
  (func $div7 (param $c i32) (param $x i32) (result i32)
    (if (result i32) (local.get $c)
      (then (i32.div_s (local.get $x) (i32.const 7)))
      (else (i32.rem_u (local.get $x) (i32.const 10)))))
  (func $truncsat (param $c i32) (param $f f32) (result i32)
    (if (result i32) (local.get $c)
      (then (i32.trunc_sat_f32_s (local.get $f)))
      (else (i32.const 3))))

  ;; ---- csinc, csneg, csinv ----
  (func $cs32 (param $c i32) (param $x i32) (param $y i32) (result i32)
    (local $h i32) (local $z i32) (local $w i32)
    ;; each arm a value of its own, so that no two of them are one
    (local.set $z (i32.xor (local.get $x) (local.get $y)))
    (local.set $w (i32.rotl (local.get $x) (local.get $y)))
    (local.set $h (select (i32.add (local.get $x) (i32.const 1)) (local.get $y) (local.get $c)))
    (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
      (select (local.get $x) (i32.add (local.get $y) (i32.const 1))
              (i32.lt_s (local.get $x) (local.get $y)))))
    (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
      (select (i32.sub (i32.const 0) (local.get $z)) (local.get $z)
              (i32.lt_s (local.get $z) (i32.const 0)))))
    (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
      (select (local.get $y) (i32.sub (i32.const 0) (local.get $w)) (local.get $c))))
    (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
      (select (i32.sub (i32.const 0) (i32.shl (local.get $x) (i32.const 3))) (local.get $y)
              (i32.ge_u (local.get $x) (local.get $y)))))
    (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
      (select (i32.xor (local.get $z) (i32.const -1)) (local.get $x)
              (i32.eq (i32.and (local.get $x) (i32.const 3)) (i32.const 1)))))
    (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
      (select (local.get $z) (i32.xor (local.get $w) (i32.const -1)) (local.get $c))))
    (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
      (select (i32.const 1) (local.get $y) (local.get $c))))
    (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
      (select (local.get $z) (i32.const 1) (i32.gt_u (local.get $x) (local.get $y)))))
    (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
      (select (i32.const -1) (local.get $x) (i32.le_s (local.get $x) (local.get $y)))))
    (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
      (select (local.get $w) (i32.const -1) (local.get $c))))
    ;; the absolute value written as a branch
    (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
      (if (result i32) (i32.lt_s (local.get $y) (i32.const 0))
        (then (i32.sub (i32.const 0) (local.get $y)))
        (else (local.get $y)))))
    (local.get $h))
  (func $cs64 (param $c i32) (param $x i64) (param $y i64) (result i64)
    (local $h i64)
    (local.set $h (select (i64.add (local.get $x) (i64.const 1)) (local.get $y) (local.get $c)))
    (local.set $h (i64.add (i64.mul (local.get $h) (i64.const 31))
      (select (local.get $y) (i64.sub (i64.const 0) (local.get $x))
              (i64.lt_s (local.get $x) (local.get $y)))))
    (local.set $h (i64.add (i64.mul (local.get $h) (i64.const 31))
      (select (i64.xor (local.get $x) (i64.const -1)) (local.get $y) (local.get $c))))
    (local.set $h (i64.add (i64.mul (local.get $h) (i64.const 31))
      (select (local.get $y) (i64.const 1) (local.get $c))))
    (local.set $h (i64.add (i64.mul (local.get $h) (i64.const 31))
      (select (i64.const -1) (local.get $y) (i64.gt_u (local.get $x) (local.get $y)))))
    (local.get $h))

  (func (export "csel") (result i32)
    (local $i i32) (local $x i32) (local $y i32) (local $h i32) (local $w i64)
    (local.set $x (global.get $seed))
    (loop $l
      (local.set $x (call $lcg (local.get $x)))
      (local.set $y (call $lcg (local.get $x)))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
        (call $cs32 (i32.and (local.get $x) (i32.const 0x400)) (local.get $x) (local.get $y))))
      (local.set $w (call $cs64 (i32.and (local.get $y) (i32.const 0x400))
        (i64.shr_s (i64.mul (i64.extend_i32_s (local.get $x)) (i64.const 0x10001)) (i64.const 3))
        (i64.extend_i32_u (local.get $y))))
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
        (i32.xor (i32.wrap_i64 (local.get $w)) (i32.wrap_i64 (i64.shr_u (local.get $w) (i64.const 32))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 64))))
    (local.get $h))

  ;; Each kind on its own export, so that one that traps does not hide the
  ;; next: every arm below is left untaken for the input that would trap.
  (func (export "unsafe_div") (result i32)
    (i32.add
      (i32.mul (call $sdiv (global.get $zero) (global.get $seed) (global.get $zero)) (i32.const 31))
      (call $sdiv (global.get $one) (global.get $seed) (i32.add (global.get $one) (i32.const 6)))))
  (func (export "unsafe_div_ovf") (result i32)
    (i32.add
      (i32.mul (call $sdiv (global.get $zero) (global.get $min) (global.get $m1)) (i32.const 31))
      (call $sdivm1 (global.get $zero) (global.get $min))))
  (func (export "unsafe_rem0") (result i32)
    (call $remz (global.get $zero) (global.get $seed)))
  (func (export "unsafe_load") (result i32)
    (i32.add (call $ld (global.get $zero) (global.get $far))
             (call $ldk (global.get $zero))))
  (func (export "unsafe_store") (result i32)
    (i32.add
      (i32.mul (call $st (global.get $zero) (i32.const 64) (global.get $seed)) (i32.const 31))
      (call $st (global.get $one) (i32.const 68) (global.get $seed))))
  (func (export "unsafe_call") (result i32)
    (i32.add
      (i32.mul (i32.add (call $callc (global.get $zero) (global.get $seed))
                        (call $callc (global.get $one) (global.get $seed)))
               (i32.const 31))
      (global.get $cnt)))
  (func (export "unsafe_gset") (result i32)
    (i32.add
      (i32.mul (call $gset (global.get $zero) (global.get $seed)) (i32.const 31))
      (call $gset (global.get $one) (i32.const 99))))
  (func (export "unsafe_trunc") (result i32)
    (i32.add
      (i32.add
        (i32.mul (call $trunc (global.get $zero) (global.get $fnan)) (i32.const 31))
        (i32.mul (call $trunc (global.get $zero) (global.get $fbig)) (i32.const 7)))
      (call $trunc (global.get $one) (global.get $fok))))
  (func (export "safe_div7") (result i32)
    (i32.add
      (i32.mul (call $div7 (global.get $zero) (global.get $min)) (i32.const 31))
      (call $div7 (global.get $one) (global.get $min))))
  (func (export "safe_truncsat") (result i32)
    (i32.add
      (i32.mul (call $truncsat (global.get $one) (global.get $fnan)) (i32.const 31))
      (call $truncsat (global.get $one) (global.get $fbig))))
)
