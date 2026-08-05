;; Vector rotates, which AArch64 does not have.
;;
;; Hand-written, and it stays hand-written: test/gen_cases.py does not emit a
;; file of this name, and run_cases.py globs the directory afterwards, so this
;; is run against wasm-interp like every generated case is.
;;
;; What it is for.  A producer writes a rotate as `(v << k) | (v >> (32-k))`
;; and LLVM folds it back into one `i32.rotl`, so by the time warmc sees it
;; the two shifts are gone.  There is no vector rotate instruction, so the
;; vectoriser takes it apart again -- and the amounts it computes are where
;; this can go wrong:
;;
;;   * an amount of zero must be the identity, not a shift by the lane width,
;;     which on AArch64 does not encode and on a shift-by-32 would give zero;
;;   * wasm reduces the amount modulo the scalar width, so 32 means 0 and 33
;;     means 1 -- a lowering that passed the raw amount through would shift by
;;     more than the lane holds;
;;   * rotr is rotl by the complement, and getting the complement backwards
;;     produces a plausible-looking wrong answer;
;;   * a lane narrower than the scalar type must be refused outright, because
;;     the bits an i32 rotate brings round come from outside an i8 lane.
;;
;; The data is deliberately extreme -- 0, 1, the sign bit, all ones -- because
;; a rotate of a middling value by a middling amount is wrong in ways that
;; still look like a number.  1021 elements, so the scalar tail runs.
(module
  (memory 2)

  ;; a[] at 0: 1021 i32 whose values cycle through the awkward ones.
  (func $fill (result i32)
    (local $i i32) (local $v i32)
    (loop $F
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $v
            (select (i32.const 0)
              (select (i32.const 1)
                (select (i32.const -2147483648)
                  (select (i32.const -1)
                    (i32.mul (local.get $i) (i32.const 2654435761))
                    (i32.eq (i32.rem_u (local.get $i) (i32.const 7)) (i32.const 3)))
                  (i32.eq (i32.rem_u (local.get $i) (i32.const 7)) (i32.const 2)))
                (i32.eq (i32.rem_u (local.get $i) (i32.const 7)) (i32.const 1)))
              (i32.eq (i32.rem_u (local.get $i) (i32.const 7)) (i32.const 0))))
          (i32.store (i32.mul (local.get $i) (i32.const 4)) (local.get $v))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $F))))
    (i32.const 1021))

  ;; A checksum of b[] that depends on *where* each value is, not just on the
  ;; multiset of values.  A plain sum will not do, and finding that out cost a
  ;; round: shifting 2D lanes where 4S was meant moves the bit crossing the
  ;; 32-bit boundary into the neighbouring word instead of round to the bottom
  ;; of its own, and the two words' sums come out identical --
  ;; (lo<<1)+(hi<<1)+(hi>>31)+(lo>>31) either way. That is exactly the bug an
  ;; out-of-range shift immediate causes, because the overflow lands in the
  ;; immh field that selects the lane width. A polynomial hash sees it.
  (func $sumb (param $n i32) (result i32)
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

  (func (export "r_rotl3") (result i32)
    (local $i i32) (local $n i32)
    (local.set $n (call $fill))
    (loop $L
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then
          (i32.store (i32.add (i32.const 8192) (i32.mul (local.get $i) (i32.const 4)))
            (i32.rotl (i32.load (i32.mul (local.get $i) (i32.const 4)))
                      (i32.const 3)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (call $sumb (local.get $n)))

  ;; Zero: the identity.  A lowering that emitted `shl 0` and `ushr 32` would
  ;; return the sum of zeroes here, because a shift by the lane width does not
  ;; encode and clears the lane if it is forced through.
  (func (export "r_rotl0") (result i32)
    (local $i i32) (local $n i32)
    (local.set $n (call $fill))
    (loop $L
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then
          (i32.store (i32.add (i32.const 8192) (i32.mul (local.get $i) (i32.const 4)))
            (i32.rotl (i32.load (i32.mul (local.get $i) (i32.const 4)))
                      (i32.const 0)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (call $sumb (local.get $n)))

  ;; One short of a full turn, the largest amount that is not zero.
  (func (export "r_rotl31") (result i32)
    (local $i i32) (local $n i32)
    (local.set $n (call $fill))
    (loop $L
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then
          (i32.store (i32.add (i32.const 8192) (i32.mul (local.get $i) (i32.const 4)))
            (i32.rotl (i32.load (i32.mul (local.get $i) (i32.const 4)))
                      (i32.const 31)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (call $sumb (local.get $n)))

  ;; A full turn and one more.  wasm reduces modulo 32, so this is a rotate by
  ;; one; a lowering that passed 33 through would shift past the lane.
  (func (export "r_rotl33") (result i32)
    (local $i i32) (local $n i32)
    (local.set $n (call $fill))
    (loop $L
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then
          (i32.store (i32.add (i32.const 8192) (i32.mul (local.get $i) (i32.const 4)))
            (i32.rotl (i32.load (i32.mul (local.get $i) (i32.const 4)))
                      (i32.const 33)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (call $sumb (local.get $n)))

  ;; A full turn exactly.  wasm reduces the amount modulo 32, so this is the
  ;; identity -- and it is the only case that reaches the zero-amount path,
  ;; because `i32.rotl(x, 0)` written literally is folded away long before the
  ;; vectoriser sees it.  Without the reduction the lowering would shift by 32
  ;; and by 0, neither of which encodes as intended.
  (func (export "r_rotl32") (result i32)
    (local $i i32) (local $n i32)
    (local.set $n (call $fill))
    (loop $L
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then
          (i32.store (i32.add (i32.const 8192) (i32.mul (local.get $i) (i32.const 4)))
            (i32.rotl (i32.load (i32.mul (local.get $i) (i32.const 4)))
                      (i32.const 32)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (call $sumb (local.get $n)))

  ;; The other direction.  rotr 7 is rotl 25, and swapping the complement is
  ;; the mistake this catches -- rotl 7 is a different number entirely.
  (func (export "r_rotr7") (result i32)
    (local $i i32) (local $n i32)
    (local.set $n (call $fill))
    (loop $L
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then
          (i32.store (i32.add (i32.const 8192) (i32.mul (local.get $i) (i32.const 4)))
            (i32.rotr (i32.load (i32.mul (local.get $i) (i32.const 4)))
                      (i32.const 7)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (call $sumb (local.get $n)))

  ;; 64-bit lanes: two per vector, and the shift amounts are modulo 64.
  (func (export "r_rotl_i64") (result i64)
    (local $i i32) (local $s i64)
    (loop $F
      (if (i32.lt_u (local.get $i) (i32.const 509))
        (then
          (i64.store (i32.mul (local.get $i) (i32.const 8))
            (i64.mul (i64.extend_i32_u (local.get $i))
                     (i64.const -7046029254386353131)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $F))))
    (local.set $i (i32.const 0))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 509))
        (then
          (i64.store (i32.add (i32.const 8192) (i32.mul (local.get $i) (i32.const 8)))
            (i64.rotl (i64.load (i32.mul (local.get $i) (i32.const 8)))
                      (i64.const 11)))
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

  ;; The narrow case, which must be refused.  The rotate is an i32 one and the
  ;; result is kept in a byte, so the bits it brings round the top come from
  ;; the three bytes above the lane -- bits a byte-lane vector does not have.
  ;; Rotating in byte lanes instead returns a different number.
  (func (export "r_rotl_narrow") (result i32)
    (local $i i32) (local $s i32)
    (loop $F
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (i32.store8 (local.get $i)
            (i32.add (i32.mul (local.get $i) (i32.const 37)) (i32.const 11)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $F))))
    (local.set $i (i32.const 0))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (i32.store8 (i32.add (i32.const 8192) (local.get $i))
            (i32.rotl (i32.load8_u (local.get $i)) (i32.const 3)))
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

  ;; A rotate whose amount is not the same in every lane, which no vector
  ;; shift can express.  Here to be refused.
  (func (export "r_rotl_var") (result i32)
    (local $i i32) (local $n i32)
    (local.set $n (call $fill))
    (loop $L
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then
          (i32.store (i32.add (i32.const 8192) (i32.mul (local.get $i) (i32.const 4)))
            (i32.rotl (i32.load (i32.mul (local.get $i) (i32.const 4)))
                      (i32.and (local.get $i) (i32.const 31))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (call $sumb (local.get $n)))
)
