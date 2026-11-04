;; Jump threading on what each edge into a merge hands it and what its way
;; has tested (opt.c thread_known, thread_eval, kt_way):
;;
;; view: a reference kept on one way and null on the other, tested again
;; past their merge -- luawasm's array view, which a store keeps or drops --
;; each edge decided by its value: a type with no null, a null constant.
;; flag: a branch on a value made before the merge, which each way into it
;; had branched on.  merged: a test that ways through a merge of their own
;; decide, every way into that merge having tested the value alike.
;; andtest: the merge's test an and of two compares, one of which a way's
;; constant makes 0.  same: the merge's test made again, of the same
;; operands, as each way had made it.  readbeyond: the test's value is read
;; past the merge too -- the edges go to copies of the block, and what it
;; makes is merged again where it is read (thread_repair_copies).  path:
;; the test is of what a block between was handed, the merge of the ways
;; that decide it, whose own test they do not: the way of the two blocks
;; is copied (thread_path).  loopy:
;; nothing of a loop's header, which an edge past its test would enter in
;; the middle.  The arms store to a global, which keeps if-conversion from
;; making selects of them first (te_diamond); mindiamond's min is left to
;; if-conversion, its merge's test not threaded (addr64's bound, whose
;; select the range prover reads).  Answers worked out by hand.
;;
;; stats -O1: threaded_eval=11 threaded_path=1
;; stats -O2: threaded_eval=11 threaded_path=1
;;
;; run: view 0 5 => i32:5
;; run: view 1 5 => i32:-1
;; run: view 2 7 => i32:7
;; run: view 3 7 => i32:-1
;; run: flag 0 5 => i32:15
;; run: flag 1 5 => i32:106
;; run: flag -1 -5 => i32:96
;; run: merged 3 0 => i32:1005
;; run: merged 3 1 => i32:1004
;; run: merged 9 9 => i32:1010
;; run: merged 10 1 => i32:0
;; run: merged -1 1 => i32:0
;; run: andtest 0 2 => i32:20
;; run: andtest 1 2 => i32:10
;; run: andtest 1 3 => i32:20
;; run: andtest 5 2 => i32:10
;; run: same 0 0 => i32:103
;; run: same 0 1 => i32:101
;; run: same 45 1 => i32:2
;; run: same 44 0 => i32:103
;; run: same -10 1 => i32:2
;; run: same -5 0 => i32:103
;; run: same -6 0 => i32:4
;; run: readbeyond 0 5 => i32:5
;; run: readbeyond 1 5 => i32:999
;; run: readbeyond 0 -3 => i32:-3
;; run: path 1 5 => i32:11
;; run: path 1 200 => i32:1
;; run: path 0 1 => i32:11
;; run: path 0 7 => i32:27
;; run: path 0 300 => i32:300
;; run: path 1 -5 => i32:11
;; run: mindiamond 0 => i32:-1
;; run: mindiamond 5 => i32:20
;; run: mindiamond 5000 => i32:4096
;; run: mindiamond -1 => i32:4096
;; run: loopy 0 => i32:0
;; run: loopy 5 => i32:15
(module
  (type $s (struct (field (mut i32))))
  (global $sink (mut i32) (i32.const 0))

  (func $view (export "view") (param $k i32) (param $x i32) (result i32)
    (local $v (ref null $s))
    (local.set $v (struct.new $s (local.get $x)))
    (if (i32.and (local.get $k) (i32.const 1))
      (then (local.set $v (ref.null $s)) (global.set $sink (i32.const 1))))
    (if (result i32) (ref.is_null (local.get $v))
      (then (i32.const -1))
      (else (struct.get $s 0 (local.get $v)))))

  (func $flag (export "flag") (param $k i32) (param $x i32) (result i32)
    (local $r i32)
    (if (local.get $k)
      (then (local.set $r (i32.add (local.get $x) (i32.const 1))) (global.set $sink (i32.const 1)))
      (else (local.set $r (i32.mul (local.get $x) (i32.const 3))) (global.set $sink (i32.const 2))))
    (global.set $sink (local.get $r))
    (if (result i32) (local.get $k)
      (then (i32.add (local.get $r) (i32.const 100)))
      (else (local.get $r))))

  (func $merged (export "merged") (param $n i32) (param $x i32) (result i32)
    (local $r i32)
    (if (i32.lt_u (local.get $n) (i32.const 10))
      (then
        (if (local.get $x)
          (then (local.set $r (i32.const 1)) (global.set $sink (i32.const 1)))
          (else (local.set $r (i32.const 2)) (global.set $sink (i32.const 2))))
        (local.set $r (i32.add (local.get $r) (local.get $n)))
        (global.set $sink (local.get $r))
        (if (i32.lt_u (local.get $n) (i32.const 20))
          (then (local.set $r (i32.add (local.get $r) (i32.const 1000)))))))
    (local.get $r))

  (func $andtest (export "andtest") (param $k i32) (param $x i32) (result i32)
    (local $kind i32) (local $tag i32)
    (if (local.get $k)
      (then (local.set $kind (i32.const 1)) (local.set $tag (local.get $x)) (global.set $sink (i32.const 1)))
      (else (local.set $kind (i32.const 0)) (local.set $tag (i32.const 2)) (global.set $sink (i32.const 2))))
    (global.set $sink (local.get $tag))
    (if (result i32) (i32.and (i32.eq (local.get $kind) (i32.const 1))
                              (i32.eq (local.get $tag) (i32.const 2)))
      (then (i32.const 10))
      (else (i32.const 20))))

  (func $same (export "same") (param $a i32) (param $b i32) (result i32)
    (local $r i32)
    (if (local.get $b)
      (then
        (if (i32.lt_u (i32.add (local.get $a) (i32.const 5)) (i32.const 50))
          (then (local.set $r (i32.const 1)) (global.set $sink (i32.const 1)))
          (else (local.set $r (i32.const 2)) (global.set $sink (i32.const 2)))))
      (else
        (if (i32.lt_u (i32.add (local.get $a) (i32.const 5)) (i32.const 50))
          (then (local.set $r (i32.const 3)) (global.set $sink (i32.const 3)))
          (else (local.set $r (i32.const 4)) (global.set $sink (i32.const 4))))))
    (global.set $sink (local.get $r))
    (if (result i32) (i32.lt_u (i32.add (local.get $a) (i32.const 5)) (i32.const 50))
      (then (i32.add (local.get $r) (i32.const 100)))
      (else (local.get $r))))

  (func $readbeyond (export "readbeyond") (param $k i32) (param $x i32) (result i32)
    (local $v (ref null $s)) (local $isn i32) (local $r i32)
    (local.set $v (struct.new $s (local.get $x)))
    (if (local.get $k) (then (local.set $v (ref.null $s)) (global.set $sink (i32.const 1))))
    (local.set $isn (ref.is_null (local.get $v)))
    (if (local.get $isn)
      (then (local.set $r (i32.const -1)))
      (else (local.set $r (struct.get $s 0 (local.get $v)))))
    (global.set $sink (local.get $r))
    (i32.add (local.get $r) (i32.mul (local.get $isn) (i32.const 1000))))

  (func $path (export "path") (param $k i32) (param $x i32) (result i32)
    (local $kind i32) (local $r i32)
    (if (local.get $k)
      (then (local.set $kind (i32.const 1)) (global.set $sink (i32.const 1)))
      (else (local.set $kind (local.get $x)) (global.set $sink (i32.const 2))))
    (global.set $sink (local.get $kind))
    (if (i32.lt_s (local.get $x) (i32.const 100))
      (then
        (if (i32.eq (local.get $kind) (i32.const 1))
          (then (local.set $r (i32.const 10)))
          (else (local.set $r (i32.const 20))))))
    (i32.add (local.get $r) (local.get $kind)))

  (func $mindiamond (export "mindiamond") (param $n i32) (result i32)
    (local $m i32)
    (local.set $m (local.get $n))
    (if (i32.gt_u (local.get $n) (i32.const 1024))
      (then (local.set $m (i32.const 1024))))
    (if (result i32) (i32.eqz (local.get $m))
      (then (i32.const -1))
      (else (i32.shl (local.get $m) (i32.const 2)))))

  (func $loopy (export "loopy") (param $n i32) (result i32)
    (local $i i32) (local $r i32)
    (block $out
      (loop $top
        (br_if $out (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $r (i32.add (local.get $r) (i32.const 3)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $top)))
    (local.get $r)))
