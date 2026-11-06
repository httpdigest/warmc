;; The memory base plus a constant, made one a block by the block's accesses
;; (opt.c mem_ptr_consts), is not shared between blocks through its twin
;; operands (pass_cse takes no mem_ptr's operands to the twins found before
;; it): press reads four cells at 0x21000, two before a loop of 26 sums,
;; which take every register, and two after it -- an add in each of the two
;; blocks, where one shared was held through the loop, spilled and loaded
;; again (cppbench/uniqtree's split, 3.5% more instructions).  Answers
;; worked out in Python.
;;
;; shape: press add\s+x\d+, x20, #0x21, lsl #12=2
;;
;; run: press 0 => i32:10
;; run: press 1 => i32:5860
;; run: press 5 => i32:483353752
;; run: press 100 => i32:1611218298
(module
  (memory 3)
  (data (i32.const 0x21000) "\01\00\00\00\02\00\00\00\03\00\00\00\04\00\00\00")
  (func $press (export "press") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $a0 i32) (local $a1 i32) (local $a2 i32) (local $a3 i32) (local $a4 i32) (local $a5 i32) (local $a6 i32) (local $a7 i32) (local $a8 i32) (local $a9 i32) (local $a10 i32) (local $a11 i32) (local $a12 i32) (local $a13 i32) (local $a14 i32) (local $a15 i32) (local $a16 i32) (local $a17 i32) (local $a18 i32) (local $a19 i32) (local $a20 i32) (local $a21 i32) (local $a22 i32) (local $a23 i32) (local $a24 i32) (local $a25 i32)
    (local.set $s (i32.add (i32.load (i32.const 0x21000)) (i32.load (i32.const 0x21008))))
    (block $d (loop $l
      (br_if $d (i32.ge_s (local.get $i) (local.get $n)))
      (local.set $a0 (i32.xor (i32.mul (local.get $a0) (i32.const 3)) (i32.add (local.get $i) (i32.const 0))))
      (local.set $a1 (i32.xor (i32.mul (local.get $a1) (i32.const 5)) (i32.add (local.get $i) (i32.const 1))))
      (local.set $a2 (i32.xor (i32.mul (local.get $a2) (i32.const 7)) (i32.add (local.get $i) (i32.const 2))))
      (local.set $a3 (i32.xor (i32.mul (local.get $a3) (i32.const 9)) (i32.add (local.get $i) (i32.const 3))))
      (local.set $a4 (i32.xor (i32.mul (local.get $a4) (i32.const 11)) (i32.add (local.get $i) (i32.const 4))))
      (local.set $a5 (i32.xor (i32.mul (local.get $a5) (i32.const 13)) (i32.add (local.get $i) (i32.const 5))))
      (local.set $a6 (i32.xor (i32.mul (local.get $a6) (i32.const 15)) (i32.add (local.get $i) (i32.const 6))))
      (local.set $a7 (i32.xor (i32.mul (local.get $a7) (i32.const 17)) (i32.add (local.get $i) (i32.const 7))))
      (local.set $a8 (i32.xor (i32.mul (local.get $a8) (i32.const 19)) (i32.add (local.get $i) (i32.const 8))))
      (local.set $a9 (i32.xor (i32.mul (local.get $a9) (i32.const 21)) (i32.add (local.get $i) (i32.const 9))))
      (local.set $a10 (i32.xor (i32.mul (local.get $a10) (i32.const 23)) (i32.add (local.get $i) (i32.const 10))))
      (local.set $a11 (i32.xor (i32.mul (local.get $a11) (i32.const 25)) (i32.add (local.get $i) (i32.const 11))))
      (local.set $a12 (i32.xor (i32.mul (local.get $a12) (i32.const 27)) (i32.add (local.get $i) (i32.const 12))))
      (local.set $a13 (i32.xor (i32.mul (local.get $a13) (i32.const 29)) (i32.add (local.get $i) (i32.const 13))))
      (local.set $a14 (i32.xor (i32.mul (local.get $a14) (i32.const 31)) (i32.add (local.get $i) (i32.const 14))))
      (local.set $a15 (i32.xor (i32.mul (local.get $a15) (i32.const 33)) (i32.add (local.get $i) (i32.const 15))))
      (local.set $a16 (i32.xor (i32.mul (local.get $a16) (i32.const 35)) (i32.add (local.get $i) (i32.const 16))))
      (local.set $a17 (i32.xor (i32.mul (local.get $a17) (i32.const 37)) (i32.add (local.get $i) (i32.const 17))))
      (local.set $a18 (i32.xor (i32.mul (local.get $a18) (i32.const 39)) (i32.add (local.get $i) (i32.const 18))))
      (local.set $a19 (i32.xor (i32.mul (local.get $a19) (i32.const 41)) (i32.add (local.get $i) (i32.const 19))))
      (local.set $a20 (i32.xor (i32.mul (local.get $a20) (i32.const 43)) (i32.add (local.get $i) (i32.const 20))))
      (local.set $a21 (i32.xor (i32.mul (local.get $a21) (i32.const 45)) (i32.add (local.get $i) (i32.const 21))))
      (local.set $a22 (i32.xor (i32.mul (local.get $a22) (i32.const 47)) (i32.add (local.get $i) (i32.const 22))))
      (local.set $a23 (i32.xor (i32.mul (local.get $a23) (i32.const 49)) (i32.add (local.get $i) (i32.const 23))))
      (local.set $a24 (i32.xor (i32.mul (local.get $a24) (i32.const 51)) (i32.add (local.get $i) (i32.const 24))))
      (local.set $a25 (i32.xor (i32.mul (local.get $a25) (i32.const 53)) (i32.add (local.get $i) (i32.const 25))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $l)))
    (i32.add (local.get $s) (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.add (i32.load (i32.const 0x21004)) (i32.load (i32.const 0x2100c))) (i32.mul (local.get $a0) (i32.const 1))) (i32.mul (local.get $a1) (i32.const 2))) (i32.mul (local.get $a2) (i32.const 3))) (i32.mul (local.get $a3) (i32.const 4))) (i32.mul (local.get $a4) (i32.const 5))) (i32.mul (local.get $a5) (i32.const 6))) (i32.mul (local.get $a6) (i32.const 7))) (i32.mul (local.get $a7) (i32.const 8))) (i32.mul (local.get $a8) (i32.const 9))) (i32.mul (local.get $a9) (i32.const 10))) (i32.mul (local.get $a10) (i32.const 11))) (i32.mul (local.get $a11) (i32.const 12))) (i32.mul (local.get $a12) (i32.const 13))) (i32.mul (local.get $a13) (i32.const 14))) (i32.mul (local.get $a14) (i32.const 15))) (i32.mul (local.get $a15) (i32.const 16))) (i32.mul (local.get $a16) (i32.const 17))) (i32.mul (local.get $a17) (i32.const 18))) (i32.mul (local.get $a18) (i32.const 19))) (i32.mul (local.get $a19) (i32.const 20))) (i32.mul (local.get $a20) (i32.const 21))) (i32.mul (local.get $a21) (i32.const 22))) (i32.mul (local.get $a22) (i32.const 23))) (i32.mul (local.get $a23) (i32.const 24))) (i32.mul (local.get $a24) (i32.const 25))) (i32.mul (local.get $a25) (i32.const 26)))))
)
