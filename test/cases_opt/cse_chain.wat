;; A chain of twins in one walk of CSE (opt.c pass_cse): an instruction
;; whose operands are twins found before it is a twin itself, its
;; operands taken to the survivors before it is looked up.  LICM takes the
;; loop-invariant hash (wrap (x * K >> 32)) out of each of the loop's three
;; arms and puts the three copies in the preheader, after which CSE runs
;; once: keyed on the operands as they were, it merged the three multiplies
;; and left two of the shifts, which used the multiplies it had dropped
;; (luawasm's permute kept four eqz of four twin ref.tests so, each
;; spilled).  The near miss, differ, hashes with another constant in one
;; arm: two multiplies, and two shifts of them.  Answers worked out in
;; Python.
;;
;; shape: arms \smul\s=1
;; shape: arms \slsr\s=1
;; shape: differ \smul\s=2
;;
;; run: arms 0 5 => i64:0
;; run: arms 1 0 => i64:0
;; run: arms 1 3 => i64:2654435769
;; run: arms 1 4 => i64:5308871538
;; run: arms 123456789 10 => i64:105308467889152
;; run: arms -5 7 => i64:996473700192
;; run: arms 9223372036854775807 33 => i64:-1016749678288513288
;; run: differ 0 5 => i64:0
;; run: differ 1 0 => i64:0
;; run: differ 1 3 => i64:3266489917
;; run: differ 1 4 => i64:5920925686
;; run: differ 123456789 10 => i64:99675374126592
;; run: differ -5 7 => i64:607806984142
;; run: differ 9223372036854775807 33 => i64:7407747611366879081
(module
  (func $arms (export "arms") (param $x i64) (param $n i32) (result i64)
    (local $i i32) (local $acc i64)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (if (i32.and (local.get $i) (i32.const 1))
          (then (local.set $acc (i64.add (local.get $acc)
                  (i64.extend_i32_u (i32.wrap_i64 (i64.shr_u (i64.mul (local.get $x) (i64.const 0x9e3779b97f4a7c15)) (i64.const 32)))))))
          (else
            (if (i32.and (local.get $i) (i32.const 2))
              (then (local.set $acc (i64.xor (local.get $acc)
                      (i64.extend_i32_u (i32.wrap_i64 (i64.shr_u (i64.mul (local.get $x) (i64.const 0x9e3779b97f4a7c15)) (i64.const 32)))))))
              (else (local.set $acc (i64.sub (i64.rotl (local.get $acc) (i64.const 7))
                      (i64.extend_i32_u (i32.wrap_i64 (i64.shr_u (i64.mul (local.get $x) (i64.const 0x9e3779b97f4a7c15)) (i64.const 32))))))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $acc))
  (func $differ (export "differ") (param $x i64) (param $n i32) (result i64)
    (local $i i32) (local $acc i64)
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (if (i32.and (local.get $i) (i32.const 1))
          (then (local.set $acc (i64.add (local.get $acc)
                  (i64.extend_i32_u (i32.wrap_i64 (i64.shr_u (i64.mul (local.get $x) (i64.const 0x9e3779b97f4a7c15)) (i64.const 32)))))))
          (else
            (if (i32.and (local.get $i) (i32.const 2))
              (then (local.set $acc (i64.xor (local.get $acc)
                      (i64.extend_i32_u (i32.wrap_i64 (i64.shr_u (i64.mul (local.get $x) (i64.const 0xc2b2ae3d27d4eb4f)) (i64.const 32)))))))
              (else (local.set $acc (i64.sub (i64.rotl (local.get $acc) (i64.const 7))
                      (i64.extend_i32_u (i32.wrap_i64 (i64.shr_u (i64.mul (local.get $x) (i64.const 0x9e3779b97f4a7c15)) (i64.const 32))))))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $acc))
)
