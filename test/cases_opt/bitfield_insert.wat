;; An or of a bitfield made from x (ubfiz, ubfx) and a value whose bits in
;; the field are zero is x inserted into that value: bfi, or bfxil for a
;; field in the low bits (isel.c's s->bfi) -- one instruction on a chain
;; through x where ubfiz and orr are two, cbench/strings' hash of two bytes.
;; Either way round, 32 and 64 bits, a field at the top, both sides fields,
;; and round a loop, where the result has the carried value's register.  Not
;; when the other value may have a bit in the field, the field is signed (it
;; fills the bits above), the other side is a constant (an orr's immediate),
;; nor into a value folded into the or (an orn's), nor from another block.
;; Answers worked out in Python.
;;
;; shape: ins8 \sbfi\s=1
;; shape: ins8 \sorr\s=0
;; shape: either \sbfi\s=1
;; shape: ins64 \sbfi\s=1
;; shape: xil \sbfxil\s=1
;; shape: twofields \s(bfi|bfxil)\s=1
;; shape: overlap \s(bfi|bfxil)\s=0
;; shape: signed \s(bfi|bfxil)\s=0
;; shape: constor \s(bfi|bfxil)\s=0
;; shape: hash \sbfi\s=5
;; shape: hoisted \s(bfi|bfxil)\s=0
;; shape: notside \sorn\s=1
;; shape: notside \s(bfi|bfxil)\s=0
;;
;; run: ins8 0 0 => i32:0
;; run: ins8 0 165 => i32:165
;; run: ins8 0 305419896 => i32:120
;; run: ins8 0 -1 => i32:255
;; run: ins8 90 0 => i32:23040
;; run: ins8 90 165 => i32:23205
;; run: ins8 90 305419896 => i32:23160
;; run: ins8 90 -1 => i32:23295
;; run: ins8 -559038737 0 => i32:61184
;; run: ins8 -559038737 165 => i32:61349
;; run: ins8 -559038737 305419896 => i32:61304
;; run: ins8 -559038737 -1 => i32:61439
;; run: ins8 -1 0 => i32:65280
;; run: ins8 -1 165 => i32:65445
;; run: ins8 -1 305419896 => i32:65400
;; run: ins8 -1 -1 => i32:65535
;; run: either 0 0 => i32:1
;; run: either 0 165 => i32:164
;; run: either 0 305419896 => i32:305397881
;; run: either 0 -1 => i32:-65282
;; run: either 90 0 => i32:23041
;; run: either 90 165 => i32:23204
;; run: either 90 305419896 => i32:305420921
;; run: either 90 -1 => i32:-42242
;; run: either -559038737 0 => i32:61185
;; run: either -559038737 165 => i32:61348
;; run: either -559038737 305419896 => i32:305459065
;; run: either -559038737 -1 => i32:-4098
;; run: either -1 0 => i32:65281
;; run: either -1 165 => i32:65444
;; run: either -1 305419896 => i32:305463161
;; run: either -1 -1 => i32:-2
;; run: ins64 0 0 => i64:0
;; run: ins64 0 165 => i64:165
;; run: ins64 0 81985529216486895 => i64:52719
;; run: ins64 0 -1 => i64:65535
;; run: ins64 90 0 => i64:5898240
;; run: ins64 90 165 => i64:5898405
;; run: ins64 90 81985529216486895 => i64:5950959
;; run: ins64 90 -1 => i64:5963775
;; run: ins64 -2401053089206439923 0 => i64:4027383808
;; run: ins64 -2401053089206439923 165 => i64:4027383973
;; run: ins64 -2401053089206439923 81985529216486895 => i64:4027436527
;; run: ins64 -2401053089206439923 -1 => i64:4027449343
;; run: ins64 -1 0 => i64:4294901760
;; run: ins64 -1 165 => i64:4294901925
;; run: ins64 -1 81985529216486895 => i64:4294954479
;; run: ins64 -1 -1 => i64:4294967295
;; run: ins64hi 0 0 => i64:0
;; run: ins64hi 0 165 => i64:0
;; run: ins64hi 0 81985529216486895 => i64:4886718345
;; run: ins64hi 0 -1 => i64:1099511627775
;; run: ins64hi 90 0 => i64:98956046499840
;; run: ins64hi 90 165 => i64:98956046499840
;; run: ins64hi 90 81985529216486895 => i64:98960933218185
;; run: ins64hi 90 -1 => i64:100055558127615
;; run: ins64hi -2401053089206439923 0 => i64:-76546900014137344
;; run: ins64hi -2401053089206439923 165 => i64:-76546900014137344
;; run: ins64hi -2401053089206439923 81985529216486895 => i64:-76546895127418999
;; run: ins64hi -2401053089206439923 -1 => i64:-76545800502509569
;; run: ins64hi -1 0 => i64:-1099511627776
;; run: ins64hi -1 165 => i64:-1099511627776
;; run: ins64hi -1 81985529216486895 => i64:-1094624909431
;; run: ins64hi -1 -1 => i64:-1
;; run: xil 0 0 => i32:0
;; run: xil 0 165 => i32:0
;; run: xil 0 305419896 => i32:305419776
;; run: xil 0 -1 => i32:-256
;; run: xil 90 0 => i32:0
;; run: xil 90 165 => i32:0
;; run: xil 90 305419896 => i32:305419776
;; run: xil 90 -1 => i32:-256
;; run: xil -559038737 0 => i32:190
;; run: xil -559038737 165 => i32:190
;; run: xil -559038737 305419896 => i32:305419966
;; run: xil -559038737 -1 => i32:-66
;; run: xil -1 0 => i32:255
;; run: xil -1 165 => i32:255
;; run: xil -1 305419896 => i32:305420031
;; run: xil -1 -1 => i32:-1
;; run: twofields 0 0 => i32:0
;; run: twofields 0 165 => i32:0
;; run: twofields 0 305419896 => i32:52
;; run: twofields 0 -1 => i32:255
;; run: twofields 90 0 => i32:23040
;; run: twofields 90 165 => i32:23040
;; run: twofields 90 305419896 => i32:23092
;; run: twofields 90 -1 => i32:23295
;; run: twofields -559038737 0 => i32:61184
;; run: twofields -559038737 165 => i32:61184
;; run: twofields -559038737 305419896 => i32:61236
;; run: twofields -559038737 -1 => i32:61439
;; run: twofields -1 0 => i32:65280
;; run: twofields -1 165 => i32:65280
;; run: twofields -1 305419896 => i32:65332
;; run: twofields -1 -1 => i32:65535
;; run: overlap 0 0 => i32:0
;; run: overlap 0 165 => i32:165
;; run: overlap 0 305419896 => i32:120
;; run: overlap 0 -1 => i32:511
;; run: overlap 90 0 => i32:23040
;; run: overlap 90 165 => i32:23205
;; run: overlap 90 305419896 => i32:23160
;; run: overlap 90 -1 => i32:23551
;; run: overlap -559038737 0 => i32:61184
;; run: overlap -559038737 165 => i32:61349
;; run: overlap -559038737 305419896 => i32:61304
;; run: overlap -559038737 -1 => i32:61439
;; run: overlap -1 0 => i32:65280
;; run: overlap -1 165 => i32:65445
;; run: overlap -1 305419896 => i32:65400
;; run: overlap -1 -1 => i32:65535
;; run: signed 0 0 => i32:0
;; run: signed 0 165 => i32:0
;; run: signed 0 305419896 => i32:3407872
;; run: signed 0 -1 => i32:16711680
;; run: signed 90 0 => i32:90
;; run: signed 90 165 => i32:90
;; run: signed 90 305419896 => i32:3407962
;; run: signed 90 -1 => i32:16711770
;; run: signed -559038737 0 => i32:-17
;; run: signed -559038737 165 => i32:-17
;; run: signed -559038737 305419896 => i32:-17
;; run: signed -559038737 -1 => i32:-17
;; run: signed -1 0 => i32:-1
;; run: signed -1 165 => i32:-1
;; run: signed -1 305419896 => i32:-1
;; run: signed -1 -1 => i32:-1
;; run: constor 0 0 => i32:18
;; run: constor 0 165 => i32:18
;; run: constor 0 305419896 => i32:18
;; run: constor 0 -1 => i32:18
;; run: constor 90 0 => i32:23058
;; run: constor 90 165 => i32:23058
;; run: constor 90 305419896 => i32:23058
;; run: constor 90 -1 => i32:23058
;; run: constor -559038737 0 => i32:61202
;; run: constor -559038737 165 => i32:61202
;; run: constor -559038737 305419896 => i32:61202
;; run: constor -559038737 -1 => i32:61202
;; run: constor -1 0 => i32:65298
;; run: constor -1 165 => i32:65298
;; run: constor -1 305419896 => i32:65298
;; run: constor -1 -1 => i32:65298
;; run: shifted 0 0 => i32:0
;; run: shifted 0 165 => i32:10813440
;; run: shifted 0 305419896 => i32:1450704896
;; run: shifted 0 -1 => i32:-65536
;; run: shifted 90 0 => i32:23040
;; run: shifted 90 165 => i32:10836480
;; run: shifted 90 305419896 => i32:1450727936
;; run: shifted 90 -1 => i32:-42496
;; run: shifted -559038737 0 => i32:61184
;; run: shifted -559038737 165 => i32:10874624
;; run: shifted -559038737 305419896 => i32:1450766080
;; run: shifted -559038737 -1 => i32:-4352
;; run: shifted -1 0 => i32:65280
;; run: shifted -1 165 => i32:10878720
;; run: shifted -1 305419896 => i32:1450770176
;; run: shifted -1 -1 => i32:-256
;; run: twouse 0 0 => i32:0
;; run: twouse 0 165 => i32:165
;; run: twouse 0 305419896 => i32:120
;; run: twouse 0 -1 => i32:255
;; run: twouse 90 0 => i32:46080
;; run: twouse 90 165 => i32:46245
;; run: twouse 90 305419896 => i32:46200
;; run: twouse 90 -1 => i32:46335
;; run: twouse -559038737 0 => i32:122368
;; run: twouse -559038737 165 => i32:122533
;; run: twouse -559038737 305419896 => i32:122488
;; run: twouse -559038737 -1 => i32:122623
;; run: twouse -1 0 => i32:130560
;; run: twouse -1 165 => i32:130725
;; run: twouse -1 305419896 => i32:130680
;; run: twouse -1 -1 => i32:130815
;; run: crossblock 0 0 => i32:3
;; run: crossblock 0 165 => i32:165
;; run: crossblock 0 305419896 => i32:120
;; run: crossblock 0 -1 => i32:255
;; run: crossblock 90 0 => i32:23043
;; run: crossblock 90 165 => i32:23205
;; run: crossblock 90 305419896 => i32:23160
;; run: crossblock 90 -1 => i32:23295
;; run: crossblock -559038737 0 => i32:61187
;; run: crossblock -559038737 165 => i32:61349
;; run: crossblock -559038737 305419896 => i32:61304
;; run: crossblock -559038737 -1 => i32:61439
;; run: crossblock -1 0 => i32:65283
;; run: crossblock -1 165 => i32:65445
;; run: crossblock -1 305419896 => i32:65400
;; run: crossblock -1 -1 => i32:65535
;; run: crossblock 52 4 => i32:13319
;; run: crossblock 52 9 => i32:13324
;; run: crossblock 52 10 => i32:13322
;; run: hash 0 1 => i32:116
;; run: hash 0 2 => i32:29800
;; run: hash 3 7 => i32:27424
;; run: hash 10 20 => i32:25970
;; run: hash 0 55 => i32:29486
;; run: notside 0 0 => i32:-65281
;; run: notside 0 165 => i32:-65446
;; run: notside 0 305419896 => i32:-305463161
;; run: notside 0 -1 => i32:0
;; run: notside 90 0 => i32:-42241
;; run: notside 90 165 => i32:-42406
;; run: notside 90 305419896 => i32:-305440121
;; run: notside 90 -1 => i32:23040
;; run: notside -559038737 0 => i32:-4097
;; run: notside -559038737 165 => i32:-4262
;; run: notside -559038737 305419896 => i32:-305401977
;; run: notside -559038737 -1 => i32:61184
;; run: notside -1 0 => i32:-1
;; run: notside -1 165 => i32:-166
;; run: notside -1 305419896 => i32:-305397881
;; run: notside -1 -1 => i32:65280
;; run: hoisted 0 0 => i32:116
;; run: hoisted 0 1 => i32:116
;; run: hoisted 0 7 => i32:101
;; run: hoisted 0 300 => i32:116
;; run: hoisted 90 0 => i32:23156
;; run: hoisted 90 1 => i32:23156
;; run: hoisted 90 7 => i32:23141
;; run: hoisted 90 300 => i32:23156
;; run: hoisted -559038737 0 => i32:61300
;; run: hoisted -559038737 1 => i32:61300
;; run: hoisted -559038737 7 => i32:61285
;; run: hoisted -559038737 300 => i32:61300
;; run: hoisted -1 0 => i32:65396
;; run: hoisted -1 1 => i32:65396
;; run: hoisted -1 7 => i32:65381
;; run: hoisted -1 300 => i32:65396

(module
  (memory 1)
  (data (i32.const 0) "the quick brown fox jumps over the lazy dog, then naps.")
  (func $ins8 (export "ins8") (param $h i32) (param $y i32) (result i32)
    (i32.or (i32.and (i32.shl (local.get $h) (i32.const 8)) (i32.const 0xff00))
            (i32.and (local.get $y) (i32.const 0xff))))
  (func $either (export "either") (param $h i32) (param $y i32) (result i32)
    (i32.xor (i32.or (i32.and (local.get $y) (i32.const 0xffff00ff))
            (i32.and (i32.shl (local.get $h) (i32.const 8)) (i32.const 0xff00))) (i32.const 1)))
  (func $ins64 (export "ins64") (param $h i64) (param $y i64) (result i64)
    (i64.or (i64.and (i64.shl (local.get $h) (i64.const 16)) (i64.const 0xffff0000))
            (i64.and (local.get $y) (i64.const 0xffff))))
  (func $ins64hi (export "ins64hi") (param $h i64) (param $y i64) (result i64)
    (i64.or (i64.shl (local.get $h) (i64.const 40))
            (i64.shr_u (local.get $y) (i64.const 24))))
  (func $xil (export "xil") (param $h i32) (param $y i32) (result i32)
    (i32.or (i32.and (i32.shr_u (local.get $h) (i32.const 8)) (i32.const 0xff))
            (i32.and (local.get $y) (i32.const 0xffffff00))))
  (func $twofields (export "twofields") (param $h i32) (param $y i32) (result i32)
    (i32.or (i32.and (i32.shl (local.get $h) (i32.const 8)) (i32.const 0xff00))
            (i32.and (i32.shr_u (local.get $y) (i32.const 16)) (i32.const 0xff))))
  (func $overlap (export "overlap") (param $h i32) (param $y i32) (result i32)
    (i32.or (i32.and (i32.shl (local.get $h) (i32.const 8)) (i32.const 0xff00))
            (i32.and (local.get $y) (i32.const 0x1ff))))
  (func $signed (export "signed") (param $h i32) (param $y i32) (result i32)
    (i32.or (i32.shr_s (i32.shl (local.get $h) (i32.const 24)) (i32.const 24))
            (i32.and (local.get $y) (i32.const 0x00ff0000))))
  (func $constor (export "constor") (param $h i32) (param $y i32) (result i32)
    (i32.or (i32.and (i32.shl (local.get $h) (i32.const 8)) (i32.const 0xff00))
            (i32.const 0x12)))
  (func $shifted (export "shifted") (param $h i32) (param $y i32) (result i32)
    (i32.or (i32.and (i32.shl (local.get $h) (i32.const 8)) (i32.const 0xff00))
            (i32.shl (local.get $y) (i32.const 16))))
  (func $twouse (export "twouse") (param $h i32) (param $y i32) (result i32)
    (i32.add (i32.or (local.tee $h (i32.and (i32.shl (local.get $h) (i32.const 8)) (i32.const 0xff00)))
                     (i32.and (local.get $y) (i32.const 0xff))) (local.get $h)))
  (func $crossblock (export "crossblock") (param $h i32) (param $y i32) (result i32)
    (local $t i32)
    (local.set $t (i32.and (i32.shl (local.get $h) (i32.const 8)) (i32.const 0xff00)))
    (if (i32.lt_u (local.get $y) (i32.const 10)) (then (local.set $y (i32.add (local.get $y) (i32.const 3)))))
    (i32.or (local.get $t) (i32.and (local.get $y) (i32.const 0xff))))
  (func $notside (export "notside") (param $h i32) (param $y i32) (result i32)
    (i32.or (i32.and (i32.shl (local.get $h) (i32.const 8)) (i32.const 0xff00))
            (i32.xor (i32.or (local.get $y) (i32.const 0xff00)) (i32.const -1))))
  (func $hoisted (export "hoisted") (param $h i32) (param $n i32) (result i32)
    (local $t i32) (local $x i32) (local $i i32)
    (local.set $t (i32.and (i32.shl (local.get $h) (i32.const 8)) (i32.const 0xff00)))
    (loop $l
      (if (i32.and (local.get $x) (i32.const 1))
        (then (local.set $i (i32.add (local.get $i) (i32.const 2)))))
      (local.set $x (i32.or (local.get $t)
        (i32.load8_u (i32.and (local.get $x) (i32.const 63)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $x))
  (func $hash (export "hash") (param $p i32) (param $n i32) (result i32)
    (local $h i32) (local $i i32)
    (loop $l
      (local.set $h (i32.or (i32.and (i32.shl (local.get $h) (i32.const 8)) (i32.const 0xff00))
                            (i32.load8_u (i32.add (local.get $p) (local.get $i)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (local.get $h))
)
