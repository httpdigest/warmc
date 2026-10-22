;; Float min and max reductions: f32.min and f32.max in a loop, the way
;; Go's built-in min and max come out of TinyGo.
;;
;; Hand-written, and it stays hand-written: test/gen_cases.py does not emit a
;; file of this name, and run_cases.py globs the directory afterwards, so this
;; is run against wasm-interp like every generated case is.
;;
;; What it is for.  A float sum may not be reassociated, but a min or a max
;; may: wasm's f32.min returns a NaN if either side is one and orders -0 below
;; +0, whatever the order of its operands, so the vectoriser takes these loops
;; four lanes at a time (two for f64), starts the accumulator at +inf for a
;; min and -inf for a max, and folds the lanes onto the value the loop was
;; entered with when it leaves.  What can go wrong:
;;
;;   * the identity.  Starting a min at 0, or at the largest finite value,
;;     loses an answer above it -- +inf among the elements is one;
;;   * the zeros.  -0 must win a min and +0 a max wherever in the array each
;;     one sits, in the vector body, in the tail, or as the seed;
;;   * NaN.  One NaN anywhere makes the answer NaN -- in the body, in the tail
;;     or as the seed;
;;   * the tail.  Lengths are not multiples of four, and two loops are shorter
;;     than one vector or empty, so the scalar remainder has to start where
;;     the vector body stopped, from the folded value;
;;   * a lane-wise min that is not a reduction, c[i] = min(a[i], b[i]);
;;   * an accumulator read by something else, which must be refused.
(module
  (memory 1)

  ;; f32 values at byte 0, 13 of them: three vectors and a one-element tail.
  ;; +inf is among them, so a max that started below it is wrong, and so is
  ;; -inf for a min.
  (func $fill (result i32)
    (f32.store (i32.const 0)  (f32.const 7.5))
    (f32.store (i32.const 4)  (f32.const -3.25))
    (f32.store (i32.const 8)  (f32.const 1e30))
    (f32.store (i32.const 12) (f32.const 0))
    (f32.store (i32.const 16) (f32.const -inf))
    (f32.store (i32.const 20) (f32.const 5))
    (f32.store (i32.const 24) (f32.const -1))
    (f32.store (i32.const 28) (f32.const inf))
    (f32.store (i32.const 32) (f32.const -100))
    (f32.store (i32.const 36) (f32.const 3.5))
    (f32.store (i32.const 40) (f32.const 1))
    (f32.store (i32.const 44) (f32.const -1e30))
    (f32.store (i32.const 48) (f32.const 42))
    (i32.const 13))

  ;; n f32 zeros at byte 1024, all +0 but for one -0 at index k.
  (func $zeros (param $n i32) (param $k i32)
    (local $i i32)
    (loop $F
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then
          (f32.store (i32.add (i32.const 1024) (i32.mul (local.get $i) (i32.const 4)))
            (select (f32.const -0) (f32.const 0)
                    (i32.eq (local.get $i) (local.get $k))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $F)))))

  (func $min32 (param $base i32) (param $n i32) (param $seed f32) (result f32)
    (local $i i32) (local $m f32)
    (local.set $m (local.get $seed))
    (loop $L
      (if (i32.lt_s (local.get $i) (local.get $n))
        (then
          (local.set $m (f32.min (local.get $m)
            (f32.load (i32.add (local.get $base) (i32.mul (local.get $i) (i32.const 4))))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $m))

  (func $max32 (param $base i32) (param $n i32) (param $seed f32) (result f32)
    (local $i i32) (local $m f32)
    (local.set $m (local.get $seed))
    (loop $L
      (if (i32.lt_s (local.get $i) (local.get $n))
        (then
          (local.set $m (f32.max
            (f32.load (i32.add (local.get $base) (i32.mul (local.get $i) (i32.const 4))))
            (local.get $m)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $m))

  (func (export "f_min") (result f32)
    (call $min32 (i32.const 0) (call $fill) (f32.const inf)))
  (func (export "f_max") (result f32)
    (call $max32 (i32.const 0) (call $fill) (f32.const -inf)))

  ;; Every element above zero for the min and below it for the max, seeded
  ;; with the opposite infinity: an accumulator started at 0, or at the
  ;; wrong infinity, answers 0 or that infinity here.
  (func $ramp (param $sign f32) (result i32)
    (local $i i32)
    (loop $F
      (if (i32.lt_u (local.get $i) (i32.const 13))
        (then
          (f32.store (i32.mul (local.get $i) (i32.const 4))
            (f32.mul (local.get $sign)
              (f32.convert_i32_u (i32.add (i32.rem_u (i32.mul (local.get $i) (i32.const 5))
                                                     (i32.const 13))
                                          (i32.const 2)))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $F))))
    (i32.const 13))
  (func (export "f_min_pos") (result f32)
    (call $min32 (i32.const 0) (call $ramp (f32.const 0.5)) (f32.const inf)))
  (func (export "f_max_neg") (result f32)
    (call $max32 (i32.const 0) (call $ramp (f32.const -0.5)) (f32.const -inf)))

  ;; Seeded from the first element, as Go's `lo := a[0]` is: the value the
  ;; loop is entered with is not the identity the vector accumulator holds.
  (func (export "f_min_seeded") (result f32)
    (local $n i32)
    (local.set $n (call $fill))
    (call $min32 (i32.const 0) (local.get $n) (f32.load (i32.const 0))))

  ;; Both in one loop, the shape of a minmax() function; returned as their
  ;; difference so one result carries both.
  (func (export "f_both") (result f32)
    (local $i i32) (local $n i32) (local $lo f32) (local $hi f32) (local $v f32)
    (local.set $n (call $fill))
    ;; drop the infinities so the difference is finite
    (f32.store (i32.const 16) (f32.const -7))
    (f32.store (i32.const 28) (f32.const 9))
    (local.set $lo (f32.load (i32.const 0)))
    (local.set $hi (local.get $lo))
    (loop $L
      (if (i32.lt_s (local.get $i) (local.get $n))
        (then
          (local.set $v (f32.load (i32.mul (local.get $i) (i32.const 4))))
          (local.set $lo (f32.min (local.get $lo) (local.get $v)))
          (local.set $hi (f32.max (local.get $hi) (local.get $v)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (f32.sub (local.get $hi) (local.get $lo)))

  ;; ---- the zeros ----------------------------------------------------------
  ;; 13 zeros, the -0 in the first vector, in the last whole one, in the
  ;; tail; and a max over the same, which must say +0 every time.
  (func (export "z_min_body") (result f32)
    (call $zeros (i32.const 13) (i32.const 1))
    (call $min32 (i32.const 1024) (i32.const 13) (f32.const 0)))
  (func (export "z_min_late") (result f32)
    (call $zeros (i32.const 13) (i32.const 11))
    (call $min32 (i32.const 1024) (i32.const 13) (f32.const 0)))
  (func (export "z_min_tail") (result f32)
    (call $zeros (i32.const 13) (i32.const 12))
    (call $min32 (i32.const 1024) (i32.const 13) (f32.const 0)))
  (func (export "z_max_body") (result f32)
    (call $zeros (i32.const 13) (i32.const 2))
    (call $max32 (i32.const 1024) (i32.const 13) (f32.const -0)))
  ;; All -0 but the seed: the max is the seed's +0 ...
  (func (export "z_max_seed") (result f32)
    (local $i i32)
    (call $zeros (i32.const 13) (i32.const 99))
    (loop $F
      (if (i32.lt_u (local.get $i) (i32.const 13))
        (then
          (f32.store (i32.add (i32.const 1024) (i32.mul (local.get $i) (i32.const 4)))
                     (f32.const -0))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $F))))
    (call $max32 (i32.const 1024) (i32.const 13) (f32.const 0)))
  ;; ... and the min of +0s seeded with -0 is the seed.
  (func (export "z_min_seed") (result f32)
    (call $zeros (i32.const 13) (i32.const 99))
    (call $min32 (i32.const 1024) (i32.const 13) (f32.const -0)))

  ;; ---- NaN ------------------------------------------------------------------
  (func (export "nan_body") (result f32)
    (drop (call $fill))
    (f32.store (i32.const 20) (f32.const nan))
    (call $min32 (i32.const 0) (i32.const 13) (f32.const inf)))
  (func (export "nan_tail") (result f32)
    (drop (call $fill))
    (f32.store (i32.const 48) (f32.const nan:0x200000))
    (call $max32 (i32.const 0) (i32.const 13) (f32.const -inf)))
  (func (export "nan_seed") (result f32)
    (call $min32 (i32.const 0) (call $fill) (f32.const nan)))
  ;; A NaN is no number to compare: the answer is NaN, not the smallest one.
  (func (export "nan_isnan") (result i32)
    (local $m f32)
    (drop (call $fill))
    (f32.store (i32.const 36) (f32.const -nan))
    (local.set $m (call $max32 (i32.const 0) (i32.const 13) (f32.const -inf)))
    (f32.ne (local.get $m) (local.get $m)))

  ;; ---- short and empty ------------------------------------------------------
  (func (export "f_short") (result f32)
    (call $min32 (i32.const 0) (i32.const 3) (f32.const inf)))
  (func (export "f_empty") (result f32)
    (drop (call $fill))
    (call $min32 (i32.const 0) (i32.const 0) (f32.const 1234.5)))

  ;; ---- f64, two lanes a vector ------------------------------------------------
  (func (export "d_minmax") (result f64)
    (local $i i32) (local $lo f64) (local $hi f64) (local $v f64)
    (loop $F
      (if (i32.lt_u (local.get $i) (i32.const 23))
        (then
          (f64.store (i32.mul (local.get $i) (i32.const 8))
            (f64.mul (f64.convert_i32_s (i32.sub (i32.rem_u (i32.mul (local.get $i)
                                                                     (i32.const 37))
                                                            (i32.const 101))
                                                 (i32.const 50)))
                     (f64.const 1.25)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $F))))
    (local.set $lo (f64.const inf))
    (local.set $hi (f64.const -inf))
    (local.set $i (i32.const 0))
    (loop $L
      (if (i32.lt_s (local.get $i) (i32.const 23))
        (then
          (local.set $v (f64.load (i32.mul (local.get $i) (i32.const 8))))
          (local.set $lo (f64.min (local.get $lo) (local.get $v)))
          (local.set $hi (f64.max (local.get $hi) (local.get $v)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (f64.add (f64.mul (local.get $hi) (f64.const 1000)) (local.get $lo)))

  ;; ---- lane-wise, not a reduction -------------------------------------------
  ;; c[i] = min(a[i], b[i]) over 1021 elements with zeros of both signs, a NaN
  ;; and infinities among them; summed back as bit patterns, position-weighted
  ;; so a lane put in the wrong place shows.
  (func (export "lanewise") (result i32)
    (local $i i32) (local $s i32) (local $x f32)
    (loop $F
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $x (f32.convert_i32_s
            (i32.sub (i32.rem_u (i32.mul (local.get $i) (i32.const 53)) (i32.const 199))
                     (i32.const 99))))
          (f32.store (i32.mul (local.get $i) (i32.const 4))
            (select (f32.const -0) (local.get $x)
                    (i32.eq (i32.rem_u (local.get $i) (i32.const 17)) (i32.const 3))))
          (f32.store (i32.add (i32.const 8192) (i32.mul (local.get $i) (i32.const 4)))
            (select (f32.const 0) (f32.neg (local.get $x))
                    (i32.eq (i32.rem_u (local.get $i) (i32.const 17)) (i32.const 3))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $F))))
    (f32.store (i32.const 400) (f32.const inf))
    (f32.store (i32.const 8592) (f32.const -inf))
    (f32.store (i32.const 404) (f32.const nan))
    (local.set $i (i32.const 0))
    (loop $L
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (f32.store (i32.add (i32.const 16384) (i32.mul (local.get $i) (i32.const 4)))
            (f32.min (f32.load (i32.mul (local.get $i) (i32.const 4)))
                     (f32.load (i32.add (i32.const 8192) (i32.mul (local.get $i) (i32.const 4))))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.set $i (i32.const 0))
    (loop $S
      (if (i32.lt_u (local.get $i) (i32.const 1021))
        (then
          (local.set $x (f32.load (i32.add (i32.const 16384) (i32.mul (local.get $i) (i32.const 4)))))
          (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
            ;; a NaN's payload is the producer's choice; count it as one value
            (select (i32.const 0x7fc00000) (i32.reinterpret_f32 (local.get $x))
                    (f32.ne (local.get $x) (local.get $x)))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $S))))
    (local.get $s))

  ;; The running minimum also summed, so the accumulator is live outside its
  ;; own update and the loop must not be vectorised.
  (func (export "escapes") (result f32)
    (local $i i32) (local $n i32) (local $m f32) (local $t f32)
    (local.set $n (call $fill))
    (f32.store (i32.const 16) (f32.const -7))
    (f32.store (i32.const 28) (f32.const 9))
    (local.set $m (f32.const 1000))
    (loop $L
      (if (i32.lt_s (local.get $i) (local.get $n))
        (then
          (local.set $m (f32.min (local.get $m)
            (f32.load (i32.mul (local.get $i) (i32.const 4)))))
          (local.set $t (f32.add (local.get $t) (local.get $m)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $L))))
    (local.get $t))
)
