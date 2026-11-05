;; In a loop, a two-way branch on a test of the loop's counter alone --
;; (i & 15) != 0, a count compared with a constant -- that picks a value the
;; loop carries round keeps its branch (opt.c ifc_try, ifc_counter_cond,
;; ifc_carried): the test repeats with the count and a branch guesses it,
;; where a select would add its latency to every time round.  counter's two
;; sums reach the header through the latch the loop's rotation makes after
;; their join (ifc_carried follows the blocks only the join leads to);
;; ktbench/exn's took 1.4x as two selects.  The near misses: stored picks a
;; value stored each time round, not carried, and hashed tests a bit of a
;; value the loop hashes, not counts (each way round hands it h * a + c, not
;; h plus a constant), which a branch would guess wrong half the time:
;; selects as before.
;;
;; shape: counter \scsel\s=0
;; shape: counter \scinc\s=0
;; shape: stored \scsel\s=1
;; shape: hashed \scsel\s=1
;;
;; run: counter 0 => i64:0
;; run: counter 1 => i64:1
;; run: counter 15 => i64:49001
;; run: counter 16 => i64:56001
;; run: counter 17 => i64:56002
;; run: counter 100 => i64:342007
;; run: counter 1000 => i64:3500063
;; run: stored 0 => i32:0
;; run: stored 1 => i32:7
;; run: stored 15 => i32:42
;; run: stored 16 => i32:45
;; run: stored 17 => i32:7
;; run: stored 100 => i32:297
;; run: stored 1000 => i32:2997
;; run: hashed 0 => i64:0
;; run: hashed 1 => i64:1
;; run: hashed 15 => i64:21008
;; run: hashed 16 => i64:24008
;; run: hashed 17 => i64:27008
;; run: hashed 100 => i64:153049
;; run: hashed 1000 => i64:1563479
(module
  (memory 1)
  (func $counter (export "counter") (param $n i32) (result i64)
    (local $i i32) (local $s i64) (local $c i64)
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (if (i32.and (local.get $i) (i32.const 15))
          (then (local.set $s (i64.add (local.get $s) (i64.extend_i32_s (i32.and (local.get $i) (i32.const 7))))))
          (else (local.set $c (i64.add (local.get $c) (i64.const 1)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i64.add (i64.mul (local.get $s) (i64.const 1000)) (local.get $c)))
  (func $stored (export "stored") (param $n i32) (result i32)
    (local $i i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (i32.store (i32.const 64)
          (if (result i32) (i32.and (local.get $i) (i32.const 15))
            (then (i32.mul (local.get $i) (i32.const 3)))
            (else (i32.const 7))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.load (i32.const 64)))
  (func $hashed (export "hashed") (param $n i32) (result i64)
    (local $i i32) (local $h i32) (local $s i64) (local $c i64)
    (local.set $h (i32.const 1))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (if (i32.and (i32.shr_u (local.get $h) (i32.const 16)) (i32.const 1))
          (then (local.set $s (i64.add (local.get $s) (i64.const 3))))
          (else (local.set $c (i64.add (local.get $c) (i64.const 1)))))
        (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 1103515245)) (i32.const 12345)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i64.add (i64.mul (local.get $s) (i64.const 1000)) (local.get $c)))
)
