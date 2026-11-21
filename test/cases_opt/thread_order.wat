;; The ordering after a jump threading, made from the one before it and
;; what the threading did (ir.c ir_order_threaded) where the graph is
;; reducible and no block threaded heads a loop, and anew where not:
;;
;; gone: every way into a merge's test is decided, so the merge is out of
;; reach, and with it the way of the test that no edge takes -- a block
;; that went back to the loop's header, which loses that predecessor, and
;; on to the block after, which loses one too.  latch: the test is a loop's
;; latch, and the fresh block of the way that goes round goes back to the
;; header as the latch did.  before: the merge is ahead of a loop whose
;; header its test's way falls into; with the merge gone the header's
;; dominator is the fresh block, which the edge back to the header -- from
;; a block the walk has not come to -- has no say in.  keeps: one way in
;; hands the test a value no one knows, so the merge stays, with one
;; predecessor fewer and a dominator further down.  three: the first of
;; three ways in is the one decided, and the other two stay listed in their
;; order.  twice: a switch with two cases into the merge, each edge sent to
;; a fresh block of its own.  through: the edges are into a block that only
;; hands its parameters on to the merge; all of them decided, that block is
;; out of reach and the merge loses it.  both: such a block's edges and an
;; edge of the merge's own, all decided in one threading -- the walk starts
;; at the block that hands on, which comes first.  machine: a loop around a switch on a state each case sets --
;; its header is the block threaded, so the order is made anew; the state
;; the function starts in is the caller's, so the switch stays, a second
;; way into the cycle of the cases, and the test after the loop is threaded
;; in a graph that is not reducible: anew again.  unrolled: the state it
;; starts in is 0, every edge into the switch is decided, the loop is gone
;; and the graph reducible, and the test after it is threaded with an
;; ordering made from the whole one the header's threading took.  again: two threadings in
;; one function, the second's ordering made from the first's, which took
;; blocks out of reach and so out of the order.  The arms store to a
;; global, which keeps if-conversion from making selects of them.  Answers
;; worked out by hand.
;;
;; stats -O1: threaded_known=5 threaded_path=1 thread_orders=12 thread_orders_whole=3
;; stats -O2: threaded_known=5 threaded_path=1 thread_orders=12 thread_orders_whole=3
;;
;; run: gone 3 1 => i32:10
;; run: gone 3 0 => i32:15
;; run: gone 0 1 => i32:8
;; run: latch 4 => i32:54
;; run: latch 1 => i32:51
;; run: latch 0 => i32:51
;; run: before 1 3 => i32:1004
;; run: before 0 3 => i32:8
;; run: before 1 0 => i32:1001
;; run: keeps 1 9 => i32:101
;; run: keeps 0 5 => i32:2
;; run: keeps 0 9 => i32:102
;; run: three 0 5 => i32:101
;; run: three 1 9 => i32:102
;; run: three 1 5 => i32:2
;; run: three 2 8 => i32:103
;; run: three 2 9 => i32:3
;; run: twice 0 0 => i32:10
;; run: twice 1 0 => i32:10
;; run: twice 2 0 => i32:1
;; run: twice 5 1 => i32:23
;; run: through 0 0 => i32:11
;; run: through 0 1 => i32:22
;; run: through 1 0 => i32:13
;; run: through 2 1 => i32:23
;; run: both 0 0 => i32:11
;; run: both 0 1 => i32:22
;; run: both 7 1 => i32:22
;; run: both 200 0 => i32:23
;; run: machine 0 3 1 => i32:113
;; run: machine 1 3 1 => i32:112
;; run: machine 2 3 0 => i32:9
;; run: machine 1 0 0 => i32:11
;; run: machine 0 2 0 => i32:15
;; run: machine 7 3 1 => i32:104
;; run: unrolled 3 1 => i32:1109
;; run: unrolled 3 0 => i32:29
;; run: unrolled 0 1 => i32:1106
;; run: again 1 1 => i32:111
;; run: again 0 0 => i32:22
;; run: again 9 0 => i32:121
(module
  (global $g (mut i32) (i32.const 0))

  (func (export "gone") (param $n i32) (param $sel i32) (result i32)
    (local $i i32) (local $acc i32) (local $k i32)
    (loop $L
      (if (local.get $sel)
        (then (global.set $g (i32.const 7)) (local.set $k (i32.const 1)))
        (else (global.set $g (i32.const 9)) (local.set $k (i32.const 2))))
      (if (i32.eq (local.get $k) (i32.const 3))
        (then
          (local.set $acc (i32.add (local.get $acc) (i32.const 100)))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
      (local.set $acc (i32.add (local.get $acc) (local.get $k)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $L (i32.lt_u (local.get $i) (local.get $n))))
    (i32.add (local.get $acc) (global.get $g)))

  (func (export "latch") (param $n i32) (result i32)
    (local $i i32) (local $k i32)
    (loop $L
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (if (i32.lt_u (local.get $i) (local.get $n))
        (then (global.set $g (local.get $i)) (local.set $k (i32.const 1)))
        (else (global.set $g (i32.const 50)) (local.set $k (i32.const 0))))
      (br_if $L (local.get $k)))
    (i32.add (local.get $i) (global.get $g)))

  (func (export "before") (param $a i32) (param $n i32) (result i32)
    (local $k i32) (local $i i32) (local $s i32)
    (block $out
      (if (local.get $a)
        (then (global.set $g (i32.const 1)) (local.set $k (i32.const 5)) (local.set $s (i32.const 1000)))
        (else (global.set $g (i32.const 2)) (local.set $k (i32.const 6)) (local.set $s (i32.const 6))))
      (br_if $out (i32.eq (local.get $k) (i32.const 6)))
      (loop $L
        (local.set $s (i32.add (local.get $s) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $L (i32.lt_u (local.get $i) (local.get $n)))))
    (i32.add (local.get $s) (global.get $g)))

  (func (export "keeps") (param $a i32) (param $x i32) (result i32)
    (local $k i32)
    (if (local.get $a)
      (then (global.set $g (i32.const 1)) (local.set $k (i32.const 9)))
      (else (global.set $g (i32.const 2)) (local.set $k (local.get $x))))
    (if (i32.eq (local.get $k) (i32.const 9))
      (then (global.set $g (i32.add (global.get $g) (i32.const 100)))))
    (global.get $g))

  (func (export "three") (param $a i32) (param $x i32) (result i32)
    (local $k i32)
    (block $M
      (if (i32.eq (local.get $a) (i32.const 0))
        (then (global.set $g (i32.const 1)) (local.set $k (i32.const 9)) (br $M)))
      (if (i32.eq (local.get $a) (i32.const 1))
        (then (global.set $g (i32.const 2)) (local.set $k (local.get $x)) (br $M)))
      (global.set $g (i32.const 3))
      (local.set $k (i32.add (local.get $x) (i32.const 1))))
    (if (i32.eq (local.get $k) (i32.const 9))
      (then (global.set $g (i32.add (global.get $g) (i32.const 100)))))
    (global.get $g))

  (func (export "twice") (param $a i32) (param $b i32) (result i32)
    (local $k i32)
    (block $D (result i32)
      (local.set $k
        (block $B (result i32)
          (if (local.get $b)
            (then (global.set $g (i32.const 3)) (br $B (i32.const 7))))
          (br_table $B $B $D (i32.const 1) (local.get $a))))
      (if (i32.eq (local.get $k) (i32.const 1))
        (then (global.set $g (i32.const 10)))
        (else (global.set $g (i32.add (global.get $g) (i32.const 20)))))
      (global.get $g)))

  (func (export "through") (param $a i32) (param $b i32) (result i32)
    (local $k i32)
    (block $M
      (if (local.get $a)
        (then
          (global.set $g (i32.const 3))
          (local.set $k (local.get $a))
          (br $M)))
      (if (local.get $b)
        (then (global.set $g (i32.const 2)) (local.set $k (i32.const 2)))
        (else (global.set $g (i32.const 1)) (local.set $k (i32.const 1)))))
    (if (i32.eq (local.get $k) (i32.const 1))
      (then (global.set $g (i32.add (global.get $g) (i32.const 10))))
      (else (global.set $g (i32.add (global.get $g) (i32.const 20)))))
    (global.get $g))

  (func (export "both") (param $a i32) (param $b i32) (result i32)
    (local $k i32)
    (block $M
      (if (i32.gt_u (local.get $a) (i32.const 100))
        (then (global.set $g (i32.const 3)) (local.set $k (local.get $a)) (br $M)))
      (if (local.get $b)
        (then (global.set $g (i32.const 2)) (local.set $k (i32.const 2)))
        (else (global.set $g (i32.const 1)) (local.set $k (i32.const 1)))))
    (if (i32.eq (local.get $k) (i32.const 1))
      (then (global.set $g (i32.add (global.get $g) (i32.const 10))))
      (else (global.set $g (i32.add (global.get $g) (i32.const 20)))))
    (global.get $g))

  (func (export "machine") (param $st i32) (param $n i32) (param $a i32) (result i32)
    (local $acc i32) (local $k i32)
    (loop $L
      (block $s2
        (block $s1
          (block $s0
            (br_table $s0 $s1 $s2 (local.get $st)))
          (local.set $acc (i32.add (local.get $acc) (i32.const 1)))
          (local.set $st (i32.const 1))
          (br $L))
        (local.set $acc (i32.add (local.get $acc) (i32.const 2)))
        (local.set $n (i32.sub (local.get $n) (i32.const 1)))
        (if (i32.gt_s (local.get $n) (i32.const 0))
          (then (global.set $g (local.get $n)) (local.set $st (i32.const 0)) (br $L)))
        (local.set $st (i32.const 2))
        (br $L)))
    (if (local.get $a)
      (then (global.set $g (i32.const 100)) (local.set $k (i32.const 4)))
      (else (global.set $g (i32.const 20)) (local.set $k (i32.const 8))))
    (if (i32.eq (local.get $k) (i32.const 4))
      (then (local.set $acc (i32.add (local.get $acc) (global.get $g))))
      (else (local.set $acc (i32.add (local.get $acc) (i32.const 1)))))
    (i32.add (local.get $acc) (local.get $k)))

  (func (export "unrolled") (param $n i32) (param $a i32) (result i32)
    (local $st i32) (local $acc i32) (local $k i32)
    (loop $L
      (block $s2
        (block $s1
          (block $s0
            (br_table $s0 $s1 $s2 (local.get $st)))
          (local.set $acc (i32.add (local.get $acc) (i32.const 1)))
          (global.set $g (i32.const 5))
          (local.set $st (i32.const 1))
          (br $L))
        (local.set $acc (i32.add (local.get $acc) (local.get $n)))
        (local.set $st (i32.const 2))
        (br $L)))
    (if (local.get $a)
      (then (global.set $g (i32.add (global.get $g) (i32.const 100))) (local.set $k (i32.const 4)))
      (else (global.set $g (i32.add (global.get $g) (i32.const 20))) (local.set $k (i32.const 8))))
    (if (i32.eq (local.get $k) (i32.const 4))
      (then (global.set $g (i32.add (global.get $g) (i32.const 1000)))))
    (i32.add (local.get $acc) (global.get $g)))

  (func (export "again") (param $a i32) (param $x i32) (result i32)
    (local $k i32) (local $j i32)
    (if (local.get $a)
      (then (global.set $g (i32.const 1)) (local.set $k (i32.const 1)))
      (else (global.set $g (i32.const 2)) (local.set $k (i32.const 2))))
    (if (i32.eq (local.get $k) (i32.const 3))
      (then (global.set $g (i32.const 77))))
    (if (local.get $x)
      (then (global.set $g (i32.add (global.get $g) (i32.const 10))) (local.set $j (i32.const 9)))
      (else (global.set $g (i32.add (global.get $g) (i32.const 20))) (local.set $j (local.get $a))))
    (if (i32.eq (local.get $j) (i32.const 9))
      (then (global.set $g (i32.add (global.get $g) (i32.const 100)))))
    (global.get $g)))
