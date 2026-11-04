;; GC array reads a word at a time, a[i] .. a[i + 3], whose checks the
;; ranges of their indices remove (opt.c rp_inbounds) where the loop's test
;; says the four are in the array: i <u n & -4 with i a multiple of 4 (both
;; are, so i is at most the bound less 4: rp_aligned), i + 4 <=u n and
;; i + 3 <u n (a fact on i + c is one on i too, unsigned ones as signed
;; ones were: rp_add), i <s n - 3 (as before), and a[i] .. a[i + 7] under
;; i <u n & -8.  Each has a twin one step off -- a bound one too high, a
;; mask too narrow for the step, a step that leaves i a multiple of 2 alone
;; -- whose read past the end traps at every level.  The count is of the
;; checks taken off, before -O2's unrolling copies a loop's reads.
;; stats -O2: bce_ranges=26
;; run: mask4 16 => i32:120
;; run: mask4 7 => i32:6
;; run: mask4 3 => i32:0
;; run: mask4_far 8 => trap: out of bounds array access
;; run: mask4_narrow 6 => trap: out of bounds array access
;; run: mask4_step2 8 => trap: out of bounds array access
;; run: add4 16 => i32:120
;; run: add4 7 => i32:6
;; run: add4 3 => i32:0
;; run: add4_far 8 => trap: out of bounds array access
;; run: add3 16 => i32:120
;; run: add3 6 => i32:6
;; run: add3_short 7 => trap: out of bounds array access
;; run: sub3 16 => i32:120
;; run: sub3 2 => i32:0
;; run: mask8 24 => i32:276
;; run: mask8 9 => i32:28
;; run: mask8_far 16 => trap: out of bounds array access
(module
  (type $B (array (mut i8)))

  ;; n bytes, byte i holding i
  (func $mk (param $n i32) (result (ref $B))
    (local $a (ref $B)) (local $i i32)
    (local.set $a (array.new_default $B (local.get $n)))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $n)))
      (array.set $B (local.get $a) (local.get $i) (local.get $i))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (local.get $a))

  ;; a[i] + a[i + 1] + a[i + 2] + a[i + 3]
  (func $word (param $a (ref $B)) (param $i i32) (result i32)
    (i32.add
      (i32.add (array.get_u $B (local.get $a) (local.get $i))
               (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 1))))
      (i32.add (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 2)))
               (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 3))))))

  ;; i <u n & -4, i a multiple of 4
  (func (export "mask4") (param $n i32) (result i32)
    (local $a (ref $B)) (local $i i32) (local $e i32) (local $s i32)
    (local.set $a (call $mk (local.get $n)))
    (local.set $e (i32.and (array.len (local.get $a)) (i32.const -4)))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $e)))
      (local.set $s (i32.add (local.get $s)
        (i32.add
          (i32.add (array.get_u $B (local.get $a) (local.get $i))
                   (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 1))))
          (i32.add (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 2)))
                   (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 3)))))))
      (local.set $i (i32.add (local.get $i) (i32.const 4)))
      (br $l)))
    (local.get $s))

  ;; a[i + 4] too: one past the bound
  (func (export "mask4_far") (param $n i32) (result i32)
    (local $a (ref $B)) (local $i i32) (local $e i32) (local $s i32)
    (local.set $a (call $mk (local.get $n)))
    (local.set $e (i32.and (array.len (local.get $a)) (i32.const -4)))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $e)))
      (local.set $s (i32.add (local.get $s) (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 4)))))
      (local.set $i (i32.add (local.get $i) (i32.const 4)))
      (br $l)))
    (local.get $s))

  ;; n & -2: i + 3 past the end for an n of 4k + 2
  (func (export "mask4_narrow") (param $n i32) (result i32)
    (local $a (ref $B)) (local $i i32) (local $e i32) (local $s i32)
    (local.set $a (call $mk (local.get $n)))
    (local.set $e (i32.and (array.len (local.get $a)) (i32.const -2)))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $e)))
      (local.set $s (i32.add (local.get $s) (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 3)))))
      (local.set $i (i32.add (local.get $i) (i32.const 4)))
      (br $l)))
    (local.get $s))

  ;; a step of 2: i is e - 2 at the last
  (func (export "mask4_step2") (param $n i32) (result i32)
    (local $a (ref $B)) (local $i i32) (local $e i32) (local $s i32)
    (local.set $a (call $mk (local.get $n)))
    (local.set $e (i32.and (array.len (local.get $a)) (i32.const -4)))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $e)))
      (local.set $s (i32.add (local.get $s) (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 3)))))
      (local.set $i (i32.add (local.get $i) (i32.const 2)))
      (br $l)))
    (local.get $s))

  ;; i + 4 <=u n
  (func (export "add4") (param $n i32) (result i32)
    (local $a (ref $B)) (local $i i32) (local $s i32)
    (local.set $a (call $mk (local.get $n)))
    (block $d (loop $l
      (br_if $d (i32.gt_u (i32.add (local.get $i) (i32.const 4)) (array.len (local.get $a))))
      (local.set $s (i32.add (local.get $s)
        (i32.add
          (i32.add (array.get_u $B (local.get $a) (local.get $i))
                   (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 1))))
          (i32.add (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 2)))
                   (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 3)))))))
      (local.set $i (i32.add (local.get $i) (i32.const 4)))
      (br $l)))
    (local.get $s))

  (func (export "add4_far") (param $n i32) (result i32)
    (local $a (ref $B)) (local $i i32) (local $s i32)
    (local.set $a (call $mk (local.get $n)))
    (block $d (loop $l
      (br_if $d (i32.gt_u (i32.add (local.get $i) (i32.const 4)) (array.len (local.get $a))))
      (local.set $s (i32.add (local.get $s) (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 4)))))
      (local.set $i (i32.add (local.get $i) (i32.const 4)))
      (br $l)))
    (local.get $s))

  ;; i + 3 <u n, three at a time
  (func (export "add3") (param $n i32) (result i32)
    (local $a (ref $B)) (local $i i32) (local $s i32)
    (local.set $a (call $mk (local.get $n)))
    (block $d (loop $l
      (br_if $d (i32.ge_u (i32.add (local.get $i) (i32.const 3)) (array.len (local.get $a))))
      (local.set $s (i32.add (local.get $s) (call $word (local.get $a) (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 4)))
      (br $l)))
    (local.get $s))

  ;; i + 2 <u n: a[i + 3] one past
  (func (export "add3_short") (param $n i32) (result i32)
    (local $a (ref $B)) (local $i i32) (local $s i32)
    (local.set $a (call $mk (local.get $n)))
    (block $d (loop $l
      (br_if $d (i32.ge_u (i32.add (local.get $i) (i32.const 2)) (array.len (local.get $a))))
      (local.set $s (i32.add (local.get $s) (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 3)))))
      (local.set $i (i32.add (local.get $i) (i32.const 4)))
      (br $l)))
    (local.get $s))

  ;; i <s n - 3
  (func (export "sub3") (param $n i32) (result i32)
    (local $a (ref $B)) (local $i i32) (local $s i32)
    (local.set $a (call $mk (local.get $n)))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (i32.sub (array.len (local.get $a)) (i32.const 3))))
      (local.set $s (i32.add (local.get $s)
        (i32.add
          (i32.add (array.get_u $B (local.get $a) (local.get $i))
                   (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 1))))
          (i32.add (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 2)))
                   (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 3)))))))
      (local.set $i (i32.add (local.get $i) (i32.const 4)))
      (br $l)))
    (local.get $s))

  ;; eight at a time under n & -8
  (func (export "mask8") (param $n i32) (result i32)
    (local $a (ref $B)) (local $i i32) (local $e i32) (local $s i32)
    (local.set $a (call $mk (local.get $n)))
    (local.set $e (i32.and (array.len (local.get $a)) (i32.const -8)))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $e)))
      (local.set $s (i32.add (local.get $s)
        (i32.add
          (i32.add
            (i32.add (array.get_u $B (local.get $a) (local.get $i))
                     (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 1))))
            (i32.add (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 2)))
                     (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 3)))))
          (i32.add
            (i32.add (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 4)))
                     (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 5))))
            (i32.add (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 6)))
                     (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 7))))))))
      (local.set $i (i32.add (local.get $i) (i32.const 8)))
      (br $l)))
    (local.get $s))

  (func (export "mask8_far") (param $n i32) (result i32)
    (local $a (ref $B)) (local $i i32) (local $e i32) (local $s i32)
    (local.set $a (call $mk (local.get $n)))
    (local.set $e (i32.and (array.len (local.get $a)) (i32.const -8)))
    (block $d (loop $l
      (br_if $d (i32.ge_u (local.get $i) (local.get $e)))
      (local.set $s (i32.add (local.get $s) (array.get_u $B (local.get $a) (i32.add (local.get $i) (i32.const 8)))))
      (local.set $i (i32.add (local.get $i) (i32.const 8)))
      (br $l)))
    (local.get $s))
)
