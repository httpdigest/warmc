;; The reload cache -- the two registers a value read from its slot, or
;; just stored to it, is kept in for the reads that follow -- goes on into
;; a block whose only way in is the end of the block emitted just before it
;; (isel_core.inc rc_carries, emit_block), and a value stored with a read
;; there to come is put in it (used_soon): the probe of asbench/mapint made
;; its hash, stored it, branched on a bounds check and loaded it back,
;; 0.93 of the time without the load.
;;
;;   carry  ten values made by calls and kept past more, so that some live
;;          in slots; k, stored, is read past a branch that returns when n
;;          is 77 -- from the cache, not its slot
;;   merge  sixteen such values; v14 and v15, in slots, read on one way
;;          into a merge only, and then in the merge, which has to load
;;          them: the other way in has other values in the cache
;;   apart  twenty-four; the likely way past a test of n, laid out next,
;;          reads v0 to v11, some from slots, and returns; the rare way,
;;          its only way in the test's block but laid out after the likely
;;          one, reads them the last first -- from their slots, not from the
;;          cache the likely way left
;;
;; f(v) is v xor 0x55, through a function that calls itself so that it is
;; not inlined.  Answers worked out by hand.
;;
;; shape: carry ldr\s+[wx]\d+, \[sp=4
;;
;; run: carry 0 => i32:557777782
;; run: carry 1 => i32:-2122420356
;; run: carry 5 => i32:1473444959
;; run: carry 77 => i32:-1
;; run: carry 1000 => i32:1955990848
;; run: carry -3 => i32:-1856037082
;; run: merge 0 => i32:169509411
;; run: merge 1 => i32:1070407797
;; run: merge 5 => i32:-1828475211
;; run: merge 77 => i32:113006501
;; run: merge 1000 => i32:1126926899
;; run: merge -3 => i32:-1828475899
;; run: apart 0 => i32:650
;; run: apart 1 => i32:694
;; run: apart 77 => i32:214750379
;; run: apart 1000 => i32:11626
;; run: apart -3 => i32:246
(module
  (global $g (mut i32) (i32.const 0))
  (global $g2 (mut i32) (i32.const 0))
  ;; adds its argument to g and gives it back xored with 0x55, by way of
  ;; itself so that it is never inlined
  (func $f (param $v i32) (result i32) (call $f2 (local.get $v) (i32.const 1)))
  (func $f2 (param $v i32) (param $k i32) (result i32)
    (if (result i32) (i32.eqz (local.get $k))
      (then
        (global.set $g (i32.add (global.get $g) (local.get $v)))
        (i32.xor (local.get $v) (i32.const 0x55)))
      (else (call $f2 (local.get $v) (i32.sub (local.get $k) (i32.const 1))))))
  (func $carry (export "carry") (param $n i32) (result i32)
    (local $a i32) (local $b i32) (local $c i32) (local $d i32) (local $e i32)
    (local $g i32) (local $h i32) (local $i i32) (local $j i32) (local $k i32)
    (local $x i32) (local $y i32)
    (local.set $a (call $f (local.get $n)))
    (local.set $b (call $f (i32.add (local.get $a) (i32.const 1))))
    (local.set $c (call $f (i32.add (local.get $b) (i32.const 2))))
    (local.set $d (call $f (i32.add (local.get $c) (i32.const 3))))
    (local.set $e (call $f (i32.add (local.get $d) (i32.const 4))))
    (local.set $g (call $f (i32.add (local.get $e) (i32.const 5))))
    (local.set $h (call $f (i32.add (local.get $g) (i32.const 6))))
    (local.set $i (call $f (i32.add (local.get $h) (i32.const 7))))
    (local.set $j (call $f (i32.add (local.get $i) (i32.const 8))))
    (local.set $k (call $f (i32.add (local.get $j) (i32.const 9))))
    ;; x: made, then a branch, then read in the block after it -- and kept
    ;; past the next call, with a to k
    (if (i32.eq (local.get $n) (i32.const 77)) (then (return (i32.const -1))))
    (local.set $x (i32.mul (i32.xor (local.get $n) (local.get $k)) (i32.const 0x9e3779b1)))
    (local.set $y (i32.add (i32.add (local.get $x) (i32.shr_u (local.get $x) (i32.const 7)))
                    (local.get $n)))
    (local.set $y (call $f (local.get $y)))
    (local.set $y (call $f (i32.add (local.get $y) (local.get $x))))
    (local.set $y (call $f (i32.add (local.get $y) (local.get $x))))
    (local.set $y (call $f (i32.add (local.get $y) (local.get $x))))
    (i32.add
      (i32.add
        (i32.add (i32.add (local.get $a) (local.get $b)) (i32.add (local.get $c) (local.get $d)))
        (i32.add (i32.add (local.get $e) (local.get $g)) (i32.add (local.get $h) (local.get $i))))
      (i32.add (i32.add (local.get $j) (local.get $k)) (i32.add (local.get $x) (local.get $y)))))
  (func $merge (export "merge") (param $n i32) (result i32)
    (local $v0 i32) (local $v1 i32) (local $v2 i32) (local $v3 i32) (local $v4 i32) (local $v5 i32) (local $v6 i32) (local $v7 i32) (local $v8 i32) (local $v9 i32) (local $v10 i32) (local $v11 i32) (local $v12 i32) (local $v13 i32) (local $v14 i32) (local $v15 i32) (local $u i32) (local $w i32)
    (local.set $v0 (call $f (local.get $n)))
    (local.set $v1 (call $f (i32.add (local.get $v0) (i32.const 1))))
    (local.set $v2 (call $f (i32.add (local.get $v1) (i32.const 2))))
    (local.set $v3 (call $f (i32.add (local.get $v2) (i32.const 3))))
    (local.set $v4 (call $f (i32.add (local.get $v3) (i32.const 4))))
    (local.set $v5 (call $f (i32.add (local.get $v4) (i32.const 5))))
    (local.set $v6 (call $f (i32.add (local.get $v5) (i32.const 6))))
    (local.set $v7 (call $f (i32.add (local.get $v6) (i32.const 7))))
    (local.set $v8 (call $f (i32.add (local.get $v7) (i32.const 8))))
    (local.set $v9 (call $f (i32.add (local.get $v8) (i32.const 9))))
    (local.set $v10 (call $f (i32.add (local.get $v9) (i32.const 10))))
    (local.set $v11 (call $f (i32.add (local.get $v10) (i32.const 11))))
    (local.set $v12 (call $f (i32.add (local.get $v11) (i32.const 12))))
    (local.set $v13 (call $f (i32.add (local.get $v12) (i32.const 13))))
    (local.set $v14 (call $f (i32.add (local.get $v13) (i32.const 14))))
    (local.set $v15 (call $f (i32.add (local.get $v14) (i32.const 15))))
    ;; read late after their calls: the cache holds two of them
    (local.set $u (i32.add (local.get $v12) (local.get $v13)))
    (block $m
      (br_if $m (i32.ne (local.get $n) (i32.const 77)))
      ;; only on this way in to the merge are v14 and v15 read, into the cache
      (global.set $g2 (i32.add (local.get $v14) (local.get $v15))))
    (local.set $w (i32.mul (i32.xor (local.get $v14) (local.get $v15)) (i32.const 0x9e3779b1)))
    (local.set $w (call $f (local.get $w)))
    (i32.add (i32.add (local.get $u) (local.get $w)) (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (local.get $v0) (local.get $v1)) (local.get $v2)) (local.get $v3)) (local.get $v4)) (local.get $v5)) (local.get $v6)) (local.get $v7)) (local.get $v8)) (local.get $v9)) (local.get $v10)) (local.get $v11)) (local.get $v12)) (local.get $v13)) (local.get $v14)) (local.get $v15))))
  (func $apart (export "apart") (param $n i32) (result i32)
    (local $v0 i32) (local $v1 i32) (local $v2 i32) (local $v3 i32) (local $v4 i32) (local $v5 i32) (local $v6 i32) (local $v7 i32) (local $v8 i32) (local $v9 i32) (local $v10 i32) (local $v11 i32) (local $v12 i32) (local $v13 i32) (local $v14 i32) (local $v15 i32) (local $v16 i32) (local $v17 i32) (local $v18 i32) (local $v19 i32) (local $v20 i32) (local $v21 i32) (local $v22 i32) (local $v23 i32) (local $w i32)
    (local.set $v0 (call $f (local.get $n)))
    (local.set $v1 (call $f (i32.add (local.get $v0) (i32.const 1))))
    (local.set $v2 (call $f (i32.add (local.get $v1) (i32.const 2))))
    (local.set $v3 (call $f (i32.add (local.get $v2) (i32.const 3))))
    (local.set $v4 (call $f (i32.add (local.get $v3) (i32.const 4))))
    (local.set $v5 (call $f (i32.add (local.get $v4) (i32.const 5))))
    (local.set $v6 (call $f (i32.add (local.get $v5) (i32.const 6))))
    (local.set $v7 (call $f (i32.add (local.get $v6) (i32.const 7))))
    (local.set $v8 (call $f (i32.add (local.get $v7) (i32.const 8))))
    (local.set $v9 (call $f (i32.add (local.get $v8) (i32.const 9))))
    (local.set $v10 (call $f (i32.add (local.get $v9) (i32.const 10))))
    (local.set $v11 (call $f (i32.add (local.get $v10) (i32.const 11))))
    (local.set $v12 (call $f (i32.add (local.get $v11) (i32.const 12))))
    (local.set $v13 (call $f (i32.add (local.get $v12) (i32.const 13))))
    (local.set $v14 (call $f (i32.add (local.get $v13) (i32.const 14))))
    (local.set $v15 (call $f (i32.add (local.get $v14) (i32.const 15))))
    (local.set $v16 (call $f (i32.add (local.get $v15) (i32.const 16))))
    (local.set $v17 (call $f (i32.add (local.get $v16) (i32.const 17))))
    (local.set $v18 (call $f (i32.add (local.get $v17) (i32.const 18))))
    (local.set $v19 (call $f (i32.add (local.get $v18) (i32.const 19))))
    (local.set $v20 (call $f (i32.add (local.get $v19) (i32.const 20))))
    (local.set $v21 (call $f (i32.add (local.get $v20) (i32.const 21))))
    (local.set $v22 (call $f (i32.add (local.get $v21) (i32.const 22))))
    (local.set $v23 (call $f (i32.add (local.get $v22) (i32.const 23))))
    ;; the likely way, laid out next, reads v0 to v11 -- some of them in
    ;; slots, through the cache -- and returns; the rare one, laid out
    ;; later, reads them again, the last first
    (if (i32.ne (local.get $n) (i32.const 77))
      (then (return (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (local.get $v0) (local.get $v1)) (local.get $v2)) (local.get $v3)) (local.get $v4)) (local.get $v5)) (local.get $v6)) (local.get $v7)) (local.get $v8)) (local.get $v9)) (local.get $v10)) (local.get $v11)))))
    (local.set $w (call $f (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (i32.xor (i32.mul (local.get $v11) (i32.const 31)) (local.get $v10)) (i32.const 31)) (local.get $v9)) (i32.const 31)) (local.get $v8)) (i32.const 31)) (local.get $v7)) (i32.const 31)) (local.get $v6)) (i32.const 31)) (local.get $v5)) (i32.const 31)) (local.get $v4)) (i32.const 31)) (local.get $v3)) (i32.const 31)) (local.get $v2)) (i32.const 31)) (local.get $v1)) (i32.const 31)) (local.get $v0))))
    (i32.add (local.get $w) (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (local.get $v0) (local.get $v1)) (local.get $v2)) (local.get $v3)) (local.get $v4)) (local.get $v5)) (local.get $v6)) (local.get $v7)) (local.get $v8)) (local.get $v9)) (local.get $v10)) (local.get $v11)) (local.get $v12)) (local.get $v13)) (local.get $v14)) (local.get $v15)) (local.get $v16)) (local.get $v17)) (local.get $v18)) (local.get $v19)) (local.get $v20)) (local.get $v21)) (local.get $v22)) (local.get $v23))))
)
