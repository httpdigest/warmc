;; Limbs of GMP's generic mpn_add_n, s = u + v, r = s + cin, cout =
;; (s <u u) | (r <u s), and mpn_sub_n, s = u - v, r = s - bin, bout =
;; (u <u v) | (s <u bin), cin and bin 0 or 1: the arm64 back end makes each
;; an adcs or sbcs and a cset, and a limb whose carry in the one before put
;; in the flag reads it there (isel.c find_full_adders) -- each compare in
;; its other forms, the or as an add or a xor, the carry in the add's
;; first operand, i64 limbs whose carry is widened, two limbs an iteration.
;; Left alone: compares that are no carry, a carry in that may be 2, a sum
;; or a compare read besides, an operand or carry in shifted into the add,
;; a sum that is a difference, a carry out past a branch, a borrow made and
;; read before its difference.  A compare between two limbs that writes
;; the flag makes the second read its carry in from its register, as do a
;; sub after an add and a limb after a call's arm emitted elsewhere; one
;; between r and its carry out is passed by moving the carry out up.  One
;; limb an iteration unless written two; the loops leave on a limb no array
;; holds too, so the unroller leaves them be.  Made by test/gen_fulladder.py.
;;
;; shape: add_or \sadcs\s=1
;; shape: add_or \sorr\s=0
;; shape: add_gt \sadcs\s=1
;; shape: add_gt \sorr\s=0
;; shape: add_vcin \sadcs\s=1
;; shape: add_vcin \sorr\s=0
;; shape: add_xor \sadcs\s=1
;; shape: add_xor \sorr\s=0
;; shape: add_swap \sadcs\s=1
;; shape: add_swap \sorr\s=0
;; shape: add64 \sadcs\s=1
;; shape: add64 \sorr\s=0
;; shape: add2 \sadcs\s=2
;; shape: add2 \scmp\s[wx]\d+, #0x1$=1
;; shape: add2 \scset\sw\d+, hs$=2
;; shape: add2_64 \sadcs\s=2
;; shape: add2_64 \scmp\s[wx]\d+, #0x1$=1
;; shape: sub_or \ssbcs\s=1
;; shape: sub_or \sorr\s=0
;; shape: sub_gt \ssbcs\s=1
;; shape: sub_gt \sorr\s=0
;; shape: sub_mix \ssbcs\s=1
;; shape: sub_mix \sorr\s=0
;; shape: sub64 \ssbcs\s=1
;; shape: sub64 \sorr\s=0
;; shape: sub2 \ssbcs\s=2
;; shape: sub2 \scmp\swzr, w\d+$=1
;; shape: sub2 \scset\sw\d+, lo$=2
;; shape: add_notc1 \sadcs\s=0
;; shape: add_notc2 \sadcs\s=0
;; shape: add_le \sadcs\s=0
;; shape: sub_notb1 \ssbcs\s=0
;; shape: sub_notb1y \ssbcs\s=0
;; shape: sub_addcmp \ssbcs\s=0
;; shape: add_cy3 \sadcs\s=0
;; shape: add_add \sadcs\s=1
;; shape: add_add \sorr\s=0
;; shape: add_sused \sadcs\s=0
;; shape: add2_clobber \sadcs\s=2
;; shape: add2_clobber \scmp\s[wx]\d+, #0x1$=2
;; shape: add_c1used \sadcs\s=0
;; shape: add_c2used \sadcs\s=0
;; shape: add_shl \sadcs\s=0
;; shape: add_split \sadcs\s=0
;; shape: add_subs \sadcs\s=0
;; shape: add_shrcin \sadcs\s=0
;; shape: add2_call \sadcs\s=2
;; shape: add2_call \scmp\s[wx]\d+, #0x1$=2
;; shape: add_between \sadcs\s=1
;; shape: add_between \scmp\s[wx]\d+, #0x1$=1
;; shape: add_then_sub \sadcs\s=1
;; shape: add_then_sub \ssbcs\s=1
;; shape: add_then_sub \scmp\s[wx]\d+, #0x1$=1
;; shape: add_then_sub \scmp\swzr, w\d+$=1
;; shape: sub_late \ssbcs\s=0
;;
;; run: add_or 1 => i32:-2147479522
;; run: add_or 2 => i32:-1628979483
;; run: add_or 3 => i32:-1278939196
;; run: add_or 7 => i32:87274355
;; run: add_or 16 => i32:74965482
;; run: add_or 32 => i32:-1877491174
;; run: add_gt 1 => i32:-2147475426
;; run: add_gt 2 => i32:-1628975387
;; run: add_gt 3 => i32:-1278935100
;; run: add_gt 7 => i32:87278451
;; run: add_gt 16 => i32:74969578
;; run: add_gt 32 => i32:-1877487078
;; run: add_vcin 1 => i32:-2147471330
;; run: add_vcin 2 => i32:-1628971291
;; run: add_vcin 3 => i32:-1278931004
;; run: add_vcin 7 => i32:87282547
;; run: add_vcin 16 => i32:74973674
;; run: add_vcin 32 => i32:-1877482982
;; run: add_xor 1 => i32:-2147467234
;; run: add_xor 2 => i32:-1628967195
;; run: add_xor 3 => i32:-1278926908
;; run: add_xor 7 => i32:87286643
;; run: add_xor 16 => i32:74977770
;; run: add_xor 32 => i32:-1877478886
;; run: add_swap 1 => i32:-2147463138
;; run: add_swap 2 => i32:-1628963099
;; run: add_swap 3 => i32:-1278922812
;; run: add_swap 7 => i32:87290739
;; run: add_swap 16 => i32:74981866
;; run: add_swap 32 => i32:-1877474790
;; run: add64 1 => i32:-1628959003
;; run: add64 2 => i32:-1131226427
;; run: add64 3 => i32:-399604062
;; run: add64 7 => i32:-1412149006
;; run: add64 16 => i32:-1877470694
;; run: add64 32 => i32:927141974
;; run: add2 1 => i32:-1628954907
;; run: add2 2 => i32:-1131222331
;; run: add2 3 => i32:-399599966
;; run: add2 7 => i32:-1412144910
;; run: add2 16 => i32:-1877466598
;; run: add2 32 => i32:927146070
;; run: add2_64 1 => i32:-1131218235
;; run: add2_64 2 => i32:897956332
;; run: add2_64 3 => i32:-761638175
;; run: add2_64 7 => i32:-1647685382
;; run: add2_64 16 => i32:927150166
;; run: add2_64 32 => i32:-1059429574
;; run: sub_or 1 => i32:-2147446785
;; run: sub_or 2 => i32:2043798591
;; run: sub_or 3 => i32:2060771769
;; run: sub_or 7 => i32:1770418873
;; run: sub_or 16 => i32:2081186273
;; run: sub_or 32 => i32:790816132
;; run: sub_gt 1 => i32:-2147442689
;; run: sub_gt 2 => i32:2043802687
;; run: sub_gt 3 => i32:2060775865
;; run: sub_gt 7 => i32:1770422969
;; run: sub_gt 16 => i32:2081190369
;; run: sub_gt 32 => i32:790820228
;; run: sub_mix 1 => i32:-2147438593
;; run: sub_mix 2 => i32:2043806783
;; run: sub_mix 3 => i32:2060779961
;; run: sub_mix 7 => i32:1770427065
;; run: sub_mix 16 => i32:2081194465
;; run: sub_mix 32 => i32:790824324
;; run: sub64 1 => i32:2043810879
;; run: sub64 2 => i32:95243335
;; run: sub64 3 => i32:-1776511823
;; run: sub64 7 => i32:843665027
;; run: sub64 16 => i32:790828420
;; run: sub64 32 => i32:-497110258
;; run: sub2 1 => i32:2043814975
;; run: sub2 2 => i32:95247431
;; run: sub2 3 => i32:-1776507727
;; run: sub2 7 => i32:843669123
;; run: sub2 16 => i32:790832516
;; run: sub2 32 => i32:-497106162
;; run: add_notc1 1 => i32:-2147426305
;; run: add_notc1 2 => i32:-1628926236
;; run: add_notc1 3 => i32:-1278885979
;; run: add_notc1 7 => i32:58698451
;; run: add_notc1 16 => i32:1286553963
;; run: add_notc1 32 => i32:279213524
;; run: add_notc2 1 => i32:-2147422178
;; run: add_notc2 2 => i32:-1628922139
;; run: add_notc2 3 => i32:-1278881852
;; run: add_notc2 7 => i32:87331699
;; run: add_notc2 16 => i32:74099305
;; run: add_notc2 32 => i32:-2088784743
;; run: add_le 1 => i32:-2147418082
;; run: add_le 2 => i32:-1628918043
;; run: add_le 3 => i32:-1278847965
;; run: add_le 7 => i32:87365617
;; run: add_le 16 => i32:-2015327443
;; run: add_le 32 => i32:1131549020
;; run: sub_notb1 1 => i32:-2147413986
;; run: sub_notb1 2 => i32:2043830397
;; run: sub_notb1 3 => i32:2060774716
;; run: sub_notb1 7 => i32:-33604
;; run: sub_notb1 16 => i32:-191961471
;; run: sub_notb1 32 => i32:-2122027036
;; run: sub_notb1y 1 => i32:-2147409921
;; run: sub_notb1y 2 => i32:2043834494
;; run: sub_notb1y 3 => i32:2060778843
;; run: sub_notb1y 7 => i32:28599675
;; run: sub_notb1y 16 => i32:-2031180095
;; run: sub_notb1y 32 => i32:1251745220
;; run: sub_addcmp 1 => i32:-2147405794
;; run: sub_addcmp 2 => i32:2043839550
;; run: sub_addcmp 3 => i32:2060812698
;; run: sub_addcmp 7 => i32:1741830650
;; run: sub_addcmp 16 => i32:-374517342
;; run: sub_addcmp 32 => i32:1200983811
;; run: add_cy3 1 => i32:-2147401696
;; run: add_cy3 2 => i32:-1628901597
;; run: add_cy3 3 => i32:-1278859450
;; run: add_cy3 7 => i32:1862359541
;; run: add_cy3 16 => i32:-946025048
;; run: add_cy3 32 => i32:-1602653736
;; run: add_add 1 => i32:-2147397602
;; run: add_add 2 => i32:-1628897563
;; run: add_add 3 => i32:-1278857276
;; run: add_add 7 => i32:87356275
;; run: add_add 16 => i32:75047402
;; run: add_add 32 => i32:-1877409254
;; run: add_sused 1 => i32:90081
;; run: add_sused 2 => i32:89625
;; run: add_sused 3 => i32:-1486119631
;; run: add_sused 7 => i32:-371644520
;; run: add_sused 16 => i32:-274568375
;; run: add_sused 32 => i32:70540822
;; run: add2_clobber 1 => i32:-1628889372
;; run: add2_clobber 2 => i32:-1131156793
;; run: add2_clobber 3 => i32:-399534431
;; run: add2_clobber 7 => i32:-1412079365
;; run: add2_clobber 16 => i32:-1877401074
;; run: add2_clobber 32 => i32:927211642
;; run: add_c1used 1 => i32:-2147385313
;; run: add_c1used 2 => i32:-1628885276
;; run: add_c1used 3 => i32:-1278844987
;; run: add_c1used 7 => i32:87368561
;; run: add_c1used 16 => i32:75059692
;; run: add_c1used 32 => i32:-1877396969
;; run: add_c2used 1 => i32:-2147381218
;; run: add_c2used 2 => i32:-1628881179
;; run: add_c2used 3 => i32:-1278840892
;; run: add_c2used 7 => i32:87372659
;; run: add_c2used 16 => i32:75063787
;; run: add_c2used 32 => i32:-1877392869
;; run: add_shl 1 => i32:106495
;; run: add_shl 2 => i32:829718039
;; run: add_shl 3 => i32:-801177398
;; run: add_shl 7 => i32:-2073678464
;; run: add_shl 16 => i32:1578777470
;; run: add_shl 32 => i32:509056862
;; run: add_split 1 => i32:-2147373114
;; run: add_split 2 => i32:1888875837
;; run: add_split 3 => i32:-2057900604
;; run: add_split 7 => i32:-312601045
;; run: add_split 16 => i32:-1509056358
;; run: add_split 32 => i32:-656104454
;; run: add_subs 1 => i32:-2147368930
;; run: add_subs 2 => i32:2043875455
;; run: add_subs 3 => i32:2060819834
;; run: add_subs 7 => i32:57269818
;; run: add_subs 16 => i32:579068834
;; run: add_subs 32 => i32:1397057799
;; run: add_shrcin 1 => i32:-2147364833
;; run: add_shrcin 2 => i32:-1628864765
;; run: add_shrcin 3 => i32:-1278823578
;; run: add_shrcin 7 => i32:946293363
;; run: add_shrcin 16 => i32:1335120235
;; run: add_shrcin 32 => i32:-1702033933
;; run: add2_call 1 => i32:-1628860697
;; run: add2_call 2 => i32:-1131128115
;; run: add2_call 3 => i32:-399505750
;; run: add2_call 7 => i32:-1412050782
;; run: add2_call 16 => i32:-1877374022
;; run: add2_call 32 => i32:542572830
;; run: add_between 1 => i32:-2147356641
;; run: add_between 2 => i32:-1628856601
;; run: add_between 3 => i32:-1278816313
;; run: add_between 7 => i32:87397242
;; run: add_between 16 => i32:75088380
;; run: add_between 32 => i32:-1877368268
;; run: add_then_sub 1 => i32:2043892798
;; run: add_then_sub 2 => i32:-1310362923
;; run: add_then_sub 3 => i32:1666638925
;; run: add_then_sub 7 => i32:-1947328614
;; run: add_then_sub 16 => i32:1907738177
;; run: add_then_sub 32 => i32:21521036
;; run: sub_late 1 => i32:-2147348481
;; run: sub_late 2 => i32:2043896894
;; run: sub_late 3 => i32:2060870077
;; run: sub_late 7 => i32:1770517465
;; run: sub_late 16 => i32:2086936421
;; run: sub_late 32 => i32:392863804

