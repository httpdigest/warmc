;; Constants that take a movz/movk chain are built once per stretch of code
;; into the reload cache and read from there.  The cache describes one path:
;; a constant built for one edge of a branch is not there on the other, a call
;; clobbers the half of it in x30, and a call argument made from a cached
;; constant still has to land in its register.  Each export puts large
;; constants in those places, in functions that call, which is when the cache
;; registers are in use: as arguments to both edges of a branch, across a call,
;; as call arguments, on the edges of a br_table and into a loop header.

(module
  (global $seed (mut i32) (i32.const 12345))
  (func $next (result i32)
    (global.set $seed (i32.add (i32.mul (global.get $seed) (i32.const 1103515245))
                               (i32.const 12345)))
    (i32.shr_u (global.get $seed) (i32.const 4)))
  (func $mix (param $a i32) (param $b i32) (result i32)
    (i32.xor (i32.mul (local.get $a) (i32.const 16777619)) (local.get $b)))

  ;; the same constant handed to the join from both arms
  (func (export "both_edges") (result i32)
    (local $i i32) (local $h i32) (local $x i32)
    (loop $l
      (local.set $x (call $next))
      (local.set $h (i32.add (local.get $h)
        (block $join (result i32)
          (drop (br_if $join (i32.const 0x811c9dc5) (i32.and (local.get $x) (i32.const 1))))
          (drop (call $next))
          (i32.const 0x811c9dc5))))
      (local.set $h (i32.xor (local.get $h)
        (if (result i32) (i32.and (local.get $x) (i32.const 2))
          (then (i32.const 0x12345678))
          (else (i32.add (i32.const 0x12345678) (local.get $x))))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 300))))
    (local.get $h))

  ;; a constant read before and after a call, and passed to calls
  (func (export "across_and_into_calls") (result i32)
    (local $i i32) (local $h i32)
    (loop $l
      (local.set $h (i32.add (local.get $h) (i32.const 0x9e3779b9)))
      (local.set $h (call $mix (local.get $h) (i32.const 0x9e3779b9)))
      (local.set $h (i32.xor (local.get $h) (i32.const 0x9e3779b9)))
      (local.set $h (call $mix (i32.const 0x7f4a7c15) (local.get $h)))
      (local.set $h (call $mix (i32.const 0x7f4a7c15) (i32.const 0x9e3779b9)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 300))))
    (local.get $h))

  ;; constants on the edges of a br_table, and one carried into a loop header
  (func (export "table_edges_and_header") (result i64)
    (local $i i32) (local $r i64) (local $k i64) (local $j i32)
    (loop $l
      (local.set $k
        (block $d (result i64)
          (drop (block $c (result i64)
            (drop (block $b (result i64)
              (drop (block $a (result i64)
                (br_table $a $b $c $d (i64.const 0x0123456789abcdef)
                          (i32.and (call $next) (i32.const 3)))))
              (br $d (i64.add (i64.const 0x0123456789abcdef) (i64.const 1)))))
            (br $d (i64.const 0x00ff00ff00ff00ff))))
          (drop (call $next))
          (i64.const 0x0123456789abcdef)))
      (local.set $j (i32.const 0))
      (loop $in
        (local.set $r (i64.add (i64.rotl (local.get $r) (i64.const 3))
                               (i64.xor (local.get $k) (i64.const 0x5555555555555555))))
        (br_if $in (i32.lt_u (local.tee $j (i32.add (local.get $j) (i32.const 1))) (i32.const 3))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 300))))
    (local.get $r))
  ;; one branch whose two edges both carry the constant, each to a target of
  ;; its own: the second edge's moves come after the first's in the buffer
  (func (export "one_branch_two_targets") (result i32)
    (local $i i32) (local $h i32) (local $c i32)
    (loop $l
      (local.set $c (i32.and (call $next) (i32.const 1)))
      (local.set $h (i32.add (local.get $h)
        (block $y (result i32)
          (i32.mul
            (block $x (result i32)
              (br_if $x (i32.const 0x811c9dc5) (local.get $c))
              (br $y (i32.const 0x811c9dc5)))
            (call $next)))))
      (local.set $h (i32.xor (local.get $h)
        (block $q (result i32)
          (i32.add
            (block $p (result i32)
              (br_if $p (i32.const 0x01000193) (i32.eqz (local.get $c)))
              (br $q (i32.add (i32.const 0x01000193) (local.get $i))))
            (call $next)))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 300))))
    (local.get $h))

  (func (export "one_branch_two_targets_64") (result i64)
    (local $i i32) (local $h i64) (local $c i32)
    (loop $l
      (local.set $c (i32.and (call $next) (i32.const 4)))
      (local.set $h (i64.add (local.get $h)
        (block $y (result i64)
          (i64.mul
            (block $x (result i64)
              (br_if $x (i64.const 0xcbf29ce484222325) (local.get $c))
              (br $y (i64.const 0xcbf29ce484222325)))
            (i64.extend_i32_u (call $next))))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 300))))
    (local.get $h))

  ;; lz77's shape: a hash starts at the FNV basis both into its loop, whose
  ;; header also takes the running hash back, and around it to the join,
  ;; which also takes the loop's result
  (func (export "hash_or_skip") (result i32)
    (local $i i32) (local $n i32) (local $j i32) (local $h i32) (local $t i32)
    (loop $l
      (local.set $n (i32.and (call $next) (i32.const 7)))
      (local.set $h (i32.const 0x811c9dc5))
      (if (local.get $n)
        (then
          (local.set $j (i32.const 0))
          (loop $hash
            (local.set $h (i32.mul (i32.xor (local.get $h) (i32.add (local.get $j) (local.get $i)))
                                   (i32.const 0x01000193)))
            (br_if $hash (i32.lt_u (local.tee $j (i32.add (local.get $j) (i32.const 1)))
                                   (local.get $n))))))
      (local.set $t (i32.add (local.get $t) (call $mix (local.get $h) (local.get $n))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 300))))
    (local.get $t))

  ;; the same with enough values live across the branch that the constant
  ;; is rematerialised where it is used rather than kept in a register
  (func (export "hash_or_skip_under_pressure") (result i32)
    (local $i i32) (local $n i32) (local $j i32) (local $h i32) (local $t i32)
    (local $v0 i32) (local $v1 i32) (local $v2 i32) (local $v3 i32) (local $v4 i32) (local $v5 i32) (local $v6 i32) (local $v7 i32) (local $v8 i32) (local $v9 i32) (local $v10 i32) (local $v11 i32) (local $v12 i32) (local $v13 i32) (local $v14 i32) (local $v15 i32) (local $v16 i32) (local $v17 i32) (local $v18 i32) (local $v19 i32) (local $v20 i32) (local $v21 i32) (local $v22 i32) (local $v23 i32) (local $v24 i32) (local $v25 i32)
    (loop $l
      (local.set $v0 (call $next))
      (local.set $v1 (call $next))
      (local.set $v2 (call $next))
      (local.set $v3 (call $next))
      (local.set $v4 (call $next))
      (local.set $v5 (call $next))
      (local.set $v6 (call $next))
      (local.set $v7 (call $next))
      (local.set $v8 (call $next))
      (local.set $v9 (call $next))
      (local.set $v10 (call $next))
      (local.set $v11 (call $next))
      (local.set $v12 (call $next))
      (local.set $v13 (call $next))
      (local.set $v14 (call $next))
      (local.set $v15 (call $next))
      (local.set $v16 (call $next))
      (local.set $v17 (call $next))
      (local.set $v18 (call $next))
      (local.set $v19 (call $next))
      (local.set $v20 (call $next))
      (local.set $v21 (call $next))
      (local.set $v22 (call $next))
      (local.set $v23 (call $next))
      (local.set $v24 (call $next))
      (local.set $v25 (call $next))
      (local.set $n (i32.and (call $next) (i32.const 7)))
      (local.set $h (i32.const 0x811c9dc5))
      (if (local.get $n)
        (then
          (local.set $j (i32.const 0))
          (loop $hash
            (local.set $h (i32.mul (i32.xor (local.get $h) (i32.add (local.get $j) (local.get $v3)))
                                   (i32.const 0x01000193)))
            (br_if $hash (i32.lt_u (local.tee $j (i32.add (local.get $j) (i32.const 1)))
                                   (local.get $n))))))
      (local.set $t (i32.add (local.get $t) (call $mix (local.get $h) (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (local.get $v0) (i32.mul (local.get $v1) (i32.const 3))) (i32.mul (local.get $v2) (i32.const 5))) (i32.mul (local.get $v3) (i32.const 7))) (i32.mul (local.get $v4) (i32.const 9))) (i32.mul (local.get $v5) (i32.const 11))) (i32.mul (local.get $v6) (i32.const 13))) (i32.mul (local.get $v7) (i32.const 15))) (i32.mul (local.get $v8) (i32.const 17))) (i32.mul (local.get $v9) (i32.const 19))) (i32.mul (local.get $v10) (i32.const 21))) (i32.mul (local.get $v11) (i32.const 23))) (i32.mul (local.get $v12) (i32.const 25))) (i32.mul (local.get $v13) (i32.const 27))) (i32.mul (local.get $v14) (i32.const 29))) (i32.mul (local.get $v15) (i32.const 31))) (i32.mul (local.get $v16) (i32.const 33))) (i32.mul (local.get $v17) (i32.const 35))) (i32.mul (local.get $v18) (i32.const 37))) (i32.mul (local.get $v19) (i32.const 39))) (i32.mul (local.get $v20) (i32.const 41))) (i32.mul (local.get $v21) (i32.const 43))) (i32.mul (local.get $v22) (i32.const 45))) (i32.mul (local.get $v23) (i32.const 47))) (i32.mul (local.get $v24) (i32.const 49))) (i32.mul (local.get $v25) (i32.const 51))))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 100))))
    (local.get $t))
)
