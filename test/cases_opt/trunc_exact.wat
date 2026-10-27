;; A float computation that rounds nowhere -- integers converted, integral
;; constants, and sums, differences, products and negations of them, every
;; value inside 2^53 (2^24 for f32) -- truncated to an integer type that
;; holds its every value, is that integer arithmetic (opt.c fold_trunc_exact;
;; gobench/bigstruct's int(float64(r & 3) + 5 + 9)): saturating or trapping,
;; i32 or i64 either way, a constant either side, f32.  Not past 2^24 in f32,
;; with a fraction, from an i64 of any size, past what the truncation holds
;; (it saturates), below zero into an unsigned result by each operation, a
;; product whose bound rounds to 2^53 in double, or a division.  And a
;; compare of such a computation with a constant, either side, or with
;; itself, is what its bounds say where they decide it (opt.c fcmp_exact):
;; each compare on and past each bound, a fraction or a value outside never
;; equal, a single value, NaN, f32; not where the bounds straddle the
;; constant, the value is not exact, or itself is any float.  Answers are
;; wasm's float semantics, worked out in Python by test/gen_fexact.py, which
;; writes this file: a change goes there.
;;
;; shape: bigstruct fcvtz=0
;; shape: bigstruct cvtf=0
;; shape: mixed fcvtz=0
;; shape: mixed cvtf=0
;; shape: constfirst fcvtz=0
;; shape: constfirst cvtf=0
;; shape: wide52 fcvtz=0
;; shape: wide52 cvtf=0
;; shape: wideu fcvtz=0
;; shape: wideu cvtf=0
;; shape: wides fcvtz=0
;; shape: wides cvtf=0
;; shape: narrow fcvtz=0
;; shape: narrow cvtf=0
;; shape: small32 fcvtz=0
;; shape: small32 cvtf=0
;; shape: negs fcvtz=0
;; shape: negs cvtf=0
;; shape: badf32 fcvtz=1
;; shape: half fcvtz=1
;; shape: anyi64 fcvtz=1
;; shape: sat31 fcvtz=1
;; shape: negu fcvtz=1
;; shape: negu64 fcvtz=1
;; shape: subu fcvtz=1
;; shape: mulu fcvtz=1
;; shape: negx fcvtz=1
;; shape: subone fcvtz=1
;; shape: edge53 fcvtz=1
;; shape: div fcvtz=1
;; shape: lt9 fcmp=0
;; shape: lt8 fcmp=1
;; shape: le8 fcmp=0
;; shape: le5 fcmp=1
;; shape: le7 fcmp=1
;; shape: gt4 fcmp=0
;; shape: gt8 fcmp=0
;; shape: gt5 fcmp=1
;; shape: ge5 fcmp=0
;; shape: ge8 fcmp=1
;; shape: ge6 fcmp=1
;; shape: ltlow fcmp=0
;; shape: gehigh fcmp=0
;; shape: eqfrac fcmp=0
;; shape: eqout fcmp=0
;; shape: nein fcmp=1
;; shape: neout fcmp=0
;; shape: eqpoint fcmp=0
;; shape: swapgt fcmp=0
;; shape: swaple fcmp=1
;; shape: swaplt fcmp=0
;; shape: swapge fcmp=0
;; shape: nanlt fcmp=0
;; shape: nanne fcmp=0
;; shape: selfeq fcmp=0
;; shape: selfge fcmp=0
;; shape: selfgt fcmp=0
;; shape: selfle fcmp=1
;; shape: f32lt fcmp=0
;; shape: inexact fcmp=1
;; run: bigstruct 0 => i32:14
;; run: bigstruct 1 => i32:15
;; run: bigstruct 2 => i32:16
;; run: bigstruct 3 => i32:17
;; run: bigstruct 5 => i32:15
;; run: bigstruct 100 => i32:14
;; run: bigstruct 255 => i32:17
;; run: bigstruct 256 => i32:14
;; run: bigstruct 321 => i32:15
;; run: bigstruct 4095 => i32:17
;; run: bigstruct 65535 => i32:17
;; run: bigstruct 16777215 => i32:17
;; run: bigstruct 2147483647 => i32:17
;; run: bigstruct -2147483648 => i32:14
;; run: bigstruct -1 => i32:17
;; run: bigstruct -5 => i32:17
;; run: mixed 0 0 => i32:0
;; run: mixed 1 5 => i32:495
;; run: mixed 5 1 => i32:95
;; run: mixed 255 15 => i32:-2325
;; run: mixed 0 255 => i32:1500
;; run: mixed 128 255 => i32:-420
;; run: mixed 200 7 => i32:-700
;; run: mixed -1 -1 => i32:-2325
;; run: constfirst 0 => i32:1000
;; run: constfirst 1 => i32:999
;; run: constfirst 2 => i32:998
;; run: constfirst 3 => i32:997
;; run: constfirst 5 => i32:995
;; run: constfirst 100 => i32:900
;; run: constfirst 255 => i32:745
;; run: constfirst 256 => i32:744
;; run: constfirst 321 => i32:679
;; run: constfirst 4095 => i32:-3095
;; run: constfirst 65535 => i32:-3095
;; run: constfirst 16777215 => i32:-3095
;; run: constfirst 2147483647 => i32:-3095
;; run: constfirst -2147483648 => i32:1000
;; run: constfirst -1 => i32:-3095
;; run: constfirst -5 => i32:-3091
;; run: wide52 0 => i64:1
;; run: wide52 1 => i64:1
;; run: wide52 4096 => i64:2
;; run: wide52 4503599627370495 => i64:1099511627776
;; run: wide52 9007199254740993 => i64:2199023255553
;; run: wide52 -9223372036854775808 => i64:2251799813685249
;; run: wide52 -1 => i64:4503599627370496
;; run: wide52 81985529216486895 => i64:20015998343869
;; run: wideu 0 => i64:1
;; run: wideu 1 => i64:2
;; run: wideu 2 => i64:3
;; run: wideu 3 => i64:4
;; run: wideu 5 => i64:6
;; run: wideu 100 => i64:101
;; run: wideu 255 => i64:256
;; run: wideu 256 => i64:257
;; run: wideu 321 => i64:322
;; run: wideu 4095 => i64:4096
;; run: wideu 65535 => i64:65536
;; run: wideu 16777215 => i64:16777216
;; run: wideu 2147483647 => i64:2147483648
;; run: wideu -2147483648 => i64:2147483649
;; run: wideu -1 => i64:4294967296
;; run: wideu -5 => i64:4294967292
;; run: wides 0 => i64:1
;; run: wides 1 => i64:2
;; run: wides 2 => i64:3
;; run: wides 3 => i64:4
;; run: wides 5 => i64:6
;; run: wides 100 => i64:101
;; run: wides 255 => i64:256
;; run: wides 256 => i64:257
;; run: wides 321 => i64:322
;; run: wides 4095 => i64:4096
;; run: wides 65535 => i64:65536
;; run: wides 16777215 => i64:16777216
;; run: wides 2147483647 => i64:2147483648
;; run: wides -2147483648 => i64:-2147483647
;; run: wides -1 => i64:0
;; run: wides -5 => i64:-4
;; run: narrow 0 => i32:2
;; run: narrow 1 => i32:3
;; run: narrow 4096 => i32:4098
;; run: narrow 4503599627370495 => i32:65537
;; run: narrow 9007199254740993 => i32:3
;; run: narrow -9223372036854775808 => i32:2
;; run: narrow -1 => i32:65537
;; run: narrow 81985529216486895 => i32:52721
;; run: small32 0 => i32:7
;; run: small32 1 => i32:8
;; run: small32 2 => i32:9
;; run: small32 3 => i32:10
;; run: small32 5 => i32:12
;; run: small32 100 => i32:107
;; run: small32 255 => i32:262
;; run: small32 256 => i32:263
;; run: small32 321 => i32:328
;; run: small32 4095 => i32:4102
;; run: small32 65535 => i32:65542
;; run: small32 16777215 => i32:65542
;; run: small32 2147483647 => i32:65542
;; run: small32 -2147483648 => i32:7
;; run: small32 -1 => i32:65542
;; run: small32 -5 => i32:65538
;; run: negs 0 => i32:0
;; run: negs 1 => i32:-1
;; run: negs 2 => i32:-2
;; run: negs 3 => i32:-3
;; run: negs 5 => i32:-5
;; run: negs 100 => i32:-100
;; run: negs 255 => i32:-255
;; run: negs 256 => i32:0
;; run: negs 321 => i32:-65
;; run: negs 4095 => i32:-255
;; run: negs 65535 => i32:-255
;; run: negs 16777215 => i32:-255
;; run: negs 2147483647 => i32:-255
;; run: negs -2147483648 => i32:0
;; run: negs -1 => i32:-255
;; run: negs -5 => i32:-251
;; run: badf32 0 => i32:2
;; run: badf32 1 => i32:3
;; run: badf32 2 => i32:4
;; run: badf32 3 => i32:5
;; run: badf32 5 => i32:7
;; run: badf32 100 => i32:102
;; run: badf32 255 => i32:257
;; run: badf32 256 => i32:258
;; run: badf32 321 => i32:323
;; run: badf32 4095 => i32:4097
;; run: badf32 65535 => i32:65537
;; run: badf32 16777215 => i32:16777216
;; run: badf32 2147483647 => i32:16777216
;; run: badf32 -2147483648 => i32:2
;; run: badf32 -1 => i32:16777216
;; run: badf32 -5 => i32:16777213
;; run: half 0 => i32:0
;; run: half 1 => i32:1
;; run: half 2 => i32:2
;; run: half 3 => i32:3
;; run: half 5 => i32:5
;; run: half 100 => i32:100
;; run: half 255 => i32:255
;; run: half 256 => i32:0
;; run: half 321 => i32:65
;; run: half 4095 => i32:255
;; run: half 65535 => i32:255
;; run: half 16777215 => i32:255
;; run: half 2147483647 => i32:255
;; run: half -2147483648 => i32:0
;; run: half -1 => i32:255
;; run: half -5 => i32:251
;; run: anyi64 0 => i64:1
;; run: anyi64 1 => i64:2
;; run: anyi64 4096 => i64:4097
;; run: anyi64 4503599627370495 => i64:4503599627370496
;; run: anyi64 9007199254740993 => i64:9007199254740992
;; run: anyi64 -9223372036854775808 => i64:9223372036854775807
;; run: anyi64 -1 => i64:9223372036854775807
;; run: anyi64 81985529216486895 => i64:81985529216486896
;; run: sat31 0 => i32:1
;; run: sat31 1 => i32:2
;; run: sat31 2 => i32:3
;; run: sat31 3 => i32:4
;; run: sat31 5 => i32:6
;; run: sat31 100 => i32:101
;; run: sat31 255 => i32:256
;; run: sat31 256 => i32:257
;; run: sat31 321 => i32:322
;; run: sat31 4095 => i32:4096
;; run: sat31 65535 => i32:65536
;; run: sat31 16777215 => i32:16777216
;; run: sat31 2147483647 => i32:2147483647
;; run: sat31 -2147483648 => i32:1
;; run: sat31 -1 => i32:2147483647
;; run: sat31 -5 => i32:2147483644
;; run: negu 0 => i32:1
;; run: negu 1 => i32:2
;; run: negu 2 => i32:3
;; run: negu 3 => i32:4
;; run: negu 5 => i32:6
;; run: negu 100 => i32:101
;; run: negu 255 => i32:256
;; run: negu 256 => i32:257
;; run: negu 321 => i32:322
;; run: negu 4095 => i32:4096
;; run: negu 65535 => i32:65536
;; run: negu 16777215 => i32:16777216
;; run: negu 2147483647 => i32:-2147483648
;; run: negu -2147483648 => i32:0
;; run: negu -1 => i32:0
;; run: negu -5 => i32:0
;; run: negu64 0 => i64:1
;; run: negu64 1 => i64:2
;; run: negu64 2 => i64:3
;; run: negu64 3 => i64:4
;; run: negu64 5 => i64:6
;; run: negu64 100 => i64:101
;; run: negu64 255 => i64:256
;; run: negu64 256 => i64:257
;; run: negu64 321 => i64:322
;; run: negu64 4095 => i64:4096
;; run: negu64 65535 => i64:65536
;; run: negu64 16777215 => i64:16777216
;; run: negu64 2147483647 => i64:2147483648
;; run: negu64 -2147483648 => i64:0
;; run: negu64 -1 => i64:0
;; run: negu64 -5 => i64:0
;; run: subu 0 0 => i32:0
;; run: subu 1 5 => i32:0
;; run: subu 5 1 => i32:4
;; run: subu 255 15 => i32:240
;; run: subu 0 255 => i32:0
;; run: subu 128 255 => i32:0
;; run: subu 200 7 => i32:193
;; run: subu -1 -1 => i32:0
;; run: mulu 0 0 => i32:0
;; run: mulu 1 5 => i32:0
;; run: mulu 5 1 => i32:0
;; run: mulu 255 15 => i32:1905
;; run: mulu 0 255 => i32:0
;; run: mulu 128 255 => i32:0
;; run: mulu 200 7 => i32:504
;; run: mulu -1 -1 => i32:32385
;; run: negx 0 => i32:0
;; run: negx 1 => i32:0
;; run: negx 2 => i32:0
;; run: negx 3 => i32:0
;; run: negx 5 => i32:0
;; run: negx 100 => i32:0
;; run: negx 255 => i32:0
;; run: negx 256 => i32:0
;; run: negx 321 => i32:0
;; run: negx 4095 => i32:0
;; run: negx 65535 => i32:0
;; run: negx 16777215 => i32:0
;; run: negx 2147483647 => i32:0
;; run: negx -2147483648 => i32:0
;; run: negx -1 => i32:0
;; run: negx -5 => i32:0
;; run: subone 0 => i32:0
;; run: subone 1 => i32:0
;; run: subone 2 => i32:1
;; run: subone 3 => i32:2
;; run: subone 5 => i32:4
;; run: subone 100 => i32:99
;; run: subone 255 => i32:254
;; run: subone 256 => i32:0
;; run: subone 321 => i32:64
;; run: subone 4095 => i32:254
;; run: subone 65535 => i32:254
;; run: subone 16777215 => i32:254
;; run: subone 2147483647 => i32:254
;; run: subone -2147483648 => i32:0
;; run: subone -1 => i32:254
;; run: subone -5 => i32:250
;; run: edge53 0 => i64:0
;; run: edge53 1 => i64:28059810762433
;; run: edge53 2 => i64:0
;; run: edge53 3 => i64:28059810762433
;; run: edge53 5 => i64:28059810762433
;; run: edge53 100 => i64:1795827888795712
;; run: edge53 255 => i64:1823887699558145
;; run: edge53 256 => i64:7183311555182848
;; run: edge53 321 => i64:9007199254740992
;; run: edge53 4095 => i64:9007199254740992
;; run: edge53 65535 => i64:9007199254740992
;; run: edge53 16777215 => i64:9007199254740992
;; run: edge53 2147483647 => i64:9007199254740992
;; run: edge53 -2147483648 => i64:0
;; run: edge53 -1 => i64:9007199254740992
;; run: edge53 -5 => i64:9007199254740992
;; run: div 0 => i32:0
;; run: div 1 => i32:0
;; run: div 2 => i32:1
;; run: div 3 => i32:1
;; run: div 5 => i32:2
;; run: div 100 => i32:50
;; run: div 255 => i32:127
;; run: div 256 => i32:0
;; run: div 321 => i32:32
;; run: div 4095 => i32:127
;; run: div 65535 => i32:127
;; run: div 16777215 => i32:127
;; run: div 2147483647 => i32:127
;; run: div -2147483648 => i32:0
;; run: div -1 => i32:127
;; run: div -5 => i32:125
;; run: lt9 0 => i32:11
;; run: lt9 1 => i32:11
;; run: lt9 2 => i32:11
;; run: lt9 3 => i32:11
;; run: lt9 5 => i32:11
;; run: lt9 100 => i32:11
;; run: lt9 255 => i32:11
;; run: lt9 256 => i32:11
;; run: lt9 321 => i32:11
;; run: lt9 4095 => i32:11
;; run: lt9 65535 => i32:11
;; run: lt9 16777215 => i32:11
;; run: lt9 2147483647 => i32:11
;; run: lt9 -2147483648 => i32:11
;; run: lt9 -1 => i32:11
;; run: lt9 -5 => i32:11
;; run: lt8 0 => i32:21
;; run: lt8 1 => i32:21
;; run: lt8 2 => i32:21
;; run: lt8 3 => i32:20
;; run: lt8 5 => i32:21
;; run: lt8 100 => i32:21
;; run: lt8 255 => i32:20
;; run: lt8 256 => i32:21
;; run: lt8 321 => i32:21
;; run: lt8 4095 => i32:20
;; run: lt8 65535 => i32:20
;; run: lt8 16777215 => i32:20
;; run: lt8 2147483647 => i32:20
;; run: lt8 -2147483648 => i32:21
;; run: lt8 -1 => i32:20
;; run: lt8 -5 => i32:20
;; run: le8 0 => i32:31
;; run: le8 1 => i32:31
;; run: le8 2 => i32:31
;; run: le8 3 => i32:31
;; run: le8 5 => i32:31
;; run: le8 100 => i32:31
;; run: le8 255 => i32:31
;; run: le8 256 => i32:31
;; run: le8 321 => i32:31
;; run: le8 4095 => i32:31
;; run: le8 65535 => i32:31
;; run: le8 16777215 => i32:31
;; run: le8 2147483647 => i32:31
;; run: le8 -2147483648 => i32:31
;; run: le8 -1 => i32:31
;; run: le8 -5 => i32:31
;; run: le5 0 => i32:41
;; run: le5 1 => i32:40
;; run: le5 2 => i32:40
;; run: le5 3 => i32:40
;; run: le5 5 => i32:40
;; run: le5 100 => i32:41
;; run: le5 255 => i32:40
;; run: le5 256 => i32:41
;; run: le5 321 => i32:40
;; run: le5 4095 => i32:40
;; run: le5 65535 => i32:40
;; run: le5 16777215 => i32:40
;; run: le5 2147483647 => i32:40
;; run: le5 -2147483648 => i32:41
;; run: le5 -1 => i32:40
;; run: le5 -5 => i32:40
;; run: le7 0 => i32:51
;; run: le7 1 => i32:51
;; run: le7 2 => i32:51
;; run: le7 3 => i32:50
;; run: le7 5 => i32:51
;; run: le7 100 => i32:51
;; run: le7 255 => i32:50
;; run: le7 256 => i32:51
;; run: le7 321 => i32:51
;; run: le7 4095 => i32:50
;; run: le7 65535 => i32:50
;; run: le7 16777215 => i32:50
;; run: le7 2147483647 => i32:50
;; run: le7 -2147483648 => i32:51
;; run: le7 -1 => i32:50
;; run: le7 -5 => i32:50
;; run: gt4 0 => i32:61
;; run: gt4 1 => i32:61
;; run: gt4 2 => i32:61
;; run: gt4 3 => i32:61
;; run: gt4 5 => i32:61
;; run: gt4 100 => i32:61
;; run: gt4 255 => i32:61
;; run: gt4 256 => i32:61
;; run: gt4 321 => i32:61
;; run: gt4 4095 => i32:61
;; run: gt4 65535 => i32:61
;; run: gt4 16777215 => i32:61
;; run: gt4 2147483647 => i32:61
;; run: gt4 -2147483648 => i32:61
;; run: gt4 -1 => i32:61
;; run: gt4 -5 => i32:61
;; run: gt8 0 => i32:70
;; run: gt8 1 => i32:70
;; run: gt8 2 => i32:70
;; run: gt8 3 => i32:70
;; run: gt8 5 => i32:70
;; run: gt8 100 => i32:70
;; run: gt8 255 => i32:70
;; run: gt8 256 => i32:70
;; run: gt8 321 => i32:70
;; run: gt8 4095 => i32:70
;; run: gt8 65535 => i32:70
;; run: gt8 16777215 => i32:70
;; run: gt8 2147483647 => i32:70
;; run: gt8 -2147483648 => i32:70
;; run: gt8 -1 => i32:70
;; run: gt8 -5 => i32:70
;; run: gt5 0 => i32:80
;; run: gt5 1 => i32:81
;; run: gt5 2 => i32:81
;; run: gt5 3 => i32:81
;; run: gt5 5 => i32:81
;; run: gt5 100 => i32:80
;; run: gt5 255 => i32:81
;; run: gt5 256 => i32:80
;; run: gt5 321 => i32:81
;; run: gt5 4095 => i32:81
;; run: gt5 65535 => i32:81
;; run: gt5 16777215 => i32:81
;; run: gt5 2147483647 => i32:81
;; run: gt5 -2147483648 => i32:80
;; run: gt5 -1 => i32:81
;; run: gt5 -5 => i32:81
;; run: ge5 0 => i32:91
;; run: ge5 1 => i32:91
;; run: ge5 2 => i32:91
;; run: ge5 3 => i32:91
;; run: ge5 5 => i32:91
;; run: ge5 100 => i32:91
;; run: ge5 255 => i32:91
;; run: ge5 256 => i32:91
;; run: ge5 321 => i32:91
;; run: ge5 4095 => i32:91
;; run: ge5 65535 => i32:91
;; run: ge5 16777215 => i32:91
;; run: ge5 2147483647 => i32:91
;; run: ge5 -2147483648 => i32:91
;; run: ge5 -1 => i32:91
;; run: ge5 -5 => i32:91
;; run: ge8 0 => i32:100
;; run: ge8 1 => i32:100
;; run: ge8 2 => i32:100
;; run: ge8 3 => i32:101
;; run: ge8 5 => i32:100
;; run: ge8 100 => i32:100
;; run: ge8 255 => i32:101
;; run: ge8 256 => i32:100
;; run: ge8 321 => i32:100
;; run: ge8 4095 => i32:101
;; run: ge8 65535 => i32:101
;; run: ge8 16777215 => i32:101
;; run: ge8 2147483647 => i32:101
;; run: ge8 -2147483648 => i32:100
;; run: ge8 -1 => i32:101
;; run: ge8 -5 => i32:101
;; run: ge6 0 => i32:110
;; run: ge6 1 => i32:111
;; run: ge6 2 => i32:111
;; run: ge6 3 => i32:111
;; run: ge6 5 => i32:111
;; run: ge6 100 => i32:110
;; run: ge6 255 => i32:111
;; run: ge6 256 => i32:110
;; run: ge6 321 => i32:111
;; run: ge6 4095 => i32:111
;; run: ge6 65535 => i32:111
;; run: ge6 16777215 => i32:111
;; run: ge6 2147483647 => i32:111
;; run: ge6 -2147483648 => i32:110
;; run: ge6 -1 => i32:111
;; run: ge6 -5 => i32:111
;; run: ltlow 0 => i32:120
;; run: ltlow 1 => i32:120
;; run: ltlow 2 => i32:120
;; run: ltlow 3 => i32:120
;; run: ltlow 5 => i32:120
;; run: ltlow 100 => i32:120
;; run: ltlow 255 => i32:120
;; run: ltlow 256 => i32:120
;; run: ltlow 321 => i32:120
;; run: ltlow 4095 => i32:120
;; run: ltlow 65535 => i32:120
;; run: ltlow 16777215 => i32:120
;; run: ltlow 2147483647 => i32:120
;; run: ltlow -2147483648 => i32:120
;; run: ltlow -1 => i32:120
;; run: ltlow -5 => i32:120
;; run: gehigh 0 => i32:130
;; run: gehigh 1 => i32:130
;; run: gehigh 2 => i32:130
;; run: gehigh 3 => i32:130
;; run: gehigh 5 => i32:130
;; run: gehigh 100 => i32:130
;; run: gehigh 255 => i32:130
;; run: gehigh 256 => i32:130
;; run: gehigh 321 => i32:130
;; run: gehigh 4095 => i32:130
;; run: gehigh 65535 => i32:130
;; run: gehigh 16777215 => i32:130
;; run: gehigh 2147483647 => i32:130
;; run: gehigh -2147483648 => i32:130
;; run: gehigh -1 => i32:130
;; run: gehigh -5 => i32:130
;; run: eqfrac 0 => i32:140
;; run: eqfrac 1 => i32:140
;; run: eqfrac 2 => i32:140
;; run: eqfrac 3 => i32:140
;; run: eqfrac 5 => i32:140
;; run: eqfrac 100 => i32:140
;; run: eqfrac 255 => i32:140
;; run: eqfrac 256 => i32:140
;; run: eqfrac 321 => i32:140
;; run: eqfrac 4095 => i32:140
;; run: eqfrac 65535 => i32:140
;; run: eqfrac 16777215 => i32:140
;; run: eqfrac 2147483647 => i32:140
;; run: eqfrac -2147483648 => i32:140
;; run: eqfrac -1 => i32:140
;; run: eqfrac -5 => i32:140
;; run: eqout 0 => i32:150
;; run: eqout 1 => i32:150
;; run: eqout 2 => i32:150
;; run: eqout 3 => i32:150
;; run: eqout 5 => i32:150
;; run: eqout 100 => i32:150
;; run: eqout 255 => i32:150
;; run: eqout 256 => i32:150
;; run: eqout 321 => i32:150
;; run: eqout 4095 => i32:150
;; run: eqout 65535 => i32:150
;; run: eqout 16777215 => i32:150
;; run: eqout 2147483647 => i32:150
;; run: eqout -2147483648 => i32:150
;; run: eqout -1 => i32:150
;; run: eqout -5 => i32:150
;; run: nein 0 => i32:161
;; run: nein 1 => i32:160
;; run: nein 2 => i32:161
;; run: nein 3 => i32:161
;; run: nein 5 => i32:160
;; run: nein 100 => i32:161
;; run: nein 255 => i32:161
;; run: nein 256 => i32:161
;; run: nein 321 => i32:160
;; run: nein 4095 => i32:161
;; run: nein 65535 => i32:161
;; run: nein 16777215 => i32:161
;; run: nein 2147483647 => i32:161
;; run: nein -2147483648 => i32:161
;; run: nein -1 => i32:161
;; run: nein -5 => i32:161
;; run: neout 0 => i32:171
;; run: neout 1 => i32:171
;; run: neout 2 => i32:171
;; run: neout 3 => i32:171
;; run: neout 5 => i32:171
;; run: neout 100 => i32:171
;; run: neout 255 => i32:171
;; run: neout 256 => i32:171
;; run: neout 321 => i32:171
;; run: neout 4095 => i32:171
;; run: neout 65535 => i32:171
;; run: neout 16777215 => i32:171
;; run: neout 2147483647 => i32:171
;; run: neout -2147483648 => i32:171
;; run: neout -1 => i32:171
;; run: neout -5 => i32:171
;; run: eqpoint 0 => i32:181
;; run: eqpoint 1 => i32:181
;; run: eqpoint 2 => i32:181
;; run: eqpoint 3 => i32:181
;; run: eqpoint 5 => i32:181
;; run: eqpoint 100 => i32:181
;; run: eqpoint 255 => i32:181
;; run: eqpoint 256 => i32:181
;; run: eqpoint 321 => i32:181
;; run: eqpoint 4095 => i32:181
;; run: eqpoint 65535 => i32:181
;; run: eqpoint 16777215 => i32:181
;; run: eqpoint 2147483647 => i32:181
;; run: eqpoint -2147483648 => i32:181
;; run: eqpoint -1 => i32:181
;; run: eqpoint -5 => i32:181
;; run: swapgt 0 => i32:191
;; run: swapgt 1 => i32:191
;; run: swapgt 2 => i32:191
;; run: swapgt 3 => i32:191
;; run: swapgt 5 => i32:191
;; run: swapgt 100 => i32:191
;; run: swapgt 255 => i32:191
;; run: swapgt 256 => i32:191
;; run: swapgt 321 => i32:191
;; run: swapgt 4095 => i32:191
;; run: swapgt 65535 => i32:191
;; run: swapgt 16777215 => i32:191
;; run: swapgt 2147483647 => i32:191
;; run: swapgt -2147483648 => i32:191
;; run: swapgt -1 => i32:191
;; run: swapgt -5 => i32:191
;; run: swaple 0 => i32:200
;; run: swaple 1 => i32:201
;; run: swaple 2 => i32:201
;; run: swaple 3 => i32:201
;; run: swaple 5 => i32:201
;; run: swaple 100 => i32:200
;; run: swaple 255 => i32:201
;; run: swaple 256 => i32:200
;; run: swaple 321 => i32:201
;; run: swaple 4095 => i32:201
;; run: swaple 65535 => i32:201
;; run: swaple 16777215 => i32:201
;; run: swaple 2147483647 => i32:201
;; run: swaple -2147483648 => i32:200
;; run: swaple -1 => i32:201
;; run: swaple -5 => i32:201
;; run: swaplt 0 => i32:211
;; run: swaplt 1 => i32:211
;; run: swaplt 2 => i32:211
;; run: swaplt 3 => i32:211
;; run: swaplt 5 => i32:211
;; run: swaplt 100 => i32:211
;; run: swaplt 255 => i32:211
;; run: swaplt 256 => i32:211
;; run: swaplt 321 => i32:211
;; run: swaplt 4095 => i32:211
;; run: swaplt 65535 => i32:211
;; run: swaplt 16777215 => i32:211
;; run: swaplt 2147483647 => i32:211
;; run: swaplt -2147483648 => i32:211
;; run: swaplt -1 => i32:211
;; run: swaplt -5 => i32:211
;; run: swapge 0 => i32:220
;; run: swapge 1 => i32:220
;; run: swapge 2 => i32:220
;; run: swapge 3 => i32:220
;; run: swapge 5 => i32:220
;; run: swapge 100 => i32:220
;; run: swapge 255 => i32:220
;; run: swapge 256 => i32:220
;; run: swapge 321 => i32:220
;; run: swapge 4095 => i32:220
;; run: swapge 65535 => i32:220
;; run: swapge 16777215 => i32:220
;; run: swapge 2147483647 => i32:220
;; run: swapge -2147483648 => i32:220
;; run: swapge -1 => i32:220
;; run: swapge -5 => i32:220
;; run: nanlt 0 => i32:230
;; run: nanlt 1 => i32:230
;; run: nanlt 2 => i32:230
;; run: nanlt 3 => i32:230
;; run: nanlt 5 => i32:230
;; run: nanlt 100 => i32:230
;; run: nanlt 255 => i32:230
;; run: nanlt 256 => i32:230
;; run: nanlt 321 => i32:230
;; run: nanlt 4095 => i32:230
;; run: nanlt 65535 => i32:230
;; run: nanlt 16777215 => i32:230
;; run: nanlt 2147483647 => i32:230
;; run: nanlt -2147483648 => i32:230
;; run: nanlt -1 => i32:230
;; run: nanlt -5 => i32:230
;; run: nanne 0 => i32:241
;; run: nanne 1 => i32:241
;; run: nanne 2 => i32:241
;; run: nanne 3 => i32:241
;; run: nanne 5 => i32:241
;; run: nanne 100 => i32:241
;; run: nanne 255 => i32:241
;; run: nanne 256 => i32:241
;; run: nanne 321 => i32:241
;; run: nanne 4095 => i32:241
;; run: nanne 65535 => i32:241
;; run: nanne 16777215 => i32:241
;; run: nanne 2147483647 => i32:241
;; run: nanne -2147483648 => i32:241
;; run: nanne -1 => i32:241
;; run: nanne -5 => i32:241
;; run: selfeq 0 => i32:251
;; run: selfeq 1 => i32:251
;; run: selfeq 2 => i32:251
;; run: selfeq 3 => i32:251
;; run: selfeq 5 => i32:251
;; run: selfeq 100 => i32:251
;; run: selfeq 255 => i32:251
;; run: selfeq 256 => i32:251
;; run: selfeq 321 => i32:251
;; run: selfeq 4095 => i32:251
;; run: selfeq 65535 => i32:251
;; run: selfeq 16777215 => i32:251
;; run: selfeq 2147483647 => i32:251
;; run: selfeq -2147483648 => i32:251
;; run: selfeq -1 => i32:251
;; run: selfeq -5 => i32:251
;; run: selfge 0 => i32:261
;; run: selfge 1 => i32:261
;; run: selfge 2 => i32:261
;; run: selfge 3 => i32:261
;; run: selfge 5 => i32:261
;; run: selfge 100 => i32:261
;; run: selfge 255 => i32:261
;; run: selfge 256 => i32:261
;; run: selfge 321 => i32:261
;; run: selfge 4095 => i32:261
;; run: selfge 65535 => i32:261
;; run: selfge 16777215 => i32:261
;; run: selfge 2147483647 => i32:261
;; run: selfge -2147483648 => i32:261
;; run: selfge -1 => i32:261
;; run: selfge -5 => i32:261
;; run: selfgt 0 => i32:270
;; run: selfgt 1 => i32:270
;; run: selfgt 2 => i32:270
;; run: selfgt 3 => i32:270
;; run: selfgt 5 => i32:270
;; run: selfgt 100 => i32:270
;; run: selfgt 255 => i32:270
;; run: selfgt 256 => i32:270
;; run: selfgt 321 => i32:270
;; run: selfgt 4095 => i32:270
;; run: selfgt 65535 => i32:270
;; run: selfgt 16777215 => i32:270
;; run: selfgt 2147483647 => i32:270
;; run: selfgt -2147483648 => i32:270
;; run: selfgt -1 => i32:270
;; run: selfgt -5 => i32:270
;; run: selfle 0 => i32:281
;; run: selfle 1 => i32:281
;; run: selfle 2 => i32:281
;; run: selfle 3 => i32:281
;; run: selfle 5 => i32:281
;; run: selfle 100 => i32:281
;; run: selfle 255 => i32:281
;; run: selfle 256 => i32:281
;; run: selfle 321 => i32:281
;; run: selfle 4095 => i32:281
;; run: selfle 65535 => i32:281
;; run: selfle 16777215 => i32:281
;; run: selfle 2147483647 => i32:281
;; run: selfle -2147483648 => i32:281
;; run: selfle -1 => i32:281
;; run: selfle -5 => i32:281
;; run: f32lt 0 => i32:291
;; run: f32lt 1 => i32:291
;; run: f32lt 2 => i32:291
;; run: f32lt 3 => i32:291
;; run: f32lt 5 => i32:291
;; run: f32lt 100 => i32:291
;; run: f32lt 255 => i32:291
;; run: f32lt 256 => i32:291
;; run: f32lt 321 => i32:291
;; run: f32lt 4095 => i32:291
;; run: f32lt 65535 => i32:291
;; run: f32lt 16777215 => i32:291
;; run: f32lt 2147483647 => i32:291
;; run: f32lt -2147483648 => i32:291
;; run: f32lt -1 => i32:291
;; run: f32lt -5 => i32:291
;; run: inexact 0 => i32:301
;; run: inexact 1 => i32:301
;; run: inexact 2 => i32:301
;; run: inexact 3 => i32:301
;; run: inexact 5 => i32:301
;; run: inexact 100 => i32:301
;; run: inexact 255 => i32:301
;; run: inexact 256 => i32:301
;; run: inexact 321 => i32:301
;; run: inexact 4095 => i32:301
;; run: inexact 65535 => i32:301
;; run: inexact 16777215 => i32:301
;; run: inexact 2147483647 => i32:301
;; run: inexact -2147483648 => i32:301
;; run: inexact -1 => i32:301
;; run: inexact -5 => i32:301

(module
  (func $bigstruct (export "bigstruct") (param i32) (result i32)
    (i32.trunc_sat_f64_s (f64.add (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.const 9))))
  (func $mixed (export "mixed") (param i32 i32) (result i32)
    (i32.trunc_f64_s (f64.mul (f64.sub (f64.convert_i32_s (i32.and (local.get 0) (i32.const 255))) (f64.const 100)) (f64.neg (f64.convert_i32_u (i32.and (local.get 1) (i32.const 15)))))))
  (func $constfirst (export "constfirst") (param i32) (result i32)
    (i32.trunc_sat_f64_s (f64.sub (f64.const 1000) (f64.convert_i32_u (i32.and (local.get 0) (i32.const 4095))))))
  (func $wide52 (export "wide52") (param i64) (result i64)
    (i64.trunc_sat_f64_u (f64.add (f64.convert_i64_u (i64.shr_u (local.get 0) (i64.const 12))) (f64.const 1))))
  (func $wideu (export "wideu") (param i32) (result i64)
    (i64.trunc_sat_f64_s (f64.add (f64.convert_i32_u (local.get 0)) (f64.const 1))))
  (func $wides (export "wides") (param i32) (result i64)
    (i64.trunc_sat_f64_s (f64.add (f64.convert_i32_s (local.get 0)) (f64.const 1))))
  (func $narrow (export "narrow") (param i64) (result i32)
    (i32.trunc_sat_f64_s (f64.add (f64.convert_i64_u (i64.and (local.get 0) (i64.const 65535))) (f64.const 2))))
  (func $small32 (export "small32") (param i32) (result i32)
    (i32.trunc_sat_f32_s (f32.add (f32.convert_i32_u (i32.and (local.get 0) (i32.const 65535))) (f32.const 7))))
  (func $negs (export "negs") (param i32) (result i32)
    (i32.trunc_sat_f64_s (f64.neg (f64.convert_i32_u (i32.and (local.get 0) (i32.const 255))))))
  (func $badf32 (export "badf32") (param i32) (result i32)
    (i32.trunc_sat_f32_s (f32.add (f32.convert_i32_u (i32.and (local.get 0) (i32.const 0xffffff))) (f32.const 2))))
  (func $half (export "half") (param i32) (result i32)
    (i32.trunc_sat_f64_s (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 255))) (f64.const 0.5))))
  (func $anyi64 (export "anyi64") (param i64) (result i64)
    (i64.trunc_sat_f64_s (f64.add (f64.convert_i64_u (local.get 0)) (f64.const 1))))
  (func $sat31 (export "sat31") (param i32) (result i32)
    (i32.trunc_sat_f64_s (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 0x7fffffff))) (f64.const 1))))
  (func $negu (export "negu") (param i32) (result i32)
    (i32.trunc_sat_f64_u (f64.add (f64.convert_i32_s (local.get 0)) (f64.const 1))))
  (func $negu64 (export "negu64") (param i32) (result i64)
    (i64.trunc_sat_f64_u (f64.add (f64.convert_i32_s (local.get 0)) (f64.const 1))))
  (func $subu (export "subu") (param i32 i32) (result i32)
    (i32.trunc_sat_f64_u (f64.sub (f64.convert_i32_u (i32.and (local.get 0) (i32.const 255))) (f64.convert_i32_u (i32.and (local.get 1) (i32.const 255))))))
  (func $mulu (export "mulu") (param i32 i32) (result i32)
    (i32.trunc_sat_f64_u (f64.mul (f64.sub (f64.convert_i32_u (i32.and (local.get 0) (i32.const 255))) (f64.const 128)) (f64.convert_i32_u (i32.and (local.get 1) (i32.const 255))))))
  (func $negx (export "negx") (param i32) (result i32)
    (i32.trunc_sat_f64_u (f64.neg (f64.convert_i32_u (i32.and (local.get 0) (i32.const 255))))))
  (func $subone (export "subone") (param i32) (result i32)
    (i32.trunc_sat_f64_u (f64.sub (f64.convert_i32_u (i32.and (local.get 0) (i32.const 255))) (f64.const 1))))
  (func $edge53 (export "edge53") (param i32) (result i64)
    (i64.trunc_sat_f64_s (f64.mul (f64.convert_i32_u (i32.and (local.get 0) (i32.const 321))) (f64.const 28059810762433))))
  (func $div (export "div") (param i32) (result i32)
    (i32.trunc_sat_f64_s (f64.div (f64.convert_i32_u (i32.and (local.get 0) (i32.const 255))) (f64.const 2))))
  (func $lt9 (export "lt9") (param i32) (result i32)
    (i32.add (f64.lt (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.const 9)) (i32.const 10)))
  (func $lt8 (export "lt8") (param i32) (result i32)
    (i32.add (f64.lt (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.const 8)) (i32.const 20)))
  (func $le8 (export "le8") (param i32) (result i32)
    (i32.add (f64.le (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.const 8)) (i32.const 30)))
  (func $le5 (export "le5") (param i32) (result i32)
    (i32.add (f64.le (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.const 5)) (i32.const 40)))
  (func $le7 (export "le7") (param i32) (result i32)
    (i32.add (f64.le (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.const 7)) (i32.const 50)))
  (func $gt4 (export "gt4") (param i32) (result i32)
    (i32.add (f64.gt (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.const 4)) (i32.const 60)))
  (func $gt8 (export "gt8") (param i32) (result i32)
    (i32.add (f64.gt (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.const 8)) (i32.const 70)))
  (func $gt5 (export "gt5") (param i32) (result i32)
    (i32.add (f64.gt (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.const 5)) (i32.const 80)))
  (func $ge5 (export "ge5") (param i32) (result i32)
    (i32.add (f64.ge (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.const 5)) (i32.const 90)))
  (func $ge8 (export "ge8") (param i32) (result i32)
    (i32.add (f64.ge (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.const 8)) (i32.const 100)))
  (func $ge6 (export "ge6") (param i32) (result i32)
    (i32.add (f64.ge (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.const 6)) (i32.const 110)))
  (func $ltlow (export "ltlow") (param i32) (result i32)
    (i32.add (f64.lt (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.const 5)) (i32.const 120)))
  (func $gehigh (export "gehigh") (param i32) (result i32)
    (i32.add (f64.ge (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.const 9)) (i32.const 130)))
  (func $eqfrac (export "eqfrac") (param i32) (result i32)
    (i32.add (f64.eq (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.const 6.5)) (i32.const 140)))
  (func $eqout (export "eqout") (param i32) (result i32)
    (i32.add (f64.eq (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.const 20)) (i32.const 150)))
  (func $nein (export "nein") (param i32) (result i32)
    (i32.add (f64.ne (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.const 6)) (i32.const 160)))
  (func $neout (export "neout") (param i32) (result i32)
    (i32.add (f64.ne (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.const 4)) (i32.const 170)))
  (func $eqpoint (export "eqpoint") (param i32) (result i32)
    (i32.add (f64.eq (f64.mul (f64.convert_i32_u (i32.and (local.get 0) (i32.const 1))) (f64.const 0)) (f64.const 0)) (i32.const 180)))
  (func $swapgt (export "swapgt") (param i32) (result i32)
    (i32.add (f64.gt (f64.const 9) (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5))) (i32.const 190)))
  (func $swaple (export "swaple") (param i32) (result i32)
    (i32.add (f64.le (f64.const 6) (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5))) (i32.const 200)))
  (func $swaplt (export "swaplt") (param i32) (result i32)
    (i32.add (f64.lt (f64.const 4) (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5))) (i32.const 210)))
  (func $swapge (export "swapge") (param i32) (result i32)
    (i32.add (f64.ge (f64.const 4) (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5))) (i32.const 220)))
  (func $nanlt (export "nanlt") (param i32) (result i32)
    (i32.add (f64.lt (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.const nan)) (i32.const 230)))
  (func $nanne (export "nanne") (param i32) (result i32)
    (i32.add (f64.ne (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.const nan)) (i32.const 240)))
  (func $selfeq (export "selfeq") (param i32) (result i32)
    (i32.add (f64.eq (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5))) (i32.const 250)))
  (func $selfge (export "selfge") (param i32) (result i32)
    (i32.add (f64.ge (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5))) (i32.const 260)))
  (func $selfgt (export "selfgt") (param i32) (result i32)
    (i32.add (f64.gt (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5)) (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5))) (i32.const 270)))
  (func $selfle (export "selfle") (param i32) (result i32)
    (i32.add (f64.le (f64.reinterpret_i64 (i64.extend_i32_u (local.get 0))) (f64.reinterpret_i64 (i64.extend_i32_u (local.get 0)))) (i32.const 280)))
  (func $f32lt (export "f32lt") (param i32) (result i32)
    (i32.add (f32.lt (f32.add (f32.convert_i32_u (i32.and (local.get 0) (i32.const 255))) (f32.const 1)) (f32.const 300)) (i32.const 290)))
  (func $inexact (export "inexact") (param i32) (result i32)
    (i32.add (f64.lt (f64.add (f64.convert_i32_u (i32.and (local.get 0) (i32.const 3))) (f64.const 5.5)) (f64.const 9)) (i32.const 300)))
)
