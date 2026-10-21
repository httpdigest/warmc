;; Store-to-load forwarding when the two addresses are the same root with
;; constants added in different orders, which is what an inlined Go call
;; leaves: the callee stores its result at (sp - 8) + 24 and the caller loads
;; it from sp + 16.  Constants added to a 32-bit address wrap, the offset an
;; access carries does not, so two spellings name one address for every root
;; exactly when their added constants agree mod 2^32 and their offsets agree.
;;
;;   * forwarded: the same slot through (sp - 8) + 24 and sp + 16, and through
;;     sp + 0xfffffff0 + 0x20, which wraps back to it.
;;   * a later store to the slot through a third spelling must be what the
;;     load sees.
;;   * sp + 16 and sp with offset 16 name one address only while sp + 16 does
;;     not wrap; they must not be forwarded one to the other, and the root
;;     here is chosen so that they are different addresses.

(module
  (memory 1)
  (global $sp (export "sp") (mut i32) (i32.const 4096))
  (global $hi (export "hi") (mut i32) (i32.const 0xfffffff8))

  (func (export "result_slot_through_two_spellings") (result i64)
    (local $p i32) (local $acc i64) (local $i i32)
    (local.set $p (global.get $sp))
    (loop $l
      (i64.store (i32.add (i32.sub (local.get $p) (i32.const 8)) (i32.const 24))
                 (i64.add (i64.extend_i32_u (local.get $i)) (local.get $acc)))
      (local.set $acc (i64.add (local.get $acc)
                               (i64.load (i32.add (local.get $p) (i32.const 16)))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 50))))
    (local.get $acc))

  (func (export "wrapping_back_to_the_slot") (result i64)
    (local $p i32)
    (local.set $p (global.get $sp))
    (i64.store (i32.add (i32.add (local.get $p) (i32.const 0xfffffff0)) (i32.const 0x20))
               (i64.const 0x1122334455667788))
    (i64.load (i32.add (local.get $p) (i32.const 16))))

  (func (export "a_later_store_wins") (result i64)
    (local $p i32)
    (local.set $p (global.get $sp))
    (i64.store (i32.add (local.get $p) (i32.const 40)) (i64.const 7))
    (i64.store offset=8 (i32.add (local.get $p) (i32.const 32)) (i64.const 9))
    (i64.store (i32.sub (i32.add (local.get $p) (i32.const 48)) (i32.const 8)) (i64.const 11))
    (i64.add (i64.load (i32.add (local.get $p) (i32.const 40)))
             (i64.load offset=40 (local.get $p))))

  ;; $hi + 16 wraps to 8; $hi with offset 16 is past 4 GiB and traps, so the
  ;; load reads through the wrapped spelling only, after a store through the
  ;; other one at a root where both are in bounds.
  (func (export "offset_and_constant_are_not_the_same") (result i64)
    (local $p i32) (local $q i32)
    (local.set $p (global.get $sp))
    (i64.store offset=16 (local.get $p) (i64.const 5))
    (i64.store (i32.add (local.get $p) (i32.const 16)) (i64.const 6))
    (local.set $q (global.get $hi))
    (i64.store (i32.const 8) (i64.const 0))
    (i64.store (i32.add (local.get $q) (i32.const 16)) (i64.const 13))
    (i64.add (i64.load offset=16 (local.get $p))
             (i64.load (i32.const 8)))))
