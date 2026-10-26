;; memory.copy and memory.fill whose length is a constant compile to
;; straight-line loads and stores, without the length tests the dynamic fast
;; path makes: overlapping integer accesses below 16 bytes, q registers from
;; 16 to 256, and the call to the runtime beyond -- or, on AArch64 for
;; memory 0, where the op saves nothing round it (memop_r.wat), q registers
;; to 96 bytes, six of them, and the call to warmc_memmove_r or
;; warmc_memset_r beyond.  Each length class gets a function of its own,
;; since the length is what picks the code:
;;
;;   * 1, 3, 7, 12 and 15 bytes take the one or two overlapping integer
;;     accesses; 16, 17, 31, 32, 33, 48, 64, 80, 95, 96, 200, 255 and 256
;;     the q registers in pairs, alone and overlapping the end; 97 and 257
;;     the call, each in one of the two cases.
;;   * Each copy runs at shifts from -20 to 20, so a copy overlapping its
;;     source in either direction is still a memmove: everything is loaded
;;     before anything is stored.
;;   * The ones ending at the last byte of memory work.  One byte further must
;;     trap before anything is written; a trap ends this harness's launch, so
;;     that half lives in the spec testsuite.
;;   * $copy80_alone is a function whose only memory access is the constant
;;     copy, reached through call_indirect so nothing inlines it, the shape
;;     that once ran without a memory base set up (see smallmemop.wat).

