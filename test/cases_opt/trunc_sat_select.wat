;; A tree of selects that works out again what a saturating truncation of a
;; float x says is that truncation (opt.c select_trunc_sat): Go's conversion
;; of a float to an int as TinyGo writes it -- the truncation, then x against
;; the int's bounds and x == x choosing between it, the bounds and 0 -- for
;; f64 and f32 to i32, f64 to i64, and unsigned, whose code then has no
;; compare left; NaN tested under eqz; the bounds by lt and gt, NaN by ne;
;; and trees that only say what the truncation says between -1 and 1, by lt
;; and gt or le and ge, f64 and f32, each compare's point decided the right
;; way round.  Not when a bound is another, NaN would give another value,
;; or NaN is not tested for: wrongbound, wrongnan and nonan keep their tree,
;; and otherx compares a different float from the one it truncates; nor
;; 7 between -1 and 1 (f32), at most 5 (the same as the truncation at 5,
;; not above), or a compare with a NaN constant.
;;
;; The answers come from the trees' own wasm semantics, worked out in Python
;; for NaN, the infinities, the zeros, fractions, and both sides of every
;; bound; arguments are the floats' bits.
;;
;; shape: s32 fcmp=0
;; shape: s32f fcmp=0
;; shape: s64 fcmp=0
;; shape: u32 fcmp=0
;;
;; run: s32 9221120237041090560 => i32:0
;; run: s32 9218868437227405312 => i32:2147483647
;; run: s32 18442240474082181120 => i32:-2147483648
;; run: s32 0 => i32:0
;; run: s32 9223372036854775808 => i32:0
;; run: s32 4602678819172646912 => i32:0
;; run: s32 13826050856027422720 => i32:0
;; run: s32 4609434218613702656 => i32:1
;; run: s32 13832806255468478464 => i32:-1
;; run: s32 4620681958683060470 => i32:7
;; run: s32 4746794004532690944 => i32:2147483000
;; run: s32 4746794007242211328 => i32:2147483646
;; run: s32 4746794007244308480 => i32:2147483647
;; run: s32 4746794007246405632 => i32:2147483647
;; run: s32 4746794007248502784 => i32:2147483647
;; run: s32 4746794007250599936 => i32:2147483647
;; run: s32 13970166044103278592 => i32:-2147483648
;; run: s32 13970166044104327168 => i32:-2147483648
;; run: s32 13970166044105375744 => i32:-2147483648
;; run: s32 13970166044106424320 => i32:-2147483648
;; run: s32 4751297606873776128 => i32:2147483647
;; run: s32 4751297606874824704 => i32:2147483647
;; run: s32 4751297606875873280 => i32:2147483647
;; run: s32 4756540486875873280 => i32:2147483647
;; run: s32 13979912523730649088 => i32:-2147483648
;; run: s32 4890886371069617664 => i32:2147483647
;; run: s32 14114258407924393472 => i32:-2147483648
;; run: s32 4890909195324358656 => i32:2147483647
;; run: s32 14114281232179134464 => i32:-2147483648
;; run: s32 9094988921128908188 => i32:2147483647
;; run: s32 18318360957983683996 => i32:-2147483648
;; run: s32f 2143289344 => i32:0
;; run: s32f 2139095040 => i32:2147483647
;; run: s32f 4286578688 => i32:-2147483648
;; run: s32f 0 => i32:0
;; run: s32f 2147483648 => i32:0
;; run: s32f 1056964608 => i32:0
;; run: s32f 3204448256 => i32:0
;; run: s32f 1069547520 => i32:1
;; run: s32f 3217031168 => i32:-1
;; run: s32f 1090498068 => i32:7
;; run: s32f 1325400059 => i32:2147483008
;; run: s32f 1325400064 => i32:2147483647
;; run: s32f 1325400064 => i32:2147483647
;; run: s32f 1325400064 => i32:2147483647
;; run: s32f 1325400064 => i32:2147483647
;; run: s32f 1325400064 => i32:2147483647
;; run: s32f 3472883712 => i32:-2147483648
;; run: s32f 3472883712 => i32:-2147483648
;; run: s32f 3472883712 => i32:-2147483648
;; run: s32f 3472883712 => i32:-2147483648
;; run: s32f 1333788672 => i32:2147483647
;; run: s32f 1333788672 => i32:2147483647
;; run: s32f 1333788672 => i32:2147483647
;; run: s32f 1343554297 => i32:2147483647
;; run: s32f 3491037945 => i32:-2147483648
;; run: s32f 1593793007 => i32:2147483647
;; run: s32f 3741276655 => i32:-2147483648
;; run: s32f 1593835520 => i32:2147483647
;; run: s32f 3741319168 => i32:-2147483648
;; run: s32f 2139095040 => i32:2147483647
;; run: s32f 4286578688 => i32:-2147483648
;; run: s64 9221120237041090560 => i64:0
;; run: s64 9218868437227405312 => i64:9223372036854775807
;; run: s64 18442240474082181120 => i64:-9223372036854775808
;; run: s64 0 => i64:0
;; run: s64 9223372036854775808 => i64:0
;; run: s64 4602678819172646912 => i64:0
;; run: s64 13826050856027422720 => i64:0
;; run: s64 4609434218613702656 => i64:1
;; run: s64 13832806255468478464 => i64:-1
;; run: s64 4620681958683060470 => i64:7
;; run: s64 4746794004532690944 => i64:2147483000
;; run: s64 4746794007242211328 => i64:2147483646
;; run: s64 4746794007244308480 => i64:2147483647
;; run: s64 4746794007246405632 => i64:2147483647
;; run: s64 4746794007248502784 => i64:2147483648
;; run: s64 4746794007250599936 => i64:2147483649
;; run: s64 13970166044103278592 => i64:-2147483648
;; run: s64 13970166044104327168 => i64:-2147483648
;; run: s64 13970166044105375744 => i64:-2147483649
;; run: s64 13970166044106424320 => i64:-2147483649
;; run: s64 4751297606873776128 => i64:4294967295
;; run: s64 4751297606874824704 => i64:4294967295
;; run: s64 4751297606875873280 => i64:4294967296
;; run: s64 4756540486875873280 => i64:10000000000
;; run: s64 13979912523730649088 => i64:-10000000000
;; run: s64 4890886371069617664 => i64:9200000000000000000
;; run: s64 14114258407924393472 => i64:-9200000000000000000
;; run: s64 4890909195324358656 => i64:9223372036854775807
;; run: s64 14114281232179134464 => i64:-9223372036854775808
;; run: s64 9094988921128908188 => i64:9223372036854775807
;; run: s64 18318360957983683996 => i64:-9223372036854775808
;; run: u32 9221120237041090560 => i32:0
;; run: u32 9218868437227405312 => i32:-1
;; run: u32 18442240474082181120 => i32:0
;; run: u32 0 => i32:0
;; run: u32 9223372036854775808 => i32:0
;; run: u32 4602678819172646912 => i32:0
;; run: u32 13826050856027422720 => i32:0
;; run: u32 4609434218613702656 => i32:1
;; run: u32 13832806255468478464 => i32:0
;; run: u32 4620681958683060470 => i32:7
;; run: u32 4746794004532690944 => i32:2147483000
;; run: u32 4746794007242211328 => i32:2147483646
;; run: u32 4746794007244308480 => i32:2147483647
;; run: u32 4746794007246405632 => i32:2147483647
;; run: u32 4746794007248502784 => i32:-2147483648
;; run: u32 4746794007250599936 => i32:-2147483647
;; run: u32 13970166044103278592 => i32:0
;; run: u32 13970166044104327168 => i32:0
;; run: u32 13970166044105375744 => i32:0
;; run: u32 13970166044106424320 => i32:0
;; run: u32 4751297606873776128 => i32:-1
;; run: u32 4751297606874824704 => i32:-1
;; run: u32 4751297606875873280 => i32:-1
;; run: u32 4756540486875873280 => i32:-1
;; run: u32 13979912523730649088 => i32:0
;; run: u32 4890886371069617664 => i32:-1
;; run: u32 14114258407924393472 => i32:0
;; run: u32 4890909195324358656 => i32:-1
;; run: u32 14114281232179134464 => i32:0
;; run: u32 9094988921128908188 => i32:-1
;; run: u32 18318360957983683996 => i32:0
;; run: wrongbound 9221120237041090560 => i32:0
;; run: wrongbound 9218868437227405312 => i32:2147483647
;; run: wrongbound 18442240474082181120 => i32:-2147483648
;; run: wrongbound 0 => i32:0
;; run: wrongbound 9223372036854775808 => i32:0
;; run: wrongbound 4602678819172646912 => i32:0
;; run: wrongbound 13826050856027422720 => i32:0
;; run: wrongbound 4609434218613702656 => i32:1
;; run: wrongbound 13832806255468478464 => i32:-1
;; run: wrongbound 4620681958683060470 => i32:7
;; run: wrongbound 4746794004532690944 => i32:2147483647
;; run: wrongbound 4746794007242211328 => i32:2147483647
;; run: wrongbound 4746794007244308480 => i32:2147483647
;; run: wrongbound 4746794007246405632 => i32:2147483647
;; run: wrongbound 4746794007248502784 => i32:2147483647
;; run: wrongbound 4746794007250599936 => i32:2147483647
;; run: wrongbound 13970166044103278592 => i32:-2147483648
;; run: wrongbound 13970166044104327168 => i32:-2147483648
;; run: wrongbound 13970166044105375744 => i32:-2147483648
;; run: wrongbound 13970166044106424320 => i32:-2147483648
;; run: wrongbound 4751297606873776128 => i32:2147483647
;; run: wrongbound 4751297606874824704 => i32:2147483647
;; run: wrongbound 4751297606875873280 => i32:2147483647
;; run: wrongbound 4756540486875873280 => i32:2147483647
;; run: wrongbound 13979912523730649088 => i32:-2147483648
;; run: wrongbound 4890886371069617664 => i32:2147483647
;; run: wrongbound 14114258407924393472 => i32:-2147483648
;; run: wrongbound 4890909195324358656 => i32:2147483647
;; run: wrongbound 14114281232179134464 => i32:-2147483648
;; run: wrongbound 9094988921128908188 => i32:2147483647
;; run: wrongbound 18318360957983683996 => i32:-2147483648
;; run: wrongnan 9221120237041090560 => i32:1
;; run: wrongnan 9218868437227405312 => i32:2147483647
;; run: wrongnan 18442240474082181120 => i32:-2147483648
;; run: wrongnan 0 => i32:0
;; run: wrongnan 9223372036854775808 => i32:0
;; run: wrongnan 4602678819172646912 => i32:0
;; run: wrongnan 13826050856027422720 => i32:0
;; run: wrongnan 4609434218613702656 => i32:1
;; run: wrongnan 13832806255468478464 => i32:-1
;; run: wrongnan 4620681958683060470 => i32:7
;; run: wrongnan 4746794004532690944 => i32:2147483000
;; run: wrongnan 4746794007242211328 => i32:2147483646
;; run: wrongnan 4746794007244308480 => i32:2147483647
;; run: wrongnan 4746794007246405632 => i32:2147483647
;; run: wrongnan 4746794007248502784 => i32:2147483647
;; run: wrongnan 4746794007250599936 => i32:2147483647
;; run: wrongnan 13970166044103278592 => i32:-2147483648
;; run: wrongnan 13970166044104327168 => i32:-2147483648
;; run: wrongnan 13970166044105375744 => i32:-2147483648
;; run: wrongnan 13970166044106424320 => i32:-2147483648
;; run: wrongnan 4751297606873776128 => i32:2147483647
;; run: wrongnan 4751297606874824704 => i32:2147483647
;; run: wrongnan 4751297606875873280 => i32:2147483647
;; run: wrongnan 4756540486875873280 => i32:2147483647
;; run: wrongnan 13979912523730649088 => i32:-2147483648
;; run: wrongnan 4890886371069617664 => i32:2147483647
;; run: wrongnan 14114258407924393472 => i32:-2147483648
;; run: wrongnan 4890909195324358656 => i32:2147483647
;; run: wrongnan 14114281232179134464 => i32:-2147483648
;; run: wrongnan 9094988921128908188 => i32:2147483647
;; run: wrongnan 18318360957983683996 => i32:-2147483648
;; run: nonan 9221120237041090560 => i32:-2147483648
;; run: nonan 9218868437227405312 => i32:2147483647
;; run: nonan 18442240474082181120 => i32:-2147483648
;; run: nonan 0 => i32:0
;; run: nonan 9223372036854775808 => i32:0
;; run: nonan 4602678819172646912 => i32:0
;; run: nonan 13826050856027422720 => i32:0
;; run: nonan 4609434218613702656 => i32:1
;; run: nonan 13832806255468478464 => i32:-1
;; run: nonan 4620681958683060470 => i32:7
;; run: nonan 4746794004532690944 => i32:2147483000
;; run: nonan 4746794007242211328 => i32:2147483646
;; run: nonan 4746794007244308480 => i32:2147483647
;; run: nonan 4746794007246405632 => i32:2147483647
;; run: nonan 4746794007248502784 => i32:2147483647
;; run: nonan 4746794007250599936 => i32:2147483647
;; run: nonan 13970166044103278592 => i32:-2147483648
;; run: nonan 13970166044104327168 => i32:-2147483648
;; run: nonan 13970166044105375744 => i32:-2147483648
;; run: nonan 13970166044106424320 => i32:-2147483648
;; run: nonan 4751297606873776128 => i32:2147483647
;; run: nonan 4751297606874824704 => i32:2147483647
;; run: nonan 4751297606875873280 => i32:2147483647
;; run: nonan 4756540486875873280 => i32:2147483647
;; run: nonan 13979912523730649088 => i32:-2147483648
;; run: nonan 4890886371069617664 => i32:2147483647
;; run: nonan 14114258407924393472 => i32:-2147483648
;; run: nonan 4890909195324358656 => i32:2147483647
;; run: nonan 14114281232179134464 => i32:-2147483648
;; run: nonan 9094988921128908188 => i32:2147483647
;; run: nonan 18318360957983683996 => i32:-2147483648
;; run: otherx 4617878467915022336 9221120237041090560 => i32:0
;; run: otherx 9221120237041090560 4607182418800017408 => i32:0
;; run: otherx 4748581863621132288 4607182418800017408 => i32:2147483647
;; run: otherx 4748581863621132288 4748581863621132288 => i32:2147483647
;; run: otherx 13843502304583483392 13979912523730649088 => i32:-2147483648
;;
;;
;; shape: eqzsel fcmp=0
;; shape: ltform fcmp=0
;; shape: bandlt fcmp=0
;; shape: bandle fcmp=0
;; shape: bandltf fcmp=0
;;
;; run: eqzsel 18442240474082181120 => i32:-2147483548
;; run: eqzsel 13970166044105375745 => i32:-2147483548
;; run: eqzsel 13970166044105375744 => i32:-2147483548
;; run: eqzsel 13970166044105375743 => i32:-2147483548
;; run: eqzsel 13970166044103278593 => i32:-2147483548
;; run: eqzsel 13970166044103278592 => i32:-2147483548
;; run: eqzsel 13970166044103278591 => i32:-2147483547
;; run: eqzsel 13830554455654793217 => i32:-101
;; run: eqzsel 13830554455654793216 => i32:-101
;; run: eqzsel 13830554455654793215 => i32:100
;; run: eqzsel 13826050856027422721 => i32:100
;; run: eqzsel 13826050856027422720 => i32:100
;; run: eqzsel 13826050856027422719 => i32:100
;; run: eqzsel 9223372036854775809 => i32:100
;; run: eqzsel 0 => i32:100
;; run: eqzsel 1 => i32:100
;; run: eqzsel 4602678819172646911 => i32:100
;; run: eqzsel 4602678819172646912 => i32:100
;; run: eqzsel 4602678819172646913 => i32:100
;; run: eqzsel 4607182418800017407 => i32:100
;; run: eqzsel 4607182418800017408 => i32:101
;; run: eqzsel 4607182418800017409 => i32:101
;; run: eqzsel 4617315517961601023 => i32:96
;; run: eqzsel 4617315517961601024 => i32:97
;; run: eqzsel 4617315517961601025 => i32:97
;; run: eqzsel 4617878467915022335 => i32:97
;; run: eqzsel 4617878467915022336 => i32:97
;; run: eqzsel 4617878467915022337 => i32:97
;; run: eqzsel 4619567317775286271 => i32:98
;; run: eqzsel 4619567317775286272 => i32:99
;; run: eqzsel 4619567317775286273 => i32:99
;; run: eqzsel 4746794007244308479 => i32:2147483546
;; run: eqzsel 4746794007244308480 => i32:2147483547
;; run: eqzsel 4746794007244308481 => i32:2147483547
;; run: eqzsel 4746794007248502783 => i32:2147483547
;; run: eqzsel 4746794007248502784 => i32:2147483547
;; run: eqzsel 4746794007248502785 => i32:2147483547
;; run: eqzsel 9218868437227405312 => i32:2147483547
;; run: eqzsel 9221120237041090560 => i32:100
;; run: ltform 18442240474082181120 => i32:-2147483547
;; run: ltform 13970166044105375745 => i32:-2147483547
;; run: ltform 13970166044105375744 => i32:-2147483547
;; run: ltform 13970166044105375743 => i32:-2147483547
;; run: ltform 13970166044103278593 => i32:-2147483547
;; run: ltform 13970166044103278592 => i32:-2147483547
;; run: ltform 13970166044103278591 => i32:-2147483548
;; run: ltform 13830554455654793217 => i32:-102
;; run: ltform 13830554455654793216 => i32:-102
;; run: ltform 13830554455654793215 => i32:101
;; run: ltform 13826050856027422721 => i32:101
;; run: ltform 13826050856027422720 => i32:101
;; run: ltform 13826050856027422719 => i32:101
;; run: ltform 9223372036854775809 => i32:101
;; run: ltform 0 => i32:101
;; run: ltform 1 => i32:101
;; run: ltform 4602678819172646911 => i32:101
;; run: ltform 4602678819172646912 => i32:101
;; run: ltform 4602678819172646913 => i32:101
;; run: ltform 4607182418800017407 => i32:101
;; run: ltform 4607182418800017408 => i32:100
;; run: ltform 4607182418800017409 => i32:100
;; run: ltform 4617315517961601023 => i32:97
;; run: ltform 4617315517961601024 => i32:96
;; run: ltform 4617315517961601025 => i32:96
;; run: ltform 4617878467915022335 => i32:96
;; run: ltform 4617878467915022336 => i32:96
;; run: ltform 4617878467915022337 => i32:96
;; run: ltform 4619567317775286271 => i32:99
;; run: ltform 4619567317775286272 => i32:98
;; run: ltform 4619567317775286273 => i32:98
;; run: ltform 4746794007244308479 => i32:2147483547
;; run: ltform 4746794007244308480 => i32:2147483546
;; run: ltform 4746794007244308481 => i32:2147483546
;; run: ltform 4746794007248502783 => i32:2147483546
;; run: ltform 4746794007248502784 => i32:2147483546
;; run: ltform 4746794007248502785 => i32:2147483546
;; run: ltform 9218868437227405312 => i32:2147483546
;; run: ltform 9221120237041090560 => i32:101
;; run: bandlt 18442240474082181120 => i32:-2147483546
;; run: bandlt 13970166044105375745 => i32:-2147483546
;; run: bandlt 13970166044105375744 => i32:-2147483546
;; run: bandlt 13970166044105375743 => i32:-2147483546
;; run: bandlt 13970166044103278593 => i32:-2147483546
;; run: bandlt 13970166044103278592 => i32:-2147483546
;; run: bandlt 13970166044103278591 => i32:-2147483545
;; run: bandlt 13830554455654793217 => i32:-103
;; run: bandlt 13830554455654793216 => i32:-103
;; run: bandlt 13830554455654793215 => i32:102
;; run: bandlt 13826050856027422721 => i32:102
;; run: bandlt 13826050856027422720 => i32:102
;; run: bandlt 13826050856027422719 => i32:102
;; run: bandlt 9223372036854775809 => i32:102
;; run: bandlt 0 => i32:102
;; run: bandlt 1 => i32:102
;; run: bandlt 4602678819172646911 => i32:102
;; run: bandlt 4602678819172646912 => i32:102
;; run: bandlt 4602678819172646913 => i32:102
;; run: bandlt 4607182418800017407 => i32:102
;; run: bandlt 4607182418800017408 => i32:103
;; run: bandlt 4607182418800017409 => i32:103
;; run: bandlt 4617315517961601023 => i32:98
;; run: bandlt 4617315517961601024 => i32:99
;; run: bandlt 4617315517961601025 => i32:99
;; run: bandlt 4617878467915022335 => i32:99
;; run: bandlt 4617878467915022336 => i32:99
;; run: bandlt 4617878467915022337 => i32:99
;; run: bandlt 4619567317775286271 => i32:96
;; run: bandlt 4619567317775286272 => i32:97
;; run: bandlt 4619567317775286273 => i32:97
;; run: bandlt 4746794007244308479 => i32:2147483544
;; run: bandlt 4746794007244308480 => i32:2147483545
;; run: bandlt 4746794007244308481 => i32:2147483545
;; run: bandlt 4746794007248502783 => i32:2147483545
;; run: bandlt 4746794007248502784 => i32:2147483545
;; run: bandlt 4746794007248502785 => i32:2147483545
;; run: bandlt 9218868437227405312 => i32:2147483545
;; run: bandlt 9221120237041090560 => i32:102
;; run: bandle 18442240474082181120 => i32:-2147483545
;; run: bandle 13970166044105375745 => i32:-2147483545
;; run: bandle 13970166044105375744 => i32:-2147483545
;; run: bandle 13970166044105375743 => i32:-2147483545
;; run: bandle 13970166044103278593 => i32:-2147483545
;; run: bandle 13970166044103278592 => i32:-2147483545
;; run: bandle 13970166044103278591 => i32:-2147483546
;; run: bandle 13830554455654793217 => i32:-104
;; run: bandle 13830554455654793216 => i32:-104
;; run: bandle 13830554455654793215 => i32:103
;; run: bandle 13826050856027422721 => i32:103
;; run: bandle 13826050856027422720 => i32:103
;; run: bandle 13826050856027422719 => i32:103
;; run: bandle 9223372036854775809 => i32:103
;; run: bandle 0 => i32:103
;; run: bandle 1 => i32:103
;; run: bandle 4602678819172646911 => i32:103
;; run: bandle 4602678819172646912 => i32:103
;; run: bandle 4602678819172646913 => i32:103
;; run: bandle 4607182418800017407 => i32:103
;; run: bandle 4607182418800017408 => i32:102
;; run: bandle 4607182418800017409 => i32:102
;; run: bandle 4617315517961601023 => i32:99
;; run: bandle 4617315517961601024 => i32:98
;; run: bandle 4617315517961601025 => i32:98
;; run: bandle 4617878467915022335 => i32:98
;; run: bandle 4617878467915022336 => i32:98
;; run: bandle 4617878467915022337 => i32:98
;; run: bandle 4619567317775286271 => i32:97
;; run: bandle 4619567317775286272 => i32:96
;; run: bandle 4619567317775286273 => i32:96
;; run: bandle 4746794007244308479 => i32:2147483545
;; run: bandle 4746794007244308480 => i32:2147483544
;; run: bandle 4746794007244308481 => i32:2147483544
;; run: bandle 4746794007248502783 => i32:2147483544
;; run: bandle 4746794007248502784 => i32:2147483544
;; run: bandle 4746794007248502785 => i32:2147483544
;; run: bandle 9218868437227405312 => i32:2147483544
;; run: bandle 9221120237041090560 => i32:103
;; run: bandltf 4286578688 => i32:-2147483544
;; run: bandltf 3472883713 => i32:-2147483544
;; run: bandltf 3472883712 => i32:-2147483544
;; run: bandltf 3472883711 => i32:-2147483416
;; run: bandltf 3212836865 => i32:-105
;; run: bandltf 3212836864 => i32:-105
;; run: bandltf 3212836863 => i32:104
;; run: bandltf 3204448257 => i32:104
;; run: bandltf 3204448256 => i32:104
;; run: bandltf 3204448255 => i32:104
;; run: bandltf 2147483649 => i32:104
;; run: bandltf 0 => i32:104
;; run: bandltf 1 => i32:104
;; run: bandltf 1056964607 => i32:104
;; run: bandltf 1056964608 => i32:104
;; run: bandltf 1056964609 => i32:104
;; run: bandltf 1065353215 => i32:104
;; run: bandltf 1065353216 => i32:105
;; run: bandltf 1065353217 => i32:105
;; run: bandltf 1084227583 => i32:108
;; run: bandltf 1084227584 => i32:109
;; run: bandltf 1084227585 => i32:109
;; run: bandltf 1085276159 => i32:109
;; run: bandltf 1085276160 => i32:109
;; run: bandltf 1085276161 => i32:109
;; run: bandltf 1088421887 => i32:110
;; run: bandltf 1088421888 => i32:111
;; run: bandltf 1088421889 => i32:111
;; run: bandltf 1325400063 => i32:2147483624
;; run: bandltf 1325400064 => i32:2147483543
;; run: bandltf 1325400065 => i32:2147483543
;; run: bandltf 2139095040 => i32:2147483543
;; run: bandltf 2143289344 => i32:104
;; run: band7f 4286578688 => i32:-2147483543
;; run: band7f 3472883713 => i32:-2147483543
;; run: band7f 3472883712 => i32:-2147483543
;; run: band7f 3472883711 => i32:-2147483415
;; run: band7f 3212836865 => i32:-106
;; run: band7f 3212836864 => i32:-106
;; run: band7f 3212836863 => i32:110
;; run: band7f 3204448257 => i32:110
;; run: band7f 3204448256 => i32:110
;; run: band7f 3204448255 => i32:110
;; run: band7f 2147483649 => i32:110
;; run: band7f 0 => i32:110
;; run: band7f 1 => i32:110
;; run: band7f 1056964607 => i32:110
;; run: band7f 1056964608 => i32:110
;; run: band7f 1056964609 => i32:110
;; run: band7f 1065353215 => i32:110
;; run: band7f 1065353216 => i32:104
;; run: band7f 1065353217 => i32:104
;; run: band7f 1084227583 => i32:109
;; run: band7f 1084227584 => i32:108
;; run: band7f 1084227585 => i32:108
;; run: band7f 1085276159 => i32:108
;; run: band7f 1085276160 => i32:108
;; run: band7f 1085276161 => i32:108
;; run: band7f 1088421887 => i32:111
;; run: band7f 1088421888 => i32:110
;; run: band7f 1088421889 => i32:110
;; run: band7f 1325400063 => i32:2147483625
;; run: band7f 1325400064 => i32:2147483542
;; run: band7f 1325400065 => i32:2147483542
;; run: band7f 2139095040 => i32:2147483542
;; run: band7f 2143289344 => i32:105
;; run: clamp5 18442240474082181120 => i32:-2147483542
;; run: clamp5 13970166044105375745 => i32:-2147483542
;; run: clamp5 13970166044105375744 => i32:-2147483542
;; run: clamp5 13970166044105375743 => i32:-2147483542
;; run: clamp5 13970166044103278593 => i32:-2147483542
;; run: clamp5 13970166044103278592 => i32:-2147483542
;; run: clamp5 13970166044103278591 => i32:-2147483541
;; run: clamp5 13830554455654793217 => i32:-107
;; run: clamp5 13830554455654793216 => i32:-107
;; run: clamp5 13830554455654793215 => i32:106
;; run: clamp5 13826050856027422721 => i32:106
;; run: clamp5 13826050856027422720 => i32:106
;; run: clamp5 13826050856027422719 => i32:106
;; run: clamp5 9223372036854775809 => i32:106
;; run: clamp5 0 => i32:106
;; run: clamp5 1 => i32:106
;; run: clamp5 4602678819172646911 => i32:106
;; run: clamp5 4602678819172646912 => i32:106
;; run: clamp5 4602678819172646913 => i32:106
;; run: clamp5 4607182418800017407 => i32:106
;; run: clamp5 4607182418800017408 => i32:107
;; run: clamp5 4607182418800017409 => i32:107
;; run: clamp5 4617315517961601023 => i32:110
;; run: clamp5 4617315517961601024 => i32:111
;; run: clamp5 4617315517961601025 => i32:111
;; run: clamp5 4617878467915022335 => i32:111
;; run: clamp5 4617878467915022336 => i32:111
;; run: clamp5 4617878467915022337 => i32:111
;; run: clamp5 4619567317775286271 => i32:111
;; run: clamp5 4619567317775286272 => i32:111
;; run: clamp5 4619567317775286273 => i32:111
;; run: clamp5 4746794007244308479 => i32:111
;; run: clamp5 4746794007244308480 => i32:111
;; run: clamp5 4746794007244308481 => i32:111
;; run: clamp5 4746794007248502783 => i32:111
;; run: clamp5 4746794007248502784 => i32:111
;; run: clamp5 4746794007248502785 => i32:111
;; run: clamp5 9218868437227405312 => i32:111
;; run: clamp5 9221120237041090560 => i32:106
;; run: nanconst 18442240474082181120 => i32:-2147483541
;; run: nanconst 13970166044105375745 => i32:-2147483541
;; run: nanconst 13970166044105375744 => i32:-2147483541
;; run: nanconst 13970166044105375743 => i32:-2147483541
;; run: nanconst 13970166044103278593 => i32:-2147483541
;; run: nanconst 13970166044103278592 => i32:-2147483541
;; run: nanconst 13970166044103278591 => i32:-2147483542
;; run: nanconst 13830554455654793217 => i32:-108
;; run: nanconst 13830554455654793216 => i32:-108
;; run: nanconst 13830554455654793215 => i32:107
;; run: nanconst 13826050856027422721 => i32:107
;; run: nanconst 13826050856027422720 => i32:107
;; run: nanconst 13826050856027422719 => i32:107
;; run: nanconst 9223372036854775809 => i32:107
;; run: nanconst 0 => i32:107
;; run: nanconst 1 => i32:107
;; run: nanconst 4602678819172646911 => i32:107
;; run: nanconst 4602678819172646912 => i32:107
;; run: nanconst 4602678819172646913 => i32:107
;; run: nanconst 4607182418800017407 => i32:107
;; run: nanconst 4607182418800017408 => i32:106
;; run: nanconst 4607182418800017409 => i32:106
;; run: nanconst 4617315517961601023 => i32:111
;; run: nanconst 4617315517961601024 => i32:110
;; run: nanconst 4617315517961601025 => i32:110
;; run: nanconst 4617878467915022335 => i32:110
;; run: nanconst 4617878467915022336 => i32:110
;; run: nanconst 4617878467915022337 => i32:110
;; run: nanconst 4619567317775286271 => i32:110
;; run: nanconst 4619567317775286272 => i32:110
;; run: nanconst 4619567317775286273 => i32:110
;; run: nanconst 4746794007244308479 => i32:110
;; run: nanconst 4746794007244308480 => i32:110
;; run: nanconst 4746794007244308481 => i32:110
;; run: nanconst 4746794007248502783 => i32:110
;; run: nanconst 4746794007248502784 => i32:110
;; run: nanconst 4746794007248502785 => i32:110
;; run: nanconst 9218868437227405312 => i32:110
;; run: nanconst 9221120237041090560 => i32:107

