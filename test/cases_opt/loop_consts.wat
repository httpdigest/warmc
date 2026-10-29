;; On arm64 a constant two or more instructions make that a loop reads is
;; read there from a copy made in the block over the loop, the header's
;; immediate dominator (codegen.c loop_consts), so that the copy, live only
;; where the loop is, can have a register of its own.  scan's xor of
;; 0x12345 has one copy in each loop the unroller leaves; cheap's 0x1235 is
;; one instruction and has none; nest's inner loop has copies of 0x20003
;; and 0x10007 and the outer one of 0x10007, its own and in each unrolled
;; loop; irred's three states are a cycle with three ways in, no natural
;; loop, whose header would not dominate the states that read the
;; constants -- none; edge's 0x60011 reaches the loop's join as an edge's
;; argument and has a copy; wide's i64 0x123456789 has one in each loop;
;; flt's float constant has none (-O3 sums its floats in another order, the
;; last bit of one answer apart).
;;
;; stats -O2 a64: loop_consts=10
;;
;; run: scan 1 => i32:74565
;; run: scan 5 => i32:74567
;; run: scan 100 => i32:4694
;; run: cheap 1 => i32:4661
;; run: cheap 5 => i32:4663
;; run: cheap 100 => i32:4790
;; run: nest 1 1 => i32:262155
;; run: nest 3 4 => i32:983163
;; run: nest 10 7 => i32:5243640
;; run: irred 5 0 => i32:720937
;; run: irred 7 1 => i32:1310765
;; run: irred 9 2 => i32:131114
;; run: irred 4 5 => i32:458769
;; run: irred 0 1 => i32:0
;; run: edge 1 => i32:0
;; run: edge 2 => i32:393233
;; run: edge 9 => i32:967355024
;; run: wide 1 => i64:4886718345
;; run: wide 5 => i64:4886718355
;; run: wide 100 => i64:3414
;; run: flt 1 => f64:0x0000000000000000
;; run: flt 5 => f64:0x402400014f8b638c
;; run -O3: flt 5 => f64:0x402400014f8b638d
;; run: flt 100 => f64:0x40b35629656d9d91

(module
  (memory 1)
  (func $scan (export "scan") (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (loop $l
      (local.set $s (i32.add (i32.xor (local.get $s) (i32.const 0x12345)) (local.get $i)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))
  (func $cheap (export "cheap") (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (loop $l
      (local.set $s (i32.add (i32.xor (local.get $s) (i32.const 0x1235)) (local.get $i)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))
  (func $nest (export "nest") (param $n i32) (param $m i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (loop $outer
      (local.set $s (i32.add (local.get $s) (i32.const 0x10007)))
      (local.set $j (i32.const 0))
      (loop $inner
        (local.set $s (i32.add (i32.xor (local.get $s) (i32.const 0x20003)) (i32.add (local.get $j) (i32.const 0x10007))))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br_if $inner (i32.lt_u (local.get $j) (local.get $m))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $outer (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))
  (func $irred (export "irred") (param $n i32) (param $st i32) (result i32)
    (local $s i32)
    (block $done
      (loop $l
        (block $b2 (block $b1 (block $b0
          (br_table $b0 $b1 $b2 (local.get $st)))
          (br_if $done (i32.eqz (local.get $n)))
          (local.set $n (i32.sub (local.get $n) (i32.const 1)))
          (local.set $s (i32.add (i32.xor (local.get $s) (i32.const 0x30005)) (i32.const 1)))
          (local.set $st (i32.const 1))
          (br $l))
        (br_if $done (i32.eqz (local.get $n)))
        (local.set $n (i32.sub (local.get $n) (i32.const 1)))
        (local.set $s (i32.xor (local.get $s) (i32.const 0x40009)))
        (local.set $st (i32.const 2))
        (br $l))
       (br_if $done (i32.eqz (local.get $n)))
       (local.set $n (i32.sub (local.get $n) (i32.const 1)))
       (local.set $s (i32.add (local.get $s) (i32.const 0x5000b)))
       (local.set $st (i32.const 0))
       (br $l)))
    (local.get $s))
  (func $edge (export "edge") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $t i32)
    (loop $l
      (local.set $t (local.get $i))
      (if (i32.and (local.get $i) (i32.const 1))
        (then (local.set $t (i32.const 0x60011)) (i32.store (i32.const 0) (local.get $i))))
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $t)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))
  (func $wide (export "wide") (param $n i32) (result i64)
    (local $i i32) (local $s i64)
    (loop $l
      (local.set $s (i64.add (i64.xor (local.get $s) (i64.const 0x123456789)) (i64.extend_i32_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))
  (func $flt (export "flt") (param $n i32) (result f64)
    (local $i i32) (local $s f64)
    (loop $l
      (local.set $s (f64.add (f64.mul (local.get $s) (f64.const 1.000001)) (f64.convert_i32_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $s))
)
