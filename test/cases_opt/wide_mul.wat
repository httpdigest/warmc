;; A 64-bit multiply of two 32-bit values extended alike is umull or smull,
;; which read w registers and extend them themselves, and a sum or a
;; difference with one is umaddl, smaddl, umsubl or smsubl (isel.c, ahead of
;; the folds into adds; isel_ops.inc's I_MUL and its add of a folded
;; multiply).  An extension only the multiply reads is no instruction; the
;; other operand is another, or a value that is its low half extended the
;; same way already.  From test/gen_wide_mul.py, which works out every
;; answer.
;;
;;   umul smul        both extended unsigned, both signed
;;   ext32 smix       i64.extend32_s, of values whose high halves are other
;;                    things, and one of each signed extension
;;   mixed mixed2     one unsigned and one signed: a mul of two extensions
;;   half half2       one extended and one a 64-bit value: as it was
;;   umul_k umul_p k_umul umul_max
;;                    an unsigned extension and a constant under 2^32, on
;;                    either side
;;   smul_p smul_n n_smul smul_min smul_top
;;                    a signed one and a constant that is an int32
;;   u_wide u_neg     an unsigned one and a constant of 2^32 or more, which a
;;                    negative one is: as it was
;;   s_big s_negbig s_wide s_over s_under u_over
;;                    a signed one and a constant that is no int32, by a
;;                    lot or by a little, and an unsigned one just over 2^32
;;   twice twice_s    one extension is read again after the multiply: it
;;                    stays, the multiply reads its low half, and the other
;;                    is dropped
;;   twice_mix        the same, the two extended differently: as it was
;;   both_twice       both read again: nothing to drop, a mul
;;   ld_u ld_s ld8_u ld8_us ld16_us ld16_s
;;                    one operand a load of 32 bits or fewer, extended as the
;;                    other is, or of fewer unsigned, which is both
;;   ld16_su ld8_su ld32_us ld32_su ld64 ld_ld ld_k
;;                    a load extended the other way, one of 64 bits, and two
;;                    loads or a load and a constant, with nothing to drop
;;   square ssquare   both operands the one extension
;;   square3          the same, read a third time: as it was
;;   and_u shru_u shrs_s ext16_s and_s both_ways sum16
;;                    the other operand any value with zeros above its low
;;                    half, or its sign's copies there: masked, shifted,
;;                    extended from fewer bits, a sum of two such
;;   shru31 shrs31 shrs_u ext16_u and_s32 sum32
;;                    one bit short of that, or extended the other way: as
;;                    it was
;;   umadd smadd umadd_l umsub smsub
;;                    the product added to a value, on either side, or taken
;;                    from one
;;   sub_l            a value taken from the product: umull and a sub
;;   hash shash       the sum with an extension times a constant
;;   sq_add           a square added
;;   far far_s        an extension made in a block before the multiply's:
;;                    dropped as one beside it is
;;   other            one extension made before the loop the multiply is
;;                    in, which each copy of the unrolled loop reads: it
;;                    stays there, and the other, in the loop, is dropped
;;   dot sdot         a hash's loop: two 32-bit states a trip and their product
;;                    into the sum, one umaddl or smsubl a trip (five of
;;                    each: the loop unrolled by four, and the trips left)
;;
;; stats -O2 a64: mull_folds=56
;; shape: umul \sumull\s=1
;; shape: umul \smul\s=0
;; shape: umul \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: smul \ssmull\s=1
;; shape: smul \smul\s=0
;; shape: smul \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: ext32 \ssmull\s=1
;; shape: ext32 \smul\s=0
;; shape: ext32 \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: smix \ssmull\s=1
;; shape: smix \smul\s=0
;; shape: smix \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: mixed \sumull\s=0
;; shape: mixed \ssmull\s=0
;; shape: mixed \smul\s=1
;; shape: mixed2 \sumull\s=0
;; shape: mixed2 \ssmull\s=0
;; shape: mixed2 \smul\s=1
;; shape: half \sumull\s=0
;; shape: half \ssmull\s=0
;; shape: half \smul\s=1
;; shape: half2 \sumull\s=0
;; shape: half2 \ssmull\s=0
;; shape: half2 \smul\s=1
;; shape: umul_k \sumull\s=1
;; shape: umul_k \smul\s=0
;; shape: umul_k \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: umul_p \sumull\s=1
;; shape: umul_p \smul\s=0
;; shape: umul_p \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: k_umul \sumull\s=1
;; shape: k_umul \smul\s=0
;; shape: k_umul \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: umul_max \sumull\s=1
;; shape: umul_max \smul\s=0
;; shape: umul_max \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: smul_p \ssmull\s=1
;; shape: smul_p \smul\s=0
;; shape: smul_p \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: smul_n \ssmull\s=1
;; shape: smul_n \smul\s=0
;; shape: smul_n \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: n_smul \ssmull\s=1
;; shape: n_smul \smul\s=0
;; shape: n_smul \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: smul_min \ssmull\s=1
;; shape: smul_min \smul\s=0
;; shape: smul_min \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: smul_top \ssmull\s=1
;; shape: smul_top \smul\s=0
;; shape: smul_top \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: u_wide \sumull\s=0
;; shape: u_wide \ssmull\s=0
;; shape: u_wide \smul\s=1
;; shape: u_neg \sumull\s=0
;; shape: u_neg \ssmull\s=0
;; shape: u_neg \smul\s=1
;; shape: s_big \sumull\s=0
;; shape: s_big \ssmull\s=0
;; shape: s_big \smul\s=1
;; shape: s_negbig \sumull\s=0
;; shape: s_negbig \ssmull\s=0
;; shape: s_negbig \smul\s=1
;; shape: s_over \sumull\s=0
;; shape: s_over \ssmull\s=0
;; shape: s_over \smul\s=1
;; shape: s_under \sumull\s=0
;; shape: s_under \ssmull\s=0
;; shape: s_under \smul\s=1
;; shape: u_over \sumull\s=0
;; shape: u_over \ssmull\s=0
;; shape: u_over \smul\s=1
;; shape: s_wide \sumull\s=0
;; shape: s_wide \ssmull\s=0
;; shape: s_wide \smul\s=1
;; shape: twice \sumull\s=1
;; shape: twice \smul\s=0
;; shape: twice \s(sxtw\s|mov\s+w\d+, w\d+$)=1
;; shape: twice_s \ssmull\s=1
;; shape: twice_s \smul\s=0
;; shape: twice_s \s(sxtw\s|mov\s+w\d+, w\d+$)=1
;; shape: twice_mix \ssmull\s=0
;; shape: twice_mix \sumull\s=0
;; shape: twice_mix \smul\s=1
;; shape: twice_mix \s(sxtw\s|mov\s+w\d+, w\d+$)=2
;; shape: both_twice \sumull\s=0
;; shape: both_twice \smul\s=1
;; shape: square \sumull\s=1
;; shape: square \smul\s=0
;; shape: square \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: ssquare \ssmull\s=1
;; shape: ssquare \smul\s=0
;; shape: ssquare \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: square3 \sumull\s=0
;; shape: square3 \smul\s=1
;; shape: ld_u \sumull\s=1
;; shape: ld_u \smul\s=0
;; shape: ld_u \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: ld_s \ssmull\s=1
;; shape: ld_s \smul\s=0
;; shape: ld_s \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: ld8_u \sumull\s=1
;; shape: ld8_u \smul\s=0
;; shape: ld8_u \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: ld8_us \ssmull\s=1
;; shape: ld8_us \smul\s=0
;; shape: ld8_us \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: ld16_us \ssmull\s=1
;; shape: ld16_us \smul\s=0
;; shape: ld16_us \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: ld16_s \ssmull\s=1
;; shape: ld16_s \smul\s=0
;; shape: ld16_s \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: ld16_su \ssmull\s=0
;; shape: ld16_su \sumull\s=0
;; shape: ld16_su \smul\s=1
;; shape: ld8_su \ssmull\s=0
;; shape: ld8_su \sumull\s=0
;; shape: ld8_su \smul\s=1
;; shape: ld32_us \ssmull\s=0
;; shape: ld32_us \sumull\s=0
;; shape: ld32_us \smul\s=1
;; shape: ld32_su \ssmull\s=0
;; shape: ld32_su \sumull\s=0
;; shape: ld32_su \smul\s=1
;; shape: ld64 \ssmull\s=0
;; shape: ld64 \sumull\s=0
;; shape: ld64 \smul\s=1
;; shape: ld_ld \ssmull\s=0
;; shape: ld_ld \sumull\s=0
;; shape: ld_ld \smul\s=1
;; shape: ld_k \ssmull\s=0
;; shape: ld_k \sumull\s=0
;; shape: ld_k \smul\s=1
;; shape: and_u \sumull\s=1
;; shape: and_u \smul\s=0
;; shape: and_u \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: shru_u \sumull\s=1
;; shape: shru_u \smul\s=0
;; shape: shru_u \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: shru31 \sumull\s=0
;; shape: shru31 \ssmull\s=0
;; shape: shru31 \smul\s=1
;; shape: shrs_s \ssmull\s=1
;; shape: shrs_s \smul\s=0
;; shape: shrs_s \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: shrs31 \sumull\s=0
;; shape: shrs31 \ssmull\s=0
;; shape: shrs31 \smul\s=1
;; shape: shrs_u \sumull\s=0
;; shape: shrs_u \ssmull\s=0
;; shape: shrs_u \smul\s=1
;; shape: ext16_s \ssmull\s=1
;; shape: ext16_s \smul\s=0
;; shape: ext16_s \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: ext16_u \sumull\s=0
;; shape: ext16_u \ssmull\s=0
;; shape: ext16_u \smul\s=1
;; shape: and_s \ssmull\s=1
;; shape: and_s \sumull\s=0
;; shape: and_s \smul\s=0
;; shape: and_s \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: and_s32 \sumull\s=0
;; shape: and_s32 \ssmull\s=0
;; shape: and_s32 \smul\s=1
;; shape: both_ways \sumull\s=1
;; shape: both_ways \ssmull\s=0
;; shape: both_ways \smul\s=0
;; shape: both_ways \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: sum16 \ssmull\s=1
;; shape: sum16 \smul\s=0
;; shape: sum16 \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: sum32 \sumull\s=0
;; shape: sum32 \ssmull\s=0
;; shape: sum32 \smul\s=1
;; shape: umadd \sumaddl\s=1
;; shape: umadd \sumull\s=0
;; shape: umadd \smul\s=0
;; shape: umadd \sadd\s=0
;; shape: umadd \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: smadd \ssmaddl\s=1
;; shape: smadd \ssmull\s=0
;; shape: smadd \smul\s=0
;; shape: smadd \sadd\s=0
;; shape: smadd \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: umadd_l \sumaddl\s=1
;; shape: umadd_l \sumull\s=0
;; shape: umadd_l \smul\s=0
;; shape: umadd_l \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: umsub \sumsubl\s=1
;; shape: umsub \sumull\s=0
;; shape: umsub \smul\s=0
;; shape: umsub \ssub\s=0
;; shape: umsub \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: smsub \ssmsubl\s=1
;; shape: smsub \ssmull\s=0
;; shape: smsub \smul\s=0
;; shape: smsub \ssub\s=0
;; shape: smsub \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: sub_l \sumull\s=1
;; shape: sub_l \ssub\s=1
;; shape: sub_l \sumsubl\s=0
;; shape: sub_l \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: hash \sumaddl\s=1
;; shape: hash \sumull\s=0
;; shape: hash \smul\s=0
;; shape: hash \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: shash \ssmsubl\s=1
;; shape: shash \ssmull\s=0
;; shape: shash \smul\s=0
;; shape: shash \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: sq_add \ssmaddl\s=1
;; shape: sq_add \ssmull\s=0
;; shape: sq_add \smul\s=0
;; shape: sq_add \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: far \sumaddl\s=1
;; shape: far \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: far_s \ssmsubl\s=1
;; shape: far_s \s(sxtw\s|mov\s+w\d+, w\d+$)=0
;; shape: other \ssmull\s=5
;; shape: other \smul\s+x=0
;; shape: other \ssxtw\s=1
;; shape: dot \sumaddl\s=5
;; shape: dot \sumull\s=0
;; shape: dot \smadd\s+x=0
;; shape: sdot \ssmsubl\s=5
;; shape: sdot \ssmull\s=0
;; shape: sdot \ssxtw\s=0
;;
;; run: umul 4294967295 4294967295 => i64:-8589934591
;; run: umul 2147483648 2147483647 => i64:4611686016279904256
;; run: umul 2147483647 2147483649 => i64:4611686018427387903
;; run: umul 3 4294967294 => i64:12884901882
;; run: umul 123456789 3141592653 => i64:387850941285371217
;; run: umul 0 5 => i64:0
;; run: umul 1 2147483648 => i64:2147483648
;; run: umul 2147483649 2147483649 => i64:4611686022722355201
;; run: smul 4294967295 4294967295 => i64:1
;; run: smul 2147483648 2147483647 => i64:-4611686016279904256
;; run: smul 2147483647 2147483649 => i64:-4611686014132420609
;; run: smul 3 4294967294 => i64:-6
;; run: smul 123456789 3141592653 => i64:-142391929938801327
;; run: smul 0 5 => i64:0
;; run: smul 1 2147483648 => i64:-2147483648
;; run: smul 2147483649 2147483649 => i64:4611686014132420609
;; run: ext32 4294967295 6510615558205997055 => i64:1
;; run: ext32 16045690983244890112 9581378200441716735 => i64:-4611686016279904256
;; run: ext32 2147483647 6510615556058513409 => i64:-4611686014132420609
;; run: ext32 16045690981097406467 9581378202589200382 => i64:-6
;; run: ext32 123456789 6510615557052622413 => i64:-142391929938801327
;; run: ext32 16045690981097406464 9581378198294233093 => i64:0
;; run: ext32 1 6510615556058513408 => i64:-2147483648
;; run: ext32 16045690983244890113 9581378200441716737 => i64:4611686014132420609
;; run: smix 1229782938533634594 4294967295 4294967295 => i64:1
;; run: smix 1229782938533634594 2147483648 16045690983244890111 => i64:-4611686016279904256
;; run: smix 1229782938533634594 2147483647 2147483649 => i64:-4611686014132420609
;; run: smix 1229782938533634594 3 16045690985392373758 => i64:-6
;; run: smix 1229782938533634594 123456789 3141592653 => i64:-142391929938801327
;; run: smix 1229782938533634594 0 16045690981097406469 => i64:0
;; run: smix 1229782938533634594 1 2147483648 => i64:-2147483648
;; run: smix 1229782938533634594 2147483649 16045690983244890113 => i64:4611686014132420609
;; run: mixed 4294967295 4294967295 => i64:-4294967295
;; run: mixed 2147483648 2147483647 => i64:4611686016279904256
;; run: mixed 2147483647 2147483649 => i64:-4611686014132420609
;; run: mixed 3 4294967294 => i64:-6
;; run: mixed 123456789 3141592653 => i64:-142391929938801327
;; run: mixed 0 5 => i64:0
;; run: mixed 1 2147483648 => i64:-2147483648
;; run: mixed 2147483649 2147483649 => i64:-4611686018427387903
;; run: mixed2 4294967295 4294967295 => i64:-4294967295
;; run: mixed2 2147483648 2147483647 => i64:-4611686016279904256
;; run: mixed2 2147483647 2147483649 => i64:4611686018427387903
;; run: mixed2 3 4294967294 => i64:12884901882
;; run: mixed2 123456789 3141592653 => i64:387850941285371217
;; run: mixed2 0 5 => i64:0
;; run: mixed2 1 2147483648 => i64:2147483648
;; run: mixed2 2147483649 2147483649 => i64:-4611686018427387903
;; run: half 4294967295 0 => i64:0
;; run: half 2147483648 4294967295 => i64:9223372034707292160
;; run: half 2147483647 4294967296 => i64:9223372032559808512
;; run: half 3 9223372036854775807 => i64:9223372036854775805
;; run: half 123456789 9223372036854775808 => i64:-9223372036854775808
;; run: half 0 18446744073709551615 => i64:0
;; run: half 1 1311768467463790320 => i64:1311768467463790320
;; run: half 2147483649 2147483647 => i64:4611686018427387903
;; run: half2 0 4294967295 => i64:0
;; run: half2 4294967295 2147483648 => i64:-9223372034707292160
;; run: half2 4294967296 2147483647 => i64:9223372032559808512
;; run: half2 9223372036854775807 3 => i64:9223372036854775805
;; run: half2 9223372036854775808 4294967294 => i64:0
;; run: half2 18446744073709551615 2147483649 => i64:2147483647
;; run: half2 1311768467463790320 1 => i64:1311768467463790320
;; run: half2 2147483647 4294967295 => i64:-2147483647
;; run: half2 2147483648 2147483648 => i64:-4611686018427387904
;; run: half2 4294967294 2147483647 => i64:9223372028264841218
;; run: half2 9223372032559808512 3 => i64:9223372023969873920
;; run: half2 32768 4294967294 => i64:-65536
;; run: half2 65535 2147483649 => i64:-140735340806145
;; run: half2 9223372036854710272 1 => i64:9223372036854710272
;; run: umul_k 0 => i64:0
;; run: umul_k 1 => i64:2654435761
;; run: umul_k 2147483647 => i64:5700357388759500367
;; run: umul_k 2147483648 => i64:5700357391413936128
;; run: umul_k 4294967295 => i64:-7046029293536115121
;; run: umul_k 3141592653 => i64:8339155884618063933
;; run: umul_p 0 => i64:0
;; run: umul_p 1 => i64:1000003
;; run: umul_p 2147483647 => i64:2147490089450941
;; run: umul_p 2147483648 => i64:2147490090450944
;; run: umul_p 4294967295 => i64:4294980179901885
;; run: umul_p 3141592653 => i64:3141602077777959
;; run: k_umul 0 => i64:0
;; run: k_umul 1 => i64:2654435763
;; run: k_umul 2147483647 => i64:5700357393054467661
;; run: k_umul 2147483648 => i64:5700357395708903424
;; run: k_umul 4294967295 => i64:-7046029284946180531
;; run: k_umul 3141592653 => i64:8339155890901249239
;; run: umul_max 0 => i64:0
;; run: umul_max 1 => i64:4294967291
;; run: umul_max 2147483647 => i64:9223372021822390277
;; run: umul_max 2147483648 => i64:9223372026117357568
;; run: umul_max 4294967295 => i64:-25769803771
;; run: umul_max 3141592653 => i64:-4953706387428638593
;; run: smul_p 0 => i64:0
;; run: smul_p 1 => i64:1000003
;; run: smul_p 2147483647 => i64:2147490089450941
;; run: smul_p 2147483648 => i64:-2147490090450944
;; run: smul_p 4294967295 => i64:-1000003
;; run: smul_p 3141592653 => i64:-1153378103123929
;; run: smul_n 0 => i64:0
;; run: smul_n 1 => i64:-1000003
;; run: smul_n 2147483647 => i64:-2147490089450941
;; run: smul_n 2147483648 => i64:2147490090450944
;; run: smul_n 4294967295 => i64:1000003
;; run: smul_n 3141592653 => i64:1153378103123929
;; run: n_smul 0 => i64:0
;; run: n_smul 1 => i64:-1000005
;; run: n_smul 2147483647 => i64:-2147494384418235
;; run: n_smul 2147483648 => i64:2147494385418240
;; run: n_smul 4294967295 => i64:1000005
;; run: n_smul 3141592653 => i64:1153380409873215
;; run: smul_min 0 => i64:0
;; run: smul_min 1 => i64:-2147483643
;; run: smul_min 2147483647 => i64:-4611686005542486021
;; run: smul_min 2147483648 => i64:4611686007689969664
;; run: smul_min 4294967295 => i64:2147483643
;; run: smul_min 3141592653 => i64:2476853180093464449
;; run: smul_top 0 => i64:0
;; run: smul_top 1 => i64:2147483643
;; run: smul_top 2147483647 => i64:4611686005542486021
;; run: smul_top 2147483648 => i64:-4611686007689969664
;; run: smul_top 4294967295 => i64:-2147483643
;; run: smul_top 3141592653 => i64:-2476853180093464449
;; run: u_wide 0 => i64:0
;; run: u_wide 1 => i64:4294967301
;; run: u_wide 2147483647 => i64:-9223372030412324869
;; run: u_wide 2147483648 => i64:-9223372026117357568
;; run: u_wide 4294967295 => i64:17179869179
;; run: u_wide 3141592653 => i64:-4953706356012712063
;; run: u_neg 0 => i64:0
;; run: u_neg 1 => i64:-1000003
;; run: u_neg 2147483647 => i64:-2147490089450941
;; run: u_neg 2147483648 => i64:-2147490090450944
;; run: u_neg 4294967295 => i64:-4294980179901885
;; run: u_neg 3141592653 => i64:-3141602077777959
;; run: s_big 0 => i64:0
;; run: s_big 1 => i64:3000000019
;; run: s_big 2147483647 => i64:6442450981802189293
;; run: s_big 2147483648 => i64:-6442450984802189312
;; run: s_big 4294967295 => i64:-3000000019
;; run: s_big 3141592653 => i64:-3460123950914118217
;; run: s_negbig 0 => i64:0
;; run: s_negbig 1 => i64:-3000000019
;; run: s_negbig 2147483647 => i64:-6442450981802189293
;; run: s_negbig 2147483648 => i64:6442450984802189312
;; run: s_negbig 4294967295 => i64:3000000019
;; run: s_negbig 3141592653 => i64:3460123950914118217
;; run: s_over 0 => i64:0
;; run: s_over 1 => i64:2147483653
;; run: s_over 2147483647 => i64:4611686027017322491
;; run: s_over 2147483648 => i64:-4611686029164806144
;; run: s_over 4294967295 => i64:-2147483653
;; run: s_over 3141592653 => i64:-2476853191627210879
;; run: s_under 0 => i64:0
;; run: s_under 1 => i64:-2147483653
;; run: s_under 2147483647 => i64:-4611686027017322491
;; run: s_under 2147483648 => i64:4611686029164806144
;; run: s_under 4294967295 => i64:2147483653
;; run: s_under 3141592653 => i64:2476853191627210879
;; run: u_over 0 => i64:0
;; run: u_over 1 => i64:4294967307
;; run: u_over 2147483647 => i64:-9223372017527422987
;; run: u_over 2147483648 => i64:-9223372013232455680
;; run: u_over 4294967295 => i64:42949672949
;; run: u_over 3141592653 => i64:-4953706337163156145
;; run: s_wide 0 => i64:0
;; run: s_wide 1 => i64:4294967301
;; run: s_wide 2147483647 => i64:-9223372030412324869
;; run: s_wide 2147483648 => i64:9223372026117357568
;; run: s_wide 4294967295 => i64:-4294967301
;; run: s_wide 3141592653 => i64:-4953706377487548543
;; run: twice 4294967295 4294967295 => i64:-4294967298
;; run: twice 2147483648 2147483647 => i64:4611686014132420608
;; run: twice 2147483647 2147483649 => i64:4611686016279904256
;; run: twice 3 4294967294 => i64:12884901881
;; run: twice 123456789 3141592653 => i64:387850941237706820
;; run: twice 0 5 => i64:0
;; run: twice 1 2147483648 => i64:2147483649
;; run: twice 2147483649 2147483649 => i64:4611686024869838848
;; run: twice_s 4294967295 4294967295 => i64:-2
;; run: twice_s 2147483648 2147483647 => i64:-4611686014132420609
;; run: twice_s 2147483647 2147483649 => i64:4611686016279904254
;; run: twice_s 3 4294967294 => i64:4
;; run: twice_s 123456789 3141592653 => i64:142391931070933788
;; run: twice_s 0 5 => i64:5
;; run: twice_s 1 2147483648 => i64:0
;; run: twice_s 2147483649 2147483649 => i64:-4611686016279904256
;; run: twice_mix 4294967295 4294967295 => i64:4294967294
;; run: twice_mix 2147483648 2147483647 => i64:4611686018427387903
;; run: twice_mix 2147483647 2147483649 => i64:4611686016279904254
;; run: twice_mix 3 4294967294 => i64:4
;; run: twice_mix 123456789 3141592653 => i64:142391931070933788
;; run: twice_mix 0 5 => i64:5
;; run: twice_mix 1 2147483648 => i64:0
;; run: twice_mix 2147483649 2147483649 => i64:4611686016279904256
;; run: both_twice 4294967295 4294967295 => i64:-8589934591
;; run: both_twice 2147483648 2147483647 => i64:4611686016279904257
;; run: both_twice 2147483647 2147483649 => i64:-4611686018427387903
;; run: both_twice 3 4294967294 => i64:-8589934593
;; run: both_twice 123456789 3141592653 => i64:-387850944269292647
;; run: both_twice 0 5 => i64:-5
;; run: both_twice 1 2147483648 => i64:-4294967295
;; run: both_twice 2147483649 2147483649 => i64:4611686022722355201
;; run: square 0 => i64:0
;; run: square 1 => i64:1
;; run: square 2147483647 => i64:4611686014132420609
;; run: square 2147483648 => i64:4611686018427387904
;; run: square 4294967295 => i64:-8589934591
;; run: square 3141592653 => i64:-8577139676325973207
;; run: ssquare 0 => i64:0
;; run: ssquare 1 => i64:1
;; run: ssquare 2147483647 => i64:4611686014132420609
;; run: ssquare 2147483648 => i64:4611686018427387904
;; run: ssquare 4294967295 => i64:1
;; run: ssquare 3141592653 => i64:1330273067115377449
;; run: square3 0 => i64:0
;; run: square3 1 => i64:0
;; run: square3 2147483647 => i64:4611686016279904254
;; run: square3 2147483648 => i64:4611686020574871552
;; run: square3 4294967295 => i64:-4294967298
;; run: square3 3141592653 => i64:-8577139674895706780
;; run: ld_u 0 4294967295 => i64:-806238324175907461
;; run: ld_u 1 2147483648 => i64:935174037158494208
;; run: ld_u 2 2147483647 => i64:2237438436290464561
;; run: ld_u 3 3 => i64:4995042780
;; run: ld_u 5 4294967294 => i64:-5942390596199040636
;; run: ld_u 8 2147483649 => i64:1043684296876348077
;; run: ld_u 13 1 => i64:3585117030
;; run: ld_u 21 123456789 => i64:525782389042540710
;; run: ld_s 0 4294967295 => i64:187716987
;; run: ld_s 1 2147483648 => i64:-935174037158494208
;; run: ld_s 2 2147483647 => i64:2237438436290464561
;; run: ld_s 3 3 => i64:4995042780
;; run: ld_s 5 4294967294 => i64:2767141252
;; run: ld_s 8 2147483649 => i64:-1043684295904341331
;; run: ld_s 13 1 => i64:-709850266
;; run: ld_s 21 123456789 => i64:-4460482181631834
;; run: ld8_u 0 4294967295 => i64:571230650235
;; run: ld8_u 1 2147483648 => i64:365072220160
;; run: ld8_u 2 2147483647 => i64:444529114929
;; run: ld8_u 3 3 => i64:732
;; run: ld8_u 5 4294967294 => i64:266287972228
;; run: ld8_u 8 2147483649 => i64:371514671277
;; run: ld8_u 13 1 => i64:102
;; run: ld8_u 21 123456789 => i64:17530864038
;; run: ld8_us 0 4294967295 => i64:-133
;; run: ld8_us 1 2147483648 => i64:-365072220160
;; run: ld8_us 2 2147483647 => i64:444529114929
;; run: ld8_us 3 3 => i64:732
;; run: ld8_us 5 4294967294 => i64:-124
;; run: ld8_us 8 2147483649 => i64:-371514670931
;; run: ld8_us 13 1 => i64:102
;; run: ld8_us 21 123456789 => i64:17530864038
;; run: ld16_us 0 4294967295 => i64:-43653
;; run: ld16_us 1 2147483648 => i64:-114164525694976
;; run: ld16_us 2 2147483647 => i64:134584947641137
;; run: ld16_us 3 3 => i64:19932
;; run: ld16_us 5 4294967294 => i64:-50812
;; run: ld16_us 8 2147483649 => i64:-115820235533651
;; run: ld16_us 13 1 => i64:35686
;; run: ld16_us 21 123456789 => i64:5674814763174
;; run: ld16_s 0 4294967295 => i64:21883
;; run: ld16_s 1 2147483648 => i64:26572962660352
;; run: ld16_s 2 2147483647 => i64:-6152540648655
;; run: ld16_s 3 3 => i64:19932
;; run: ld16_s 5 4294967294 => i64:-50812
;; run: ld16_s 8 2147483649 => i64:24917252756141
;; run: ld16_s 13 1 => i64:-29850
;; run: ld16_s 21 123456789 => i64:-2416049360730
;; run: ld16_su 0 4294967295 => i64:-93986769316485
;; run: ld16_su 1 2147483648 => i64:-26572962660352
;; run: ld16_su 2 2147483647 => i64:-6152540648655
;; run: ld16_su 3 3 => i64:19932
;; run: ld16_su 5 4294967294 => i64:109117939071364
;; run: ld16_su 8 2147483649 => i64:-24917252779347
;; run: ld16_su 13 1 => i64:-29850
;; run: ld16_su 21 123456789 => i64:-2416049360730
;; run: ld8_su 0 4294967295 => i64:-528280977285
;; run: ld8_su 1 2147483648 => i64:-184683593728
;; run: ld8_su 2 2147483647 => i64:-105226698703
;; run: ld8_su 3 3 => i64:-36
;; run: ld8_su 5 4294967294 => i64:266287972228
;; run: ld8_su 8 2147483649 => i64:-178241142867
;; run: ld8_su 13 1 => i64:102
;; run: ld8_su 21 123456789 => i64:-14074073946
;; run: ld32_us 0 4294967295 => i64:-4107250309
;; run: ld32_us 1 2147483648 => i64:-935174037158494208
;; run: ld32_us 2 2147483647 => i64:2237438436290464561
;; run: ld32_us 3 3 => i64:4995042780
;; run: ld32_us 5 4294967294 => i64:-5822793340
;; run: ld32_us 8 2147483649 => i64:-1043684295904341331
;; run: ld32_us 13 1 => i64:3585117030
;; run: ld32_us 21 123456789 => i64:525782389042540710
;; run: ld32_su 0 4294967295 => i64:-806238319880940165
;; run: ld32_su 1 2147483648 => i64:935174037158494208
;; run: ld32_su 2 2147483647 => i64:2237438436290464561
;; run: ld32_su 3 3 => i64:4995042780
;; run: ld32_su 5 4294967294 => i64:-5942390587609106044
;; run: ld32_su 8 2147483649 => i64:1043684296876348077
;; run: ld32_su 13 1 => i64:-709850266
;; run: ld32_su 21 123456789 => i64:-4460482181631834
;; run: ld64 0 4294967295 => i64:7812738660585985403
;; run: ld64 1 2147483648 => i64:935174037158494208
;; run: ld64 2 2147483647 => i64:-3720129404573709519
;; run: ld64 3 3 => i64:-1767653399819825700
;; run: ld64 5 4294967294 => i64:3120558691253696900
;; run: ld64 8 2147483649 => i64:4541686261818577581
;; run: ld64 13 1 => i64:7585222834586159974
;; run: ld64 21 123456789 => i64:-2217455373721702234
;; run: ld_ld 0 0 => i64:-1577238972928956135
;; run: ld_ld 1 4 => i64:996454722643784602
;; run: ld_ld 2 7 => i64:4331942321878200568
;; run: ld_ld 3 9 => i64:1818890698723407400
;; run: ld_ld 5 16 => i64:3327570683581898390
;; run: ld_ld 8 31 => i64:907431532261867776
;; run: ld_ld 13 40 => i64:-7103283914972112466
;; run: ld_ld 21 52 => i64:8597388261384246270
;; run: ld_k 0 => i64:-7544311974121651467
;; run: ld_k 9 => i64:2899752051397859890
;; run: ld_k 18 => i64:6342146187518525551
;; run: ld_k 27 => i64:-8662029109432772692
;; run: ld_k 36 => i64:1782034916086738665
;; run: ld_k 45 => i64:5224429052207404326
;; run: ld_k 54 => i64:8666997828965657699
;; run: and_u 0 4294967295 => i64:0
;; run: and_u 4294967295 2147483648 => i64:576460750155939840
;; run: and_u 4294967296 2147483647 => i64:0
;; run: and_u 9223372036854775807 3 => i64:805306365
;; run: and_u 9223372036854775808 4294967294 => i64:0
;; run: and_u 18446744073709551615 2147483649 => i64:576460750424375295
;; run: and_u 1311768467463790320 1 => i64:180150000
;; run: and_u 2147483647 4294967295 => i64:1152921500043444225
;; run: and_u 2147483648 2147483648 => i64:0
;; run: and_u 4294967294 2147483647 => i64:576460747740020738
;; run: and_u 9223372032559808512 3 => i64:0
;; run: and_u 32768 4294967294 => i64:140737488289792
;; run: and_u 65535 2147483649 => i64:140735340937215
;; run: and_u 9223372036854710272 1 => i64:268369920
;; run: shru_u 0 4294967295 => i64:0
;; run: shru_u 4294967295 2147483648 => i64:0
;; run: shru_u 4294967296 2147483647 => i64:2147483647
;; run: shru_u 9223372036854775807 3 => i64:6442450941
;; run: shru_u 9223372036854775808 4294967294 => i64:9223372032559808512
;; run: shru_u 18446744073709551615 2147483649 => i64:-9223372034707292161
;; run: shru_u 1311768467463790320 1 => i64:305419896
;; run: shru_u 2147483647 4294967295 => i64:0
;; run: shru_u 2147483648 2147483648 => i64:0
;; run: shru_u 4294967294 2147483647 => i64:0
;; run: shru_u 9223372032559808512 3 => i64:6442450941
;; run: shru_u 32768 4294967294 => i64:0
;; run: shru_u 65535 2147483649 => i64:0
;; run: shru_u 9223372036854710272 1 => i64:2147483647
;; run: shru31 0 4294967295 => i64:0
;; run: shru31 4294967295 2147483648 => i64:2147483648
;; run: shru31 4294967296 2147483647 => i64:4294967294
;; run: shru31 9223372036854775807 3 => i64:12884901885
;; run: shru31 9223372036854775808 4294967294 => i64:-8589934592
;; run: shru31 18446744073709551615 2147483649 => i64:6442450943
;; run: shru31 1311768467463790320 1 => i64:610839793
;; run: shru31 2147483647 4294967295 => i64:0
;; run: shru31 2147483648 2147483648 => i64:2147483648
;; run: shru31 4294967294 2147483647 => i64:2147483647
;; run: shru31 9223372032559808512 3 => i64:12884901882
;; run: shru31 32768 4294967294 => i64:0
;; run: shru31 65535 2147483649 => i64:0
;; run: shru31 9223372036854710272 1 => i64:4294967295
;; run: shrs_s 0 4294967295 => i64:0
;; run: shrs_s 4294967295 2147483648 => i64:0
;; run: shrs_s 4294967296 2147483647 => i64:2147483647
;; run: shrs_s 9223372036854775807 3 => i64:6442450941
;; run: shrs_s 9223372036854775808 4294967294 => i64:4294967296
;; run: shrs_s 18446744073709551615 2147483649 => i64:2147483647
;; run: shrs_s 1311768467463790320 1 => i64:305419896
;; run: shrs_s 2147483647 4294967295 => i64:0
;; run: shrs_s 2147483648 2147483648 => i64:0
;; run: shrs_s 4294967294 2147483647 => i64:0
;; run: shrs_s 9223372032559808512 3 => i64:6442450941
;; run: shrs_s 32768 4294967294 => i64:0
;; run: shrs_s 65535 2147483649 => i64:0
;; run: shrs_s 9223372036854710272 1 => i64:2147483647
;; run: shrs31 0 4294967295 => i64:0
;; run: shrs31 4294967295 2147483648 => i64:-2147483648
;; run: shrs31 4294967296 2147483647 => i64:4294967294
;; run: shrs31 9223372036854775807 3 => i64:12884901885
;; run: shrs31 9223372036854775808 4294967294 => i64:8589934592
;; run: shrs31 18446744073709551615 2147483649 => i64:2147483647
;; run: shrs31 1311768467463790320 1 => i64:610839793
;; run: shrs31 2147483647 4294967295 => i64:0
;; run: shrs31 2147483648 2147483648 => i64:-2147483648
;; run: shrs31 4294967294 2147483647 => i64:2147483647
;; run: shrs31 9223372032559808512 3 => i64:12884901882
;; run: shrs31 32768 4294967294 => i64:0
;; run: shrs31 65535 2147483649 => i64:0
;; run: shrs31 9223372036854710272 1 => i64:4294967295
;; run: shrs_u 0 4294967295 => i64:0
;; run: shrs_u 4294967295 2147483648 => i64:0
;; run: shrs_u 4294967296 2147483647 => i64:2147483647
;; run: shrs_u 9223372036854775807 3 => i64:6442450941
;; run: shrs_u 9223372036854775808 4294967294 => i64:-9223372032559808512
;; run: shrs_u 18446744073709551615 2147483649 => i64:-2147483649
;; run: shrs_u 1311768467463790320 1 => i64:305419896
;; run: shrs_u 2147483647 4294967295 => i64:0
;; run: shrs_u 2147483648 2147483648 => i64:0
;; run: shrs_u 4294967294 2147483647 => i64:0
;; run: shrs_u 9223372032559808512 3 => i64:6442450941
;; run: shrs_u 32768 4294967294 => i64:0
;; run: shrs_u 65535 2147483649 => i64:0
;; run: shrs_u 9223372036854710272 1 => i64:2147483647
;; run: ext16_s 0 4294967295 => i64:0
;; run: ext16_s 4294967295 2147483648 => i64:2147483648
;; run: ext16_s 4294967296 2147483647 => i64:0
;; run: ext16_s 9223372036854775807 3 => i64:-3
;; run: ext16_s 9223372036854775808 4294967294 => i64:0
;; run: ext16_s 18446744073709551615 2147483649 => i64:2147483647
;; run: ext16_s 1311768467463790320 1 => i64:-8464
;; run: ext16_s 2147483647 4294967295 => i64:1
;; run: ext16_s 2147483648 2147483648 => i64:0
;; run: ext16_s 4294967294 2147483647 => i64:-4294967294
;; run: ext16_s 9223372032559808512 3 => i64:0
;; run: ext16_s 32768 4294967294 => i64:65536
;; run: ext16_s 65535 2147483649 => i64:2147483647
;; run: ext16_s 9223372036854710272 1 => i64:0
;; run: ext16_u 0 4294967295 => i64:0
;; run: ext16_u 4294967295 2147483648 => i64:-2147483648
;; run: ext16_u 4294967296 2147483647 => i64:0
;; run: ext16_u 9223372036854775807 3 => i64:-3
;; run: ext16_u 9223372036854775808 4294967294 => i64:0
;; run: ext16_u 18446744073709551615 2147483649 => i64:-2147483649
;; run: ext16_u 1311768467463790320 1 => i64:-8464
;; run: ext16_u 2147483647 4294967295 => i64:-4294967295
;; run: ext16_u 2147483648 2147483648 => i64:0
;; run: ext16_u 4294967294 2147483647 => i64:-4294967294
;; run: ext16_u 9223372032559808512 3 => i64:0
;; run: ext16_u 32768 4294967294 => i64:-140737488289792
;; run: ext16_u 65535 2147483649 => i64:-2147483649
;; run: ext16_u 9223372036854710272 1 => i64:0
;; run: and_s 0 4294967295 => i64:0
;; run: and_s 4294967295 2147483648 => i64:-4611686016279904256
;; run: and_s 4294967296 2147483647 => i64:0
;; run: and_s 9223372036854775807 3 => i64:6442450941
;; run: and_s 9223372036854775808 4294967294 => i64:0
;; run: and_s 18446744073709551615 2147483649 => i64:-4611686014132420609
;; run: and_s 1311768467463790320 1 => i64:448585456
;; run: and_s 2147483647 4294967295 => i64:-2147483647
;; run: and_s 2147483648 2147483648 => i64:0
;; run: and_s 4294967294 2147483647 => i64:4611686011984936962
;; run: and_s 9223372032559808512 3 => i64:0
;; run: and_s 32768 4294967294 => i64:-65536
;; run: and_s 65535 2147483649 => i64:-140735340806145
;; run: and_s 9223372036854710272 1 => i64:2147418112
;; run: and_s32 0 4294967295 => i64:0
;; run: and_s32 4294967295 2147483648 => i64:-9223372032559808512
;; run: and_s32 4294967296 2147483647 => i64:0
;; run: and_s32 9223372036854775807 3 => i64:12884901882
;; run: and_s32 9223372036854775808 4294967294 => i64:0
;; run: and_s32 18446744073709551615 2147483649 => i64:-9223372028264841218
;; run: and_s32 1311768467463790320 1 => i64:2596069104
;; run: and_s32 2147483647 4294967295 => i64:-2147483646
;; run: and_s32 2147483648 2147483648 => i64:-4611686018427387904
;; run: and_s32 4294967294 2147483647 => i64:9223372028264841218
;; run: and_s32 9223372032559808512 3 => i64:0
;; run: and_s32 32768 4294967294 => i64:-65536
;; run: and_s32 65535 2147483649 => i64:-140733193322498
;; run: and_s32 9223372036854710272 1 => i64:4294901760
;; run: both_ways 0 4294967295 => i64:0
;; run: both_ways 4294967295 2147483648 => i64:4396972769280
;; run: both_ways 4294967296 2147483647 => i64:0
;; run: both_ways 9223372036854775807 3 => i64:4095
;; run: both_ways 9223372036854775808 4294967294 => i64:0
;; run: both_ways 18446744073709551615 2147483649 => i64:4396972769280
;; run: both_ways 1311768467463790320 1 => i64:0
;; run: both_ways 2147483647 4294967295 => i64:8793945534465
;; run: both_ways 2147483648 2147483648 => i64:0
;; run: both_ways 4294967294 2147483647 => i64:4395899023362
;; run: both_ways 9223372032559808512 3 => i64:0
;; run: both_ways 32768 4294967294 => i64:0
;; run: both_ways 65535 2147483649 => i64:4396972769280
;; run: both_ways 9223372036854710272 1 => i64:0
;; run: sum16 0 32767 4294967295 => i64:-32767
;; run: sum16 4294967295 2147483647 2147483648 => i64:4294967296
;; run: sum16 4294967296 2147483648 2147483647 => i64:0
;; run: sum16 9223372036854775807 32768 3 => i64:-98307
;; run: sum16 9223372036854775808 32767 4294967294 => i64:-65534
;; run: sum16 18446744073709551615 2147483647 4294967295 => i64:2
;; run: sum16 1311768467463790320 2147483648 2147483648 => i64:18176301596672
;; run: sum16 2147483647 32768 2147483647 => i64:-70370891628543
;; run: sum16 2147483648 32767 3 => i64:98301
;; run: sum16 4294967294 2147483647 4294967294 => i64:6
;; run: sum16 9223372032559808512 2147483648 4294967295 => i64:0
;; run: sum16 32768 32768 2147483648 => i64:140737488355328
;; run: sum16 65535 32767 2147483647 => i64:70364449177602
;; run: sum16 9223372036854710272 2147483647 3 => i64:-3
;; run: sum32 0 32767 4294967295 => i64:-32767
;; run: sum32 4294967295 2147483647 2147483648 => i64:-4611686014132420608
;; run: sum32 4294967296 2147483648 2147483647 => i64:-4611686016279904256
;; run: sum32 9223372036854775807 32768 3 => i64:98301
;; run: sum32 9223372036854775808 32767 4294967294 => i64:-65534
;; run: sum32 18446744073709551615 2147483647 4294967295 => i64:-2147483646
;; run: sum32 1311768467463790320 2147483648 2147483648 => i64:8260042105364152320
;; run: sum32 2147483647 32768 2147483647 => i64:4611756382876565505
;; run: sum32 2147483648 32767 3 => i64:-6442352643
;; run: sum32 4294967294 2147483647 4294967294 => i64:-4294967290
;; run: sum32 9223372032559808512 2147483648 4294967295 => i64:2147483648
;; run: sum32 32768 32768 2147483648 => i64:-140737488355328
;; run: sum32 65535 32767 2147483647 => i64:211101937467394
;; run: sum32 9223372036854710272 2147483647 3 => i64:6442254333
;; run: umadd 4294967295 4294967295 0 => i64:-8589934591
;; run: umadd 2147483648 2147483647 4294967295 => i64:4611686020574871551
;; run: umadd 2147483647 2147483649 4294967296 => i64:4611686022722355199
;; run: umadd 3 4294967294 9223372036854775807 => i64:-9223372023969873927
;; run: umadd 123456789 3141592653 9223372036854775808 => i64:-8835521095569404591
;; run: umadd 0 5 18446744073709551615 => i64:-1
;; run: umadd 1 2147483648 1311768467463790320 => i64:1311768469611273968
;; run: umadd 2147483649 2147483649 2147483647 => i64:4611686024869838848
;; run: smadd 4294967295 4294967295 0 => i64:1
;; run: smadd 2147483648 2147483647 4294967295 => i64:-4611686011984936961
;; run: smadd 2147483647 2147483649 4294967296 => i64:-4611686009837453313
;; run: smadd 3 4294967294 9223372036854775807 => i64:9223372036854775801
;; run: smadd 123456789 3141592653 9223372036854775808 => i64:9080980106915974481
;; run: smadd 0 5 18446744073709551615 => i64:-1
;; run: smadd 1 2147483648 1311768467463790320 => i64:1311768465316306672
;; run: smadd 2147483649 2147483649 2147483647 => i64:4611686016279904256
;; run: umadd_l 4294967295 4294967295 0 => i64:-8589934591
;; run: umadd_l 2147483648 2147483647 4294967295 => i64:4611686020574871551
;; run: umadd_l 2147483647 2147483649 4294967296 => i64:4611686022722355199
;; run: umadd_l 3 4294967294 9223372036854775807 => i64:-9223372023969873927
;; run: umadd_l 123456789 3141592653 9223372036854775808 => i64:-8835521095569404591
;; run: umadd_l 0 5 18446744073709551615 => i64:-1
;; run: umadd_l 1 2147483648 1311768467463790320 => i64:1311768469611273968
;; run: umadd_l 2147483649 2147483649 2147483647 => i64:4611686024869838848
;; run: umsub 4294967295 4294967295 0 => i64:8589934591
;; run: umsub 2147483648 2147483647 4294967295 => i64:-4611686011984936961
;; run: umsub 2147483647 2147483649 4294967296 => i64:-4611686014132420607
;; run: umsub 3 4294967294 9223372036854775807 => i64:9223372023969873925
;; run: umsub 123456789 3141592653 9223372036854775808 => i64:8835521095569404591
;; run: umsub 0 5 18446744073709551615 => i64:-1
;; run: umsub 1 2147483648 1311768467463790320 => i64:1311768465316306672
;; run: umsub 2147483649 2147483649 2147483647 => i64:-4611686020574871554
;; run: smsub 4294967295 4294967295 0 => i64:-1
;; run: smsub 2147483648 2147483647 4294967295 => i64:4611686020574871551
;; run: smsub 2147483647 2147483649 4294967296 => i64:4611686018427387905
;; run: smsub 3 4294967294 9223372036854775807 => i64:-9223372036854775803
;; run: smsub 123456789 3141592653 9223372036854775808 => i64:-9080980106915974481
;; run: smsub 0 5 18446744073709551615 => i64:-1
;; run: smsub 1 2147483648 1311768467463790320 => i64:1311768469611273968
;; run: smsub 2147483649 2147483649 2147483647 => i64:-4611686011984936962
;; run: sub_l 4294967295 4294967295 0 => i64:-8589934591
;; run: sub_l 2147483648 2147483647 4294967295 => i64:4611686011984936961
;; run: sub_l 2147483647 2147483649 4294967296 => i64:4611686014132420607
;; run: sub_l 3 4294967294 9223372036854775807 => i64:-9223372023969873925
;; run: sub_l 123456789 3141592653 9223372036854775808 => i64:-8835521095569404591
;; run: sub_l 0 5 18446744073709551615 => i64:1
;; run: sub_l 1 2147483648 1311768467463790320 => i64:-1311768465316306672
;; run: sub_l 2147483649 2147483649 2147483647 => i64:4611686020574871554
;; run: hash 4294967295 0 => i64:-7046029293536115121
;; run: hash 2147483648 4294967295 => i64:5700357395708903423
;; run: hash 2147483647 4294967296 => i64:5700357393054467663
;; run: hash 3 9223372036854775807 => i64:-9223372028891468526
;; run: hash 123456789 9223372036854775808 => i64:-8895663921194944379
;; run: hash 0 18446744073709551615 => i64:-1
;; run: hash 1 1311768467463790320 => i64:1311768470118226081
;; run: hash 2147483649 2147483647 => i64:5700357396215855536
;; run: shash 4294967295 0 => i64:-1000003
;; run: shash 2147483648 4294967295 => i64:-2147485795483649
;; run: shash 2147483647 4294967296 => i64:2147494384418237
;; run: shash 3 9223372036854775807 => i64:-9223372036851775800
;; run: shash 123456789 9223372036854775808 => i64:-9223248579695405441
;; run: shash 0 18446744073709551615 => i64:-1
;; run: shash 1 1311768467463790320 => i64:1311768467464790323
;; run: shash 2147483649 2147483647 => i64:-2147487941967294
;; run: sq_add 4294967295 0 => i64:1
;; run: sq_add 2147483648 4294967295 => i64:4611686022722355199
;; run: sq_add 2147483647 4294967296 => i64:4611686018427387905
;; run: sq_add 3 9223372036854775807 => i64:-9223372036854775800
;; run: sq_add 123456789 9223372036854775808 => i64:-9208130458104585287
;; run: sq_add 0 18446744073709551615 => i64:-1
;; run: sq_add 1 1311768467463790320 => i64:1311768467463790321
;; run: sq_add 2147483649 2147483647 => i64:4611686016279904256
;; run: far 4294967295 4294967295 1 => i64:-8589934582
;; run: far 4294967295 4294967295 0 => i64:-8589934591
;; run: far_s 4294967295 4294967295 1 => i64:8
;; run: far_s 4294967295 4294967295 0 => i64:-1
;; run: far 2147483648 2147483647 1 => i64:4611686016279904265
;; run: far 2147483648 2147483647 0 => i64:4611686016279904256
;; run: far_s 2147483648 2147483647 1 => i64:4611686016279904265
;; run: far_s 2147483648 2147483647 0 => i64:4611686016279904256
;; run: far 2147483647 2147483649 1 => i64:4611686018427387912
;; run: far 2147483647 2147483649 0 => i64:4611686018427387903
;; run: far_s 2147483647 2147483649 1 => i64:4611686014132420618
;; run: far_s 2147483647 2147483649 0 => i64:4611686014132420609
;; run: far 3 4294967294 1 => i64:12884901891
;; run: far 3 4294967294 0 => i64:12884901882
;; run: far_s 3 4294967294 1 => i64:15
;; run: far_s 3 4294967294 0 => i64:6
;; run: far 123456789 3141592653 1 => i64:387850941285371226
;; run: far 123456789 3141592653 0 => i64:387850941285371217
;; run: far_s 123456789 3141592653 1 => i64:142391929938801336
;; run: far_s 123456789 3141592653 0 => i64:142391929938801327
;; run: other 5 0 => i64:0
;; run: other 5 1 => i64:5111134240
;; run: other 5 3 => i64:-84126299855131
;; run: other 5 4 => i64:10768168598720466
;; run: other 5 5 => i64:-1378325586368847068
;; run: other 5 6 => i64:8041765674555723481
;; run: other 5 100 => i64:8054451761458058257
;; run: other 4294967295 0 => i64:0
;; run: other 4294967295 1 => i64:-1012239698
;; run: other 4294967295 3 => i64:-16549753776212
;; run: other 4294967295 4 => i64:-2118369246756318
;; run: other 4294967295 5 => i64:-271151264519441883
;; run: other 4294967295 6 => i64:2186126288394266317
;; run: other 4294967295 100 => i64:4798776669807872819
;; run: other 2147483648 0 => i64:0
;; run: other 2147483648 1 => i64:2434343278896742400
;; run: other 2147483648 3 => i64:-2488213455962634116
;; run: other 2147483648 4 => i64:6235466394012761710
;; run: other 2147483648 5 => i64:5386127576800376619
;; run: other 2147483648 6 => i64:-5780471293717801563
;; run: other 2147483648 100 => i64:3677018446744459994
;; run: dot 0 1 => i64:0
;; run: sdot 0 1 => i64:0
;; run: dot 0 3735928559 => i64:0
;; run: sdot 0 3735928559 => i64:0
;; run: dot 1 1 => i64:737062059779293068
;; run: sdot 1 1 => i64:-737062059779293068
;; run: dot 1 3735928559 => i64:7296560934489991710
;; run: sdot 1 3735928559 => i64:389922003986634210
;; run: dot 3 1 => i64:2160791520509905464
;; run: sdot 3 1 => i64:3407533680521645351
;; run: dot 3 3735928559 => i64:164618390003165466
;; run: sdot 3 3735928559 => i64:3672624154886622305
;; run: dot 4 1 => i64:1292911743542245976
;; run: sdot 4 1 => i64:-1105145049038405343
;; run: dot 4 3735928559 => i64:219834254349849608
;; run: sdot 4 3735928559 => i64:6624960340936654192
;; run: dot 5 1 => i64:756058850619169620
;; run: sdot 5 1 => i64:1715253982517367134
;; run: dot 5 3735928559 => i64:2890956531274419130
;; run: sdot 5 3735928559 => i64:6195307023005555449
;; run: dot 37 1 => i64:336785610021859692
;; run: sdot 37 1 => i64:-2177751370920684442
;; run: dot 37 3735928559 => i64:-9146322403456459053
;; run: sdot 37 3735928559 => i64:-1526360303805890556
;; run: dot 1000 1 => i64:2798867120948058922
;; run: sdot 1000 1 => i64:5649509857386710721
;; run: dot 1000 3735928559 => i64:-5029839657442414651
;; run: sdot 1000 3735928559 => i64:9173121846685938119

