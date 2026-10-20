;; The relaxed vector instructions, checking their own answers.
;;
;; Their own harness for the same reason the GC cases have one: there is no
;; oracle.  A relaxed instruction is *allowed* several answers, so comparing
;; against another engine proves nothing -- the two can differ and both be
;; right.  What can be asserted is the answer warmc actually owes, which is:
;;
;;   full profile           whatever this back end chose, written down here so
;;                          that changing it is a decision rather than a drift
;;   deterministic profile  the FIRST alternative the spec lists, which the
;;                          spec pins by prescribing every relaxed parameter
;;                          to 0 (exec/numerics.rst, `relaxed(R)[A0..An]`)
;;
;; On AArch64 those are the same choice for eighteen of the twenty, because
;; the natural instruction already computes the first alternative.  Only
;; relaxed_madd and relaxed_nmadd differ, and they have an export per profile.
;;
;; What the upstream corpus does *not* reach, and this exists for:
;;   - i32x4_relaxed_trunc.wast has no assertions at all -- it declares four
;;     exports and stops -- so nothing upstream checks the trunc family;
;;   - every other interesting upstream assertion is an `(either ...)`, which
;;     admits the alternatives this file picks between.
;;
;; Inputs arrive through mutable globals rather than as literals so that no
;; amount of folding can answer the question before instruction selection
;; sees it.

