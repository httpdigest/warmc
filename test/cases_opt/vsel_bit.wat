;; A select is made in the register its result is in, whichever operand's
;; that is (isel_ops.inc's I_VSEL): the mask's, bsl; the false value's,
;; bit -- the true value's bits where the mask's are set; the true value's,
;; bif.  A select carried round a loop is in the register of the value it
;; keeps, where bsl took a copy of the mask, the select and a move back:
;; vecbench/argmin, 0.60 of the time.
;;
;;   keepf  a vector carried round a loop takes x's lanes where they are
;;          greater than 5, keeping its own -- the false value -- elsewhere
;;   keept  the other way round: keeps its own lanes where x's are greater
;;          than 5 -- the true value -- and takes x's elsewhere
;;   mask   a select of a fresh x and a constant by a mask made for it,
;;          which dies there: bsl in the mask's register
;;
;; with x_i = (7i mod 13, 5i mod 11, 3i mod 17, i mod 19), keepf's and
;; keept's vector starting as (1, 2, 3, 4), and each answer its lanes'
;; sum.  Answers worked out by hand.
;;
;; shape: keepf \sbit\.16b\s=1
;; shape: keepf \sbsl\.16b\s=0
;; shape: keepf \smov\.16b\s=0
;; shape: keept \sbif\.16b\s=1
;; shape: keept \sbsl\.16b\s=0
;; shape: keept \smov\.16b\s=0
;; shape: mask \sbsl\.16b\s=1
;; shape: mask \smov\.16b\s=0
;;
;; run: keepf 0 => i32:10
;; run: keepf 1 => i32:10
;; run: keepf 2 => i32:16
;; run: keepf 7 => i32:38
;; run: keepf 100 => i32:42
;; run: keepf 1000 => i32:46
;; run: keept 0 => i32:10
;; run: keept 1 => i32:0
;; run: keept 2 => i32:9
;; run: keept 7 => i32:12
;; run: keept 100 => i32:13
;; run: keept 1000 => i32:16
;; run: mask 0 => i32:1000
;; run: mask 1 => i32:907
;; run: mask 2 => i32:516
;; run: mask 3 => i32:617
;; run: mask 50 => i32:46
(module
  ;; x_i = (7i mod 13, 5i mod 11, 3i mod 17, i mod 19)
  (func $x (param $i i32) (result v128)
    (i32x4.replace_lane 3
      (i32x4.replace_lane 2
        (i32x4.replace_lane 1
          (i32x4.splat (i32.rem_u (i32.mul (local.get $i) (i32.const 7)) (i32.const 13)))
          (i32.rem_u (i32.mul (local.get $i) (i32.const 5)) (i32.const 11)))
        (i32.rem_u (i32.mul (local.get $i) (i32.const 3)) (i32.const 17)))
      (i32.rem_u (local.get $i) (i32.const 19))))
  (func $sum (param $v v128) (result i32)
    (i32.add
      (i32.add (i32x4.extract_lane 0 (local.get $v)) (i32x4.extract_lane 1 (local.get $v)))
      (i32.add (i32x4.extract_lane 2 (local.get $v)) (i32x4.extract_lane 3 (local.get $v)))))
  (func $keepf (export "keepf") (param $n i32) (result i32)
    (local $v v128) (local $x v128) (local $i i32)
    (local.set $v (v128.const i32x4 1 2 3 4))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $x (call $x (local.get $i)))
        (local.set $v (v128.bitselect (local.get $x) (local.get $v)
                        (i32x4.gt_s (local.get $x) (v128.const i32x4 5 5 5 5))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum (local.get $v)))
  (func $keept (export "keept") (param $n i32) (result i32)
    (local $v v128) (local $x v128) (local $i i32)
    (local.set $v (v128.const i32x4 1 2 3 4))
    (block $done
      (loop $l
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $x (call $x (local.get $i)))
        (local.set $v (v128.bitselect (local.get $v) (local.get $x)
                        (i32x4.gt_s (local.get $x) (v128.const i32x4 5 5 5 5))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum (local.get $v)))
  (func $mask (export "mask") (param $i i32) (result i32)
    (call $sum (v128.bitselect (call $x (local.get $i)) (v128.const i32x4 100 200 300 400)
                 (i32x4.gt_s (call $x (local.get $i)) (v128.const i32x4 5 5 5 5)))))
)
