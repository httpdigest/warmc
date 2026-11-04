;; GC array bounds checks the ranges of their indices remove (opt.c
;; rp_inbounds) -- no compare of the index against the length unsigned, but
;; facts that put it in [0, length): signed compares, a loop counting up from
;; 0 or down to >= 0, i - 1 and i + 1 under tests, a mask by length - 1, a
;; remainder, an i64 wrapped, a do-while's i != last, a sieve's i <= n into
;; an array of n + 1, a select's clamp, a length tested above a constant.
;; Each has a twin one step off whose check must stay, so the read past the
;; end traps at every level.  The count is of the checks taken off, before
;; -O2's unrolling copies a loop's read with its flags, a loop copied behind
;; a test of its bounds (pass_bce_version) counting its copy's too.
;; stats -O2: bce_ranges=21
;; run: up 10 => i32:45
;; run: up 0 => i32:0
;; run: up_le 10 => trap: out of bounds array access
;; run: up_from 10 -1 => trap: out of bounds array access
;; run: down 10 => i32:30
;; run: down 0 => i32:0
;; run: down_gt 10 => trap: out of bounds array access
;; run: down_len 10 => trap: out of bounds array access
;; run: sub1 10 5 => i32:5
;; run: sub1 10 0 => i32:0
;; run: sub1 10 10 => i32:0
;; run: sub1_nolo 10 0 => trap: out of bounds array access
;; run: plus1 10 8 => i32:5
;; run: plus1 10 9 => i32:0
;; run: plus1_len 10 9 => trap: out of bounds array access
;; run: mask 6 13 => i32:5
;; run: mask 0 3 => i32:0
;; run: mask_noguard 0 3 => trap: out of bounds array access
;; run: remu 6 100 => i32:5
;; run: remu 0 5 => trap: integer divide by zero
;; run: rems 6 7 => i32:5
;; run: rems 6 -7 => i32:0
;; run: rems_neg 6 -7 => trap: out of bounds array access
;; run: wide 5 3 => i32:5
;; run: wide 5 -1 => i32:0
;; run: wide 5 -4294967289 => i32:0
;; run: wide_neg 5 -4294967289 => trap: out of bounds array access
;; run: wide_loop 5 => i32:10
;; run: wide_from 5 -4294967291 => trap: out of bounds array access
;; run: ne_loop 5 => i32:5
;; run: ne_loop 0 => i32:0
;; run: ne_loop_len 5 => trap: out of bounds array access
;; run: step 6 => i32:3
;; run: step 5 => trap: out of bounds array access
;; run: sieve 100 => i32:99
;; run: sieve_over 100 => trap: out of bounds array access
;; run: sieve_wide 100 => i32:74
;; run: sieve_wide_over 99 => trap: out of bounds array access
;; run: clamp 5 100 => i32:5
;; run: clamp 0 3 => i32:0
;; run: len_gt 5 3 => i32:5
;; run: len_gt 3 3 => i32:0
;; run: len_gt2 3 2 => trap: out of bounds array access
;; run: two 5 => trap: out of bounds array access
;; run: wrap_up 5 0 10 => i32:5
;; run: wrap_up 5 2147483632 -2147483643 => trap: out of bounds array access
(module
  (type $a (array (mut i32)))

  ;; for (i = 0; i < a.length; i++) a[i] = i; the sum
  (func $up_from (export "up_from") (param $n i32) (param $k i32) (result i32)
    (local $a (ref $a)) (local $i i32) (local $s i32)
    (local.set $a (array.new_default $a (local.get $n)))
    (local.set $i (local.get $k))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (array.len (local.get $a))))
        (array.set $a (local.get $a) (local.get $i) (local.get $i))
        (local.set $s (i32.add (local.get $s) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func (export "up") (param $n i32) (result i32)
    (local $a (ref $a)) (local $i i32) (local $s i32)
    (local.set $a (array.new_default $a (local.get $n)))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (array.len (local.get $a))))
        (array.set $a (local.get $a) (local.get $i) (local.get $i))
        (local.set $s (i32.add (local.get $s) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  ;; i <= a.length: one past the end
  (func (export "up_le") (param $n i32) (result i32)
    (local $a (ref $a)) (local $i i32) (local $s i32)
    (local.set $a (array.new_default $a (local.get $n)))
    (block $done
      (loop $l
        (br_if $done (i32.gt_s (local.get $i) (array.len (local.get $a))))
        (array.set $a (local.get $a) (local.get $i) (local.get $i))
        (local.set $s (i32.add (local.get $s) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))

  ;; for (i = a.length - 1; i >= 0; i--) s += a[i]
  (func (export "down") (param $n i32) (result i32)
    (local $a (ref $a)) (local $i i32) (local $s i32)
    (local.set $a (array.new $a (i32.const 3) (local.get $n)))
    (local.set $i (i32.sub (array.len (local.get $a)) (i32.const 1)))
    (block $done
      (loop $l
        (br_if $done (i32.lt_s (local.get $i) (i32.const 0)))
        (local.set $s (i32.add (local.get $s) (array.get $a (local.get $a) (local.get $i))))
        (local.set $i (i32.sub (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  ;; i > -2: one below the start
  (func (export "down_gt") (param $n i32) (result i32)
    (local $a (ref $a)) (local $i i32) (local $s i32)
    (local.set $a (array.new $a (i32.const 3) (local.get $n)))
    (local.set $i (i32.sub (array.len (local.get $a)) (i32.const 1)))
    (block $done
      (loop $l
        (br_if $done (i32.le_s (local.get $i) (i32.const -2)))
        (local.set $s (i32.add (local.get $s) (array.get $a (local.get $a) (local.get $i))))
        (local.set $i (i32.sub (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  ;; from a.length: one past the end
  (func (export "down_len") (param $n i32) (result i32)
    (local $a (ref $a)) (local $i i32) (local $s i32)
    (local.set $a (array.new $a (i32.const 3) (local.get $n)))
    (local.set $i (array.len (local.get $a)))
    (block $done
      (loop $l
        (br_if $done (i32.lt_s (local.get $i) (i32.const 0)))
        (local.set $s (i32.add (local.get $s) (array.get $a (local.get $a) (local.get $i))))
        (local.set $i (i32.sub (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))

  ;; a[i - 1] where 1 <= i < a.length
  (func (export "sub1") (param $n i32) (param $i i32) (result i32)
    (local $a (ref $a))
    (local.set $a (array.new $a (i32.const 5) (local.get $n)))
    (if (i32.ge_s (local.get $i) (array.len (local.get $a))) (then (return (i32.const 0))))
    (if (i32.lt_s (local.get $i) (i32.const 1)) (then (return (i32.const 0))))
    (array.get $a (local.get $a) (i32.sub (local.get $i) (i32.const 1))))
  ;; without the i >= 1
  (func (export "sub1_nolo") (param $n i32) (param $i i32) (result i32)
    (local $a (ref $a))
    (local.set $a (array.new $a (i32.const 5) (local.get $n)))
    (if (i32.ge_s (local.get $i) (array.len (local.get $a))) (then (return (i32.const 0))))
    (array.get $a (local.get $a) (i32.sub (local.get $i) (i32.const 1))))

  ;; a[i + 1] where 0 <= i < a.length - 1
  (func (export "plus1") (param $n i32) (param $i i32) (result i32)
    (local $a (ref $a))
    (local.set $a (array.new $a (i32.const 5) (local.get $n)))
    (if (i32.lt_s (local.get $i) (i32.const 0)) (then (return (i32.const 0))))
    (if (i32.ge_s (local.get $i) (i32.sub (array.len (local.get $a)) (i32.const 1)))
      (then (return (i32.const 0))))
    (array.get $a (local.get $a) (i32.add (local.get $i) (i32.const 1))))
  ;; i < a.length: one past the end
  (func (export "plus1_len") (param $n i32) (param $i i32) (result i32)
    (local $a (ref $a))
    (local.set $a (array.new $a (i32.const 5) (local.get $n)))
    (if (i32.lt_s (local.get $i) (i32.const 0)) (then (return (i32.const 0))))
    (if (i32.ge_s (local.get $i) (array.len (local.get $a))) (then (return (i32.const 0))))
    (array.get $a (local.get $a) (i32.add (local.get $i) (i32.const 1))))

  ;; a[h & (a.length - 1)] once a.length != 0
  (func (export "mask") (param $n i32) (param $h i32) (result i32)
    (local $a (ref $a))
    (local.set $a (array.new $a (i32.const 5) (local.get $n)))
    (if (i32.eqz (array.len (local.get $a))) (then (return (i32.const 0))))
    (array.get $a (local.get $a)
      (i32.and (local.get $h) (i32.sub (array.len (local.get $a)) (i32.const 1)))))
  ;; of a.length 0, h & -1 is h
  (func (export "mask_noguard") (param $n i32) (param $h i32) (result i32)
    (local $a (ref $a))
    (local.set $a (array.new $a (i32.const 5) (local.get $n)))
    (array.get $a (local.get $a)
      (i32.and (local.get $h) (i32.sub (array.len (local.get $a)) (i32.const 1)))))

  ;; a[h %u a.length]: of length 0, the remainder traps first
  (func (export "remu") (param $n i32) (param $h i32) (result i32)
    (local $a (ref $a))
    (local.set $a (array.new $a (i32.const 5) (local.get $n)))
    (array.get $a (local.get $a) (i32.rem_u (local.get $h) (array.len (local.get $a)))))

  ;; a[h %s a.length] where h >= 0
  (func (export "rems") (param $n i32) (param $h i32) (result i32)
    (local $a (ref $a))
    (local.set $a (array.new $a (i32.const 5) (local.get $n)))
    (if (i32.lt_s (local.get $h) (i32.const 0)) (then (return (i32.const 0))))
    (array.get $a (local.get $a) (i32.rem_s (local.get $h) (array.len (local.get $a)))))
  ;; of h < 0, the remainder is negative
  (func (export "rems_neg") (param $n i32) (param $h i32) (result i32)
    (local $a (ref $a))
    (local.set $a (array.new $a (i32.const 5) (local.get $n)))
    (array.get $a (local.get $a) (i32.rem_s (local.get $h) (array.len (local.get $a)))))

  ;; a[(int) j] where 0 <= j < a.length, j an i64
  (func (export "wide") (param $n i32) (param $j i64) (result i32)
    (local $a (ref $a))
    (local.set $a (array.new $a (i32.const 5) (local.get $n)))
    (if (i64.lt_s (local.get $j) (i64.const 0)) (then (return (i32.const 0))))
    (if (i64.ge_s (local.get $j) (i64.extend_i32_u (array.len (local.get $a))))
      (then (return (i32.const 0))))
    (array.get $a (local.get $a) (i32.wrap_i64 (local.get $j))))
  ;; without j >= 0, -2^32 + 7 is below the length and wraps to 7
  (func (export "wide_neg") (param $n i32) (param $j i64) (result i32)
    (local $a (ref $a))
    (local.set $a (array.new $a (i32.const 5) (local.get $n)))
    (if (i64.ge_s (local.get $j) (i64.extend_i32_u (array.len (local.get $a))))
      (then (return (i32.const 0))))
    (array.get $a (local.get $a) (i32.wrap_i64 (local.get $j))))

  ;; for (long j = 0; j < a.length; j++) s += a[(int) j]
  (func (export "wide_loop") (param $n i32) (result i32)
    (local $a (ref $a)) (local $j i64) (local $s i32)
    (local.set $a (array.new $a (i32.const 2) (local.get $n)))
    (block $done
      (loop $l
        (br_if $done (i64.ge_s (local.get $j) (i64.extend_i32_s (array.len (local.get $a)))))
        (local.set $s (i32.add (local.get $s)
          (array.get $a (local.get $a) (i32.wrap_i64 (local.get $j)))))
        (local.set $j (i64.add (local.get $j) (i64.const 1)))
        (br $l)))
    (local.get $s))
  ;; from k, which wraps into the array
  (func (export "wide_from") (param $n i32) (param $k i64) (result i32)
    (local $a (ref $a)) (local $j i64) (local $s i32)
    (local.set $a (array.new $a (i32.const 2) (local.get $n)))
    (local.set $j (local.get $k))
    (block $done
      (loop $l
        (br_if $done (i64.ge_s (local.get $j) (i64.extend_i32_s (array.len (local.get $a)))))
        (local.set $s (i32.add (local.get $s)
          (array.get $a (local.get $a) (i32.wrap_i64 (local.get $j)))))
        (local.set $j (i64.add (local.get $j) (i64.const 1)))
        (br $l)))
    (local.get $s))

  ;; Kotlin's `for (i in a.indices)`: last = a.length - 1, then
  ;; do { s += a[i] } while (i++ != last)
  (func (export "ne_loop") (param $n i32) (result i32)
    (local $a (ref $a)) (local $i i32) (local $last i32) (local $s i32)
    (local.set $a (array.new $a (i32.const 1) (local.get $n)))
    (local.set $last (i32.sub (array.len (local.get $a)) (i32.const 1)))
    (if (i32.gt_s (i32.const 0) (local.get $last)) (then (return (i32.const 0))))
    (loop $l
      (local.set $s (i32.add (local.get $s) (array.get $a (local.get $a) (local.get $i))))
      (if (i32.ne (local.get $i) (local.get $last))
        (then (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $l))))
    (local.get $s))
  ;; last = a.length
  (func (export "ne_loop_len") (param $n i32) (result i32)
    (local $a (ref $a)) (local $i i32) (local $last i32) (local $s i32)
    (local.set $a (array.new $a (i32.const 1) (local.get $n)))
    (local.set $last (array.len (local.get $a)))
    (if (i32.gt_s (i32.const 0) (local.get $last)) (then (return (i32.const 0))))
    (loop $l
      (local.set $s (i32.add (local.get $s) (array.get $a (local.get $a) (local.get $i))))
      (if (i32.ne (local.get $i) (local.get $last))
        (then (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $l))))
    (local.get $s))

  ;; i = 0; while (i != a.length) { s += a[i]; i += 2 }: of an odd length, i
  ;; steps over it
  (func (export "step") (param $n i32) (result i32)
    (local $a (ref $a)) (local $i i32) (local $s i32)
    (local.set $a (array.new $a (i32.const 1) (local.get $n)))
    (block $done
      (loop $l
        (br_if $done (i32.eq (local.get $i) (array.len (local.get $a))))
        (local.set $s (i32.add (local.get $s) (array.get $a (local.get $a) (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 2)))
        (br $l)))
    (local.get $s))

  ;; a of n + 1; for (i = 2; i <= n; i++) s += a[i]
  (func (export "sieve") (param $n i32) (result i32)
    (local $a (ref $a)) (local $i i32) (local $s i32)
    (local.set $a (array.new $a (i32.const 1) (i32.add (local.get $n) (i32.const 1))))
    (local.set $i (i32.const 2))
    (block $done
      (loop $l
        (br_if $done (i32.gt_s (local.get $i) (local.get $n)))
        (local.set $s (i32.add (local.get $s) (array.get $a (local.get $a) (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  ;; i <= n + 1
  (func (export "sieve_over") (param $n i32) (result i32)
    (local $a (ref $a)) (local $i i32) (local $s i32)
    (local.set $a (array.new $a (i32.const 1) (i32.add (local.get $n) (i32.const 1))))
    (local.set $i (i32.const 2))
    (block $done
      (loop $l
        (br_if $done (i32.gt_s (local.get $i) (i32.add (local.get $n) (i32.const 1))))
        (local.set $s (i32.add (local.get $s) (array.get $a (local.get $a) (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))

  ;; for (i = 2; i * i <= n; i++) for (long j = i * i; j <= n + k; j += i)
  ;; a[(int) j] = 1, a of n + 1; the count of what was struck
  (func $sieve_wide_k (param $n i32) (param $k i32) (result i32)
    (local $a (ref $a)) (local $i i32) (local $j i64) (local $s i32) (local $lim i64)
    (local.set $a (array.new $a (i32.const 0) (i32.add (local.get $n) (i32.const 1))))
    (local.set $lim (i64.extend_i32_s (i32.add (local.get $n) (local.get $k))))
    (local.set $i (i32.const 2))
    (block $done
      (loop $l
        (br_if $done (i64.gt_s (i64.mul (i64.extend_i32_s (local.get $i))
                                        (i64.extend_i32_s (local.get $i)))
                               (i64.extend_i32_s (local.get $n))))
        (local.set $j (i64.mul (i64.extend_i32_s (local.get $i)) (i64.extend_i32_s (local.get $i))))
        (block $jd
          (loop $jl
            (br_if $jd (i64.gt_s (local.get $j) (local.get $lim)))
            (array.set $a (local.get $a) (i32.wrap_i64 (local.get $j)) (i32.const 1))
            (local.set $j (i64.add (local.get $j) (i64.extend_i32_s (local.get $i))))
            (br $jl)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.set $i (i32.const 0))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (array.len (local.get $a))))
        (local.set $s (i32.add (local.get $s) (array.get $a (local.get $a) (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func (export "sieve_wide") (param $n i32) (result i32)
    (call $sieve_wide_k (local.get $n) (i32.const 0)))
  (func (export "sieve_wide_over") (param $n i32) (result i32)
    (call $sieve_wide_k (local.get $n) (i32.const 1)))

  ;; j = i < a.length ? i : a.length - 1, then a[j] where j >= 0
  (func (export "clamp") (param $n i32) (param $i i32) (result i32)
    (local $a (ref $a)) (local $j i32)
    (local.set $a (array.new $a (i32.const 5) (local.get $n)))
    (local.set $j (select (local.get $i) (i32.sub (array.len (local.get $a)) (i32.const 1))
                          (i32.lt_s (local.get $i) (array.len (local.get $a)))))
    (if (i32.lt_s (local.get $j) (i32.const 0)) (then (return (i32.const 0))))
    (array.get $a (local.get $a) (local.get $j)))

  ;; a[3] where a.length > m >= 3
  (func (export "len_gt") (param $n i32) (param $m i32) (result i32)
    (local $a (ref $a))
    (local.set $a (array.new $a (i32.const 5) (local.get $n)))
    (if (i32.le_s (array.len (local.get $a)) (local.get $m)) (then (return (i32.const 0))))
    (if (i32.lt_s (local.get $m) (i32.const 3)) (then (return (i32.const 0))))
    (array.get $a (local.get $a) (i32.const 3)))
  ;; m >= 2: a.length 3 is enough for the tests
  (func (export "len_gt2") (param $n i32) (param $m i32) (result i32)
    (local $a (ref $a))
    (local.set $a (array.new $a (i32.const 5) (local.get $n)))
    (if (i32.le_s (array.len (local.get $a)) (local.get $m)) (then (return (i32.const 0))))
    (if (i32.lt_s (local.get $m) (i32.const 2)) (then (return (i32.const 0))))
    (array.get $a (local.get $a) (i32.const 3)))

  ;; i < a.length, read of b, one shorter
  (func (export "two") (param $n i32) (result i32)
    (local $a (ref $a)) (local $b (ref $a)) (local $i i32) (local $s i32)
    (local.set $a (array.new $a (i32.const 1) (local.get $n)))
    (local.set $b (array.new $a (i32.const 1) (i32.sub (local.get $n) (i32.const 1))))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (array.len (local.get $a))))
        (local.set $s (i32.add (local.get $s) (array.get $a (local.get $b) (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))

  ;; i from start >= 0 up by one until stop -- through 2^31, where it wraps
  ;; negative -- reading a[i] where i < a.length
  (func (export "wrap_up") (param $n i32) (param $start i32) (param $stop i32) (result i32)
    (local $a (ref $a)) (local $i i32) (local $s i32)
    (local.set $a (array.new $a (i32.const 1) (local.get $n)))
    (if (i32.lt_s (local.get $start) (i32.const 0)) (then (return (i32.const 0))))
    (local.set $i (local.get $start))
    (loop $l
      (if (i32.lt_s (local.get $i) (array.len (local.get $a)))
        (then (local.set $s (i32.add (local.get $s)
                (array.get $a (local.get $a) (local.get $i))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.ne (local.get $i) (local.get $stop))))
    (local.get $s))
)
