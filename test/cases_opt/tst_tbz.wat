;; A branch on one bit of a value -- its and with the bit, or eqz, eq 0 or
;; ne 0 of that -- is tbz or tbnz on arm64 (isel.c find_tbits), the and no
;; instruction and its constant in no register, even one other code holds
;; (shared).  A select or branch on a wider mask is a tst whose flags the
;; csel or b.ne reads (find_tst_conds), not the and's value compared with
;; 0.  An add or sub of a constant whose low 12 bits are 0, below 2^24, is
;; the immediate shifted by 12 (imm_ok_a64); a compare is not, as Apple's
;; cores fuse only the unshifted one with its branch: lt_hi's constant is
;; made in a register.  The near misses: and_used's and is its result too,
;; two_bits tests two bits (tst), odd_mask's no logical immediate encodes
;; (and of a register), sel_used's and is returned as well, tb_eq8
;; compares with 8, not 0, tb_teed's compare is returned too, and
;; sel_shift's and takes a shift in (no tst of it), and and_teed's and
;; is returned too.  The early returns ahead of the prologue add -5, 0x7000
;; and -0x3000 (early_make).  Each branch
;; stores 70 at 0 or 9 at 4 and returns the sum, so that no if-conversion
;; takes it.
;;
;; shape: tb_set \stb(n)?z\s=1
;; shape: tb_set \sand\s=0
;; shape: tb_set \stst\s=0
;; shape: tb_eqz \stb(n)?z\s=1
;; shape: tb_eqz \sand\s=0
;; shape: tb_ne0 \stb(n)?z\s=1
;; shape: tb_ne0 \sand\s=0
;; shape: tb_eq0 \stb(n)?z\s=1
;; shape: tb_eq8 \stb(n)?z\s=0
;; shape: tb_teed \stb(n)?z\s=0
;; shape: sel_shift \stst\s=0
;; shape: and_teed \stb(n)?z\s=0
;; shape: early_hi lsl #12=1
;; shape: early_neg_hi lsl #12=1
;; shape: tb_31 \stb(n)?z\s+w\d+, #0x1f=1
;; shape: tb_63 \stb(n)?z\s+x\d+, #0x3f=1
;; shape: shared \stb(n)?z\s=1
;; shape: and_used \stb(n)?z\s=0
;; shape: two_bits \stb(n)?z\s=0
;; shape: two_bits \stst\s=1
;; shape: br_mask \stst\s=1
;; shape: br_mask \sand\s=0
;; shape: odd_mask \stst\s=0
;; shape: sel_mask \stst\s=1
;; shape: sel_mask \sand\s=0
;; shape: sel_mask \scmp\s=0
;; shape: sel_reg \stst\s=1
;; shape: sel_used \stst\s=0
;; shape: add_hi lsl #12=1
;; shape: sub_hi lsl #12=1
;; shape: add_neg_hi lsl #12=1
;; shape: add_hi64 lsl #12=1
;; shape: lt_hi lsl #12=0
;;
;; run: tb_set 8 => i32:70
;; run: tb_set 7 => i32:9
;; run: tb_set -1 => i32:70
;; run: tb_eqz 8 => i32:9
;; run: tb_eqz 23 => i32:70
;; run: tb_ne0 64 => i32:70
;; run: tb_ne0 63 => i32:9
;; run: tb_eq0 64 => i32:9
;; run: tb_eq0 191 => i32:70
;; run: tb_eq8 8 => i32:70
;; run: tb_eq8 7 => i32:9
;; run: tb_eq8 15 => i32:70
;; run: tb_teed 8 => i32:9
;; run: tb_teed 7 => i32:71
;; run: sel_shift 8 1 5 => i32:1
;; run: sel_shift 7 1 5 => i32:5
;; run: sel_shift 16 2 5 => i32:2
;; run: and_teed 8 => i32:17
;; run: and_teed 7 => i32:70
;; run: early_add 7 => i32:2
;; run: early_add 3 => i32:-2
;; run: early_add 107 => i32:109
;; run: early_hi 7 => i32:28679
;; run: early_hi 107 => i32:28786
;; run: early_neg_hi 7 => i32:-12281
;; run: early_neg_hi 207 => i32:-11967
;; run: tb_31 -2147483648 => i32:70
;; run: tb_31 2147483647 => i32:9
;; run: tb_63 -9223372036854775808 => i32:70
;; run: tb_63 9223372036854775807 => i32:9
;; run: tb_63 4294967296 => i32:9
;; run: shared 8 => i32:78
;; run: shared 1 => i32:17
;; run: and_used 8 => i32:78
;; run: and_used 7 => i32:9
;; run: two_bits 1 => i32:70
;; run: two_bits 2 => i32:70
;; run: two_bits 4 => i32:9
;; run: br_mask 32 => i32:70
;; run: br_mask 15 => i32:9
;; run: odd_mask 8192 => i32:70
;; run: odd_mask 4096 => i32:9
;; run: odd_mask 74565 => i32:70
;; run: odd_mask 2 => i32:9
;; run: sel_mask 16 3 4 => i32:3
;; run: sel_mask 15 3 4 => i32:4
;; run: sel_mask 4096 3 4 => i32:4
;; run: sel_reg 6 3 7 => i32:3
;; run: sel_reg 8 3 7 => i32:7
;; run: sel_used 48 5 6 => i32:53
;; run: sel_used 1 5 6 => i32:6
;; run: add_hi 1 => i32:4194305
;; run: add_hi -4194304 => i32:0
;; run: sub_hi 20481 => i32:1
;; run: sub_hi 0 => i32:-20480
;; run: add_neg_hi 12288 => i32:0
;; run: add_neg_hi 0 => i32:-12288
;; run: add_hi64 1 => i64:8384513
;; run: add_hi64 -8384512 => i64:0
;; run: lt_hi 262143 => i32:1
;; run: lt_hi 262144 => i32:0
;; run: lt_hi -1 => i32:0
;; run: lt_hi 0 => i32:1