(module
  (func $s32 (export "s32") (param $x f64) (result i32)
    (local $t i32) (local $c1 i32) (local $a i32) (local $b i32)
    (local.set $t (i32.trunc_sat_f64_s (local.get $x)))
    (local.set $c1 (f64.ge (local.get $x) (f64.const -0x1.0000000000000p+31)))
    (local.set $a (select (i32.const 2147483647) (i32.const -2147483648) (local.get $c1)))
    (local.set $b (select (local.get $a) (i32.const 0) (f64.eq (local.get $x) (local.get $x))))
    (select
      (select (local.get $t) (local.get $b) (f64.le (local.get $x) (f64.const 0x1.fffffffc00000p+30)))
      (local.get $b) (local.get $c1)))
  (func $s32f (export "s32f") (param $x f32) (result i32)
    (local $t i32) (local $c1 i32) (local $a i32) (local $b i32)
    (local.set $t (i32.trunc_sat_f32_s (local.get $x)))
    (local.set $c1 (f32.ge (local.get $x) (f32.const -0x1.0000000000000p+31)))
    (local.set $a (select (i32.const 2147483647) (i32.const -2147483648) (local.get $c1)))
    (local.set $b (select (local.get $a) (i32.const 0) (f32.eq (local.get $x) (local.get $x))))
    (select
      (select (local.get $t) (local.get $b) (f32.le (local.get $x) (f32.const 0x1.0000000000000p+31)))
      (local.get $b) (local.get $c1)))
  (func $s64 (export "s64") (param $x f64) (result i64)
    (local $t i64) (local $c1 i32) (local $a i64) (local $b i64)
    (local.set $t (i64.trunc_sat_f64_s (local.get $x)))
    (local.set $c1 (f64.ge (local.get $x) (f64.const -0x1.0000000000000p+63)))
    (local.set $a (select (i64.const 9223372036854775807) (i64.const -9223372036854775808) (local.get $c1)))
    (local.set $b (select (local.get $a) (i64.const 0) (f64.eq (local.get $x) (local.get $x))))
    (select
      (select (local.get $t) (local.get $b) (f64.le (local.get $x) (f64.const 0x1.0000000000000p+63)))
      (local.get $b) (local.get $c1)))
  (func $u32 (export "u32") (param $x f64) (result i32)
    (local $t i32) (local $c1 i32) (local $a i32) (local $b i32)
    (local.set $t (i32.trunc_sat_f64_u (local.get $x)))
    (local.set $c1 (f64.ge (local.get $x) (f64.const 0x0.0p+0)))
    (local.set $a (select (i32.const -1) (i32.const 0) (local.get $c1)))
    (local.set $b (select (local.get $a) (i32.const 0) (f64.eq (local.get $x) (local.get $x))))
    (select
      (select (local.get $t) (local.get $b) (f64.le (local.get $x) (f64.const 0x1.fffffffe00000p+31)))
      (local.get $b) (local.get $c1)))
  (func $wrongbound (export "wrongbound") (param $x f64) (result i32)
    (local $t i32) (local $c1 i32) (local $a i32) (local $b i32)
    (local.set $t (i32.trunc_sat_f64_s (local.get $x)))
    (local.set $c1 (f64.ge (local.get $x) (f64.const -0x1.0000000000000p+31)))
    (local.set $a (select (i32.const 2147483647) (i32.const -2147483648) (local.get $c1)))
    (local.set $b (select (local.get $a) (i32.const 0) (f64.eq (local.get $x) (local.get $x))))
    (select
      (select (local.get $t) (local.get $b) (f64.le (local.get $x) (f64.const 0x1.fffff5e000000p+30)))
      (local.get $b) (local.get $c1)))
  (func $wrongnan (export "wrongnan") (param $x f64) (result i32)
    (local $t i32) (local $c1 i32) (local $a i32) (local $b i32)
    (local.set $t (i32.trunc_sat_f64_s (local.get $x)))
    (local.set $c1 (f64.ge (local.get $x) (f64.const -0x1.0000000000000p+31)))
    (local.set $a (select (i32.const 2147483647) (i32.const -2147483648) (local.get $c1)))
    (local.set $b (select (local.get $a) (i32.const 1) (f64.eq (local.get $x) (local.get $x))))
    (select
      (select (local.get $t) (local.get $b) (f64.le (local.get $x) (f64.const 0x1.fffffffc00000p+30)))
      (local.get $b) (local.get $c1)))
  (func $nonan (export "nonan") (param $x f64) (result i32)
    (local $t i32) (local $c1 i32) (local $a i32) (local $b i32)
    (local.set $t (i32.trunc_sat_f64_s (local.get $x)))
    (local.set $c1 (f64.ge (local.get $x) (f64.const -0x1.0000000000000p+31)))
    (local.set $a (select (i32.const 2147483647) (i32.const -2147483648) (local.get $c1)))
    (local.set $b (local.get $a))
    (select
      (select (local.get $t) (local.get $b) (f64.le (local.get $x) (f64.const 0x1.fffffffc00000p+30)))
      (local.get $b) (local.get $c1)))
  (func $otherx (export "otherx") (param $x f64) (param $y f64) (result i32)
    (local $t i32) (local $c1 i32) (local $a i32) (local $b i32)
    (local.set $t (i32.trunc_sat_f64_s (local.get $x)))
    (local.set $c1 (f64.ge (local.get $y) (f64.const -0x1.0000000000000p+31)))
    (local.set $a (select (i32.const 2147483647) (i32.const -2147483648) (local.get $c1)))
    (local.set $b (select (local.get $a) (i32.const 0) (f64.eq (local.get $y) (local.get $y))))
    (select
      (select (local.get $t) (local.get $b) (f64.le (local.get $y) (f64.const 0x1.fffffffc00000p+30)))
      (local.get $b) (local.get $c1)))
  (func $eqzsel (export "eqzsel") (param $x f64) (result i32)
    (local $t i32)
    (local.set $t (i32.trunc_sat_f64_s (local.get $x)))
    (i32.xor (select (select (local.get $t) (select (select (i32.const 2147483647) (i32.const -2147483648) (f64.ge (local.get $x) (f64.const -0x1.0000000000000p+31))) (i32.const 0) (i32.eqz (f64.ne (local.get $x) (local.get $x)))) (f64.le (local.get $x) (f64.const 0x1.fffffffc00000p+30))) (select (select (i32.const 2147483647) (i32.const -2147483648) (f64.ge (local.get $x) (f64.const -0x1.0000000000000p+31))) (i32.const 0) (i32.eqz (f64.ne (local.get $x) (local.get $x)))) (f64.ge (local.get $x) (f64.const -0x1.0000000000000p+31))) (i32.const 100)))
  (func $ltform (export "ltform") (param $x f64) (result i32)
    (local $t i32)
    (local.set $t (i32.trunc_sat_f64_s (local.get $x)))
    (i32.xor (select (i32.const -2147483648) (select (i32.const 2147483647) (select (i32.const 0) (local.get $t) (f64.ne (local.get $x) (local.get $x))) (f64.gt (local.get $x) (f64.const 0x1.fffffffc00000p+30))) (f64.lt (local.get $x) (f64.const -0x1.0000000000000p+31))) (i32.const 101)))
  (func $bandlt (export "bandlt") (param $x f64) (result i32)
    (local $t i32)
    (local.set $t (i32.trunc_sat_f64_s (local.get $x)))
    (i32.xor (select (select (i32.const 0) (local.get $t) (f64.gt (local.get $x) (f64.const -0x1.0000000000000p+0))) (local.get $t) (f64.lt (local.get $x) (f64.const 0x1.0000000000000p+0))) (i32.const 102)))
  (func $bandle (export "bandle") (param $x f64) (result i32)
    (local $t i32)
    (local.set $t (i32.trunc_sat_f64_s (local.get $x)))
    (i32.xor (select (local.get $t) (select (local.get $t) (i32.const 0) (f64.le (local.get $x) (f64.const -0x1.0000000000000p+0))) (f64.ge (local.get $x) (f64.const 0x1.0000000000000p+0))) (i32.const 103)))
  (func $bandltf (export "bandltf") (param $x f32) (result i32)
    (local $t i32)
    (local.set $t (i32.trunc_sat_f32_s (local.get $x)))
    (i32.xor (select (select (i32.const 0) (local.get $t) (f32.gt (local.get $x) (f32.const -0x1.0000000000000p+0))) (local.get $t) (f32.lt (local.get $x) (f32.const 0x1.0000000000000p+0))) (i32.const 104)))
  (func $band7f (export "band7f") (param $x f32) (result i32)
    (local $t i32)
    (local.set $t (i32.trunc_sat_f32_s (local.get $x)))
    (i32.xor (select (select (i32.const 7) (local.get $t) (f32.gt (local.get $x) (f32.const -0x1.0000000000000p+0))) (local.get $t) (f32.lt (local.get $x) (f32.const 0x1.0000000000000p+0))) (i32.const 105)))
  (func $clamp5 (export "clamp5") (param $x f64) (result i32)
    (local $t i32)
    (local.set $t (i32.trunc_sat_f64_s (local.get $x)))
    (i32.xor (select (i32.const 5) (local.get $t) (f64.ge (local.get $x) (f64.const 0x1.4000000000000p+2))) (i32.const 106)))
  (func $nanconst (export "nanconst") (param $x f64) (result i32)
    (local $t i32)
    (local.set $t (i32.trunc_sat_f64_s (local.get $x)))
    (i32.xor (select (select (i32.const 5) (local.get $t) (f64.ge (local.get $x) (f64.const 0x1.4000000000000p+2))) (i32.const 9) (f64.ne (local.get $x) (f64.const nan))) (i32.const 107)))
)
