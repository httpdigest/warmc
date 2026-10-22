;; A load, a store that may overwrite what it read, and the same load again.
;;
;; In a function with an unrolled loop, unroll_addr moves the later of two
;; loads from one base up next to the earlier, so the emitter can pair them,
;; past nothing but pure ops and other loads.  It checked the ops in the gap
;; from the second one on, so a store right after the first load was never
;; looked at: the second load went above it, read the old bytes, and memopt
;; then merged the two (C's dead-store probe, built with memcpy, at -O2).
;; The store's address and value are both made before the first load, so
;; nothing pure sits between that load and the store.  The loop is only there
;; to be unrolled.
(module
  (memory 1)
  (global $gp (mut i32) (i32.const 64))
  (global $gq (mut i32) (i32.const 64))
  (global $ga (mut i64) (i64.const 0x1111111111111111))
  (global $gb (mut i64) (i64.const 0x2222222222222222))

  (func (export "load_store_load") (result i64)
    (local $p i32) (local $q i32) (local $a i64) (local $b i64)
    (local $x i64) (local $i i32) (local $s i32)
    (local.set $p (global.get $gp))
    (local.set $q (global.get $gq))
    (local.set $a (global.get $ga))
    (local.set $b (global.get $gb))
    (loop $l
      (local.set $s (i32.add (local.get $s)
        (i32.load offset=256 (i32.shl (local.get $i) (i32.const 2)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 8))))
    (i64.store (local.get $q) (local.get $a))
    (local.set $x (i64.load32_u (local.get $p)))
    (i64.store (local.get $q) (local.get $b))
    (i64.or (i64.shl (local.get $x) (i64.const 32))
            (i64.add (i64.load32_u (local.get $p))
                     (i64.extend_i32_u (local.get $s))))))
