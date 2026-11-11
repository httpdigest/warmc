;; The hot way through a function with no rare way back into it (opt.c
;; pass_hot_tails, hot_set_build): each block a rare way comes back into --
;; the block after an `if` whose arm is rare -- is copied for the rare ways,
;; with what follows it up to the next loop header, so what the hot way
;; knew before the `if` it still knows after it.  The table holds a null at
;; 0 and three objects of fields 10/100, 20/200 and 30/300; every function
;; tests an element of it against null, the rare way by ir_unlikely_way.
;;
;;   fwd       a field read before the `if` and again after it, the rare arm
;;             a call that writes the field: the hot way reads it once (the
;;             second read is the first's value) and the copy, emitted late,
;;             reads it again -- of two reads of the field, one late
;;   phi       a local the rare arm writes, read after the `if`: the copy
;;             reads the arm's value, or the hot way's where a second rare
;;             way joins it
;;   chain     two such `if`s, the second's rare arm reached from the hot
;;             way and from the first's copy: all four ways
;;   loop      the `if` in a loop, the copy going round to the loop's own
;;             header; a sum the arm and the code after it both add to
;;   pre       the `if` ahead of a loop: the copy enters the loop beside the
;;             hot way, through one block ahead of the header that merges
;;             them (hot_tail_pres), so LICM still takes the loop -- its
;;             product of two parameters, which is made once, ahead of it:
;;             of one multiply, none in the loop's block
;;   walk      a loop left only by the rare way of its test, a null: the
;;             code after it is on the hot way all the same, and its `if`
;;             gets a copy too (the second read of the field is late)
;;   skip      a type test's rare way straight into the block after an `if`
;;             with no else: that block's store is in the code twice, once
;;             late, in the copy the rare way goes to
;;   nullskip  the same of a null test: a null test and nothing else, its
;;             rare way a way with no block of its own, is taken for one
;;             the program may well take, and the block has no copy (the
;;             copies would be more than twice the function's rare blocks)
;;   nullarm   the null's arm a block of its own: one copy for one rare
;;             block, and the null test's rare way is taken at its word
;;   strict    three null tests in a row, the first's and the third's rare
;;             way with no block of its own, the second's arm a call: the
;;             copies would be more than twice the one rare block, so a
;;             null test's rare way counts only where it is slow -- the
;;             block after the first has no copy, the one after the second
;;             has, and so has what follows it
;;   konst     a constant made in a copied block and read in a loop after it
;;             is a constant there still, the add's own (#1000 once in the
;;             code), where merged with its copy's it was a register
;;   cont      the rare arm goes straight round the loop, to its header: no
;;             block gets a copy (a header is not copied)
;;   arm2      a rare arm of three blocks, a call and an `if` in it: the
;;             block after has its copy, every block of the arm a slow one
;;   coldonly  a function whose one rare way is a branch hint's, no test's
;;             by a rule: left as it is, the store after it once in the code
;;   tiers     two type tests in a row, each with an arm that calls: the
;;             second's arm, which the hot way and the first's copy both
;;             branch to, is emitted after that copy's end, so the copy of
;;             the block after the first test runs into the copy of the
;;             block after the second (ir.c ir_emit_order's count of rare
;;             ways): one call after the function's last ret
;;   licmrare  a product of two parameters in the arm of a loop a type test
;;             fails into stays there (nothing comes out of a rare block):
;;             the multiply is late.
;;             The loop has a loop in it: one the unswitcher may take, an
;;             innermost one, gives up its rare blocks' as any other's
;;   licmtag   the same in the arm a test of a tag makes the rare one, a
;;             guess LICM does not take: the multiply comes out of the loop
;;   licminner licmrare without the loop inside: the multiply comes out
;;
;; Answers worked out in Python.
;;
;; stats -O2: hot_tails=19 hot_tail_pres=2
;; shape: fwd ldr\tw[0-9]+, \[x[0-9]+, #0x8\]=2
;; late: fwd ldr\tw[0-9]+, \[x[0-9]+, #0x8\]=1
;; shape: pre \bmul\b=1
;; late: walk ldr\tw[0-9]+, \[x[0-9]+, #0x8\]=1
;; shape: skip #0x301\b=2
;; late: skip #0x301\b=1
;; shape: nullskip #0x302\b=1
;; shape: nullarm #0x303\b=2
;; shape: strict #0x304\b=1
;; shape: strict #0x305\b=2
;; shape: strict #0x306\b=2
;; shape: konst #0x3e8\b=1
;; shape: cont #0x307\b=1
;; last: tiers \bbl\b.*dbl=1
;; shape: tiers \bbl\b.*dbl=2
;; shape: arm2 #0x308\b=2
;; shape: coldonly #0x309\b=1
;; late: licmrare \bm(ul|add)\b=1
;; shape: licmrare \bm(ul|add)\b=1
;; late: licmtag \bm(ul|add)\b=0
;; shape: licmtag \bm(ul|add)\b=1
;; late: licminner \bm(ul|add)\b=0
;; shape: licminner \bm(ul|add)\b=1
;;
;; run: fwd 1 => i32:20020
;; run: fwd 0 => i32:20025
;; run: fwd 2 => i32:20020
;; run: phi 1 5 => i32:75
;; run: phi 0 5 => i32:145
;; run: phi 0 0 => i32:140
;; run: chain 1 2 => i32:111
;; run: chain 0 2 => i32:212
;; run: chain 1 0 => i32:122
;; run: chain 0 0 => i32:224
;; run: loop 0 => i32:0
;; run: loop 1 => i32:1000
;; run: loop 4 => i32:1606
;; run: loop 8 => i32:3232
;; run: loop 9 => i32:4248
;; run: pre 1 4 3 5 => i32:78
;; run: pre 0 4 3 5 => i32:90
;; run: pre 0 0 3 5 => i32:0
;; run: pre 2 1 7 9 => i32:66
;; run: walk 1 => i32:60060
;; run: walk 3 => i32:30030
;; run: walk 2 => i32:50050
;; run: walk 0 => i32:5
;; run: skip 1 => i32:7
;; run: skip 0 => i32:3
;; run: skip 3 => i32:9
;; run: nullskip 1 => i32:7
;; run: nullskip 0 => i32:3
;; run: nullarm 1 => i32:4
;; run: nullarm 0 => i32:40
;; run: strict 1 1 1 => i32:12
;; run: strict 0 1 1 => i32:6
;; run: strict 1 0 1 => i32:24
;; run: strict 1 1 0 => i32:6
;; run: strict 0 0 0 => i32:6
;; run: konst 1 3 => i32:3006
;; run: konst 0 3 => i32:3020
;; run: konst 1 0 => i32:0
;; run: konst 0 0 => i32:14
;; run: cont 0 => i32:0
;; run: cont 1 => i32:1000
;; run: cont 4 => i32:1606
;; run: cont 9 => i32:4248
;; run: arm2 1 5 => i32:75
;; run: arm2 0 5 => i32:145
;; run: arm2 0 7 => i32:157
;; run: coldonly 1 => i32:7
;; run: coldonly 0 => i32:3
;; run: tiers 1 1 => i32:4
;; run: tiers 0 1 => i32:6
;; run: tiers 1 0 => i32:7
;; run: tiers 0 0 => i32:12
;; run: licmrare 8 3 5 => i32:170
;; run: licmrare 1 3 5 => i32:15
;; run: licmrare 4 7 9 => i32:133
;; run: licminner 8 3 5 => i32:150
;; run: licminner 1 3 5 => i32:15
;; run: licminner 4 7 9 => i32:123
;; run: licmtag 8 3 5 => i32:84
;; run: licmtag 2 3 5 => i32:17
;; run: licmtag 5 7 9 => i32:139
(module
  (type $t (struct (field (mut i32)) (field (mut i32))))
  (type $objs (array (mut (ref null $t))))
  (type $tags (array (mut eqref)))
  (type $ints (array (mut i32)))
  (global $buf (ref $ints) (array.new_default $ints (i32.const 2)))
  (global $tab (ref $objs)
    (array.new_fixed $objs 4 (ref.null $t)
      (struct.new $t (i32.const 10) (i32.const 100))
      (struct.new $t (i32.const 20) (i32.const 200))
      (struct.new $t (i32.const 30) (i32.const 300))))
  (global $tg (ref $tags)
    (array.new_fixed $tags 4 (ref.i31 (i32.const 7)) (ref.i31 (i32.const 5))
      (ref.i31 (i32.const 7)) (ref.i31 (i32.const 5))))
  (func $at (param $i i32) (result (ref null $t))
    (array.get $objs (global.get $tab) (local.get $i)))
;; adds d to o's first field, recursively so that it is not inlined
  (func $bump (param $o (ref null $t)) (param $d i32) (param $k i32)
    (if (i32.eqz (local.get $k))
      (then (struct.set $t 0 (local.get $o) (i32.add (struct.get $t 0 (local.get $o)) (local.get $d))))
      (else (call $bump (local.get $o) (local.get $d) (i32.sub (local.get $k) (i32.const 1))))))
;; twice c, recursively so that it is not inlined
  (func $dbl (param $c i32) (param $k i32) (result i32)
    (if (result i32) (i32.eqz (local.get $k))
      (then (i32.shl (local.get $c) (i32.const 1)))
      (else (call $dbl (local.get $c) (i32.sub (local.get $k) (i32.const 1))))))
  (func $fwd (export "fwd") (param $i i32) (result i32)
    (local $o (ref null $t)) (local $a i32) (local $r i32)
    (local.set $o (call $at (i32.const 2)))
    (local.set $a (struct.get $t 0 (local.get $o)))
    (if (ref.is_null (call $at (local.get $i)))
      (then (call $bump (local.get $o) (i32.const 5) (i32.const 2))))
    (local.set $r (i32.add (i32.mul (local.get $a) (i32.const 1000)) (struct.get $t 0 (local.get $o))))
    (struct.set $t 0 (local.get $o) (i32.const 20))
    (local.get $r))
  (func $phi (export "phi") (param $i i32) (param $j i32) (result i32)
    (local $r i32)
    (local.set $r (i32.const 7))
    (if (ref.is_null (call $at (local.get $i)))
      (then (local.set $r (call $dbl (local.get $r) (i32.const 2)))))
    (i32.add (i32.mul (local.get $r) (i32.const 10)) (local.get $j)))
  (func $chain (export "chain") (param $i i32) (param $j i32) (result i32)
    (local $a i32) (local $b i32)
    (local.set $a (i32.const 1))
    (if (ref.is_null (call $at (local.get $i)))
      (then (local.set $a (call $dbl (local.get $a) (i32.const 2)))))
    (local.set $b (i32.add (local.get $a) (i32.const 10)))
    (if (ref.is_null (call $at (local.get $j)))
      (then (local.set $b (call $dbl (local.get $b) (i32.const 2)))))
    (i32.add (i32.mul (local.get $a) (i32.const 100)) (local.get $b)))
  (func $loop (export "loop") (param $n i32) (result i32)
    (local $j i32) (local $s i32) (local $o (ref null $t))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $j) (local.get $n)))
        (local.set $o (call $at (i32.and (local.get $j) (i32.const 3))))
        (if (ref.is_null (local.get $o))
          (then (local.set $s (i32.add (local.get $s)
                  (i32.add (call $dbl (i32.const 500) (i32.const 2)) (local.get $j)))))
          (else (local.set $s (i32.add (local.get $s) (struct.get $t 1 (local.get $o))))))
        (local.set $s (i32.add (local.get $s) (local.get $j)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $pre (export "pre") (param $i i32) (param $n i32) (param $a i32) (param $b i32) (result i32)
    (local $k i32) (local $j i32) (local $s i32)
    (local.set $k (i32.const 3))
    (if (ref.is_null (call $at (local.get $i)))
      (then (local.set $k (call $dbl (local.get $k) (i32.const 2)))))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $j) (local.get $n)))
        (local.set $s (i32.add (local.get $s)
          (i32.add (i32.mul (local.get $a) (local.get $b)) (i32.add (local.get $k) (local.get $j)))))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $walk (export "walk") (param $p i32) (result i32)
    (local $s i32) (local $o (ref null $t)) (local $q (ref null $t)) (local $a i32) (local $r i32)
    (block $done
      (loop $l
        (local.set $o (call $at (local.get $p)))
        (br_if $done (ref.is_null (local.get $o)))
        (local.set $s (i32.add (local.get $s) (struct.get $t 0 (local.get $o))))
        (local.set $p (i32.and (i32.add (local.get $p) (i32.const 1)) (i32.const 3)))
        (br $l)))
    (local.set $q (call $at (i32.const 2)))
    (local.set $a (i32.add (local.get $s) (i32.sub (struct.get $t 0 (local.get $q)) (i32.const 20))))
    (if (i32.eqz (local.get $s))
      (then (nop))
      (else
        (if (ref.is_null (call $at (i32.and (i32.shr_u (local.get $s) (i32.const 1)) (i32.const 1))))
          (then (call $bump (local.get $q) (i32.const 0) (i32.const 2))))
        (local.set $r (i32.add (i32.mul (local.get $a) (i32.const 1000))
          (i32.add (local.get $s) (i32.sub (struct.get $t 0 (local.get $q)) (i32.const 20)))))
        (return (local.get $r))))
    (i32.const 5))
  (func $skip (export "skip") (param $i i32) (result i32)
    (local $r i32)
    (local.set $r (i32.const 3))
    (block $after
      (br_if $after (i32.eqz (ref.test (ref $t) (call $at (local.get $i)))))
      (local.set $r (call $dbl (local.get $r) (i32.const 2))))
    (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x301))
    (i32.add (local.get $r) (local.get $i)))
  (func $nullskip (export "nullskip") (param $i i32) (result i32)
    (local $r i32)
    (local.set $r (i32.const 3))
    (block $after
      (br_if $after (ref.is_null (call $at (local.get $i))))
      (local.set $r (call $dbl (local.get $r) (i32.const 2))))
    (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x302))
    (i32.add (local.get $r) (local.get $i)))
  (func $nullarm (export "nullarm") (param $i i32) (result i32)
    (local $r i32)
    (local.set $r (i32.const 3))
    (if (ref.is_null (call $at (local.get $i)))
      (then (local.set $r (i32.const 40))
            (array.set $ints (global.get $buf) (i32.const 1) (i32.const 0x403))))
    (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x303))
    (i32.add (local.get $r) (local.get $i)))
  (func $konst (export "konst") (param $i i32) (param $n i32) (result i32)
    (local $m i32) (local $j i32) (local $s i32)
    (if (ref.is_null (call $at (local.get $i)))
      (then (local.set $s (call $dbl (i32.const 7) (i32.const 2)))))
    (local.set $m (i32.const 1000))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $j) (local.get $n)))
        (local.set $s (i32.add (local.get $s)
          (i32.add (local.get $m) (call $dbl (local.get $j) (i32.const 2)))))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $strict (export "strict") (param $i i32) (param $j i32) (param $k i32) (result i32)
    (local $r i32)
    (local.set $r (i32.const 3))
    (block $after
      (br_if $after (ref.is_null (call $at (local.get $i))))
      (local.set $r (call $dbl (local.get $r) (i32.const 2))))
    (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x304))
    (if (ref.is_null (call $at (local.get $j)))
      (then (local.set $r (call $dbl (local.get $r) (i32.const 2)))))
    (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x305))
    (block $last
      (br_if $last (ref.is_null (call $at (local.get $k))))
      (local.set $r (call $dbl (local.get $r) (i32.const 2))))
    (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x306))
    (local.get $r))
  (func $cont (export "cont") (param $n i32) (result i32)
    (local $j i32) (local $s i32) (local $o (ref null $t))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $j) (local.get $n)))
        (local.set $o (call $at (i32.and (local.get $j) (i32.const 3))))
        (local.set $s (i32.add (local.get $s) (local.get $j)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (if (ref.is_null (local.get $o))
          (then (local.set $s (i32.add (local.get $s)
                  (i32.add (call $dbl (i32.const 500) (i32.const 2)) (i32.sub (local.get $j) (i32.const 1)))))
                (br $l)))
        (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x307))
        (local.set $s (i32.add (local.get $s) (struct.get $t 1 (local.get $o))))
        (br $l)))
    (local.get $s))
  (func $arm2 (export "arm2") (param $i i32) (param $j i32) (result i32)
    (local $r i32)
    (local.set $r (i32.const 7))
    (if (ref.is_null (call $at (local.get $i)))
      (then (local.set $r (call $dbl (local.get $r) (i32.const 2)))
            (if (i32.eq (local.get $j) (i32.const 7))
              (then (local.set $r (i32.add (local.get $r) (i32.const 1)))
                    (array.set $ints (global.get $buf) (i32.const 1) (i32.const 0x408))))))
    (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x308))
    (i32.add (i32.mul (local.get $r) (i32.const 10)) (local.get $j)))
  (func $coldonly (export "coldonly") (param $i i32) (result i32)
    (local $r i32)
    (local.set $r (i32.const 3))
    (@metadata.code.branch_hint "\00") (if (i32.eqz (local.get $i))
      (then (local.set $r (call $dbl (i32.const 3) (i32.const 0)))
            (local.set $r (i32.sub (local.get $r) (i32.const 3))))
      (else (local.set $r (call $dbl (local.get $r) (i32.const 2)))))
    (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x309))
    (i32.add (local.get $r) (local.get $i)))
  (func $tiers (export "tiers") (param $i i32) (param $j i32) (result i32)
    (local $r i32)
    (local.set $r (i32.const 3))
    (if (i32.eqz (ref.test (ref $t) (call $at (local.get $i))))
      (then (local.set $r (call $dbl (local.get $r) (i32.const 2)))))
    (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x30a))
    (if (i32.eqz (ref.test (ref $t) (call $at (local.get $j))))
      (then (local.set $r (call $dbl (local.get $r) (i32.const 2)))))
    (array.set $ints (global.get $buf) (i32.const 0) (i32.const 0x30b))
    (i32.add (local.get $r) (local.get $i)))
  (func $licmrare (export "licmrare") (param $n i32) (param $a i32) (param $b i32) (result i32)
    (local $j i32) (local $k i32) (local $s i32) (local $o (ref null $t))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $j) (local.get $n)))
        (local.set $k (i32.const 0))
        (loop $in
          (local.set $s (i32.add (local.get $s) (local.get $k)))
          (local.set $k (i32.add (local.get $k) (i32.const 1)))
          (br_if $in (i32.le_u (local.get $k) (i32.and (local.get $j) (i32.const 3)))))
        (local.set $o (call $at (i32.and (local.get $j) (i32.const 3))))
        (if (ref.test (ref $t) (local.get $o))
          (then (local.set $s (i32.add (local.get $s) (struct.get $t 0 (local.get $o)))))
          (else (local.set $s (i32.add (local.get $s) (i32.mul (local.get $a) (local.get $b))))))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $licminner (export "licminner") (param $n i32) (param $a i32) (param $b i32) (result i32)
    (local $j i32) (local $s i32) (local $o (ref null $t))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $j) (local.get $n)))
        (local.set $o (call $at (i32.and (local.get $j) (i32.const 3))))
        (if (ref.test (ref $t) (local.get $o))
          (then (local.set $s (i32.add (local.get $s) (struct.get $t 0 (local.get $o)))))
          (else (local.set $s (i32.add (local.get $s) (i32.mul (local.get $a) (local.get $b))))))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $licmtag (export "licmtag") (param $n i32) (param $a i32) (param $b i32) (result i32)
    (local $j i32) (local $k i32) (local $s i32)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $j) (local.get $n)))
        (local.set $k (i32.const 0))
        (loop $in
          (local.set $s (i32.add (local.get $s) (local.get $k)))
          (local.set $k (i32.add (local.get $k) (i32.const 1)))
          (br_if $in (i32.le_u (local.get $k) (i32.and (local.get $j) (i32.const 3)))))
        (if (ref.eq (array.get $tags (global.get $tg) (i32.and (local.get $j) (i32.const 3)))
                    (ref.i31 (i32.const 7)))
          (then (local.set $s (i32.add (local.get $s) (i32.const 1))))
          (else (local.set $s (i32.add (local.get $s) (i32.mul (local.get $a) (local.get $b))))))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (local.get $s))
)