(module
  (memory 1)
  (data (i32.const 0) "\85\aa\cf\f4\19\3e\63\88\ad\d2\f7\1c\41\66\8b\b0\d5\fa\1f\44\69\8e\b3\d8\fd\22\47\6c\91\b6\db\00\25\4a\6f\94\b9\de\03\28\4d\72\97\bc\e1\06\2b\50\75\9a\bf\e4\09\2e\53\78\9d\c2\e7\0c\31\56\7b\a0")
  (func $umul (export "umul") (param i32 i32) (result i64)
    (i64.mul (i64.extend_i32_u (local.get 0)) (i64.extend_i32_u (local.get 1))))
  (func $smul (export "smul") (param i32 i32) (result i64)
    (i64.mul (i64.extend_i32_s (local.get 0)) (i64.extend_i32_s (local.get 1))))
  (func $ext32 (export "ext32") (param i64 i64) (result i64)
    (i64.mul (i64.extend32_s (local.get 1)) (i64.extend32_s (local.get 0))))
  (func $smix (export "smix") (param i64 i32 i64) (result i64)
    (i64.mul (i64.extend_i32_s (local.get 1)) (i64.extend32_s (local.get 2))))
  (func $mixed (export "mixed") (param i32 i32) (result i64)
    (i64.mul (i64.extend_i32_u (local.get 0)) (i64.extend_i32_s (local.get 1))))
  (func $mixed2 (export "mixed2") (param i32 i32) (result i64)
    (i64.mul (i64.extend_i32_s (local.get 0)) (i64.extend_i32_u (local.get 1))))
  (func $half (export "half") (param i32 i64) (result i64)
    (i64.mul (i64.extend_i32_u (local.get 0)) (local.get 1)))
  (func $half2 (export "half2") (param i64 i32) (result i64)
    (i64.mul (local.get 0) (i64.extend_i32_s (local.get 1))))
  (func $umul_k (export "umul_k") (param i32) (result i64)
    (i64.mul (i64.extend_i32_u (local.get 0)) (i64.const 2654435761)))
  (func $umul_p (export "umul_p") (param i32) (result i64)
    (i64.mul (i64.extend_i32_u (local.get 0)) (i64.const 1000003)))
  (func $k_umul (export "k_umul") (param i32) (result i64)
    (i64.mul (i64.const 2654435763) (i64.extend_i32_u (local.get 0))))
  (func $umul_max (export "umul_max") (param i32) (result i64)
    (i64.mul (i64.extend_i32_u (local.get 0)) (i64.const 4294967291)))
  (func $smul_p (export "smul_p") (param i32) (result i64)
    (i64.mul (i64.extend_i32_s (local.get 0)) (i64.const 1000003)))
  (func $smul_n (export "smul_n") (param i32) (result i64)
    (i64.mul (i64.extend_i32_s (local.get 0)) (i64.const -1000003)))
  (func $n_smul (export "n_smul") (param i32) (result i64)
    (i64.mul (i64.const -1000005) (i64.extend_i32_s (local.get 0))))
  (func $smul_min (export "smul_min") (param i32) (result i64)
    (i64.mul (i64.extend_i32_s (local.get 0)) (i64.const -2147483643)))
  (func $smul_top (export "smul_top") (param i32) (result i64)
    (i64.mul (i64.extend_i32_s (local.get 0)) (i64.const 2147483643)))
  (func $u_wide (export "u_wide") (param i32) (result i64)
    (i64.mul (i64.extend_i32_u (local.get 0)) (i64.const 4294967301)))
  (func $u_neg (export "u_neg") (param i32) (result i64)
    (i64.mul (i64.extend_i32_u (local.get 0)) (i64.const -1000003)))
  (func $s_big (export "s_big") (param i32) (result i64)
    (i64.mul (i64.extend_i32_s (local.get 0)) (i64.const 3000000019)))
  (func $s_negbig (export "s_negbig") (param i32) (result i64)
    (i64.mul (i64.extend_i32_s (local.get 0)) (i64.const -3000000019)))
  (func $s_over (export "s_over") (param i32) (result i64)
    (i64.mul (i64.extend_i32_s (local.get 0)) (i64.const 2147483653)))
  (func $s_under (export "s_under") (param i32) (result i64)
    (i64.mul (i64.extend_i32_s (local.get 0)) (i64.const -2147483653)))
  (func $u_over (export "u_over") (param i32) (result i64)
    (i64.mul (i64.extend_i32_u (local.get 0)) (i64.const 4294967307)))
  (func $s_wide (export "s_wide") (param i32) (result i64)
    (i64.mul (i64.extend_i32_s (local.get 0)) (i64.const 4294967301)))
  (func $twice (export "twice") (param i32 i32) (result i64)
    (local $e i64) (local.set $e (i64.extend_i32_u (local.get 0)))
    (i64.xor (i64.mul (local.get $e) (i64.extend_i32_u (local.get 1))) (local.get $e)))
  (func $twice_s (export "twice_s") (param i32 i32) (result i64)
    (local $e i64) (local.set $e (i64.extend_i32_s (local.get 1)))
    (i64.xor (i64.mul (i64.extend_i32_s (local.get 0)) (local.get $e)) (local.get $e)))
  (func $twice_mix (export "twice_mix") (param i32 i32) (result i64)
    (local $e i64) (local.set $e (i64.extend_i32_s (local.get 1)))
    (i64.xor (i64.mul (i64.extend_i32_u (local.get 0)) (local.get $e)) (local.get $e)))
  (func $both_twice (export "both_twice") (param i32 i32) (result i64)
    (local $e i64) (local $g i64) (local.set $e (i64.extend_i32_u (local.get 0))) (local.set $g (i64.extend_i32_u (local.get 1)))
    (i64.xor (i64.mul (local.get $e) (local.get $g)) (i64.sub (local.get $e) (local.get $g))))
  (func $square (export "square") (param i32) (result i64)
    (local $e i64) (local.set $e (i64.extend_i32_u (local.get 0)))
    (i64.mul (local.get $e) (local.get $e)))
  (func $ssquare (export "ssquare") (param i32) (result i64)
    (local $e i64) (local.set $e (i64.extend_i32_s (local.get 0)))
    (i64.mul (local.get $e) (local.get $e)))
  (func $square3 (export "square3") (param i32) (result i64)
    (local $e i64) (local.set $e (i64.extend_i32_u (local.get 0)))
    (i64.xor (i64.mul (local.get $e) (local.get $e)) (local.get $e)))
  (func $ld_u (export "ld_u") (param i32 i32) (result i64)
    (i64.mul (i64.load32_u (local.get 0)) (i64.extend_i32_u (local.get 1))))
  (func $ld_s (export "ld_s") (param i32 i32) (result i64)
    (i64.mul (i64.extend_i32_s (local.get 1)) (i64.load32_s (local.get 0))))
  (func $ld8_u (export "ld8_u") (param i32 i32) (result i64)
    (i64.mul (i64.load8_u (local.get 0)) (i64.extend_i32_u (local.get 1))))
  (func $ld8_us (export "ld8_us") (param i32 i32) (result i64)
    (i64.mul (i64.load8_u (local.get 0)) (i64.extend_i32_s (local.get 1))))
  (func $ld16_us (export "ld16_us") (param i32 i32) (result i64)
    (i64.mul (i64.load16_u (local.get 0)) (i64.extend_i32_s (local.get 1))))
  (func $ld16_s (export "ld16_s") (param i32 i32) (result i64)
    (i64.mul (i64.load16_s (local.get 0)) (i64.extend_i32_s (local.get 1))))
  (func $ld16_su (export "ld16_su") (param i32 i32) (result i64)
    (i64.mul (i64.load16_s (local.get 0)) (i64.extend_i32_u (local.get 1))))
  (func $ld8_su (export "ld8_su") (param i32 i32) (result i64)
    (i64.mul (i64.load8_s (local.get 0)) (i64.extend_i32_u (local.get 1))))
  (func $ld32_us (export "ld32_us") (param i32 i32) (result i64)
    (i64.mul (i64.load32_u (local.get 0)) (i64.extend_i32_s (local.get 1))))
  (func $ld32_su (export "ld32_su") (param i32 i32) (result i64)
    (i64.mul (i64.load32_s (local.get 0)) (i64.extend_i32_u (local.get 1))))
  (func $ld64 (export "ld64") (param i32 i32) (result i64)
    (i64.mul (i64.load (local.get 0)) (i64.extend_i32_u (local.get 1))))
  (func $ld_ld (export "ld_ld") (param i32 i32) (result i64)
    (i64.mul (i64.load32_u (local.get 0)) (i64.load32_u (local.get 1))))
  (func $ld_k (export "ld_k") (param i32) (result i64)
    (i64.mul (i64.load32_u (local.get 0)) (i64.const 2654435761)))
  (func $and_u (export "and_u") (param i64 i32) (result i64)
    (i64.mul (i64.and (local.get 0) (i64.const 0xfffffff)) (i64.extend_i32_u (local.get 1))))
  (func $shru_u (export "shru_u") (param i64 i32) (result i64)
    (i64.mul (i64.extend_i32_u (local.get 1)) (i64.shr_u (local.get 0) (i64.const 32))))
  (func $shru31 (export "shru31") (param i64 i32) (result i64)
    (i64.mul (i64.extend_i32_u (local.get 1)) (i64.shr_u (local.get 0) (i64.const 31))))
  (func $shrs_s (export "shrs_s") (param i64 i32) (result i64)
    (i64.mul (i64.shr_s (local.get 0) (i64.const 32)) (i64.extend_i32_s (local.get 1))))
  (func $shrs31 (export "shrs31") (param i64 i32) (result i64)
    (i64.mul (i64.shr_s (local.get 0) (i64.const 31)) (i64.extend_i32_s (local.get 1))))
  (func $shrs_u (export "shrs_u") (param i64 i32) (result i64)
    (i64.mul (i64.shr_s (local.get 0) (i64.const 32)) (i64.extend_i32_u (local.get 1))))
  (func $ext16_s (export "ext16_s") (param i64 i32) (result i64)
    (i64.mul (i64.extend16_s (local.get 0)) (i64.extend_i32_s (local.get 1))))
  (func $ext16_u (export "ext16_u") (param i64 i32) (result i64)
    (i64.mul (i64.extend16_s (local.get 0)) (i64.extend_i32_u (local.get 1))))
  (func $and_s (export "and_s") (param i64 i32) (result i64)
    (i64.mul (i64.and (local.get 0) (i64.const 0x7fffffff)) (i64.extend_i32_s (local.get 1))))
  (func $and_s32 (export "and_s32") (param i64 i32) (result i64)
    (i64.mul (i64.and (local.get 0) (i64.const 0xfffffffe)) (i64.extend_i32_s (local.get 1))))
  (func $both_ways (export "both_ways") (param i64 i32) (result i64)
    (i64.mul (i64.and (local.get 0) (i64.const 0xfff)) (i64.extend_i32_u (i32.shr_u (local.get 1) (i32.const 1)))))
  (func $sum16 (export "sum16") (param i64 i64 i32) (result i64)
    (i64.mul (i64.add (i64.extend16_s (local.get 0)) (i64.extend16_s (local.get 1))) (i64.extend_i32_s (local.get 2))))
  (func $sum32 (export "sum32") (param i64 i64 i32) (result i64)
    (i64.mul (i64.add (i64.extend32_s (local.get 0)) (i64.extend32_s (local.get 1))) (i64.extend_i32_s (local.get 2))))
  (func $umadd (export "umadd") (param i32 i32 i64) (result i64)
    (i64.add (local.get 2) (i64.mul (i64.extend_i32_u (local.get 0)) (i64.extend_i32_u (local.get 1)))))
  (func $smadd (export "smadd") (param i32 i32 i64) (result i64)
    (i64.add (local.get 2) (i64.mul (i64.extend_i32_s (local.get 0)) (i64.extend_i32_s (local.get 1)))))
  (func $umadd_l (export "umadd_l") (param i32 i32 i64) (result i64)
    (i64.add (i64.mul (i64.extend_i32_u (local.get 1)) (i64.extend_i32_u (local.get 0))) (local.get 2)))
  (func $umsub (export "umsub") (param i32 i32 i64) (result i64)
    (i64.sub (local.get 2) (i64.mul (i64.extend_i32_u (local.get 0)) (i64.extend_i32_u (local.get 1)))))
  (func $smsub (export "smsub") (param i32 i32 i64) (result i64)
    (i64.sub (local.get 2) (i64.mul (i64.extend_i32_s (local.get 0)) (i64.extend_i32_s (local.get 1)))))
  (func $sub_l (export "sub_l") (param i32 i32 i64) (result i64)
    (i64.sub (i64.mul (i64.extend_i32_u (local.get 0)) (i64.extend_i32_u (local.get 1))) (local.get 2)))
  (func $hash (export "hash") (param i32 i64) (result i64)
    (i64.add (local.get 1) (i64.mul (i64.extend_i32_u (local.get 0)) (i64.const 2654435761))))
  (func $shash (export "shash") (param i32 i64) (result i64)
    (i64.sub (local.get 1) (i64.mul (i64.extend_i32_s (local.get 0)) (i64.const -1000003))))
  (func $sq_add (export "sq_add") (param i32 i64) (result i64)
    (local $e i64) (local.set $e (i64.extend_i32_s (local.get 0)))
    (i64.add (local.get 1) (i64.mul (local.get $e) (local.get $e))))
  (func $far (export "far") (param $a i32) (param $b i32) (param $c i32) (result i64)
    (local $e i64)
    (local.set $e (i64.extend_i32_u (local.get $a)))
    (if (local.get $c) (then (i64.store (i32.const 64) (i64.const 9))))
    (i64.add (i64.load (i32.const 64)) (i64.mul (local.get $e) (i64.extend_i32_u (local.get $b)))))
  (func $far_s (export "far_s") (param $a i32) (param $b i32) (param $c i32) (result i64)
    (local $e i64)
    (local.set $e (i64.extend_i32_s (local.get $a)))
    (if (local.get $c) (then (i64.store (i32.const 72) (i64.const 9))))
    (i64.sub (i64.load (i32.const 72)) (i64.mul (i64.extend_i32_s (local.get $b)) (local.get $e))))
  (func $other (export "other") (param $a i32) (param $n i32) (result i64)
    (local $e i64) (local $x i32) (local $acc i64)
    (local.set $e (i64.extend_i32_s (local.get $a)))
    (local.set $x (local.get $a))
    (block $done (loop $l
      (br_if $done (i32.eqz (local.get $n)))
      (local.set $x (i32.add (i32.mul (local.get $x) (i32.const 1664525)) (i32.const 1013904223)))
      (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7))
        (i64.mul (local.get $e) (i64.extend_i32_s (local.get $x)))))
      (local.set $n (i32.sub (local.get $n) (i32.const 1)))
      (br $l)))
    (local.get $acc))
  (func $dot (export "dot") (param $n i32) (param $seed i32) (result i64)
    (local $acc i64) (local $x i32) (local $y i32)
    (local.set $x (local.get $seed))
    (local.set $y (i32.xor (local.get $seed) (i32.const 0x9e3779b9)))
    (block $done (loop $l
      (br_if $done (i32.eqz (local.get $n)))
      (local.set $x (i32.add (i32.mul (local.get $x) (i32.const 1664525)) (i32.const 1013904223)))
      (local.set $y (i32.add (i32.mul (local.get $y) (i32.const 22695477)) (i32.const 1)))
      (local.set $acc (i64.add (i64.shr_u (local.get $acc) (i64.const 7))
        (i64.mul (i64.extend_i32_u (local.get $x)) (i64.extend_i32_u (local.get $y)))))
      (local.set $n (i32.sub (local.get $n) (i32.const 1)))
      (br $l)))
    (local.get $acc))
  (func $sdot (export "sdot") (param $n i32) (param $seed i32) (result i64)
    (local $acc i64) (local $x i32) (local $y i32)
    (local.set $x (local.get $seed))
    (local.set $y (i32.xor (local.get $seed) (i32.const 0x9e3779b9)))
    (block $done (loop $l
      (br_if $done (i32.eqz (local.get $n)))
      (local.set $x (i32.add (i32.mul (local.get $x) (i32.const 1664525)) (i32.const 1013904223)))
      (local.set $y (i32.add (i32.mul (local.get $y) (i32.const 22695477)) (i32.const 1)))
      (local.set $acc (i64.sub (i64.rotl (local.get $acc) (i64.const 5))
        (i64.mul (i64.extend_i32_s (local.get $x)) (i64.extend_i32_s (local.get $y)))))
      (local.set $n (i32.sub (local.get $n) (i32.const 1)))
      (br $l)))
    (local.get $acc))
)
