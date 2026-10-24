;; A vectorised loop (pass_vectorize, opt.c) moves 16 bytes at a time, and
;; for a 32-bit memory left a range past the memory's end to the guard page:
;; the first vector's fault came before the scalar iterations ahead of it,
;; and the loop's loads come before its stores.  With -fexact-trap every
;; range is checked against the memory's size, and one past it runs the
;; scalar loop, which traps where it should.  cp copies n words: from 65528
;; two are copied before the trap, and to 65528 two are written.  cpb copies
;; bytes, whose stores have no check of their own on AArch64 with the flag
;; for the vector loop to leave out, as a word's do.
;; run: cp:0,65528,8 a b => trap 1 2
;; run: cp:65528,256,8 r s => trap 3 4
;; run: cp:512,256,8 c => 0 3
;; run: cpb:0,65528,32 a b => trap 1 2
;; run: cpb:65528,256,32 r s => trap 3 4
(module
  (memory (export "memory") 1 1)
  (data (i32.const 65528) "\01\00\00\00\02\00\00\00")
  (data (i32.const 256) "\03\00\00\00\04\00\00\00\05\00\00\00\06\00\00\00\07\00\00\00\08\00\00\00\09\00\00\00\0a\00\00\00")
  (func (export "cp") (param $dst i32) (param $src i32) (param $n i32) (result i32)
    (local $i i32)
    (loop $l
      (i32.store (i32.add (local.get $dst) (i32.shl (local.get $i) (i32.const 2)))
        (i32.load (i32.add (local.get $src) (i32.shl (local.get $i) (i32.const 2)))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (local.get $n))))
    (i32.const 0))
  (func (export "cpb") (param $dst i32) (param $src i32) (param $n i32) (result i32)
    (local $i i32)
    (loop $l
      (i32.store8 (i32.add (local.get $dst) (local.get $i))
        (i32.load8_u (i32.add (local.get $src) (local.get $i))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1))) (local.get $n))))
    (i32.const 0))
  (func (export "a") (result i32) (i32.load (i32.const 0)))
  (func (export "b") (result i32) (i32.load (i32.const 4)))
  (func (export "r") (result i32) (i32.load (i32.const 65528)))
  (func (export "s") (result i32) (i32.load (i32.const 65532)))
  (func (export "c") (result i32) (i32.load (i32.const 512)))
)
