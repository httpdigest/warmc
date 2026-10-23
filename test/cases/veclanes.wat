;; Idioms the vectoriser takes because a lane has an instruction for them that
;; wasm lacks, and values in a lane of another type than the loop's accesses.
;;
;; Hand-written, and it stays hand-written: test/gen_cases.py does not emit a
;; file of this name, and run_cases.py globs the directory afterwards, so this
;; is run against wasm-interp like every generated case is.
;;
;; What is taken, and the shape it arrives in (src/opt.c, v_build):
;;   * bswap, which fold makes of LLVM's two rotates and two masks: REV32 on
;;     bytes (bswap32);
;;   * a saturating add of bytes or halfwords written in i32, where nothing
;;     wraps -- select(255, s, s >=u 255) on the sum of two load8_u -- and a
;;     subtract clamped at zero by a signed max or by a compare of its
;;     operands: UQADD, UQSUB (sat8, sat8gt, sat16, subsat8, subsat16,
;;     subsat8le), and the signed ones, a min and a max of a sum or
;;     difference of signed lane values: SQADD, SQSUB (ssat16, ssub16,
;;     ssub8);
;;   * the high half of a product of two lane values, (x * y) >> 16 on
;;     halfwords or >> 8 on bytes, unsigned or signed: UMULL, UMULL2 or SMULL
;;     and UZP2 (mulhi16u, mulhi16s, mulhi8u, mulhi16shrs);
;;   * i32 to f32 and back, converting and saturating as the scalar forms
;;     do, and the f32 compares and selects TinyGo puts round a conversion
;;     (cvt, cvtu, i2f, u2f, f2i, f2u);
;;   * a select on any compare, floats included: f32::min and max as Rust
;;     writes them, x != x ? y : x (fmm32, fmm64), f32 compares as i32
;;     values (fcmpval), an i32 compare choosing f32s (iselect), an
;;     if-converted f32 merge (fmerge), a byte select on an equality
;;     (bsel8);
;;   * the high word of a 32-bit product, a division by a constant (div10,
;;     with a remainder beside it);
;;   * a loop whose bounds check its own exit test has decided, once the
;;     check is folded, and a loop tested before its counter steps (bounds,
;;     down).
;;
;; And what must stay scalar, each beside the case it resembles:
;;   * a bswap whose bytes a narrow lane does not have (bswap16, bswap16hi);
;;   * a clamp that is not the lane's top (clamp254) or range (ssat16short),
;;     operands that do not fit the lane (satsigned, ssat16u), a select on a
;;     compare the lane cannot make (bsel8wide);
;;   * a product's high half with operands of mixed sign (mulhimixed) or a
;;     shift that is not the lane's width (mulhi15, mulhi17), and a division
;;     left a division (div7);
;;   * conversions of another width: from i64 (i64tof) and to i64 kept in
;;     32 bits (f2i64), and a loop of mixed lanes with a reduction
;;     (f2isum);
;;   * a bounds check that can fail (bounds_live), and one the exit test
;;     leaves open (bounds_last).
;;
;; Lengths are odd, so the scalar tail runs; buffers are noise with the
;; values that matter planted at fixed strides, and the checksums are
;; polynomial, so a value in the wrong lane shows.
(module
  (memory 2)
  (global $seed (mut i32) (i32.const 12345))

  (func $rand (result i32)
    (global.set $seed
      (i32.add (i32.mul (global.get $seed) (i32.const 1103515245)) (i32.const 12345)))
    (i32.xor (global.get $seed) (i32.shr_u (global.get $seed) (i32.const 13))))

  ;; ---- fills ----------------------------------------------------------

  ;; bytes: 255 every 9th, 0 every 13th, noise elsewhere
  (func $fill8 (param $p i32) (param $n i32)
    (local $i i32) (local $v i32)
    (loop $l
      (local.set $v (call $rand))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 9)))
        (then (local.set $v (i32.const 255))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 13)))
        (then (local.set $v (i32.const 0))))
      (i32.store8 (i32.add (local.get $p) (local.get $i)) (local.get $v))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))

  ;; halfwords: 0xffff, 0, 0x8000 and 0x7fff at strides, noise elsewhere
  (func $fill16 (param $p i32) (param $n i32)
    (local $i i32) (local $v i32)
    (loop $l
      (local.set $v (call $rand))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 9)))
        (then (local.set $v (i32.const 0xffff))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 13)))
        (then (local.set $v (i32.const 0))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 7)))
        (then (local.set $v (i32.const 0x8000))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 11)))
        (then (local.set $v (i32.const 0x7fff))))
      (i32.store16 (i32.add (local.get $p) (i32.shl (local.get $i) (i32.const 1)))
                   (local.get $v))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))

  ;; words: the extremes at strides, 32-bit noise elsewhere
  (func $fill32 (param $p i32) (param $n i32)
    (local $i i32) (local $v i32)
    (loop $l
      (local.set $v (i32.xor (i32.shl (call $rand) (i32.const 16)) (call $rand)))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 9)))
        (then (local.set $v (i32.const 0x80000000))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 13)))
        (then (local.set $v (i32.const 0x7fffffff))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 7)))
        (then (local.set $v (i32.const -1))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 11)))
        (then (local.set $v (i32.const 0))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 17)))
        (then (local.set $v (i32.const 16777217))))
      (i32.store (i32.add (local.get $p) (i32.shl (local.get $i) (i32.const 2)))
                 (local.get $v))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))

  ;; f32 bit patterns: NaNs of both signs and two payloads, both zeros, both
  ;; infinities, the int32 bounds and the floats beside them, 2^32, and
  ;; finite noise from 2^-27 to 2^33 of either sign
  (func $fillf (param $p i32) (param $n i32)
    (local $i i32) (local $v i32) (local $r i32)
    (loop $l
      (local.set $r (call $rand))
      (local.set $v
        (i32.or (i32.or (i32.and (local.get $r) (i32.const 0x807fffff))
                        (i32.shl (i32.add (i32.const 100)
                                          (i32.rem_u (call $rand) (i32.const 60)))
                                 (i32.const 23)))
                (i32.const 0)))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 17)))
        (then (local.set $v (i32.const 0x7fc00000))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 19)))
        (then (local.set $v (i32.const 0xffa00001))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 5)))
        (then (local.set $v (i32.const 0x80000000))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 6)))
        (then (local.set $v (i32.const 0))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 23)))
        (then (local.set $v (i32.const 0x7f800000))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 29)))
        (then (local.set $v (i32.const 0xff800000))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 31)))
        (then (local.set $v (i32.const 0x4f000000))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 37)))
        (then (local.set $v (i32.const 0xcf000000))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 41)))
        (then (local.set $v (i32.const 0x4effffff))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 43)))
        (then (local.set $v (i32.const 0x4f800000))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 47)))
        (then (local.set $v (i32.const 0xcf000001))))
      (i32.store (i32.add (local.get $p) (i32.shl (local.get $i) (i32.const 2)))
                 (local.get $v))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))

  ;; f64 the same way: NaNs, zeros, infinities, noise
  (func $fillf64 (param $p i32) (param $n i32)
    (local $i i32) (local $v i64)
    (loop $l
      (local.set $v
        (i64.or (i64.and (i64.or (i64.shl (i64.extend_i32_u (call $rand)) (i64.const 32))
                                 (i64.extend_i32_u (call $rand)))
                         (i64.const 0x800fffffffffffff))
                (i64.shl (i64.extend_i32_u
                           (i32.add (i32.const 990) (i32.rem_u (call $rand) (i32.const 60))))
                         (i64.const 52))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 7)))
        (then (local.set $v (i64.const 0x7ff8000000000000))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 11)))
        (then (local.set $v (i64.const 0xfff4000000000001))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 5)))
        (then (local.set $v (i64.const 0x8000000000000000))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 6)))
        (then (local.set $v (i64.const 0))))
      (if (i32.eqz (i32.rem_u (local.get $i) (i32.const 13)))
        (then (local.set $v (i64.const 0x7ff0000000000000))))
      (i64.store (i32.add (local.get $p) (i32.shl (local.get $i) (i32.const 3)))
                 (local.get $v))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))

  ;; ---- checksums ------------------------------------------------------

  (func $h8 (param $p i32) (param $n i32) (result i32)
    (local $i i32) (local $h i32)
    (loop $l
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
                             (i32.load8_u (i32.add (local.get $p) (local.get $i)))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n))))
    (local.get $h))

  (func $h16 (param $p i32) (param $n i32) (result i32)
    (local $i i32) (local $h i32)
    (loop $l
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
                             (i32.load16_u (i32.add (local.get $p)
                                                    (i32.shl (local.get $i) (i32.const 1))))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n))))
    (local.get $h))

  (func $h32 (param $p i32) (param $n i32) (result i32)
    (local $i i32) (local $h i32)
    (loop $l
      (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31))
                             (i32.load (i32.add (local.get $p)
                                                (i32.shl (local.get $i) (i32.const 2))))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n))))
    (local.get $h))

  (func $h64 (param $p i32) (param $n i32) (result i64)
    (local $i i32) (local $h i64)
    (loop $l
      (local.set $h (i64.add (i64.mul (local.get $h) (i64.const 31))
                             (i64.load (i32.add (local.get $p)
                                                (i32.shl (local.get $i) (i32.const 3))))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n))))
    (local.get $h))

  ;; ---- bswap ----------------------------------------------------------

  ;; LLVM's bswap: ((x & 0x00ff00ff) rotr 8) | ((x rotr 24) & 0x00ff00ff)
  (func $bswap32 (param $d i32) (param $a i32) (param $n i32)
    (local $i i32) (local $o i32) (local $x i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 2)))
      (local.set $x (i32.load (i32.add (local.get $a) (local.get $o))))
      (i32.store (i32.add (local.get $d) (local.get $o))
        (i32.or (i32.rotr (i32.and (local.get $x) (i32.const 0x00ff00ff)) (i32.const 8))
                (i32.and (i32.rotr (local.get $x) (i32.const 24)) (i32.const 0x00ff00ff))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "bswap32") (result i32)
    (call $fill32 (i32.const 0) (i32.const 1021))
    (call $bswap32 (i32.const 32768) (i32.const 0) (i32.const 1021))
    (call $h32 (i32.const 32768) (i32.const 1021)))

  ;; the same of a halfword: the bytes it swaps in are zero, so the low half
  ;; of the result is, and a 16-bit lane has neither
  (func $bswap16 (param $d i32) (param $a i32) (param $n i32) (param $hi i32)
    (local $i i32) (local $o i32) (local $x i32) (local $r i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 1)))
      (local.set $x (i32.load16_u (i32.add (local.get $a) (local.get $o))))
      (local.set $r
        (i32.or (i32.rotr (i32.and (local.get $x) (i32.const 0x00ff00ff)) (i32.const 8))
                (i32.and (i32.rotr (local.get $x) (i32.const 24)) (i32.const 0x00ff00ff))))
      (i32.store16 (i32.add (local.get $d) (local.get $o))
        (i32.shr_u (local.get $r) (local.get $hi)))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "bswap16") (result i32)
    (call $fill16 (i32.const 0) (i32.const 1021))
    (call $bswap16 (i32.const 32768) (i32.const 0) (i32.const 1021) (i32.const 0))
    (call $h16 (i32.const 32768) (i32.const 1021)))
  (func (export "bswap16hi") (result i32)
    (call $fill16 (i32.const 0) (i32.const 1021))
    (call $bswap16 (i32.const 32768) (i32.const 0) (i32.const 1021) (i32.const 16))
    (call $h16 (i32.const 32768) (i32.const 1021)))

  ;; ---- saturating add and subtract --------------------------------------

  (func $sat8 (param $d i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $s i32)
    (loop $l
      (local.set $s (i32.add (i32.load8_u (i32.add (local.get $a) (local.get $i)))
                             (i32.load8_u (i32.add (local.get $b) (local.get $i)))))
      (i32.store8 (i32.add (local.get $d) (local.get $i))
        (select (i32.const 255) (local.get $s)
                (i32.ge_u (local.get $s) (i32.const 255))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "sat8") (result i32)
    (call $fill8 (i32.const 0) (i32.const 1021))
    (call $fill8 (i32.const 16384) (i32.const 1021))
    (call $sat8 (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021))
    (call $h8 (i32.const 32768) (i32.const 1021)))

  ;; the clamp as a min the other way round: s > 255 ? 255 : s
  (func $sat8gt (param $d i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $s i32)
    (loop $l
      (local.set $s (i32.add (i32.load8_u (i32.add (local.get $a) (local.get $i)))
                             (i32.load8_u (i32.add (local.get $b) (local.get $i)))))
      (i32.store8 (i32.add (local.get $d) (local.get $i))
        (select (local.get $s) (i32.const 255)
                (i32.le_u (local.get $s) (i32.const 255))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "sat8gt") (result i32)
    (call $fill8 (i32.const 0) (i32.const 1021))
    (call $fill8 (i32.const 16384) (i32.const 1021))
    (call $sat8gt (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021))
    (call $h8 (i32.const 32768) (i32.const 1021)))

  ;; a clamp at 254 is a min, not a saturation, and a byte lane cannot make it
  (func $clamp254 (param $d i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $s i32)
    (loop $l
      (local.set $s (i32.add (i32.load8_u (i32.add (local.get $a) (local.get $i)))
                             (i32.load8_u (i32.add (local.get $b) (local.get $i)))))
      (i32.store8 (i32.add (local.get $d) (local.get $i))
        (select (i32.const 254) (local.get $s)
                (i32.ge_u (local.get $s) (i32.const 254))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "clamp254") (result i32)
    (call $fill8 (i32.const 0) (i32.const 1021))
    (call $fill8 (i32.const 16384) (i32.const 1021))
    (call $clamp254 (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021))
    (call $h8 (i32.const 32768) (i32.const 1021)))

  ;; signed bytes: a negative sum is above 255 unsigned and clamps to it,
  ;; which UQADD on the bytes would not do
  (func $satsigned (param $d i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $s i32)
    (loop $l
      (local.set $s (i32.add (i32.load8_s (i32.add (local.get $a) (local.get $i)))
                             (i32.load8_s (i32.add (local.get $b) (local.get $i)))))
      (i32.store8 (i32.add (local.get $d) (local.get $i))
        (select (i32.const 255) (local.get $s)
                (i32.ge_u (local.get $s) (i32.const 255))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "satsigned") (result i32)
    (call $fill8 (i32.const 0) (i32.const 1021))
    (call $fill8 (i32.const 16384) (i32.const 1021))
    (call $satsigned (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021))
    (call $h8 (i32.const 32768) (i32.const 1021)))

  (func $sat16 (param $d i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $o i32) (local $s i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 1)))
      (local.set $s (i32.add (i32.load16_u (i32.add (local.get $a) (local.get $o)))
                             (i32.load16_u (i32.add (local.get $b) (local.get $o)))))
      (i32.store16 (i32.add (local.get $d) (local.get $o))
        (select (i32.const 0xffff) (local.get $s)
                (i32.ge_u (local.get $s) (i32.const 0xffff))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "sat16") (result i32)
    (call $fill16 (i32.const 0) (i32.const 1021))
    (call $fill16 (i32.const 16384) (i32.const 1021))
    (call $sat16 (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021))
    (call $h16 (i32.const 32768) (i32.const 1021)))

  ;; a - b, or zero where a < b
  (func $subsat8 (param $d i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $x i32) (local $y i32)
    (loop $l
      (local.set $x (i32.load8_u (i32.add (local.get $a) (local.get $i))))
      (local.set $y (i32.load8_u (i32.add (local.get $b) (local.get $i))))
      (i32.store8 (i32.add (local.get $d) (local.get $i))
        (select (i32.const 0) (i32.sub (local.get $x) (local.get $y))
                (i32.lt_u (local.get $x) (local.get $y))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "subsat8") (result i32)
    (call $fill8 (i32.const 0) (i32.const 1021))
    (call $fill8 (i32.const 16384) (i32.const 1021))
    (call $subsat8 (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021))
    (call $h8 (i32.const 32768) (i32.const 1021)))

  ;; a - b where b <= a, else zero: the compare with its operands swapped
  (func $subsat8le (param $d i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $x i32) (local $y i32)
    (loop $l
      (local.set $x (i32.load8_u (i32.add (local.get $a) (local.get $i))))
      (local.set $y (i32.load8_u (i32.add (local.get $b) (local.get $i))))
      (i32.store8 (i32.add (local.get $d) (local.get $i))
        (select (i32.sub (local.get $x) (local.get $y)) (i32.const 0)
                (i32.le_u (local.get $y) (local.get $x))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "subsat8le") (result i32)
    (call $fill8 (i32.const 0) (i32.const 1021))
    (call $fill8 (i32.const 16384) (i32.const 1021))
    (call $subsat8le (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021))
    (call $h8 (i32.const 32768) (i32.const 1021)))

  ;; the difference of two halfwords, kept by a signed max with zero
  (func $subsat16 (param $d i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $o i32) (local $s i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 1)))
      (local.set $s (i32.sub (i32.load16_u (i32.add (local.get $a) (local.get $o)))
                             (i32.load16_u (i32.add (local.get $b) (local.get $o)))))
      (i32.store16 (i32.add (local.get $d) (local.get $o))
        (select (local.get $s) (i32.const 0)
                (i32.gt_s (local.get $s) (i32.const 0))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "subsat16") (result i32)
    (call $fill16 (i32.const 0) (i32.const 1021))
    (call $fill16 (i32.const 16384) (i32.const 1021))
    (call $subsat16 (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021))
    (call $h16 (i32.const 32768) (i32.const 1021)))

  ;; signed: x + y (or x - y) clamped to [lo, hi] by a min and a max, of
  ;; halfwords loaded signed
  (func $ssat16 (param $d i32) (param $a i32) (param $b i32) (param $n i32)
    (param $sub i32) (param $lo i32) (param $hi i32)
    (local $i i32) (local $o i32) (local $x i32) (local $y i32) (local $s i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 1)))
      (local.set $x (i32.load16_s (i32.add (local.get $a) (local.get $o))))
      (local.set $y (i32.load16_s (i32.add (local.get $b) (local.get $o))))
      (local.set $s (select (i32.sub (local.get $x) (local.get $y))
                            (i32.add (local.get $x) (local.get $y))
                            (local.get $sub)))
      (local.set $s (select (local.get $s) (local.get $hi)
                            (i32.lt_s (local.get $s) (local.get $hi))))
      (i32.store16 (i32.add (local.get $d) (local.get $o))
        (select (local.get $s) (local.get $lo)
                (i32.gt_s (local.get $s) (local.get $lo))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "ssat16") (result i32)
    (call $fill16 (i32.const 0) (i32.const 1021))
    (call $fill16 (i32.const 16384) (i32.const 1021))
    (call $ssat16 (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021)
                  (i32.const 0) (i32.const -32768) (i32.const 32767))
    (call $h16 (i32.const 32768) (i32.const 1021)))
  (func (export "ssub16") (result i32)
    (call $fill16 (i32.const 0) (i32.const 1021))
    (call $fill16 (i32.const 16384) (i32.const 1021))
    (call $ssat16 (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021)
                  (i32.const 1) (i32.const -32768) (i32.const 32767))
    (call $h16 (i32.const 32768) (i32.const 1021)))
  ;; a range one short of the lane's is a clamp, not SQADD
  (func (export "ssat16short") (result i32)
    (call $fill16 (i32.const 0) (i32.const 1021))
    (call $fill16 (i32.const 16384) (i32.const 1021))
    (call $ssat16 (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021)
                  (i32.const 0) (i32.const -32768) (i32.const 32766))
    (call $h16 (i32.const 32768) (i32.const 1021)))

  ;; and halfwords loaded unsigned are not the lane's signed values
  (func $ssat16u (param $d i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $o i32) (local $s i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 1)))
      (local.set $s (i32.add (i32.load16_u (i32.add (local.get $a) (local.get $o)))
                             (i32.load16_u (i32.add (local.get $b) (local.get $o)))))
      (local.set $s (select (local.get $s) (i32.const 32767)
                            (i32.lt_s (local.get $s) (i32.const 32767))))
      (i32.store16 (i32.add (local.get $d) (local.get $o))
        (select (local.get $s) (i32.const -32768)
                (i32.gt_s (local.get $s) (i32.const -32768))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "ssat16u") (result i32)
    (call $fill16 (i32.const 0) (i32.const 1021))
    (call $fill16 (i32.const 16384) (i32.const 1021))
    (call $ssat16u (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021))
    (call $h16 (i32.const 32768) (i32.const 1021)))

  ;; bytes: max first, then min, of a difference
  (func $ssub8 (param $d i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $s i32)
    (loop $l
      (local.set $s (i32.sub (i32.load8_s (i32.add (local.get $a) (local.get $i)))
                             (i32.load8_s (i32.add (local.get $b) (local.get $i)))))
      (local.set $s (select (local.get $s) (i32.const -128)
                            (i32.gt_s (local.get $s) (i32.const -128))))
      (i32.store8 (i32.add (local.get $d) (local.get $i))
        (select (i32.const 127) (local.get $s)
                (i32.gt_s (local.get $s) (i32.const 127))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "ssub8") (result i32)
    (call $fill8 (i32.const 0) (i32.const 1021))
    (call $fill8 (i32.const 16384) (i32.const 1021))
    (call $ssub8 (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021))
    (call $h8 (i32.const 32768) (i32.const 1021)))

  ;; ---- selects --------------------------------------------------------

  ;; a byte select on an equality of bytes, which the lane can make ...
  (func $bsel8 (param $d i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $x i32) (local $y i32)
    (loop $l
      (local.set $x (i32.load8_u (i32.add (local.get $a) (local.get $i))))
      (local.set $y (i32.load8_u (i32.add (local.get $b) (local.get $i))))
      (i32.store8 (i32.add (local.get $d) (local.get $i))
        (select (local.get $x) (i32.add (local.get $y) (i32.const 3))
                (i32.eq (local.get $x) (i32.and (local.get $y) (i32.const 0xf3)))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "bsel8") (result i32)
    (call $fill8 (i32.const 0) (i32.const 1021))
    (call $fill8 (i32.const 16384) (i32.const 1021))
    (call $bsel8 (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021))
    (call $h8 (i32.const 32768) (i32.const 1021)))
  ;; ... and one on a compare with a value a byte does not hold: y ^ 0x1f3
  ;; is never a byte, though its low byte is y ^ 0xf3
  (func $bsel8wide (param $d i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $x i32) (local $y i32)
    (loop $l
      (local.set $x (i32.load8_u (i32.add (local.get $a) (local.get $i))))
      (local.set $y (i32.load8_u (i32.add (local.get $b) (local.get $i))))
      (i32.store8 (i32.add (local.get $d) (local.get $i))
        (select (local.get $x) (i32.add (local.get $y) (i32.const 3))
                (i32.eq (local.get $x) (i32.xor (local.get $y) (i32.const 0x1f3)))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "bsel8wide") (result i32)
    (call $fill8 (i32.const 0) (i32.const 1021))
    (call $fill8 (i32.const 16384) (i32.const 1021))
    (call $bsel8wide (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021))
    (call $h8 (i32.const 32768) (i32.const 1021)))

  ;; f32::min and max the way Rust's minnum comes out of LLVM:
  ;; t = y != y ? x : y; u = x != x ? t : x; min = t < u ? t : u, max = t > u ? t : u
  (func $fmm32 (param $d i32) (param $e i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $o i32) (local $x f32) (local $y f32) (local $t f32) (local $u f32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 2)))
      (local.set $x (f32.load (i32.add (local.get $a) (local.get $o))))
      (local.set $y (f32.load (i32.add (local.get $b) (local.get $o))))
      (local.set $t (select (local.get $x) (local.get $y) (f32.ne (local.get $y) (local.get $y))))
      (local.set $u (select (local.get $t) (local.get $x) (f32.ne (local.get $x) (local.get $x))))
      (f32.store (i32.add (local.get $d) (local.get $o))
        (select (local.get $t) (local.get $u) (f32.lt (local.get $t) (local.get $u))))
      (f32.store (i32.add (local.get $e) (local.get $o))
        (select (local.get $t) (local.get $u) (f32.gt (local.get $t) (local.get $u))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "fmm32") (result i32)
    (call $fillf (i32.const 0) (i32.const 1021))
    (call $fillf (i32.const 16384) (i32.const 1021))
    (call $fmm32 (i32.const 32768) (i32.const 49152) (i32.const 0) (i32.const 16384)
                 (i32.const 1021))
    (i32.add (call $h32 (i32.const 32768) (i32.const 1021))
             (i32.mul (call $h32 (i32.const 49152) (i32.const 1021)) (i32.const 7))))

  (func $fmm64 (param $d i32) (param $e i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $o i32) (local $x f64) (local $y f64) (local $t f64) (local $u f64)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 3)))
      (local.set $x (f64.load (i32.add (local.get $a) (local.get $o))))
      (local.set $y (f64.load (i32.add (local.get $b) (local.get $o))))
      (local.set $t (select (local.get $x) (local.get $y) (f64.ne (local.get $y) (local.get $y))))
      (local.set $u (select (local.get $t) (local.get $x) (f64.ne (local.get $x) (local.get $x))))
      (f64.store (i32.add (local.get $d) (local.get $o))
        (select (local.get $t) (local.get $u) (f64.le (local.get $t) (local.get $u))))
      (f64.store (i32.add (local.get $e) (local.get $o))
        (select (local.get $t) (local.get $u) (f64.ge (local.get $t) (local.get $u))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "fmm64") (result i64)
    (call $fillf64 (i32.const 0) (i32.const 1021))
    (call $fillf64 (i32.const 16384) (i32.const 1021))
    (call $fmm64 (i32.const 32768) (i32.const 65536) (i32.const 0) (i32.const 16384)
                 (i32.const 1021))
    (i64.add (call $h64 (i32.const 32768) (i32.const 1021))
             (i64.mul (call $h64 (i32.const 65536) (i32.const 1021)) (i64.const 7))))

  ;; f32 compares as i32 values, beside f32 loads: x < y, x != x, x == y and
  ;; x >= y weighted 1, 2, 4, 8
  (func $fcmpval (param $d i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $o i32) (local $x f32) (local $y f32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 2)))
      (local.set $x (f32.load (i32.add (local.get $a) (local.get $o))))
      (local.set $y (f32.load (i32.add (local.get $b) (local.get $o))))
      (i32.store (i32.add (local.get $d) (local.get $o))
        (i32.add
          (i32.add (f32.lt (local.get $x) (local.get $y))
                   (i32.shl (f32.ne (local.get $x) (local.get $x)) (i32.const 1)))
          (i32.add (i32.shl (f32.eq (local.get $x) (local.get $y)) (i32.const 2))
                   (i32.shl (f32.ge (local.get $x) (local.get $y)) (i32.const 3)))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "fcmpval") (result i32)
    (call $fillf (i32.const 0) (i32.const 1021))
    (call $fillf (i32.const 16384) (i32.const 1021))
    (call $fcmpval (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021))
    (call $h32 (i32.const 32768) (i32.const 1021)))

  ;; an i32 compare choosing between f32s
  (func $iselect (param $d i32) (param $k i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $o i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 2)))
      (f32.store (i32.add (local.get $d) (local.get $o))
        (select (f32.load (i32.add (local.get $a) (local.get $o)))
                (f32.load (i32.add (local.get $b) (local.get $o)))
                (i32.gt_s (i32.load (i32.add (local.get $k) (local.get $o))) (i32.const 100))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "iselect") (result i32)
    (call $fill32 (i32.const 0) (i32.const 1021))
    (call $fillf (i32.const 16384) (i32.const 1021))
    (call $fillf (i32.const 65536) (i32.const 1021))
    (call $iselect (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 65536)
                   (i32.const 1021))
    (call $h32 (i32.const 32768) (i32.const 1021)))

  ;; an f32 merge of two arms on an f32 compare
  (func $fmerge (param $d i32) (param $a i32) (param $n i32)
    (local $i i32) (local $o i32) (local $x f32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 2)))
      (local.set $x (f32.load (i32.add (local.get $a) (local.get $o))))
      (f32.store (i32.add (local.get $d) (local.get $o))
        (if (result f32) (f32.lt (local.get $x) (f32.const 0))
          (then (f32.neg (local.get $x)))
          (else (f32.abs (local.get $x)))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "fmerge") (result i32)
    (call $fillf (i32.const 0) (i32.const 1021))
    (call $fmerge (i32.const 32768) (i32.const 0) (i32.const 1021))
    (call $h32 (i32.const 32768) (i32.const 1021)))

  ;; ---- high halves of products ------------------------------------------

  ;; (x * y) >> 16 on halfwords, of the kind $sgn says and with $sh
  (func $mulhi16 (param $d i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $o i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 1)))
      (i32.store16 (i32.add (local.get $d) (local.get $o))
        (i32.shr_u (i32.mul (i32.load16_u (i32.add (local.get $a) (local.get $o)))
                            (i32.load16_u (i32.add (local.get $b) (local.get $o))))
                   (i32.const 16)))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "mulhi16u") (result i32)
    (call $fill16 (i32.const 0) (i32.const 1021))
    (call $fill16 (i32.const 16384) (i32.const 1021))
    (call $mulhi16 (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021))
    (call $h16 (i32.const 32768) (i32.const 1021)))

  (func $mulhi16s (param $d i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $o i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 1)))
      (i32.store16 (i32.add (local.get $d) (local.get $o))
        (i32.shr_s (i32.mul (i32.load16_s (i32.add (local.get $a) (local.get $o)))
                            (i32.load16_s (i32.add (local.get $b) (local.get $o))))
                   (i32.const 16)))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "mulhi16s") (result i32)
    (call $fill16 (i32.const 0) (i32.const 1021))
    (call $fill16 (i32.const 16384) (i32.const 1021))
    (call $mulhi16s (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021))
    (call $h16 (i32.const 32768) (i32.const 1021)))

  ;; unsigned operands with a signed shift: the product can be negative as an
  ;; i32, but the 16 bits kept are its upper half either way
  (func $mulhi16shrs (param $d i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $o i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 1)))
      (i32.store16 (i32.add (local.get $d) (local.get $o))
        (i32.shr_s (i32.mul (i32.load16_u (i32.add (local.get $a) (local.get $o)))
                            (i32.load16_u (i32.add (local.get $b) (local.get $o))))
                   (i32.const 16)))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "mulhi16shrs") (result i32)
    (call $fill16 (i32.const 0) (i32.const 1021))
    (call $fill16 (i32.const 16384) (i32.const 1021))
    (call $mulhi16shrs (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021))
    (call $h16 (i32.const 32768) (i32.const 1021)))

  ;; one signed operand and one unsigned: neither multiply is the product
  (func $mulhimixed (param $d i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $o i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 1)))
      (i32.store16 (i32.add (local.get $d) (local.get $o))
        (i32.shr_u (i32.mul (i32.load16_s (i32.add (local.get $a) (local.get $o)))
                            (i32.load16_s (i32.add (local.get $b) (local.get $o))))
                   (i32.const 17)))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "mulhi17") (result i32)
    (call $fill16 (i32.const 0) (i32.const 1021))
    (call $fill16 (i32.const 16384) (i32.const 1021))
    (call $mulhimixed (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021))
    (call $h16 (i32.const 32768) (i32.const 1021)))

  (func $mulhimix2 (param $d i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $o i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 1)))
      (i32.store16 (i32.add (local.get $d) (local.get $o))
        (i32.shr_u (i32.mul (i32.load16_s (i32.add (local.get $a) (local.get $o)))
                            (i32.load16_u (i32.add (local.get $b) (local.get $o))))
                   (i32.const 16)))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "mulhimixed") (result i32)
    (call $fill16 (i32.const 0) (i32.const 1021))
    (call $fill16 (i32.const 16384) (i32.const 1021))
    (call $mulhimix2 (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021))
    (call $h16 (i32.const 32768) (i32.const 1021)))

  ;; Q15: the shift is one short of the lane
  (func $mulhi15 (param $d i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32) (local $o i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 1)))
      (i32.store16 (i32.add (local.get $d) (local.get $o))
        (i32.shr_s (i32.mul (i32.load16_s (i32.add (local.get $a) (local.get $o)))
                            (i32.load16_s (i32.add (local.get $b) (local.get $o))))
                   (i32.const 15)))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "mulhi15") (result i32)
    (call $fill16 (i32.const 0) (i32.const 1021))
    (call $fill16 (i32.const 16384) (i32.const 1021))
    (call $mulhi15 (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021))
    (call $h16 (i32.const 32768) (i32.const 1021)))

  (func $mulhi8 (param $d i32) (param $a i32) (param $b i32) (param $n i32)
    (local $i i32)
    (loop $l
      (i32.store8 (i32.add (local.get $d) (local.get $i))
        (i32.shr_u (i32.mul (i32.load8_u (i32.add (local.get $a) (local.get $i)))
                            (i32.load8_u (i32.add (local.get $b) (local.get $i))))
                   (i32.const 8)))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "mulhi8u") (result i32)
    (call $fill8 (i32.const 0) (i32.const 1021))
    (call $fill8 (i32.const 16384) (i32.const 1021))
    (call $mulhi8 (i32.const 32768) (i32.const 0) (i32.const 16384) (i32.const 1021))
    (call $h8 (i32.const 32768) (i32.const 1021)))

  ;; ---- divisions by constants -------------------------------------------

  (func $div10 (param $d i32) (param $a i32) (param $n i32)
    (local $i i32) (local $o i32) (local $x i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 2)))
      (local.set $x (i32.load (i32.add (local.get $a) (local.get $o))))
      (i32.store (i32.add (local.get $d) (local.get $o))
        (i32.add (i32.div_u (local.get $x) (i32.const 10))
                 (i32.mul (i32.rem_u (local.get $x) (i32.const 1000)) (i32.const 5))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "div10") (result i32)
    (call $fill32 (i32.const 0) (i32.const 1021))
    (call $div10 (i32.const 32768) (i32.const 0) (i32.const 1021))
    (call $h32 (i32.const 32768) (i32.const 1021)))

  ;; by 7, whose magic number needs a 33rd bit
  (func $div7 (param $d i32) (param $a i32) (param $n i32)
    (local $i i32) (local $o i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 2)))
      (i32.store (i32.add (local.get $d) (local.get $o))
        (i32.div_u (i32.load (i32.add (local.get $a) (local.get $o))) (i32.const 7)))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "div7") (result i32)
    (call $fill32 (i32.const 0) (i32.const 1021))
    (call $div7 (i32.const 32768) (i32.const 0) (i32.const 1021))
    (call $h32 (i32.const 32768) (i32.const 1021)))

  ;; ---- conversions ------------------------------------------------------

  ;; int32(float32(a[i]) * 1.5 + 0.5) as TinyGo writes it: trunc_sat and
  ;; its own saturation, NaN to the minimum
  (func $cvt (param $d i32) (param $a i32) (param $n i32)
    (local $i i32) (local $o i32) (local $v f32) (local $t i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 2)))
      (local.set $v (f32.add (f32.mul (f32.convert_i32_s
                                        (i32.load (i32.add (local.get $a) (local.get $o))))
                                      (f32.const 1.5))
                             (f32.const 0.5)))
      (local.set $t (i32.trunc_sat_f32_s (local.get $v)))
      (local.set $t (select (local.get $t) (i32.const 0x7fffffff)
                            (f32.le (local.get $v) (f32.const 2147483520))))
      (local.set $t (select (local.get $t) (i32.const 0x80000000)
                            (f32.ge (local.get $v) (f32.const -2147483648))))
      (i32.store (i32.add (local.get $d) (local.get $o)) (local.get $t))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "cvt") (result i32)
    (call $fill32 (i32.const 0) (i32.const 1021))
    (call $cvt (i32.const 32768) (i32.const 0) (i32.const 1021))
    (call $h32 (i32.const 32768) (i32.const 1021)))

  (func $cvtu (param $d i32) (param $a i32) (param $n i32)
    (local $i i32) (local $o i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 2)))
      (i32.store (i32.add (local.get $d) (local.get $o))
        (i32.trunc_sat_f32_u
          (f32.sub (f32.mul (f32.convert_i32_u
                              (i32.load (i32.add (local.get $a) (local.get $o))))
                            (f32.const 1.25))
                   (f32.const 1e9))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "cvtu") (result i32)
    (call $fill32 (i32.const 0) (i32.const 1021))
    (call $cvtu (i32.const 32768) (i32.const 0) (i32.const 1021))
    (call $h32 (i32.const 32768) (i32.const 1021)))

  ;; i32 loads and f32 stores, and the other way round
  (func $i2f (param $d i32) (param $a i32) (param $n i32) (param $u i32)
    (local $i i32) (local $o i32) (local $x i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 2)))
      (local.set $x (i32.load (i32.add (local.get $a) (local.get $o))))
      (f32.store (i32.add (local.get $d) (local.get $o))
        (select (f32.convert_i32_u (local.get $x)) (f32.convert_i32_s (local.get $x))
                (local.get $u)))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "i2f") (result i32)
    (call $fill32 (i32.const 0) (i32.const 1021))
    (call $i2f (i32.const 32768) (i32.const 0) (i32.const 1021) (i32.const 0))
    (call $h32 (i32.const 32768) (i32.const 1021)))
  (func (export "u2f") (result i32)
    (call $fill32 (i32.const 0) (i32.const 1021))
    (call $i2f (i32.const 32768) (i32.const 0) (i32.const 1021) (i32.const 1))
    (call $h32 (i32.const 32768) (i32.const 1021)))

  (func $f2i (param $d i32) (param $a i32) (param $n i32)
    (local $i i32) (local $o i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 2)))
      (i32.store (i32.add (local.get $d) (local.get $o))
        (i32.trunc_sat_f32_s (f32.load (i32.add (local.get $a) (local.get $o)))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "f2i") (result i32)
    (call $fillf (i32.const 0) (i32.const 1021))
    (call $f2i (i32.const 32768) (i32.const 0) (i32.const 1021))
    (call $h32 (i32.const 32768) (i32.const 1021)))

  (func $f2u (param $d i32) (param $a i32) (param $n i32)
    (local $i i32) (local $o i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 2)))
      (i32.store (i32.add (local.get $d) (local.get $o))
        (i32.trunc_sat_f32_u (f32.load (i32.add (local.get $a) (local.get $o)))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "f2u") (result i32)
    (call $fillf (i32.const 0) (i32.const 1021))
    (call $f2u (i32.const 32768) (i32.const 0) (i32.const 1021))
    (call $h32 (i32.const 32768) (i32.const 1021)))

  ;; from an i64 that a 32-bit load sign-extended: the same value, but no
  ;; conversion a lane has
  (func $i64tof (param $d i32) (param $a i32) (param $n i32)
    (local $i i32) (local $o i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 2)))
      (f32.store (i32.add (local.get $d) (local.get $o))
        (f32.convert_i64_s (i64.load32_s (i32.add (local.get $a) (local.get $o)))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "i64tof") (result i32)
    (call $fill32 (i32.const 0) (i32.const 1021))
    (call $i64tof (i32.const 32768) (i32.const 0) (i32.const 1021))
    (call $h32 (i32.const 32768) (i32.const 1021)))

  ;; to i64, of which 32 bits are kept: 3e9 is 0xb2d05e00, not i32's
  ;; saturated 0x7fffffff
  (func $f2i64 (param $d i32) (param $a i32) (param $n i32)
    (local $i i32) (local $o i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 2)))
      (i64.store32 (i32.add (local.get $d) (local.get $o))
        (i64.trunc_sat_f32_s (f32.load (i32.add (local.get $a) (local.get $o)))))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n)))))
  (func (export "f2i64") (result i32)
    (call $fillf (i32.const 0) (i32.const 1021))
    (call $f2i64 (i32.const 32768) (i32.const 0) (i32.const 1021))
    (call $h32 (i32.const 32768) (i32.const 1021)))

  ;; f32 loads, i32 stores and an i32 sum: mixed lanes with a reduction
  (func $f2isum (param $d i32) (param $a i32) (param $n i32) (result i32)
    (local $i i32) (local $o i32) (local $t i32) (local $s i32)
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 2)))
      (local.set $t (i32.trunc_sat_f32_s (f32.load (i32.add (local.get $a) (local.get $o)))))
      (i32.store (i32.add (local.get $d) (local.get $o)) (local.get $t))
      (local.set $s (i32.add (local.get $s) (local.get $t)))
      (br_if $l (i32.ne (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                        (local.get $n))))
    (local.get $s))
  (func (export "f2isum") (result i32)
    (call $fillf (i32.const 0) (i32.const 1021))
    (i32.add (call $f2isum (i32.const 32768) (i32.const 0) (i32.const 1021))
             (call $h32 (i32.const 32768) (i32.const 1021))))

  ;; ---- exits a dominating test decides ----------------------------------

  ;; i == len is a bounds check before each element, and the loop goes on
  ;; while i != 1020 before stepping, so with len 1021 the check cannot fail;
  ;; with len 700 it fails at 700, and with len 1020 at the last element
  (func $bounds (param $d i32) (param $a i32) (param $len i32) (result i32)
    (local $i i32) (local $j i32) (local $o i32)
    (block $oob
      (loop $l
        (br_if $oob (i32.eq (local.get $i) (local.get $len)))
        (local.set $o (i32.shl (local.get $i) (i32.const 2)))
        (i32.store (i32.add (local.get $d) (local.get $o))
          (i32.add (i32.mul (i32.load (i32.add (local.get $a) (local.get $o))) (i32.const 3))
                   (i32.const 1)))
        (local.set $j (local.get $i))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.ne (local.get $j) (i32.const 1020))))
      (return (i32.const -1)))
    (local.get $i))
  (func (export "bounds") (result i32)
    (call $fill32 (i32.const 0) (i32.const 1021))
    (i32.add (i32.mul (call $bounds (i32.const 32768) (i32.const 0) (i32.const 1021))
                      (i32.const 1000003))
             (call $h32 (i32.const 32768) (i32.const 1021))))
  (func (export "bounds_live") (result i32)
    (call $fill32 (i32.const 0) (i32.const 1021))
    (call $fill32 (i32.const 32768) (i32.const 1021))
    (i32.add (i32.mul (call $bounds (i32.const 32768) (i32.const 0) (i32.const 700))
                      (i32.const 1000003))
             (call $h32 (i32.const 32768) (i32.const 1021))))
  (func (export "bounds_last") (result i32)
    (call $fill32 (i32.const 0) (i32.const 1021))
    (call $fill32 (i32.const 32768) (i32.const 1021))
    (i32.add (i32.mul (call $bounds (i32.const 32768) (i32.const 0) (i32.const 1020))
                      (i32.const 1000003))
             (call $h32 (i32.const 32768) (i32.const 1021))))

  ;; down from n - 1, tested against zero before the decrement it carries
  (func $down (param $d i32) (param $a i32) (param $n i32)
    (local $i i32) (local $j i32) (local $o i32)
    (local.set $i (i32.sub (local.get $n) (i32.const 1)))
    (loop $l
      (local.set $o (i32.shl (local.get $i) (i32.const 2)))
      (i32.store (i32.add (local.get $d) (local.get $o))
        (i32.xor (i32.load (i32.add (local.get $a) (local.get $o))) (i32.const 0x5a5a5a5a)))
      (local.set $j (local.get $i))
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (br_if $l (i32.ne (local.get $j) (i32.const 0)))))
  (func (export "down") (result i32)
    (call $fill32 (i32.const 0) (i32.const 1021))
    (call $down (i32.const 32768) (i32.const 0) (i32.const 1021))
    (call $h32 (i32.const 32768) (i32.const 1021)))
)
