;; Reads of one array at base + c whose greatest and least are moved first,
;; for the others' checks to go (opt.c gc_smear): a[i] .. a[i + 3] of an i
;; of any sign keep two checks of four, and the least's goes too where the
;; base cannot be negative (a loop counting up from 0).  A store between
;; two reads ends the run -- the store must happen before the later read
;; traps -- and so does a null check of another array (a null's trap is
;; another).  Each read out of bounds traps as it did: at the greatest
;; index, or the least.
;; stats -O2: bce_smeared=6
;; run: run4 8 0 => i32:6
;; run: run4 8 4 => i32:22
;; run: run4 8 5 => trap: out of bounds array access
;; run: run4 8 -1 => trap: out of bounds array access
;; run: run4 8 -3 => trap: out of bounds array access
;; run: pairs 9 8 => i32:68
;; run: pairs 1 0 => i32:0
;; run: pairs 8 9 => trap: out of bounds array access
;; run: stored 8 4 => i32:22
;; run: stored 8 5 => trap: out of bounds array access
;; run: mixed 8 4 => i32:18
;; run: mixed 8 7 => trap: out of bounds array access
(module
  (type $B (array (mut i8)))

  (func $mk (param $n i32) (result (ref $B))
    (local $a (ref $B)) (local $i i32)
    (local.set $a (array.new_default $B (local.get $n)))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (array.set $B (local.get $a) (local.get $i) (local.get $i))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $a))

  ;; a[i] + a[i + 1] + a[i + 2] + a[i + 3], i of any sign: the checks of
  ;; i + 3 and i
  (func (export "run4") (param $n i32) (param $i i32) (result i32)
    (local $a (ref $B))
    (local.set $a (call $mk (local.get $n)))
    (i32.add
      (i32.add (array.get_u $B (local.get $a) (local.get $i))
               (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 1))))
      (i32.add (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 2)))
               (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 3))))))

  ;; a[i] * a[i + 1] for i = 0, 2, .. while i < m, m no length the
  ;; prover knows: i + 1's check alone, i counting up from 0
  (func (export "pairs") (param $n i32) (param $m i32) (result i32)
    (local $a (ref $B)) (local $i i32) (local $s i32)
    (local.set $a (call $mk (local.get $n)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $m)))
      (local.set $s (i32.add (local.get $s)
        (i32.mul (array.get_u $B (local.get $a) (local.get $i))
                 (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 1))))))
      (local.set $i (i32.add (local.get $i) (i32.const 2)))
      (br $l)))
    (local.get $s))

  ;; a store between: two runs, each read its own check
  (func (export "stored") (param $n i32) (param $i i32) (result i32)
    (local $a (ref $B)) (local $x i32)
    (local.set $a (call $mk (local.get $n)))
    (local.set $x (i32.add (array.get_u $B (local.get $a) (local.get $i))
                           (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 1)))))
    (array.set $B (local.get $a) (i32.const 0) (local.get $x))
    (i32.add (local.get $x)
      (i32.add (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 2)))
               (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 3))))))

  ;; reads of two arrays among each other: each array's run its own
  (func (export "mixed") (param $n i32) (param $i i32) (result i32)
    (local $a (ref $B)) (local $b (ref $B))
    (local.set $a (call $mk (local.get $n)))
    (local.set $b (call $mk (local.get $n)))
    (i32.add
      (i32.add (array.get_u $B (local.get $a) (local.get $i))
               (array.get_u $B (local.get $b) (i32.add (local.get $i) (i32.const 1))))
      (i32.add (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 1)))
               (array.get_u $B (local.get $b) (local.get $i)))))
)
