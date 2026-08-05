;; Edges whose parameters travel in both register files at once.
;;
;; The moves an edge needs are sequenced by a parallel copy: emit the ones
;; nothing else still reads, and when every one that is left is part of a
;; cycle, spill one source to a scratch.  A location is a frame slot or a
;; register *number*, and the number alone does not say which file it is in --
;; x1 and v1 are both 1.  Comparing the numbers alone makes a general-register
;; move and a vector one look like they depend on each other, and three such
;; moves can each be "blocked" by the others with no cycle to break at all:
;; the sequencer then goes round for ever and warmc never terminates.  Where
;; it does terminate, the cycle-breaking rewrite can point a vector move at
;; the general-purpose scratch, which is a wrong answer rather than a slow one.
;;
;; So every function here puts values of both files live across one join, and
;; most of them permute both files over the same edge so the sequencer has to
;; break a cycle in each.  Found by random module generation, which hit it as
;; a compile that did not finish.

(module
  (memory 1)
  (data (i32.const 0)
    "\01\00\00\00\02\00\00\00\03\00\00\00\04\00\00\00\05\00\00\00\06\00\00\00")

  ;; A loop that swaps a pair in each file every iteration.  The back edge
  ;; carries a two-cycle in the general registers and another in the vector
  ;; registers, and it has to sequence both without confusing x_n for v_n.
  (func (export "swap_both") (result i32)
    (local $i i32) (local $t i32) (local $u f32)
    (local $x i32) (local $y i32)
    (local $a f32) (local $b f32)
    (local.set $x (i32.load (i32.const 0)))
    (local.set $y (i32.load (i32.const 4)))
    (local.set $a (f32.convert_i32_s (i32.load (i32.const 8))))
    (local.set $b (f32.convert_i32_s (i32.load (i32.const 12))))
    (block $done (loop $l
      (local.set $t (local.get $x))
      (local.set $x (local.get $y))
      (local.set $y (local.get $t))
      (local.set $u (local.get $a))
      (local.set $a (local.get $b))
      (local.set $b (local.get $u))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 5)))))
    (i32.add (i32.add (local.get $x) (i32.mul (local.get $y) (i32.const 16)))
             (i32.mul (i32.add (i32.trunc_f32_s (local.get $a))
                               (i32.mul (i32.trunc_f32_s (local.get $b))
                                        (i32.const 16)))
                      (i32.const 256))))

  ;; The same with three of each, so the cycles are longer than a swap and the
  ;; scratch has to survive more than one move.
  (func (export "rot_both") (result i32)
    (local $i i32) (local $t i32) (local $u f64)
    (local $x i32) (local $y i32) (local $z i32)
    (local $a f64) (local $b f64) (local $c f64)
    (local.set $x (i32.load (i32.const 0)))
    (local.set $y (i32.load (i32.const 4)))
    (local.set $z (i32.load (i32.const 8)))
    (local.set $a (f64.convert_i32_s (i32.load (i32.const 12))))
    (local.set $b (f64.convert_i32_s (i32.load (i32.const 16))))
    (local.set $c (f64.convert_i32_s (i32.load (i32.const 20))))
    (block $done (loop $l
      (local.set $t (local.get $x))
      (local.set $x (local.get $y))
      (local.set $y (local.get $z))
      (local.set $z (local.get $t))
      (local.set $u (local.get $a))
      (local.set $a (local.get $b))
      (local.set $b (local.get $c))
      (local.set $c (local.get $u))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 7)))))
    (i32.add
      (i32.add (local.get $x) (i32.mul (local.get $y) (i32.const 8)))
      (i32.add (i32.mul (local.get $z) (i32.const 64))
               (i32.mul (i32.add (i32.trunc_f64_s (local.get $a))
                                 (i32.mul (i32.trunc_f64_s (local.get $b))
                                          (i32.const 8)))
                        (i32.const 512)))))

  ;; A v128 travels in the vector file too, so the same collision is available
  ;; between a general register and a vector one holding sixteen bytes.
  (func (export "swap_vec") (result i32)
    (local $i i32) (local $t i32) (local $u v128)
    (local $x i32) (local $y i32)
    (local $p v128) (local $q v128)
    (local.set $x (i32.load (i32.const 0)))
    (local.set $y (i32.load (i32.const 4)))
    (local.set $p (v128.load (i32.const 0)))
    (local.set $q (v128.load (i32.const 8)))
    (block $done (loop $l
      (local.set $t (local.get $x))
      (local.set $x (local.get $y))
      (local.set $y (local.get $t))
      (local.set $u (local.get $p))
      (local.set $p (local.get $q))
      (local.set $q (local.get $u))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 3)))))
    (i32.add (i32.add (local.get $x) (i32.mul (local.get $y) (i32.const 16)))
             (i32.mul (i32.add (i32x4.extract_lane 0 (local.get $p))
                               (i32x4.extract_lane 1 (local.get $q)))
                      (i32.const 256))))

  ;; A join reached from two arms, each of which leaves a different value in
  ;; every local -- so the edge into the merge carries a move for each file
  ;; without any of them being a cycle.  This is the shape that hung: three
  ;; moves, two of them in different files with the same register number.
  (func (export "merge_mixed") (param $sel i32) (result i32)
    (local $x i32) (local $y i32) (local $z i32)
    (local $a f32) (local $b f32) (local $c f32)
    (local.set $x (i32.load (i32.const 0)))
    (local.set $y (i32.load (i32.const 4)))
    (local.set $z (i32.load (i32.const 8)))
    (local.set $a (f32.convert_i32_s (i32.load (i32.const 12))))
    (local.set $b (f32.convert_i32_s (i32.load (i32.const 16))))
    (local.set $c (f32.convert_i32_s (i32.load (i32.const 20))))
    (if (local.get $sel)
      (then
        (local.set $x (local.get $z))
        (local.set $y (local.get $x))
        (local.set $a (local.get $c))
        (local.set $b (local.get $a)))
      (else
        (local.set $z (local.get $y))
        (local.set $x (local.get $y))
        (local.set $c (local.get $b))
        (local.set $a (local.get $b))))
    (i32.add
      (i32.add (local.get $x) (i32.mul (local.get $y) (i32.const 8)))
      (i32.add (i32.mul (local.get $z) (i32.const 64))
               (i32.mul (i32.add (i32.trunc_f32_s (local.get $a))
                                 (i32.add (i32.mul (i32.trunc_f32_s (local.get $b))
                                                   (i32.const 8))
                                          (i32.mul (i32.trunc_f32_s (local.get $c))
                                                   (i32.const 64))))
                        (i32.const 512)))))
  (func (export "merge_mixed_t") (result i32) (call 3 (i32.const 1)))
  ;; The other arm of the same select, which nothing ran: the wrapper above
  ;; covers only the arm it passes.
  (func (export "merge_mixed_f") (result i32) (call 3 (i32.const 0)))

  ;; Nested selects and ifs over a float, with integers live across them --
  ;; the shape the random generator produced, kept because it reaches the join
  ;; through `select` rather than through a branch.
  (func (export "select_join") (result i32)
    (local $x i32) (local $y i32)
    (local $a f32) (local $b f32) (local $c f32)
    (local.set $x (i32.load (i32.const 0)))
    (local.set $y (i32.load (i32.const 4)))
    (local.set $a (f32.convert_i32_s (i32.load (i32.const 8))))
    (local.set $b (f32.convert_i32_s (i32.load (i32.const 12))))
    (i32.add
      (i32.reinterpret_f32
        (select
          (if (result f32) (local.get $x)
            (then (local.get $a))
            (else (select (if (result f32) (local.get $y)
                            (then (local.get $b))
                            (else (local.get $c)))
                          (if (result f32) (local.get $x)
                            (then (local.get $c))
                            (else (local.get $a)))
                          (local.get $y))))
          (f32.const 1)
          (i32.and (local.get $x) (local.get $y))))
      (i32.add (local.get $x) (i32.mul (local.get $y) (i32.const 65536)))))
)
