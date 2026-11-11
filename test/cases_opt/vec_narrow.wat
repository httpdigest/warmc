;; What a lane narrower than its values' type compares, takes the lesser
;; or the greater of, and extends (opt.c v_mask and v_build): the lane's own
;; compare where both values are the lane's by ir_known_bits or ir_sign_ge.
;; Each function fills two arrays by its second argument, runs its loop over
;; the first n elements and hashes the memory.  From test/gen_vec_sat.py.
;;
;;   ltu_u8 lts_u8 ges_u8 geu_u16
;;               unsigned bytes and halfwords: every relation the lane's
;;               unsigned one, a signed relation too -- none is negative
;;   lts_s8 les_s16 ltu_s8 gtu_s16 eq_s8 ne_u16
;;               signed ones: each relation as it is, the unsigned ones too
;;   lts_mix ltu_mix eq_mix
;;               a signed byte against an unsigned one: no byte's compare
;;   umin_u8 smin_u8 smax_u16 smin_s8 smax_s16 umax_s8 umin_s16
;;               the lesser or the greater: the lane's min or max, the
;;               unsigned one for unsigned values whatever the relation
;;   smin_mix umax_mix
;;               of a signed byte and an unsigned one: no byte's min or max
;;   ext8_h      a halfword's sum extended from bit 8: a shift up and down
;;   ext8_b ext16_b ext16_h ext32_h
;;               a byte's sum extended from bit 8 or 16, a halfword's from
;;               bit 16 and a product of two, made in 64 bits, from bit 32:
;;               the lane as it is
;;
;; shape: ltu_u8 \scmhi\.16b\s=5
;; shape: lts_u8 \scmhi\.16b\s=5
;; shape: lts_u8 \scmgt\.=0
;; shape: ges_u8 \scmhs\.16b\s=5
;; shape: ges_u8 \scmge\.=0
;; shape: geu_u16 \scmhs\.8h\s=5
;; shape: lts_s8 \scmgt\.16b\s=5
;; shape: les_s16 \scmge\.8h\s=5
;; shape: ltu_s8 \scmhi\.16b\s=5
;; shape: gtu_s16 \scmhi\.8h\s=5
;; shape: eq_s8 \scmeq\.16b\s=5
;; shape: ne_u16 \scmeq\.8h\s=5
;; shape: lts_mix \scm(gt|hi)\.16b\s=0
;; shape: ltu_mix \scm(gt|hi)\.16b\s=0
;; shape: eq_mix \scmeq\.16b\s=0
;; shape: umin_u8 \sumin\.16b\s=5
;; shape: smin_u8 \sumin\.16b\s=5
;; shape: smin_u8 \ssmin\.=0
;; shape: smax_u16 \sumax\.8h\s=5
;; shape: smax_u16 \ssmax\.=0
;; shape: smin_s8 \ssmin\.16b\s=5
;; shape: smax_s16 \ssmax\.8h\s=5
;; shape: umax_s8 \sumax\.16b\s=5
;; shape: umin_s16 \sumin\.8h\s=5
;; shape: smin_mix \s[su]min\.16b\s=0
;; shape: umax_mix \s[su]max\.16b\s=0
;; shape: ext8_h \sshl\.8h\s=5
;; shape: ext8_h \ssshr\.8h\s=5
;; shape: ext8_b \sadd\.16b\s=5
;; shape: ext8_b \ssshr\.=0
;; shape: ext16_b \sadd\.16b\s=5
;; shape: ext16_b \ssshr\.=0
;; shape: ext16_h \sadd\.8h\s=5
;; shape: ext16_h \ssshr\.=0
;; shape: ext32_h \smul\.8h\s=5
;; shape: ext32_h \ssshr\.=0
;;
;; run: ltu_u8 0 1 => i64:697877659462342111
;; run: ltu_u8 15 1 => i64:-9194821167985332948
;; run: ltu_u8 16 1 => i64:-3051448705121234813
;; run: ltu_u8 17 1 => i64:7613175253683141850
;; run: ltu_u8 100 1 => i64:7634688624312843222
;; run: ltu_u8 0 3735928559 => i64:-1401310947981892218
;; run: ltu_u8 15 3735928559 => i64:7943770669837081399
;; run: ltu_u8 16 3735928559 => i64:4977057460527291646
;; run: ltu_u8 17 3735928559 => i64:-5281904866099019790
;; run: ltu_u8 100 3735928559 => i64:-670191781532047314
;; run: lts_u8 0 1 => i64:697877659462342111
;; run: lts_u8 15 1 => i64:-9194821167985332948
;; run: lts_u8 16 1 => i64:-3051448705121234813
;; run: lts_u8 17 1 => i64:7613175253683141850
;; run: lts_u8 100 1 => i64:7634688624312843222
;; run: lts_u8 0 3735928559 => i64:-1401310947981892218
;; run: lts_u8 15 3735928559 => i64:7943770669837081399
;; run: lts_u8 16 3735928559 => i64:4977057460527291646
;; run: lts_u8 17 3735928559 => i64:-5281904866099019790
;; run: lts_u8 100 3735928559 => i64:-670191781532047314
;; run: ges_u8 0 1 => i64:697877659462342111
;; run: ges_u8 15 1 => i64:5844062752657947342
;; run: ges_u8 16 1 => i64:6983038824885836812
;; run: ges_u8 17 1 => i64:-8211861297601208817
;; run: ges_u8 100 1 => i64:7555372517828546530
;; run: ges_u8 0 3735928559 => i64:-1401310947981892218
;; run: ges_u8 15 3735928559 => i64:-4900829549358802994
;; run: ges_u8 16 3735928559 => i64:-6353093667074846751
;; run: ges_u8 17 3735928559 => i64:6720094306445327119
;; run: ges_u8 100 3735928559 => i64:-6486445699604966878
;; run: geu_u16 0 1 => i64:697877659462342111
;; run: geu_u16 15 1 => i64:2812434991688997832
;; run: geu_u16 16 1 => i64:-1159173695113502239
;; run: geu_u16 17 1 => i64:4372161663817617180
;; run: geu_u16 100 1 => i64:-2497805456014537864
;; run: geu_u16 0 3735928559 => i64:-1401310947981892218
;; run: geu_u16 15 3735928559 => i64:-646218766804127963
;; run: geu_u16 16 3735928559 => i64:2324530033677707532
;; run: geu_u16 17 3735928559 => i64:4514441769495907590
;; run: geu_u16 100 3735928559 => i64:-3198491703922429970
;; run: lts_s8 0 1 => i64:697877659462342111
;; run: lts_s8 15 1 => i64:-5307001958204587632
;; run: lts_s8 16 1 => i64:7390748376240004434
;; run: lts_s8 17 1 => i64:-506951740225507339
;; run: lts_s8 100 1 => i64:4013997136373919045
;; run: lts_s8 0 3735928559 => i64:-1401310947981892218
;; run: lts_s8 15 3735928559 => i64:482113422260285911
;; run: lts_s8 16 3735928559 => i64:-7550532710909552674
;; run: lts_s8 17 3735928559 => i64:8472343725739671826
;; run: lts_s8 100 3735928559 => i64:7702473395732048553
;; run: les_s16 0 1 => i64:697877659462342111
;; run: les_s16 15 1 => i64:-561087280493390727
;; run: les_s16 16 1 => i64:2360837530838883673
;; run: les_s16 17 1 => i64:-7637655439098322358
;; run: les_s16 100 1 => i64:5229160409722621634
;; run: les_s16 0 3735928559 => i64:-1401310947981892218
;; run: les_s16 15 3735928559 => i64:8384599758488355459
;; run: les_s16 16 3735928559 => i64:2917310791126878608
;; run: les_s16 17 3735928559 => i64:-4707420360084036848
;; run: les_s16 100 3735928559 => i64:-2574045471412988835
;; run: ltu_s8 0 1 => i64:697877659462342111
;; run: ltu_s8 15 1 => i64:-9194821167985332948
;; run: ltu_s8 16 1 => i64:-3051448705121234813
;; run: ltu_s8 17 1 => i64:7613175253683141850
;; run: ltu_s8 100 1 => i64:7634688624312843222
;; run: ltu_s8 0 3735928559 => i64:-1401310947981892218
;; run: ltu_s8 15 3735928559 => i64:7943770669837081399
;; run: ltu_s8 16 3735928559 => i64:4977057460527291646
;; run: ltu_s8 17 3735928559 => i64:-5281904866099019790
;; run: ltu_s8 100 3735928559 => i64:-670191781532047314
;; run: gtu_s16 0 1 => i64:697877659462342111
;; run: gtu_s16 15 1 => i64:2812434991688997832
;; run: gtu_s16 16 1 => i64:-1159173695113502239
;; run: gtu_s16 17 1 => i64:4372161663817617180
;; run: gtu_s16 100 1 => i64:-2497805456014537864
;; run: gtu_s16 0 3735928559 => i64:-1401310947981892218
;; run: gtu_s16 15 3735928559 => i64:-646218766804127963
;; run: gtu_s16 16 3735928559 => i64:2324530033677707532
;; run: gtu_s16 17 3735928559 => i64:4514441769495907590
;; run: gtu_s16 100 3735928559 => i64:-3198491703922429970
;; run: eq_s8 0 1 => i64:697877659462342111
;; run: eq_s8 15 1 => i64:-1375530610098904369
;; run: eq_s8 16 1 => i64:3559791291049144013
;; run: eq_s8 17 1 => i64:3178704713658915962
;; run: eq_s8 100 1 => i64:476991697041576291
;; run: eq_s8 0 3735928559 => i64:-1401310947981892218
;; run: eq_s8 15 3735928559 => i64:-9182401394150104643
;; run: eq_s8 16 3735928559 => i64:-7787160855724531404
;; run: eq_s8 17 3735928559 => i64:1284539201148536390
;; run: eq_s8 100 3735928559 => i64:-5020612923986186547
;; run: ne_u16 0 1 => i64:697877659462342111
;; run: ne_u16 15 1 => i64:-4622271195969522485
;; run: ne_u16 16 1 => i64:7255307468034765051
;; run: ne_u16 17 1 => i64:5272512343931071552
;; run: ne_u16 100 1 => i64:-284946906523578472
;; run: ne_u16 0 3735928559 => i64:-1401310947981892218
;; run: ne_u16 15 3735928559 => i64:-2529058789295091623
;; run: ne_u16 16 3735928559 => i64:8303985765915731886
;; run: ne_u16 17 3735928559 => i64:7024639476564801784
;; run: ne_u16 100 3735928559 => i64:-6104139284493429010
;; run: lts_mix 0 1 => i64:697877659462342111
;; run: lts_mix 15 1 => i64:-5384763324651784406
;; run: lts_mix 16 1 => i64:-2034550575422654779
;; run: lts_mix 17 1 => i64:8652312089982511000
;; run: lts_mix 100 1 => i64:8410986842719771675
;; run: lts_mix 0 3735928559 => i64:-1401310947981892218
;; run: lts_mix 15 3735928559 => i64:-3632895637562766626
;; run: lts_mix 16 3735928559 => i64:4399750495607071959
;; run: lts_mix 17 3735928559 => i64:517038635846710755
;; run: lts_mix 100 3735928559 => i64:5167334367344445428
;; run: ltu_mix 0 1 => i64:697877659462342111
;; run: ltu_mix 15 1 => i64:6209890916747695734
;; run: ltu_mix 16 1 => i64:6379347765059845145
;; run: ltu_mix 17 1 => i64:-614706638420730812
;; run: ltu_mix 100 1 => i64:-7278597034704439481
;; run: ltu_mix 0 3735928559 => i64:-1401310947981892218
;; run: ltu_mix 15 3735928559 => i64:-5241214828273402873
;; run: ltu_mix 16 3735928559 => i64:1683491503270036878
;; run: ltu_mix 17 3735928559 => i64:-8743829763672418686
;; run: ltu_mix 100 3735928559 => i64:2281522200489905091
;; run: eq_mix 0 1 => i64:697877659462342111
;; run: eq_mix 15 1 => i64:-1375530610098904369
;; run: eq_mix 16 1 => i64:3559791291049144013
;; run: eq_mix 17 1 => i64:3178704713658915962
;; run: eq_mix 100 1 => i64:476991697041576291
;; run: eq_mix 0 3735928559 => i64:-1401310947981892218
;; run: eq_mix 15 3735928559 => i64:-9182401394150104643
;; run: eq_mix 16 3735928559 => i64:-7787160855724531404
;; run: eq_mix 17 3735928559 => i64:1284539201148536390
;; run: eq_mix 100 3735928559 => i64:-5020612923986186547
;; run: umin_u8 0 1 => i64:697877659462342111
;; run: umin_u8 15 1 => i64:3619694313160895902
;; run: umin_u8 16 1 => i64:4215440601171678148
;; run: umin_u8 17 1 => i64:8420778870849169722
;; run: umin_u8 100 1 => i64:-2119278530493902736
;; run: umin_u8 0 3735928559 => i64:-1401310947981892218
;; run: umin_u8 15 3735928559 => i64:6490619117536043776
;; run: umin_u8 16 3735928559 => i64:8800145828903160393
;; run: umin_u8 17 3735928559 => i64:6902727638338693286
;; run: umin_u8 100 3735928559 => i64:-4502179050108988749
;; run: smin_u8 0 1 => i64:697877659462342111
;; run: smin_u8 15 1 => i64:3619694313160895902
;; run: smin_u8 16 1 => i64:4215440601171678148
;; run: smin_u8 17 1 => i64:8420778870849169722
;; run: smin_u8 100 1 => i64:-2119278530493902736
;; run: smin_u8 0 3735928559 => i64:-1401310947981892218
;; run: smin_u8 15 3735928559 => i64:6490619117536043776
;; run: smin_u8 16 3735928559 => i64:8800145828903160393
;; run: smin_u8 17 3735928559 => i64:6902727638338693286
;; run: smin_u8 100 3735928559 => i64:-4502179050108988749
;; run: smax_u16 0 1 => i64:697877659462342111
;; run: smax_u16 15 1 => i64:2536753016143403839
;; run: smax_u16 16 1 => i64:3218544035874452470
;; run: smax_u16 17 1 => i64:-8744167550766707097
;; run: smax_u16 100 1 => i64:-1496161694774635036
;; run: smax_u16 0 3735928559 => i64:-1401310947981892218
;; run: smax_u16 15 3735928559 => i64:-5948256810084758047
;; run: smax_u16 16 3735928559 => i64:-1626685783680511032
;; run: smax_u16 17 3735928559 => i64:7520044979453448361
;; run: smax_u16 100 3735928559 => i64:-643700953655854310
;; run: smin_s8 0 1 => i64:697877659462342111
;; run: smin_s8 15 1 => i64:481743871691470891
;; run: smin_s8 16 1 => i64:2629942035488215849
;; run: smin_s8 17 1 => i64:8633109043911644102
;; run: smin_s8 100 1 => i64:-1877155916519700315
;; run: smin_s8 0 3735928559 => i64:-1401310947981892218
;; run: smin_s8 15 3735928559 => i64:-3077233361332751270
;; run: smin_s8 16 3735928559 => i64:5068105829969350611
;; run: smin_s8 17 3735928559 => i64:-4223826798925125264
;; run: smin_s8 100 3735928559 => i64:7933598078003860704
;; run: smax_s16 0 1 => i64:697877659462342111
;; run: smax_s16 15 1 => i64:-5967872892255856391
;; run: smax_s16 16 1 => i64:-8483952254593909840
;; run: smax_s16 17 1 => i64:-9168572082857981895
;; run: smax_s16 100 1 => i64:-2452477973204581414
;; run: smax_s16 0 3735928559 => i64:-1401310947981892218
;; run: smax_s16 15 3735928559 => i64:-896999057410361484
;; run: smax_s16 16 3735928559 => i64:-3379299620387027615
;; run: smax_s16 17 3735928559 => i64:-6693557562531419020
;; run: smax_s16 100 3735928559 => i64:5649102211839761869
;; run: umax_s8 0 1 => i64:697877659462342111
;; run: umax_s8 15 1 => i64:-6769554071391787999
;; run: umax_s8 16 1 => i64:6932563143021932579
;; run: umax_s8 17 1 => i64:-2676254826154818912
;; run: umax_s8 100 1 => i64:-1187212852949278636
;; run: umax_s8 0 3735928559 => i64:-1401310947981892218
;; run: umax_s8 15 3735928559 => i64:-474233983692828916
;; run: umax_s8 16 3735928559 => i64:-6967565232777224234
;; run: umax_s8 17 3735928559 => i64:9193264507822590404
;; run: umax_s8 100 3735928559 => i64:5931654967970063483
;; run: umin_s16 0 1 => i64:697877659462342111
;; run: umin_s16 15 1 => i64:4744570125541192199
;; run: umin_s16 16 1 => i64:4787590320040440220
;; run: umin_s16 17 1 => i64:-4414649223613519552
;; run: umin_s16 100 1 => i64:-344777035063401169
;; run: umin_s16 0 3735928559 => i64:-1401310947981892218
;; run: umin_s16 15 3735928559 => i64:3592827662490208562
;; run: umin_s16 16 3735928559 => i64:-3289580682758461446
;; run: umin_s16 17 3735928559 => i64:978793168860284318
;; run: umin_s16 100 3735928559 => i64:-5207072291312879742
;; run: smin_mix 0 1 => i64:697877659462342111
;; run: smin_mix 15 1 => i64:2199207577947363666
;; run: smin_mix 16 1 => i64:-2166316913141568888
;; run: smin_mix 17 1 => i64:3952708759192490622
;; run: smin_mix 100 1 => i64:5332642225461597811
;; run: smin_mix 0 3735928559 => i64:-1401310947981892218
;; run: smin_mix 15 3735928559 => i64:-156405946383153714
;; run: smin_mix 16 3735928559 => i64:1744800470761800647
;; run: smin_mix 17 3735928559 => i64:-1661559050588345004
;; run: smin_mix 100 3735928559 => i64:8180073749629634812
;; run: umax_mix 0 1 => i64:697877659462342111
;; run: umax_mix 15 1 => i64:-4245227948529446036
;; run: umax_mix 16 1 => i64:75437617845713582
;; run: umax_mix 17 1 => i64:3094989787178640705
;; run: umax_mix 100 1 => i64:-4705825341924277608
;; run: umax_mix 0 3735928559 => i64:-1401310947981892218
;; run: umax_mix 15 3735928559 => i64:8215887064519213254
;; run: umax_mix 16 3735928559 => i64:2576425101592375964
;; run: umax_mix 17 3735928559 => i64:-5043513452368730274
;; run: umax_mix 100 3735928559 => i64:-8434635858410736236
;; run: ext8_h 0 1 => i64:697877659462342111
;; run: ext8_h 7 1 => i64:-2527355145081266415
;; run: ext8_h 8 1 => i64:4269489189676305801
;; run: ext8_h 9 1 => i64:4278891803380013215
;; run: ext8_h 100 1 => i64:-108035814930641642
;; run: ext8_h 0 3735928559 => i64:-1401310947981892218
;; run: ext8_h 7 3735928559 => i64:1108627068792166657
;; run: ext8_h 8 3735928559 => i64:-1716607179580815101
;; run: ext8_h 9 3735928559 => i64:8405884727317467125
;; run: ext8_h 100 3735928559 => i64:7301701528384743174
;; run: ext8_b 0 1 => i64:697877659462342111
;; run: ext8_b 15 1 => i64:505874484447392450
;; run: ext8_b 16 1 => i64:5349653790217607616
;; run: ext8_b 17 1 => i64:3841651782744956411
;; run: ext8_b 100 1 => i64:-5279197354092682511
;; run: ext8_b 0 3735928559 => i64:-1401310947981892218
;; run: ext8_b 15 3735928559 => i64:-8872817927246367474
;; run: ext8_b 16 3735928559 => i64:2230842641221639111
;; run: ext8_b 17 3735928559 => i64:-4870352504300408808
;; run: ext8_b 100 3735928559 => i64:-6468962786817394370
;; run: ext16_b 0 1 => i64:697877659462342111
;; run: ext16_b 15 1 => i64:505874484447392450
;; run: ext16_b 16 1 => i64:5349653790217607616
;; run: ext16_b 17 1 => i64:3841651782744956411
;; run: ext16_b 100 1 => i64:-5279197354092682511
;; run: ext16_b 0 3735928559 => i64:-1401310947981892218
;; run: ext16_b 15 3735928559 => i64:-8872817927246367474
;; run: ext16_b 16 3735928559 => i64:2230842641221639111
;; run: ext16_b 17 3735928559 => i64:-4870352504300408808
;; run: ext16_b 100 3735928559 => i64:-6468962786817394370
;; run: ext16_h 0 1 => i64:697877659462342111
;; run: ext16_h 7 1 => i64:1088148943382214209
;; run: ext16_h 8 1 => i64:7171348183310557790
;; run: ext16_h 9 1 => i64:-2922157849606822318
;; run: ext16_h 100 1 => i64:6026283607897332148
;; run: ext16_h 0 3735928559 => i64:-1401310947981892218
;; run: ext16_h 7 3735928559 => i64:4217986780660371653
;; run: ext16_h 8 3735928559 => i64:8751023909042042084
;; run: ext16_h 9 3735928559 => i64:1180155877015410218
;; run: ext16_h 100 3735928559 => i64:7177689112447152475
;; run: ext32_h 0 1 => i64:697877659462342111
;; run: ext32_h 7 1 => i64:-2129945123690303073
;; run: ext32_h 8 1 => i64:-6934922609455605593
;; run: ext32_h 9 1 => i64:-1773820984464144690
;; run: ext32_h 100 1 => i64:3250562675950882218
;; run: ext32_h 0 3735928559 => i64:-1401310947981892218
;; run: ext32_h 7 3735928559 => i64:7237465950442684758
;; run: ext32_h 8 3735928559 => i64:-4163228659475838598
;; run: ext32_h 9 3735928559 => i64:476192111003361098
;; run: ext32_h 100 3735928559 => i64:-417439029314952114

