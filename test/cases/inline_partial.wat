;; Partial inlining (inline.c pinl_build): a callee the inliner will not
;; take whole -- it calls, or loops -- whose start returns early, taken in
;; up to where it would call or loop.  Each way from that start into the
;; rest becomes a call of the callee itself with the site's arguments, which
;; runs the start again: so the start may read and compare on the way to
;; such a cut but not write, and a callee that writes first must not be
;; taken in that way (bump_then_test).  Covered: a grow-style callee in a
;; loop, fast most trips and slow on some; two results; early exits through
;; a br_table; a slow way that throws, caught around the site (the site's
;; handler edges go to the cut's call); a recursive callee whose callers take
;; in its base case; and a start whose only write is on its way to a return.
;; Each slow way makes five calls, more than the inliner takes a callee
;; with (inl_callee), so these callees are partial inlining's.  Everything
;; is witnessed through memory so a wrong way cannot cancel out to the
;; right answer; wasm-interp referees every export.
(module
  (memory 1)
  (tag $t)
  (global $calls (mut i32) (i32.const 0))

  ;; the slow work, recursive so that it stays a call wherever it is made
  (func $fill (param $base i32) (param $n i32)
    (if (i32.eqz (local.get $n)) (then (return)))
    (i32.store (local.get $base) (i32.mul (local.get $n) (i32.const 3)))
    (call $fill (i32.add (local.get $base) (i32.const 4))
                (i32.sub (local.get $n) (i32.const 1))))

  ;; capacity at 0, length of the buffer at 4: grows by doubling when need
  ;; is past capacity, and returns the capacity it has
  (func $ensure (param $need i32) (result i32)
    (local $cap i32)
    (local.set $cap (i32.load (i32.const 0)))
    (if (i32.ge_s (local.get $cap) (local.get $need))
      (then (return (local.get $cap))))
    (global.set $calls (i32.add (global.get $calls) (i32.const 1)))
    (loop $dbl
      (local.set $cap (i32.add (i32.shl (local.get $cap) (i32.const 1)) (i32.const 1)))
      (br_if $dbl (i32.lt_s (local.get $cap) (local.get $need))))
    (call $fill (i32.const 1024) (local.get $cap))
    (call $fill (i32.const 1024) (local.get $cap))
    (call $fill (i32.const 1024) (local.get $cap))
    (call $fill (i32.const 1024) (local.get $cap))
    (call $fill (i32.const 1024) (local.get $cap))
    (i32.store (i32.const 0) (local.get $cap))
    (local.get $cap))

  (func (export "grow_in_loop") (result i32)
    (local $i i32) (local $sum i32)
    (i32.store (i32.const 0) (i32.const 4))
    (global.set $calls (i32.const 0))
    (loop $l
      (local.set $sum (i32.add (local.get $sum) (call $ensure (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 200))))
    (i32.add (i32.add (local.get $sum) (i32.mul (global.get $calls) (i32.const 100000)))
             (i32.load (i32.const 1060))))

  ;; two results, the early way and the slow one
  (func $pair (param $x i32) (result i32 i32)
    (if (i32.lt_u (local.get $x) (i32.const 10))
      (then (return (local.get $x) (i32.mul (local.get $x) (i32.const 2)))))
    (call $fill (i32.const 2048) (i32.const 4))
    (call $fill (i32.const 2112) (i32.const 4))
    (call $fill (i32.const 2176) (i32.const 4))
    (call $fill (i32.const 2240) (i32.const 4))
    (call $fill (i32.const 2304) (i32.const 4))
    (i32.load (i32.const 2052))
    (i32.add (local.get $x) (i32.load (i32.const 2056))))

  (func (export "two_results") (result i32)
    (local $i i32) (local $acc i32)
    (loop $l
      (call $pair (local.get $i))
      (local.set $acc (i32.add (local.get $acc)))
      (local.set $acc (i32.add (local.get $acc)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 14))))
    (local.get $acc))

  ;; early ways out of a br_table; the default goes slow
  (func $classify (param $k i32) (result i32)
    (block $slow
      (block $c2
        (block $c1
          (block $c0
            (br_table $c0 $c1 $c2 $slow (local.get $k)))
          (return (i32.const 11)))
        (return (i32.const 22)))
      (return (i32.add (i32.load (i32.const 0)) (i32.const 33))))
    (call $fill (i32.const 3072) (i32.const 2))
    (call $fill (i32.const 3136) (i32.const 2))
    (call $fill (i32.const 3200) (i32.const 2))
    (call $fill (i32.const 3264) (i32.const 2))
    (call $fill (i32.const 3328) (i32.const 2))
    (i32.add (local.get $k) (i32.load (i32.const 3076))))

  (func (export "table_exits") (result i32)
    (local $i i32) (local $acc i32)
    (i32.store (i32.const 0) (i32.const 7))
    (loop $l
      (local.set $acc (i32.add (i32.mul (local.get $acc) (i32.const 3))
                               (call $classify (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 6))))
    (local.get $acc))

  ;; a slow way that throws on 13 and returns on the rest past 12, the site
  ;; in a try
  (func $checked (param $x i32) (result i32)
    (if (i32.lt_u (local.get $x) (i32.const 12))
      (then (return (i32.add (local.get $x) (i32.const 1)))))
    (call $fill (i32.const 4096) (i32.const 3))
    (call $fill (i32.const 4160) (i32.const 3))
    (call $fill (i32.const 4224) (i32.const 3))
    (call $fill (i32.const 4288) (i32.const 3))
    (call $fill (i32.const 4352) (i32.const 3))
    (i32.store (i32.const 16) (i32.add (i32.load (i32.const 16)) (i32.load (i32.const 4100))))
    (if (i32.eq (local.get $x) (i32.const 13)) (then (throw $t)))
    (i32.mul (local.get $x) (i32.const 5)))

  (func (export "caught_slow") (result i32)
    (local $i i32) (local $acc i32)
    (loop $l
      (block $out
        (try_table (catch $t $out)
          (local.set $acc (i32.add (local.get $acc) (call $checked (local.get $i))))
          (br $out)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 20))))
    (i32.add (local.get $acc) (i32.mul (i32.load (i32.const 16)) (i32.const 1000))))

  ;; recursive: its callers take in the base case
  (func $fib (param $n i32) (result i32)
    (if (i32.lt_u (local.get $n) (i32.const 2)) (then (return (local.get $n))))
    (i32.add (call $fib (i32.sub (local.get $n) (i32.const 1)))
             (call $fib (i32.sub (local.get $n) (i32.const 2)))))

  (func (export "fib_callers") (result i32)
    (local $i i32) (local $acc i32)
    (loop $l
      (local.set $acc (i32.add (local.get $acc) (call $fib (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 16))))
    (local.get $acc))

  ;; writes before its test: taken in partially, the write would run twice
  ;; on the slow way
  (func $bump_then_test (param $x i32) (result i32)
    (global.set $calls (i32.add (global.get $calls) (i32.const 1)))
    (if (i32.lt_u (local.get $x) (i32.const 5)) (then (return (global.get $calls))))
    (call $fill (i32.const 5120) (i32.const 2))
    (call $fill (i32.const 5184) (i32.const 2))
    (call $fill (i32.const 5248) (i32.const 2))
    (call $fill (i32.const 5312) (i32.const 2))
    (call $fill (i32.const 5376) (i32.const 2))
    (i32.add (global.get $calls) (i32.const 1000)))

  (func (export "write_first") (result i32)
    (local $i i32) (local $acc i32)
    (global.set $calls (i32.const 0))
    (loop $l
      (local.set $acc (i32.add (local.get $acc) (call $bump_then_test (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 9))))
    (i32.add (local.get $acc) (i32.mul (global.get $calls) (i32.const 100))))

  ;; a memory write before the test, the same
  (func $store_then_test (param $x i32) (result i32)
    (i32.store (i32.const 20) (i32.add (i32.load (i32.const 20)) (i32.const 1)))
    (if (i32.lt_u (local.get $x) (i32.const 3)) (then (return (i32.load (i32.const 20)))))
    (call $fill (i32.const 6144) (i32.const 2))
    (call $fill (i32.const 6208) (i32.const 2))
    (call $fill (i32.const 6272) (i32.const 2))
    (call $fill (i32.const 6336) (i32.const 2))
    (call $fill (i32.const 6400) (i32.const 2))
    (i32.load (i32.const 20)))

  (func (export "store_first") (result i32)
    (local $i i32) (local $acc i32)
    (i32.store (i32.const 20) (i32.const 0))
    (loop $l
      (local.set $acc (i32.add (local.get $acc) (call $store_then_test (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 7))))
    (i32.add (local.get $acc) (i32.mul (i32.load (i32.const 20)) (i32.const 100))))

  ;; a write only on the way to a return is the callee's to make once
  (func $test_then_note (param $x i32) (result i32)
    (if (i32.lt_u (local.get $x) (i32.const 4))
      (then
        (i32.store (i32.const 24) (i32.add (i32.load (i32.const 24)) (local.get $x)))
        (return (i32.load (i32.const 24)))))
    (call $fill (i32.const 7168) (i32.const 2))
    (call $fill (i32.const 7232) (i32.const 2))
    (call $fill (i32.const 7296) (i32.const 2))
    (call $fill (i32.const 7360) (i32.const 2))
    (call $fill (i32.const 7424) (i32.const 2))
    (i32.add (i32.load (i32.const 24)) (i32.load (i32.const 7172))))

  (func (export "write_on_return") (result i32)
    (local $i i32) (local $acc i32)
    (i32.store (i32.const 24) (i32.const 0))
    (loop $l
      (local.set $acc (i32.add (local.get $acc) (call $test_then_note (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 8))))
    (i32.add (local.get $acc) (i32.load (i32.const 24))))
)
