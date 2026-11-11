;; How many of a value's top bits are copies of its sign (ir.c ir_sign_ge):
;; a sign extension of a value that is one already is dropped (opt.c's
;; I_EXT8S, I_EXT16S and I_EXT32S), which every function here ends in.
;; From test/gen_sign_bits.py, which works out every answer; a function
;; whose extension stays has arguments it changes the answer for.
;;
;;   shrs32 shrs31 shrs_of shrs_of15
;;                     a right shift has its amount and one, and what its
;;                     operand had beyond: gone, kept one short, twice
;;   shru33 shru32 andk andk32
;;                     zeros from there up
;;   ork ork31 orneg orneg31
;;                     a constant in range, either sign, and one a bit over
;;   ext8in16 ext16in8 ext32in16 ext16in32 ext8in8 ext16in16 ext32in32
;;                     an extension from fewer bits, from more, and from as
;;                     many
;;   widen widen16 widen15
;;                     a widened i32, and one with copies of its own
;;   ld16s ld16s8 ld32s ld32s16 ld64 ld32u ld16u
;;                     a signed load, a wider one, and unsigned ones
;;   wrap48 wrap47     a wrap of a value with 32 more
;;   shl16 shl17       a left shift takes its amount away
;;   and2 and1 and1b or2 xor1 sel2 sel1 sel1b
;;                     both operands of an and, or, xor or select
;;   add34 add33 add33b sub34 sub33
;;                     both of a sum or difference, with one more each
;;   add_w add_ld xor_ld
;;                     a sum of two widened i32s and of two signed loads,
;;                     each a copy short, and the loads' xor
;;   rewiden33 rewiden32
;;                     an i64 narrowed and widened again: itself with 33
;;   mul49 mul48 mul48b
;;                     both of a product, with half of the copies and the
;;                     width together each
;;   w_*               the same of an i32: shifts, loads, constants, a sum
;;   deep8 deep9       a widened i32 under eight selects, as deep as the
;;                     walk goes, and under nine: gone, kept
;;   loop_ok           a loop's value every edge hands one: gone
;;   loop_grow         one doubled each trip, which no count of copies
;;                     holds for: kept
;;
;; shape: shrs32 \ssxtw\s=0
;; shape: shrs31 \ssxtw\s=1
;; shape: shrs_of \ssxth\s=0
;; shape: shrs_of15 \ssxth\s=1
;; shape: shru33 \ssxtw\s=0
;; shape: shru32 \ssxtw\s=1
;; shape: andk \ssxtw\s=0
;; shape: andk32 \ssxtw\s=1
;; shape: ork \ssxtw\s=0
;; shape: ork31 \ssxtw\s=1
;; shape: orneg \ssxtw\s=0
;; shape: orneg31 \ssxtw\s=1
;; shape: ext8in16 \ssxth\s=0
;; shape: ext16in8 \ssxtb\s=1
;; shape: ext32in16 \ssxth\s=1
;; shape: ext16in32 \ssxtw\s=0
;; shape: ext8in8 \ssxtb\s=1
;; shape: ext16in16 \ssxth\s=1
;; shape: ext32in32 \ssxtw\s=1
;; shape: widen \ssxtw\s=1
;; shape: widen16 \ssxth\s=1
;; shape: widen15 \ssxth\s=1
;; shape: ld16s \ssxth\s=0
;; shape: ld16s8 \ssxtb\s=2
;; shape: ld32s \ssxtw\s=0
;; shape: ld32s16 \ssxth\s=1
;; shape: ld64 \ssxtw\s=1
;; shape: ld32u \ssxtw\s=1
;; shape: ld16u \ssxtw\s=0
;; shape: wrap48 \ssxth\s=0
;; shape: wrap47 \ssxth\s=1
;; shape: shl16 \ssxtw\s=0
;; shape: shl17 \ssxtw\s=1
;; shape: and2 \ssxtw\s=0
;; shape: and1 \ssxtw\s=1
;; shape: and1b \ssxtw\s=1
;; shape: or2 \ssxtw\s=0
;; shape: xor1 \ssxtw\s=1
;; shape: sel2 \ssxtw\s=0
;; shape: sel1 \ssxtw\s=1
;; shape: sel1b \ssxtw\s=1
;; shape: add34 \ssxtw\s=0
;; shape: add33 \ssxtw\s=1
;; shape: add33b \ssxtw\s=1
;; shape: sub34 \ssxtw\s=0
;; shape: sub33 \ssxtw\s=1
;; shape: add_w \ssxtw\s=2
;; shape: add_ld \ssxth\s=1
;; shape: xor_ld \ssxth\s=0
;; shape: mul49 \ssxtw\s=0
;; shape: mul48 \ssxtw\s=1
;; shape: mul48b \ssxtw\s=1
;; shape: rewiden33 \ssxtw\s=0
;; shape: rewiden32 \ssxtw\s=1
;; shape: w_shrs24 \ssxtb\s=0
;; shape: w_shrs23 \ssxtb\s=1
;; shape: w_ld8 \ssxth\s=0
;; shape: w_ld16 \ssxtb\s=1
;; shape: w_k \ssxtb\s=0
;; shape: w_k80 \ssxtb\s=1
;; shape: w_kneg \ssxtb\s=0
;; shape: w_kneg81 \ssxtb\s=1
;; shape: w_add18 \ssxth\s=0
;; shape: w_add17 \ssxth\s=1
;; shape: deep8 \ssxtw\s=1
;; shape: deep9 \ssxtw\s=2
;; shape: loop_ok \ssxtw\s=0
;; shape: loop_grow \ssxtw\s=1
;;
;; run: shrs32 0 128 => i64:0
;; run: shrs32 32768 9223372036854775808 => i64:0
;; run: shrs32 78187493520 18446744073709551615 => i64:18
;; run: shrs32 18364758544493064720 74565 => i64:-19088744
;; run: shrs32 9223372036854775808 9223372036854775807 => i64:-2147483648
;; run: shrs31 9223372036854775807 18446744073709551615 => i64:-1
;; run: shrs31 9223372036854775808 128 => i64:0
;; run: shrs31 9223372036854775807 9223372036854775807 => i64:-1
;; run: shrs31 9223372036854775808 9223372036854775807 => i64:0
;; run: shrs31 0 128 => i64:0
;; run: shrs31 32767 4294967295 => i64:0
;; run: shrs31 2147483648 128 => i64:1
;; run: shrs31 140737488355328 140737488355328 => i64:65536
;; run: shrs31 140737488355328 18446744073709551615 => i64:65536
;; run: shrs_of 0 128 => i64:0
;; run: shrs_of 32768 9223372036854775808 => i64:0
;; run: shrs_of 78187493520 18446744073709551615 => i64:13398
;; run: shrs_of 18364758544493064720 74565 => i64:30292
;; run: shrs_of 9223372036854775808 9223372036854775807 => i64:0
;; run: shrs_of15 2147483647 18446744073709551615 => i64:-1
;; run: shrs_of15 2147483648 128 => i64:0
;; run: shrs_of15 18364758544493064720 4294967295 => i64:-4952
;; run: shrs_of15 18446744071562067967 128 => i64:-1
;; run: shrs_of15 0 128 => i64:0
;; run: shrs_of15 32767 4294967295 => i64:0
;; run: shrs_of15 78187493520 18446744073709551615 => i64:26796
;; run: shrs_of15 9223372036854775808 127 => i64:0
;; run: shrs_of15 9223372036854775808 9223372036854775807 => i64:0
;; run: shru33 0 128 => i64:0
;; run: shru33 32768 9223372036854775808 => i64:0
;; run: shru33 78187493520 18446744073709551615 => i64:9
;; run: shru33 18364758544493064720 74565 => i64:2137939276
;; run: shru33 9223372036854775808 9223372036854775807 => i64:1073741824
;; run: shru32 9223372036854775808 128 => i64:-2147483648
;; run: shru32 18364758544493064720 74565 => i64:-19088744
;; run: shru32 18446744071562067967 128 => i64:-1
;; run: shru32 18446603336221196287 1 => i64:-32769
;; run: shru32 0 128 => i64:0
;; run: shru32 32767 4294967295 => i64:0
;; run: shru32 2147483647 18446744073709551615 => i64:0
;; run: shru32 140737488355327 128 => i64:32767
;; run: shru32 9223372036854775807 9223372036854775807 => i64:2147483647
;; run: andk 0 128 => i64:0
;; run: andk 32768 9223372036854775808 => i64:32768
;; run: andk 78187493520 18446744073709551615 => i64:878082192
;; run: andk 18364758544493064720 74565 => i64:1985229328
;; run: andk 9223372036854775808 9223372036854775807 => i64:0
;; run: andk32 2147483648 128 => i64:-2147483648
;; run: andk32 4294967295 18446603336221196287 => i64:-1
;; run: andk32 9223372036854775807 18446744073709551615 => i64:-1
;; run: andk32 18446744073709551615 140737488355327 => i64:-1
;; run: andk32 9223372036854775807 9223372036854775807 => i64:-1
;; run: andk32 0 128 => i64:0
;; run: andk32 32767 4294967295 => i64:32767
;; run: andk32 2147483647 18446744073709551615 => i64:2147483647
;; run: andk32 18364758544493064720 4294967295 => i64:1985229328
;; run: ork 0 128 => i64:2147418112
;; run: ork 32768 9223372036854775808 => i64:-32768
;; run: ork 78187493520 18446744073709551615 => i64:2147448976
;; run: ork 18364758544493064720 74565 => i64:2147430928
;; run: ork 9223372036854775808 9223372036854775807 => i64:2147418112
;; run: ork31 0 128 => i64:-2147483648
;; run: ork31 128 128 => i64:-2147483520
;; run: ork31 2147483648 128 => i64:-2147483648
;; run: ork31 9223372036854775808 128 => i64:-2147483648
;; run: ork31 9223372036854775808 9223372036854775807 => i64:-2147483648
;; run: ork31 32768 18446744073709551615 => i64:-32768
;; run: ork31 2147483647 2147483648 => i64:-1
;; run: ork31 9223372036854775807 18446744073709551615 => i64:-1
;; run: ork31 18446744071562067967 18446744073709551615 => i64:-1
;; run: orneg 0 128 => i64:-2147483648
;; run: orneg 32768 9223372036854775808 => i64:2147450880
;; run: orneg 78187493520 18446744073709551615 => i64:-2147452784
;; run: orneg 18364758544493064720 74565 => i64:-2147470832
;; run: orneg 9223372036854775808 9223372036854775807 => i64:-2147483648
;; run: orneg31 0 128 => i64:2147483647
;; run: orneg31 32768 9223372036854775808 => i64:-2147450881
;; run: orneg31 78187493520 18446744073709551615 => i64:2147452783
;; run: orneg31 18364758544493064720 74565 => i64:2147470831
;; run: orneg31 9223372036854775808 9223372036854775807 => i64:2147483647
;; run: ext8in16 0 128 => i64:85
;; run: ext8in16 32768 9223372036854775808 => i64:85
;; run: ext8in16 78187493520 18446744073709551615 => i64:-59
;; run: ext8in16 18364758544493064720 74565 => i64:69
;; run: ext8in16 9223372036854775808 9223372036854775807 => i64:85
;; run: ext16in8 128 128 => i64:-43
;; run: ext16in8 32767 4294967295 => i64:-86
;; run: ext16in8 74565 4294967295 => i64:16
;; run: ext16in8 78187493520 128 => i64:-59
;; run: ext16in8 0 128 => i64:85
;; run: ext16in8 2147483647 18446744073709551615 => i64:-86
;; run: ext16in8 140737488355328 4294967295 => i64:85
;; run: ext16in8 18446744071562067967 128 => i64:-86
;; run: ext16in8 9223372036854775808 9223372036854775807 => i64:85
;; run: ext32in16 32768 18446744073709551615 => i64:-32683
;; run: ext32in16 74565 4294967295 => i64:8976
;; run: ext32in16 2147483648 128 => i64:85
;; run: ext32in16 18364758544493064720 4294967295 => i64:12869
;; run: ext32in16 0 128 => i64:85
;; run: ext32in16 128 32768 => i64:213
;; run: ext32in16 140737488355328 4294967295 => i64:85
;; run: ext32in16 18446744073709551615 140737488355327 => i64:-86
;; run: ext32in16 9223372036854775808 9223372036854775807 => i64:85
;; run: ext16in32 0 128 => i64:85
;; run: ext16in32 32768 9223372036854775808 => i64:-32683
;; run: ext16in32 78187493520 18446744073709551615 => i64:30917
;; run: ext16in32 18364758544493064720 74565 => i64:12869
;; run: ext16in32 9223372036854775808 9223372036854775807 => i64:85
;; run: ext8in8 0 128 => i64:21
;; run: ext8in8 32768 9223372036854775808 => i64:21
;; run: ext8in8 78187493520 18446744073709551615 => i64:-123
;; run: ext8in8 18364758544493064720 74565 => i64:5
;; run: ext8in8 9223372036854775808 9223372036854775807 => i64:21
;; run: ext16in16 0 128 => i64:21
;; run: ext16in16 32768 9223372036854775808 => i64:-32747
;; run: ext16in16 78187493520 18446744073709551615 => i64:30853
;; run: ext16in16 18364758544493064720 74565 => i64:12805
;; run: ext16in16 9223372036854775808 9223372036854775807 => i64:21
;; run: ext32in32 0 128 => i64:21
;; run: ext32in32 32768 9223372036854775808 => i64:32789
;; run: ext32in32 78187493520 18446744073709551615 => i64:878082181
;; run: ext32in32 18364758544493064720 74565 => i64:1985229317
;; run: ext32in32 9223372036854775808 9223372036854775807 => i64:21
;; run: widen 0 128 => i64:85
;; run: widen 32768 127 => i64:32853
;; run: widen 2147483647 1 => i64:2147483562
;; run: widen 8388608 0 => i64:8388693
;; run: widen16 0 128 => i64:85
;; run: widen16 32768 127 => i64:-32683
;; run: widen16 2147483647 1 => i64:-86
;; run: widen16 8388608 0 => i64:85
;; run: widen15 2147483647 1 => i64:-86
;; run: widen15 2147483648 8388608 => i64:85
;; run: widen15 2147483647 2147483647 => i64:-86
;; run: widen15 2147483648 2147483647 => i64:85
;; run: widen15 0 128 => i64:85
;; run: widen15 32767 4294967295 => i64:85
;; run: widen15 8388608 32768 => i64:341
;; run: widen15 4286578687 2147483648 => i64:-342
;; run: ld16s 0 128 => i64:6644
;; run: ld16s 32768 9223372036854775808 => i64:6644
;; run: ld16s 78187493520 18446744073709551615 => i64:-6556
;; run: ld16s 18364758544493064720 74565 => i64:6628
;; run: ld16s 9223372036854775808 9223372036854775807 => i64:6644
;; run: ld16s8 0 128 => i64:-12
;; run: ld16s8 32768 9223372036854775808 => i64:-12
;; run: ld16s8 78187493520 18446744073709551615 => i64:100
;; run: ld16s8 18364758544493064720 74565 => i64:-28
;; run: ld16s8 9223372036854775808 9223372036854775807 => i64:-12
;; run: ld32s 0 128 => i64:1665014260
;; run: ld32s 32768 9223372036854775808 => i64:1665014260
;; run: ld32s 78187493520 18446744073709551615 => i64:-1665014172
;; run: ld32s 18364758544493064720 74565 => i64:1665014244
;; run: ld32s 9223372036854775808 9223372036854775807 => i64:1665014260
;; run: ld32s16 0 128 => i64:6644
;; run: ld32s16 32768 9223372036854775808 => i64:6644
;; run: ld32s16 78187493520 18446744073709551615 => i64:-6556
;; run: ld32s16 18364758544493064720 74565 => i64:6628
;; run: ld32s16 9223372036854775808 9223372036854775807 => i64:6644
;; run: ld64 0 128 => i64:1665014260
;; run: ld64 32768 9223372036854775808 => i64:1665014260
;; run: ld64 78187493520 18446744073709551615 => i64:-1665014172
;; run: ld64 18364758544493064720 74565 => i64:1665014244
;; run: ld64 9223372036854775808 9223372036854775807 => i64:1665014260
;; run: ld32u 0 128 => i64:-187716987
;; run: ld32u 32768 9223372036854775808 => i64:-187716987
;; run: ld32u 78187493520 18446744073709551615 => i64:187716885
;; run: ld32u 18364758544493064720 74565 => i64:-187716971
;; run: ld32u 9223372036854775808 9223372036854775807 => i64:-187716987
;; run: ld16u 0 128 => i64:6644
;; run: ld16u 32768 9223372036854775808 => i64:6644
;; run: ld16u 78187493520 18446744073709551615 => i64:-6556
;; run: ld16u 18364758544493064720 74565 => i64:6628
;; run: ld16u 9223372036854775808 9223372036854775807 => i64:6644
;; run: wrap48 0 128 => i32:0
;; run: wrap48 32768 9223372036854775808 => i32:0
;; run: wrap48 78187493520 18446744073709551615 => i32:0
;; run: wrap48 18364758544493064720 74565 => i32:-292
;; run: wrap48 9223372036854775808 9223372036854775807 => i32:-32768
;; run: wrap47 9223372036854775807 18446744073709551615 => i32:-1
;; run: wrap47 9223372036854775808 128 => i32:0
;; run: wrap47 9223372036854775807 9223372036854775807 => i32:-1
;; run: wrap47 9223372036854775808 9223372036854775807 => i32:0
;; run: wrap47 0 128 => i32:0
;; run: wrap47 32767 4294967295 => i32:0
;; run: wrap47 2147483648 128 => i32:0
;; run: wrap47 140737488355328 140737488355328 => i32:1
;; run: wrap47 140737488355328 18446744073709551615 => i32:1
;; run: shl16 0 128 => i64:0
;; run: shl16 32768 9223372036854775808 => i64:-2147483648
;; run: shl16 78187493520 18446744073709551615 => i64:2022703104
;; run: shl16 18364758544493064720 74565 => i64:839909376
;; run: shl16 9223372036854775808 9223372036854775807 => i64:0
;; run: shl17 32767 4294967295 => i64:-131072
;; run: shl17 32767 4294967295 => i64:-131072
;; run: shl17 32768 18446744073709551615 => i64:0
;; run: shl17 32768 9223372036854775808 => i64:0
;; run: shl17 78187493520 18446744073709551615 => i64:-249561088
;; run: shl17 0 128 => i64:0
;; run: shl17 74565 4294967295 => i64:1183449088
;; run: shl17 140737488355328 4294967295 => i64:0
;; run: shl17 18446744071562067967 128 => i64:-131072
;; run: and2 0 128 => i64:0
;; run: and2 32768 9223372036854775808 => i64:-8388608
;; run: and2 78187493520 18446744073709551615 => i64:30864
;; run: and2 18364758544493064720 74565 => i64:0
;; run: and2 9223372036854775808 9223372036854775807 => i64:0
;; run: and1 32768 9223372036854775808 => i64:0
;; run: and1 2147483647 2147483648 => i64:-2147483648
;; run: and1 4294967295 18446603336221196287 => i64:-1
;; run: and1 18446744073709551615 140737488355327 => i64:-1
;; run: and1 9223372036854775807 9223372036854775807 => i64:-1
;; run: and1 0 128 => i64:0
;; run: and1 32767 4294967295 => i64:32767
;; run: and1 78187493520 128 => i64:128
;; run: and1 18364758544493064720 74565 => i64:8704
;; run: and1b 32768 9223372036854775808 => i64:0
;; run: and1b 2147483647 2147483648 => i64:-2147483648
;; run: and1b 4294967295 18446603336221196287 => i64:-1
;; run: and1b 18446744073709551615 140737488355327 => i64:-1
;; run: and1b 9223372036854775807 9223372036854775807 => i64:-1
;; run: and1b 0 128 => i64:0
;; run: and1b 32767 4294967295 => i64:32767
;; run: and1b 78187493520 128 => i64:128
;; run: and1b 18364758544493064720 74565 => i64:8704
;; run: or2 0 128 => i64:0
;; run: or2 32768 9223372036854775808 => i64:-32768
;; run: or2 78187493520 18446744073709551615 => i64:-1
;; run: or2 18364758544493064720 74565 => i64:12816
;; run: or2 9223372036854775808 9223372036854775807 => i64:8388607
;; run: xor1 0 78187493520 => i64:878082192
;; run: xor1 32768 9223372036854775808 => i64:-32768
;; run: xor1 4294967295 4294967295 => i64:0
;; run: xor1 18364758544493064720 4294967295 => i64:-12817
;; run: xor1 9223372036854775808 9223372036854775807 => i64:-1
;; run: xor1 0 128 => i64:128
;; run: xor1 65535 128 => i64:-129
;; run: xor1 140737488355327 128 => i64:-129
;; run: xor1 18446744073709551615 18446744073709551615 => i64:0
;; run: sel2 0 128 => i64:0
;; run: sel2 32768 9223372036854775808 => i64:-32768
;; run: sel2 78187493520 18446744073709551615 => i64:30864
;; run: sel2 18364758544493064720 74565 => i64:0
;; run: sel2 9223372036854775808 9223372036854775807 => i64:8388607
;; run: sel1 0 78187493520 => i64:878082192
;; run: sel1 65535 140737488355328 => i64:0
;; run: sel1 4294967295 4294967295 => i64:-1
;; run: sel1 18446603336221196287 4294967295 => i64:-1
;; run: sel1 0 128 => i64:128
;; run: sel1 32768 9223372036854775808 => i64:-32768
;; run: sel1 78187493520 128 => i64:30864
;; run: sel1 9223372036854775808 127 => i64:127
;; run: sel1 140737488355328 65535 => i64:0
;; run: sel1b 1 18364758544493064720 => i64:1985229328
;; run: sel1b 32768 9223372036854775808 => i64:0
;; run: sel1b 4294967295 18446603336221196287 => i64:-1
;; run: sel1b 140737488355328 4294967295 => i64:-1
;; run: sel1b 9223372036854775807 18446744071562067967 => i64:2147483647
;; run: sel1b 0 128 => i64:0
;; run: sel1b 65535 128 => i64:128
;; run: sel1b 78187493520 128 => i64:128
;; run: sel1b 18446744073709551615 140737488355327 => i64:-1
;; run: add34 0 128 => i64:0
;; run: add34 32768 9223372036854775808 => i64:-1073741824
;; run: add34 78187493520 18446744073709551615 => i64:8
;; run: add34 18364758544493064720 74565 => i64:-9544372
;; run: add34 9223372036854775808 9223372036854775807 => i64:-1
;; run: add33 9223372036854775807 9223372036854775807 => i64:-1073741826
;; run: add33 9223372036854775808 9223372036854775808 => i64:1073741824
;; run: add33 0 128 => i64:0
;; run: add33 32768 9223372036854775808 => i64:-1073741824
;; run: add33 78187493520 18446744073709551615 => i64:17
;; run: add33 18364758544493064720 74565 => i64:-19088744
;; run: add33b 9223372036854775807 9223372036854775807 => i64:-1073741826
;; run: add33b 9223372036854775808 9223372036854775808 => i64:1073741824
;; run: add33b 0 128 => i64:0
;; run: add33b 32768 9223372036854775808 => i64:-2147483648
;; run: add33b 78187493520 18446744073709551615 => i64:8
;; run: add33b 18364758544493064720 74565 => i64:-9544372
;; run: sub34 0 128 => i64:0
;; run: sub34 32768 9223372036854775808 => i64:1073741824
;; run: sub34 78187493520 18446744073709551615 => i64:10
;; run: sub34 18364758544493064720 74565 => i64:-9544372
;; run: sub34 9223372036854775808 9223372036854775807 => i64:-2147483647
;; run: sub33 32768 9223372036854775808 => i64:-2147483648
;; run: sub33 9223372036854775807 9223372036854775808 => i64:-1073741825
;; run: sub33 9223372036854775808 9223372036854775807 => i64:1073741825
;; run: sub33 0 128 => i64:0
;; run: sub33 32768 18446744073709551615 => i64:1
;; run: sub33 4294967295 18446603336221196287 => i64:32769
;; run: sub33 9223372036854775808 127 => i64:-1073741824
;; run: sub33 140737488355328 65535 => i64:16384
;; run: add_w 1 2147483647 => i64:-2147483648
;; run: add_w 2147483647 1 => i64:-2147483648
;; run: add_w 2147483648 4275878552 => i64:2128394904
;; run: add_w 2147483647 2147483647 => i64:-2
;; run: add_w 0 128 => i64:128
;; run: add_w 32767 4294967295 => i64:32766
;; run: add_w 16777216 4275878552 => i64:-2311528
;; run: add_w 8388608 0 => i64:8388608
;; run: add_ld 0 128 => i64:-13953
;; run: add_ld 32768 9223372036854775808 => i64:-13953
;; run: add_ld 78187493520 18446744073709551615 => i64:-13953
;; run: add_ld 18364758544493064720 74565 => i64:-13953
;; run: add_ld 9223372036854775808 9223372036854775807 => i64:-13953
;; run: xor_ld 0 128 => i64:1407
;; run: xor_ld 32768 9223372036854775808 => i64:1407
;; run: xor_ld 78187493520 18446744073709551615 => i64:1407
;; run: xor_ld 18364758544493064720 74565 => i64:1407
;; run: xor_ld 9223372036854775808 9223372036854775807 => i64:1407
;; run: mul49 0 128 => i64:0
;; run: mul49 32768 9223372036854775808 => i64:0
;; run: mul49 78187493520 18446744073709551615 => i64:0
;; run: mul49 18364758544493064720 74565 => i64:0
;; run: mul49 9223372036854775808 9223372036854775807 => i64:-1073709056
;; run: mul48 9223372036854775808 9223372036854775808 => i64:-2147483648
;; run: mul48 0 128 => i64:0
;; run: mul48 32768 9223372036854775808 => i64:0
;; run: mul48 78187493520 18446744073709551615 => i64:0
;; run: mul48 18364758544493064720 74565 => i64:0
;; run: mul48 9223372036854775808 9223372036854775807 => i64:-2147450880
;; run: mul48b 9223372036854775808 9223372036854775808 => i64:-2147483648
;; run: mul48b 0 128 => i64:0
;; run: mul48b 32768 9223372036854775808 => i64:0
;; run: mul48b 78187493520 18446744073709551615 => i64:0
;; run: mul48b 18364758544493064720 74565 => i64:0
;; run: mul48b 9223372036854775808 9223372036854775807 => i64:-2147418112
;; run: rewiden33 0 128 => i64:0
;; run: rewiden33 32768 9223372036854775808 => i64:-2147483648
;; run: rewiden33 78187493520 18446744073709551615 => i64:-19
;; run: rewiden33 18364758544493064720 74565 => i64:-19088744
;; run: rewiden33 9223372036854775808 9223372036854775807 => i64:-1
;; run: rewiden32 32768 9223372036854775808 => i64:0
;; run: rewiden32 2147483648 9223372036854775807 => i64:-2
;; run: rewiden32 9223372036854775807 18446744073709551615 => i64:0
;; run: rewiden32 9223372036854775807 18446744071562067967 => i64:1
;; run: rewiden32 9223372036854775808 128 => i64:0
;; run: rewiden32 0 128 => i64:0
;; run: rewiden32 32768 18446744073709551615 => i64:-1
;; run: rewiden32 78187493520 18446744073709551615 => i64:-37
;; run: rewiden32 18446744071562067967 128 => i64:-2
;; run: w_shrs24 0 128 => i32:0
;; run: w_shrs24 32768 127 => i32:0
;; run: w_shrs24 2147483647 1 => i32:127
;; run: w_shrs24 8388608 0 => i32:0
;; run: w_shrs23 2147483647 1 => i32:-1
;; run: w_shrs23 2147483648 8388608 => i32:0
;; run: w_shrs23 2147483647 2147483647 => i32:-1
;; run: w_shrs23 2147483648 2147483647 => i32:0
;; run: w_shrs23 0 128 => i32:0
;; run: w_shrs23 32767 4294967295 => i32:0
;; run: w_shrs23 8388608 32768 => i32:1
;; run: w_shrs23 4286578687 2147483648 => i32:-2
;; run: w_ld8 0 128 => i32:62
;; run: w_ld8 32768 127 => i32:62
;; run: w_ld8 2147483647 1 => i32:1985
;; run: w_ld8 8388608 0 => i32:54
;; run: w_ld16 0 128 => i32:62
;; run: w_ld16 32768 127 => i32:62
;; run: w_ld16 2147483647 1 => i32:33
;; run: w_ld16 8388608 0 => i32:62
;; run: w_k 0 128 => i32:64
;; run: w_k 32768 127 => i32:64
;; run: w_k 2147483647 1 => i32:127
;; run: w_k 8388608 0 => i32:64
;; run: w_k80 0 128 => i32:-128
;; run: w_k80 32768 127 => i32:-128
;; run: w_k80 2147483647 1 => i32:-65
;; run: w_k80 8388608 0 => i32:-128
;; run: w_kneg 0 128 => i32:-128
;; run: w_kneg 32768 127 => i32:-128
;; run: w_kneg 2147483647 1 => i32:-65
;; run: w_kneg 8388608 0 => i32:-128
;; run: w_kneg81 0 128 => i32:127
;; run: w_kneg81 32768 127 => i32:127
;; run: w_kneg81 2147483647 1 => i32:64
;; run: w_kneg81 8388608 0 => i32:127
;; run: w_add18 0 128 => i32:0
;; run: w_add18 32768 127 => i32:0
;; run: w_add18 2147483647 1 => i32:16383
;; run: w_add18 8388608 0 => i32:64
;; run: w_add17 2147483647 8388607 => i32:-32706
;; run: w_add17 2147483648 4275878552 => i32:32622
;; run: w_add17 2147483647 2147483647 => i32:-16386
;; run: w_add17 2147483648 2147483648 => i32:16384
;; run: w_add17 0 128 => i32:0
;; run: w_add17 32767 4294967295 => i32:-1
;; run: w_add17 16777216 4275878552 => i32:110
;; run: w_add17 4286578687 16777216 => i32:-1
;; run: deep8 2147483648 0 => i64:-2147483648
;; run: deep8 5 3 => i64:3
;; run: deep8 4294967295 98304 => i64:-32768
;; run: deep8 7 20 => i64:20
;; run: deep8 2147483647 18446744073709551615 => i64:2147483647
;; run: deep9 2147483648 0 => i64:-2147483648
;; run: deep9 5 3 => i64:3
;; run: deep9 4294967295 98304 => i64:-32768
;; run: deep9 7 20 => i64:20
;; run: deep9 2147483647 18446744073709551615 => i64:2147483647
;; run: loop_ok 3 0 => i64:3
;; run: loop_grow 3 0 => i64:3
;; run: loop_ok 3 1 => i64:3
;; run: loop_grow 3 1 => i64:6
;; run: loop_ok 3 3 => i64:-1652579
;; run: loop_grow 3 3 => i64:24
;; run: loop_ok 3 15 => i64:97
;; run: loop_grow 3 15 => i64:98304
;; run: loop_ok 3 16 => i64:97
;; run: loop_grow 3 16 => i64:196608
;; run: loop_ok 3 17 => i64:97
;; run: loop_grow 3 17 => i64:393216
;; run: loop_ok 3 40 => i64:-2683108
;; run: loop_grow 3 40 => i64:0
;; run: loop_ok 32767 0 => i64:32767
;; run: loop_grow 32767 0 => i64:32767
;; run: loop_ok 32767 1 => i64:32767
;; run: loop_grow 32767 1 => i64:65534
;; run: loop_ok 32767 3 => i64:-1656991
;; run: loop_grow 32767 3 => i64:262136
;; run: loop_ok 32767 15 => i64:32669
;; run: loop_grow 32767 15 => i64:1073709056
;; run: loop_ok 32767 16 => i64:32669
;; run: loop_grow 32767 16 => i64:2147418112
;; run: loop_ok 32767 17 => i64:32669
;; run: loop_grow 32767 17 => i64:-131072
;; run: loop_ok 32767 40 => i64:-2658080
;; run: loop_grow 32767 40 => i64:0
;; run: loop_ok 32768 0 => i64:-32768
;; run: loop_grow 32768 0 => i64:-32768
;; run: loop_ok 32768 1 => i64:-32768
;; run: loop_grow 32768 1 => i64:-65536
;; run: loop_ok 32768 3 => i64:1656990
;; run: loop_grow 32768 3 => i64:-262144
;; run: loop_ok 32768 15 => i64:-32670
;; run: loop_grow 32768 15 => i64:-1073741824
;; run: loop_ok 32768 16 => i64:-32670
;; run: loop_grow 32768 16 => i64:-2147483648
;; run: loop_ok 32768 17 => i64:-32670
;; run: loop_grow 32768 17 => i64:0
;; run: loop_ok 32768 40 => i64:2658079
;; run: loop_grow 32768 40 => i64:0

