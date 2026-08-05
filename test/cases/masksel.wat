;; A mask that is all ones or nothing is a condition, not a value.
;;
;; `x op (c ? -1 : 0)` never has to build the mask: the three logical
;; operations each collapse to one conditional instruction.  The mapping is
;; `csinv Rd, Rn, Rm, cc` = `Rd = cc ? Rn : NOT(Rm)`, and with `ccm` standing
;; for "the mask is all ones":
;;
;;   x ^ mask  is  ccm ? NOT(x) : x     csinv d, x, x, !ccm
;;   x & mask  is  ccm ? x : 0          csel  d, x, xzr, ccm
;;   x | mask  is  ccm ? -1 : x         csinv d, x, xzr, !ccm
;;
;; The `or` arm had its operands the other way round -- `csinv d, xzr, x` --
;; which reads as `ccm ? NOT(x) : 0`.  So `x | (c ? -1 : 0)` answered the
;; complement of x where all ones was wanted and zero where x was, silently,
;; at every optimisation level, on wasm nothing had to be coaxed into writing.
;; The fusion's own comment said the `and` and `or` mirrors "fire nowhere here
;; and are speculative": nothing in eight workloads, twenty-four case modules
;; or the upstream corpus had ever emitted one, so the arm went in unmeasured
;; and stayed wrong.
;;
;; Every arm is written with the mask in both polarities (all-ones on the true
;; side and on the false side), because the polarity is folded into the
;; condition rather than into the instruction, and at both widths.  The
;; operands come from memory so that nothing folds the select away first, and
;; both branches of each condition are exercised by picking one value that
;; satisfies it and one that does not.
(module
(memory 1)
(data (i32.const 0) "\21\43\65\87\09\ba\dc\fe\11\22\33\44\55\66\77\88")

;; $p is 0x87654321, read signed that is negative, so `< 5` holds.
;; $r is 0x44332211, positive, so `< 5` does not.
(func $p (result i32) (i32.load (i32.const 0)))
(func $q (result i32) (i32.load (i32.const 4)))
(func $r (result i32) (i32.load (i32.const 8)))

(func (export "or_t_lo") (result i32)
  (i32.or (call $q) (select (i32.const -1) (i32.const 0)
                            (i32.lt_s (call $p) (i32.const 5)))))
(func (export "or_t_hi") (result i32)
  (i32.or (call $q) (select (i32.const -1) (i32.const 0)
                            (i32.lt_s (call $r) (i32.const 5)))))
(func (export "or_f_lo") (result i32)
  (i32.or (call $q) (select (i32.const 0) (i32.const -1)
                            (i32.lt_s (call $p) (i32.const 5)))))
(func (export "or_f_hi") (result i32)
  (i32.or (call $q) (select (i32.const 0) (i32.const -1)
                            (i32.lt_s (call $r) (i32.const 5)))))

(func (export "and_t_lo") (result i32)
  (i32.and (call $q) (select (i32.const -1) (i32.const 0)
                             (i32.lt_s (call $p) (i32.const 5)))))
(func (export "and_t_hi") (result i32)
  (i32.and (call $q) (select (i32.const -1) (i32.const 0)
                             (i32.lt_s (call $r) (i32.const 5)))))
(func (export "and_f_lo") (result i32)
  (i32.and (call $q) (select (i32.const 0) (i32.const -1)
                             (i32.lt_s (call $p) (i32.const 5)))))

(func (export "xor_t_lo") (result i32)
  (i32.xor (call $q) (select (i32.const -1) (i32.const 0)
                             (i32.lt_s (call $p) (i32.const 5)))))
(func (export "xor_t_hi") (result i32)
  (i32.xor (call $q) (select (i32.const -1) (i32.const 0)
                             (i32.lt_s (call $r) (i32.const 5)))))
(func (export "xor_f_lo") (result i32)
  (i32.xor (call $q) (select (i32.const 0) (i32.const -1)
                             (i32.lt_s (call $p) (i32.const 5)))))

;; Sixty-four bits: the mask is compared against the other width's all-ones,
;; and `sf` decides both the select and the conditional instruction.
(func $P (result i64) (i64.load (i32.const 0)))
(func $Q (result i64) (i64.load (i32.const 8)))
(func (export "or64_t") (result i64)
  (i64.or (call $Q) (select (i64.const -1) (i64.const 0)
                            (i64.lt_s (call $P) (i64.const 5)))))
(func (export "or64_f") (result i64)
  (i64.or (call $Q) (select (i64.const 0) (i64.const -1)
                            (i64.lt_s (call $P) (i64.const 5)))))
(func (export "and64_t") (result i64)
  (i64.and (call $Q) (select (i64.const -1) (i64.const 0)
                             (i64.lt_s (call $P) (i64.const 5)))))
(func (export "xor64_t") (result i64)
  (i64.xor (call $Q) (select (i64.const -1) (i64.const 0)
                             (i64.lt_s (call $P) (i64.const 5)))))

;; The mask on the *left*, which the fusion reaches by swapping the operands.
(func (export "or_swapped") (result i32)
  (i32.or (select (i32.const -1) (i32.const 0)
                  (i32.lt_s (call $p) (i32.const 5))) (call $q)))
(func (export "and_swapped") (result i32)
  (i32.and (select (i32.const -1) (i32.const 0)
                   (i32.lt_s (call $r) (i32.const 5))) (call $q)))
(func (export "xor_swapped") (result i32)
  (i32.xor (select (i32.const -1) (i32.const 0)
                   (i32.lt_s (call $r) (i32.const 5))) (call $q)))

;; A mask that is *not* all-ones-or-nothing must still be an ordinary select
;; and an ordinary logical operation, so that the fusion cannot be "fixed" by
;; firing more often.
(func (export "or_not_a_mask") (result i32)
  (i32.or (call $q) (select (i32.const 0x0f0f0f0f) (i32.const 0)
                            (i32.lt_s (call $p) (i32.const 5)))))
;; And a mask whose condition is a floating-point comparison, whose condition
;; codes are a different set.
(func (export "or_float_cond") (result i32)
  (i32.or (call $q) (select (i32.const -1) (i32.const 0)
                            (f64.lt (f64.const 1.5) (f64.const 2.5)))))
(func (export "or_float_cond_f") (result i32)
  (i32.or (call $q) (select (i32.const -1) (i32.const 0)
                            (f64.lt (f64.const 3.5) (f64.const 2.5)))))
)
