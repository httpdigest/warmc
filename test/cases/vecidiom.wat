;; Idioms a producer writes because wasm has no instruction for them, and
;; AArch64 does.
;;
;; Hand-written, and it stays hand-written: test/gen_cases.py does not emit a
;; file of this name, and run_cases.py globs the directory afterwards, so this
;; is run against wasm-interp like every generated case is.
;;
;; So far: absolute value.  wasm has no i32.abs, so LLVM emits the branchless
;; form `(x ^ (x >> 31)) - (x >> 31)` and warmc receives three operations
;; where NEON has one.  Recognising it is worth two of the nine instructions
;; in the vector body.
;;
;; Three things to get wrong:
;;
;;   * the shift amount.  Only a shift by the width less one broadcasts the
;;     sign; by anything else the expression is not an absolute value at all,
;;     and `a_notabs` is that expression with 30 instead of 31.
;;   * the lane.  The shift names the *scalar* type's sign bit, so folding
;;     this in a narrower lane would take the wrong bit -- `a_abs_narrow`
;;     keeps an i32 absolute value in a byte.
;;   * the smallest negative number, whose absolute value does not exist.
;;     Both the three-operation form and ABS return it unchanged, and a
;;     lowering that saturated instead would return INT_MAX.  Every buffer
;;     here contains it.
;;
;; The checksums are polynomial rather than sums, so a value landing in the
;; wrong lane is visible; a plain sum is not, which cost a round in
;; test/cases/vecrotate.wat.
(module
  (memory 2)

  ;; 1021 i32 cycling through the awkward values.  Odd, so the tail runs.
  (func $fill (result i32)
    (local $i i32) (local $m i32) (local $v i32)
    (loop $F
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $m (i32.rem_u (local.get $i) (i32.const 5)))
          (local.set $v
            (select (i32.const -2147483648)
              (select (i32.const 2147483647)
                (select (i32.const 0)
                  (select (i32.const -1)
                    (i32.sub (i32.mul (local.get $i) (i32.const 7)) (i32.const 3000))
                    (i32.eq (local.get $m) (i32.const 3)))
                  (i32.eq (local.get $m) (i32.const 2)))
                (i32.eq (local.get $m) (i32.const 1)))
              (i32.eq (local.get $m) (i32.const 0))))
          (i32.store (i32.mul (local.get $i) (i32.const 4)) (local.get $v))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $F))))
    (i32.const 1021))

  (func $hash (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (loop $L
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then
          (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
            (i32.load (i32.add (i32.const 8192)
                               (i32.mul (local.get $i) (i32.const 4))))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))

  ;; The idiom itself.  INT_MIN is in the buffer and its absolute value is
  ;; itself, which ABS and the three-operation form agree on.
  (func (export "a_abs") (result i32)
    (local $i i32) (local $n i32) (local $v i32)
    (local.set $n (call $fill))
    (loop $L
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then
          (local.set $v (i32.load (i32.mul (local.get $i) (i32.const 4))))
          (i32.store (i32.add (i32.const 8192) (i32.mul (local.get $i) (i32.const 4)))
            (i32.sub (i32.xor (local.get $v)
                              (i32.shr_s (local.get $v) (i32.const 31)))
                     (i32.shr_s (local.get $v) (i32.const 31))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (call $hash (local.get $n)))

  ;; A shift by 30, which broadcasts nothing and is not an absolute value.
  ;; Folding this to ABS returns a different number for every element that is
  ;; not already small and positive.
  (func (export "a_notabs") (result i32)
    (local $i i32) (local $n i32) (local $v i32)
    (local.set $n (call $fill))
    (loop $L
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then
          (local.set $v (i32.load (i32.mul (local.get $i) (i32.const 4))))
          (i32.store (i32.add (i32.const 8192) (i32.mul (local.get $i) (i32.const 4)))
            (i32.sub (i32.xor (local.get $v)
                              (i32.shr_s (local.get $v) (i32.const 30)))
                     (i32.shr_s (local.get $v) (i32.const 30))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (call $hash (local.get $n)))

  ;; A logical shift rather than an arithmetic one: also not an absolute
  ;; value, and the operation one letter away from the one that is.
  (func (export "a_shru") (result i32)
    (local $i i32) (local $n i32) (local $v i32)
    (local.set $n (call $fill))
    (loop $L
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then
          (local.set $v (i32.load (i32.mul (local.get $i) (i32.const 4))))
          (i32.store (i32.add (i32.const 8192) (i32.mul (local.get $i) (i32.const 4)))
            (i32.sub (i32.xor (local.get $v)
                              (i32.shr_u (local.get $v) (i32.const 31)))
                     (i32.shr_u (local.get $v) (i32.const 31))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (call $hash (local.get $n)))

  ;; 64-bit lanes, where ABS does have a .2d form.
  (func (export "a_abs_i64") (result i64)
    (local $i i32) (local $v i64) (local $s i64)
    (loop $F
      (if (i32.lt_u (local.get $i) (i32.const 509))
        (then
          (i64.store (i32.mul (local.get $i) (i32.const 8))
            (select (i64.const -9223372036854775808)
              (i64.sub (i64.mul (i64.extend_i32_u (local.get $i))
                                (i64.const 7046029254386353131))
                       (i64.const 3000))
              (i32.eq (i32.rem_u (local.get $i) (i32.const 11)) (i32.const 0))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $F))))
    (local.set $i (i32.const 0))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 509))
        (then
          (local.set $v (i64.load (i32.mul (local.get $i) (i32.const 8))))
          (i64.store (i32.add (i32.const 8192) (i32.mul (local.get $i) (i32.const 8)))
            (i64.sub (i64.xor (local.get $v)
                              (i64.shr_s (local.get $v) (i64.const 63)))
                     (i64.shr_s (local.get $v) (i64.const 63))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.set $i (i32.const 0))
    (loop $S
      (if (i32.lt_u (local.get $i) (i32.const 509))
        (then
          (local.set $s (i64.add (i64.mul (local.get $s) (i64.const 31))
            (i64.load (i32.add (i32.const 8192)
                               (i32.mul (local.get $i) (i32.const 8))))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $S))))
    (local.get $s))

  ;; An i32 absolute value kept in a byte.  The shift by 31 names the i32's
  ;; sign bit, which a byte lane does not contain, so this must not be folded
  ;; into a byte-lane ABS -- that would take bit 7 as the sign and return the
  ;; absolute value of a different number.
  ;;
  ;; The load is *unsigned* on purpose, and the first version of this case
  ;; used a signed one and caught nothing. Sign-extended, a byte is -128..127,
  ;; its i32 absolute value is 0..128, and truncating that back to a byte
  ;; gives 0x80 for -128 -- exactly what a byte-lane ABS gives. The two agree
  ;; on every input. Zero-extended the values are 0..255, all non-negative in
  ;; i32, so the expression is the identity, while a byte lane reads anything
  ;; from 0x80 up as negative and returns 256-v.
  (func (export "a_abs_narrow") (result i32)
    (local $i i32) (local $v i32) (local $s i32)
    (loop $F
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (i32.store8 (local.get $i)
            (i32.sub (i32.mul (local.get $i) (i32.const 37)) (i32.const 400)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $F))))
    (local.set $i (i32.const 0))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $v (i32.load8_u (local.get $i)))
          (i32.store8 (i32.add (i32.const 8192) (local.get $i))
            (i32.sub (i32.xor (local.get $v)
                              (i32.shr_s (local.get $v) (i32.const 31)))
                     (i32.shr_s (local.get $v) (i32.const 31))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.set $i (i32.const 0))
    (loop $S
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
            (i32.load8_u (i32.add (i32.const 8192) (local.get $i)))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $S))))
    (local.get $s))

  ;; The subtrahend is a *different* value's sign, not the one being xored.
  ;; Same three operations, same shapes, not an absolute value.
  (func (export "a_crossed") (result i32)
    (local $i i32) (local $n i32) (local $v i32) (local $w i32)
    (local.set $n (call $fill))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1020))
        (then
          (local.set $v (i32.load (i32.mul (local.get $i) (i32.const 4))))
          (local.set $w (i32.load (i32.add (i32.const 4)
                                    (i32.mul (local.get $i) (i32.const 4)))))
          (i32.store (i32.add (i32.const 8192) (i32.mul (local.get $i) (i32.const 4)))
            (i32.sub (i32.xor (local.get $v)
                              (i32.shr_s (local.get $v) (i32.const 31)))
                     (i32.shr_s (local.get $w) (i32.const 31))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (call $hash (i32.const 1020)))

  ;; The narrow case that actually bites, which a byte cannot express.
  ;;
  ;; Sixteen-bit lanes, loaded *unsigned*, so the i32 value is 0..65535 and
  ;; `v >> 15` is 0 or 1 rather than a mask of all ones -- the expression is
  ;; not an absolute value at all, it is `(v^1)-1` for the top half of the
  ;; range. A 16-bit-lane ABS reads 40000 as -25536 and returns 25536; the
  ;; scalar code returns 39999. Only here does the shift amount equal the lane
  ;; width less one *and* the operand fail to be that lane's sign, which is
  ;; why the byte version above cannot catch a missing narrow check and this
  ;; one can.
  (func (export "a_abs_narrow16") (result i32)
    (local $i i32) (local $v i32) (local $s i32)
    (loop $F
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (i32.store16 (i32.mul (local.get $i) (i32.const 2))
            (i32.add (i32.mul (local.get $i) (i32.const 277)) (i32.const 30000)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $F))))
    (local.set $i (i32.const 0))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $v (i32.load16_u (i32.mul (local.get $i) (i32.const 2))))
          (i32.store16 (i32.add (i32.const 8192) (i32.mul (local.get $i) (i32.const 2)))
            (i32.sub (i32.xor (local.get $v)
                              (i32.shr_s (local.get $v) (i32.const 15)))
                     (i32.shr_s (local.get $v) (i32.const 15))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.set $i (i32.const 0))
    (loop $S
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
            (i32.load16_u (i32.add (i32.const 8192)
                                   (i32.mul (local.get $i) (i32.const 2))))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $S))))
    (local.get $s))

  ;; A comparison counted, which is the shape the mask-subtract applies to:
  ;; a NEON compare sets every bit of a matching lane, so subtracting the mask
  ;; adds one and the AND that normalises it to 0-or-1 is not needed.
  (func (export "a_count") (result i32)
    (local $i i32) (local $n i32) (local $c i32)
    (local.set $n (call $fill))
    (loop $L
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then
          (local.set $c (i32.add (local.get $c)
            (i32.eq (i32.load (i32.mul (local.get $i) (i32.const 4)))
                    (i32.const -1))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $c))

  ;; The same comparison folded with xor rather than added, where the mask is
  ;; *not* interchangeable with the 0-or-1 it normalises to: xor against all
  ;; ones inverts the accumulator, and subtracting a mask is not an xor at
  ;; all.  Here so that restricting the substitution to sums is tested and not
  ;; merely asserted.
  (func (export "a_xor_cmp") (result i32)
    (local $i i32) (local $n i32) (local $x i32)
    (local.set $n (call $fill))
    (loop $L
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then
          (local.set $x (i32.xor (local.get $x)
            (i32.eq (i32.load (i32.mul (local.get $i) (i32.const 4)))
                    (i32.const -1))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $x))

  ;; And with or, whose identity is zero and whose answer is 1 as soon as any
  ;; element matches.
  (func (export "a_or_cmp") (result i32)
    (local $i i32) (local $n i32) (local $o i32)
    (local.set $n (call $fill))
    (loop $L
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then
          (local.set $o (i32.or (local.get $o)
            (i32.eq (i32.load (i32.mul (local.get $i) (i32.const 4)))
                    (i32.const -1))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $o))

  ;; One mask feeding two bitselects, written as SIMD by hand.
  ;;
  ;; This is what makes the tie between a two-address destination and its
  ;; dying operand a rule rather than a freebie.  BSL writes over its mask, so
  ;; when the mask dies at the select the allocator hands the select's result
  ;; that same register and both copies around the BSL disappear.  Here the
  ;; mask does *not* die at the first select -- the second one still needs it
  ;; -- and taking its register would destroy it in between.
  ;;
  ;; Hand-written rather than left to the vectoriser on purpose: it reaches
  ;; I_VSEL directly, so the rule is tested without depending on which loops
  ;; the vectoriser happens to accept.  The two selects choose opposite arms,
  ;; so a clobbered mask changes the second output and not only the first.
  (func (export "a_bitselect_shared") (result i32)
    (local $i i32) (local $x v128) (local $y v128) (local $m v128) (local $s i32)
    (loop $F
      (if (i32.lt_u (local.get $i) (i32.const 256))
        (then
          (i32.store (i32.mul (local.get $i) (i32.const 4))
            (i32.sub (i32.mul (local.get $i) (i32.const 2654435761)) (i32.const 7)))
          (i32.store (i32.add (i32.const 8192) (i32.mul (local.get $i) (i32.const 4)))
            (i32.sub (i32.mul (local.get $i) (i32.const 40503)) (i32.const 900)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $F))))
    (local.set $i (i32.const 0))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 64))
        (then
          (local.set $x (v128.load (i32.mul (local.get $i) (i32.const 16))))
          (local.set $y (v128.load (i32.add (i32.const 8192)
                                     (i32.mul (local.get $i) (i32.const 16)))))
          (local.set $m (i32x4.gt_s (local.get $x) (local.get $y)))
          (v128.store (i32.add (i32.const 16384) (i32.mul (local.get $i) (i32.const 16)))
            (v128.bitselect (local.get $x) (local.get $y) (local.get $m)))
          (v128.store (i32.add (i32.const 24576) (i32.mul (local.get $i) (i32.const 16)))
            (v128.bitselect (local.get $y) (local.get $x) (local.get $m)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.set $i (i32.const 0))
    (loop $S
      (if (i32.lt_u (local.get $i) (i32.const 256))
        (then
          (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
            (i32.load (i32.add (i32.const 16384)
                               (i32.mul (local.get $i) (i32.const 4))))))
          (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
            (i32.load (i32.add (i32.const 24576)
                               (i32.mul (local.get $i) (i32.const 4))))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $S))))
    (local.get $s))

  ;; A shift by an amount that is not a constant.
  ;;
  ;; `a[i] << k` for a k computed before the loop is still one vector shift --
  ;; the register form takes the amount in a general register and splats it.
  ;; Two things make it a rule: every lane has to name the *same* amount, and
  ;; that amount has to come from outside the loop.
  (func (export "s_shl_var") (result i32)
    (local $i i32) (local $n i32) (local $k i32)
    (local.set $n (call $fill))
    (local.set $k (i32.and (i32.load (i32.const 0)) (i32.const 7)))
    (loop $L
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then
          (i32.store (i32.add (i32.const 8192) (i32.mul (local.get $i) (i32.const 4)))
            (i32.shl (i32.load (i32.mul (local.get $i) (i32.const 4)))
                     (local.get $k)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (call $hash (local.get $n)))

  ;; An amount that differs per element, which no vector shift can express:
  ;; the register form splats one amount across every lane.
  (func (export "s_shl_perelem") (result i32)
    (local $i i32) (local $n i32)
    (local.set $n (call $fill))
    (loop $L
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then
          (i32.store (i32.add (i32.const 8192) (i32.mul (local.get $i) (i32.const 4)))
            (i32.shl (i32.load (i32.mul (local.get $i) (i32.const 4)))
                     (i32.and (local.get $i) (i32.const 7))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (call $hash (local.get $n)))

  ;; A variable amount in a narrow lane, which must be refused: wasm reduces
  ;; the amount modulo the *scalar* width and the instruction reduces it
  ;; modulo the *lane*.  The amount here is 17, so wasm shifts a 16-bit
  ;; value's worth left by seventeen and leaves zero in the low half, while a
  ;; 16-bit lane would shift by one.  The constant path can compare the two
  ;; numbers; this one cannot, so it refuses narrow lanes outright.
  ;;
  ;; Sixteen-bit rather than byte lanes, for the same reason a_abs_narrow16 is:
  ;; the byte version of this loop is refused for an unrelated reason and so
  ;; cannot tell whether the guard is there.
  (func (export "s_shl_narrow_var") (result i32)
    (local $i i32) (local $k i32) (local $s i32)
    (i32.store (i32.const 4096) (i32.const 17))
    (local.set $k (i32.load (i32.const 4096)))
    (loop $F
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (i32.store16 (i32.mul (local.get $i) (i32.const 2))
            (i32.add (i32.mul (local.get $i) (i32.const 277)) (i32.const 30000)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $F))))
    (local.set $i (i32.const 0))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (i32.store16 (i32.add (i32.const 8192) (i32.mul (local.get $i) (i32.const 2)))
            (i32.shl (i32.load16_u (i32.mul (local.get $i) (i32.const 2)))
                     (local.get $k)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.set $i (i32.const 0))
    (loop $S
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
            (i32.load16_u (i32.add (i32.const 8192)
                                   (i32.mul (local.get $i) (i32.const 2))))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $S))))
    (local.get $s))

  ;; A counted search, which pass_vec_search replaces with a 16-byte compare.
  ;;
  ;; Its entry guard asked only whether the loop had VF iterations left to run,
  ;; and the trip count is not the size of the memory: a scalar search stops at
  ;; the first match, so only the bytes up to the match have to exist.  The
  ;; vector body reads the whole block containing that match, so a match near
  ;; the end of memory made a correct program trap.  65530 is the last byte of
  ;; the first page, and the block containing it runs 14 bytes past the second.
  ;;
  ;; The guard must not answer this by declining every search, so the pair below
  ;; is the point: `near_end` used to trap and now returns, `inside` vectorises
  ;; either way, and both are read against wasm-interp.
  (func (export "vs_near_end") (result i32)
    (local $i i32)
    (i32.store8 (i32.const 131066) (i32.const 7))
    (block $out
      (loop $l
        (br_if $out (i32.eq (i32.load8_u (i32.add (i32.const 131066)
                                                  (local.get $i)))
                            (i32.const 7)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (i32.const 1000)))))
    (local.get $i))

  (func (export "vs_inside") (result i32)
    (local $i i32)
    (i32.store8 (i32.const 4096) (i32.const 0))
    (i32.store8 (i32.const 4137) (i32.const 7))
    (block $out
      (loop $l
        (br_if $out (i32.eq (i32.load8_u (i32.add (i32.const 4096)
                                                  (local.get $i)))
                            (i32.const 7)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (i32.const 1000)))))
    (local.get $i))

  ;; The entry guard is not the only one: the continuation test decides
  ;; whether to run *another* block, and it asked the same trip-count
  ;; question.  Here the first block is inside the memory and the second is
  ;; not, and the scalar search stops at 131070 -- so a correct answer exists
  ;; and only the vector path can miss it.  The match is 20 in from 131050,
  ;; which puts the second block's 16 bytes 10 past the end of two pages.
  (func (export "vs_second_block") (result i32)
    (local $i i32)
    (i32.store8 (i32.const 131070) (i32.const 9))
    (block $out
      (loop $l
        (br_if $out (i32.eq (i32.load8_u (i32.add (i32.const 131050)
                                                  (local.get $i)))
                            (i32.const 9)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (i32.const 1000)))))
    (local.get $i))

  ;; When the trip count is an exact multiple of the lane count, the last
  ;; block ends flush with the bound -- and the continuation test handed the
  ;; scalar loop back at that index, one past the last element the source
  ;; loop ever reaches.  The scalar loop then ran a body the source did not:
  ;; an extra element test, which answers wrongly if it matches, and an extra
  ;; load.  Here the searched range ends at the end of the memory, so the
  ;; extra load traps in a program that cannot: 1024 elements from 130048 is
  ;; the last byte of the second page.
  (func (export "vs_exact_end") (result i32)
    (local $i i32)
    (memory.fill (i32.const 130048) (i32.const 0) (i32.const 1024))
    (block $out
      (loop $l
        (br_if $out (i32.eq (i32.load8_u (i32.add (i32.const 130048)
                                                  (local.get $i)))
                            (i32.const 7)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (i32.const 1024)))))
    (local.get $i))
)