(module
  (memory 1)
  (data (i32.const 0) "\85\aa\cf\f4\19\3e\63\88\ad\d2\f7\1c\41\66\8b\b0\d5\fa\1f\44\69\8e\b3\d8\fd\22\47\6c\91\b6\db\00\25\4a\6f\94\b9\de\03\28\4d\72\97\bc\e1\06\2b\50\75\9a\bf\e4\09\2e\53\78\9d\c2\e7\0c\31\56\7b\a0")
  (func $shrs32 (export "shrs32") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.shr_s (local.get $p) (i64.const 32))))
  (func $shrs31 (export "shrs31") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.shr_s (local.get $p) (i64.const 31))))
  (func $shrs_of (export "shrs_of") (param $p i64) (param $q i64) (result i64)
    (i64.extend16_s (i64.shr_s (i64.extend32_s (local.get $p)) (i64.const 16))))
  (func $shrs_of15 (export "shrs_of15") (param $p i64) (param $q i64) (result i64)
    (i64.extend16_s (i64.shr_s (i64.extend32_s (local.get $p)) (i64.const 15))))
  (func $shru33 (export "shru33") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.shr_u (local.get $p) (i64.const 33))))
  (func $shru32 (export "shru32") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.shr_u (local.get $p) (i64.const 32))))
  (func $andk (export "andk") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.and (local.get $p) (i64.const 0x7fffffff))))
  (func $andk32 (export "andk32") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.and (local.get $p) (i64.const 0xffffffff))))
  (func $ork (export "ork") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.or (i64.extend16_s (local.get $p)) (i64.const 0x7fff0000))))
  (func $ork31 (export "ork31") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.or (i64.extend16_s (local.get $p)) (i64.const 0x80000000))))
  (func $orneg (export "orneg") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.xor (i64.extend16_s (local.get $p)) (i64.const -0x80000000))))
  (func $orneg31 (export "orneg31") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.xor (i64.extend16_s (local.get $p)) (i64.const -0x80000001))))
  (func $ext8in16 (export "ext8in16") (param $p i64) (param $q i64) (result i64)
    (i64.extend16_s (i64.xor (i64.extend8_s (local.get $p)) (i64.const 0x55))))
  (func $ext16in8 (export "ext16in8") (param $p i64) (param $q i64) (result i64)
    (i64.extend8_s (i64.xor (i64.extend16_s (local.get $p)) (i64.const 0x55))))
  (func $ext32in16 (export "ext32in16") (param $p i64) (param $q i64) (result i64)
    (i64.extend16_s (i64.xor (i64.extend32_s (local.get $p)) (i64.const 0x55))))
  (func $ext16in32 (export "ext16in32") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.xor (i64.extend16_s (local.get $p)) (i64.const 0x55))))
  (func $ext8in8 (export "ext8in8") (param $p i64) (param $q i64) (result i64)
    (i64.extend8_s (i64.xor (i64.extend8_s (local.get $p)) (i64.const 0x15))))
  (func $ext16in16 (export "ext16in16") (param $p i64) (param $q i64) (result i64)
    (i64.extend16_s (i64.xor (i64.extend16_s (local.get $p)) (i64.const 0x15))))
  (func $ext32in32 (export "ext32in32") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.xor (i64.extend32_s (local.get $p)) (i64.const 0x15))))
  (func $widen (export "widen") (param $a i32) (param $b i32) (result i64)
    (i64.extend32_s (i64.xor (i64.extend_i32_s (local.get $a)) (i64.const 0x55))))
  (func $widen16 (export "widen16") (param $a i32) (param $b i32) (result i64)
    (i64.extend16_s (i64.xor (i64.extend_i32_s (i32.extend16_s (local.get $a))) (i64.const 0x55))))
  (func $widen15 (export "widen15") (param $a i32) (param $b i32) (result i64)
    (i64.extend16_s (i64.xor (i64.extend_i32_s (i32.shr_s (local.get $a) (i32.const 15))) (i64.const 0x55))))
  (func $ld16s (export "ld16s") (param $p i64) (param $q i64) (result i64)
    (i64.extend16_s (i64.xor (i64.load16_s (i32.const 3)) (i64.extend8_s (local.get $p)))))
  (func $ld16s8 (export "ld16s8") (param $p i64) (param $q i64) (result i64)
    (i64.extend8_s (i64.xor (i64.load16_s (i32.const 3)) (i64.extend8_s (local.get $p)))))
  (func $ld32s (export "ld32s") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.xor (i64.load32_s (i32.const 3)) (i64.extend8_s (local.get $p)))))
  (func $ld32s16 (export "ld32s16") (param $p i64) (param $q i64) (result i64)
    (i64.extend16_s (i64.xor (i64.load32_s (i32.const 3)) (i64.extend8_s (local.get $p)))))
  (func $ld64 (export "ld64") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.xor (i64.load (i32.const 3)) (i64.extend8_s (local.get $p)))))
  (func $ld32u (export "ld32u") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.xor (i64.load32_u (i32.const 0)) (i64.extend8_s (local.get $p)))))
  (func $ld16u (export "ld16u") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.xor (i64.load16_u (i32.const 3)) (i64.extend8_s (local.get $p)))))
  (func $wrap48 (export "wrap48") (param $p i64) (param $q i64) (result i32)
    (i32.extend16_s (i32.wrap_i64 (i64.shr_s (local.get $p) (i64.const 48)))))
  (func $wrap47 (export "wrap47") (param $p i64) (param $q i64) (result i32)
    (i32.extend16_s (i32.wrap_i64 (i64.shr_s (local.get $p) (i64.const 47)))))
  (func $shl16 (export "shl16") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.shl (i64.extend16_s (local.get $p)) (i64.const 16))))
  (func $shl17 (export "shl17") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.shl (i64.extend16_s (local.get $p)) (i64.const 17))))
  (func $and2 (export "and2") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.and (i64.extend16_s (local.get $p)) (i64.shr_s (local.get $q) (i64.const 40)))))
  (func $and1 (export "and1") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.and (i64.extend16_s (local.get $p)) (local.get $q))))
  (func $and1b (export "and1b") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.and (local.get $q) (i64.extend16_s (local.get $p)))))
  (func $or2 (export "or2") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.or (i64.extend16_s (local.get $p)) (i64.shr_s (local.get $q) (i64.const 40)))))
  (func $xor1 (export "xor1") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.xor (i64.extend16_s (local.get $p)) (local.get $q))))
  (func $sel2 (export "sel2") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (select (i64.extend16_s (local.get $p)) (i64.shr_s (local.get $q) (i64.const 40)) (i64.gt_s (local.get $p) (local.get $q)))))
  (func $sel1 (export "sel1") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (select (i64.extend16_s (local.get $p)) (local.get $q) (i64.gt_s (local.get $p) (local.get $q)))))
  (func $sel1b (export "sel1b") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (select (local.get $q) (i64.extend16_s (local.get $p)) (i64.gt_s (local.get $p) (local.get $q)))))
  (func $add34 (export "add34") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.add (i64.shr_s (local.get $p) (i64.const 33)) (i64.shr_s (local.get $q) (i64.const 33)))))
  (func $add33 (export "add33") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.add (i64.shr_s (local.get $p) (i64.const 32)) (i64.shr_s (local.get $q) (i64.const 33)))))
  (func $add33b (export "add33b") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.add (i64.shr_s (local.get $p) (i64.const 33)) (i64.shr_s (local.get $q) (i64.const 32)))))
  (func $sub34 (export "sub34") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.sub (i64.shr_s (local.get $p) (i64.const 33)) (i64.shr_s (local.get $q) (i64.const 33)))))
  (func $sub33 (export "sub33") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.sub (i64.shr_s (local.get $p) (i64.const 33)) (i64.shr_s (local.get $q) (i64.const 32)))))
  (func $add_w (export "add_w") (param $a i32) (param $b i32) (result i64)
    (i64.extend32_s (i64.add (i64.extend_i32_s (local.get $a)) (i64.extend_i32_s (local.get $b)))))
  (func $add_ld (export "add_ld") (param $p i64) (param $q i64) (result i64)
    (i64.extend16_s (i64.add (i64.load16_s (i32.const 5)) (i64.load16_s (i32.const 12)))))
  (func $xor_ld (export "xor_ld") (param $p i64) (param $q i64) (result i64)
    (i64.extend16_s (i64.xor (i64.load16_s (i32.const 5)) (i64.load16_s (i32.const 12)))))
  (func $mul49 (export "mul49") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.mul (i64.shr_s (local.get $p) (i64.const 48)) (i64.shr_s (local.get $q) (i64.const 48)))))
  (func $mul48 (export "mul48") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.mul (i64.shr_s (local.get $p) (i64.const 48)) (i64.shr_s (local.get $q) (i64.const 47)))))
  (func $mul48b (export "mul48b") (param $p i64) (param $q i64) (result i64)
    (i64.extend32_s (i64.mul (i64.shr_s (local.get $p) (i64.const 47)) (i64.shr_s (local.get $q) (i64.const 48)))))
  (func $rewiden33 (export "rewiden33") (param $p i64) (param $q i64) (result i64)
    (i64.extend_i32_s (i32.wrap_i64 (i64.shr_s (i64.xor (local.get $p) (local.get $q)) (i64.const 32)))))
  (func $rewiden32 (export "rewiden32") (param $p i64) (param $q i64) (result i64)
    (i64.extend_i32_s (i32.wrap_i64 (i64.shr_s (i64.xor (local.get $p) (local.get $q)) (i64.const 31)))))
  (func $w_shrs24 (export "w_shrs24") (param $a i32) (param $b i32) (result i32)
    (i32.extend8_s (i32.shr_s (local.get $a) (i32.const 24))))
  (func $w_shrs23 (export "w_shrs23") (param $a i32) (param $b i32) (result i32)
    (i32.extend8_s (i32.shr_s (local.get $a) (i32.const 23))))
  (func $w_ld8 (export "w_ld8") (param $a i32) (param $b i32) (result i32)
    (i32.extend16_s (i32.xor (i32.load8_s (i32.const 5)) (i32.shr_s (local.get $a) (i32.const 20)))))
  (func $w_ld16 (export "w_ld16") (param $a i32) (param $b i32) (result i32)
    (i32.extend8_s (i32.xor (i32.load16_s (i32.const 5)) (i32.shr_s (local.get $a) (i32.const 26)))))
  (func $w_k (export "w_k") (param $a i32) (param $b i32) (result i32)
    (i32.extend8_s (i32.xor (i32.shr_s (local.get $a) (i32.const 25)) (i32.const 0x40))))
  (func $w_k80 (export "w_k80") (param $a i32) (param $b i32) (result i32)
    (i32.extend8_s (i32.xor (i32.shr_s (local.get $a) (i32.const 25)) (i32.const 0x80))))
  (func $w_kneg (export "w_kneg") (param $a i32) (param $b i32) (result i32)
    (i32.extend8_s (i32.xor (i32.shr_s (local.get $a) (i32.const 25)) (i32.const -128))))
  (func $w_kneg81 (export "w_kneg81") (param $a i32) (param $b i32) (result i32)
    (i32.extend8_s (i32.xor (i32.shr_s (local.get $a) (i32.const 25)) (i32.const -129))))
  (func $w_add18 (export "w_add18") (param $a i32) (param $b i32) (result i32)
    (i32.extend16_s (i32.add (i32.shr_s (local.get $a) (i32.const 17)) (i32.shr_s (local.get $b) (i32.const 17)))))
  (func $w_add17 (export "w_add17") (param $a i32) (param $b i32) (result i32)
    (i32.extend16_s (i32.add (i32.shr_s (local.get $a) (i32.const 16)) (i32.shr_s (local.get $b) (i32.const 17)))))
  (func $deep8 (export "deep8") (param $a i32) (param $q i64) (result i64)
    (i64.extend32_s (select (i64.extend16_s (local.get $q)) (select (i64.extend16_s (local.get $q)) (select (i64.extend16_s (local.get $q)) (select (i64.extend16_s (local.get $q)) (select (i64.extend16_s (local.get $q)) (select (i64.extend16_s (local.get $q)) (select (i64.extend16_s (local.get $q)) (select (i64.extend16_s (local.get $q)) (i64.extend_i32_s (local.get $a)) (i64.gt_s (local.get $q) (i64.const 0))) (i64.gt_s (local.get $q) (i64.const 1))) (i64.gt_s (local.get $q) (i64.const 2))) (i64.gt_s (local.get $q) (i64.const 3))) (i64.gt_s (local.get $q) (i64.const 4))) (i64.gt_s (local.get $q) (i64.const 5))) (i64.gt_s (local.get $q) (i64.const 6))) (i64.gt_s (local.get $q) (i64.const 7)))))
  (func $deep9 (export "deep9") (param $a i32) (param $q i64) (result i64)
    (i64.extend32_s (select (i64.extend16_s (local.get $q)) (select (i64.extend16_s (local.get $q)) (select (i64.extend16_s (local.get $q)) (select (i64.extend16_s (local.get $q)) (select (i64.extend16_s (local.get $q)) (select (i64.extend16_s (local.get $q)) (select (i64.extend16_s (local.get $q)) (select (i64.extend16_s (local.get $q)) (select (i64.extend16_s (local.get $q)) (i64.extend_i32_s (local.get $a)) (i64.gt_s (local.get $q) (i64.const 0))) (i64.gt_s (local.get $q) (i64.const 1))) (i64.gt_s (local.get $q) (i64.const 2))) (i64.gt_s (local.get $q) (i64.const 3))) (i64.gt_s (local.get $q) (i64.const 4))) (i64.gt_s (local.get $q) (i64.const 5))) (i64.gt_s (local.get $q) (i64.const 6))) (i64.gt_s (local.get $q) (i64.const 7))) (i64.gt_s (local.get $q) (i64.const 8)))))
  (func $loop_ok (export "loop_ok") (param $p i64) (param $n i64) (result i64)
    (local $m i64)
    (local.set $m (i64.extend16_s (local.get $p)))
    (block $done (loop $l
      (br_if $done (i64.eqz (local.get $n)))
      (local.set $m (select (local.get $m)
                            (i64.xor (local.get $m) (i64.shr_s (i64.mul (local.get $n) (i64.const 0x9e3779b97f4a7c15)) (i64.const 40)))
                            (i64.eqz (i64.and (local.get $n) (i64.const 2)))))
      (local.set $n (i64.sub (local.get $n) (i64.const 1)))
      (br $l)))
    (i64.extend32_s (local.get $m)))
  (func $loop_grow (export "loop_grow") (param $p i64) (param $n i64) (result i64)
    (local $m i64)
    (local.set $m (i64.extend16_s (local.get $p)))
    (block $done (loop $l
      (br_if $done (i64.eqz (local.get $n)))
      (local.set $m (i64.add (local.get $m) (local.get $m)))
      (local.set $n (i64.sub (local.get $n) (i64.const 1)))
      (br $l)))
    (i64.extend32_s (local.get $m)))
)
