;; memory.copy and memory.fill of up to 16 bytes compile to inline loads and
;; stores, with the call to the runtime kept for everything else.  Three ways
;; that can go wrong, one export group each:
;;
;;   * The fast path reads the memory base, and a function whose only memory
;;     access is the bulk op used to get none set up -- the op was a call, and
;;     calls need no base.  $fill and $copy are exactly that function, reached
;;     through call_indirect on an index in an exported mutable global so
;;     nothing inlines them.  The register holding the base is callee-saved,
;;     so a caller that set it up for its own accesses hides the bug: the
;;     *_alone exports call in with no memory access of their own on the way,
;;     and the export after each hashes what it wrote.  The spec testsuite's
;;     bulk.wast caught it.
;;   * Every length from 0 to 20, so each of the 8+8, 4+4 and first/middle/last
;;     byte shapes and the call beyond them run, and overlapping copies in both
;;     directions, which must still be memmoves.
;;   * The bounds: a copy or fill ending at the last byte, and an empty one at
;;     the end itself, all work.  One byte further must trap before anything is
;;     written; a trap ends this harness's launch, so that half lives in the
;;     spec testsuite's memory_copy.wast and memory_fill.wast, which check both.

(module
  (memory 1)
  (type $op_t (func (param i32 i32 i32)))
  (table 2 funcref)
  (elem (i32.const 0) $fill $copy)
  ;; Exported, so the host could change them and the calls stay indirect: an
  ;; index nothing can write would be folded and the callee inlined.
  (global $fill_ix (export "fill_ix") (mut i32) (i32.const 0))
  (global $copy_ix (export "copy_ix") (mut i32) (i32.const 1))

  (func $fill (type $op_t)
    (memory.fill (local.get 0) (local.get 1) (local.get 2)))
  (func $copy (type $op_t)
    (memory.copy (local.get 0) (local.get 1) (local.get 2)))

  (func $do_fill (param i32 i32 i32)
    (call_indirect (type $op_t) (local.get 0) (local.get 1) (local.get 2)
                   (global.get $fill_ix)))
  (func $do_copy (param i32 i32 i32)
    (call_indirect (type $op_t) (local.get 0) (local.get 1) (local.get 2)
                   (global.get $copy_ix)))

  ;; bytes [from, from + 512) = i * 37 + 11
  (func $seed (param $from i32)
    (local $i i32)
    (loop $l
      (i32.store8 (i32.add (local.get $from) (local.get $i))
                  (i32.add (i32.mul (local.get $i) (i32.const 37)) (i32.const 11)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 512)))))

  ;; FNV-1a over bytes [from, from + 512)
  (func $hash (param $from i32) (result i32)
    (local $i i32) (local $h i32)
    (local.set $h (i32.const 0x811c9dc5))
    (loop $l
      (local.set $h (i32.mul (i32.xor (local.get $h)
                                      (i32.load8_u (i32.add (local.get $from) (local.get $i))))
                             (i32.const 0x01000193)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 512))))
    (local.get $h))

  ;; Each returns a value: the harness runs only the exports that answer.
  (func (export "seed_for_the_lone_ops") (result i32)
    (call $seed (i32.const 0))
    (i32.const 1))
  (func (export "fill_alone") (result i32)
    (call $do_fill (i32.const 100) (i32.const 0x77) (i32.const 13))
    (i32.const 2))
  (func (export "copy_alone") (result i32)
    (call $do_copy (i32.const 300) (i32.const 40) (i32.const 11))
    (i32.const 3))
  (func (export "what_the_lone_ops_wrote") (result i32)
    (call $hash (i32.const 0)))

  (func (export "fill_lengths") (result i32)
    (local $n i32) (local $acc i32)
    (loop $l
      (call $seed (i32.const 0))
      (call $do_fill (i32.add (i32.const 100) (i32.rem_u (i32.mul (local.get $n) (i32.const 23))
                                                         (i32.const 50)))
                     (i32.add (i32.const 0x100) (local.get $n))
                     (local.get $n))
      (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 31))
                               (call $hash (i32.const 0))))
      (br_if $l (i32.le_u (local.tee $n (i32.add (local.get $n) (i32.const 1)))
                          (i32.const 20))))
    (local.get $acc))

  (func (export "copy_lengths_and_overlaps") (result i32)
    (local $n i32) (local $shift i32) (local $acc i32)
    (loop $ln
      (local.set $shift (i32.const -6))
      (loop $ls
        (call $seed (i32.const 0))
        (call $do_copy (i32.const 200) (i32.add (i32.const 200) (local.get $shift))
                       (local.get $n))
        (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 31))
                                 (call $hash (i32.const 0))))
        (br_if $ls (i32.le_s (local.tee $shift (i32.add (local.get $shift) (i32.const 1)))
                             (i32.const 6))))
      (br_if $ln (i32.le_u (local.tee $n (i32.add (local.get $n) (i32.const 1)))
                           (i32.const 20))))
    (local.get $acc))

  (func (export "ops_ending_at_the_last_byte") (result i32)
    (call $seed (i32.const 65024))
    (call $do_fill (i32.const 65532) (i32.const 0x5a) (i32.const 4))
    (call $do_copy (i32.const 65520) (i32.const 65024) (i32.const 12))
    (call $do_fill (i32.const 65536) (i32.const 0) (i32.const 0))
    (call $do_copy (i32.const 65536) (i32.const 65536) (i32.const 0))
    (call $hash (i32.const 65024))))
