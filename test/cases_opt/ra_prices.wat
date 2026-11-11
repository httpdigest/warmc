;; What the allocator is told of what the selection does with each read
;; (regalloc.inc ra_cuse_cost, ra_cs_worth; isel.c analyse_immediates;
;; isel_ops.inc T_BRIF; ir.c ir_reads_low32).
;;
;; $args passes 1, 2 and 3 to every call of its loop, and counts down by 1:
;; each call makes them in its argument registers, `mov w2, #0x1` where the
;; copy from a register holding 1 was `mov x2, x22`, and the count takes 1
;; as an immediate, so no constant holds a callee-saved register over the
;; calls (ra_cpriced; ra_cs_spared where one is free).  $anew passes 5 to a
;; call and fills an array.new of the constant length 3 with it, which
;; escapes: an allocation's operands are made in place too, as its
;; helper's arguments, and 5 is `mov w3, #0x5` there, in no register of
;; its own (analyse_immediates leaves it to be made again).  $fone reads
;; the float 1.5, set at its start, only on a way the branch hint calls
;; rare, in a loop that calls: made again there, it holds no d8 that every
;; call of $fone would save and restore (ra_cs_worth).  $twice tests a
;; compare right after it and once more in a later block: the first branch
;; reads the compare's flags, b.lo, not a cbnz of its 0 or 1 (br_flags).
;; $idx reads and writes an array at i - 1, an i64 wrapped: the index is
;; read as a w register, so the wrap is the i64's register, no mov of it to
;; a w register of its own -- the two left are the allocation's length,
;; the wrap of n, made a w register for its helper.  $pieces adds 77 to two
;; values after each call, an immediate both times, and so no register
;; made 77 there for a piece (regalloc.inc's use-cluster pieces) that
;; neither read -- 77 is a call's argument once, so it is no immediate
;; everywhere, which would leave it no location at all.  $flagsx makes a < b a 0 or 1, read by an add, and tests
;; it after an array's bounds were compared: the flags are the bounds', and
;; the branch is a cbnz of the 0 or 1.  $callee calls through the table's
;; slot 1, a constant the lookup reads from a register, which keeps one --
;; not as an argument is made in place.  $rets passes 9 to a call ahead of
;; its loop, on an edge in it and returns it -- one constant over all its
;; calls, returned on five ways out of it -- each made where it goes,
;; `mov w1, #0x9` and `mov w0, #0x9`, where a register holding 9 over the
;; calls was moved from.  The calls
;; go through an exported table, which the host may change, and so stay
;; calls.  $edge hands 341 on one rare way (a type test's) and 597 on another
;; (a bounds test's) to the block
;; the two meet in, and reads neither anywhere else: each is made on its
;; way, after the function's ret, and no register holds it in the code
;; before -- a register that was free took a mov there each time through
;; (ra_cnone: a constant no read of which a register would save anything).
;;
;; stats -O2 a64: br_flags=1
;; shape: args \b[wx]22\b=0
;; shape: args mov\tw2, #0x1\b=1
;; shape: anew \b[wx]22\b=0
;; shape: anew mov\tw3, #0x5\b=1
;; shape: fone \bd8\b=0
;; shape: twice \bcbnz\b=0
;; shape: twice b\.(lo|hs)\b=1
;; shape: idx mov\tw\d+, w\d+$=2
;; shape: pieces mov\tw\d+, #0x4d\b=1
;; shape: flagsx \bcbnz\tw=1
;; shape: callee \bw22\b=3
;; shape: rets mov\tw\d+, #0x9\b=7
;; shape: edge #0x155\b=1
;; late: edge #0x155\b=1
;; shape: edge #0x255\b=1
;; late: edge #0x255\b=1
;;
;; run: edge 2 0 => i32:347
;; run: edge 2 1 => i32:13
;; run: edge 7 1 => i32:639
;; run: args 0 => i32:6
;; run: args 3 => i32:30
;; run: args 10 => i32:121
;; run: anew 1 => i32:20
;; run: anew 4 => i32:80
;; run: fone 0 => i32:1
;; run: fone 5 => i32:46
;; run: fone 6 => i32:64
;; run: twice 1 2 => i32:123
;; run: twice 2 1 => i32:0
;; run: twice 5 9 => i32:123
;; run: pieces 0 => i32:6160
;; run: pieces 2 => i32:18957
;; run: rets 0 => i32:0
;; run: rets 1 => i32:12
;; run: rets 3 => i32:42
;; run: rets 20 => i32:9
;; run: flagsx 1 2 => i32:1046
;; run: flagsx 2 1 => i32:12
;; run: flagsx 7 7 => i32:30
;; run: callee 0 => i32:0
;; run: callee 4 => i32:30
;; run: idx 1 => i32:7
;; run: idx 3 => i32:91
;; run: idx 10 => i32:206668
(module
  (type $t4 (func (param i32 i32 i32 i32) (result i32)))
  (type $t1 (func (param i32) (result i32)))
  (type $arr (array (mut i32)))
  (table (export "tab") 2 funcref)
  (global $hidx (mut i32) (i32.const 1))
  (global $keep (mut (ref null $arr)) (ref.null $arr))
  (elem (i32.const 0) $g $h)

  (func $g (type $t4) (param i32 i32 i32 i32) (result i32)
    (i32.add (i32.add (local.get 0) (local.get 1))
             (i32.add (local.get 2) (local.get 3))))
  (func $h (type $t1) (param i32) (result i32)
    (i32.mul (local.get 0) (i32.const 3)))

  ;; sum over n, n-1, .. 0 of (k + 1 + 2 + 3)
  (func $args (export "args") (param $n i32) (result i32)
    (local $acc i32)
    (loop $l
      (local.set $acc
        (i32.add (local.get $acc)
          (call_indirect (type $t4) (local.get $n) (i32.const 1) (i32.const 2) (i32.const 3)
                         (i32.const 0))))
      (if (local.get $n)
        (then
          (local.set $n (i32.sub (local.get $n) (i32.const 1)))
          (br $l))))
    (local.get $acc))

  ;; each of n trips: an array [5, 5, 5], kept, its last element, plus h(5)
  (func $anew (export "anew") (param $n i32) (result i32)
    (local $acc i32)
    (loop $l
      (global.set $keep (array.new $arr (i32.const 5) (i32.const 3)))
      (local.set $acc
        (i32.add (local.get $acc)
          (i32.add (array.get $arr (global.get $keep) (i32.const 2))
                   (call_indirect (type $t1) (i32.const 5) (global.get $hidx)))))
      (local.set $n (i32.sub (local.get $n) (i32.const 1)))
      (br_if $l (i32.gt_s (local.get $n) (i32.const 0))))
    (local.get $acc))

  ;; sum over k = n .. 0 of h(k) = 3k, and trunc(0 + 1.5) = 1 where k is 0
  (func $fone (export "fone") (param $n i32) (result i32)
    (local $acc i32) (local $c f64)
    (local.set $c (f64.const 1.5))
    (loop $l
      (local.set $acc
        (i32.add (local.get $acc) (call_indirect (type $t1) (local.get $n) (global.get $hidx))))
      (@metadata.code.branch_hint "\00")
      (if (i32.eqz (local.get $n))
        (then
          (local.set $acc
            (i32.add (local.get $acc)
              (i32.trunc_f64_s
                (f64.add (f64.convert_i32_s (local.get $n)) (local.get $c)))))))
      (if (local.get $n)
        (then
          (local.set $n (i32.sub (local.get $n) (i32.const 1)))
          (br $l))))
    (local.get $acc))

  ;; a < b (unsigned): r = h(1) - 2 = 1; r += h(r); r += 3r; + 107
  (func $twice (export "twice") (param $a i32) (param $b i32) (result i32)
    (local $c i32) (local $r i32)
    (local.set $c (i32.lt_u (local.get $a) (local.get $b)))
    (if (local.get $c)
      (then (local.set $r (i32.add (local.get $r)
                                   (i32.sub (call_indirect (type $t1) (local.get $c) (i32.const 1))
                                            (i32.const 2))))))
    (local.set $r (i32.add (local.get $r) (call_indirect (type $t1) (local.get $r) (i32.const 1))))
    (local.set $r (i32.add (local.get $r) (i32.mul (local.get $r) (i32.const 3))))
    (if (local.get $c) (then (local.set $r (i32.add (local.get $r) (i32.const 107)))))
    (local.get $r))

  ;; a[k] = 7 for k < n, then i from n down to 1: s += a[i-1]; a[i-1] = s
  (func $idx (export "idx") (param $n i64) (result i32)
    (local $a (ref null $arr)) (local $i i64) (local $s i32)
    (local.set $a (array.new $arr (i32.const 7) (i32.wrap_i64 (local.get $n))))
    (local.set $i (local.get $n))
    (loop $l
      (local.set $s
        (i32.add (i32.mul (local.get $s) (i32.const 3))
                 (array.get $arr (local.get $a)
                   (i32.wrap_i64 (i64.sub (local.get $i) (i64.const 1))))))
      (array.set $arr (local.get $a)
        (i32.wrap_i64 (i64.sub (local.get $i) (i64.const 1))) (local.get $s))
      (local.set $i (i64.sub (local.get $i) (i64.const 1)))
      (br_if $l (i64.ne (local.get $i) (i64.const 0))))
    (local.get $s))

  ;; h(77) and the sum over k = n .. 0 of (h(k) + 77) * (k + 77), 77 an
  ;; argument once and an immediate of both adds after each call
  (func $pieces (export "pieces") (param $n i32) (result i32)
    (local $acc i32) (local $t i32)
    (local.set $acc (call_indirect (type $t1) (i32.const 77) (global.get $hidx)))
    (loop $l
      (local.set $t (call_indirect (type $t1) (local.get $n) (global.get $hidx)))
      (local.set $acc
        (i32.add (local.get $acc)
          (i32.mul (i32.add (local.get $t) (i32.const 77))
                   (i32.add (local.get $n) (i32.const 77)))))
      (if (local.get $n)
        (then
          (local.set $n (i32.sub (local.get $n) (i32.const 1)))
          (br $l))))
    (local.get $acc))

  ;; c = a < b (unsigned), and c plus [9, 9, 9, 9] at a & 3, its bounds
  ;; compared after c's compare: c decides the next branch, and a later one
  (func $flagsx (export "flagsx") (param $a i32) (param $b i32) (result i32)
    (local $c i32) (local $v i32)
    (global.set $keep (array.new $arr (i32.const 9) (i32.const 4)))
    (local.set $c (i32.lt_u (local.get $a) (local.get $b)))
    (local.set $v (i32.add (local.get $c)
                    (array.get $arr (global.get $keep) (i32.and (local.get $a) (i32.const 3)))))
    (if (local.get $c)
      (then (local.set $v (i32.add (local.get $v)
                                   (call_indirect (type $t1) (local.get $v) (global.get $hidx))))))
    (local.set $v (i32.add (local.get $v) (call_indirect (type $t1) (local.get $b) (global.get $hidx))))
    (if (local.get $c) (then (local.set $v (i32.add (local.get $v) (i32.const 1000)))))
    (local.get $v))

  ;; sum over k = n .. 0 of h(k), through the table's slot 1, a constant
  ;; the call reads to find its callee, and so from a register
  (func $callee (export "callee") (param $n i32) (result i32)
    (local $acc i32)
    (loop $l
      (local.set $acc
        (i32.add (local.get $acc) (call_indirect (type $t1) (local.get $n) (i32.const 1))))
      (if (local.get $n)
        (then
          (local.set $n (i32.sub (local.get $n) (i32.const 1)))
          (br $l))))
    (local.get $acc))

  ;; h(n) twice and 1; or, where the array a call made is not there (a is
  ;; 0), h(n) and 341; or, where n is past the array's five elements, h(n)
  ;; twice and 597 -- ways a type test's and a bounds test's rules call rare
  (func $edge (export "edge") (param $n i32) (param $a i32) (result i32)
    (local $c i32) (local $r i32)
    (if (local.get $a)
      (then (global.set $keep (array.new $arr (i32.const 5) (i32.const 3)))))
    (block $rare
      (local.set $r (call_indirect (type $t1) (local.get $n) (global.get $hidx)))
      (local.set $c (i32.const 0x155))
      (br_if $rare (i32.eqz (ref.test (ref $arr) (global.get $keep))))
      (local.set $r
        (i32.add (local.get $r) (call_indirect (type $t1) (local.get $n) (global.get $hidx))))
      (local.set $c (i32.const 0x255))
      (br_if $rare (i32.ge_u (local.get $n) (array.len (global.get $keep))))
      (return (i32.add (local.get $r) (i32.const 1))))
    (i32.add (local.get $r) (local.get $c)))

  ;; h(9) - 27, and the sum over k = n .. 0 of h(k) and of 9 or h(k), k's
  ;; lowest bit choosing; 9 at once where the sum passes 1000 or is one of
  ;; four odd sums it never is
  (func $rets (export "rets") (param $n i32) (result i32)
    (local $acc i32) (local $k i32)
    (local.set $acc (i32.sub (call_indirect (type $t1) (i32.const 9) (global.get $hidx))
                             (i32.const 27)))
    (loop $l
      (local.set $acc
        (i32.add (local.get $acc) (call_indirect (type $t1) (local.get $n) (global.get $hidx))))
      (if (i32.gt_u (local.get $acc) (i32.const 1000)) (then (return (i32.const 9))))
      (if (i32.eq (local.get $acc) (i32.const 501)) (then (return (i32.const 9))))
      (if (i32.eq (local.get $acc) (i32.const 503)) (then (return (i32.const 9))))
      (if (i32.eq (local.get $acc) (i32.const 507)) (then (return (i32.const 9))))
      (if (i32.eq (local.get $acc) (i32.const 509)) (then (return (i32.const 9))))
      (if (i32.and (local.get $n) (i32.const 1))
        (then (local.set $k (i32.const 9)))
        (else (local.set $k (call_indirect (type $t1) (local.get $n) (global.get $hidx)))))
      (local.set $acc (i32.add (local.get $acc) (local.get $k)))
      (if (local.get $n)
        (then
          (local.set $n (i32.sub (local.get $n) (i32.const 1)))
          (br $l))))
    (local.get $acc))
)
