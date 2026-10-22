;; Index reductions beside a min or max (argmin, argmax), which the vectoriser
;; turns into a per-lane position kept beside each lane's best, resolved at
;; the exit to the first (strict compare) or last (non-strict) of the equal
;; best elements.  Each export scans an array of i32 in memory and returns
;; best * 65599 + index, over arrays whose length is no multiple of the
;; vector, with ties everywhere, with elements equal to the reduction's
;; identity, and with a starting best no element beats; signed and unsigned,
;; min and max, strict and not; the compare written either way round and the
;; select's arms either way round; an index offset from the counter, and a
;; loop that takes two elements per iteration.

(module
  (memory 1)
  (global $seed (mut i32) (i32.const 99991))

  (func $next (result i32)
    (global.set $seed (i32.add (i32.mul (global.get $seed) (i32.const 1103515245))
                               (i32.const 12345)))
    (global.get $seed))

  ;; fill n words at 0: mode 0 random, 1 few distinct values (ties), 2 all
  ;; equal, 3 mostly the extreme values, 4 small negatives
  (func $fill (param $n i32) (param $mode i32)
    (local $i i32) (local $x i32)
    (loop $l
      (local.set $x (call $next))
      (if (i32.eq (local.get $mode) (i32.const 1))
        (then (local.set $x (i32.and (i32.shr_u (local.get $x) (i32.const 16)) (i32.const 3)))))
      (if (i32.eq (local.get $mode) (i32.const 2))
        (then (local.set $x (i32.const 7))))
      (if (i32.eq (local.get $mode) (i32.const 3))
        (then (local.set $x (select (i32.const 0x7fffffff)
                              (select (i32.const 0x80000000) (i32.const -1)
                                      (i32.and (local.get $x) (i32.const 0x100)))
                              (i32.and (local.get $x) (i32.const 0x10000))))))
      (if (i32.eq (local.get $mode) (i32.const 4))
        (then (local.set $x (i32.sub (i32.const 0) (i32.and (i32.shr_u (local.get $x) (i32.const 20)) (i32.const 15))))))
      (i32.store (i32.shl (local.get $i) (i32.const 2)) (local.get $x))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (local.get $n)))))

  (func $pack (param $best i32) (param $at i32) (result i32)
    (i32.add (i32.mul (local.get $best) (i32.const 65599)) (local.get $at)))

  ;; signed min, first of equals: select(i, at, v < best)
  (func $argmin_first (param $n i32) (result i32)
    (local $i i32) (local $v i32) (local $best i32) (local $at i32) (local $c i32)
    (local.set $best (i32.load (i32.const 0)))
    (loop $l
      (local.set $v (i32.load (i32.shl (local.get $i) (i32.const 2))))
      (local.set $c (i32.lt_s (local.get $v) (local.get $best)))
      (local.set $at (select (local.get $i) (local.get $at) (local.get $c)))
      (local.set $best (select (local.get $v) (local.get $best) (local.get $c)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (local.get $n))))
    (call $pack (local.get $best) (local.get $at)))

  ;; signed max, last of equals: v >= best
  (func $argmax_last (param $n i32) (result i32)
    (local $i i32) (local $v i32) (local $best i32) (local $at i32) (local $c i32)
    (local.set $best (i32.const 0x80000000))
    (local.set $at (i32.const -1))
    (loop $l
      (local.set $v (i32.load (i32.shl (local.get $i) (i32.const 2))))
      (local.set $c (i32.ge_s (local.get $v) (local.get $best)))
      (local.set $at (select (local.get $i) (local.get $at) (local.get $c)))
      (local.set $best (select (local.get $v) (local.get $best) (local.get $c)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (local.get $n))))
    (call $pack (local.get $best) (local.get $at)))

  ;; unsigned min, the compare the other way round and the arms swapped:
  ;; keep while best <= v, so the first of equals; index offset by 1000
  (func $uargmin_swapped (param $n i32) (result i32)
    (local $i i32) (local $v i32) (local $best i32) (local $at i32) (local $keep i32)
    (local.set $best (i32.const -1))
    (local.set $at (i32.const 77))
    (loop $l
      (local.set $v (i32.load (i32.shl (local.get $i) (i32.const 2))))
      (local.set $keep (i32.le_u (local.get $best) (local.get $v)))
      (local.set $at (select (local.get $at) (i32.add (local.get $i) (i32.const 1000)) (local.get $keep)))
      (local.set $best (select (local.get $best) (local.get $v) (local.get $keep)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (local.get $n))))
    (call $pack (local.get $best) (local.get $at)))

  ;; unsigned max written as best < v (strict, first of equals)
  (func $uargmax_first (param $n i32) (result i32)
    (local $i i32) (local $v i32) (local $best i32) (local $at i32) (local $c i32)
    (loop $l
      (local.set $v (i32.load (i32.shl (local.get $i) (i32.const 2))))
      (local.set $c (i32.lt_u (local.get $best) (local.get $v)))
      (local.set $at (select (local.get $i) (local.get $at) (local.get $c)))
      (local.set $best (select (local.get $v) (local.get $best) (local.get $c)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (local.get $n))))
    (call $pack (local.get $best) (local.get $at)))

  ;; a starting best below every element: the index stays what it came in as
  (func $argmin_unbeaten (param $n i32) (result i32)
    (local $i i32) (local $v i32) (local $best i32) (local $at i32) (local $c i32)
    (local.set $best (i32.const 0x80000000))
    (local.set $at (i32.const 4242))
    (loop $l
      (local.set $v (i32.load (i32.shl (local.get $i) (i32.const 2))))
      (local.set $c (i32.lt_s (local.get $v) (local.get $best)))
      (local.set $at (select (local.get $i) (local.get $at) (local.get $c)))
      (local.set $best (select (local.get $v) (local.get $best) (local.get $c)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (local.get $n))))
    (call $pack (local.get $best) (local.get $at)))

  ;; two elements an iteration, each with its own select pair
  (func $argmin_by_two (param $n i32) (result i32)
    (local $i i32) (local $v i32) (local $w i32) (local $best i32) (local $at i32) (local $c i32)
    (local.set $best (i32.const 0x7fffffff))
    (loop $l
      (local.set $v (i32.load (i32.shl (local.get $i) (i32.const 2))))
      (local.set $c (i32.lt_s (local.get $v) (local.get $best)))
      (local.set $at (select (local.get $i) (local.get $at) (local.get $c)))
      (local.set $best (select (local.get $v) (local.get $best) (local.get $c)))
      (local.set $w (i32.load offset=4 (i32.shl (local.get $i) (i32.const 2))))
      (local.set $c (i32.lt_s (local.get $w) (local.get $best)))
      (local.set $at (select (i32.add (local.get $i) (i32.const 1)) (local.get $at) (local.get $c)))
      (local.set $best (select (local.get $w) (local.get $best) (local.get $c)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 2))) (local.get $n))))
    (call $pack (local.get $best) (local.get $at)))

  (func $run (param $f i32) (result i32)
    (local $mode i32) (local $len i32) (local $h i32) (local $r i32)
    (loop $modes
      (local.set $len (i32.const 1))
      (loop $lens
        (call $fill (i32.add (local.get $len) (i32.const 2)) (local.get $mode))
        (local.set $r
          (if (result i32) (i32.eqz (local.get $f)) (then (call $argmin_first (local.get $len)))
          (else (if (result i32) (i32.eq (local.get $f) (i32.const 1)) (then (call $argmax_last (local.get $len)))
          (else (if (result i32) (i32.eq (local.get $f) (i32.const 2)) (then (call $uargmin_swapped (local.get $len)))
          (else (if (result i32) (i32.eq (local.get $f) (i32.const 3)) (then (call $uargmax_first (local.get $len)))
          (else (if (result i32) (i32.eq (local.get $f) (i32.const 4)) (then (call $argmin_unbeaten (local.get $len)))
          (else (call $argmin_by_two (i32.and (i32.add (local.get $len) (i32.const 1)) (i32.const -2))))))))))))))
        (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31)) (local.get $r)))
        (local.set $len (i32.add (local.get $len) (select (i32.const 1) (i32.const 37)
                                                          (i32.lt_u (local.get $len) (i32.const 40)))))
        (br_if $lens (i32.lt_u (local.get $len) (i32.const 700))))
      (br_if $modes (i32.lt_u (local.tee $mode (i32.add (local.get $mode) (i32.const 1))) (i32.const 5))))
    (local.get $h))

  (func (export "argmin_first") (result i32) (call $run (i32.const 0)))
  (func (export "argmax_last") (result i32) (call $run (i32.const 1)))
  (func (export "uargmin_swapped") (result i32) (call $run (i32.const 2)))
  (func (export "uargmax_first") (result i32) (call $run (i32.const 3)))
  (func (export "argmin_unbeaten") (result i32) (call $run (i32.const 4)))
  (func (export "argmin_by_two") (result i32) (call $run (i32.const 5))))
