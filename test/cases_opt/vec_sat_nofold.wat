;; test/cases_opt/vec_sat.wat's saturating product with nothing folded: an
;; and that clears no bit stays, of a halfword shifted up by 20, more than
;; the 16 that put it at the top of its i32 -- no lane's value shifted
;; down, and no saturating product in the lanes (opt.c v_top_lane).  From
;; test/gen_vec_sat.py.
;;
;; flags: -O2 -fno-fold

;;
;; run: mul_sh20 0 1 => i64:697877659462342111
;; run: mul_sh20 9 1 => i64:-4796484018694306519
;; run: mul_sh20 33 1 => i64:-5810554337534998045
;; run: mul_sh20 100 1 => i64:-3536049483652716405
;; run: mul_sh20 0 3735928559 => i64:-1401310947981892218
;; run: mul_sh20 9 3735928559 => i64:657046177528796369
;; run: mul_sh20 33 3735928559 => i64:-8717464277387116551
;; run: mul_sh20 100 3735928559 => i64:-8436688404610907441

(module
  (memory 1)
  ;; A and B, a thousand bytes each, by a generator of the seed; A starts
  ;; with the halfwords at the ends of their range
  (func $fill (param $x i32)
    (local $i i32)
    (loop $l
      (local.set $x (i32.add (i32.mul (local.get $x) (i32.const 1664525)) (i32.const 1013904223)))
      (i32.store8 (i32.add (i32.const 1024) (local.get $i)) (i32.shr_u (local.get $x) (i32.const 13)))
      (i32.store8 (i32.add (i32.const 5120) (local.get $i)) (i32.shr_u (local.get $x) (i32.const 21)))
      (i32.store8 (i32.add (i32.const 9216) (local.get $i)) (i32.const 0x5a))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 1024))))
    (i64.store (i32.const 1024) (i64.const -281472829194241))
    (i64.store (i32.const 1032) (i64.const 4611826762358063105)))
  ;; the three, hashed
  (func $sum (result i64)
    (local $i i32) (local $h i64)
    (loop $l
      (local.set $h (i64.xor (i64.mul (local.get $h) (i64.const 1099511628211))
                             (i64.load8_u (i32.add (i32.const 1024) (local.get $i)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 9216))))
    (local.get $h))
  (func $mul_sh20 (export "mul_sh20") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $p (i64.mul (i64.extend_i32_s (i32.and (i32.shl (i32.load16_s (i32.add (i32.const 1024) (i32.shl (local.get $i) (i32.const 1)))) (i32.const 20)) (i32.const -65536))) (i64.const 70)))
          (local.set $t (i32.shr_s (select (i32.xor (i32.shr_s (i32.wrap_i64 (i64.shr_u (local.get $p) (i64.const 32))) (i32.const 31)) (i32.const 2147418112)) (i32.wrap_i64 (local.get $p)) (i32.ne (i32.wrap_i64 (i64.shr_u (local.get $p) (i64.const 32))) (i32.shr_s (i32.wrap_i64 (local.get $p)) (i32.const 31)))) (i32.const 16)))
          (i32.store16 (i32.add (i32.const 9216) (i32.shl (local.get $i) (i32.const 1))) (local.get $t))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
)
