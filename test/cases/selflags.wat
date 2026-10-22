;; Several selects on one compare.  The compare is materialised to 0/1 for
;; whoever else reads it, and a select reads the flags it set instead of
;; testing that 0/1 again -- for as long as nothing in between has written the
;; flags.  Each export puts something between two selects on the same compare:
;; nothing, other selects, another compare, a call, a checked division, a
;; loop, a load that may be bounds-checked, and a branch on the compare too;
;; and float compares, whose conditions are not the integer ones.  A block
;; entered only from the block before it starts with that block's flags, so
;; the last exports compare the same operands again after a branch on them:
;; other predicates, the operands swapped, 64-bit, and a successor reached by
;; a jump from elsewhere as well.
;;
;; The inputs come from a mutable global so nothing folds them away first.

(module
  (global $seed (mut i32) (i32.const 0x2545f491))
  (memory 1)

  (func $next (result i32)
    (global.set $seed (i32.add (i32.mul (global.get $seed) (i32.const 1103515245))
                               (i32.const 12345)))
    (i32.shr_s (global.get $seed) (i32.const 3)))

  (func $id (param i32) (result i32) (local.get 0))

  ;; argmin's shape: index and minimum both chosen by one compare
  (func (export "argmin_pair") (result i32)
    (local $i i32) (local $v i32) (local $best i32) (local $at i32) (local $c i32)
    (local.set $best (i32.const 0x7fffffff))
    (loop $l
      (local.set $v (call $next))
      (local.set $c (i32.lt_s (local.get $v) (local.get $best)))
      (local.set $at (select (local.get $i) (local.get $at) (local.get $c)))
      (local.set $best (select (local.get $v) (local.get $best) (local.get $c)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 300))))
    (i32.add (i32.mul (local.get $at) (i32.const 65599)) (local.get $best)))

  ;; a different compare between the two selects on the first
  (func (export "other_compare_between") (result i32)
    (local $a i32) (local $b i32) (local $c i32) (local $d i32) (local $r i32) (local $i i32)
    (loop $l
      (local.set $a (call $next)) (local.set $b (call $next))
      (local.set $c (i32.gt_u (local.get $a) (local.get $b)))
      (local.set $r (i32.add (local.get $r) (select (local.get $a) (local.get $b) (local.get $c))))
      (local.set $d (i32.eq (i32.and (local.get $a) (i32.const 3)) (i32.const 1)))
      (local.set $r (i32.xor (local.get $r) (select (i32.const 77) (i32.const 5) (local.get $d))))
      (local.set $r (i32.sub (local.get $r) (select (local.get $b) (local.get $a) (local.get $c))))
      (local.set $r (i32.add (local.get $r) (local.get $c)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 200))))
    (local.get $r))

  ;; a call, which clobbers the flags, between the selects
  (func (export "call_between") (result i32)
    (local $a i32) (local $b i32) (local $c i32) (local $r i32) (local $i i32)
    (loop $l
      (local.set $a (call $next)) (local.set $b (call $next))
      (local.set $c (i32.le_s (local.get $a) (local.get $b)))
      (local.set $r (i32.add (local.get $r) (select (local.get $a) (i32.const 9) (local.get $c))))
      (local.set $r (i32.add (local.get $r) (call $id (local.get $b))))
      (local.set $r (i32.xor (local.get $r) (select (i32.const 1000) (local.get $b) (local.get $c))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 200))))
    (local.get $r))

  ;; a signed division, whose overflow and zero checks compare, between them
  (func (export "division_between") (result i32)
    (local $a i32) (local $b i32) (local $c i32) (local $r i32) (local $i i32)
    (loop $l
      (local.set $a (call $next)) (local.set $b (i32.or (call $next) (i32.const 1)))
      (local.set $c (i32.ge_u (local.get $a) (local.get $b)))
      (local.set $r (i32.add (local.get $r) (select (local.get $a) (local.get $b) (local.get $c))))
      (local.set $r (i32.add (local.get $r) (i32.div_s (local.get $a) (local.get $b))))
      (local.set $r (i32.add (local.get $r) (i32.rem_s (local.get $b) (i32.or (local.get $a) (i32.const 1)))))
      (local.set $r (i32.xor (local.get $r) (select (local.get $b) (i32.const -3) (local.get $c))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 200))))
    (local.get $r))

  ;; a loop between the selects, and a load whose address comes from data
  (func (export "loop_and_load_between") (result i32)
    (local $a i32) (local $b i32) (local $c i32) (local $r i32) (local $i i32) (local $j i32)
    (loop $l
      (local.set $a (call $next)) (local.set $b (call $next))
      (i32.store (i32.and (local.get $a) (i32.const 0xfffc)) (local.get $b))
      (local.set $c (i32.lt_u (local.get $a) (local.get $b)))
      (local.set $r (i32.add (local.get $r) (select (local.get $a) (local.get $b) (local.get $c))))
      (local.set $j (i32.const 0))
      (loop $in
        (local.set $r (i32.add (local.get $r) (i32.mul (local.get $j) (local.get $j))))
        (br_if $in (i32.lt_u (local.tee $j (i32.add (local.get $j) (i32.const 1)))
                             (i32.and (local.get $a) (i32.const 7)))))
      (local.set $r (i32.xor (local.get $r)
                             (i32.load (i32.and (local.get $a) (i32.const 0xfffc)))))
      (local.set $r (i32.sub (local.get $r) (select (local.get $b) (local.get $a) (local.get $c))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 200))))
    (local.get $r))

  ;; the compare also decides a branch, with selects on both of its arms
  (func (export "branch_and_selects") (result i32)
    (local $a i32) (local $b i32) (local $c i32) (local $r i32) (local $i i32)
    (loop $l
      (local.set $a (call $next)) (local.set $b (call $next))
      (local.set $c (i32.ne (i32.and (local.get $a) (i32.const 0x30)) (i32.const 0)))
      (local.set $r (i32.add (local.get $r) (select (local.get $a) (local.get $b) (local.get $c))))
      (if (local.get $c)
        (then (local.set $r (i32.xor (local.get $r) (select (i32.const 11) (i32.const 22) (local.get $c)))))
        (else (local.set $r (i32.add (local.get $r) (select (local.get $b) (i32.const 33) (local.get $c))))))
      (local.set $r (i32.rotl (local.get $r) (select (i32.const 3) (i32.const 7) (local.get $c))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 200))))
    (local.get $r))

  ;; 64-bit selects, and float compares (unordered for NaN) selecting floats and ints
  (func (export "float_compares") (result i64)
    (local $x f64) (local $y f64) (local $c i32) (local $d i32) (local $r i64) (local $s f64) (local $i i32)
    (loop $l
      (local.set $x (f64.convert_i32_s (call $next)))
      (local.set $y (f64.convert_i32_s (call $next)))
      (if (i32.eqz (i32.and (local.get $i) (i32.const 7)))
        (then (local.set $y (f64.div (f64.const 0) (f64.const 0)))))
      (local.set $c (f64.lt (local.get $x) (local.get $y)))
      (local.set $d (f64.ne (local.get $x) (local.get $y)))
      (local.set $s (f64.add (local.get $s) (select (local.get $x) (f64.const 0.5) (local.get $c))))
      (local.set $r (i64.add (local.get $r) (select (i64.const 3) (i64.const 0x100000000) (local.get $c))))
      (local.set $r (i64.add (local.get $r) (select (i64.const 5) (i64.const 7) (local.get $d))))
      (local.set $r (i64.xor (local.get $r) (select (i64.extend_i32_s (local.get $i)) (i64.const -1) (local.get $c))))
      (local.set $r (i64.add (local.get $r) (i64.extend_i32_u (i32.add (local.get $c) (local.get $d)))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 200))))
    (i64.add (local.get $r) (i64.trunc_sat_f64_s (local.get $s))))
  ;; a tree search: eq on the key, then ltu on the same key picks the child
  (func (export "tree_search") (result i32)
    (local $i i32) (local $n i32) (local $k i32) (local $node i32) (local $found i32) (local $steps i32)
    ;; 63 nodes of 12 bytes at 1024: key, left, right, as a complete BST of
    ;; keys 2, 4, ..., 126 laid out in heap order
    (local.set $i (i32.const 1))
    (loop $build
      (i32.store (i32.add (i32.const 1024) (i32.mul (local.get $i) (i32.const 12)))
                 (call $heapkey (local.get $i)))
      (i32.store offset=4 (i32.add (i32.const 1024) (i32.mul (local.get $i) (i32.const 12)))
                 (select (i32.add (i32.const 1024) (i32.mul (i32.shl (local.get $i) (i32.const 1)) (i32.const 12)))
                         (i32.const 0) (i32.lt_u (local.get $i) (i32.const 32))))
      (i32.store offset=8 (i32.add (i32.const 1024) (i32.mul (local.get $i) (i32.const 12)))
                 (select (i32.add (i32.const 1024) (i32.mul (i32.add (i32.shl (local.get $i) (i32.const 1)) (i32.const 1)) (i32.const 12)))
                         (i32.const 0) (i32.lt_u (local.get $i) (i32.const 32))))
      (br_if $build (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (i32.const 64))))
    (local.set $n (i32.const 0))
    (loop $q
      (local.set $k (i32.and (call $next) (i32.const 127)))
      (local.set $node (i32.const 1036))
      (block $done
        (loop $walk
          (br_if $done (i32.eqz (local.get $node)))
          (local.set $steps (i32.add (local.get $steps) (i32.const 1)))
          (if (i32.eq (local.get $k) (i32.load (local.get $node)))
            (then (local.set $found (i32.add (local.get $found) (local.get $k))) (br $done)))
          (local.set $node (i32.load (i32.add (local.get $node)
            (select (i32.const 4) (i32.const 8) (i32.lt_u (local.get $k) (i32.load (local.get $node)))))))
          (br $walk)))
      (br_if $q (i32.lt_u (local.tee $n (i32.add (local.get $n) (i32.const 1))) (i32.const 500))))
    (i32.add (i32.mul (local.get $found) (i32.const 1000)) (local.get $steps)))

  ;; the in-order key of heap slot i in a complete tree of 63 nodes
  (func $heapkey (param $i i32) (result i32)
    (local $d i32) (local $lvl i32)
    (local.set $lvl (i32.sub (i32.const 31) (i32.clz (local.get $i))))
    (local.set $d (i32.sub (local.get $i) (i32.shl (i32.const 1) (local.get $lvl))))
    (i32.shl (i32.add (i32.mul (i32.add (i32.shl (local.get $d) (i32.const 1)) (i32.const 1))
                               (i32.shl (i32.const 1) (i32.sub (i32.const 5) (local.get $lvl))))
                      (i32.const 0))
             (i32.const 1)))

  ;; after a branch on a compare, the same operands compared again
  (func (export "compare_after_branch") (result i64)
    (local $a i32) (local $b i32) (local $x i64) (local $y i64) (local $r i64) (local $i i32)
    (loop $l
      (local.set $a (i32.and (call $next) (i32.const 15)))
      (local.set $b (i32.and (call $next) (i32.const 15)))
      (local.set $x (i64.extend_i32_s (call $next)))
      (local.set $y (i64.extend_i32_s (call $next)))
      (if (i32.ne (local.get $a) (local.get $b))
        (then
          (local.set $r (i64.add (local.get $r) (i64.extend_i32_u (i32.lt_s (local.get $a) (local.get $b)))))
          (local.set $r (i64.add (local.get $r) (i64.extend_i32_u (i32.gt_u (local.get $b) (local.get $a)))))
          (local.set $r (i64.add (local.get $r) (select (i64.const 4) (i64.const 64) (i32.ge_s (local.get $a) (local.get $b))))))
        (else
          (local.set $r (i64.add (local.get $r) (i64.extend_i32_u (i32.le_u (local.get $a) (local.get $b)))))))
      (if (i64.lt_s (local.get $x) (local.get $y))
        (then (local.set $r (i64.xor (local.get $r) (i64.const 0x55))))
        (else (local.set $r (i64.add (local.get $r) (select (i64.const 256) (i64.const 512)
                                                            (i64.gt_u (local.get $x) (local.get $y)))))))
      ;; reached from both arms above: the flags here are not either arm's
      (local.set $r (i64.add (local.get $r) (select (i64.const 1024) (i64.const 2048)
                                                    (i64.le_s (local.get $x) (local.get $y)))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 300))))
    (local.get $r))
)
