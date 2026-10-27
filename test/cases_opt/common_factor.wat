;; x*c + y*c is (x + y)*c, and x*c - y*c is (x - y)*c, where each product
;; is read there alone: the distributive law, exact in wrapping arithmetic,
;; an integer's or lane by lane (opt.c fold_common_factor) -- one multiply,
;; either operand order, i32 and i64, and after reassociation has added an
;; unrolled chain's products among themselves (pass_common_factor:
;; zigbench/simdsum's four multiplies by a splat, one).  Not for a product
;; read again, different factors, products in other lanes than the sum's,
;; or floats, whose sum rounds otherwise.
;; And a vector product added into a value, or taken from one, is an mla or
;; an mls into it (isel.c's vmla), but not into one a loop carries round,
;; where it would join the chain each iteration waits on, nor in i64 lanes,
;; which mla has not, other lanes than the sum's, a product read again or
;; made in another block, or one a sub takes the value from, nor into a
;; pairwise widening already an add-accumulate (uadalp).  Answers
;; worked out in Python.
;;
;; stats -O1: common_factors=7
;; stats -O2: common_factors=7
;; stats -O3: common_factors=7
;; shape: s32 \s(mul|madd)\s=1
;; shape: swap32 \s(mul|madd)\s=1
;; shape: sub64 \s(mul|madd|msub)\s=1
;; shape: twouse \s(mul|madd)\s=2
;; shape: twouse \smadd\s=1
;; shape: diff \s(mul|madd)\s=2
;; shape: float \sfmul\s=2
;; shape: vloop \smul\.4s\s=1
;; shape: vloop \smla\.4s\s=0
;; shape: vmla \smla\.4s\s=1
;; shape: vmla \smul\.4s\s=0
;; shape: vmls \smls\.4s\s=1
;; shape: v64 \sml[as]\.=0
;; shape: vsubf \smul\.4s\s=1
;; shape: vmix \smul\.4s\s=2
;; shape: vmulsub \smls\.4s\s=0
;; shape: vmulsub \smul\.4s\s=1
;; shape: vmix2 \sml[as]\.=0
;; shape: vtwo \smla\.4s\s=0
;; shape: vkeep \smla\.4s\s=1
;; shape: vcross \smla\.4s\s=0
;; shape: vadalmul \suadalp\.4s\s=1
;; shape: vadalmul \smla\.4s\s=0
;;
;; run: s32 0 0 0 => i32:0
;; run: swap32 0 0 0 => i32:1
;; run: twouse 0 0 0 => i32:0
;; run: diff 0 0 0 1 => i32:0
;; run: s32 3 4 5 => i32:35
;; run: swap32 3 4 5 => i32:36
;; run: twouse 3 4 5 => i32:44
;; run: diff 3 4 5 6 => i32:39
;; run: s32 -7 9 1000003 => i32:2000006
;; run: swap32 -7 9 1000003 => i32:2000007
;; run: twouse -7 9 1000003 => i32:-7621459
;; run: diff -7 9 1000003 1000004 => i32:2000015
;; run: s32 2147483647 1 3 => i32:-2147483648
;; run: swap32 2147483647 1 3 => i32:-2147483647
;; run: twouse 2147483647 1 3 => i32:-3
;; run: diff 2147483647 1 3 4 => i32:-2147483647
;; run: s32 -1 -1 -1 => i32:2
;; run: swap32 -1 -1 -1 => i32:3
;; run: twouse -1 -1 -1 => i32:3
;; run: diff -1 -1 -1 0 => i32:1
;; run: s32 123456789 987654321 77 => i32:-343790450
;; run: swap32 123456789 987654321 77 => i32:-343790449
;; run: twouse 123456789 987654321 77 => i32:-585202721
;; run: diff 123456789 987654321 77 78 => i32:643863871
;; run: sub64 0 0 0 => i64:0
;; run: sub64 3 4 5 => i64:-5
;; run: sub64 -7 9 1000003 => i64:-16000048
;; run: sub64 9223372036854775807 1 3 => i64:9223372036854775802
;; run: sub64 -1 5 -1 => i64:6
;; run: sub64 1099511627776 8589934592 12345 => i64:13467428302356480
;; run: float 1 2 3 => i64:4601392076421969627
;; run: float 10 7 1 => i64:4605466761799114362
;; run: float -5 11 13 => i64:4615546246679419757
;; run: float 100 -3 9 => i64:4631047475753985171
;; run: float 1 1 1 => i64:4591527048666777112
;; run: vloop 1 => i32:-1240884528
;; run: vloop 2 => i32:1863331744
;; run: vloop 3 => i32:672581488
;; run: vloop 4 => i32:-517972160
;; run: vmla 0 16 32 => i32:605277816
;; run: vmls 0 16 32 => i32:-1601591856
;; run: v64 0 16 32 => i64:-5118056519390922880
;; run: vmla 4 100 200 => i32:966243256
;; run: vmls 4 100 200 => i32:-884735792
;; run: v64 4 100 200 => i64:4152441906562779584
;; run: vmla 64 128 192 => i32:-1808965448
;; run: vmls 64 128 192 => i32:795808912
;; run: v64 64 128 192 => i64:-6341109971821055264
;; run: vmla 8 8 8 => i32:953798296
;; run: vmls 8 8 8 => i32:-905845264
;; run: v64 8 8 8 => i64:-3912355419208740080
;; run: vsubf 0 16 32 => i32:590150592
;; run: vmix 0 16 32 => i64:844947497301613092
;; run: vmulsub 0 16 32 => i32:1601591856
;; run: vmix2 0 16 32 => i32:1468386936
;; run: vtwo 0 16 32 => i32:-1046332196
;; run: vkeep 0 16 32 => i32:107120796
;; run: vsubf 4 100 200 => i32:-1232877440
;; run: vmix 4 100 200 => i64:-754111634821896684
;; run: vmulsub 4 100 200 => i32:884735792
;; run: vmix2 4 100 200 => i32:209957816
;; run: vtwo 4 100 200 => i32:1768647260
;; run: vkeep 4 100 200 => i32:1006996988
;; run: vsubf 64 128 192 => i32:-28527104
;; run: vmix 64 128 192 => i64:1232516684609200324
;; run: vmulsub 64 128 192 => i32:-795808912
;; run: vmix2 64 128 192 => i32:-1220255560
;; run: vtwo 64 128 192 => i32:-622293412
;; run: vkeep 64 128 192 => i32:-168060068
;; run: vsubf 8 8 8 => i32:0
;; run: vmix 8 8 8 => i64:3684916077922645252
;; run: vmulsub 8 8 8 => i32:905845264
;; run: vmix2 8 8 8 => i32:1453510296
;; run: vtwo 8 8 8 => i32:353587804
;; run: vkeep 8 8 8 => i32:977774812
;; run: vcross 0 16 1 => i32:-1550627336
;; run: vcross 0 16 3 => i32:-1418089816
;; run: vcross 0 16 8 => i32:496530112
;; run: vcross 32 48 1 => i32:-813253896
;; run: vcross 32 48 3 => i32:794030504
;; run: vcross 32 48 8 => i32:2100550336
;; run: vadalmul 0 16 32 => i32:1200145372
;; run: vadalmul 4 100 200 => i32:-163814196
;; run: vadalmul 64 128 192 => i32:139911196
;; run: vadalmul 8 8 8 => i32:929823708

(module
  (memory 1)
  (data (i32.const 0) "\0b\30\55\7a\9f\c4\e9\0e\33\58\7d\a2\c7\ec\11\36\5b\80\a5\ca\ef\14\39\5e\83\a8\cd\f2\17\3c\61\86\ab\d0\f5\1a\3f\64\89\ae\d3\f8\1d\42\67\8c\b1\d6\fb\20\45\6a\8f\b4\d9\fe\23\48\6d\92\b7\dc\01\26\4b\70\95\ba\df\04\29\4e\73\98\bd\e2\07\2c\51\76\9b\c0\e5\0a\2f\54\79\9e\c3\e8\0d\32\57\7c\a1\c6\eb\10\35\5a\7f\a4\c9\ee\13\38\5d\82\a7\cc\f1\16\3b\60\85\aa\cf\f4\19\3e\63\88\ad\d2\f7\1c\41\66\8b\b0\d5\fa\1f\44\69\8e\b3\d8\fd\22\47\6c\91\b6\db\00\25\4a\6f\94\b9\de\03\28\4d\72\97\bc\e1\06\2b\50\75\9a\bf\e4\09\2e\53\78\9d\c2\e7\0c\31\56\7b\a0\c5\ea\0f\34\59\7e\a3\c8\ed\12\37\5c\81\a6\cb\f0\15\3a\5f\84\a9\ce\f3\18\3d\62\87\ac\d1\f6\1b\40\65\8a\af\d4\f9\1e\43\68\8d\b2\d7\fc\21\46\6b\90\b5\da\ff\24\49\6e\93\b8\dd\02\27\4c\71\96\bb\e0\05\2a\4f\74\99\be\e3\08\2d\52\77\9c\c1\e6")
  (func $s32 (export "s32") (param $x i32) (param $y i32) (param $c i32) (result i32)
    (i32.add (i32.mul (local.get $x) (local.get $c)) (i32.mul (local.get $y) (local.get $c))))
  (func $swap32 (export "swap32") (param $x i32) (param $y i32) (param $c i32) (result i32)
    (i32.add (i32.add (i32.mul (local.get $c) (local.get $x)) (i32.mul (local.get $y) (local.get $c)))
             (i32.const 1)))
  (func $sub64 (export "sub64") (param $x i64) (param $y i64) (param $c i64) (result i64)
    (i64.sub (i64.mul (local.get $x) (local.get $c)) (i64.mul (local.get $y) (local.get $c))))
  (func $twouse (export "twouse") (param $x i32) (param $y i32) (param $c i32) (result i32)
    (local $p i32)
    (local.set $p (i32.mul (local.get $x) (local.get $c)))
    (i32.xor (i32.add (local.get $p) (i32.mul (local.get $y) (local.get $c))) (local.get $p)))
  (func $diff (export "diff") (param $x i32) (param $y i32) (param $c i32) (param $d i32) (result i32)
    (i32.add (i32.mul (local.get $x) (local.get $c)) (i32.mul (local.get $y) (local.get $d))))
  (func $float (export "float") (param $x i32) (param $y i32) (param $c i32) (result i64)
    (local $cc f64)
    (local.set $cc (f64.div (f64.convert_i32_s (local.get $c)) (f64.const 7)))
    (i64.reinterpret_f64
      (f64.add (f64.mul (f64.div (f64.convert_i32_s (local.get $x)) (f64.const 3)) (local.get $cc))
               (f64.mul (f64.div (f64.convert_i32_s (local.get $y)) (f64.const 3)) (local.get $cc)))))
  (func $vloop (export "vloop") (param $n i32) (result i32)
    (local $i i32) (local $acc v128) (local $k v128) (local $m v128)
    (local.set $k (i32x4.splat (i32.const 3)))
    (local.set $m (i32x4.splat (i32.const 0x5a5a5a5a)))
    (loop $l
      (local.set $acc
        (i32x4.add (i32x4.add (i32x4.add (i32x4.add (local.get $acc)
          (i32x4.mul (v128.xor (v128.load (local.get $i)) (local.get $m)) (local.get $k)))
          (i32x4.mul (v128.xor (v128.load offset=16 (local.get $i)) (local.get $m)) (local.get $k)))
          (i32x4.mul (v128.xor (v128.load offset=32 (local.get $i)) (local.get $m)) (local.get $k)))
          (i32x4.mul (v128.xor (v128.load offset=48 (local.get $i)) (local.get $m)) (local.get $k))))
      (local.set $i (i32.add (local.get $i) (i32.const 64)))
      (br_if $l (i32.lt_u (local.get $i) (i32.mul (local.get $n) (i32.const 64)))))
    (i32.add (i32.add (i32x4.extract_lane 0 (local.get $acc)) (i32x4.extract_lane 1 (local.get $acc)))
             (i32.add (i32x4.extract_lane 2 (local.get $acc)) (i32x4.extract_lane 3 (local.get $acc)))))
  (func $vmla (export "vmla") (param $x i32) (param $y i32) (param $z i32) (result i32)
    (local $v v128)
    (local.set $v (i32x4.add (v128.load (local.get $z))
                             (i32x4.mul (v128.load (local.get $x)) (v128.load (local.get $y)))))
    (i32.add (i32.add (i32x4.extract_lane 0 (local.get $v)) (i32x4.extract_lane 1 (local.get $v)))
             (i32.add (i32x4.extract_lane 2 (local.get $v)) (i32x4.extract_lane 3 (local.get $v)))))
  (func $vmls (export "vmls") (param $x i32) (param $y i32) (param $z i32) (result i32)
    (local $v v128)
    (local.set $v (i32x4.sub (v128.load (local.get $z))
                             (i32x4.mul (v128.load (local.get $x)) (v128.load (local.get $y)))))
    (i32.add (i32.add (i32x4.extract_lane 0 (local.get $v)) (i32x4.extract_lane 1 (local.get $v)))
             (i32.add (i32x4.extract_lane 2 (local.get $v)) (i32x4.extract_lane 3 (local.get $v)))))
  (func $v64 (export "v64") (param $x i32) (param $y i32) (param $z i32) (result i64)
    (local $v v128)
    (local.set $v (i64x2.add (v128.load (local.get $z))
                             (i64x2.mul (v128.load (local.get $x)) (v128.load (local.get $y)))))
    (i64.add (i64x2.extract_lane 0 (local.get $v)) (i64x2.extract_lane 1 (local.get $v))))
  (func $vsubf (export "vsubf") (param $x i32) (param $y i32) (param $z i32) (result i32)
    (local $v v128) (local $c v128)
    (local.set $c (v128.load (local.get $z)))
    (local.set $v (i32x4.sub (i32x4.mul (v128.load (local.get $x)) (local.get $c))
                             (i32x4.mul (v128.load (local.get $y)) (local.get $c))))
    (i32.add (i32.add (i32x4.extract_lane 0 (local.get $v)) (i32x4.extract_lane 1 (local.get $v)))
             (i32.add (i32x4.extract_lane 2 (local.get $v)) (i32x4.extract_lane 3 (local.get $v)))))
  (func $vmix (export "vmix") (param $x i32) (param $y i32) (param $z i32) (result i64)
    (local $v v128) (local $c v128)
    (local.set $c (v128.load (local.get $z)))
    (local.set $v (i64x2.add (i32x4.mul (v128.load (local.get $x)) (local.get $c))
                             (i32x4.mul (v128.load (local.get $y)) (local.get $c))))
    (i64.add (i64x2.extract_lane 0 (local.get $v)) (i64x2.extract_lane 1 (local.get $v))))
  (func $vmulsub (export "vmulsub") (param $x i32) (param $y i32) (param $z i32) (result i32)
    (local $v v128)
    (local.set $v (i32x4.sub (i32x4.mul (v128.load (local.get $x)) (v128.load (local.get $y)))
                             (v128.load (local.get $z))))
    (i32.add (i32.add (i32x4.extract_lane 0 (local.get $v)) (i32x4.extract_lane 1 (local.get $v)))
             (i32.add (i32x4.extract_lane 2 (local.get $v)) (i32x4.extract_lane 3 (local.get $v)))))
  (func $vmix2 (export "vmix2") (param $x i32) (param $y i32) (param $z i32) (result i32)
    (local $v v128)
    (local.set $v (i32x4.add (v128.load (local.get $z))
                             (i16x8.mul (v128.load (local.get $x)) (v128.load (local.get $y)))))
    (i32.add (i32.add (i32x4.extract_lane 0 (local.get $v)) (i32x4.extract_lane 1 (local.get $v)))
             (i32.add (i32x4.extract_lane 2 (local.get $v)) (i32x4.extract_lane 3 (local.get $v)))))
  (func $vtwo (export "vtwo") (param $x i32) (param $y i32) (param $z i32) (result i32)
    (local $v v128) (local $p v128)
    (local.set $p (i32x4.mul (v128.load (local.get $x)) (v128.load (local.get $y))))
    (local.set $v (v128.xor (i32x4.add (v128.load (local.get $z)) (local.get $p)) (local.get $p)))
    (i32.add (i32.add (i32x4.extract_lane 0 (local.get $v)) (i32x4.extract_lane 1 (local.get $v)))
             (i32.add (i32x4.extract_lane 2 (local.get $v)) (i32x4.extract_lane 3 (local.get $v)))))
  (func $vkeep (export "vkeep") (param $x i32) (param $y i32) (param $z i32) (result i32)
    (local $v v128) (local $c v128)
    (local.set $c (v128.load (local.get $z)))
    (local.set $v (i32x4.add (local.get $c)
                             (i32x4.mul (v128.load (local.get $x)) (v128.load (local.get $y)))))
    (i32.add
      (i32.add (i32.add (i32x4.extract_lane 0 (local.get $v)) (i32x4.extract_lane 1 (local.get $v)))
               (i32.add (i32x4.extract_lane 2 (local.get $v)) (i32x4.extract_lane 3 (local.get $v))))
      (i32.add (i32.add (i32x4.extract_lane 0 (local.get $c)) (i32x4.extract_lane 1 (local.get $c)))
               (i32.add (i32x4.extract_lane 2 (local.get $c)) (i32x4.extract_lane 3 (local.get $c))))))
  (func $vcross (export "vcross") (param $x i32) (param $y i32) (param $n i32) (result i32)
    (local $p v128) (local $v v128) (local $i i32) (local $t i32)
    (local.set $p (i32x4.mul (v128.load (local.get $x)) (v128.load (local.get $y))))
    (loop $l
      (local.set $v (i32x4.add (v128.load (local.get $i)) (local.get $p)))
      (local.set $t (i32.add (local.get $t)
        (i32.add (i32.add (i32x4.extract_lane 0 (local.get $v)) (i32x4.extract_lane 1 (local.get $v)))
                 (i32.add (i32x4.extract_lane 2 (local.get $v)) (i32x4.extract_lane 3 (local.get $v))))))
      (if (i32.and (i32x4.extract_lane 0 (local.get $v)) (i32.const 1))
        (then (local.set $t (i32.add (local.get $t) (i32.const 1)))))
      (local.set $i (i32.add (local.get $i) (i32.const 16)))
      (br_if $l (i32.lt_u (local.get $i) (i32.mul (local.get $n) (i32.const 16)))))
    (local.get $t))
  (func $vadalmul (export "vadalmul") (param $x i32) (param $y i32) (param $z i32) (result i32)
    (local $v v128)
    (local.set $v (i32x4.add
      (i32x4.extadd_pairwise_i16x8_u (i16x8.extadd_pairwise_i8x16_u (v128.load (local.get $x))))
      (i32x4.mul (v128.load (local.get $y)) (v128.load (local.get $z)))))
    (i32.add (i32.add (i32x4.extract_lane 0 (local.get $v)) (i32x4.extract_lane 1 (local.get $v)))
             (i32.add (i32x4.extract_lane 2 (local.get $v)) (i32x4.extract_lane 3 (local.get $v)))))
)
