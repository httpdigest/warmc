;; Tests of an Ordering (opt.c fold_order_cmp): d = (a > b) - (a < b), as
;; Rust's Ord::cmp makes it, tested as d & 255 against 1, 0, 255 and a
;; value it never is, by eq and ne, by eqz, and d itself against 1 and -1
;; and signed against 0 -- each the compare of a and b it stands for, signed
;; or unsigned as the pair is, either compare spelled the other way round,
;; at i32 and i64.  What does not fold: a pair of mixed signedness ($mixed),
;; a mask that is even or 1, or a masked value tested signed ($masks),
;; compares of different operands ($others), and two compares of which
;; neither is a > b however spelled ($degenerate).
;; Each export packs its tests' answers into the bits of one i32.
;;
;; search is BTreeMap's search of a node: a branch on d & 255 == 1, then
;; on == 0; its loop keeps no Ordering:
;; shape: search cset=0
;; shape: search sub\s+w=0
;;
;; run: tests_u 5 9 => i32:22708
;; run: tests_u 9 5 => i32:42209
;; run: tests_u 7 7 => i32:50130
;; run: tests_u -1 1 => i32:42209
;; run: tests_s -1 1 => i32:22708
;; run: tests_s 1 -1 => i32:42209
;; run: tests_s 3 3 => i32:50130
;; run: tests_s -5 -9 => i32:42209
;; run: swapped 2 3 => i32:21
;; run: swapped 3 2 => i32:10
;; run: swapped 3 3 => i32:76
;; run: wide 1 2 => i32:1
;; run: wide 4294967296 1 => i32:2
;; run: wide -1 -1 => i32:4
;; run: mixed -1 1 => i32:1
;; run: mixed 1 2 => i32:0
;; run: masks -1 1 => i32:6
;; run: masks 1 -1 => i32:3
;; run: masks 1 2 => i32:3
;; run: masks 5 5 => i32:4
;; run: others 1 2 3 => i32:1
;; run: others 1 1 3 => i32:1
;; run: degenerate 1 2 => i32:1
;; run: degenerate 2 1 => i32:1
;; run: search 25 => i32:2
;; run: search 30 => i32:-2147483645
;; run: search 70 => i32:6
;; run: search 1 => i32:-2147483648
(module
  (memory 1)
  (data (i32.const 0) "\0a\00\00\00\14\00\00\00\19\00\00\00\28\00\00\00\32\00\00\00\3c\00\00\00")

  ;; bit i: test i of the unsigned Ordering of a and b
  (func $tests_u (export "tests_u") (param $a i32) (param $b i32) (result i32)
    (local $d i32) (local $t i32)
    (local.set $d (i32.sub (i32.gt_u (local.get $a) (local.get $b))
                           (i32.lt_u (local.get $a) (local.get $b))))
    (local.set $t (i32.and (local.get $d) (i32.const 255)))
    (i32.or (i32.or (i32.or
      (i32.or (i32.shl (i32.eq (local.get $t) (i32.const 1)) (i32.const 0))
              (i32.shl (i32.eq (local.get $t) (i32.const 0)) (i32.const 1)))
      (i32.or (i32.shl (i32.eq (local.get $t) (i32.const 255)) (i32.const 2))
              (i32.shl (i32.eq (local.get $t) (i32.const 7)) (i32.const 3))))
      (i32.or
      (i32.or (i32.shl (i32.ne (local.get $t) (i32.const 1)) (i32.const 4))
              (i32.shl (i32.ne (local.get $t) (i32.const 0)) (i32.const 5)))
      (i32.or (i32.shl (i32.ne (local.get $t) (i32.const 255)) (i32.const 6))
              (i32.shl (i32.ne (local.get $t) (i32.const 7)) (i32.const 7)))))
      (i32.or
      (i32.or (i32.shl (i32.eqz (local.get $t)) (i32.const 8))
              (i32.shl (i32.eqz (local.get $d)) (i32.const 9)))
      (i32.or
      (i32.or (i32.shl (i32.eq (local.get $d) (i32.const 1)) (i32.const 10))
              (i32.shl (i32.eq (local.get $d) (i32.const -1)) (i32.const 11)))
      (i32.or
      (i32.or (i32.shl (i32.lt_s (local.get $d) (i32.const 0)) (i32.const 12))
              (i32.shl (i32.gt_s (local.get $d) (i32.const 0)) (i32.const 13)))
      (i32.or (i32.shl (i32.le_s (local.get $d) (i32.const 0)) (i32.const 14))
              (i32.shl (i32.ge_s (local.get $d) (i32.const 0)) (i32.const 15))))))))

  ;; the same tests of the signed Ordering
  (func $tests_s (export "tests_s") (param $a i32) (param $b i32) (result i32)
    (local $d i32) (local $t i32)
    (local.set $d (i32.sub (i32.gt_s (local.get $a) (local.get $b))
                           (i32.lt_s (local.get $a) (local.get $b))))
    (local.set $t (i32.and (local.get $d) (i32.const 255)))
    (i32.or (i32.or (i32.or
      (i32.or (i32.shl (i32.eq (local.get $t) (i32.const 1)) (i32.const 0))
              (i32.shl (i32.eq (local.get $t) (i32.const 0)) (i32.const 1)))
      (i32.or (i32.shl (i32.eq (local.get $t) (i32.const 255)) (i32.const 2))
              (i32.shl (i32.eq (local.get $t) (i32.const 7)) (i32.const 3))))
      (i32.or
      (i32.or (i32.shl (i32.ne (local.get $t) (i32.const 1)) (i32.const 4))
              (i32.shl (i32.ne (local.get $t) (i32.const 0)) (i32.const 5)))
      (i32.or (i32.shl (i32.ne (local.get $t) (i32.const 255)) (i32.const 6))
              (i32.shl (i32.ne (local.get $t) (i32.const 7)) (i32.const 7)))))
      (i32.or
      (i32.or (i32.shl (i32.eqz (local.get $t)) (i32.const 8))
              (i32.shl (i32.eqz (local.get $d)) (i32.const 9)))
      (i32.or
      (i32.or (i32.shl (i32.eq (local.get $d) (i32.const 1)) (i32.const 10))
              (i32.shl (i32.eq (local.get $d) (i32.const -1)) (i32.const 11)))
      (i32.or
      (i32.or (i32.shl (i32.lt_s (local.get $d) (i32.const 0)) (i32.const 12))
              (i32.shl (i32.gt_s (local.get $d) (i32.const 0)) (i32.const 13)))
      (i32.or (i32.shl (i32.le_s (local.get $d) (i32.const 0)) (i32.const 14))
              (i32.shl (i32.ge_s (local.get $d) (i32.const 0)) (i32.const 15))))))))

  ;; (b < a) - (b > a), and (a > b) - (b > a): a > b, == and a < b of each,
  ;; in bits 0-2 and 3-5
  (func $swapped (export "swapped") (param $a i32) (param $b i32) (result i32)
    (local $d i32) (local $e i32)
    (local.set $d (i32.and (i32.sub (i32.lt_u (local.get $b) (local.get $a))
                                    (i32.gt_u (local.get $b) (local.get $a))) (i32.const 255)))
    (local.set $e (i32.and (i32.sub (i32.gt_u (local.get $a) (local.get $b))
                                    (i32.gt_u (local.get $b) (local.get $a))) (i32.const 255)))
    (i32.or
      (i32.or (i32.or (i32.eq (local.get $d) (i32.const 255))
                      (i32.shl (i32.eq (local.get $d) (i32.const 1)) (i32.const 1)))
              (i32.shl (i32.eqz (local.get $d)) (i32.const 6)))
      (i32.or (i32.or (i32.shl (i32.ne (local.get $e) (i32.const 1)) (i32.const 2))
                      (i32.shl (i32.eq (local.get $e) (i32.const 255)) (i32.const 4)))
              (i32.shl (i32.ne (local.get $e) (i32.const 255)) (i32.const 3)))))

  ;; i64 operands, an i32 Ordering: a <u b, a >u b, a == b in bits 0-2
  (func $wide (export "wide") (param $a i64) (param $b i64) (result i32)
    (local $t i32)
    (local.set $t (i32.and (i32.sub (i64.gt_u (local.get $a) (local.get $b))
                                    (i64.lt_u (local.get $a) (local.get $b))) (i32.const 255)))
    (i32.or (i32.or (i32.eq (local.get $t) (i32.const 255))
                    (i32.shl (i32.eq (local.get $t) (i32.const 1)) (i32.const 1)))
            (i32.shl (i32.eqz (local.get $t)) (i32.const 2))))

  ;; gt_u less lt_s is no Ordering: -1 against 1 makes it 0 though they
  ;; differ
  (func $mixed (export "mixed") (param $a i32) (param $b i32) (result i32)
    (i32.eqz (i32.and (i32.sub (i32.gt_u (local.get $a) (local.get $b))
                               (i32.lt_s (local.get $a) (local.get $b))) (i32.const 255))))

  ;; masks fold_order_cmp does not read: d & 254 == 254 is a < b, and
  ;; d & 1 == 1 is a != b, where the rule for a mask m would say a > b for
  ;; 1 and never for 254 (bits 0 and 1); eqz(d & 254) is a >= b, not a == b
  ;; (bit 2); and d & 255 is never below zero (bit 3)
  (func $masks (export "masks") (param $a i32) (param $b i32) (result i32)
    (local $d i32)
    (local.set $d (i32.sub (i32.gt_u (local.get $a) (local.get $b))
                           (i32.lt_u (local.get $a) (local.get $b))))
    (i32.or
      (i32.or (i32.eq (i32.and (local.get $d) (i32.const 254)) (i32.const 254))
              (i32.shl (i32.eq (i32.and (local.get $d) (i32.const 1)) (i32.const 1)) (i32.const 1)))
      (i32.or (i32.shl (i32.eqz (i32.and (local.get $d) (i32.const 254))) (i32.const 2))
              (i32.shl (i32.lt_s (i32.and (local.get $d) (i32.const 255)) (i32.const 0)) (i32.const 3)))))

  ;; (a < b) - (b > a) is 0 whatever a and b are: its first compare is no
  ;; a > b spelled the other way round
  (func $degenerate (export "degenerate") (param $a i32) (param $b i32) (result i32)
    (i32.eqz (i32.and (i32.sub (i32.lt_u (local.get $a) (local.get $b))
                               (i32.gt_u (local.get $b) (local.get $a))) (i32.const 255))))

  ;; (a > b) - (a < c) is no Ordering
  (func $others (export "others") (param $a i32) (param $b i32) (param $c i32) (result i32)
    (i32.eq (i32.and (i32.sub (i32.gt_u (local.get $a) (local.get $b))
                              (i32.lt_u (local.get $a) (local.get $c))) (i32.const 255))
            (i32.const 255)))

  ;; the index of x among the six sorted words at 0, or of the first word
  ;; above it with the top bit set
  (func $search (export "search") (param $x i32) (result i32)
    (local $i i32) (local $t i32)
    (block $done (result i32)
      (loop $l
        (local.set $t
          (i32.and (i32.sub (i32.gt_u (local.get $x) (i32.load (i32.shl (local.get $i) (i32.const 2))))
                            (i32.lt_u (local.get $x) (i32.load (i32.shl (local.get $i) (i32.const 2)))))
                   (i32.const 255)))
        (if (i32.eq (local.get $t) (i32.const 1))
          (then
            (local.set $i (i32.add (local.get $i) (i32.const 1)))
            (br_if $l (i32.lt_u (local.get $i) (i32.const 6)))
            (br $done (local.get $i))))
        (br_if $done (local.get $i) (i32.eqz (local.get $t)))
        (br $done (i32.or (local.get $i) (i32.const 0x80000000))))
      (unreachable)))
)