(module
  ;; ---- the two checks -------------------------------------------------
  ;; A bytewise compare, so a NaN's payload is part of the answer.
  (func $chk (param $got v128) (param $want v128)
    (if (i32.eqz (i8x16.all_true (i8x16.eq (local.get $got) (local.get $want))))
      (then unreachable)))
  ;; Every lane is some NaN.  Used where the *class* of the answer is what the
  ;; rule says and the payload is the hardware's business.
  (func $chk_nan32 (param $got v128)
    (if (i32.eqz (i32x4.all_true (f32x4.ne (local.get $got) (local.get $got))))
      (then unreachable)))
  (func $chk_nan64 (param $got v128)
    (if (i32.eqz (i64x2.all_true (f64x2.ne (local.get $got) (local.get $got))))
      (then unreachable)))

  ;; ---- opaque inputs --------------------------------------------------
  (global $a (mut v128) (v128.const i64x2 0 0))
  (global $b (mut v128) (v128.const i64x2 0 0))
  (global $c (mut v128) (v128.const i64x2 0 0))

  ;; ---- 0x100  i8x16.relaxed_swizzle ------------------------------------
  ;; A0 zeroes an out-of-range index, which is i8x16.swizzle exactly.  The
  ;; other alternative returns a[j mod 16] for 16 <= j < 128, so the second
  ;; and third cases are what tell the two apart.
  (func (export "swizzle")
    (global.set $a (v128.const i8x16 10 11 12 13 14 15 16 17
                                     18 19 20 21 22 23 24 25))
    (global.set $b (v128.const i8x16 15 14 13 12 11 10 9 8
                                     7 6 5 4 3 2 1 0))
    (call $chk (i8x16.relaxed_swizzle (global.get $a) (global.get $b))
               (v128.const i8x16 25 24 23 22 21 20 19 18
                                 17 16 15 14 13 12 11 10))
    ;; 16..31: in range for the modulo reading, zero for this one
    (global.set $b (v128.const i8x16 16 17 18 19 20 21 22 23
                                     24 25 26 27 28 29 30 31))
    (call $chk (i8x16.relaxed_swizzle (global.get $a) (global.get $b))
               (v128.const i64x2 0 0))
    ;; >= 128: zero under every reading
    (global.set $b (v128.const i8x16 128 129 130 131 132 133 134 135
                                     248 249 250 251 252 253 254 255))
    (call $chk (i8x16.relaxed_swizzle (global.get $a) (global.get $b))
               (v128.const i64x2 0 0)))

  ;; ---- 0x101..0x104  the relaxed_trunc family --------------------------
  ;; A0 is the saturating conversion: a NaN lane is 0 and an out-of-range one
  ;; saturates.  Nothing upstream checks any of this.
  (func (export "trunc")
    (global.set $a (v128.const f32x4 1.5 -1.5 nan 1e10))
    (call $chk (i32x4.relaxed_trunc_f32x4_s (global.get $a))
               (v128.const i32x4 1 -1 0 2147483647))
    (call $chk (i32x4.relaxed_trunc_f32x4_u (global.get $a))
               (v128.const i32x4 1 0 0 4294967295))
    (global.set $a (v128.const f32x4 -1e10 -1e10 -1e10 -1e10))
    (call $chk (i32x4.relaxed_trunc_f32x4_s (global.get $a))
               (v128.const i32x4 -2147483648 -2147483648
                                 -2147483648 -2147483648))
    (call $chk (i32x4.relaxed_trunc_f32x4_u (global.get $a))
               (v128.const i32x4 0 0 0 0))
    ;; the f64 forms, whose top two lanes are zero rather than converted
    (global.set $a (v128.const f64x2 1.9 -1e30))
    (call $chk (i32x4.relaxed_trunc_f64x2_s_zero (global.get $a))
               (v128.const i32x4 1 -2147483648 0 0))
    (global.set $a (v128.const f64x2 1.9 -1.0))
    (call $chk (i32x4.relaxed_trunc_f64x2_u_zero (global.get $a))
               (v128.const i32x4 1 0 0 0))
    (global.set $a (v128.const f64x2 nan 1e30))
    (call $chk (i32x4.relaxed_trunc_f64x2_s_zero (global.get $a))
               (v128.const i32x4 0 2147483647 0 0)))

  ;; ---- 0x109..0x10c  the relaxed_laneselect family ---------------------
  ;; A0 is a plain bitselect.  Every mask below has a lane whose bits are
  ;; *mixed*, which is the only thing that tells a bitselect from a select on
  ;; the lane's top bit -- a mask of all-ones or all-zeros agrees either way.
  (func (export "laneselect")
    (global.set $a (v128.const i8x16 0 1 0x12 0x12 4 5 6 7
                                     8 9 10 11 12 13 14 15))
    (global.set $b (v128.const i8x16 16 17 0x34 0x34 20 21 22 23
                                     24 25 26 27 28 29 30 31))
    (global.set $c (v128.const i8x16 0xff 0 0xf0 0x0f 0 0 0 0
                                     0 0 0 0 0 0 0 0))
    (call $chk (i8x16.relaxed_laneselect
                 (global.get $a) (global.get $b) (global.get $c))
               (v128.const i8x16 0 17 0x14 0x32 20 21 22 23
                                 24 25 26 27 28 29 30 31))
    (global.set $a (v128.const i16x8 0x1234 0x1234 0x1234 0x1234
                                     0x1234 0x1234 0x1234 0x1234))
    (global.set $b (v128.const i16x8 0x5678 0x5678 0x5678 0x5678
                                     0x5678 0x5678 0x5678 0x5678))
    (global.set $c (v128.const i16x8 0xff00 0xff00 0xff00 0xff00
                                     0xff00 0xff00 0xff00 0xff00))
    (call $chk (i16x8.relaxed_laneselect
                 (global.get $a) (global.get $b) (global.get $c))
               (v128.const i16x8 0x1278 0x1278 0x1278 0x1278
                                 0x1278 0x1278 0x1278 0x1278))
    (global.set $a (v128.const i32x4 0x12341234 0x12341234
                                     0x12341234 0x12341234))
    (global.set $b (v128.const i32x4 0x56785678 0x56785678
                                     0x56785678 0x56785678))
    (global.set $c (v128.const i32x4 0xffff0000 0xffff0000
                                     0xffff0000 0xffff0000))
    (call $chk (i32x4.relaxed_laneselect
                 (global.get $a) (global.get $b) (global.get $c))
               (v128.const i32x4 0x12345678 0x12345678
                                 0x12345678 0x12345678))
    (global.set $a (v128.const i64x2 0x1234123412341234 0x1234123412341234))
    (global.set $b (v128.const i64x2 0x5678567856785678 0x5678567856785678))
    (global.set $c (v128.const i64x2 0xffffffff00000000 0xffffffff00000000))
    (call $chk (i64x2.relaxed_laneselect
                 (global.get $a) (global.get $b) (global.get $c))
               (v128.const i64x2 0x1234123456785678 0x1234123456785678)))

  ;; ---- 0x10d..0x110  relaxed_min and relaxed_max -----------------------
  ;; A0 is wasm's own min/max, and the two rules that distinguishes it by are
  ;; that a NaN operand *propagates* (rather than the other operand winning,
  ;; which is what FMINNM would do) and that min(+0,-0) is -0.
  (func (export "minmax")
    (global.set $a (v128.const f32x4 1 4 -0.0 0.0))
    (global.set $b (v128.const f32x4 4 1 0.0 -0.0))
    (call $chk (f32x4.relaxed_min (global.get $a) (global.get $b))
               (v128.const f32x4 1 1 -0.0 -0.0))
    (call $chk (f32x4.relaxed_max (global.get $a) (global.get $b))
               (v128.const f32x4 4 4 0.0 0.0))
    (global.set $a (v128.const f64x2 -0.0 2))
    (global.set $b (v128.const f64x2 0.0 7))
    (call $chk (f64x2.relaxed_min (global.get $a) (global.get $b))
               (v128.const f64x2 -0.0 2))
    (call $chk (f64x2.relaxed_max (global.get $a) (global.get $b))
               (v128.const f64x2 0.0 7))
    ;; a NaN on either side leaves a NaN, whichever payload the hardware picks
    (global.set $a (v128.const f32x4 nan nan 1 1))
    (global.set $b (v128.const f32x4 1 1 nan nan))
    (call $chk_nan32 (f32x4.relaxed_min (global.get $a) (global.get $b)))
    (call $chk_nan32 (f32x4.relaxed_max (global.get $a) (global.get $b)))
    (global.set $a (v128.const f64x2 nan 1))
    (global.set $b (v128.const f64x2 1 nan))
    (call $chk_nan64 (f64x2.relaxed_min (global.get $a) (global.get $b)))
    (call $chk_nan64 (f64x2.relaxed_max (global.get $a) (global.get $b))))

  ;; ---- 0x111  i16x8.relaxed_q15mulr_s ----------------------------------
  ;; A0 saturates INT16_MIN squared to INT16_MAX; the other alternative
  ;; answers INT16_MIN.  Lane 0 is that case, and the rest pin the ordinary
  ;; rounding arithmetic around it.
  (func (export "q15mulr")
    (global.set $a (v128.const i16x8 -32768 16384 -32768 32767 0 1 -1 100))
    (global.set $b (v128.const i16x8 -32768 16384 32767 32767 5 1 -1 200))
    ;; lane 0 is the relaxed one: (a*b + 2^14) >> 15 is 32768, one past
    ;; INT16_MAX, and A0 saturates where the alternative wraps to INT16_MIN.
    ;; The rest are ordinary Q15 rounding -- lane 2 lands exactly on -32766.5
    ;; and the shift floors, which is -32767 and not -32768.
    (call $chk (i16x8.relaxed_q15mulr_s (global.get $a) (global.get $b))
               (v128.const i16x8 32767 8192 -32767 32766 0 0 0 1)))

  ;; ---- 0x112/0x113  the relaxed dot products ---------------------------
  ;; One relaxed choice decides two things together ($R_idot in the spec):
  ;; choice 0 reads both sides of the multiply SIGNED and adds adjacent
  ;; products with WRAP-AROUND; choice 1 reads the second operand unsigned
  ;; and saturates the sum.  warmc takes choice 0 in both profiles, since it
  ;; is the deterministic one.
  ;;   - the -128 x -127 case separates the readings of the multiply --
  ;;     signed gives +32512, unsigned-second gives -32768;
  ;;   - the -128 x -128 case is the one whose sum overflows i16: it wraps to
  ;;     -32768.  (Signed AND saturated, 32767, is neither choice: the spec
  ;;     used to read it that way, and the upstream corpus now rejects it.)
  (func (export "dot")
    (global.set $a (v128.const i8x16 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15))
    (global.set $b (v128.const i8x16 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15))
    (call $chk (i16x8.relaxed_dot_i8x16_i7x16_s
                 (global.get $a) (global.get $b))
               (v128.const i16x8 1 13 41 85 145 221 313 421))
    ;; signed x signed: (-128 * -127) * 2 = 32512, which does not saturate
    (global.set $a (v128.const i8x16 -128 -128 0 0 0 0 0 0
                                     0 0 0 0 0 0 0 0))
    (global.set $b (v128.const i8x16 -127 -127 0 0 0 0 0 0
                                     0 0 0 0 0 0 0 0))
    (call $chk (i16x8.relaxed_dot_i8x16_i7x16_s
                 (global.get $a) (global.get $b))
               (v128.const i16x8 32512 0 0 0 0 0 0 0))
    ;; (-128 * -128) * 2 = 32768, one past INT16_MAX, so the sum wraps
    (global.set $b (v128.const i8x16 -128 -128 0 0 0 0 0 0
                                     0 0 0 0 0 0 0 0))
    (call $chk (i16x8.relaxed_dot_i8x16_i7x16_s
                 (global.get $a) (global.get $b))
               (v128.const i16x8 -32768 0 0 0 0 0 0 0)))

  (func (export "dot_add")
    (global.set $a (v128.const i8x16 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15))
    (global.set $b (v128.const i8x16 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15))
    (global.set $c (v128.const i32x4 0 1 2 3))
    ;; the i16 stage is [1,13,41,85,145,221,313,421]; pairs of those widen and
    ;; add to [14,126,366,734], and then the third operand goes on
    (call $chk (i32x4.relaxed_dot_i8x16_i7x16_add_s
                 (global.get $a) (global.get $b) (global.get $c))
               (v128.const i32x4 14 127 368 737))
    ;; signed x signed, four lanes of (-128 * -127) = 65024, and the widening
    ;; add does NOT saturate -- 65024 is well past INT16_MAX
    (global.set $a (v128.const i8x16 -128 -128 -128 -128 0 0 0 0
                                     0 0 0 0 0 0 0 0))
    (global.set $b (v128.const i8x16 -127 -127 -127 -127 0 0 0 0
                                     0 0 0 0 0 0 0 0))
    (global.set $c (v128.const i32x4 1 2 3 4))
    (call $chk (i32x4.relaxed_dot_i8x16_i7x16_add_s
                 (global.get $a) (global.get $b) (global.get $c))
               (v128.const i32x4 65025 2 3 4))
    ;; A NEGATIVE intermediate, which is the only thing that says the widening
    ;; from i16 to i32 is signed.  Every case above leaves the i16 stage
    ;; positive, where a signed and an unsigned widening agree -- ablating
    ;; `saddlp` to `uaddlp` was caught by the upstream corpus and *not* by
    ;; this file until this case existed.
    ;;   -128 * 127 = -16256; two of those is -32512, which does not saturate;
    ;;   two of *those* widen and add to -65024, and then c goes on.
    ;; Read unsigned instead, -32512 is 33024 and the answer is +66049.
    (global.set $b (v128.const i8x16 127 127 127 127 0 0 0 0
                                     0 0 0 0 0 0 0 0))
    (call $chk (i32x4.relaxed_dot_i8x16_i7x16_add_s
                 (global.get $a) (global.get $b) (global.get $c))
               (v128.const i32x4 -65023 2 3 4))
    ;; The i32x4 form is the i16x8 one widened, so it inherits the wrap: each
    ;; pair of -128 * -128 is 32768, which wraps to -32768 in the i16 stage;
    ;; two of those widen and add to -65536, and c goes on.  Summing the four
    ;; products exactly instead would give +65537.
    (global.set $b (v128.const i8x16 -128 -128 -128 -128 0 0 0 0
                                     0 0 0 0 0 0 0 0))
    (call $chk (i32x4.relaxed_dot_i8x16_i7x16_add_s
                 (global.get $a) (global.get $b) (global.get $c))
               (v128.const i32x4 -65535 2 3 4)))

  ;; ---- 0x105..0x108  relaxed_madd and relaxed_nmadd --------------------
  ;;
  ;; The two whose answer the profile decides, and the witness is the classic
  ;; one: FLT_MAX * 2 - FLT_MAX.
  ;;
  ;;   fused (one rounding)    the intermediate 2*FLT_MAX is never rounded, so
  ;;                           the answer is FLT_MAX
  ;;   unfused (two roundings) 2*FLT_MAX overflows to +inf first, and
  ;;                           inf - FLT_MAX is inf
  ;;
  ;; A0 -- and so the deterministic profile -- is the *unfused* one.  That is
  ;; worth stating twice: the deterministic profile forbids the fma.
  ;;
  ;; nmadd negates its first operand and is otherwise madd, so feeding it
  ;; -FLT_MAX asks the identical question.
  (func $madd_common (param $want_f32 v128) (param $want_f64 v128)
    (global.set $a (v128.const f32x4 0x1.fffffep+127 0x1.fffffep+127
                                     0x1.fffffep+127 0x1.fffffep+127))
    (global.set $b (v128.const f32x4 2 2 2 2))
    (global.set $c (v128.const f32x4 -0x1.fffffep+127 -0x1.fffffep+127
                                     -0x1.fffffep+127 -0x1.fffffep+127))
    (call $chk (f32x4.relaxed_madd
                 (global.get $a) (global.get $b) (global.get $c))
               (local.get $want_f32))
    ;; nmadd with the first operand negated asks the same question
    (global.set $a (v128.const f32x4 -0x1.fffffep+127 -0x1.fffffep+127
                                     -0x1.fffffep+127 -0x1.fffffep+127))
    (call $chk (f32x4.relaxed_nmadd
                 (global.get $a) (global.get $b) (global.get $c))
               (local.get $want_f32))
    (global.set $a (v128.const f64x2 0x1.fffffffffffffp+1023
                                     0x1.fffffffffffffp+1023))
    (global.set $b (v128.const f64x2 2 2))
    (global.set $c (v128.const f64x2 -0x1.fffffffffffffp+1023
                                     -0x1.fffffffffffffp+1023))
    (call $chk (f64x2.relaxed_madd
                 (global.get $a) (global.get $b) (global.get $c))
               (local.get $want_f64))
    (global.set $a (v128.const f64x2 -0x1.fffffffffffffp+1023
                                     -0x1.fffffffffffffp+1023))
    (call $chk (f64x2.relaxed_nmadd
                 (global.get $a) (global.get $b) (global.get $c))
               (local.get $want_f64)))

  ;; The full profile takes the fused form, which is the whole reason the
  ;; instruction exists.
  (func (export "madd_fused")
    (call $madd_common
      (v128.const f32x4 0x1.fffffep+127 0x1.fffffep+127
                        0x1.fffffep+127 0x1.fffffep+127)
      (v128.const f64x2 0x1.fffffffffffffp+1023 0x1.fffffffffffffp+1023)))

  ;; The deterministic profile takes A0, which rounds twice.
  (func (export "madd_unfused")
    (call $madd_common
      (v128.const f32x4 inf inf inf inf)
      (v128.const f64x2 inf inf)))

  ;; Which operand nmadd negates, which no profile changes and which the
  ;; witness above cannot see -- it feeds the same magnitude to both forms on
  ;; purpose.  -3*5 + 2 = -13 separates negating the first operand from
  ;; negating the third (+13), from negating the result (-17), and from not
  ;; negating at all (+17).
  (func (export "nmadd_operand")
    (global.set $a (v128.const f32x4 3 3 -0.0 -0.0))
    (global.set $b (v128.const f32x4 5 5 1 1))
    (global.set $c (v128.const f32x4 2 2 -0.0 -0.0))
    (call $chk (f32x4.relaxed_nmadd
                 (global.get $a) (global.get $b) (global.get $c))
               (v128.const f32x4 -13 -13 0.0 0.0)))

  ;; ---- the deterministic profile's other half: NaN canonicalisation ----
  ;;
  ;; The profile has two rules.  The relaxed instructions above are one; the
  ;; other is that every NaN a floating-point instruction *generates* must be
  ;; the positive canonical NaN.  warmc implements it by setting FPCR.DN at
  ;; instantiation, so it costs nothing per operation and covers the scalar
  ;; and vector register files together.
  (global $f (mut f32) (f32.const 0))
  (global $d (mut f64) (f64.const 0))

  (func $chk32 (param $got i32) (param $want i32)
    (if (i32.ne (local.get $got) (local.get $want)) (then unreachable)))
  (func $chk64 (param $got i64) (param $want i64)
    (if (i64.ne (local.get $got) (local.get $want)) (then unreachable)))

  ;; With no NaN operand, an invalid operation already produces a canonical
  ;; NaN on this hardware, so both profiles agree on the payload here.  This
  ;; is the case that says the *other* results below are about propagation
  ;; and not about NaN production in general.
  ;;
  ;; The sign is masked off because the spec leaves it open -- a canonical
  ;; NaN is +-nan(canonN) -- and the two hardwares disagree: AArch64's
  ;; default NaN is positive, x86-64's "floating-point indefinite" is
  ;; negative.  Everything below still pins the payload exactly.
  (func (export "nan_invalid")
    (global.set $f (f32.const inf))
    (call $chk32 (i32.and
                   (i32.reinterpret_f32
                     (f32.sub (global.get $f) (f32.const inf)))
                   (i32.const 0x7fffffff))
                 (i32.const 0x7fc00000))
    (global.set $d (f64.const inf))
    (call $chk64 (i64.and
                   (i64.reinterpret_f64
                     (f64.sub (global.get $d) (f64.const inf)))
                   (i64.const 0x7fffffffffffffff))
                 (i64.const 0x7ff8000000000000)))

  ;; fneg, fabs and fcopysign are excluded from the rule by name, and FPCR.DN
  ;; does not reach them because they touch the sign bit only.  Both profiles
  ;; must leave the payload alone.
  (func (export "nan_excluded")
    (global.set $f (f32.const nan:0x200000))
    (call $chk32 (i32.reinterpret_f32 (f32.neg (global.get $f)))
                 (i32.const 0xffa00000))
    (call $chk32 (i32.reinterpret_f32
                   (f32.copysign (global.get $f) (f32.const -1)))
                 (i32.const 0xffa00000))
    (global.set $f (f32.const -nan:0x200000))
    (call $chk32 (i32.reinterpret_f32 (f32.abs (global.get $f)))
                 (i32.const 0x7fa00000))
    (global.set $f (f32.const nan:0x200000))
    (call $chk64 (i64x2.extract_lane 0
                   (f32x4.neg (f32x4.splat (global.get $f))))
                 (i64.const 0xffa00000ffa00000)))

  ;; An operation with a NaN operand, which is where the two profiles differ.
  ;; The inputs are signalling NaNs, so the full profile also has to quieten
  ;; them: payload 0x200000 becomes 0x600000.
  (func $nan_arith (param $w32 i32) (param $w32neg i32)
                   (param $w64 i64) (param $wvec i64)
    (global.set $f (f32.const nan:0x200000))
    (call $chk32 (i32.reinterpret_f32 (f32.add (global.get $f) (f32.const 1)))
                 (local.get $w32))
    ;; the sign, which the profile pins to positive and the hardware carries
    (global.set $f (f32.const -nan:0x200000))
    (call $chk32 (i32.reinterpret_f32 (f32.add (global.get $f) (f32.const 1)))
                 (local.get $w32neg))
    (global.set $d (f64.const nan:0x4000000000000))
    (call $chk64 (i64.reinterpret_f64 (f64.mul (global.get $d) (f64.const 2)))
                 (local.get $w64))
    ;; and a vector lane, since FPCR covers both register files
    (global.set $f (f32.const nan:0x200000))
    (call $chk64 (i64x2.extract_lane 0
                   (f32x4.add (f32x4.splat (global.get $f))
                              (v128.const f32x4 1 1 1 1)))
                 (local.get $wvec)))

  (func (export "nan_full")
    (call $nan_arith (i32.const 0x7fe00000) (i32.const 0xffe00000)
                     (i64.const 0x7ffc000000000000)
                     (i64.const 0x7fe000007fe00000)))
  (func (export "nan_det")
    (call $nan_arith (i32.const 0x7fc00000) (i32.const 0x7fc00000)
                     (i64.const 0x7ff8000000000000)
                     (i64.const 0x7fc000007fc00000)))

  ;; ---- the same rule, over every instruction it covers -----------------
  ;;
  ;; nan_det above asks three of them.  The rule reaches every floating-point
  ;; instruction that *generates* a NaN, so these are the rest -- including
  ;; the ones where the two hardwares differ about the SIGN rather than the
  ;; payload: an invalid operation with no NaN operand answers x86-64's
  ;; negative "indefinite" and AArch64's positive default NaN, so
  ;; f32.sqrt(-1) is as much a case here as f32.add(nan, 1) is.
  ;;
  ;; Asked only in the deterministic profile.  In the full profile these are
  ;; propagation, which the two hardwares are entitled to differ over and
  ;; which nan_invalid above masks the sign of for that reason.
  (global $g (mut f32) (f32.const 0))
  (global $e (mut f64) (f64.const 0))

  (func $c32 (param $got f32)
    (call $chk32 (i32.reinterpret_f32 (local.get $got))
                 (i32.const 0x7fc00000)))
  (func $c64 (param $got f64)
    (call $chk64 (i64.reinterpret_f64 (local.get $got))
                 (i64.const 0x7ff8000000000000)))
  (func $cv32 (param $got v128)
    (call $chk (local.get $got)
               (v128.const i32x4 0x7fc00000 0x7fc00000 0x7fc00000 0x7fc00000)))
  (func $cv64 (param $got v128)
    (call $chk (local.get $got)
               (v128.const i64x2 0x7ff8000000000000 0x7ff8000000000000)))

  (func (export "nan_det_ops")
    (global.set $f (f32.const nan:0x200000))
    (global.set $g (f32.const 1))
    (call $c32 (f32.sub (global.get $f) (global.get $g)))
    (call $c32 (f32.div (global.get $f) (global.get $g)))
    (call $c32 (f32.min (global.get $f) (global.get $g)))
    (call $c32 (f32.max (global.get $f) (global.get $g)))
    (call $c32 (f32.sqrt (global.get $f)))
    (call $c32 (f32.ceil (global.get $f)))
    (call $c32 (f32.floor (global.get $f)))
    (call $c32 (f32.trunc (global.get $f)))
    (call $c32 (f32.nearest (global.get $f)))
    (call $c64 (f64.promote_f32 (global.get $f)))
    ;; no NaN to propagate: the sign is the whole question
    (global.set $f (f32.const -1))
    (call $c32 (f32.sqrt (global.get $f)))
    (global.set $f (f32.const 0))
    (global.set $g (f32.const 0))
    (call $c32 (f32.div (global.get $f) (global.get $g)))

    (global.set $d (f64.const nan:0x4000000000000))
    (global.set $e (f64.const 1))
    (call $c64 (f64.add (global.get $d) (global.get $e)))
    (call $c64 (f64.min (global.get $d) (global.get $e)))
    (call $c64 (f64.max (global.get $d) (global.get $e)))
    (call $c64 (f64.sqrt (global.get $d)))
    (call $c64 (f64.nearest (global.get $d)))
    (call $c32 (f32.demote_f64 (global.get $d)))
    (global.set $d (f64.const -1))
    (call $c64 (f64.sqrt (global.get $d)))
    (global.set $d (f64.const 0))
    (global.set $e (f64.const 0))
    (call $c64 (f64.div (global.get $d) (global.get $e)))

    ;; the vector register file, which FPCR.DN covers in the same breath and
    ;; which the x64 back end has to canonicalise lane by lane
    (global.set $a (v128.const f32x4 nan:0x200000 nan:0x200000
                                     nan:0x200000 nan:0x200000))
    (global.set $b (v128.const f32x4 1 1 1 1))
    (call $cv32 (f32x4.add (global.get $a) (global.get $b)))
    (call $cv32 (f32x4.sub (global.get $a) (global.get $b)))
    (call $cv32 (f32x4.mul (global.get $a) (global.get $b)))
    (call $cv32 (f32x4.div (global.get $a) (global.get $b)))
    (call $cv32 (f32x4.min (global.get $a) (global.get $b)))
    (call $cv32 (f32x4.max (global.get $a) (global.get $b)))
    (call $cv32 (f32x4.sqrt (global.get $a)))
    (call $cv32 (f32x4.ceil (global.get $a)))
    (call $cv32 (f32x4.floor (global.get $a)))
    (call $cv32 (f32x4.trunc (global.get $a)))
    (call $cv32 (f32x4.nearest (global.get $a)))
    (call $cv64 (f64x2.promote_low_f32x4 (global.get $a)))
    (global.set $a (v128.const f32x4 -1 -1 -1 -1))
    (call $cv32 (f32x4.sqrt (global.get $a)))
    (global.set $a (v128.const f32x4 0 0 0 0))
    (global.set $b (v128.const f32x4 0 0 0 0))
    (call $cv32 (f32x4.div (global.get $a) (global.get $b)))

    (global.set $a (v128.const f64x2 nan:0x4000000000000 nan:0x4000000000000))
    (global.set $b (v128.const f64x2 1 1))
    (call $cv64 (f64x2.add (global.get $a) (global.get $b)))
    (call $cv64 (f64x2.min (global.get $a) (global.get $b)))
    (call $cv64 (f64x2.max (global.get $a) (global.get $b)))
    (call $cv64 (f64x2.sqrt (global.get $a)))
    (call $cv64 (f64x2.ceil (global.get $a)))
    (call $chk (f32x4.demote_f64x2_zero (global.get $a))
               (v128.const i32x4 0x7fc00000 0x7fc00000 0 0)))
)
