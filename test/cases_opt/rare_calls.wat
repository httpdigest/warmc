;; What the allocator makes of a call on a way the rules call rare
;; (regalloc.inc's rareb, opt.c ir_rare_blocks).  The table holds a $base at
;; 0 and an $a at 1; x's two mixes, a and b, are read after the call each
;; function makes.
;;
;;   csa       the call on the way a type test fails by: a and b, live
;;             across that call alone, are in caller-saved registers, saved
;;             round the call where it is made -- the function saves no
;;             callee-saved register on its way in, and runs none of the
;;             saves where the test passes (rare_csa)
;;   hotcall   the call on the way the test passes by: a and b in
;;             callee-saved registers, saved once
;;   quarter   the call behind two tests of x no rule knows, in a function
;;             whose type test has a rare way: a call on a likely way is
;;             one every run makes, not one run in four as even splits at
;;             each branch would have it -- a and b keep callee-saved
;;             registers, and nothing is saved round the call
;;   nulledge  on the rare way, a local set on one of two ways and null on
;;             the other, read past a call: the null, a constant the
;;             function's start holds for the local, is made on its edge,
;;             no register set to 0 where the function starts
;;   constedge on the rare way, two blocks hand 1234 or 1235 to the block
;;             they meet in, which calls: each constant is made where it is
;;             handed on, after the function's ret, none where the function
;;             starts
;;
;; Answers worked out in Python: a = 3x + 1, b = x ^ (x >> 2), slow(c, k) =
;; c + 3k.
;;
;; stats -O2 a64: rare_csa=3
;;
;; shape: csa stp\s+x2[0-8]=0
;; shape: hotcall stp\s+x2[0-8]=1
;; shape: quarter stp\s+x2[0-8]=1
;; shape: quarter (str|ldr)\s+x\d+, \[sp=0
;; shape: nulledge mov\s+x\d+, #0x0\b=1
;; late: nulledge mov\s+x\d+, #0x0\b=1
;; shape: constedge #0x4d2\b=1
;; late: constedge #0x4d2\b=1
;; shape: constedge #0x4d3\b=1
;; late: constedge #0x4d3\b=1
;;
;; run: csa 1 20 => i32:98
;; run: csa 0 20 => i32:104
;; run: hotcall 1 20 => i32:104
;; run: hotcall 0 20 => i32:98
;; run: quarter 1 20 => i32:98
;; run: quarter 1 23 => i32:117
;; run: quarter 0 23 => i32:1114
;; run: nulledge 1 20 => i32:20
;; run: nulledge 0 20 => i32:24
;; run: nulledge 0 21 => i32:24
;; run: constedge 1 20 => i32:20
;; run: constedge 0 20 => i32:1260
;; run: constedge 0 21 => i32:1259
(module
  (type $base (sub (struct (field i32))))
  (type $a (sub $base (struct (field i32) (field i32))))
  (type $objs (array (mut (ref null $base))))
  (global $tab (mut (ref $objs))
    (array.new_fixed $objs 2 (struct.new $base (i32.const 5))
      (struct.new $a (i32.const 6) (i32.const 60))))
  ;; c + 3k, recursively so that it is not inlined
  (func $slow (param $c i32) (param $k i32) (result i32)
    (if (result i32) (i32.eqz (local.get $k))
      (then (local.get $c))
      (else (call $slow (i32.add (local.get $c) (i32.const 3)) (i32.sub (local.get $k) (i32.const 1))))))
  (func $csa (export "csa") (param $i i32) (param $x i32) (result i32)
    (local $a i32) (local $b i32)
    (local.set $a (i32.add (i32.mul (local.get $x) (i32.const 3)) (i32.const 1)))
    (local.set $b (i32.xor (local.get $x) (i32.shr_u (local.get $x) (i32.const 2))))
    (if (i32.eqz (ref.test (ref $a) (array.get $objs (global.get $tab) (local.get $i))))
      (then (local.set $x (call $slow (local.get $x) (i32.const 2)))))
    (i32.add (i32.add (local.get $a) (local.get $b)) (local.get $x)))
  (func $hotcall (export "hotcall") (param $i i32) (param $x i32) (result i32)
    (local $a i32) (local $b i32)
    (local.set $a (i32.add (i32.mul (local.get $x) (i32.const 3)) (i32.const 1)))
    (local.set $b (i32.xor (local.get $x) (i32.shr_u (local.get $x) (i32.const 2))))
    (if (ref.test (ref $a) (array.get $objs (global.get $tab) (local.get $i)))
      (then (local.set $x (call $slow (local.get $x) (i32.const 2)))))
    (i32.add (i32.add (local.get $a) (local.get $b)) (local.get $x)))
  (func $quarter (export "quarter") (param $i i32) (param $x i32) (result i32)
    (local $a i32) (local $b i32)
    (if (i32.eqz (ref.test (ref $a) (array.get $objs (global.get $tab) (local.get $i))))
      (then (return (i32.add (call $slow (local.get $x) (i32.const 0)) (i32.const 1091)))))
    (local.set $a (i32.add (i32.mul (local.get $x) (i32.const 3)) (i32.const 1)))
    (local.set $b (i32.xor (local.get $x) (i32.shr_u (local.get $x) (i32.const 2))))
    (if (i32.and (local.get $x) (i32.const 1))
      (then
        (if (i32.and (local.get $x) (i32.const 2))
          (then (local.set $x (call $slow (local.get $x) (i32.const 2)))))))
    (i32.add (i32.add (local.get $a) (local.get $b)) (local.get $x)))
  (func $nulledge (export "nulledge") (param $i i32) (param $x i32) (result i32)
    (local $r (ref null $base))
    (if (i32.eqz (ref.test (ref $a) (array.get $objs (global.get $tab) (local.get $i))))
      (then
        (if (i32.and (local.get $x) (i32.const 1))
          (then (local.set $r (array.get $objs (global.get $tab) (i32.const 0)))))
        (local.set $x (call $slow (local.get $x) (i32.const 1)))
        (local.set $x (i32.add (local.get $x) (ref.is_null (local.get $r))))))
    (local.get $x))
  (func $constedge (export "constedge") (param $i i32) (param $x i32) (result i32)
    (local $c i32)
    (if (i32.eqz (ref.test (ref $a) (array.get $objs (global.get $tab) (local.get $i))))
      (then
        (if (i32.and (local.get $x) (i32.const 1))
          (then (local.set $x (call $slow (local.get $x) (i32.const 1)))
                (local.set $c (i32.const 0x4d3)))
          (else (local.set $x (call $slow (local.get $x) (i32.const 2)))
                (local.set $c (i32.const 0x4d2))))
        (local.set $x (i32.add (call $slow (local.get $c) (i32.const 0)) (local.get $x)))
        (local.set $x (i32.add (local.get $x) (local.get $c)))
        (local.set $x (i32.sub (local.get $x) (local.get $c)))))
    (local.get $x)))
