;; A loop's two ways out that meet: a B-tree node's scan leaves when its
;; keys run out and when the key is not above the one it stopped at, and
;; both go on down to a child.  The layout puts the block that goes down
;; after the second way out, which falls into it, and the found key's block
;; out of line (ir.c dfs_defer_exit): one taken branch a level, where the
;; block came after the scan's other way and the found block in line took
;; two.  rsbench/btreemap's search.  near: the same scan whose second way
;; out leaves another loop -- a second scan's -- which keeps its layout.
;; near's way into its second scan, unrolled, has moves both ways, and the
;; way into the copy, as likely, falls into it, the other way's moves in a
;; stub out of line (isel_ops.inc; stubs_out), where the way in branched
;; over the other's moves: four unconditional branches, not six.
;; twoexit: a scan whose second way out is a test in the loop, and twopred:
;; one whose second way out is a block two tests in the loop reach -- no
;; exit taken from either (the count of those taken, exit_defers, is
;; find's, near's none, and the inlined copies').
;;
;; late: find \smadd\s=1
;; late: near \smadd\s=0
;; shape: near \sb\s=4
;; stats -O2 a64: stubs_out=2
;; stats -O2: exit_defers=3
;;
;; run: find -1 => i32:-1
;; run: find 0 => i32:-1
;; run: find 1 => i32:0
;; run: find 2 => i32:-1
;; run: find 3 => i32:1
;; run: find 4 => i32:-1
;; run: find 5 => i32:2
;; run: find 6 => i32:-1
;; run: find 7 => i32:-1
;; run: find 8 => i32:-1
;; run: find 9 => i32:-1
;; run: find 10 => i32:100
;; run: find 11 => i32:-1
;; run: find 12 => i32:0
;; run: find 13 => i32:-1
;; run: find 14 => i32:-1
;; run: find 15 => i32:1
;; run: find 16 => i32:-1
;; run: find 17 => i32:-1
;; run: find 18 => i32:-1
;; run: find 19 => i32:-1
;; run: find 20 => i32:101
;; run: find 21 => i32:-1
;; run: find 22 => i32:0
;; run: find 23 => i32:-1
;; run: find 24 => i32:-1
;; run: find 25 => i32:1
;; run: find 26 => i32:-1
;; run: find 27 => i32:-1
;; run: find 28 => i32:2
;; run: find 29 => i32:-1
;; run: find 30 => i32:102
;; run: find 31 => i32:0
;; run: find 32 => i32:-1
;; run: find 33 => i32:-1
;; run: find 34 => i32:-1
;; run: find 35 => i32:-1
;; run: find 36 => i32:-1
;; run: find 37 => i32:-1
;; run: find 38 => i32:-1
;; run: find 39 => i32:-1
;; run: find 40 => i32:1
;; run: find 41 => i32:-1
;; run: find 42 => i32:-1
;; run: find 43 => i32:-1
;; run: find 44 => i32:-1
;; run: find 45 => i32:-1
;; run: sum 0 46 => i32:278
;; run: sum 10 31 => i32:294
;; run: sum 31 41 => i32:-7
;; run: sum -5 0 => i32:-5
;; run: near -1 => i32:-1
;; run: near 0 => i32:-1
;; run: near 1 => i32:0
;; run: near 2 => i32:-1
;; run: near 3 => i32:1
;; run: near 4 => i32:-1
;; run: near 5 => i32:2
;; run: near 6 => i32:-1
;; run: near 7 => i32:-1
;; run: near 8 => i32:-1
;; run: near 9 => i32:-1
;; run: near 10 => i32:100
;; run: near 11 => i32:-1
;; run: near 12 => i32:0
;; run: near 13 => i32:-1
;; run: near 14 => i32:-1
;; run: near 15 => i32:1
;; run: near 16 => i32:-1
;; run: near 17 => i32:-1
;; run: near 18 => i32:-1
;; run: near 19 => i32:-1
;; run: near 20 => i32:101
;; run: near 21 => i32:-1
;; run: near 22 => i32:0
;; run: near 23 => i32:-1
;; run: near 24 => i32:-1
;; run: near 25 => i32:1
;; run: near 26 => i32:-1
;; run: near 27 => i32:-1
;; run: near 28 => i32:2
;; run: near 29 => i32:-1
;; run: near 30 => i32:102
;; run: near 31 => i32:0
;; run: near 32 => i32:-1
;; run: near 33 => i32:-1
;; run: near 34 => i32:-1
;; run: near 35 => i32:-1
;; run: near 36 => i32:-1
;; run: near 37 => i32:-1
;; run: near 38 => i32:-1
;; run: near 39 => i32:-1
;; run: near 40 => i32:1
;; run: near 41 => i32:-1
;; run: near 42 => i32:-1
;; run: near 43 => i32:-1
;; run: near 44 => i32:-1
;; run: near 45 => i32:-1
;; run: twoexit 0 10 => i32:0
;; run: twopred 0 10 => i32:0
;; run: twoexit 0 20 => i32:0
;; run: twopred 0 20 => i32:0
;; run: twoexit 0 30 => i32:0
;; run: twopred 0 30 => i32:0
;; run: twoexit 0 3 => i32:0
;; run: twopred 0 3 => i32:0
;; run: twoexit 0 13 => i32:0
;; run: twopred 0 13 => i32:0
;; run: twoexit 0 23 => i32:0
;; run: twopred 0 23 => i32:0
;; run: twoexit 0 0 => i32:0
;; run: twopred 0 0 => i32:0
;; run: twoexit 0 -7 => i32:0
;; run: twopred 0 -7 => i32:0
;; run: twoexit 1 10 => i32:1000
;; run: twopred 1 10 => i32:2000
;; run: twoexit 1 20 => i32:3
;; run: twopred 1 20 => i32:5
;; run: twoexit 1 30 => i32:3
;; run: twopred 1 30 => i32:5
;; run: twoexit 1 3 => i32:3
;; run: twopred 1 3 => i32:0
;; run: twoexit 1 13 => i32:3
;; run: twopred 1 13 => i32:5
;; run: twoexit 1 23 => i32:3
;; run: twopred 1 23 => i32:5
;; run: twoexit 1 0 => i32:3
;; run: twopred 1 0 => i32:5
;; run: twoexit 1 -7 => i32:3
;; run: twopred 1 -7 => i32:5
;; run: twoexit 2 10 => i32:1000
;; run: twopred 2 10 => i32:2000
;; run: twoexit 2 20 => i32:1001
;; run: twopred 2 20 => i32:2001
;; run: twoexit 2 30 => i32:6
;; run: twopred 2 30 => i32:10
;; run: twoexit 2 3 => i32:6
;; run: twopred 2 3 => i32:0
;; run: twoexit 2 13 => i32:6
;; run: twopred 2 13 => i32:5
;; run: twoexit 2 23 => i32:6
;; run: twopred 2 23 => i32:10
;; run: twoexit 2 0 => i32:6
;; run: twopred 2 0 => i32:10
;; run: twoexit 2 -7 => i32:6
;; run: twopred 2 -7 => i32:10
;; run: twoexit 3 10 => i32:1000
;; run: twopred 3 10 => i32:2000
;; run: twoexit 3 20 => i32:1001
;; run: twopred 3 20 => i32:2001
;; run: twoexit 3 30 => i32:1002
;; run: twopred 3 30 => i32:2002
;; run: twoexit 3 3 => i32:9
;; run: twopred 3 3 => i32:0
;; run: twoexit 3 13 => i32:9
;; run: twopred 3 13 => i32:5
;; run: twoexit 3 23 => i32:9
;; run: twopred 3 23 => i32:10
;; run: twoexit 3 0 => i32:9
;; run: twopred 3 0 => i32:15
;; run: twoexit 3 -7 => i32:9
;; run: twopred 3 -7 => i32:15
;; run: twoexit 4 10 => i32:1000
;; run: twopred 4 10 => i32:2000
;; run: twoexit 4 20 => i32:1001
;; run: twopred 4 20 => i32:2001
;; run: twoexit 4 30 => i32:1002
;; run: twopred 4 30 => i32:2002
;; run: twoexit 4 3 => i32:12
;; run: twopred 4 3 => i32:0
;; run: twoexit 4 13 => i32:12
;; run: twopred 4 13 => i32:5
;; run: twoexit 4 23 => i32:12
;; run: twopred 4 23 => i32:10
;; run: twoexit 4 0 => i32:1003
;; run: twopred 4 0 => i32:2003
;; run: twoexit 4 -7 => i32:12
;; run: twopred 4 -7 => i32:15
(module
  (memory 1)
  (data (i32.const 1024) "\03\00\00\00\0a\00\00\00\14\00\00\00\1e\00\00\00")
  (data (i32.const 1088) "\80\04\00\00\00\05\00\00\80\05\00\00\00\06\00\00")
  (data (i32.const 1152) "\03\00\00\00\01\00\00\00\03\00\00\00\05\00\00\00")
  (data (i32.const 1280) "\02\00\00\00\0c\00\00\00\0f\00\00\00")
  (data (i32.const 1408) "\03\00\00\00\16\00\00\00\19\00\00\00\1c\00\00\00")
  (data (i32.const 1536) "\02\00\00\00\1f\00\00\00\28\00\00\00")
  ;; the scan: exhausted, down with i = n; above the key, on; else found or down
  (func $find (export "find") (param $key i32) (result i32)
    (local $node i32) (local $n i32) (local $i i32) (local $k i32) (local $h i32)
    (local.set $node (i32.const 1024))
    (local.set $h (i32.const 1))
    (loop $level
      (local.set $n (i32.load (local.get $node)))
      (local.set $i (i32.const 0))
      (block $down
        (loop $scan
          (br_if $down (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $k (i32.load offset=4 (i32.add (local.get $node) (i32.shl (local.get $i) (i32.const 2)))))
          (if (i32.gt_u (local.get $key) (local.get $k))
            (then (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $scan)))
          (br_if $down (i32.ne (local.get $key) (local.get $k)))
          (return (i32.add (local.get $i) (i32.mul (local.get $h) (i32.const 100))))))
      (if (i32.eqz (local.get $h)) (then (return (i32.const -1))))
      (local.set $h (i32.sub (local.get $h) (i32.const 1)))
      (local.set $node (i32.load offset=64 (i32.add (local.get $node) (i32.shl (local.get $i) (i32.const 2)))))
      (br $level))
    (unreachable))
  (func (export "sum") (param $lo i32) (param $hi i32) (result i32)
    (local $s i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $lo) (local.get $hi)))
        (local.set $s (i32.add (local.get $s) (call $find (local.get $lo))))
        (local.set $lo (i32.add (local.get $lo) (i32.const 1)))
        (br $l)))
    (local.get $s))
  ;; the same, but the second way out is a second scan's, over the keys again
  ;; from the first: a way out of another loop, which joins the first's
  (func $near (export "near") (param $key i32) (result i32)
    (local $node i32) (local $n i32) (local $i i32) (local $k i32) (local $h i32) (local $j i32)
    (local.set $node (i32.const 1024))
    (local.set $h (i32.const 1))
    (loop $level
      (local.set $n (i32.load (local.get $node)))
      (local.set $i (i32.const 0))
      (block $down
        (loop $scan
          (br_if $down (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $k (i32.load offset=4 (i32.add (local.get $node) (i32.shl (local.get $i) (i32.const 2)))))
          (if (i32.gt_u (local.get $key) (local.get $k))
            (then (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $scan))))
        (local.set $j (i32.const 0))
        (loop $again
          (local.set $k (i32.load offset=4 (i32.add (local.get $node) (i32.shl (local.get $j) (i32.const 2)))))
          (if (i32.lt_u (local.get $j) (local.get $i))
            (then (local.set $j (i32.add (local.get $j) (i32.const 1))) (br $again))))
        (br_if $down (i32.ne (local.get $key) (local.get $k)))
        (return (i32.add (local.get $i) (i32.mul (local.get $h) (i32.const 100)))))
      (if (i32.eqz (local.get $h)) (then (return (i32.const -1))))
      (local.set $h (i32.sub (local.get $h) (i32.const 1)))
      (local.set $node (i32.load offset=64 (i32.add (local.get $node) (i32.shl (local.get $i) (i32.const 2)))))
      (br $level))
    (unreachable))
  ;; outside: a scan with two ways out of the loop itself, the second from
  ;; the block after the header, to one block
  (func $twoexit (export "twoexit") (param $n i32) (param $key i32) (result i32)
    (local $i i32) (local $k i32)
    (block $out
      (loop $scan
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $k (i32.load offset=1028 (i32.shl (local.get $i) (i32.const 2))))
        (br_if $out (i32.eq (local.get $k) (local.get $key)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $scan)))
    (if (i32.lt_u (local.get $i) (local.get $n))
      (then (i32.store (i32.const 64) (local.get $i)) (return (i32.add (local.get $i) (i32.const 1000)))))
    (i32.store (i32.const 68) (local.get $n))
    (i32.mul (local.get $i) (i32.const 3)))
  ;; one_pred: the scan's second way out through a block two tests in the
  ;; loop reach
  (func $twopred (export "twopred") (param $n i32) (param $key i32) (result i32)
    (local $i i32) (local $k i32)
    (block $out
      (block $hit
        (loop $scan
          (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $k (i32.load offset=1028 (i32.shl (local.get $i) (i32.const 2))))
          (br_if $hit (i32.eq (local.get $k) (local.get $key)))
          (br_if $hit (i32.eq (local.get $k) (i32.add (local.get $key) (i32.const 7))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $scan)))
      (i32.store (i32.const 72) (local.get $k))
      (br_if $out (i32.ne (local.get $k) (local.get $key)))
      (return (i32.add (local.get $i) (i32.const 2000))))
    (i32.store (i32.const 76) (local.get $n))
    (i32.mul (local.get $i) (i32.const 5)))
)