(module
  (memory 1)
  ;; A and B, a thousand bytes each, by a generator of the seed; A starts
  ;; with the halfwords at the ends of their range
  (func $fill (param $x i32)
    (local $i i32)
    (loop $l
      (local.set $x (i32.add (i32.mul (local.get $x) (i32.const 1664525)) (i32.const 1013904223)))
      (i32.store8 (i32.add (i32.const 1024) (local.get $i)) (i32.shr_u (local.get $x) (i32.const 13)))
      (i32.store8 (i32.add (i32.const 5120) (local.get $i)) (i32.shr_u (local.get $x) (i32.const 21)))
      (i32.store8 (i32.add (i32.const 9216) (local.get $i)) (i32.const 0x5a))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 1024))))
    (i64.store (i32.const 1024) (i64.const -281472829194241))
    (i64.store (i32.const 1032) (i64.const 4611826762358063105)))
  ;; the three, hashed
  (func $sum (result i64)
    (local $i i32) (local $h i64)
    (loop $l
      (local.set $h (i64.xor (i64.mul (local.get $h) (i64.const 1099511628211))
                             (i64.load8_u (i32.add (i32.const 1024) (local.get $i)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 9216))))
    (local.get $h))
  (func $ltu_u8 (export "ltu_u8") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load8_u (i32.add (i32.const 1024) (local.get $i))))
          (local.set $u (i32.load8_u (i32.add (i32.const 5120) (local.get $i))))
          (i32.store8 (i32.add (i32.const 9216) (local.get $i)) (select (i32.xor (local.get $t) (i32.const 0x55)) (local.get $u) (i32.lt_u (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $lts_u8 (export "lts_u8") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load8_u (i32.add (i32.const 1024) (local.get $i))))
          (local.set $u (i32.load8_u (i32.add (i32.const 5120) (local.get $i))))
          (i32.store8 (i32.add (i32.const 9216) (local.get $i)) (select (i32.xor (local.get $t) (i32.const 0x55)) (local.get $u) (i32.lt_s (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $ges_u8 (export "ges_u8") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load8_u (i32.add (i32.const 1024) (local.get $i))))
          (local.set $u (i32.load8_u (i32.add (i32.const 5120) (local.get $i))))
          (i32.store8 (i32.add (i32.const 9216) (local.get $i)) (select (i32.xor (local.get $t) (i32.const 0x55)) (local.get $u) (i32.ge_s (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $geu_u16 (export "geu_u16") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load16_u (i32.add (i32.const 1024) (i32.shl (local.get $i) (i32.const 1)))))
          (local.set $u (i32.load16_u (i32.add (i32.const 5120) (i32.shl (local.get $i) (i32.const 1)))))
          (i32.store16 (i32.add (i32.const 9216) (i32.shl (local.get $i) (i32.const 1))) (select (i32.xor (local.get $t) (i32.const 0x55)) (local.get $u) (i32.ge_u (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $lts_s8 (export "lts_s8") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load8_s (i32.add (i32.const 1024) (local.get $i))))
          (local.set $u (i32.load8_s (i32.add (i32.const 5120) (local.get $i))))
          (i32.store8 (i32.add (i32.const 9216) (local.get $i)) (select (i32.xor (local.get $t) (i32.const 0x55)) (local.get $u) (i32.lt_s (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $les_s16 (export "les_s16") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load16_s (i32.add (i32.const 1024) (i32.shl (local.get $i) (i32.const 1)))))
          (local.set $u (i32.load16_s (i32.add (i32.const 5120) (i32.shl (local.get $i) (i32.const 1)))))
          (i32.store16 (i32.add (i32.const 9216) (i32.shl (local.get $i) (i32.const 1))) (select (i32.xor (local.get $t) (i32.const 0x55)) (local.get $u) (i32.le_s (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $ltu_s8 (export "ltu_s8") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load8_s (i32.add (i32.const 1024) (local.get $i))))
          (local.set $u (i32.load8_s (i32.add (i32.const 5120) (local.get $i))))
          (i32.store8 (i32.add (i32.const 9216) (local.get $i)) (select (i32.xor (local.get $t) (i32.const 0x55)) (local.get $u) (i32.lt_u (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $gtu_s16 (export "gtu_s16") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load16_s (i32.add (i32.const 1024) (i32.shl (local.get $i) (i32.const 1)))))
          (local.set $u (i32.load16_s (i32.add (i32.const 5120) (i32.shl (local.get $i) (i32.const 1)))))
          (i32.store16 (i32.add (i32.const 9216) (i32.shl (local.get $i) (i32.const 1))) (select (i32.xor (local.get $t) (i32.const 0x55)) (local.get $u) (i32.gt_u (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $eq_s8 (export "eq_s8") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load8_s (i32.add (i32.const 1024) (local.get $i))))
          (local.set $u (i32.load8_s (i32.add (i32.const 5120) (local.get $i))))
          (i32.store8 (i32.add (i32.const 9216) (local.get $i)) (select (i32.xor (local.get $t) (i32.const 0x55)) (local.get $u) (i32.eq (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $ne_u16 (export "ne_u16") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load16_u (i32.add (i32.const 1024) (i32.shl (local.get $i) (i32.const 1)))))
          (local.set $u (i32.load16_u (i32.add (i32.const 5120) (i32.shl (local.get $i) (i32.const 1)))))
          (i32.store16 (i32.add (i32.const 9216) (i32.shl (local.get $i) (i32.const 1))) (select (i32.xor (local.get $t) (i32.const 0x55)) (local.get $u) (i32.ne (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $lts_mix (export "lts_mix") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load8_s (i32.add (i32.const 1024) (local.get $i))))
          (local.set $u (i32.load8_u (i32.add (i32.const 5120) (local.get $i))))
          (i32.store8 (i32.add (i32.const 9216) (local.get $i)) (select (i32.xor (local.get $t) (i32.const 0x55)) (local.get $u) (i32.lt_s (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $ltu_mix (export "ltu_mix") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load8_u (i32.add (i32.const 1024) (local.get $i))))
          (local.set $u (i32.load8_s (i32.add (i32.const 5120) (local.get $i))))
          (i32.store8 (i32.add (i32.const 9216) (local.get $i)) (select (i32.xor (local.get $t) (i32.const 0x55)) (local.get $u) (i32.lt_u (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $eq_mix (export "eq_mix") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load8_s (i32.add (i32.const 1024) (local.get $i))))
          (local.set $u (i32.load8_u (i32.add (i32.const 5120) (local.get $i))))
          (i32.store8 (i32.add (i32.const 9216) (local.get $i)) (select (i32.xor (local.get $t) (i32.const 0x55)) (local.get $u) (i32.eq (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $umin_u8 (export "umin_u8") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load8_u (i32.add (i32.const 1024) (local.get $i))))
          (local.set $u (i32.load8_u (i32.add (i32.const 5120) (local.get $i))))
          (i32.store8 (i32.add (i32.const 9216) (local.get $i)) (select (local.get $t) (local.get $u) (i32.lt_u (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $smin_u8 (export "smin_u8") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load8_u (i32.add (i32.const 1024) (local.get $i))))
          (local.set $u (i32.load8_u (i32.add (i32.const 5120) (local.get $i))))
          (i32.store8 (i32.add (i32.const 9216) (local.get $i)) (select (local.get $t) (local.get $u) (i32.lt_s (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $smax_u16 (export "smax_u16") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load16_u (i32.add (i32.const 1024) (i32.shl (local.get $i) (i32.const 1)))))
          (local.set $u (i32.load16_u (i32.add (i32.const 5120) (i32.shl (local.get $i) (i32.const 1)))))
          (i32.store16 (i32.add (i32.const 9216) (i32.shl (local.get $i) (i32.const 1))) (select (local.get $t) (local.get $u) (i32.gt_s (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $smin_s8 (export "smin_s8") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load8_s (i32.add (i32.const 1024) (local.get $i))))
          (local.set $u (i32.load8_s (i32.add (i32.const 5120) (local.get $i))))
          (i32.store8 (i32.add (i32.const 9216) (local.get $i)) (select (local.get $t) (local.get $u) (i32.lt_s (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $smax_s16 (export "smax_s16") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load16_s (i32.add (i32.const 1024) (i32.shl (local.get $i) (i32.const 1)))))
          (local.set $u (i32.load16_s (i32.add (i32.const 5120) (i32.shl (local.get $i) (i32.const 1)))))
          (i32.store16 (i32.add (i32.const 9216) (i32.shl (local.get $i) (i32.const 1))) (select (local.get $t) (local.get $u) (i32.ge_s (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $umax_s8 (export "umax_s8") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load8_s (i32.add (i32.const 1024) (local.get $i))))
          (local.set $u (i32.load8_s (i32.add (i32.const 5120) (local.get $i))))
          (i32.store8 (i32.add (i32.const 9216) (local.get $i)) (select (local.get $t) (local.get $u) (i32.gt_u (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $umin_s16 (export "umin_s16") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load16_s (i32.add (i32.const 1024) (i32.shl (local.get $i) (i32.const 1)))))
          (local.set $u (i32.load16_s (i32.add (i32.const 5120) (i32.shl (local.get $i) (i32.const 1)))))
          (i32.store16 (i32.add (i32.const 9216) (i32.shl (local.get $i) (i32.const 1))) (select (local.get $t) (local.get $u) (i32.le_u (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $smin_mix (export "smin_mix") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load8_s (i32.add (i32.const 1024) (local.get $i))))
          (local.set $u (i32.load8_u (i32.add (i32.const 5120) (local.get $i))))
          (i32.store8 (i32.add (i32.const 9216) (local.get $i)) (select (local.get $t) (local.get $u) (i32.lt_s (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $umax_mix (export "umax_mix") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (local.set $t (i32.load8_u (i32.add (i32.const 1024) (local.get $i))))
          (local.set $u (i32.load8_s (i32.add (i32.const 5120) (local.get $i))))
          (i32.store8 (i32.add (i32.const 9216) (local.get $i)) (select (local.get $t) (local.get $u) (i32.gt_u (local.get $t) (local.get $u))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $ext8_h (export "ext8_h") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (i32.store16 (i32.add (i32.const 9216) (i32.shl (local.get $i) (i32.const 1))) (i32.extend8_s (i32.add (i32.load16_u (i32.add (i32.const 1024) (i32.shl (local.get $i) (i32.const 1)))) (i32.load16_u (i32.add (i32.const 5120) (i32.shl (local.get $i) (i32.const 1)))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $ext8_b (export "ext8_b") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (i32.store8 (i32.add (i32.const 9216) (local.get $i)) (i32.extend8_s (i32.add (i32.load8_u (i32.add (i32.const 1024) (local.get $i))) (i32.load8_u (i32.add (i32.const 5120) (local.get $i))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $ext16_b (export "ext16_b") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (i32.store8 (i32.add (i32.const 9216) (local.get $i)) (i32.extend16_s (i32.add (i32.load8_u (i32.add (i32.const 1024) (local.get $i))) (i32.load8_u (i32.add (i32.const 5120) (local.get $i))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $ext16_h (export "ext16_h") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (i32.store16 (i32.add (i32.const 9216) (i32.shl (local.get $i) (i32.const 1))) (i32.extend16_s (i32.add (i32.load16_u (i32.add (i32.const 1024) (i32.shl (local.get $i) (i32.const 1)))) (i32.load16_u (i32.add (i32.const 5120) (i32.shl (local.get $i) (i32.const 1)))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
  (func $ext32_h (export "ext32_h") (param $n i32) (param $q i32) (result i64)
    (local $i i32) (local $t i32) (local $u i32) (local $p i64) (local $g i64)
    (call $fill (local.get $q))
    (block $done
      (loop $l
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
          (i64.store16 (i32.add (i32.const 9216) (i32.shl (local.get $i) (i32.const 1))) (i64.extend32_s (i64.mul (i64.load16_u (i32.add (i32.const 1024) (i32.shl (local.get $i) (i32.const 1)))) (i64.load16_u (i32.add (i32.const 5120) (i32.shl (local.get $i) (i32.const 1)))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (call $sum))
)
