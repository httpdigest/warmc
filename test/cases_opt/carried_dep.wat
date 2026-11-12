;; A value a loop carries round by a chain of one associative operation is
;; added last (opt.c pass_reassoc_carried), so that an iteration waits on
;; one link -- and after it the terms that read the value themselves, which
;; wait on it as the chain does: h * 33 + c is (c + h) + (h << 5), two adds
;; that each take in an extension or a shift, where (h << 5) + c added
;; first made the shift an instruction of its own.  Each function folds the
;; bytes of a string into h.  From test/gen_carried_dep.py, which works out
;; every answer.
;;
;;   h33      h * 33 + c as LLVM leaves it, (h << 5) + h + c with c extended:
;;           the shift is added after h, which the add takes in
;;   first    the term that reads h is the last the chain adds
;;   two      two terms read h, a shift each way
;;   konly    beside the shift of h a constant alone: h and the constant,
;;           then the shift
;;   kboth    a constant and the byte
;;   xs2      32 bits, xors: h ^ (h << 5) ^ (c >> 1), two shifts for two xors
;;           to take in
;;   mulc     a product: h * (h | 1) * c
;;   plain    no term reads h: h is added last, as it was
;;   alldep   every term reads h: the chain stays as it is
;;   k2       two constants among the terms: one
;;   invar    a term that is a sum made before the loop is one term: it is
;;           not taken apart into the loop
;;   mixed    a term that is an xor of a shift of h and the byte is no sum:
;;           one term, which does not read h itself, and the chain is left
;;   twicep   h is a term twice: left
;;   shared   a sum of two shifts of h that something else reads too is one
;;           term, which does not read h itself
;;   amount   the term reads h as its second operand, the count of a shift
;;   vec      lanes of 16 bits in a vector: the terms after it are added in
;;           those lanes
;;   vec2     two terms of the vector's that do not read it, twice the byte
;;           in every lane and sixteen bytes of the string, whose sum carries
;;           out of a lane's low byte: added in its lanes
;;   vec3     two that do
;;   veclane  a sum in lanes of 16 bits is one term of a sum in lanes of 32
;;
;; stats -O2: reassoc=14
;; loop: h33 :\s+lsl\s=0
;; loop: h33 \sadd\s=3
;; loop: first :\s+lsl\s=0
;; loop: two :\s+ls[lr]\s=0
;; loop: konly :\s+lsl\s=0
;; loop: kboth :\s+lsl\s=0
;; loop: xs2 :\s+ls[lr]\s=0
;; loop: plain add\sx0, x\d+, x0$=1
;; loop: k2 :\s+lsl\s=0
;; loop: k2 \sadd\s=4
;; loop: invar \sadd\s=3
;;
;; run: h33 0 => i64:5748972371582471833
;; run: h33 64 => i64:5863208
;; run: h33 67 => i64:-3425079495697580175
;; run: h33 40 => i64:-8064230799024898342
;; run: h33 66 => i64:5381
;; run: first 0 => i64:-6495121368251868965
;; run: first 64 => i64:10922
;; run: first 67 => i64:5897827067241200691
;; run: first 40 => i64:-8174477366377799908
;; run: first 66 => i64:7
;; run: two 0 => i64:5580727303490802208
;; run: two 64 => i64:111136
;; run: two 67 => i64:-491762220766876169
;; run: two 40 => i64:-7508967211393096579
;; run: two 66 => i64:99
;; run: konly 0 => i64:-546652304438539636
;; run: konly 64 => i64:1327
;; run: konly 67 => i64:-7218815095848716718
;; run: konly 40 => i64:-7289296538709132556
;; run: konly 66 => i64:1
;; run: kboth 0 => i64:1349817139319604546
;; run: kboth 64 => i64:6804
;; run: kboth 67 => i64:-3395896685088164320
;; run: kboth 40 => i64:724222711730611435
;; run: kboth 66 => i64:3
;; run: xs2 0 => i32:-896797466
;; run: xs2 64 => i32:4768821
;; run: xs2 67 => i32:1074889908
;; run: xs2 40 => i32:1355598279
;; run: xs2 66 => i32:4660
;; run: mulc 0 => i32:0
;; run: mulc 64 => i32:74688642
;; run: mulc 67 => i32:0
;; run: mulc 40 => i32:-1744830464
;; run: mulc 66 => i32:3
;; run: plain 0 => i64:47423
;; run: plain 64 => i64:1766
;; run: plain 67 => i64:7031
;; run: plain 40 => i64:13448
;; run: plain 66 => i64:11
;; run: alldep 0 => i64:-7030537643336874096
;; run: alldep 64 => i64:84468
;; run: alldep 67 => i64:2400147294098978230
;; run: alldep 40 => i64:6920825840701933839
;; run: alldep 66 => i64:77
;; run: k2 0 => i64:6924046074607913416
;; run: k2 64 => i64:8199
;; run: k2 67 => i64:-4362999297853899264
;; run: k2 40 => i64:9142892978885747849
;; run: k2 66 => i64:4
;; run: invar 0 3 4 => i64:5153727162643278381
;; run: invar 64 1099511627776 5 => i64:37383395346732
;; run: invar 67 7 7 => i64:4009113882012118180
;; run: mixed 0 => i64:1202503778446955130
;; run: mixed 64 => i64:5417
;; run: mixed 67 => i64:-1895852296119315886
;; run: mixed 40 => i64:7623294042166749243
;; run: mixed 66 => i64:6
;; run: twicep 0 => i64:-874411620165839015
;; run: twicep 64 => i64:12644
;; run: twicep 67 => i64:8111569469379533079
;; run: twicep 40 => i64:6670334972106807129
;; run: twicep 66 => i64:8
;; run: shared 0 => i64:-4685559741540454823
;; run: shared 64 => i64:50264
;; run: shared 67 => i64:924482569517748835
;; run: shared 40 => i64:-7731099935922133264
;; run: shared 66 => i64:9
;; run: amount 0 => i64:5023038846541818317
;; run: amount 64 => i64:6734508720518
;; run: amount 67 => i64:7482097919183344147
;; run: amount 40 => i64:-4843775169505278270
;; run: amount 66 => i64:1
;; run: vec 0 => i32:24668
;; run: vec 64 => i32:448
;; run: vec 67 => i32:20092
;; run: vec 40 => i32:54224
;; run: vec 66 => i32:4
;; run: vec2 0 => i32:362295303
;; run: vec2 64 => i32:-1044353786
;; run: vec2 67 => i32:-1298610240
;; run: vec2 40 => i32:-433059733
;; run: vec2 66 => i32:4
;; run: vec3 0 => i32:37111
;; run: vec3 64 => i32:347
;; run: vec3 67 => i32:22801
;; run: vec3 40 => i32:27179
;; run: vec3 66 => i32:4
;; run: veclane 0 => i32:1355243612
;; run: veclane 64 => i32:448
;; run: veclane 67 => i32:-869839236
;; run: veclane 40 => i32:-40512560
;; run: veclane 66 => i32:4

