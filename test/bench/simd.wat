;; The v128 instruction set, as kernels rather than as one-shot assertions.
;;
;; `test/cases/v128.wat` and `test/cases/simd.wat` say whether each of the 236
;; vector opcodes computes the right answer; neither says what one costs.
;; These are loops, so what shows up here is the lowering: an operation the
;; hardware does not have and the back end has to synthesise, a lane extract
;; that goes through memory, a constant re-materialised every iteration, a
;; shuffle that could have been a single `tbl`.
;;
;; This is the other half of the vectoriser's story and deliberately separate
;; from it.  `vecwork.c` measures loops warmc *chooses* to widen; these are
;; loops a producer already widened, which is what a module built with
;; `-msimd128` or with intrinsics looks like when it arrives.
;;
;; Every kernel takes its vector count as a parameter, so no trip count is
;; known and nothing here can be unrolled against a constant.  All the
;; floating-point data is a small integer converted to f32: a random bit
;; pattern is a NaN about half the time, and which NaN an implementation
;; produces is not fixed by the specification, so a differential test built on
;; one would be comparing two legal answers.
(module
  (memory 1)

  ;; i32/byte lanes: VA, VB in, VC out -- 256 vectors each.
  ;; f32 lanes: FA, FB in, FC out.
  ;;   VA 0   VB 4096   VC 8192   FA 12288   FB 16384   FC 20480

  (func $init (local $i i32) (local $s i32)
    (local.set $s (i32.const 0x2545F491))
    (loop $l
      (local.set $s (i32.xor (local.get $s) (i32.shl (local.get $s) (i32.const 13))))
      (local.set $s (i32.xor (local.get $s) (i32.shr_u (local.get $s) (i32.const 17))))
      (local.set $s (i32.xor (local.get $s) (i32.shl (local.get $s) (i32.const 5))))
      (i32.store offset=0 (i32.shl (local.get $i) (i32.const 2)) (local.get $s))
      (f32.store offset=12288 (i32.shl (local.get $i) (i32.const 2))
        (f32.convert_i32_u (i32.and (local.get $s) (i32.const 0xffff))))
      (local.set $s (i32.xor (local.get $s) (i32.shl (local.get $s) (i32.const 13))))
      (local.set $s (i32.xor (local.get $s) (i32.shr_u (local.get $s) (i32.const 17))))
      (local.set $s (i32.xor (local.get $s) (i32.shl (local.get $s) (i32.const 5))))
      (i32.store offset=4096 (i32.shl (local.get $i) (i32.const 2)) (local.get $s))
      (f32.store offset=16384 (i32.shl (local.get $i) (i32.const 2))
        (f32.convert_i32_u (i32.and (local.get $s) (i32.const 0x3ff))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 1024)))))
  (start $init)

  ;; Every kernel funnels its vector accumulator through this, so a wrong lane
  ;; cannot cancel out against another one.
  (func $fold (param $v v128) (result i32)
    (i32.add
      (i32.mul (i32x4.extract_lane 0 (local.get $v)) (i32.const 1))
      (i32.add
        (i32.mul (i32x4.extract_lane 1 (local.get $v)) (i32.const 3))
        (i32.add
          (i32.mul (i32x4.extract_lane 2 (local.get $v)) (i32.const 5))
          (i32.mul (i32x4.extract_lane 3 (local.get $v)) (i32.const 7))))))

  ;; ---- integer lanes -------------------------------------------------

  ;; The plainest vector loop there is: two loads, a multiply, an add.
  (func (export "s_dot") (param $n i32) (result i32)
    (local $i i32) (local $acc v128)
    (if (i32.gt_u (local.get $n) (i32.const 256))
      (then (local.set $n (i32.const 256))))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $acc (i32x4.add (local.get $acc)
          (i32x4.mul (v128.load offset=0 (i32.shl (local.get $i) (i32.const 4)))
                     (v128.load offset=4096 (i32.shl (local.get $i) (i32.const 4))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $fold (local.get $acc)))

  ;; Byte lanes and the saturating forms.  `i8x16.popcnt` is one instruction on
  ;; this target and several on most others, so it belongs in the same loop as
  ;; the ones that are not.
  (func (export "s_bytes") (param $n i32) (result i32)
    (local $i i32) (local $acc v128) (local $a v128) (local $b v128)
    (if (i32.gt_u (local.get $n) (i32.const 256))
      (then (local.set $n (i32.const 256))))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $a (v128.load offset=0 (i32.shl (local.get $i) (i32.const 4))))
        (local.set $b (v128.load offset=4096 (i32.shl (local.get $i) (i32.const 4))))
        (local.set $acc (v128.xor (local.get $acc)
          (i8x16.add_sat_u
            (i8x16.avgr_u (local.get $a) (local.get $b))
            (i8x16.sub_sat_s
              (i8x16.min_u (local.get $a) (local.get $b))
              (i8x16.popcnt (i8x16.max_s (local.get $a) (local.get $b)))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $fold (local.get $acc)))

  ;; Two ways to permute: a shuffle with a constant pattern, which is a `tbl`
  ;; against a materialised index vector, and a swizzle, whose pattern is data.
  (func (export "s_shuf") (param $n i32) (result i32)
    (local $i i32) (local $acc v128) (local $a v128) (local $b v128)
    (if (i32.gt_u (local.get $n) (i32.const 256))
      (then (local.set $n (i32.const 256))))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $a (v128.load offset=0 (i32.shl (local.get $i) (i32.const 4))))
        (local.set $b (v128.load offset=4096 (i32.shl (local.get $i) (i32.const 4))))
        (local.set $acc (v128.xor (local.get $acc)
          (v128.or
            (i8x16.shuffle 0 17 2 19 4 21 6 23 8 25 10 27 12 29 14 31
                           (local.get $a) (local.get $b))
            (i8x16.swizzle (local.get $a)
                           (v128.and (local.get $b) (v128.const i8x16
                             15 15 15 15 15 15 15 15 15 15 15 15 15 15 15 15))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $fold (local.get $acc)))

  ;; The widening family: extend, widening multiply, pairwise add, and a
  ;; narrowing store back down.  This is what image and audio code is made of.
  (func (export "s_widen") (param $n i32) (result i32)
    (local $i i32) (local $acc v128) (local $a v128) (local $b v128)
    (if (i32.gt_u (local.get $n) (i32.const 256))
      (then (local.set $n (i32.const 256))))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $a (v128.load offset=0 (i32.shl (local.get $i) (i32.const 4))))
        (local.set $b (v128.load offset=4096 (i32.shl (local.get $i) (i32.const 4))))
        (local.set $acc (i32x4.add (local.get $acc)
          (i32x4.add
            (i32x4.extadd_pairwise_i16x8_u
              (i16x8.extmul_low_i8x16_u (local.get $a) (local.get $b)))
            (i32x4.extend_high_i16x8_s
              (i8x16.narrow_i16x8_s
                (i16x8.extend_low_i8x16_s (local.get $a))
                (i16x8.extend_high_i8x16_s (local.get $b)))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $fold (local.get $acc)))

  ;; A lane-wise conditional, which is what if-conversion produces: compare,
  ;; select, and a scalar branch on whether any lane survived.
  (func (export "s_cmp") (param $n i32) (result i32)
    (local $i i32) (local $acc v128) (local $a v128) (local $b v128) (local $hits i32)
    (if (i32.gt_u (local.get $n) (i32.const 256))
      (then (local.set $n (i32.const 256))))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $a (v128.load offset=0 (i32.shl (local.get $i) (i32.const 4))))
        (local.set $b (v128.load offset=4096 (i32.shl (local.get $i) (i32.const 4))))
        (local.set $acc (i32x4.add (local.get $acc)
          (v128.bitselect (local.get $a) (local.get $b)
                          (i32x4.gt_s (local.get $a) (local.get $b)))))
        (if (i32.and (v128.any_true (i32x4.lt_u (local.get $a) (local.get $b)))
                     (i32x4.all_true (i32x4.ne (local.get $a) (local.get $b))))
          (then (local.set $hits (i32.add (local.get $hits) (i32.const 1)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (i32.add (call $fold (local.get $acc)) (local.get $hits)))

  ;; A bitmask taken back into scalar registers every iteration, which is the
  ;; one place a vector loop has to cross the register files.
  (func (export "s_mask") (param $n i32) (result i32)
    (local $i i32) (local $acc i32) (local $a v128)
    (if (i32.gt_u (local.get $n) (i32.const 256))
      (then (local.set $n (i32.const 256))))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $a (v128.load offset=0 (i32.shl (local.get $i) (i32.const 4))))
        (local.set $acc (i32.add (local.get $acc)
          (i32.add (i32.popcnt (i8x16.bitmask (local.get $a)))
                   (i32.add (i16x8.bitmask (local.get $a))
                            (i32x4.bitmask (local.get $a))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (local.get $acc))

  ;; Lane traffic in both directions: a scalar out, a scalar back in, and a
  ;; splat of a value the loop computes.
  (func (export "s_lane") (param $n i32) (result i32)
    (local $i i32) (local $acc i32) (local $a v128) (local $t v128)
    (if (i32.gt_u (local.get $n) (i32.const 256))
      (then (local.set $n (i32.const 256))))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $a (v128.load offset=0 (i32.shl (local.get $i) (i32.const 4))))
        (local.set $t (i32x4.replace_lane 2
          (i32x4.splat (i32.add (local.get $acc) (local.get $i)))
          (i32x4.extract_lane 1 (local.get $a))))
        (local.set $acc (i32.add (local.get $acc)
          (i32.add (i32x4.extract_lane 3 (i32x4.add (local.get $a) (local.get $t)))
                   (i32.and (i8x16.extract_lane_u 5 (local.get $a)) (i32.const 255)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (local.get $acc))

  ;; Two lanes to a vector, and `i64x2.mul`, which the hardware does not have:
  ;; the back end builds it out of four 32-bit lane operations.
  (func (export "s_i64") (param $n i32) (result i64)
    (local $i i32) (local $acc v128) (local $a v128) (local $b v128)
    (if (i32.gt_u (local.get $n) (i32.const 256))
      (then (local.set $n (i32.const 256))))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $a (v128.load offset=0 (i32.shl (local.get $i) (i32.const 4))))
        (local.set $b (v128.load offset=4096 (i32.shl (local.get $i) (i32.const 4))))
        (local.set $acc (i64x2.add (local.get $acc)
          (i64x2.shr_s (i64x2.mul (local.get $a) (local.get $b)) (i32.const 3))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (i64.add (i64x2.extract_lane 0 (local.get $acc))
             (i64x2.extract_lane 1 (local.get $acc))))

  ;; The signal-processing pair: a widening dot product and a saturating
  ;; fixed-point multiply.
  (func (export "s_dotp") (param $n i32) (result i32)
    (local $i i32) (local $acc v128) (local $a v128) (local $b v128)
    (if (i32.gt_u (local.get $n) (i32.const 256))
      (then (local.set $n (i32.const 256))))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (local.set $a (v128.load offset=0 (i32.shl (local.get $i) (i32.const 4))))
        (local.set $b (v128.load offset=4096 (i32.shl (local.get $i) (i32.const 4))))
        (local.set $acc (i32x4.add (local.get $acc)
          (i32x4.dot_i16x8_s (local.get $a)
            (i16x8.q15mulr_sat_s (local.get $a) (local.get $b)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (call $fold (local.get $acc)))

  ;; ---- floating-point lanes ------------------------------------------

  ;; Written, then read back in a second loop so nothing can decide the store
  ;; is dead.  Every input is a small integer, so no operation here can produce
  ;; a NaN and the answer is fixed by IEEE rather than by the implementation.
  (func (export "s_fma") (param $n i32) (result i32)
    (local $i i32) (local $acc v128)
    (if (i32.gt_u (local.get $n) (i32.const 256))
      (then (local.set $n (i32.const 256))))
    (block $done
      (br_if $done (i32.eqz (local.get $n)))
      (loop $l
        (v128.store offset=20480 (i32.shl (local.get $i) (i32.const 4))
          (f32x4.add
            (f32x4.mul (v128.load offset=12288 (i32.shl (local.get $i) (i32.const 4)))
                       (v128.const f32x4 1.5 1.5 1.5 1.5))
            (v128.load offset=16384 (i32.shl (local.get $i) (i32.const 4)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
      (local.set $i (i32.const 0))
      (loop $l2
        (local.set $acc (f32x4.add (local.get $acc)
          (f32x4.min
            (v128.load offset=20480 (i32.shl (local.get $i) (i32.const 4)))
            (f32x4.sqrt (v128.load offset=16384 (i32.shl (local.get $i) (i32.const 4)))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l2 (i32.lt_u (local.get $i) (local.get $n)))))
    (call $fold (i32x4.trunc_sat_f32x4_s (local.get $acc))))

  (func (export "startup") (param i32) (result i32) (i32.const 0))
)
