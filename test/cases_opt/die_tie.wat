;; A result takes the register it is hinted to from an operand that dies
;; where it is made (regalloc.inc, the scan's rule ahead of its hint): an
;; a64 instruction reads its operands before it writes its result.  big, of
;; more instructions than a call takes in, is what the others call.  From
;; test/gen_die_tie.py, which works out every answer.
;;
;;   arg arg2 arg_sh   a call's argument made out of the parameters in the
;;                     register it goes in: no move
;;   res res2          a call's result, and what is made of it, in x0
;;   web               a value two ways hand the function's end, one of
;;                     them out of a call's result: in x0 on both
;;   shift             no call: each value made where the last was
;;   fsq fadd          a float's
;;   live              the operand is read again after the result is made:
;;                     the result is not in its register
;;   cross cross_ret   the result lives across a call: not in a register the
;;                     call writes
;;   field             a bfi's field comes from a value it reads after it
;;                     has written: the result is not in that value's
;;                     register
;;   field2            and it writes into its other operand: in that one's
;;                     register where it dies there, hinted or not
;;   loopfield         the field out of a value a loop carried round, the
;;                     bfi's other operand living on: the same
;;   wrapweb           a wrap stays in its operand's register where that
;;                     dies, also as one of the values two ways hand a block
;;   many              forty values live at once: the scan runs under
;;                     several rules, and counts once
;;   gcnext gcrun      a reference loaded through the collector's barrier,
;;                     whose mask is read after the reference is written:
;;                     not in the mask's register
;;
;; stats -O2 a64: die_ties=20
;; shape: arg \smov\s+x\d+, x\d+$=0
;; shape: arg \sadd\s+x1, x1, #0x3=1
;; shape: arg2 \smov\s+x\d+, x\d+$=0
;; shape: arg2 \ssub\s+x1, x2, x1=1
;; shape: arg_sh \smov\s+x\d+, x\d+$=0
;; shape: arg_sh \sadd\s+x1, x1, x2, lsl #4=1
;; shape: res \smov\s+x\d+, x\d+$=0
;; shape: res \seor\s+x0, x0, #0xff=1
;; shape: res2 \smov\s+x\d+, x\d+$=0
;; shape: web \smov\s+x\d+, x\d+$=0
;; shape: web \sadd\s+x0, x0, #0x5=1
;; shape: shift \smov\s+x\d+, x\d+$=0
;; shape: shift \sasr\s+x0, x0, #16=1
;; shape: fsq \sfmov\s=0
;; shape: fsq \sfsqrt\s+d0, d0=1
;; shape: fadd \sfmov\s=0
;; shape: fadd \sfadd\s+d0, d1, d0=1
;; shape: field \sbfi\s+x0, x1, #8, #8=1
;; shape: field \smov\s+x\d+, x\d+$=1
;; shape: field2 \smov\s+x\d+, x\d+$=0
;; shape: field2 \sbfi\s=1
;; shape: wrapweb \smov\s+w0, w0$=1
;; shape: loopfield \sbfi\s+x0, x1, #8, #8=1
;; shape: gcnext \stst\s+(x\d+), \1$=0
;;
;; run: arg 0 => i64:3425383069646722515
;; run: arg 1 => i64:-7699213191879159348
;; run: arg 2 => i64:2901157798094342480
;; run: arg 77 => i64:-2216132216006706541
;; run: arg 4294967295 => i64:2227249665718543197
;; run: arg 2147483648 => i64:-7857966967996908899
;; run: arg 1311768467463790320 => i64:7907740665864630715
;; run: arg 18446744073709551615 => i64:7031955584568368900
;; run: arg 9223372036854775808 => i64:3411472337644472781
;; run: arg2 0 77 => i64:-4331483044362770568
;; run: arg2 1 18446744073709551615 => i64:4741312859408994899
;; run: arg2 2 2 => i64:-8884436252563615814
;; run: arg2 77 1311768467463790320 => i64:2673618008383018740
;; run: arg2 4294967295 1 => i64:-6417245288104676003
;; run: arg2 2147483648 2147483648 => i64:-8884436252563615814
;; run: arg2 1311768467463790320 0 => i64:8558589460189120415
;; run: arg2 18446744073709551615 4294967295 => i64:-2466215820621075080
;; run: arg2 9223372036854775808 9223372036854775808 => i64:-8884436252563615814
;; run: arg_sh 0 77 => i64:-4167430796888813168
;; run: arg_sh 1 18446744073709551615 => i64:1277887160734185619
;; run: arg_sh 2 2 => i64:1490836425452684272
;; run: arg_sh 77 1311768467463790320 => i64:5982691898604809199
;; run: arg_sh 4294967295 1 => i64:7421141739681362801
;; run: arg_sh 2147483648 2147483648 => i64:8737856462684750544
;; run: arg_sh 1311768467463790320 0 => i64:7416307858694325397
;; run: arg_sh 18446744073709551615 4294967295 => i64:3905377481417956316
;; run: arg_sh 9223372036854775808 9223372036854775808 => i64:6644193719340179617
;; run: res 0 => i64:-8884436252563615931
;; run: res 1 => i64:8398958991255978125
;; run: res 2 => i64:7031955584568369147
;; run: res 77 => i64:-4331483044362770553
;; run: res 4294967295 => i64:3626218474126595207
;; run: res 2147483648 => i64:1902350014564642316
;; run: res 1311768467463790320 => i64:7416307858694325354
;; run: res 18446744073709551615 => i64:1600522957551600953
;; run: res 9223372036854775808 => i64:6644193719340179550
;; run: res2 0 => i64:-18676382463175655
;; run: res2 1 => i64:-13734288251110095
;; run: res2 2 => i64:-20991271253574984
;; run: res2 77 => i64:-7664336567554386
;; run: res2 4294967295 => i64:-27835197550378548
;; run: res2 2147483648 => i64:-31211969244313430
;; run: res2 1311768467463790320 => i64:-33547575399419890
;; run: res2 18446744073709551615 => i64:-9260376678533193
;; run: res2 9223372036854775808 => i64:-18676382463175655
;; run: web 0 => i64:77
;; run: web 1 => i64:8398958991255978103
;; run: web 2 => i64:7031955584568368905
;; run: web 77 => i64:-4331483044362770563
;; run: web 4294967295 => i64:3626218474126595197
;; run: web 2147483648 => i64:1902350014564642552
;; run: web 1311768467463790320 => i64:7416307858694325402
;; run: web 18446744073709551615 => i64:1600522957551601099
;; run: web 9223372036854775808 => i64:6644193719340179622
;; run: shift 0 => i64:0
;; run: shift 1 => i64:0
;; run: shift 2 => i64:0
;; run: shift 77 => i64:0
;; run: shift 4294967295 => i64:-1
;; run: shift 2147483648 => i64:-32768
;; run: shift 1311768467463790320 => i64:-25924
;; run: shift 18446744073709551615 => i64:-1
;; run: shift 9223372036854775808 => i64:0
;; run: fsq 4612248968380809216 => f64:0x3ff8000000000000
;; run: fsq 9094988921128908188 => f64:0x5f138d352e5096af
;; run: fsq 0 => f64:0x0000000000000000
;; run: fsq 4618722892845154304 => f64:0x4004000000000000
;; run: fadd 4612248968380809216 4616189618054758400 => f64:0x4019000000000000
;; run: fadd 9094988921128908188 4615063718147915776 => f64:0x7e37e43c8800759c
;; run: fadd 0 4609434218613702656 => f64:0x3ff8000000000000
;; run: fadd 4618722892845154304 13826050856027422720 => f64:0x4017000000000000
;; run: live 0 => i64:2417580224800533012
;; run: live 3 => i64:-2356871199759767600
;; run: live 8 => i64:-4454736211996620108
;; run: live 16 => i64:7119691442028538708
;; run: live 40 => i64:4660129980827420980
;; run: cross 0 => i64:7769273054576163386
;; run: cross 3 => i64:1469179804710799689
;; run: cross 8 => i64:5997203671362748946
;; run: cross 16 => i64:4252155468771158634
;; run: cross 40 => i64:-5505025651190635790
;; run: cross_ret 0 => i64:-8618976980654642554
;; run: cross_ret 3 => i64:-589217799939941899
;; run: cross_ret 8 => i64:-5725370071426542930
;; run: cross_ret 16 => i64:-2831763157920253226
;; run: cross_ret 40 => i64:5776718513634243150
;; run: field 0 77 => i64:-4331483044362770568
;; run: field 1 18446744073709551615 => i64:8655310031663689666
;; run: field 2 2 => i64:-273133126106782961
;; run: field 77 1311768467463790320 => i64:8678928415531435101
;; run: field 4294967295 1 => i64:-3297201170774440231
;; run: field 2147483648 2147483648 => i64:1902350014564642547
;; run: field 1311768467463790320 0 => i64:4602397826989588104
;; run: field 18446744073709551615 4294967295 => i64:3626218474126595192
;; run: field 9223372036854775808 9223372036854775808 => i64:6644193719340179617
;; run: field2 0 77 2147483648 => i64:165356240896
;; run: field2 1 18446744073709551615 18446744073709551615 => i64:65025
;; run: field2 2 2 0 => i64:0
;; run: field2 77 1311768467463790320 2 => i64:2623536934927506400
;; run: field2 4294967295 1 4294967295 => i64:280379759984895
;; run: field2 2147483648 2147483648 1311768467463790320 => i64:5575015949918011392
;; run: field2 1311768467463790320 0 9223372036854775808 => i64:0
;; run: field2 18446744073709551615 4294967295 1 => i64:4294967295
;; run: field2 9223372036854775808 9223372036854775808 77 => i64:-9223372036854775808
;; run: wrapweb 3 5 1 => i32:25
;; run: wrapweb 3 5 0 => i32:45
;; run: wrapweb 18446744073709551615 2 1 => i32:4
;; run: wrapweb 4294967297 4294967299 0 => i32:9
;; run: wrapweb 2147483647 2 7 => i32:-2147483644
;; run: loopfield 1 65535 18446744073709551614 => i64:3032317676507142336
;; run: loopfield 4660 0 4611686018427387904 => i64:-7062996148898482377
;; run: loopfield 18446744073709551615 1311768467463790320 9223372036854775808 => i64:-8544052020093618979
;; run: loopfield 7 18446744073709551615 2305843009213693952 => i64:-6762846322024434906
;; run: many 0 => i64:2596217644239458681
;; run: many 7 => i64:1261540349437874693
;; run: many 16 => i64:3843866252803078436
;; run: many 30 => i64:-3408662119297756903
;; run: gcrun 5 => i64:6
;; run: gcrun 1099511627776 => i64:1099511627777

(module
  (type $N (struct (field (mut (ref null $N))) (field (mut i64))))
  (func $gcnext (export "gcnext") (param $n (ref $N)) (result (ref null $N))
    (struct.get $N 0 (local.get $n)))
  (func $gcrun (export "gcrun") (param $k i64) (result i64)
    (local $a (ref null $N)) (local $b (ref null $N))
    (local.set $a (struct.new $N (ref.null $N) (local.get $k)))
    (local.set $b (struct.new $N (local.get $a) (i64.add (local.get $k) (i64.const 1))))
    (i64.add (struct.get $N 1 (ref.as_non_null (call $gcnext (ref.as_non_null (local.get $b)))))
             (i64.extend_i32_u (ref.is_null (call $gcnext (ref.as_non_null (local.get $a)))))))
  (memory 1)
  (data (i32.const 0) "\85\aa\cf\f4\19\3e\63\88\ad\d2\f7\1c\41\66\8b\b0\d5\fa\1f\44\69\8e\b3\d8\fd\22\47\6c\91\b6\db\00\25\4a\6f\94\b9\de\03\28\4d\72\97\bc\e1\06\2b\50\75\9a\bf\e4\09\2e\53\78\9d\c2\e7\0c\31\56\7b\a0")
  (global $g (mut i64) (i64.const 0))
  (func $big (param $x i64) (result i64)
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 1)) (i64.const -6955090304568392861)) (i64.const -6414314011457766553)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 8)) (i64.const -7024767540724392435)) (i64.const 6502438387485576253)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 15)) (i64.const -7016713631532789821)) (i64.const 6083462413101427465)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 22)) (i64.const -7026329522676133498)) (i64.const 2668277108584139191)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 29)) (i64.const -6932217263622469781)) (i64.const -120386660438524037)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 36)) (i64.const -7017998352840736950)) (i64.const 6213572841009426369)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 43)) (i64.const -6983064395511083425)) (i64.const -8058862017582652099)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 50)) (i64.const -7056723668178562124)) (i64.const 1369454512963047643)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 57)) (i64.const -6984486882379372116)) (i64.const -7875727619595248433)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 1)) (i64.const -7037080191559107310)) (i64.const 3467402795550802821)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 8)) (i64.const -6997938855527829216)) (i64.const 7680049951680142001)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 15)) (i64.const -7005544095272484847)) (i64.const 8654719109295899199)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 22)) (i64.const -7052997049914421157)) (i64.const 1504371777931192163)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 29)) (i64.const -6963404127020352510)) (i64.const -5386666665524065399)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 36)) (i64.const -6975706915170827541)) (i64.const -8999566171655143579)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 43)) (i64.const -6928891056115133674)) (i64.const -1121966456253742621)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 50)) (i64.const -7000728788754738189)) (i64.const 8613694725063963447)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 57)) (i64.const -7007729624681604606)) (i64.const 4934072486178524109)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 1)) (i64.const -7053915605459114853)) (i64.const 1587039589590910809)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 8)) (i64.const -7052398781815315086)) (i64.const 851332708120179655)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 15)) (i64.const -6988706473324644419)) (i64.const -7337240659343584181)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 22)) (i64.const -6924575631681495803)) (i64.const -1673280857167591343)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 29)) (i64.const -7045472283091380904)) (i64.const 506364696003389069)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 36)) (i64.const -7006518779754040222)) (i64.const 8813819219572702187)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 43)) (i64.const -6923381146290294488)) (i64.const -1286306528772055393)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 50)) (i64.const -6995331480629636193)) (i64.const 7959926364435891477)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 57)) (i64.const -7055634598631924937)) (i64.const 1229490662994579713)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 1)) (i64.const -7042118923520757271)) (i64.const 4112859115450465871)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 8)) (i64.const -7056753020237926183)) (i64.const 1372646427140187699)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 15)) (i64.const -7053127541782539111)) (i64.const 1485119056816391705)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 22)) (i64.const -6932743420518421920)) (i64.const -52469583190476107)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 29)) (i64.const -6973580265596898264)) (i64.const -8659852916193747213)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 36)) (i64.const -6940577322160002837)) (i64.const -3696885884897296633)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 43)) (i64.const -7042421783308861557)) (i64.const 4150494870097743709)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 50)) (i64.const -7038033953559835078)) (i64.const 3624387230928803753)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 57)) (i64.const -6988894704198150634)) (i64.const -7312023403958566441)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 1)) (i64.const -7035910973420013841)) (i64.const 3894139304091680027)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 8)) (i64.const -7022891020560900900)) (i64.const 6875366124822750433)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 15)) (i64.const -7049125950208053342)) (i64.const 972910937487563741)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 22)) (i64.const -7027484434569036160)) (i64.const 2815538485560167163)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 29)) (i64.const -6957770702413485443)) (i64.const -6685337715532417937)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 36)) (i64.const -6955201987921417088)) (i64.const -6437738936318539099)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 43)) (i64.const -6953364405469993245)) (i64.const -2599940058699170991)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 50)) (i64.const -7060630960416376407)) (i64.const 1905052116689542751)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 57)) (i64.const -7051299295041833625)) (i64.const 710601148039739651)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 1)) (i64.const -7040622811621662692)) (i64.const 4533912313728468137)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 8)) (i64.const -6952076853295158719)) (i64.const -2801894836757911035)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 15)) (i64.const -7042637536573034539)) (i64.const 4215334156915826691)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 22)) (i64.const -6999212249048058712)) (i64.const 8456099572757061335)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 29)) (i64.const -6974186506657402696)) (i64.const -9158080944559204627)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 36)) (i64.const -6972444843337460062)) (i64.const -8804553644799861767)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 43)) (i64.const -6920768324878090894)) (i64.const -2197144650616097817)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 50)) (i64.const -6938618612795355290)) (i64.const -4524624937262401045)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 57)) (i64.const -6946263543592269665)) (i64.const -2933582044688923279)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 1)) (i64.const -6968172087902500961)) (i64.const -4776367121485675219)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 8)) (i64.const -6932281036468870167)) (i64.const -110535438695924213)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 15)) (i64.const -7031734312784005335)) (i64.const 3359032514630229567)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 22)) (i64.const -7031512448289961411)) (i64.const 3331128640086414389)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 29)) (i64.const -6973748786150658394)) (i64.const -8674238512391169631)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 36)) (i64.const -6934085853371139759)) (i64.const -492564225785159057)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 43)) (i64.const -6951994425081438166)) (i64.const -2775854449219985453)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 50)) (i64.const -6931275846186979173)) (i64.const -276840473880541383)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 57)) (i64.const -7053588876755159921)) (i64.const 1581880981233880405)))
    (local.set $x (i64.mul (i64.xor (i64.rotl (local.get $x) (i64.const 1)) (i64.const -6965378620112834657)) (i64.const -5711590220503595757)))
    (local.get $x))
  (func $arg (export "arg") (param $p i64) (result i64)
    (call $big (i64.add (local.get $p) (i64.const 3))))
  (func $arg2 (export "arg2") (param $p i64) (param $q i64) (result i64)
    (call $big (i64.sub (local.get $q) (local.get $p))))
  (func $arg_sh (export "arg_sh") (param $p i64) (param $q i64) (result i64)
    (call $big (i64.add (local.get $p) (i64.shl (local.get $q) (i64.const 4)))))
  (func $res (export "res") (param $p i64) (result i64)
    (i64.xor (call $big (local.get $p)) (i64.const 0xff)))
  (func $res2 (export "res2") (param $p i64) (result i64)
    (i64.sub (i64.const 0) (i64.shr_u (call $big (i64.add (local.get $p) (local.get $p))) (i64.const 9))))
  (func $web (export "web") (param $p i64) (result i64)
    (if (result i64) (i64.eqz (local.get $p)) (then (i64.const 77))
      (else (i64.add (call $big (local.get $p)) (i64.const 5)))))
  (func $shift (export "shift") (param $p i64) (result i64)
    (i64.shr_s (i64.extend32_s (local.get $p)) (i64.const 16)))
  (func $fsq (export "fsq") (param $a f64) (result f64)
    (f64.sqrt (local.get $a)))
  (func $fadd (export "fadd") (param $a f64) (param $b f64) (result f64)
    (f64.add (local.get $b) (local.get $a)))
  (func $live (export "live") (param $p i32) (result i64)
    (local $t i64) (local $r i64)
    (local.set $t (i64.load (local.get $p)))
    (local.set $r (i64.mul (local.get $t) (i64.const 3)))
    (i64.store offset=8 (local.get $p) (local.get $t))
    (i64.add (local.get $r) (i64.load offset=8 (local.get $p))))
  (func $cross (export "cross") (param $p i32) (result i64)
    (local $r i64)
    (local.set $r (i64.add (i64.load (local.get $p)) (i64.const 1)))
    (global.set $g (call $big (i64.const 9)))
    (i64.xor (local.get $r) (global.get $g)))
  (func $cross_ret (export "cross_ret") (param $p i32) (result i64)
    (local $r i64)
    (local.set $r (i64.add (i64.load (local.get $p)) (i64.const 1)))
    (global.set $g (call $big (i64.const 9)))
    (local.get $r))
  (func $field (export "field") (param $x i64) (param $o i64) (result i64)
    (call $big (i64.or (i64.and (local.get $o) (i64.const 0xffffffffffff00ff))
                       (i64.and (i64.shl (local.get $x) (i64.const 8)) (i64.const 0xff00)))))
  (func $field2 (export "field2") (param $x i64) (param $o i64) (param $c i64) (result i64)
    (i64.mul (local.get $c)
             (i64.or (i64.and (local.get $o) (i64.const 0xffffffffffff00ff))
                     (i64.and (i64.shl (local.get $x) (i64.const 8)) (i64.const 0xff00)))))
  (func $wrapweb (export "wrapweb") (param $a i64) (param $b i64) (param $c i32) (result i32)
    (local $w i32)
    (if (local.get $c)
      (then (i32.store (i32.const 64) (i32.const 1))
            (local.set $w (i32.wrap_i64 (i64.add (local.get $a) (local.get $b)))))
      (else (i32.store (i32.const 68) (i32.const 2))
            (local.set $w (i32.wrap_i64 (i64.mul (local.get $a) (local.get $b))))))
    (i32.add (i32.mul (local.get $w) (i32.const 3)) (i32.load (i32.const 64))))
  (func $loopfield (export "loopfield") (param $x i64) (param $o i64) (param $n i64) (result i64)
    (loop $l
      (local.set $x (i64.mul (i64.rotl (local.get $x) (i64.const 3)) (i64.const 5)))
      (br_if $l (i64.gt_u (local.get $x) (local.get $n))))
    (local.set $o (i64.and (local.get $o) (i64.const 0xffffffffffff00ff)))
    (i64.add (local.get $o)
      (call $big (i64.or (local.get $o)
                         (i64.and (i64.shl (local.get $x) (i64.const 8)) (i64.const 0xff00))))))
  (func $many (export "many") (param $p i32) (result i64)
    (local $t0 i64) (local $t1 i64) (local $t2 i64) (local $t3 i64) (local $t4 i64) (local $t5 i64) (local $t6 i64) (local $t7 i64) (local $t8 i64) (local $t9 i64) (local $t10 i64) (local $t11 i64) (local $t12 i64) (local $t13 i64) (local $t14 i64) (local $t15 i64) (local $t16 i64) (local $t17 i64) (local $t18 i64) (local $t19 i64) (local $t20 i64) (local $t21 i64) (local $t22 i64) (local $t23 i64) (local $t24 i64) (local $t25 i64) (local $t26 i64) (local $t27 i64) (local $t28 i64) (local $t29 i64) (local $t30 i64) (local $t31 i64) (local $t32 i64) (local $t33 i64) (local $t34 i64) (local $t35 i64) (local $t36 i64) (local $t37 i64) (local $t38 i64) (local $t39 i64) (local $s i64)
    (local.set $t0 (i64.mul (i64.load8_u offset=0 (local.get $p)) (i64.const 3)))
    (local.set $t1 (i64.mul (i64.load8_u offset=1 (local.get $p)) (i64.const 5)))
    (local.set $t2 (i64.mul (i64.load8_u offset=2 (local.get $p)) (i64.const 7)))
    (local.set $t3 (i64.mul (i64.load8_u offset=3 (local.get $p)) (i64.const 9)))
    (local.set $t4 (i64.mul (i64.load8_u offset=4 (local.get $p)) (i64.const 11)))
    (local.set $t5 (i64.mul (i64.load8_u offset=5 (local.get $p)) (i64.const 13)))
    (local.set $t6 (i64.mul (i64.load8_u offset=6 (local.get $p)) (i64.const 15)))
    (local.set $t7 (i64.mul (i64.load8_u offset=7 (local.get $p)) (i64.const 17)))
    (local.set $t8 (i64.mul (i64.load8_u offset=8 (local.get $p)) (i64.const 19)))
    (local.set $t9 (i64.mul (i64.load8_u offset=9 (local.get $p)) (i64.const 21)))
    (local.set $t10 (i64.mul (i64.load8_u offset=10 (local.get $p)) (i64.const 23)))
    (local.set $t11 (i64.mul (i64.load8_u offset=11 (local.get $p)) (i64.const 25)))
    (local.set $t12 (i64.mul (i64.load8_u offset=12 (local.get $p)) (i64.const 27)))
    (local.set $t13 (i64.mul (i64.load8_u offset=13 (local.get $p)) (i64.const 29)))
    (local.set $t14 (i64.mul (i64.load8_u offset=14 (local.get $p)) (i64.const 31)))
    (local.set $t15 (i64.mul (i64.load8_u offset=15 (local.get $p)) (i64.const 33)))
    (local.set $t16 (i64.mul (i64.load8_u offset=16 (local.get $p)) (i64.const 35)))
    (local.set $t17 (i64.mul (i64.load8_u offset=17 (local.get $p)) (i64.const 37)))
    (local.set $t18 (i64.mul (i64.load8_u offset=18 (local.get $p)) (i64.const 39)))
    (local.set $t19 (i64.mul (i64.load8_u offset=19 (local.get $p)) (i64.const 41)))
    (local.set $t20 (i64.mul (i64.load8_u offset=20 (local.get $p)) (i64.const 43)))
    (local.set $t21 (i64.mul (i64.load8_u offset=21 (local.get $p)) (i64.const 45)))
    (local.set $t22 (i64.mul (i64.load8_u offset=22 (local.get $p)) (i64.const 47)))
    (local.set $t23 (i64.mul (i64.load8_u offset=23 (local.get $p)) (i64.const 49)))
    (local.set $t24 (i64.mul (i64.load8_u offset=0 (local.get $p)) (i64.const 51)))
    (local.set $t25 (i64.mul (i64.load8_u offset=1 (local.get $p)) (i64.const 53)))
    (local.set $t26 (i64.mul (i64.load8_u offset=2 (local.get $p)) (i64.const 55)))
    (local.set $t27 (i64.mul (i64.load8_u offset=3 (local.get $p)) (i64.const 57)))
    (local.set $t28 (i64.mul (i64.load8_u offset=4 (local.get $p)) (i64.const 59)))
    (local.set $t29 (i64.mul (i64.load8_u offset=5 (local.get $p)) (i64.const 61)))
    (local.set $t30 (i64.mul (i64.load8_u offset=6 (local.get $p)) (i64.const 63)))
    (local.set $t31 (i64.mul (i64.load8_u offset=7 (local.get $p)) (i64.const 65)))
    (local.set $t32 (i64.mul (i64.load8_u offset=8 (local.get $p)) (i64.const 67)))
    (local.set $t33 (i64.mul (i64.load8_u offset=9 (local.get $p)) (i64.const 69)))
    (local.set $t34 (i64.mul (i64.load8_u offset=10 (local.get $p)) (i64.const 71)))
    (local.set $t35 (i64.mul (i64.load8_u offset=11 (local.get $p)) (i64.const 73)))
    (local.set $t36 (i64.mul (i64.load8_u offset=12 (local.get $p)) (i64.const 75)))
    (local.set $t37 (i64.mul (i64.load8_u offset=13 (local.get $p)) (i64.const 77)))
    (local.set $t38 (i64.mul (i64.load8_u offset=14 (local.get $p)) (i64.const 79)))
    (local.set $t39 (i64.mul (i64.load8_u offset=15 (local.get $p)) (i64.const 81)))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t0) (local.get $t39))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t1) (local.get $t38))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t2) (local.get $t37))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t3) (local.get $t36))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t4) (local.get $t35))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t5) (local.get $t34))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t6) (local.get $t33))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t7) (local.get $t32))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t8) (local.get $t31))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t9) (local.get $t30))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t10) (local.get $t29))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t11) (local.get $t28))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t12) (local.get $t27))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t13) (local.get $t26))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t14) (local.get $t25))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t15) (local.get $t24))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t16) (local.get $t23))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t17) (local.get $t22))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t18) (local.get $t21))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t19) (local.get $t20))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t20) (local.get $t19))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t21) (local.get $t18))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t22) (local.get $t17))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t23) (local.get $t16))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t24) (local.get $t15))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t25) (local.get $t14))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t26) (local.get $t13))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t27) (local.get $t12))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t28) (local.get $t11))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t29) (local.get $t10))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t30) (local.get $t9))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t31) (local.get $t8))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t32) (local.get $t7))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t33) (local.get $t6))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t34) (local.get $t5))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t35) (local.get $t4))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t36) (local.get $t3))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t37) (local.get $t2))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t38) (local.get $t1))))
    (local.set $s (i64.add (i64.rotl (local.get $s) (i64.const 7)) (i64.xor (local.get $t39) (local.get $t0))))
    (i64.xor (call $big (local.get $s)) (i64.const 0xff)))
)