(module
  (memory 1)
  (data (i32.const 0) "\74\68\65\20\71\75\69\63\6b\20\62\72\6f\77\6e\20\66\6f\78\20\6a\75\6d\70\73\20\6f\76\65\72\20\74\68\65\20\6c\61\7a\79\20\64\6f\67\2c\20\30\31\32\33\34\35\36\37\38\39\20\74\69\6d\65\73\00\00\00\61\62\00\01\02\03\04\05\06\07\08\09\0a\0b\0c\0d\0e\0f\10\11\12\13\14\15\16\17\18\19\1a\1b\1c\1d\1e\1f\20\21\22\23\24\25\26\27\00")
  (func $h33 (export "h33") (param $p i32) (result i64)
    (local $h i64) (local $c i32)
    (local.set $h (i64.const 5381))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $h (i64.add (i64.add (i64.shl (local.get $h) (i64.const 5)) (local.get $h)) (i64.extend_i32_u (local.get $c))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.get $h))
  (func $first (export "first") (param $p i32) (result i64)
    (local $h i64) (local $c i32)
    (local.set $h (i64.const 7))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $h (i64.add (i64.add (i64.extend_i32_u (local.get $c)) (local.get $h)) (i64.shl (local.get $h) (i64.const 5))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.get $h))
  (func $two (export "two") (param $p i32) (result i64)
    (local $h i64) (local $c i32)
    (local.set $h (i64.const 99))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $h (i64.add (i64.add (i64.add (i64.shl (local.get $h) (i64.const 5)) (i64.shr_u (local.get $h) (i64.const 7))) (i64.extend_i32_u (local.get $c))) (local.get $h)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.get $h))
  (func $konly (export "konly") (param $p i32) (result i64)
    (local $h i64) (local $c i32)
    (local.set $h (i64.const 1))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $h (i64.add (i64.add (i64.shl (local.get $h) (i64.const 5)) (i64.const 7)) (local.get $h)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.get $h))
  (func $kboth (export "kboth") (param $p i32) (result i64)
    (local $h i64) (local $c i32)
    (local.set $h (i64.const 3))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $h (i64.add (i64.add (i64.add (i64.shl (local.get $h) (i64.const 5)) (i64.const 7)) (i64.extend_i32_u (local.get $c))) (local.get $h)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.get $h))
  (func $xs2 (export "xs2") (param $p i32) (result i32)
    (local $h i32) (local $c i32)
    (local.set $h (i32.const 4660))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $h (i32.xor (i32.xor (i32.shl (local.get $h) (i32.const 5)) (i32.shr_u (local.get $c) (i32.const 1))) (local.get $h)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.get $h))
  (func $mulc (export "mulc") (param $p i32) (result i32)
    (local $h i32) (local $c i32)
    (local.set $h (i32.const 3))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $h (i32.mul (i32.mul (i32.or (local.get $h) (i32.const 1)) (local.get $c)) (local.get $h)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.get $h))
  (func $plain (export "plain") (param $p i32) (result i64)
    (local $h i64) (local $c i32)
    (local.set $h (i64.const 11))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $h (i64.add (i64.add (local.get $h) (i64.extend_i32_u (local.get $c))) (i64.shl (i64.extend_i32_u (local.get $c)) (i64.const 3))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.get $h))
  (func $alldep (export "alldep") (param $p i32) (result i64)
    (local $h i64) (local $c i32)
    (local.set $h (i64.const 77))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $h (i64.add (i64.add (i64.shl (local.get $h) (i64.const 5)) (local.get $h)) (i64.shr_u (local.get $h) (i64.const 3))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.get $h))
  (func $k2 (export "k2") (param $p i32) (result i64)
    (local $h i64) (local $c i32)
    (local.set $h (i64.const 4))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $h (i64.add (i64.add (i64.add (i64.add (i64.shl (local.get $h) (i64.const 5)) (i64.const 7)) (i64.extend_i32_u (local.get $c))) (i64.const 9)) (local.get $h)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.get $h))
  (func $invar (export "invar") (param $p i32) (param $a i64) (param $b i64) (result i64)
    (local $h i64) (local $c i32) (local $k i64)
    (local.set $h (i64.const 2))
    (local.set $k (i64.add (local.get $a) (local.get $b)))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $h (i64.add (i64.add (i64.shl (local.get $h) (i64.const 5)) (local.get $k)) (local.get $h)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.get $h))
  (func $mixed (export "mixed") (param $p i32) (result i64)
    (local $h i64) (local $c i32)
    (local.set $h (i64.const 6))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $h (i64.add (i64.xor (i64.shl (local.get $h) (i64.const 5)) (i64.extend_i32_u (local.get $c))) (local.get $h)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.get $h))
  (func $twicep (export "twicep") (param $p i32) (result i64)
    (local $h i64) (local $c i32)
    (local.set $h (i64.const 8))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $h (i64.add (i64.add (local.get $h) (i64.extend_i32_u (local.get $c))) (i64.add (i64.shl (local.get $h) (i64.const 5)) (local.get $h))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.get $h))
  (func $shared (export "shared") (param $p i32) (result i64)
    (local $h i64) (local $c i32) (local $t i64) (local $g i64)
    (local.set $h (i64.const 9))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $h (i64.add (i64.add (local.tee $t (i64.add (i64.shl (local.get $h) (i64.const 5)) (i64.shr_u (local.get $h) (i64.const 7)))) (i64.extend_i32_u (local.get $c))) (local.get $h)))
        (local.set $g (i64.xor (local.get $g) (local.get $t)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (i64.add (local.get $h) (i64.mul (local.get $g) (i64.const 3))))
  (func $amount (export "amount") (param $p i32) (result i64)
    (local $h i64) (local $c i32)
    (local.set $h (i64.const 1))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $h (i64.add (i64.add (i64.shl (i64.extend_i32_u (local.get $c)) (local.get $h)) (i64.extend_i32_u (local.get $c))) (local.get $h)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (local.get $h))
  (func $vec (export "vec") (param $p i32) (result i32)
    (local $a v128) (local $c i32)
    (local.set $a (v128.const i32x4 1 2 3 4))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $a (i16x8.add (i16x8.add (i16x8.shl (local.get $a) (i32.const 3)) (i16x8.splat (local.get $c)))
                                 (local.get $a)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (i32.xor (i32.xor (i32x4.extract_lane 0 (local.get $a)) (i32x4.extract_lane 1 (local.get $a)))
             (i32.xor (i32x4.extract_lane 2 (local.get $a)) (i32x4.extract_lane 3 (local.get $a)))))
  (func $vec2 (export "vec2") (param $p i32) (result i32)
    (local $a v128) (local $c i32)
    (local.set $a (v128.const i32x4 1 2 3 4))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $a (i16x8.add (i16x8.add (i16x8.add (i16x8.shl (local.get $a) (i32.const 3))
                                                       (i16x8.splat (i32.shl (local.get $c) (i32.const 1))))
                                            (v128.load align=1 (local.get $p)))
                                 (local.get $a)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (i32.xor (i32.xor (i32x4.extract_lane 0 (local.get $a)) (i32x4.extract_lane 1 (local.get $a)))
             (i32.xor (i32x4.extract_lane 2 (local.get $a)) (i32x4.extract_lane 3 (local.get $a)))))
  (func $vec3 (export "vec3") (param $p i32) (result i32)
    (local $a v128) (local $c i32)
    (local.set $a (v128.const i32x4 1 2 3 4))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $a (i16x8.add (i16x8.add (i16x8.add (i16x8.shl (local.get $a) (i32.const 3)) (i16x8.shr_u (local.get $a) (i32.const 2)))
                                            (i16x8.splat (local.get $c)))
                                 (local.get $a)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (i32.xor (i32.xor (i32x4.extract_lane 0 (local.get $a)) (i32x4.extract_lane 1 (local.get $a)))
             (i32.xor (i32x4.extract_lane 2 (local.get $a)) (i32x4.extract_lane 3 (local.get $a)))))
  (func $veclane (export "veclane") (param $p i32) (result i32)
    (local $a v128) (local $c i32)
    (local.set $a (v128.const i32x4 1 2 3 4))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $p)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $a (i32x4.add (i16x8.add (i16x8.shl (local.get $a) (i32.const 3)) (i16x8.splat (local.get $c)))
                                 (local.get $a)))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $l)))
    (i32.xor (i32.xor (i32x4.extract_lane 0 (local.get $a)) (i32x4.extract_lane 1 (local.get $a)))
             (i32.xor (i32x4.extract_lane 2 (local.get $a)) (i32x4.extract_lane 3 (local.get $a)))))
)
