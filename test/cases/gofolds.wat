;; Folds for the shapes Go's back end writes: it keeps every integer and bool
;; in an i64 and narrows it again where wasm wants an i32, it computes
;; LeadingZeros64 as 64 - (64 - clz), and its portable ReverseBytes64 ends in
;; a rotate by 32.  Each export runs one fold over values chosen to reach the
;; edges -- the sign bit, 2^32, constants the narrowed compare cannot hold --
;; next to near misses that must be left alone.  The values come from an
;; exported mutable global so nothing folds them away first.

(module
  (global $seed (export "seed") (mut i64) (i64.const 0x8000_0001_ffff_fffe))

  (func $vals (param $k i32) (result i64)
    (i64.add (global.get $seed) (i64.mul (i64.extend_i32_u (local.get $k))
                                         (i64.const 0x3fff_ffff_0000_0001))))

  ;; wrap(extend(x)) is x, wrap(ext32s(x)) is wrap(x), wrap(x & 0xffffffff)
  ;; is wrap(x); wrap(x & 0x7fffffff) is not.
  (func (export "narrowing_round_trips") (result i32)
    (local $k i32) (local $acc i32) (local $x i64) (local $w i32)
    (loop $l
      (local.set $x (call $vals (local.get $k)))
      (local.set $w (i32.wrap_i64 (local.get $x)))
      (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 31))
        (i32.add
          (i32.add (i32.wrap_i64 (i64.extend_i32_u (local.get $w)))
                   (i32.wrap_i64 (i64.extend_i32_s (local.get $w))))
          (i32.add
            (i32.add (i32.wrap_i64 (i64.extend32_s (local.get $x)))
                     (i32.wrap_i64 (i64.and (local.get $x) (i64.const 0xffff_ffff))))
            (i32.wrap_i64 (i64.and (local.get $x) (i64.const 0x7fff_ffff)))))))
      (br_if $l (i32.lt_u (local.tee $k (i32.add (local.get $k) (i32.const 1)))
                          (i32.const 9))))
    (local.get $acc))

  ;; eq/ne of a widened i32 against a constant: narrowed when the widening
  ;; could have produced the constant, decided outright otherwise.
  (func (export "widened_compares") (result i32)
    (local $k i32) (local $acc i32) (local $w i32) (local $u i64) (local $s i64)
    (loop $l
      (local.set $w (i32.wrap_i64 (call $vals (local.get $k))))
      (local.set $u (i64.extend_i32_u (local.get $w)))
      (local.set $s (i64.extend_i32_s (local.get $w)))
      (local.set $acc (i32.add (i32.shl (local.get $acc) (i32.const 1))
        (i32.add
          (i32.add (i32.add (i64.ne (local.get $u) (i64.const 0))
                            (i64.eq (local.get $u) (i64.const 0)))
                   (i32.add (i64.eqz (local.get $u))
                            (i64.eq (local.get $u) (i64.const 0xffff_fffe))))
          (i32.add
            (i32.add (i64.eq (local.get $u) (i64.const 0x1_0000_0000))
                     (i64.ne (local.get $u) (i64.const -2)))
            (i32.add (i64.eq (local.get $s) (i64.const -2))
                     (i64.ne (local.get $s) (i64.const 0xffff_fffe)))))))
      (br_if $l (i32.lt_u (local.tee $k (i32.add (local.get $k) (i32.const 1)))
                          (i32.const 9))))
    (local.get $acc))

  ;; A bool that went through an i64 and back, the way Go branches on one.
  (func (export "bools_through_i64") (result i32)
    (local $k i32) (local $acc i32) (local $x i64) (local $b i64)
    (loop $l
      (local.set $x (call $vals (local.get $k)))
      (local.set $b (i64.extend_i32_u (i64.lt_s (local.get $x) (i64.const 0))))
      (if (i64.ne (local.get $b) (i64.const 0))
        (then (local.set $acc (i32.add (local.get $acc) (i32.const 3)))))
      (local.set $acc (i32.add (local.get $acc)
        (select (i32.const 10) (i32.const 20) (i32.wrap_i64 (local.get $b)))))
      (local.set $acc (i32.mul (local.get $acc) (i32.const 7)))
      (br_if $l (i32.lt_u (local.tee $k (i32.add (local.get $k) (i32.const 1)))
                          (i32.const 9))))
    (local.get $acc))

  ;; x - (x - y) and x + (y - x) are y, wrapping; x - (y - x) is not.
  (func (export "subtractions_that_cancel") (result i64)
    (local $k i32) (local $acc i64) (local $x i64) (local $y i64) (local $w i32) (local $z i32)
    (loop $l
      (local.set $x (call $vals (local.get $k)))
      (local.set $y (call $vals (i32.add (local.get $k) (i32.const 5))))
      (local.set $w (i32.wrap_i64 (local.get $x)))
      (local.set $z (i32.wrap_i64 (i64.shr_u (local.get $y) (i64.const 17))))
      (local.set $acc (i64.add (i64.mul (local.get $acc) (i64.const 1_000_003))
        (i64.add
          (i64.add (i64.sub (local.get $x) (i64.sub (local.get $x) (local.get $y)))
                   (i64.add (local.get $x) (i64.sub (local.get $y) (local.get $x))))
          (i64.add
            (i64.sub (i64.const 64) (i64.sub (i64.const 64) (i64.clz (local.get $y))))
            (i64.add
              (i64.extend_i32_u
                (i32.add (i32.sub (local.get $w) (i32.sub (local.get $w) (local.get $z)))
                         (i32.add (i32.sub (local.get $z) (local.get $w)) (local.get $w))))
              (i64.sub (local.get $x) (i64.sub (local.get $y) (local.get $x))))))))
      (br_if $l (i32.lt_u (local.tee $k (i32.add (local.get $k) (i32.const 1)))
                          (i32.const 9))))
    (local.get $acc))

  ;; Go's bits.ReverseBytes64: swap bytes, then halfwords, then rotate by 32.
  (func $rev64 (param $x i64) (result i64)
    (local.set $x (i64.or (i64.and (i64.shr_u (local.get $x) (i64.const 8))
                                   (i64.const 0x00ff_00ff_00ff_00ff))
                          (i64.shl (i64.and (local.get $x) (i64.const 0x00ff_00ff_00ff_00ff))
                                   (i64.const 8))))
    (local.set $x (i64.or (i64.and (i64.shr_u (local.get $x) (i64.const 16))
                                   (i64.const 0x0000_ffff_0000_ffff))
                          (i64.shl (i64.and (local.get $x) (i64.const 0x0000_ffff_0000_ffff))
                                   (i64.const 16))))
    (i64.or (i64.shr_u (local.get $x) (i64.const 32))
            (i64.shl (local.get $x) (i64.const 32))))

  ;; The same with the halfword masks off by one bit: not a byte reversal.
  (func $notrev64 (param $x i64) (result i64)
    (local.set $x (i64.or (i64.and (i64.shr_u (local.get $x) (i64.const 8))
                                   (i64.const 0x00ff_00ff_00ff_00ff))
                          (i64.shl (i64.and (local.get $x) (i64.const 0x00ff_00ff_00ff_00ff))
                                   (i64.const 8))))
    (local.set $x (i64.or (i64.and (i64.shr_u (local.get $x) (i64.const 16))
                                   (i64.const 0x0000_ffff_0000_fffe))
                          (i64.shl (i64.and (local.get $x) (i64.const 0x0000_ffff_0000_ffff))
                                   (i64.const 16))))
    (i64.rotl (local.get $x) (i64.const 32)))

  (func (export "byte_reversals") (result i64)
    (local $k i32) (local $acc i64) (local $x i64)
    (loop $l
      (local.set $x (call $vals (local.get $k)))
      (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7))
                               (i64.add (call $rev64 (local.get $x))
                                        (call $notrev64 (local.get $x)))))
      (br_if $l (i32.lt_u (local.tee $k (i32.add (local.get $k) (i32.const 1)))
                          (i32.const 9))))
    (local.get $acc)))