(module
  (memory 1)
  (data (i32.const 0) "\ff\ff\ff\ff\d0\7f\5c\0c\11\e8\18\18\75\04\d9\0e\cc\75\f6\36\ff\ff\ff\ff\8f\0b\e2\11\ff\ff\ff\ff\b6\dd\21\0f\23\7c\b1\1f\00\00\00\00\f1\48\3f\95\ff\ff\ff\ff\14\da\8c\65\00\00\00\00\cb\96\c4\db\00\00\00\80\c0\a1\27\1e\ff\ff\ff\7f\00\00\00\00\69\73\3a\92\00\00\00\00\ff\ff\ff\ff\37\f0\12\10\ff\ff\ff\ff\00\00\00\00\62\d1\1e\88\00\00\00\80\58\a9\90\5c\00\00\00\00\20\ea\a2\c7\ff\ff\ff\ff\ff\ff\ff\7f\fe\ff\ff\ff\ff\ff\ff\7f\ff\ff\ff\ff\d4\f4\3a\2a\ff\ff\ff\ff\97\7c\09\0a\86\ef\de\13\f2\d3\b1\92\01\00\00\00\ff\ff\ff\7f\dd\1c\01\cc\ff\ff\ff\ff\00\00\00\80\b2\d6\a3\10\36\fb\94\b3\b3\48\f4\93\a5\a9\69\d2\fe\ff\ff\ff\3d\56\d5\58\29\b2\ff\5a\78\fd\f9\1d\ff\ff\ff\ff\ff\ff\ff\ff\00\00\00\00\d2\77\b4\ea\e7\f9\a0\14\76\28\d2\66\ff\ff\ff\ff\4d\9a\46\47\4a\1b\89\fc\05\76\5a\e2\00\00\00\00\ff\ff\ff\ff\00\00\00\00\ea\e9\bb\3b\2e\8c\c2\d4\00\00\00\00\00\00\00\00\01\f4\da\88\11\bd\fb\90\00\00\00\00\7a\e0\41\f3\db\72\1b\ad\ff\ff\ff\ff\a3\69\41\cc\23\e4\e7\65\2c\68\81\1a\86\68\83\66\00\00\00\00\00\00\00\00\00\00\00\00\ff\ff\ff\7f\ff\ff\ff\ff\7f\4c\b9\26\ff\ff\ff\ff\fa\39\87\06\00\00\00\00\9d\67\07\26\01\00\00\00\00\00\00\80\ff\ff\ff\ff\68\2c\fb\7a\00\00\00\00\ff\ff\ff\ff\fe\ff\ff\ff\bb\dd\2f\d4\00\00\00\00\ff\ff\ff\ff\35\cf\9b\5c\0b\59\0d\8b\ff\ff\ff\ff\fe\ff\ff\ff\f6\40\6f\d8\97\09\e0\5d\00\00\00\00\00\00\00\00\20\24\70\c7\fe\ff\ff\ff\60\33\bf\cf\00\00\00\00\67\51\68\bd\00\00\00\00\6a\f3\26\7e\36\3e\6b\07\ff\ff\ff\ff\52\40\59\42\29\64\ea\9a\00\00\00\80\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\24\47\39\fc\6b\c8\26\d7\43\47\c1\e8\fe\ff\ff\ff\09\01\b2\1e\70\00\45\c8\00\00\00\00\00\00\00\00\f9\d8\1f\55\fe\ab\55\65\e6\5c\4c\be\ff\ff\ff\ff\00\00\00\00\00\00\00\00\00\00\00\00\f4\e9\76\ce\00\00\00\00\96\54\f5\fa\ea\de\fd\ef\00\00\00\00\7e\28\88\21\ff\ff\ff\ff\f9\44\4f\1a\0b\09\02\ef\b3\80\8e\fc\00\00\00\00\ff\ff\ff\ff\00\00\00\00\4c\fd\93\3d\02\09\74\53\06\68\44\6b\00\00\00\00\f0\96\91\5a\51\f3\97\a9\5b\4b\ae\6b\7d\b3\79\21\00\00\00\00\8d\d7\c9\04\92\1b\c9\c6\11\b8\01\01\00\00\00\00\00\00\00\00\2e\44\a6\b9\ca\31\cf\0f\54\80\b2\84\d1\44\84\7b\ff\ff\ff\ff\ff\ff\ff\ff\00\00\00\00\ff\ff\ff\ff\2c\cd\c1\73\ff\ff\ff\ff\43\6a\5b\53\c0\d6\2b\9b\00\00\00\00\00\00\00\80\15\49\af\ce\ce\37\06\f1\5b\11\f1\85\ff\ff\ff\7f\14\3e\1b\23\ff\ff\ff\ff\54\0d\e4\50\79\80\9a\3d\ff\ff\ff\ff\ac\fe\82\4d\ff\ff\ff\ff\c4\a9\b9\a4\ff\ff\ff\ff\00\00\00\00\00\00\00\00\ff\ff\ff\ff\a5\f1\ac\29\15\18\1b\d5\00\00\00\00\e4\8a\7b\fe\cd\50\d0\56\00\00\00\00\00\00\00\00\00\00\00\00\89\72\6b\75\47\05\a1\04\ff\ff\ff\7f\ff\ff\ff\7f\ff\ff\ff\ff\ff\ff\ff\ff\01\00\00\00\00\00\00\00\00\00\00\00\37\9b\a8\d1\00\00\00\80\e3\d2\4e\eb\1d\e5\9e\7e\00\00\00\00\ff\ff\ff\ff\00\00\00\00\ff\ff\ff\ff\ff\ff\ff\ff\ff\ff\ff\ff\ff\ff\ff\ff\00\00\00\00\00\00\00\00\ff\ff\ff\ff\48\57\f2\6a\ff\ff\ff\7f\ff\ff\ff\ff\0b\27\0a\3d\ff\ff\ff\ff\00\00\00\00\00\00\00\00\71\d5\14\4e\00\00\00\00\74\ce\05\80\00\00\00\00\00\00\00\00\ff\ff\ff\ff\ff\ff\ff\ff\37\8e\11\8d\00\00\00\00\5a\da\e4\3e\1b\41\35\1b\a1\30\a3\6e\11\83\c0\8b\ae\64\66\f8\fe\ff\ff\ff\00\00\00\00\00\00\00\00\ba\62\cf\a2\30\d0\4b\fd\ff\ff\ff\ff")
  (data (i32.const 1024) "\00\00\00\80\33\2f\8b\12\f8\c9\9d\5d\94\5d\e2\e8\d8\50\99\09\01\00\00\00\24\17\9c\3d\ff\ff\ff\ff\af\94\ac\d3\5d\10\8c\f2\ff\ff\ff\ff\a9\0d\9d\f2\ff\ff\ff\ff\9c\e2\b1\0c\00\00\00\00\ad\be\17\22\ff\ff\ff\ff\58\66\27\92\fe\ff\ff\ff\00\00\00\00\47\d5\8f\a3\01\00\00\00\ff\ff\ff\ff\c3\70\7a\90\ff\ff\ff\ff\01\00\00\00\7e\b0\76\6d\ff\ff\ff\7f\ad\87\bd\4c\00\00\00\00\fb\1b\7d\3e\ff\ff\ff\ff\00\00\00\80\00\00\00\80\00\00\00\00\ff\ff\ff\ff\ff\fc\d3\c1\00\00\00\80\f4\e1\46\f6\9e\ea\ba\c3\5e\13\02\ca\fe\ff\ff\ff\00\00\00\80\6a\df\c9\74\00\00\00\00\fe\ff\ff\ff\0b\08\88\0f\cb\6d\42\4f\0b\89\3b\fe\70\83\15\72\00\00\00\80\07\af\c6\05\e6\37\05\2b\0a\aa\62\7e\00\00\00\00\fe\ff\ff\ff\01\00\00\00\b0\82\15\df\1a\fb\96\2a\11\18\a8\8c\00\00\00\80\3a\6a\d6\b4\40\6d\d8\5b\c9\99\64\61\00\00\00\00\00\00\00\00\00\00\00\00\9e\90\16\03\5f\cc\d0\96\01\00\00\00\ff\ff\ff\ff\ed\66\87\5e\f5\88\90\51\ff\ff\ff\ff\f4\8e\1a\9e\81\88\62\bd\01\00\00\00\c8\6e\2c\8f\04\7a\23\66\5c\14\45\7b\28\79\ef\0f\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\b3\d7\5f\89\01\00\00\00\9d\33\00\12\ff\ff\ff\ff\87\aa\68\a2\ff\ff\ff\7f\00\00\00\00\01\00\00\00\8b\96\dc\7b\ff\ff\ff\ff\ff\ff\ff\ff\01\00\00\00\f6\a1\2a\b1\ff\ff\ff\ff\00\00\00\00\6b\be\87\25\43\75\05\ea\ff\ff\ff\ff\00\00\00\00\08\72\d8\42\d4\a1\83\e8\01\00\00\00\ff\ff\ff\ff\8b\c0\b0\80\ff\ff\ff\ff\b1\88\d4\c9\00\00\00\00\fd\c6\a6\cd\00\00\00\00\8e\25\06\5b\26\a9\56\fd\01\00\00\00\04\b7\92\31\08\2c\de\f4\fe\ff\ff\ff\00\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\01\00\00\00\75\9b\7a\e6\34\10\7d\00\b9\91\29\a7\00\00\00\00\89\27\e7\e8\71\67\24\b6\01\00\00\00\01\00\00\00\03\3d\35\16\6f\b0\91\76\ed\49\61\f2\ff\ff\ff\ff\00\00\00\00\00\00\00\00\ff\ff\ff\ff\9b\52\e6\a7\ff\ff\ff\ff\ad\74\6f\79\92\4e\b4\59\ff\ff\ff\ff\b2\40\7a\05\ff\ff\ff\ff\f9\03\ce\86\88\ef\a5\23\79\8b\2a\df\00\00\00\00\01\00\00\00\01\00\00\00\a0\84\80\c3\31\bb\65\42\b4\cd\8d\d5\00\00\00\00\fa\ed\cf\e5\5e\58\56\95\01\6d\bf\d3\56\ae\25\88\ff\ff\ff\ff\17\30\70\df\9d\28\e0\2e\55\7d\aa\c6\00\00\00\00\01\00\00\00\5d\61\ce\1e\e5\90\73\53\b7\ae\dd\87\b2\14\c6\c8\ff\ff\ff\ff\00\00\00\00\01\00\00\00\00\00\00\00\40\7f\cd\8f\00\00\00\00\98\a0\ce\9c\bf\03\1d\83\ff\ff\ff\ff\ff\ff\ff\7f\83\96\60\7a\de\5e\66\3f\14\a1\64\e0\ff\ff\ff\ff\e0\b9\a8\6a\01\00\00\00\85\61\92\12\87\9a\a7\6d\00\00\00\00\ee\07\b0\c8\00\00\00\00\2f\92\06\a9\01\00\00\00\01\00\00\00\ff\ff\ff\ff\01\00\00\00\3b\37\68\fd\6b\33\45\39\ff\ff\ff\ff\7b\b1\fe\83\76\c6\d8\6b\01\00\00\00\fe\ff\ff\ff\01\00\00\00\a1\dc\c1\70\ba\67\64\62\ff\ff\ff\7f\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\82\28\2d\f2\ff\ff\ff\ff\28\cb\c8\83\ce\8e\4e\b3\01\00\00\00\ff\ff\ff\ff\01\00\00\00\01\00\00\00\ff\ff\ff\ff\01\00\00\00\ff\ff\ff\ff\00\00\00\00\00\00\00\80\01\00\00\00\a8\32\3a\ed\ff\ff\ff\ff\ff\ff\ff\ff\31\05\29\f0\00\00\00\00\ff\ff\ff\ff\01\00\00\00\dd\3d\f5\87\01\00\00\00\93\7e\12\ac\01\00\00\00\01\00\00\00\00\00\00\00\ff\ff\ff\ff\74\97\61\fa\ff\ff\ff\ff\d5\c0\44\ef\22\ae\87\a8\a1\00\11\a8\2a\42\a9\d5\b5\2b\b6\81\ff\ff\ff\ff\01\00\00\00\ff\ff\ff\ff\ae\9c\c4\23\ea\2d\f9\58\00\00\00\00")
  ;; doubles c, recursively so that it is not inlined
  (func $grow (param $c i32) (param $k i32) (result i32)
    (if (result i32) (i32.eqz (local.get $k))
      (then (i32.shl (local.get $c) (i32.const 1)))
      (else (call $grow (local.get $c) (i32.sub (local.get $k) (i32.const 1))))))
  (func $hash (param $w i32) (param $h i32) (result i32)
    (local $p i32)
    (local.set $p (i32.const 2048))
    (block $done
      (loop $l
        (br_if $done (i32.eqz (local.get $w)))
        (local.set $h (i32.add (i32.mul (local.get $h) (i32.const 31)) (i32.load (local.get $p))))
        (local.set $p (i32.add (local.get $p) (i32.const 4)))
        (local.set $w (i32.sub (local.get $w) (i32.const 1)))
        (br $l)))
    (local.get $h))
  (func $add_or (export "add_or") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $u)) (i32.lt_u (local.get $r) (local.get $s))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 4096)))
  (func $add_gt (export "add_gt") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.gt_u (local.get $u) (local.get $s)) (i32.gt_u (local.get $s) (local.get $r))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 8192)))
  (func $add_vcin (export "add_vcin") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $v)) (i32.lt_u (local.get $r) (local.get $cy))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 12288)))
  (func $add_xor (export "add_xor") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.xor (i32.lt_u (local.get $r) (local.get $s)) (i32.lt_u (local.get $s) (local.get $u))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 16384)))
  (func $add_swap (export "add_swap") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $cy) (local.get $s)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $u)) (i32.lt_u (local.get $r) (local.get $s))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 20480)))
  (func $add64 (export "add64") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i64) (local $u i64) (local $v i64) (local $s i64) (local $r i64)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i64.load offset=0 (local.get $up)))
        (local.set $v (i64.load offset=0 (local.get $vp)))
        (local.set $s (i64.add (local.get $u) (local.get $v)))
        (local.set $r (i64.add (local.get $s) (local.get $cy)))
        (i64.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i64.extend_i32_u (i32.or (i64.lt_u (local.get $s) (local.get $u)) (i64.lt_u (local.get $r) (local.get $s)))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 8)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 8)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 8)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 2)) (i32.wrap_i64 (local.get $cy))) (local.get $z))
             (i32.const 24576)))
  (func $add2 (export "add2") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $u)) (i32.lt_u (local.get $r) (local.get $s))))
        (local.set $u (i32.load offset=4 (local.get $up)))
        (local.set $v (i32.load offset=4 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=4 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $u)) (i32.lt_u (local.get $r) (local.get $s))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 8)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 8)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 8)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 2)) (local.get $cy)) (local.get $z))
             (i32.const 28672)))
  (func $add2_64 (export "add2_64") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i64) (local $u i64) (local $v i64) (local $s i64) (local $r i64)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i64.load offset=0 (local.get $up)))
        (local.set $v (i64.load offset=0 (local.get $vp)))
        (local.set $s (i64.add (local.get $u) (local.get $v)))
        (local.set $r (i64.add (local.get $s) (local.get $cy)))
        (i64.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i64.extend_i32_u (i32.or (i64.lt_u (local.get $s) (local.get $v)) (i64.lt_u (local.get $r) (local.get $s)))))
        (local.set $u (i64.load offset=8 (local.get $up)))
        (local.set $v (i64.load offset=8 (local.get $vp)))
        (local.set $s (i64.add (local.get $u) (local.get $v)))
        (local.set $r (i64.add (local.get $s) (local.get $cy)))
        (i64.store offset=8 (local.get $rp) (local.get $r))
        (local.set $cy (i64.extend_i32_u (i32.or (i64.lt_u (local.get $s) (local.get $v)) (i64.lt_u (local.get $r) (local.get $s)))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 16)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 16)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 16)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 4)) (i32.wrap_i64 (local.get $cy))) (local.get $z))
             (i32.const 32768)))
  (func $sub_or (export "sub_or") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.sub (local.get $u) (local.get $v)))
        (local.set $r (i32.sub (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $u) (local.get $v)) (i32.lt_u (local.get $s) (local.get $cy))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 36864)))
  (func $sub_gt (export "sub_gt") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.sub (local.get $u) (local.get $v)))
        (local.set $r (i32.sub (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.gt_u (local.get $s) (local.get $u)) (i32.gt_u (local.get $r) (local.get $s))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 40960)))
  (func $sub_mix (export "sub_mix") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.sub (local.get $u) (local.get $v)))
        (local.set $r (i32.sub (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.xor (i32.lt_u (local.get $u) (local.get $s)) (i32.lt_u (local.get $s) (local.get $r))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 45056)))
  (func $sub64 (export "sub64") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i64) (local $u i64) (local $v i64) (local $s i64) (local $r i64)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i64.load offset=0 (local.get $up)))
        (local.set $v (i64.load offset=0 (local.get $vp)))
        (local.set $s (i64.sub (local.get $u) (local.get $v)))
        (local.set $r (i64.sub (local.get $s) (local.get $cy)))
        (i64.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i64.extend_i32_u (i32.or (i64.lt_u (local.get $u) (local.get $v)) (i64.lt_u (local.get $s) (local.get $cy)))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 8)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 8)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 8)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 2)) (i32.wrap_i64 (local.get $cy))) (local.get $z))
             (i32.const 49152)))
  (func $sub2 (export "sub2") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.sub (local.get $u) (local.get $v)))
        (local.set $r (i32.sub (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $u) (local.get $v)) (i32.lt_u (local.get $s) (local.get $cy))))
        (local.set $u (i32.load offset=4 (local.get $up)))
        (local.set $v (i32.load offset=4 (local.get $vp)))
        (local.set $s (i32.sub (local.get $u) (local.get $v)))
        (local.set $r (i32.sub (local.get $s) (local.get $cy)))
        (i32.store offset=4 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $u) (local.get $v)) (i32.lt_u (local.get $s) (local.get $cy))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 8)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 8)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 8)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 2)) (local.get $cy)) (local.get $z))
             (i32.const 53248)))
  (func $add_notc1 (export "add_notc1") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $r)) (i32.lt_u (local.get $r) (local.get $s))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 57344)))
  (func $add_notc2 (export "add_notc2") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $u)) (i32.lt_u (local.get $r) (local.get $u))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 61440)))
  (func $add_le (export "add_le") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $u)) (i32.le_u (local.get $r) (local.get $s))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 65536)))
  (func $sub_notb1 (export "sub_notb1") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.sub (local.get $u) (local.get $v)))
        (local.set $r (i32.sub (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $v) (local.get $u)) (i32.lt_u (local.get $s) (local.get $cy))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 69632)))
  (func $sub_notb1y (export "sub_notb1y") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.sub (local.get $u) (local.get $v)))
        (local.set $r (i32.sub (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $u) (local.get $cy)) (i32.lt_u (local.get $s) (local.get $cy))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 73728)))
  (func $sub_addcmp (export "sub_addcmp") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.sub (local.get $u) (local.get $v)))
        (local.set $r (i32.sub (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $u)) (i32.lt_u (local.get $r) (local.get $s))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 77824)))
  (func $add_cy3 (export "add_cy3") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (local.set $cy (i32.const 6))
    (block $out
      (loop $l
      (local.set $cy (i32.and (local.get $cy) (i32.const 3)))
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.add (i32.lt_u (local.get $s) (local.get $u)) (i32.lt_u (local.get $r) (local.get $s))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 81920)))
  (func $add_add (export "add_add") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $cy (i32.and (local.get $cy) (i32.const 1)))
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.add (i32.lt_u (local.get $s) (local.get $u)) (i32.lt_u (local.get $r) (local.get $s))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 86016)))
  (func $add_sused (export "add_sused") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $u)) (i32.lt_u (local.get $r) (local.get $s))))
        (local.set $z (i32.xor (local.get $z) (local.get $s)))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 90112)))
  (func $add2_clobber (export "add2_clobber") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $u)) (i32.lt_u (local.get $r) (local.get $s))))
        (local.set $z (i32.add (local.get $z) (select (i32.const 3) (i32.const 1)
          (i32.lt_u (local.get $r) (local.get $z)))))
        (local.set $u (i32.load offset=4 (local.get $up)))
        (local.set $v (i32.load offset=4 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=4 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $u)) (i32.lt_u (local.get $r) (local.get $s))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 8)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 8)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 8)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 2)) (local.get $cy)) (local.get $z))
             (i32.const 94208)))
  (func $add_c1used (export "add_c1used") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $z (i32.add (local.get $z) (i32.lt_u (local.get $s) (local.get $u))))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $u)) (i32.lt_u (local.get $r) (local.get $s))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 98304)))
  (func $add_c2used (export "add_c2used") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $z (i32.add (local.get $z) (i32.lt_u (local.get $r) (local.get $s))))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $u)) (i32.lt_u (local.get $r) (local.get $s))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 102400)))
  (func $add_shl (export "add_shl") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.shl (i32.load offset=0 (local.get $vp)) (i32.const 1)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $u)) (i32.lt_u (local.get $r) (local.get $s))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 106496)))
  (func $add_split (export "add_split") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (if (i32.eq (local.get $u) (i32.const 1592614637)) (then (i32.store (i32.const 4000) (local.get $r))))
        (local.set $z (i32.add (i32.mul (local.get $z) (i32.const 3)) (local.get $u)))
        (local.set $z (i32.add (i32.mul (local.get $z) (i32.const 3)) (local.get $u)))
        (local.set $z (i32.add (i32.mul (local.get $z) (i32.const 3)) (local.get $u)))
        (local.set $z (i32.add (i32.mul (local.get $z) (i32.const 3)) (local.get $u)))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $u)) (i32.lt_u (local.get $r) (local.get $s))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 110592)))
  (func $add_subs (export "add_subs") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.sub (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $u)) (i32.lt_u (local.get $r) (local.get $s))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 114688)))
  (func $add_shrcin (export "add_shrcin") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (i32.shr_u (local.get $v) (i32.const 31))))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $u)) (i32.lt_u (local.get $r) (local.get $s))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 118784)))
  (func $add2_call (export "add2_call") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $u)) (i32.lt_u (local.get $r) (local.get $s))))
        (if (i32.and (local.get $u) (i32.const 1))
          (then (local.set $z (i32.add (local.get $z)
                  (call $grow (i32.or (local.get $z) (i32.const 1)) (i32.const 2))))))
        (local.set $u (i32.load offset=4 (local.get $up)))
        (local.set $v (i32.load offset=4 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=4 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $u)) (i32.lt_u (local.get $r) (local.get $s))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 8)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 8)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 8)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 2)) (local.get $cy)) (local.get $z))
             (i32.const 122880)))
  (func $add_between (export "add_between") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $z (i32.add (local.get $z) (select (i32.const 3) (i32.const 1)
          (i32.lt_u (local.get $r) (local.get $z)))))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $u)) (i32.lt_u (local.get $r) (local.get $s))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 126976)))
  (func $add_then_sub (export "add_then_sub") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.add (local.get $u) (local.get $v)))
        (local.set $r (i32.add (local.get $s) (local.get $cy)))
        (i32.store offset=0 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $s) (local.get $u)) (i32.lt_u (local.get $r) (local.get $s))))
        (local.set $u (i32.load offset=4 (local.get $up)))
        (local.set $v (i32.load offset=4 (local.get $vp)))
        (local.set $s (i32.sub (local.get $u) (local.get $v)))
        (local.set $r (i32.sub (local.get $s) (local.get $cy)))
        (i32.store offset=4 (local.get $rp) (local.get $r))
        (local.set $cy (i32.or (i32.lt_u (local.get $u) (local.get $v)) (i32.lt_u (local.get $s) (local.get $cy))))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 8)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 8)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 8)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 2)) (local.get $cy)) (local.get $z))
             (i32.const 131072)))
  (func $sub_late (export "sub_late") (param $n i32) (result i32)
    (local $up i32) (local $vp i32) (local $rp i32) (local $k i32) (local $z i32)
    (local $cy i32) (local $u i32) (local $v i32) (local $s i32) (local $r i32)
    (local.set $vp (i32.const 1024))
    (local.set $rp (i32.const 2048))
    (local.set $k (local.get $n))
    (block $out
      (loop $l
        (local.set $u (i32.load offset=0 (local.get $up)))
        (local.set $v (i32.load offset=0 (local.get $vp)))
        (local.set $s (i32.sub (local.get $u) (local.get $v)))
        (local.set $v (i32.or (i32.lt_u (local.get $u) (local.get $v)) (i32.lt_u (local.get $s) (local.get $cy))))
        (local.set $z (i32.add (i32.mul (local.get $z) (i32.const 3)) (local.get $v)))
        (i32.store offset=0 (local.get $rp) (i32.sub (local.get $s) (local.get $cy)))
        (local.set $cy (local.get $v))
        (br_if $out (i32.eq (i32.load (local.get $up)) (i32.const 1592614637)))
        (local.set $up (i32.add (local.get $up) (i32.const 4)))
        (local.set $vp (i32.add (local.get $vp) (i32.const 4)))
        (local.set $rp (i32.add (local.get $rp) (i32.const 4)))
        (br_if $l (local.tee $k (i32.sub (local.get $k) (i32.const 1))))))
    (i32.add (i32.xor (call $hash (i32.mul (local.get $n) (i32.const 1)) (local.get $cy)) (local.get $z))
             (i32.const 135168)))
)
