;; pass_reassoc follows an i32 chain down through a wrap into i64 links of
;; the same op, as Go's code has it (adds in i64, wrapped where wasm wants an
;; i32), and rebuilds the whole chain in i32 with its deepest term last.  The
;; low half of each op does not depend on the high halves of its operands,
;; which every export here has set:
;;
;;   * md5's step, rotl(wrap((F + a) + x) + K), F deep;
;;   * the same with xor, and with mul;
;;   * an i64 constant term, which becomes its low half;
;;   * an extended i32 term, which is used unextended;
;;   * a wrap read twice, and an i64 link read twice, where the chain stops.
;;
;; The inputs come from a mutable global so nothing folds them away first.

(module
  (global $seed (mut i64) (i64.const 0x9e37_79b9_7f4a_7c15))

  (func $w (param $k i32) (result i64)
    (i64.mul (i64.add (global.get $seed) (i64.extend_i32_u (local.get $k)))
             (i64.const 0x2545_f491_4f6c_dd1d)))

  ;; Four ops deep from what the loop carries in, all of it i64.
  (func $deep (param $x i64) (result i64)
    (i64.rotl (i64.xor (i64.mul (local.get $x) (i64.const 0xff51_afd7_ed55_8ccd))
                       (i64.shr_u (local.get $x) (i64.const 29)))
              (i64.const 13)))

  (func (export "md5_step") (result i32)
    (local $b i64) (local $a i64) (local $i i32) (local $t i64)
    (local.set $b (i64.const 0xefcd_ab89_1234_5678))
    (local.set $a (i64.const 0x6745_2301_8765_4321))
    (loop $l
      (local.set $t
        (i64.add
          (i64.extend_i32_u
            (i32.rotl
              (i32.add
                (i32.wrap_i64
                  (i64.add (i64.add (i64.xor (i64.and (local.get $b) (i64.const 0x0f0f_0f0f_0f0f_0f0f))
                                             (i64.rotl (local.get $b) (i64.const 7)))
                                    (local.get $a))
                           (call $w (local.get $i))))
                (i32.const 0xd76a_a478))
              (i32.const 7)))
          (local.get $b)))
      (local.set $a (local.get $b))
      (local.set $b (local.get $t))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 64))))
    (i32.xor (i32.wrap_i64 (local.get $b)) (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 3)))))

  (func (export "xor_chain") (result i32)
    (local $s i32) (local $i i32) (local $x i64)
    (loop $l
      (local.set $x (call $w (local.get $i)))
      (local.set $s
        (i32.xor
          (i32.wrap_i64
            (i64.xor (i64.xor (call $deep (i64.extend_i32_u (local.get $s))) (local.get $x))
                     (i64.shl (local.get $x) (i64.const 40))))
          (i32.const 0x5a5a_1234)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 50))))
    (local.get $s))

  (func (export "mul_chain") (result i32)
    (local $s i32) (local $i i32) (local $x i64)
    (local.set $s (i32.const 3))
    (loop $l
      (local.set $x (call $w (local.get $i)))
      (local.set $s
        (i32.mul
          (i32.wrap_i64
            (i64.mul (i64.mul (i64.or (i64.rotl (i64.xor (i64.extend_i32_s (local.get $s))
                                                         (i64.shr_u (local.get $x) (i64.const 17)))
                                                (i64.const 9))
                                      (i64.const 1))
                              (local.get $x))
                     (i64.const 0x1_0000_0003)))
          (i32.const 0x2545_f491)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 50))))
    (local.get $s))

  ;; An i64 constant term with bits above the low half, and an extended i32.
  (func (export "const_and_extend") (result i32)
    (local $s i32) (local $i i32) (local $x i64) (local $y i32)
    (loop $l
      (local.set $x (call $w (local.get $i)))
      (local.set $y (i32.wrap_i64 (i64.shr_u (local.get $x) (i64.const 11))))
      (local.set $s
        (i32.add
          (i32.wrap_i64
            (i64.add (i64.add (i64.add (call $deep (i64.extend_i32_u (local.get $s)))
                                       (i64.const 0x7_0000_0005))
                              (i64.extend_i32_s (local.get $y)))
                     (local.get $x)))
          (i32.const -77)))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 50))))
    (local.get $s))

  ;; The wrap is read twice; then an i64 link is read twice.
  (func (export "shared_links") (result i64)
    (local $s i32) (local $u i32) (local $i i32) (local $x i64) (local $m i64) (local $v i64)
    (loop $l
      (local.set $x (call $w (local.get $i)))
      (local.set $u
        (i32.wrap_i64 (i64.add (i64.add (call $deep (i64.extend_i32_u (local.get $s)))
                                        (local.get $x))
                               (i64.const 0xabcd))))
      (local.set $s (i32.add (i32.add (local.get $u) (i32.const 99)) (i32.const 5)))
      (local.set $m (i64.add (call $deep (i64.extend_i32_u (local.get $u))) (local.get $x)))
      (local.set $v
        (i64.xor (local.get $v)
                 (i64.xor (local.get $m)
                          (i64.extend_i32_u
                            (i32.add (i32.add (i32.wrap_i64 (i64.add (local.get $m) (i64.const 17)))
                                              (i32.const 3))
                                     (local.get $s))))))
      (br_if $l (i32.lt_u (local.tee $i (i32.add (local.get $i) (i32.const 1)))
                          (i32.const 50))))
    (i64.add (local.get $v) (i64.extend_i32_u (local.get $s)))))
