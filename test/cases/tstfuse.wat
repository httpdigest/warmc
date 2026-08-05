;; An AND that has already absorbed one of its operands is not a `tst`.
;;
;; Three separate passes in analyse_immediates can leave an AND holding an
;; operand that was deliberately given no location -- the complement into
;; `bic`, a constant shift into `and ..., lsl #k`, and an all-ones-or-nothing
;; `select` into `csel` -- and the `tst` fusion then folds the AND *itself*
;; into the zero test that reads it.  `emit_compare` re-reads both of the
;; AND's operands as registers, so the second fusion asks the allocator for
;; the value the first one removed.  Every shape below failed the compile
;; outright with "internal: value vN has no location", at -O1 and at -O2, on
;; wasm a producer writes without thinking about it.
;;
;; Same family as test/cases/fuse.wat -- two passes claiming one operand of
;; the same logical operation -- but the claimant here is the *consumer* of
;; the AND rather than another operand of it, which is why the guard fuse.wat
;; pins did not cover it.
;;
;; Two things the shapes have to get right.  The zero test must not itself
;; reach a branch, or it is already a `cbz` and the fusion declines for its
;; own reasons rather than for the one under test -- hence the arithmetic
;; wrapped round every result.  And the operands come from memory rather than
;; from constants, so nothing folds the whole expression away before
;; instruction selection sees it.
(module
(memory 1)
(data (i32.const 0) "\21\43\65\87\09\ba\dc\fe\11\22\33\44\55\66\77\88")

;; The complement, both ways round: the complement pass swaps the operands
;; when it takes the one in position zero, so the two are different paths.
(func (export "tst_not") (result i32)
  (local $x i32) (local $y i32)
  (local.set $x (i32.load (i32.const 0)))
  (local.set $y (i32.load (i32.const 4)))
  (i32.add (i32.eqz (i32.and (local.get $y) (i32.xor (local.get $x) (i32.const -1))))
           (i32.mul (local.get $y) (i32.const 3))))
(func (export "tst_not_swapped") (result i32)
  (local $x i32) (local $y i32)
  (local.set $x (i32.load (i32.const 0)))
  (local.set $y (i32.load (i32.const 4)))
  (i32.add (i32.eqz (i32.and (i32.xor (local.get $x) (i32.const -1)) (local.get $y)))
           (i32.mul (local.get $y) (i32.const 3))))

;; A shift folded into the AND, one per encodable direction.
(func (export "tst_shl") (result i32)
  (local $x i32) (local $y i32)
  (local.set $x (i32.load (i32.const 0)))
  (local.set $y (i32.load (i32.const 4)))
  (i32.add (i32.eqz (i32.and (local.get $y) (i32.shl (local.get $x) (i32.const 3))))
           (i32.mul (local.get $y) (i32.const 5))))
(func (export "tst_shr_u") (result i32)
  (local $x i32) (local $y i32)
  (local.set $x (i32.load (i32.const 0)))
  (local.set $y (i32.load (i32.const 4)))
  (i32.add (i32.eq (i32.and (local.get $y) (i32.shr_u (local.get $x) (i32.const 5))) (i32.const 0))
           (i32.mul (local.get $y) (i32.const 7))))
(func (export "tst_shr_s") (result i32)
  (local $x i32) (local $y i32)
  (local.set $x (i32.load (i32.const 0)))
  (local.set $y (i32.load (i32.const 4)))
  (i32.add (i32.ne (i32.and (local.get $y) (i32.shr_s (local.get $x) (i32.const 6))) (i32.const 0))
           (i32.mul (local.get $y) (i32.const 9))))
(func (export "tst_rotr") (result i32)
  (local $x i32) (local $y i32)
  (local.set $x (i32.load (i32.const 0)))
  (local.set $y (i32.load (i32.const 4)))
  (i32.add (i32.eqz (i32.and (local.get $y) (i32.rotr (local.get $x) (i32.const 9))))
           (i32.mul (local.get $y) (i32.const 11))))

;; The mask that is a condition rather than a value.
(func (export "tst_masksel") (result i32)
  (local $x i32) (local $y i32)
  (local.set $x (i32.load (i32.const 0)))
  (local.set $y (i32.load (i32.const 4)))
  (i32.add (i32.eqz (i32.and (local.get $y)
                             (select (i32.const -1) (i32.const 0)
                                     (i32.lt_s (local.get $x) (i32.const 5)))))
           (i32.mul (local.get $y) (i32.const 13))))

;; Sixty-four bits, where the flags and the fold are the other width.
(func (export "tst_not_64") (result i64)
  (local $x i64) (local $y i64)
  (local.set $x (i64.load (i32.const 0)))
  (local.set $y (i64.load (i32.const 8)))
  (i64.add (i64.extend_i32_u
             (i64.eqz (i64.and (local.get $y) (i64.xor (local.get $x) (i64.const -1)))))
           (i64.mul (local.get $y) (i64.const 3))))
(func (export "tst_shl_64") (result i64)
  (local $x i64) (local $y i64)
  (local.set $x (i64.load (i32.const 0)))
  (local.set $y (i64.load (i32.const 8)))
  (i64.add (i64.extend_i32_u
             (i64.eqz (i64.and (local.get $y) (i64.shl (local.get $x) (i64.const 17)))))
           (i64.mul (local.get $y) (i64.const 5))))

;; The two shapes the fusion is *supposed* to take, so that refusing
;; everything cannot pass this file.  `tst_plain` is an AND with nothing else
;; claimed, and `tst_branch` is the zero test that reaches a branch and is a
;; `cbz` already.
(func (export "tst_plain") (result i32)
  (local $y i32)
  (local.set $y (i32.load (i32.const 4)))
  (i32.add (i32.eqz (i32.and (local.get $y) (i32.const 0xff00)))
           (i32.mul (local.get $y) (i32.const 17))))
(func (export "tst_branch") (result i32)
  (local $x i32) (local $y i32)
  (local.set $x (i32.load (i32.const 0)))
  (local.set $y (i32.load (i32.const 4)))
  (if (result i32) (i32.eqz (i32.and (local.get $y) (i32.xor (local.get $x) (i32.const -1))))
    (then (i32.const 101))
    (else (i32.const 202))))

;; The complement's *own* first operand can be a constant, and the emitter
;; had a guard that swapped to the second one when it was -- which is the
;; all-ones constant, so `x & ~c` was emitted as `x & ~(-1)`, or nothing.
;; The guard was added for a shape a later commit made unreachable, so the
;; only thing left reaching it was this.  Constant folding collapses the
;; complement before selection sees it, so these read correctly at -O1 and
;; above and wrongly at -O0 -- and under -fno-fold at any level, which is
;; why run_all.sh now runs that too.
(func (export "not_const_and") (result i32)
  (i32.and (i32.load (i32.const 0)) (i32.xor (i32.const 5) (i32.const -1))))
(func (export "not_const_or") (result i32)
  (i32.or (i32.load (i32.const 0)) (i32.xor (i32.const 5) (i32.const -1))))
(func (export "not_const_xor") (result i32)
  (i32.xor (i32.load (i32.const 0)) (i32.xor (i32.const 5) (i32.const -1))))
(func (export "not_const_and64") (result i64)
  (i64.and (i64.load (i32.const 0)) (i64.xor (i64.const 5) (i64.const -1))))
(func (export "not_const_or64") (result i64)
  (i64.or (i64.load (i32.const 0)) (i64.xor (i64.const 5) (i64.const -1))))

;; The shape the guard was meant for, so removing it stays honest: the
;; complement's first operand is a value and its second the all-ones constant.
(func (export "not_value_and") (result i32)
  (i32.and (i32.load (i32.const 0))
           (i32.xor (i32.load (i32.const 4)) (i32.const -1))))
)