(module
  (memory 1)
  (func $set (param $c i32) (result i32)
    (if (local.get $c)
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; bit 3
  (func $tb_set (export "tb_set") (param $x i32) (result i32)
    (if (i32.and (local.get $x) (i32.const 8))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; bit 3 clear
  (func $tb_eqz (export "tb_eqz") (param $x i32) (result i32)
    (if (i32.eqz (i32.and (local.get $x) (i32.const 8)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; bit 6 set, as ne 0
  (func $tb_ne0 (export "tb_ne0") (param $x i32) (result i32)
    (if (i32.ne (i32.and (local.get $x) (i32.const 64)) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; bit 6 clear, as eq 0
  (func $tb_eq0 (export "tb_eq0") (param $x i32) (result i32)
    (if (i32.eq (i32.and (local.get $x) (i32.const 64)) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; (x & 8) == 8: bit 3 set, a compare against 8, no test of a bit
  (func $tb_eq8 (export "tb_eq8") (param $x i32) (result i32)
    (if (i32.eq (i32.and (local.get $x) (i32.const 8)) (i32.const 8))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; the eqz of bit 3 is the branch's and the result's: no tbz
  (func $tb_teed (export "tb_teed") (param $x i32) (result i32)
    (local $t i32)
    (if (local.tee $t (i32.eqz (i32.and (local.get $x) (i32.const 8))))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))) (local.get $t)))
  ;; the eqz of bit 3, whose and is the result's as well: no tbz
  (func $and_teed (export "and_teed") (param $x i32) (result i32)
    (local $t i32)
    (if (i32.eqz (local.tee $t (i32.and (local.get $x) (i32.const 8))))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))) (local.get $t)))
  ;; the sign bit of an i32
  (func $tb_31 (export "tb_31") (param $x i32) (result i32)
    (if (i32.and (local.get $x) (i32.const 0x80000000))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; the sign bit of an i64
  (func $tb_63 (export "tb_63") (param $x i64) (result i32)
    (if (i64.ne (i64.and (local.get $x) (i64.const 0x8000000000000000)) (i64.const 0))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; 8 is also added to the sum: the branch on bit 3 still needs it in no
  ;; register
  (func $shared (export "shared") (param $x i32) (result i32)
    (if (i32.and (local.get $x) (i32.const 8))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))) (i32.const 8)))
  ;; the and is the result as well as the test
  (func $and_used (export "and_used") (param $x i32) (result i32)
    (local $t i32)
    (if (local.tee $t (i32.and (local.get $x) (i32.const 8)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))) (local.get $t)))
  ;; two bits: a tst
  (func $two_bits (export "two_bits") (param $x i32) (result i32)
    (if (i32.and (local.get $x) (i32.const 3))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; a wider mask: a tst
  (func $br_mask (export "br_mask") (param $x i32) (result i32)
    (if (i32.and (local.get $x) (i32.const 0xf0))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; 0x12345 is no logical immediate
  (func $odd_mask (export "odd_mask") (param $x i32) (result i32)
    (if (i32.and (local.get $x) (i32.const 0x12345))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; a select on a mask
  (func $sel_mask (export "sel_mask") (param $x i32) (param $a i32) (param $b i32) (result i32)
    (select (local.get $a) (local.get $b) (i32.and (local.get $x) (i32.const 0xff0))))
  ;; a select on the and of two values
  (func $sel_reg (export "sel_reg") (param $x i32) (param $a i32) (param $b i32) (result i32)
    (select (local.get $a) (local.get $b) (i32.and (local.get $x) (local.get $b))))
  ;; a select on x & (y << 3), whose shift the and takes in: no tst
  (func $sel_shift (export "sel_shift") (param $x i32) (param $y i32) (param $b i32) (result i32)
    (select (local.get $y) (local.get $b) (i32.and (local.get $x) (i32.shl (local.get $y) (i32.const 3)))))
  ;; the and selected on and added
  (func $sel_used (export "sel_used") (param $x i32) (param $a i32) (param $b i32) (result i32)
    (local $t i32)
    (i32.add (select (local.get $a) (local.get $b) (local.tee $t (i32.and (local.get $x) (i32.const 0xf0))))
             (local.get $t)))
  (func $add_hi (export "add_hi") (param $x i32) (result i32)
    (i32.add (local.get $x) (i32.const 0x400000)))
  (func $sub_hi (export "sub_hi") (param $x i32) (result i32)
    (i32.sub (local.get $x) (i32.const 0x5000)))
  (func $add_neg_hi (export "add_neg_hi") (param $x i32) (result i32)
    (i32.add (local.get $x) (i32.const -0x3000)))
  (func $add_hi64 (export "add_hi64") (param $x i64) (result i64)
    (i64.add (local.get $x) (i64.const 0x7ff000)))
  ;; an early return ahead of the prologue (isel.c early_make) of x - 5,
  ;; an add of -5, and of x + 0x7000 and x - 0x3000; it calls itself the
  ;; other way, so it has a prologue to skip
  (func $early_add (export "early_add") (param $x i32) (result i32)
    (if (i32.lt_u (local.get $x) (i32.const 100)) (then (return (i32.add (local.get $x) (i32.const -5)))))
    (i32.add (local.get $x) (call $early_add (i32.sub (local.get $x) (i32.const 100)))))
  (func $early_hi (export "early_hi") (param $x i32) (result i32)
    (if (i32.lt_u (local.get $x) (i32.const 100)) (then (return (i32.add (local.get $x) (i32.const 0x7000)))))
    (i32.add (local.get $x) (call $early_hi (i32.sub (local.get $x) (i32.const 100)))))
  (func $early_neg_hi (export "early_neg_hi") (param $x i32) (result i32)
    (if (i32.lt_u (local.get $x) (i32.const 100)) (then (return (i32.add (local.get $x) (i32.const -0x3000)))))
    (i32.add (local.get $x) (call $early_neg_hi (i32.sub (local.get $x) (i32.const 100)))))
  ;; a compare of 0x40000: its constant in a register
  (func $lt_hi (export "lt_hi") (param $x i32) (result i32)
    (i32.lt_u (local.get $x) (i32.const 0x40000)))
)
