;; The scalar unroller clones K-1 copies of a counted loop's body in place
;; and rewires the exit test and the loop edges to the last copy.  The bug
;; this file guards: a copy seeded from this iteration's params instead of
;; the previous copy's outputs -- off by one iteration, and invisible when
;; the body is symmetric.  Every accumulator here is order-sensitive
;; (acc = acc*3 + x) and the data bytes are distinct, so a skipped,
;; repeated, or reordered iteration changes the answer.  The refuses_*
;; twins hold the gates in place; loops that are declined because they
;; would not terminate under the closed form (an NE bound off the step
;; grid, a wrapping counter) cannot appear here at all -- that is the
;; point of declining them.
(module
  (memory 1)
  (data (i32.const 0) "\01\07\02\0b\03\0d\05\11\08\13\0c\17\0e\1d\15\1f")
  (data (i32.const 16) "\2b\29\25\23\1e\1c\1a\16\14\12\10\0f\0a\09\06\04")
  (data (i32.const 63) "\00")

  ;; do-while in the source is a single self-looping block in the IR: the
  ;; test lands in the same block as the body (shape C), trip 16, step 1.
  (func (export "fires_selfloop") (result i32)
    (local $i i32) (local $acc i32)
    (loop $l
      (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
                               (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.ne (local.get $i) (i32.const 16))))
    (local.get $acc))

  ;; while + a body that touches memory: the rotate pass clones the header
  ;; test into a latch, leaving the two-block bottom-tested shape (A).
  (func (export "fires_rotated") (result i32)
    (local $i i32) (local $acc i32)
    (block $x
      (loop $l
        (br_if $x (i32.eq (local.get $i) (i32.const 32)))
        (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
                                 (i32.load8_u (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $acc))

  ;; while + a memory-free body: rotate declines it, so the test stays in
  ;; the header (shape B) and the params already hold the exit values.
  (func (export "fires_toptest") (result i32)
    (local $i i32) (local $acc i32)
    (block $x
      (loop $l
        (br_if $x (i32.ge_u (local.get $i) (i32.const 24)))
        (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
                                 (i32.xor (local.get $i) (i32.const 21))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $acc))

  ;; step 4 with an NE bound on the step grid: 64/4 = 16 iterations.
  (func (export "fires_ne_step4") (result i32)
    (local $i i32) (local $acc i32)
    (loop $l
      (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
                               (i32.load8_u (i32.shr_u (local.get $i)
                                                       (i32.const 2)))))
      (local.set $i (i32.add (local.get $i) (i32.const 4)))
      (br_if $l (i32.ne (local.get $i) (i32.const 64))))
    (local.get $acc))

  ;; downward counter against a strict signed test, trip 12.
  (func (export "fires_down") (result i32)
    (local $i i32) (local $acc i32)
    (local.set $i (i32.const 12))
    (loop $l
      (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
                               (i32.load8_u (local.get $i))))
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (br_if $l (i32.gt_s (local.get $i) (i32.const 0))))
    (local.get $acc))

  ;; inclusive bound: i <= 15 runs 16 bodies, one more than the strict twin.
  (func (export "fires_rel_le") (result i32)
    (local $i i32) (local $acc i32)
    (loop $l
      (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
                               (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.le_s (local.get $i) (i32.const 15))))
    (local.get $acc))

  ;; an unsigned counter grazing the top of the domain: 0xffffffec by 4
  ;; below 0xfffffffc is 4 iterations and the final value does not wrap.
  (func (export "fires_near_wrap") (result i32)
    (local $i i32) (local $acc i32)
    (local.set $i (i32.const 0xffffffec))
    (block $x
      (loop $l
        (br_if $x (i32.ge_u (local.get $i) (i32.const 0xfffffffc)))
        (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
                                 (i32.and (local.get $i) (i32.const 63))))
        (local.set $i (i32.add (local.get $i) (i32.const 4)))
        (br $l)))
    (local.get $acc))

  ;; the serial-FP invariant: f32 addition is not associative, so the sum
  ;; is only right if the copies keep the exact evaluation order.
  (func (export "fires_serial_f32") (result i32)
    (local $i i32) (local $s f32)
    (loop $l
      (local.set $s (f32.add (local.get $s)
                             (f32.div (f32.convert_i32_u (i32.load8_u (local.get $i)))
                                      (f32.const 7))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.ne (local.get $i) (i32.const 16))))
    (i32.reinterpret_f32 (local.get $s)))

  ;; divides keep their zero checks; the divisor is loaded so the check
  ;; cannot be proven away, and each clone must keep its own.
  (func (export "fires_trap_div") (result i32)
    (local $i i32) (local $acc i64)
    (loop $l
      (local.set $acc
        (i64.add (i64.mul (local.get $acc) (i64.const 3))
                 (i64.xor (i64.div_u (i64.const 0xfedcba987654321)
                                     (i64.load8_u (local.get $i)))
                          (i64.rem_u (i64.const 0xfedcba987654321)
                                     (i64.load8_u (local.get $i))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.ne (local.get $i) (i32.const 16))))
    (i32.wrap_i64 (i64.xor (local.get $acc)
                           (i64.shr_u (local.get $acc) (i64.const 32)))))

  ;; three carried values: the counter and two accumulators that feed each
  ;; other, so a per-param seeding mistake cannot cancel out.
  (func (export "fires_two_accs") (result i32)
    (local $i i32) (local $a i32) (local $b i32)
    (local.set $b (i32.const 5))
    (loop $l
      (local.set $a (i32.add (i32.mul (local.get $a) (i32.const 3))
                             (i32.load8_u (local.get $i))))
      (local.set $b (i32.add (i32.mul (local.get $b) (i32.const 5))
                             (local.get $a)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.ne (local.get $i) (i32.const 16))))
    (i32.xor (local.get $a) (local.get $b)))

  ;; ---- refusals: same answers, no unrolling ----

  ;; a call in the body; the callee is self-recursive so the inliner
  ;; cannot dissolve it first.
  (func $weight (param $x i32) (result i32)
    (if (result i32) (i32.gt_u (local.get $x) (i32.const 3))
      (then (i32.add (call $weight (i32.sub (local.get $x) (i32.const 4)))
                     (i32.const 1)))
      (else (local.get $x))))
  (func (export "refuses_call") (result i32)
    (local $i i32) (local $acc i32)
    (loop $l
      (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
                               (call $weight (i32.load8_u (local.get $i)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.ne (local.get $i) (i32.const 16))))
    (local.get $acc))

  ;; a data-dependent branch in the body is not a simple cycle.
  (func (export "refuses_diamond") (result i32)
    (local $i i32) (local $acc i32) (local $x i32)
    (loop $l
      (local.set $x (i32.load8_u (local.get $i)))
      (if (i32.and (local.get $x) (i32.const 1))
        (then (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
                                       (local.get $x))))
        (else (local.set $acc (i32.xor (local.get $acc) (local.get $x)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.ne (local.get $i) (i32.const 16))))
    (local.get $acc))

  ;; a second way out of the loop (never taken: no data byte is 255).
  (func (export "refuses_multiexit") (result i32)
    (local $i i32) (local $acc i32)
    (block $x
      (loop $l
        (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
                                 (i32.load8_u (local.get $i))))
        (br_if $x (i32.eq (local.get $acc) (i32.const 255)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.ne (local.get $i) (i32.const 16)))))
    (local.get $acc))

  ;; the scan shape: the tested value is a load, not the counter.  The
  ;; terminator byte at 63 stops it.
  (func (export "refuses_scan") (result i32)
    (local $i i32) (local $acc i32)
    (block $x
      (loop $l
        (br_if $x (i32.eqz (i32.load8_u (local.get $i))))
        (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
                                 (i32.load8_u (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $acc) (local.get $i)))

  ;; a 13-trip loop has no factor up to the default cap, so it falls
  ;; through to the guarded form: three 4-wide rounds and a remainder of
  ;; one in the original loop.
  (func (export "fires_guarded_odd") (result i32)
    (local $i i32) (local $acc i32)
    (loop $l
      (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
                               (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.ne (local.get $i) (i32.const 13))))
    (local.get $acc))

  ;; ---- the guarded form: dynamic trip counts ----

  ;; the bound lives in memory (29 at runtime): seven 4-wide rounds, then
  ;; the original loop finishes the last iteration and carries the exits.
  (func (export "fires_guarded") (result i32)
    (local $i i32) (local $acc i32) (local $n i32)
    (local.set $n (i32.load8_u (i32.const 13)))
    (loop $l
      (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
                               (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $acc))

  ;; NE exit against the same dynamic bound.
  (func (export "fires_guarded_ne") (result i32)
    (local $i i32) (local $acc i32) (local $n i32)
    (local.set $n (i32.load8_u (i32.const 13)))
    (loop $l
      (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
                               (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.ne (local.get $i) (local.get $n))))
    (local.get $acc))

  ;; inclusive dynamic bound: one more iteration than the strict twin.
  (func (export "fires_guarded_le") (result i32)
    (local $i i32) (local $acc i32) (local $n i32)
    (local.set $n (i32.load8_u (i32.const 13)))
    (loop $l
      (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
                               (i32.load8_u (i32.and (local.get $i)
                                                     (i32.const 31)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.le_u (local.get $i) (local.get $n))))
    (local.get $acc))

  ;; dynamic start counting down to zero.
  (func (export "fires_guarded_down") (result i32)
    (local $i i32) (local $acc i32)
    (local.set $i (i32.load8_u (i32.const 13)))
    (loop $l
      (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
                               (i32.load8_u (i32.and (local.get $i)
                                                     (i32.const 31)))))
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (br_if $l (i32.gt_s (local.get $i) (i32.const 0))))
    (local.get $acc))

  ;; runtime trips of 2 and 1: the guard computes a zero round count and
  ;; bypasses to the original loop -- the transform fires, the unrolled
  ;; body never runs, and the answer must not notice.
  (func (export "fires_guarded_short") (result i32)
    (local $i i32) (local $acc i32) (local $n i32)
    (local.set $n (i32.load8_u (i32.const 2)))
    (loop $l
      (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
                               (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $acc))
  (func (export "fires_guarded_one") (result i32)
    (local $i i32) (local $acc i32) (local $n i32)
    (local.set $n (i32.load8_u (i32.const 0)))
    (loop $l
      (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
                               (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.ne (local.get $i) (local.get $n))))
    (local.get $acc))

  ;; a top-tested dynamic loop: the trip arithmetic is the same as the
  ;; bottom-tested form (total body executions), the empty region bypasses,
  ;; and the re-entered header re-tests before every remainder iteration.
  (func (export "fires_guarded_top") (result i32)
    (local $i i32) (local $acc i32) (local $n i32)
    (local.set $n (i32.load8_u (i32.const 13)))
    (block $x
      (loop $l
        (br_if $x (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
                                 (i32.xor (local.get $i) (i32.const 21))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $acc)))