(module
  (memory 1)
  (type $op_t (func (param i32 i32)))
  (table 1 funcref)
  (elem (i32.const 0) $copy80_alone)
  (global $copy_ix (export "copy_ix") (mut i32) (i32.const 0))

  (func $copy80_alone (type $op_t)
    (memory.copy (local.get 0) (local.get 1) (i32.const 80)))

  (func $c1 (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 1)))
  (func $c3 (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 3)))
  (func $c7 (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 7)))
  (func $c12 (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 12)))
  (func $c15 (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 15)))
  (func $c16 (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 16)))
  (func $c17 (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 17)))
  (func $c31 (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 31)))
  (func $c32 (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 32)))
  (func $c48 (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 48)))
  (func $c80 (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 80)))
  (func $c200 (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 200)))
  (func $c255 (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 255)))
  (func $c256 (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 256)))
  (func $c257 (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 257)))
  (func $c33 (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 33)))
  (func $c64 (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 64)))
  (func $c95 (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 95)))
  (func $c96 (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 96)))
  (func $c97 (param i32 i32) (memory.copy (local.get 0) (local.get 1) (i32.const 97)))

  (func $f1 (param i32 i32) (memory.fill (local.get 0) (local.get 1) (i32.const 1)))
  (func $f5 (param i32 i32) (memory.fill (local.get 0) (local.get 1) (i32.const 5)))
  (func $f15 (param i32 i32) (memory.fill (local.get 0) (local.get 1) (i32.const 15)))
  (func $f16 (param i32 i32) (memory.fill (local.get 0) (local.get 1) (i32.const 16)))
  (func $f33 (param i32 i32) (memory.fill (local.get 0) (local.get 1) (i32.const 33)))
  (func $f96 (param i32 i32) (memory.fill (local.get 0) (local.get 1) (i32.const 96)))
  (func $f250 (param i32 i32) (memory.fill (local.get 0) (local.get 1) (i32.const 250)))
  (func $f256 (param i32 i32) (memory.fill (local.get 0) (local.get 1) (i32.const 256)))
  (func $f300 (param i32 i32) (memory.fill (local.get 0) (local.get 1) (i32.const 300)))
  (func $f64 (param i32 i32) (memory.fill (local.get 0) (local.get 1) (i32.const 64)))
  (func $f95 (param i32 i32) (memory.fill (local.get 0) (local.get 1) (i32.const 95)))
  (func $f97 (param i32 i32) (memory.fill (local.get 0) (local.get 1) (i32.const 97)))

  ;; bytes [from, from + 1024) = i * 37 + 11
  (func $seed (param $from i32)
    (local $i i32)
    (loop $l
      (i32.store8 (i32.add (local.get $from) (local.get $i))
                  (i32.add (i32.mul (local.get $i) (i32.const 37)) (i32.const 11)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 1024)))))

  ;; FNV-1a over bytes [from, from + 1024)
  (func $hash (param $from i32) (result i32)
    (local $i i32) (local $h i32)
    (local.set $h (i32.const 0x811c9dc5))
    (loop $l
      (local.set $h (i32.mul (i32.xor (local.get $h)
                                      (i32.load8_u (i32.add (local.get $from) (local.get $i))))
                             (i32.const 0x01000193)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 1024))))
    (local.get $h))

  ;; Every copy length at every shift from -20 to 20, each from a fresh seed,
  ;; folded into one hash.
  (func (export "copies_at_every_shift") (result i32)
    (local $shift i32) (local $acc i32) (local $k i32)
    (loop $lk
      (local.set $shift (i32.const -20))
      (loop $ls
        (call $seed (i32.const 0))
        (block $done
          (block $b14 (block $b13 (block $b12 (block $b11 (block $b10 (block $b9
          (block $b8 (block $b7 (block $b6 (block $b5 (block $b4 (block $b3
          (block $b2 (block $b1 (block $b0
            (br_table $b0 $b1 $b2 $b3 $b4 $b5 $b6 $b7 $b8 $b9 $b10 $b11 $b12
                      $b13 $b14 (local.get $k)))
            (call $c1 (i32.const 400) (i32.add (i32.const 400) (local.get $shift))) (br $done))
            (call $c3 (i32.const 400) (i32.add (i32.const 400) (local.get $shift))) (br $done))
            (call $c7 (i32.const 400) (i32.add (i32.const 400) (local.get $shift))) (br $done))
            (call $c12 (i32.const 400) (i32.add (i32.const 400) (local.get $shift))) (br $done))
            (call $c15 (i32.const 400) (i32.add (i32.const 400) (local.get $shift))) (br $done))
            (call $c16 (i32.const 400) (i32.add (i32.const 400) (local.get $shift))) (br $done))
            (call $c17 (i32.const 400) (i32.add (i32.const 400) (local.get $shift))) (br $done))
            (call $c31 (i32.const 400) (i32.add (i32.const 400) (local.get $shift))) (br $done))
            (call $c32 (i32.const 400) (i32.add (i32.const 400) (local.get $shift))) (br $done))
            (call $c48 (i32.const 400) (i32.add (i32.const 400) (local.get $shift))) (br $done))
            (call $c80 (i32.const 400) (i32.add (i32.const 400) (local.get $shift))) (br $done))
            (call $c200 (i32.const 400) (i32.add (i32.const 400) (local.get $shift))) (br $done))
            (call $c255 (i32.const 400) (i32.add (i32.const 400) (local.get $shift))) (br $done))
            (call $c256 (i32.const 400) (i32.add (i32.const 400) (local.get $shift))) (br $done))
          (call $c257 (i32.const 400) (i32.add (i32.const 400) (local.get $shift))))
        (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 31))
                                 (call $hash (i32.const 0))))
        (br_if $ls (i32.le_s (local.tee $shift (i32.add (local.get $shift) (i32.const 1)))
                             (i32.const 20))))
      (br_if $lk (i32.le_u (local.tee $k (i32.add (local.get $k) (i32.const 1)))
                           (i32.const 14))))
    (local.get $acc))

  ;; The lengths round six q registers, the same way.
  (func (export "copies_round_96_at_every_shift") (result i32)
    (local $shift i32) (local $acc i32) (local $k i32)
    (loop $lk
      (local.set $shift (i32.const -20))
      (loop $ls
        (call $seed (i32.const 0))
        (block $done
          (block $b4 (block $b3 (block $b2 (block $b1 (block $b0
            (br_table $b0 $b1 $b2 $b3 $b4 (local.get $k)))
            (call $c33 (i32.const 400) (i32.add (i32.const 400) (local.get $shift))) (br $done))
            (call $c64 (i32.const 400) (i32.add (i32.const 400) (local.get $shift))) (br $done))
            (call $c95 (i32.const 400) (i32.add (i32.const 400) (local.get $shift))) (br $done))
            (call $c96 (i32.const 400) (i32.add (i32.const 400) (local.get $shift))) (br $done))
          (call $c97 (i32.const 400) (i32.add (i32.const 400) (local.get $shift))))
        (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 31))
                                 (call $hash (i32.const 0))))
        (br_if $ls (i32.le_s (local.tee $shift (i32.add (local.get $shift) (i32.const 1)))
                             (i32.const 20))))
      (br_if $lk (i32.le_u (local.tee $k (i32.add (local.get $k) (i32.const 1)))
                           (i32.const 4))))
    (local.get $acc))

  ;; Every fill length at a few alignments.
  (func (export "fills_at_every_alignment") (result i32)
    (local $at i32) (local $acc i32)
    (loop $la
      (call $seed (i32.const 0))
      (call $f1 (i32.add (i32.const 3) (local.get $at)) (i32.const 0x81))
      (call $f5 (i32.add (i32.const 20) (local.get $at)) (i32.const 0x1ff))
      (call $f15 (i32.add (i32.const 40) (local.get $at)) (i32.const 0x42))
      (call $f16 (i32.add (i32.const 70) (local.get $at)) (i32.const 0x99))
      (call $f33 (i32.add (i32.const 100) (local.get $at)) (i32.const 0xab))
      (call $f96 (i32.add (i32.const 150) (local.get $at)) (i32.const 0x5c))
      (call $f250 (i32.add (i32.const 260) (local.get $at)) (i32.const 0x13))
      (call $f256 (i32.add (i32.const 520) (local.get $at)) (i32.const 0xee))
      (call $f300 (i32.add (i32.const 700) (local.get $at)) (i32.const 0x07))
      (call $f64 (i32.add (i32.const 30) (local.get $at)) (i32.const 0x6d))
      (call $f95 (i32.add (i32.const 420) (local.get $at)) (i32.const 0x2e1))
      (call $f97 (i32.add (i32.const 870) (local.get $at)) (i32.const 0xc4))
      (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 31))
                               (call $hash (i32.const 0))))
      (br_if $la (i32.le_u (local.tee $at (i32.add (local.get $at) (i32.const 1)))
                           (i32.const 17))))
    (local.get $acc))

  (func (export "seed_for_the_lone_copy") (result i32)
    (call $seed (i32.const 0))
    (i32.const 1))
  (func (export "copy80_alone") (result i32)
    (call_indirect (type $op_t) (i32.const 501) (i32.const 23) (global.get $copy_ix))
    (i32.const 2))
  (func (export "what_the_lone_copy_wrote") (result i32)
    (call $hash (i32.const 0)))

  (func (export "ops_ending_at_the_last_byte") (result i32)
    (call $seed (i32.const 64512))
    (call $c256 (i32.const 65280) (i32.const 64512))
    (call $c17 (i32.const 64600) (i32.const 65519))
    (call $f96 (i32.const 65440) (i32.const 0x3c))
    (call $f1 (i32.const 65535) (i32.const 0x77))
    (call $hash (i32.const 64512))))
