;; Reductions whose accumulator is wider than the values being summed.
;;
;; Hand-written, and it stays hand-written: test/gen_cases.py does not emit a
;; file of this name, and run_cases.py globs the directory afterwards, so this
;; is run against wasm-interp like every generated case is.
;;
;; What it is for.  `s += uint64(a[i])` over a []uint32 and `n += b[i]` over a
;; []byte are the two shapes a Go program writes constantly, and the vector
;; form of either has to *widen*: sixteen bytes come in as one vector and have
;; to land in four i32 accumulators, not one i8 accumulator that wraps at 256.
;; Every case here is sized so that accumulating in the narrow lane gives a
;; different answer from accumulating in the wide one -- a vectoriser that
;; forgets to widen does not merely run fast, it returns the wrong number, and
;; nothing else in the suite sums enough narrow values to notice.
;;
;; The odd lengths are deliberate.  A vector body handles whole vectors and a
;; scalar tail handles the rest, so a length that is not a multiple of the
;; width is what proves the tail runs and that it starts where the vector body
;; stopped.
(module
  (memory 1)

  ;; 4096 bytes of u32 at 0, each 0x01020304 -- summing 1024 of them as u32
  ;; wraps (1024 * 0x01020304 > 2^32) and as u64 does not.
  (func $fill32 (param $n i32)
    (local $i i32)
    (loop $L
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then
          (i32.store (i32.mul (local.get $i) (i32.const 4))
                     (i32.const 0x01020304))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L)))))

  ;; Bytes at 8192, all 0xff: summing more than 257 of them overflows a byte
  ;; and more than 65793 overflows a halfword.
  (func $fill8 (param $n i32)
    (local $i i32)
    (loop $L
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then
          (i32.store8 (i32.add (i32.const 8192) (local.get $i))
                      (i32.const 0xff))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L)))))

  ;; u32 -> u64.  1023 is not a multiple of four, so the tail runs.
  (func (export "w_u32_u64") (result i64)
    (local $i i32) (local $s i64)
    (call $fill32 (i32.const 1023))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1023))
        (then
          (local.set $s (i64.add (local.get $s)
            (i64.load32_u (i32.mul (local.get $i) (i32.const 4)))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))

  ;; The same values read *signed*.  0x01020304 is positive so the answer is
  ;; the same number -- what differs is the widening a compiler must use, and
  ;; a signed load widened as unsigned would still pass here.  The negative
  ;; case below is the one that separates them.
  (func (export "w_u32_u64_s") (result i64)
    (local $i i32) (local $s i64)
    (call $fill32 (i32.const 1023))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1023))
        (then
          (local.set $s (i64.add (local.get $s)
            (i64.load32_s (i32.mul (local.get $i) (i32.const 4)))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))

  ;; Negative values read signed: every element is 0xfffffffe, which is -2
  ;; sign-extended and 4294967294 zero-extended.  Widening the wrong way is
  ;; off by 1023 * 2^32 here, which no amount of luck hides.
  (func (export "w_i32_i64_neg") (result i64)
    (local $i i32) (local $s i64)
    (loop $F
      (if (i32.lt_u (local.get $i) (i32.const 1023))
        (then
          (i32.store (i32.mul (local.get $i) (i32.const 4))
                     (i32.const 0xfffffffe))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $F))))
    (local.set $i (i32.const 0))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1023))
        (then
          (local.set $s (i64.add (local.get $s)
            (i64.load32_s (i32.mul (local.get $i) (i32.const 4)))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))

  ;; u8 -> i32, 1021 elements of 0xff.  An i8 accumulator wraps every 257
  ;; elements and an i16 one every 65793; the answer is 260355.
  (func (export "w_u8_i32") (result i32)
    (local $i i32) (local $s i32)
    (call $fill8 (i32.const 1021))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $s (i32.add (local.get $s)
            (i32.load8_u (i32.add (i32.const 8192) (local.get $i)))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))

  ;; The same bytes read signed: 0xff is -1, so the sum is -1021.
  (func (export "w_i8_i32") (result i32)
    (local $i i32) (local $s i32)
    (call $fill8 (i32.const 1021))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $s (i32.add (local.get $s)
            (i32.load8_s (i32.add (i32.const 8192) (local.get $i)))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))

  ;; u16 -> i64, the middle width, and a length that is an exact multiple of
  ;; the vector width so the tail is empty -- the other half of the boundary.
  (func (export "w_u16_i64") (result i64)
    (local $i i32) (local $s i64)
    (loop $F
      (if (i32.lt_u (local.get $i) (i32.const 1024))
        (then
          (i32.store16 (i32.add (i32.const 16384)
                                (i32.mul (local.get $i) (i32.const 2)))
                       (i32.const 0xbeef))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $F))))
    (local.set $i (i32.const 0))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1024))
        (then
          (local.set $s (i64.add (local.get $s)
            (i64.load16_u (i32.add (i32.const 16384)
                                   (i32.mul (local.get $i) (i32.const 2))))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))

  ;; A reduction that is *not* a plain accumulate: the term is scaled first.
  ;; Widening still has to happen, and it has to happen after the multiply,
  ;; which is where an implementation that widens the load and forgets the
  ;; rest of the chain goes wrong.
  (func (export "w_scaled") (result i64)
    (local $i i32) (local $s i64)
    (call $fill32 (i32.const 1023))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1023))
        (then
          (local.set $s (i64.add (local.get $s)
            (i64.mul (i64.const 3)
              (i64.load32_u (i32.mul (local.get $i) (i32.const 4))))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))

  ;; Counting: the term is a *comparison*, not a load.  `n += b[i] == c` over
  ;; bytes is the shape a byte scanner has, and its vector form needs the
  ;; comparison's lane mask -- all ones -- normalised to 0/1 before it is
  ;; widened, because the scalar term is 0 or 1 and 0xff widened is 255.
  ;; 1021 bytes, of which those at multiples of 7 are 42: 146 of them.
  (func $fillmix (param $n i32)
    (local $i i32)
    (loop $L
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then
          (i32.store8 (i32.add (i32.const 24576) (local.get $i))
                      (select (i32.const 42) (i32.const 7)
                              (i32.eqz (i32.rem_u (local.get $i) (i32.const 7)))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L)))))

  (func (export "w_count_eq") (result i32)
    (local $i i32) (local $n i32)
    (call $fillmix (i32.const 1021))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $n (i32.add (local.get $n)
            (i32.eq (i32.load8_u (i32.add (i32.const 24576) (local.get $i)))
                    (i32.const 42))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $n))

  (func (export "w_count_ne") (result i32)
    (local $i i32) (local $n i32)
    (call $fillmix (i32.const 1021))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $n (i32.add (local.get $n)
            (i32.ne (i32.load8_u (i32.add (i32.const 24576) (local.get $i)))
                    (i32.const 42))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $n))

  ;; The adversarial one.  No byte can equal 300, so the answer is 0 -- but
  ;; 300 truncated to a byte is 44, and a comparison performed in byte lanes
  ;; against a constant that does not fit would count every 44 instead.  The
  ;; buffer is full of 44s to make sure that is not a hypothetical: a lane
  ;; comparison that forgets to check the constant fits returns 1021 here.
  (func (export "w_count_wide_const") (result i32)
    (local $i i32) (local $n i32)
    (loop $F
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (i32.store8 (i32.add (i32.const 28672) (local.get $i)) (i32.const 44))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $F))))
    (local.set $i (i32.const 0))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $n (i32.add (local.get $n)
            (i32.eq (i32.load8_u (i32.add (i32.const 28672) (local.get $i)))
                    (i32.const 300))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $n))

  ;; xor rather than add, which must *not* be widened into several
  ;; accumulators the way a sum is: xor of the low halves is not the low half
  ;; of the xor once the lanes are different widths.  Here to be refused.
  (func (export "w_xor") (result i64)
    (local $i i32) (local $s i64)
    (call $fill32 (i32.const 1023))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1023))
        (then
          (local.set $s (i64.xor (local.get $s)
            (i64.load32_u (i32.mul (local.get $i) (i32.const 4)))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))

  ;; ---- widening multiplies ------------------------------------------------
  ;;
  ;; `s += int64(a[i]) * int64(b[i])` is the one shape the rule at the top of
  ;; this file is written around.  Everything else in the vectoriser computes
  ;; narrow lanes and relies on them agreeing with the wide ones on the low
  ;; bits; a product does not, because its high half is exactly what a narrow
  ;; multiply throws away.  SMULL takes two narrow lanes and produces the whole
  ;; product in the wide one, so it is the exception rather than a violation.
  ;;
  ;; Every value below is large enough that the products overflow 32 bits --
  ;; that is the whole point.  A vectoriser that multiplied in i32 lanes and
  ;; widened afterwards returns a different, smaller number.
  (func $fillpair (param $n i32)
    (local $i i32)
    (loop $F
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then
          (i32.store (i32.add (i32.const 32768) (i32.mul (local.get $i) (i32.const 4)))
            (i32.sub (i32.mul (local.get $i) (i32.const 131071)) (i32.const 1000000)))
          (i32.store (i32.add (i32.const 40960) (i32.mul (local.get $i) (i32.const 4)))
            (i32.sub (i32.mul (local.get $i) (i32.const 65521)) (i32.const 2000000)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $F)))))

  (func (export "w_mul_s") (result i64)
    (local $i i32) (local $s i64)
    (call $fillpair (i32.const 1021))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $s (i64.add (local.get $s)
            (i64.mul
              (i64.load32_s (i32.add (i32.const 32768)
                              (i32.mul (local.get $i) (i32.const 4))))
              (i64.load32_s (i32.add (i32.const 40960)
                              (i32.mul (local.get $i) (i32.const 4)))))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))

  ;; The same with UMULL.  Read unsigned, the same bytes are huge, so the sum
  ;; differs from the signed one by far more than a rounding.
  (func (export "w_mul_u") (result i64)
    (local $i i32) (local $s i64)
    (call $fillpair (i32.const 1021))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $s (i64.add (local.get $s)
            (i64.mul
              (i64.load32_u (i32.add (i32.const 32768)
                              (i32.mul (local.get $i) (i32.const 4))))
              (i64.load32_u (i32.add (i32.const 40960)
                              (i32.mul (local.get $i) (i32.const 4)))))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))

  ;; One factor signed and the other not.  There is no mixed SMULL, and taking
  ;; either sign for both gives a different answer, so this must be refused.
  (func (export "w_mul_mixed") (result i64)
    (local $i i32) (local $s i64)
    (call $fillpair (i32.const 1021))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $s (i64.add (local.get $s)
            (i64.mul
              (i64.load32_s (i32.add (i32.const 32768)
                              (i32.mul (local.get $i) (i32.const 4))))
              (i64.load32_u (i32.add (i32.const 40960)
                              (i32.mul (local.get $i) (i32.const 4)))))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))

  ;; Sixteen-bit factors into i32: the same shape one lane width down.
  (func (export "w_mul_16") (result i32)
    (local $i i32) (local $s i32)
    (loop $F
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (i32.store16 (i32.add (i32.const 32768) (i32.mul (local.get $i) (i32.const 2)))
            (i32.sub (i32.mul (local.get $i) (i32.const 277)) (i32.const 20000)))
          (i32.store16 (i32.add (i32.const 40960) (i32.mul (local.get $i) (i32.const 2)))
            (i32.sub (i32.mul (local.get $i) (i32.const 131)) (i32.const 9000)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $F))))
    (local.set $i (i32.const 0))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $s (i32.add (local.get $s)
            (i32.mul
              (i32.load16_s (i32.add (i32.const 32768)
                              (i32.mul (local.get $i) (i32.const 2))))
              (i32.load16_s (i32.add (i32.const 40960)
                              (i32.mul (local.get $i) (i32.const 2)))))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))

  ;; A factor that is not a load -- one of them is scaled first.  The scaling
  ;; happens in the narrow lane, where it overflows, so the product SMULL
  ;; would compute is not the product the scalar loop computes.  Refused.
  ;;
  ;; The multiplier is large on purpose.  An earlier version scaled by three,
  ;; which does not overflow i32 for this data, so the narrow and wide
  ;; products agreed and the case could not fail however the rule was broken.
  (func (export "w_mul_scaled") (result i64)
    (local $i i32) (local $s i64)
    (call $fillpair (i32.const 1021))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $s (i64.add (local.get $s)
            (i64.mul
              (i64.mul (i64.load32_s (i32.add (i32.const 32768)
                                       (i32.mul (local.get $i) (i32.const 4))))
                       (i64.const 1000003))
              (i64.load32_s (i32.add (i32.const 40960)
                              (i32.mul (local.get $i) (i32.const 4)))))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))

  ;; ---- three doublings, and the widening written out ----------------------
  ;;
  ;; A byte lane summed into i64 is three doublings above the loop's lane, one
  ;; more than used to be allowed.  The old lowering carried one accumulator
  ;; per half -- eight of them for one sum, and `strscan` in test/zigbench has
  ;; three such sums in one loop, which is twenty-four vector registers out of
  ;; thirty-two.  The widening now adds its two halves together at each
  ;; doubling instead, which costs one accumulator whatever the depth and is
  ;; exact because a half is twice as wide as the value in it: two values below
  ;; 2^n add to less than 2^2n.
  ;;
  ;; 1021 bytes of 0xff, so 260355 -- a byte accumulator wraps every 257 of
  ;; them and a halfword one every 65793, and both doublings plus the last are
  ;; needed to get the number right.
  (func (export "w_u8_i64") (result i64)
    (local $i i32) (local $s i64)
    (call $fill8 (i32.const 1021))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $s (i64.add (local.get $s)
            (i64.load8_u (i32.add (i32.const 8192) (local.get $i)))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))

  ;; The same sum as an LLVM-based producer writes it: an i32 load and then an
  ;; explicit extend, where TinyGo writes `i64.load8_u` and lets the access
  ;; carry the widening.  Nothing about the loop differs, and every Zig loop in
  ;; test/zigbench was refused until the extend was stripped and the expression
  ;; underneath became the term.
  (func (export "w_ext_u8_i64") (result i64)
    (local $i i32) (local $s i64)
    (call $fill8 (i32.const 1021))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $s (i64.add (local.get $s)
            (i64.extend_i32_u
              (i32.load8_u (i32.add (i32.const 8192) (local.get $i))))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))

  ;; Signed, with both halves agreeing: a signed byte load under a signed
  ;; extend, which is one signed chain from the lane.  0xff is -1, so -1021 --
  ;; an unsigned chain would give 260355.
  (func (export "w_ext_s8_i64") (result i64)
    (local $i i32) (local $s i64)
    (call $fill8 (i32.const 1021))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $s (i64.add (local.get $s)
            (i64.extend_i32_s
              (i32.load8_s (i32.add (i32.const 8192) (local.get $i))))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))

  ;; The mismatch, which is neither chain and has to be refused: a signed byte
  ;; load *zero*-extended is 0xffffffff per element, where a signed chain from
  ;; the byte lane gives -1 and an unsigned one 255.  Three different answers,
  ;; so this case fails whichever way the rule is broken -- which the two above
  ;; taken alone do not, since each of them is right for one of the two chains.
  (func (export "w_ext_u_of_s8") (result i64)
    (local $i i32) (local $s i64)
    (call $fill8 (i32.const 1021))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $s (i64.add (local.get $s)
            (i64.extend_i32_u
              (i32.load8_s (i32.add (i32.const 8192) (local.get $i))))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))

  ;; Counting into an i64, where there is no load to carry the widening at all:
  ;; the term is a comparison with an extend on top.  `spaces += (b == c)` into
  ;; a u64 is this, and it is one of test/zigbench/strscan's three reductions.
  ;; 875 of the 1021 bytes are not 42, which wraps a byte lane.
  (func (export "w_ext_count") (result i64)
    (local $i i32) (local $n i64)
    (call $fillmix (i32.const 1021))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $n (i64.add (local.get $n)
            (i64.extend_i32_u
              (i32.ne (i32.load8_u (i32.add (i32.const 24576) (local.get $i)))
                      (i32.const 42)))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $n))

  ;; `high += b >>u 7`, which is how LLVM writes `b >= 0x80` for an unsigned
  ;; byte and is strscan's third reduction.  A logical right shift is not safe
  ;; in a narrow lane in general -- it brings down bits from above the lane,
  ;; which the lane does not have -- and is safe here because a zero-extended
  ;; byte load has nothing up there.  All 1021 bytes are 0xff, so 1021.
  (func (export "w_ext_shr") (result i64)
    (local $i i32) (local $s i64)
    (call $fill8 (i32.const 1021))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $s (i64.add (local.get $s)
            (i64.extend_i32_u
              (i32.shr_u
                (i32.load8_u (i32.add (i32.const 8192) (local.get $i)))
                (i32.const 7)))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))

  ;; The adversarial one for that rule.  `(b | 0x100) >>u 7` is 3 for every
  ;; byte here, so 3063 -- and in a byte lane, where the 0x100 is not there to
  ;; be shifted down, it would be 1 and the answer 1021.  Refused.
  (func (export "w_ext_shr_wide") (result i64)
    (local $i i32) (local $s i64)
    (call $fill8 (i32.const 1021))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $s (i64.add (local.get $s)
            (i64.extend_i32_u
              (i32.shr_u
                (i32.or
                  (i32.load8_u (i32.add (i32.const 8192) (local.get $i)))
                  (i32.const 0x100))
                (i32.const 7)))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))

  ;; An AND against a mask that fits the lane bounds the term whatever the
  ;; other side holds, which is what lets a computed value be widened at all:
  ;; `(b * 7) & 0x0f` overflows a byte before the mask and is back inside one
  ;; after it, and AND agrees with the wide form on the low bits, so the lane
  ;; holds the answer exactly.
  (func (export "w_ext_and") (result i64)
    (local $i i32) (local $s i64)
    (call $fillmix (i32.const 1021))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $s (i64.add (local.get $s)
            (i64.extend_i32_u
              (i32.and
                (i32.mul (i32.load8_u (i32.add (i32.const 24576) (local.get $i)))
                         (i32.const 7))
                (i32.const 0x0f)))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))

  ;; The same with a mask that does *not* fit a byte, which has to be refused.
  ;; 42 * 7 is 294 and `294 & 0x1ff` is 294; in a byte lane the product is 38
  ;; and the mask cannot bring the ninth bit back, so a lane that skipped the
  ;; check answers 256 lower for each of the 146 bytes that are 42.
  (func (export "w_ext_and_wide") (result i64)
    (local $i i32) (local $s i64)
    (call $fillmix (i32.const 1021))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $s (i64.add (local.get $s)
            (i64.extend_i32_u
              (i32.and
                (i32.mul (i32.load8_u (i32.add (i32.const 24576) (local.get $i)))
                         (i32.const 7))
                (i32.const 0x1ff)))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $s))
)
