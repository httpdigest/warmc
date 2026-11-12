;; A loop's counters that step together (opt.c pass_iv_lockstep): parameters
;; of its header that every way round hands their own value plus one
;; constant.  One the loop reads nothing of is taken out where it starts a
;; constant from one that stays on every way in -- two constants, one value,
;; each of those plus constants, the parameters of a block whose ways in
;; agree -- and what read it after the loop reads the one that stays plus
;; that constant: an edge leaving the loop in a block of the edge's own, a
;; reader past the loop, in no other loop, where it reads.  A counter that
;; stays, read past its step by nothing but the ways out below the step and
;; what comes after the loop below it, is read there as the stepped value
;; less the step; and (x + i) + 1 made after the step i + 1 is x + (i + 1).
;; From test/gen_lockstep.py, which works out every answer and says, with
;; --each, what --stats counts for each function alone.  Beside each
;; function below, what is done in it: out, a counter taken out; exit, a
;; reader handed the stepped value; addr, an address remade.
;;
;;   same         out       two counters from one value, the second read only
;;                         after the loop: taken out
;;   apart        out       two a constant apart, the way out above the step:
;;                         the counter plus the constant there
;;   below        out       the one taken out starts below the one that
;;                         stays: a constant taken off
;;   stepped      out       the way out is below the step and hands on the
;;                         stepped values
;;   oldafter     out       counting down, the way out below the step hands
;;                         on the value before it
;;   mixed        out       one steps by a subtraction of 1, the other by an
;;                         addition of -1: alike
;;   wide         out       64-bit counters more than 32 bits apart
;;   threeout     out       three read only after the loop, beside a pointer
;;                         that starts as no constant: the first stays for
;;                         the other two
;;   leadconst    out       of two that stay, the one a constant from it is
;;                         the one it is made from
;;   ret          out       a return in the loop is a way out as any other
;;   twoback      out       two ways round, each with the same stepped values
;;   twowaysrot   out       two ways to a test ahead of the loop, the two the
;;                         same constant apart on both: through the
;;                         parameters of where they meet
;;   nested       out       a loop in a loop, each with one to take out
;;   sibling      out       a loop whose way out leads into the next: each
;;                         has one taken out
;;   many         out       seventeen counters, the last two read only after
;;                         the loop: sixteen are looked at, and the sixteenth
;;                         taken out
;;   twoways      out       two ways into the loop, the two the same constant
;;                         apart on both
;;   twowayswrap  out       the same constant apart only in 32 bits: 20 and
;;                         37 one way, -16 and 1 the other
;;   leadlater    out       the one that stays comes after the first taken
;;                         out, among the parameters: the third is made from
;;                         it too, not from the first
;;   wrapzero     out       the one taken out starts at -1 and is stepped
;;                         above the way out, the other at 0 and below it:
;;                         what leaves is the counter that stays, with
;;                         nothing added
;;   stripadd     out       the two start as one value plus a constant and
;;                         the value less another
;;   mergeoff     out       two ways meet, the two 5 apart on both, and a
;;                         constant is added to the one after: 8 apart
;;   goneclosed   out       a loop past the loop reads the one taken out, as
;;                         the parameter of the block the one way out leads
;;                         to: the edge hands on the counter that stays, plus
;;                         the constant made on the edge
;;   outret       out       the function returns the stepped value of the one
;;                         taken out
;;   outbrif      out       a branch past the loop is on it
;;   outedge      out       an arm past the loop hands it on
;;   outloop      -         a loop past the loop reads it, each time round:
;;                         it stays
;;   ehread       out       a handler past the loop reads it, come to from a
;;                         call in the loop
;;   ehblock      -         handed on by the edge out of a block past the
;;                         loop that ends in a call a handler stands behind:
;;                         nothing can be made after that call, and it stays
;;   types        -         a 32-bit counter and a 64-bit one: both stay
;;   steps        -         steps of 1 and of 2: both stay
;;   runtime      -         two that start as two values, no constants: both
;;                         stay
;;   halfconst    -         one starts as a constant, the other as a value:
;;                         both stay
;;   allread      -         the loop reads both: both stay
;;   stepread     -         the loop reads the stepped value of the second:
;;                         it stays
;;   brifon       -         the loop branches on the second, and reads no
;;                         more of it: it stays
;;   brifstep     -         the loop branches on its stepped value: it stays
;;   inneredge    -         an arm in the loop hands the second on to where
;;                         the arms meet: it stays
;;   prev         -         the way round hands its stepped value to another
;;                         parameter, which the loop reads: it stays
;;   twobackdiff  -         two ways round, the second stepped by 1 on one
;;                         and by 2 on the other: no counter
;;   twowaysrotdiff -         two ways to the test, the two 5 apart on one
;;                         and 9 on the other: both stay
;;   twowaysdiff  -         two ways in, the two 5 apart on one and 9 on the
;;                         other: both stay
;;   xorstep      -         an xor with 1 each time round is no step
;;   updown       -         one counts up and one down: both stay
;;   outloopedge  -         an arm in a loop past the loop hands it on: it
;;                         stays
;;   stripand     -         one starts as the other masked: both stay
;;   swap         -         a parameter handed another's value plus a
;;                         constant, not its own: no counter
;;   condstep     -         two ways round, the second stepped on one and
;;                         handed on as it is on the other: no counter
;;   condstep2    -         the same, stepped on the other way round
;;   rsub         -         a constant less the parameter is no step
;;   varstep      -         a step by a value that is no constant
;;   late         exit      the way out below the step hands on the counter
;;                         as it was: the stepped value less the step
;;   late64       exit      a 64-bit one counting down
;;   lateafter    -         the loop reads the counter after its step: the
;;                         way out keeps the counter
;;   lateabove    exit      a way out above the step keeps the counter, one
;;                         below reads the stepped value
;;   latebrif     -         the loop branches on the counter as it was, below
;;                         the step: the way out keeps it
;;   lateinner    -         an arm below the step hands the counter as it was
;;                         on in the loop: the way out keeps it
;;   latebeside   exit      an arm above the step reads the counter, in a
;;                         block that is not above the step's: read before
;;                         the step all the same
;;   lateret      exit      the function returns the counter as it was
;;   lateclosed   exit      a loop past the loop reads the counter as it was,
;;                         as the parameter of the block the one way out
;;                         leads to: the edge is handed the stepped value
;;   lateoutloop  -         two ways out lead to a loop that reads the
;;                         counter as it was, each time round: the counter
;;                         stays for it
;;   lateafterblk -         an arm below the step reads the counter as it
;;                         was: the way out keeps it
;;   latebrabove  exit      the loop branches on the counter above the step:
;;                         the way out below reads the stepped value
;;   lateedgeabove exit      an arm above the step hands the counter on in
;;                         the loop: read before the step
;;   lateaboveret exit      a return above the step hands on the counter, the
;;                         way out below reads the stepped value
;;   latemerge    -         a way out above the step and one below lead to
;;                         one reader of the counter as it was: it stays for
;;                         that reader, and a third way out keeps it too
;;   latemergeret -         the same, the one reader the function's return
;;   addr         addr      an address of a value, the counter and the step,
;;                         made after the step: of the stepped value
;;   addrswap     addr      the counter first
;;   addrk        -         the constant is not the step: it stays
;;   addrtwo      -         the sum of value and counter is read twice: it
;;                         stays
;;   addrself     -         the counter added to itself: it stays
;;   addrxor      -         an xor with the step, not an addition of it: it
;;                         stays
;;   addrsubr     -         the value less the counter, not their sum: it
;;                         stays
;;   addrgone     out       an address past the loop of a counter taken out:
;;                         made from the one that stays
;;   addrbrif     -         the loop branches on the sum: it stays
;;   addrnest     addr      an address in an inner loop of the outer counter
;;                         and the inner one: the inner loop, whose counter
;;                         it now reads, waits a run and keeps the counter
;;   addrearly    -         the address is made above the step: it stays
;;   addresc      -         the sum leaves the loop too: it stays
;;
;; stats -O2: lockstep=37 lockstep_exits=9 lockstep_addrs=3
;; shape: oldafter \sb\s=0
;; shape: oldafter \smov\s=2
;; shape: wrapzero \sadd\s=3
;; shape: wrapzero \smov\s=3
;; loop: goneclosed \sadd\s=4
;; shape: late \sb\s=0
;; shape: late \ssub\s=1
;; shape: late64 \sb\s=0
;; shape: latebeside \sb\s=0
;; loop: lateclosed \ssub\s=0
;;
;; run: same 0 => i32:36
;; run: same 4 => i32:36
;; run: same 9 => i32:36
;; run: same 16 => i32:76
;; run: same 18 => i32:76
;; run: same 48 => i32:220
;; run: apart 0 => i32:9000
;; run: apart 1 => i32:9000
;; run: apart 2 => i32:9000
;; run: apart 5 => i32:12031
;; run: apart 9 => i32:18729
;; run: below 0 => i32:196611
;; run: below 20 => i32:196611
;; run: below 21 => i32:262168
;; run: below 25 => i32:540110
;; run: below 40 => i32:350444645
;; run: stepped 0 => i32:65637
;; run: stepped 1 => i32:65637
;; run: stepped 2 => i32:131174
;; run: stepped 5 => i32:327785
;; run: stepped 9 => i32:589933
;; run: oldafter 0 => i32:1219
;; run: oldafter 2 => i32:1235
;; run: oldafter 4 => i32:1251
;; run: oldafter 9 => i32:1259
;; run: mixed 0 => i32:2438
;; run: mixed 2 => i32:2446
;; run: mixed 4 => i32:2454
;; run: mixed 9 => i32:2462
;; run: wide 0 => i64:5527622909955
;; run: wide 5 => i64:5527622909955
;; run: wide 6 => i64:6627134537732
;; run: wide 40 => i64:44010529882150
;; run: threeout 0 => i32:1091919
;; run: threeout 5 => i32:1041414
;; run: threeout 9 => i32:1001010
;; run: threeout 16 => i32:1031323
;; run: threeout 48 => i32:1071763
;; run: leadconst 0 => i32:14199
;; run: leadconst 5 => i32:9042
;; run: leadconst 9 => i32:5009
;; run: leadconst 16 => i32:8022
;; run: leadconst 48 => i32:12144
;; run: ret 3 => i32:40
;; run: ret 9 => i32:43
;; run: ret 5 => i32:48
;; run: ret 0 => i32:49
;; run: ret 77 => i32:-991
;; run: twoback 0 => i32:2009
;; run: twoback 4 => i32:2009
;; run: twoback 16 => i32:19
;; run: twoback 48 => i32:2055
;; run: twoback 52 => i32:55
;; run: twowaysrot 0 0 => i32:28672
;; run: twowaysrot 1 0 => i32:20480
;; run: twowaysrot 0 6 => i32:45922
;; run: twowaysrot 1 6 => i32:48323
;; run: twowaysrot 0 3 => i32:32770
;; run: twowaysrot 1 1 => i32:24576
;; run: nested 0 => i32:3000000
;; run: nested 1 => i32:3100016
;; run: nested 3 => i32:3302088
;; run: nested 7 => i32:34283584
;; run: nested 9 => i32:-590453524
;; run: sibling 0 => i32:28
;; run: sibling 2 => i32:2064
;; run: sibling 5 => i32:5064
;; run: sibling 9 => i32:9064
;; run: many 0 => i32:17112
;; run: many 1 => i32:10779791
;; run: many 3 => i32:1718706718
;; run: many 6 => i32:1730666529
;; run: twoways 0 0 => i32:32769
;; run: twoways 1 0 => i32:24579
;; run: twoways 0 50 => i32:41076
;; run: twoways 1 50 => i32:32965
;; run: twoways 0 3000 => i32:54880
;; run: twoways 1 3000 => i32:50680
;; run: twowayswrap 0 0 => i32:8193
;; run: twowayswrap 1 0 => i32:155653
;; run: twowayswrap 0 50 => i32:16450
;; run: twowayswrap 1 50 => i32:164134
;; run: twowayswrap 0 3000 => i32:27843
;; run: twowayswrap 1 3000 => i32:186503
;; run: leadlater 0 => i32:90600
;; run: leadlater 1 => i32:100700
;; run: leadlater 5 => i32:141158
;; run: leadlater 9 => i32:186416
;; run: leadlater 30 => i32:186416
;; run: wrapzero 0 => i32:0
;; run: wrapzero 1 => i32:100
;; run: wrapzero 5 => i32:558
;; run: wrapzero 9 => i32:5816
;; run: wrapzero 30 => i32:5816
;; run: stripadd 0 0 => i32:-4087
;; run: stripadd 0 50 => i32:67
;; run: stripadd 3 50 => i32:16543
;; run: stripadd 5 3000 => i32:44773
;; run: stripadd 2 0 => i32:4102
;; run: mergeoff 0 0 => i32:36867
;; run: mergeoff 1 0 => i32:36867
;; run: mergeoff 0 51 => i32:49511
;; run: mergeoff 1 51 => i32:53315
;; run: mergeoff 0 3002 => i32:62968
;; run: mergeoff 1 3002 => i32:67168
;; run: goneclosed 0 => i32:646
;; run: goneclosed 4 => i32:646
;; run: goneclosed 9 => i32:646
;; run: goneclosed 16 => i32:786
;; run: goneclosed 48 => i32:1290
;; run: outret 0 => i32:64
;; run: outret 2 => i32:66
;; run: outret 4 => i32:68
;; run: outret 9 => i32:69
;; run: outbrif 0 => i32:111
;; run: outbrif 2 => i32:102
;; run: outbrif 4 => i32:111
;; run: outbrif 9 => i32:111
;; run: outedge 0 => i32:32005
;; run: outedge 1 => i32:33085
;; run: outedge 2 => i32:34005
;; run: outedge 3 => i32:35087
;; run: outedge 9 => i32:37089
;; run: outloop 0 => i32:604
;; run: outloop 2 => i32:632
;; run: outloop 4 => i32:660
;; run: outloop 9 => i32:674
;; run: ehread 0 => i32:1020
;; run: ehread 2 => i32:1022
;; run: ehread 3 => i32:1023
;; run: ehread 4 => i32:161
;; run: ehread 9 => i32:161
;; run: ehblock 0 0 => i32:32005
;; run: ehblock 0 1 => i32:32064
;; run: ehblock 0 3 => i32:-7
;; run: ehblock 4 2 => i32:36068
;; run: ehblock 9 1 => i32:37069
;; run: types 0 => i64:30064771077
;; run: types 1 => i64:30065819654
;; run: types 2 => i64:30066868231
;; run: types 5 => i64:30070013962
;; run: types 9 => i64:30074208270
;; run: steps 0 => i32:4000
;; run: steps 1 => i32:6000
;; run: steps 2 => i32:8001
;; run: steps 5 => i32:14058
;; run: steps 9 => i32:26916
;; run: runtime 0 0 => i32:9009
;; run: runtime 0 50 => i32:59009
;; run: runtime 5 2 => i32:6009
;; run: runtime 9 9 => i32:9009
;; run: runtime 16 1 => i32:4019
;; run: halfconst 0 => i32:15009
;; run: halfconst 5 => i32:10009
;; run: halfconst 9 => i32:6009
;; run: halfconst 16 => i32:9019
;; run: allread 0 => i32:11
;; run: allread 1 => i32:12
;; run: allread 2 => i32:25
;; run: allread 5 => i32:760
;; run: allread 9 => i32:63892
;; run: stepread 0 => i32:11
;; run: stepread 1 => i32:24
;; run: stepread 2 => i32:54
;; run: stepread 5 => i32:1008
;; run: stepread 9 => i32:80696
;; run: brifon 0 => i32:7
;; run: brifon 1 => i32:14
;; run: brifon 4 => i32:206
;; run: brifon 8 => i32:17945
;; run: brifstep 1 => i32:16
;; run: brifstep 2 => i32:21
;; run: brifstep 4 => i32:746
;; run: brifstep 8 => i32:526371
;; run: inneredge 0 => i32:50
;; run: inneredge 1 => i32:5051
;; run: inneredge 2 => i32:5152
;; run: inneredge 5 => i32:5355
;; run: inneredge 9 => i32:5859
;; run: prev 0 => i32:0
;; run: prev 1 => i32:1
;; run: prev 2 => i32:11
;; run: prev 5 => i32:389
;; run: prev 9 => i32:31489
;; run: prev 30 => i32:31510
;; run: twobackdiff 0 => i32:2013
;; run: twobackdiff 4 => i32:2013
;; run: twobackdiff 16 => i32:19
;; run: twobackdiff 48 => i32:2059
;; run: twobackdiff 52 => i32:55
;; run: twowaysrotdiff 0 0 => i32:45056
;; run: twowaysrotdiff 1 0 => i32:20480
;; run: twowaysrotdiff 0 6 => i32:62306
;; run: twowaysrotdiff 1 6 => i32:48323
;; run: twowaysrotdiff 0 3 => i32:49154
;; run: twowaysrotdiff 1 1 => i32:24576
;; run: twowaysdiff 0 0 => i32:49153
;; run: twowaysdiff 1 0 => i32:24579
;; run: twowaysdiff 0 50 => i32:57460
;; run: twowaysdiff 1 50 => i32:32965
;; run: twowaysdiff 0 3000 => i32:71264
;; run: twowaysdiff 1 3000 => i32:50680
;; run: xorstep 0 => i32:6032
;; run: xorstep 3 => i32:7035
;; run: xorstep 4 => i32:6036
;; run: xorstep 5 => i32:7037
;; run: xorstep 9 => i32:6038
;; run: updown 0 => i32:100000
;; run: updown 1 => i32:99001
;; run: updown 5 => i32:95005
;; run: updown 30 => i32:91009
;; run: outloopedge 0 => i32:3244
;; run: outloopedge 2 => i32:3446
;; run: outloopedge 4 => i32:3648
;; run: outloopedge 9 => i32:3749
;; run: stripand 0 0 => i32:4099
;; run: stripand 0 50 => i32:12485
;; run: stripand 3 50 => i32:20547
;; run: stripand 258 3000 => i32:1082976
;; run: stripand 20 0 => i32:86020
;; run: swap 0 => i32:70000
;; run: swap 1 => i32:90801
;; run: swap 2 => i32:111002
;; run: swap 5 => i32:171605
;; run: swap 30 => i32:252409
;; run: condstep 0 => i32:2005
;; run: condstep 4 => i32:2005
;; run: condstep 16 => i32:19
;; run: condstep 48 => i32:2051
;; run: condstep 52 => i32:55
;; run: condstep2 0 => i32:2004
;; run: condstep2 4 => i32:2008
;; run: condstep2 16 => i32:16
;; run: condstep2 48 => i32:2052
;; run: condstep2 52 => i32:52
;; run: rsub 0 => i32:300
;; run: rsub 1 => i32:-199
;; run: rsub 2 => i32:302
;; run: rsub 5 => i32:-195
;; run: rsub 9 => i32:-191
;; run: varstep 0 => i32:0
;; run: varstep 1 => i32:101
;; run: varstep 2 => i32:402
;; run: varstep 5 => i32:2505
;; run: varstep 9 => i32:8109
;; run: late 0 => i32:3
;; run: late 4 => i32:110
;; run: late 11 => i32:211
;; run: late 20 => i32:320
;; run: late 45 => i32:845
;; run: late64 0 => i64:163208757254
;; run: late64 6 => i64:163208757254
;; run: late64 12 => i64:154618822671
;; run: late64 20 => i64:146028888084
;; run: lateafter 0 => i32:0
;; run: lateafter 4 => i32:219
;; run: lateafter 30 => i32:331
;; run: lateafter 90 => i32:713
;; run: lateabove 0 77 => i32:3
;; run: lateabove 11 77 => i32:211
;; run: lateabove 11 1 => i32:5002
;; run: lateabove 45 4 => i32:5004
;; run: lateabove 45 0 => i32:845
;; run: lateabove 3 3 => i32:5000
;; run: latebrif 0 => i32:3
;; run: latebrif 1 => i32:24
;; run: latebrif 4 => i32:600
;; run: latebrif 8 => i32:53817
;; run: lateinner 0 => i32:3
;; run: lateinner 4 => i32:110
;; run: lateinner 11 => i32:20211
;; run: lateinner 20 => i32:20320
;; run: lateinner 45 => i32:80845
;; run: latebeside 0 => i32:3
;; run: latebeside 4 => i32:10110
;; run: latebeside 11 => i32:30211
;; run: latebeside 20 => i32:60320
;; run: latebeside 45 => i32:140845
;; run: lateret 0 => i32:0
;; run: lateret 4 => i32:1
;; run: lateret 11 => i32:2
;; run: lateret 20 => i32:3
;; run: lateret 45 => i32:8
;; run: lateclosed 0 => i32:3
;; run: lateclosed 4 => i32:23
;; run: lateclosed 11 => i32:37
;; run: lateclosed 20 => i32:59
;; run: lateclosed 45 => i32:149
;; run: lateoutloop 0 => i32:3
;; run: lateoutloop 4 => i32:23
;; run: lateoutloop 11 => i32:37
;; run: lateoutloop 20 => i32:37
;; run: lateoutloop 45 => i32:37
;; run: lateafterblk 0 => i32:3
;; run: lateafterblk 4 => i32:110
;; run: lateafterblk 11 => i32:213
;; run: lateafterblk 20 => i32:322
;; run: lateafterblk 45 => i32:853
;; run: latebrabove 0 => i32:3
;; run: latebrabove 4 => i32:110
;; run: latebrabove 11 => i32:211
;; run: latebrabove 20 => i32:320
;; run: latebrabove 45 => i32:845
;; run: lateedgeabove 0 => i32:3
;; run: lateedgeabove 4 => i32:10110
;; run: lateedgeabove 11 => i32:20211
;; run: lateedgeabove 20 => i32:30320
;; run: lateedgeabove 45 => i32:80845
;; run: lateaboveret 0 77 => i32:3
;; run: lateaboveret 11 77 => i32:211
;; run: lateaboveret 11 1 => i32:2
;; run: lateaboveret 45 4 => i32:4
;; run: lateaboveret 45 0 => i32:845
;; run: lateaboveret 3 3 => i32:0
;; run: latemerge 5 77 => i32:78
;; run: latemerge 50 77 => i32:7002
;; run: latemerge 50 7 => i32:8
;; run: latemerge 11 77 => i32:79
;; run: latemerge 50 1 => i32:3
;; run: latemergeret 5 77 => i32:1
;; run: latemergeret 50 77 => i32:7002
;; run: latemergeret 50 7 => i32:1
;; run: latemergeret 11 77 => i32:2
;; run: latemergeret 50 1 => i32:2
;; run: addr 100 0 => i32:102
;; run: addr 100 1 => i32:102
;; run: addr 7 5 => i32:497
;; run: addr 4294967294 9 => i32:-7472
;; run: addrswap 100 0 => i32:302
;; run: addrswap 100 1 => i32:302
;; run: addrswap 7 5 => i32:2083
;; run: addrswap 4294967294 9 => i32:-27300
;; run: addrk 100 0 => i32:103
;; run: addrk 100 1 => i32:103
;; run: addrk 7 5 => i32:486
;; run: addrk 4294967294 9 => i32:257
;; run: addrtwo 100 0 => i32:25702
;; run: addrtwo 100 1 => i32:25702
;; run: addrtwo 7 5 => i32:211697
;; run: addrtwo 4294967294 9 => i32:-2771760
;; run: addrself 100 0 => i32:2
;; run: addrself 100 1 => i32:2
;; run: addrself 7 5 => i32:22
;; run: addrself 4294967294 9 => i32:1466
;; run: addrxor 100 0 => i32:102
;; run: addrxor 100 1 => i32:102
;; run: addrxor 7 5 => i32:783
;; run: addrxor 4294967294 9 => i32:6160
;; run: addrsubr 100 0 => i32:102
;; run: addrsubr 100 1 => i32:102
;; run: addrsubr 7 5 => i32:829
;; run: addrsubr 4294967294 9 => i32:-8112
;; run: addrgone 0 5 => i32:3500602
;; run: addrgone 2 100 => i32:3500697
;; run: addrgone 4 7 => i32:3600605
;; run: addrgone 9 0 => i32:3700599
;; run: addrbrif 0 => i32:2
;; run: addrbrif 4294967295 => i32:3
;; run: addrbrif 4294967291 => i32:727
;; run: addrbrif 4294967287 => i32:117371
;; run: addrnest 0 => i32:81
;; run: addrnest 1 => i32:81
;; run: addrnest 3 => i32:384673286
;; run: addrnest 6 => i32:-2045832223
;; run: addrearly 100 0 => i32:102
;; run: addrearly 100 1 => i32:102
;; run: addrearly 7 5 => i32:497
;; run: addrearly 4294967294 9 => i32:-7472
;; run: addresc 100 0 => i32:100102
;; run: addresc 100 1 => i32:100102
;; run: addresc 7 5 => i32:11497
;; run: addresc 4294967294 9 => i32:-1472

(module
  (type $t1 (func (param i32)))
  (memory 1)
  (table 1 funcref)
  (elem (i32.const 0) $thrower)
  (tag $e)
  (data (i32.const 0) "\03\07\01\09\04\06\02\08\05\00\00\00\00\00\00\00\01\01\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\02\03\04\05\06\00\00\00\00\00\00\00\00\00\02\04\06\08\01\03\05\00\00\00\00\00\00\00\00\00")
  ;; throws at 3; called through the table at an index read from memory, which keeps it out of line
  (func $thrower (type $t1) (if (i32.eq (local.get 0) (i32.const 3)) (then (throw $e))))
  (func $same (export "same") (param $a i32) (result i32)
    (local $i i32) (local $j i32)
    (local.set $i (local.get $a))
    (local.set $j (local.get $a))
    (block $out
      (loop $l
        (br_if $out (i32.eqz (i32.load8_u (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $j) (i32.const 3)) (local.get $i)))
  (func $apart (export "apart") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (local.set $i (i32.const 2))
    (local.set $j (i32.const 9))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $j) (i32.const 1000)) (local.get $s)))
  (func $below (export "below") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (local.set $i (i32.const 20))
    (local.set $j (i32.const 3))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 5)) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $j) (i32.const 65537)) (local.get $s)))
  (func $stepped (export "stepped") (param $n i32) (result i32)
    (local $i i32) (local $j i32)
    (local.set $j (i32.const 100))
    (loop $l
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (i32.xor (local.get $j) (i32.shl (local.get $i) (i32.const 16))))
  (func $oldafter (export "oldafter") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $o i32)
    (local.set $i (i32.const 38))
    (local.set $j (i32.const 1000))
    (loop $l
      (local.set $o (local.get $j))
      (local.set $j (i32.sub (local.get $j) (i32.const 1)))
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (br_if $l (i32.gt_u (i32.load8_u (local.get $i)) (local.get $n))))
    (i32.add (local.get $o) (i32.mul (local.get $i) (i32.const 7))))
  (func $mixed (export "mixed") (param $n i32) (result i32)
    (local $i i32) (local $j i32)
    (local.set $i (i32.const 38))
    (local.set $j (i32.const 500))
    (block $out
      (loop $l
        (br_if $out (i32.le_u (i32.load8_u (local.get $i)) (local.get $n)))
        (local.set $i (i32.sub (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const -1)))
        (br $l)))
    (i32.sub (i32.mul (local.get $j) (i32.const 5)) (local.get $i)))
  (func $wide (export "wide") (param $n i64) (result i64)
    (local $i i64) (local $j i64)
    (local.set $i (i64.const 5))
    (local.set $j (i64.const 0x700000003))
    (block $out
      (loop $l
        (br_if $out (i64.ge_u (local.get $i) (local.get $n)))
        (local.set $i (i64.add (local.get $i) (i64.const 1)))
        (local.set $j (i64.add (local.get $j) (i64.const 1)))
        (br $l)))
    (i64.xor (local.get $j) (i64.shl (local.get $i) (i64.const 40))))
  (func $threeout (export "threeout") (param $p i32) (result i32)
    (local $c1 i32) (local $c2 i32) (local $c3 i32)
    (local.set $c1 (i32.const 1))
    (local.set $c2 (i32.const 10))
    (local.set $c3 (i32.const 100))
    (block $out
      (loop $l
        (br_if $out (i32.eqz (i32.load8_u (local.get $p))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (local.set $c1 (i32.add (local.get $c1) (i32.const 1)))
        (local.set $c2 (i32.add (local.get $c2) (i32.const 1)))
        (local.set $c3 (i32.add (local.get $c3) (i32.const 1)))
        (br $l)))
    (i32.add (i32.add (local.get $c1) (i32.mul (local.get $c2) (i32.const 100)))
             (i32.add (i32.mul (local.get $c3) (i32.const 10000)) (local.get $p))))
  (func $leadconst (export "leadconst") (param $p i32) (result i32)
    (local $b i32) (local $c i32) (local $s i32)
    (local.set $c (i32.const 5))
    (block $out
      (loop $l
        (br_if $out (i32.eqz (i32.load8_u (local.get $p))))
        (local.set $s (i32.add (local.get $s) (i32.mul (local.get $b) (i32.load8_u (local.get $p)))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (local.set $b (i32.add (local.get $b) (i32.const 1)))
        (local.set $c (i32.add (local.get $c) (i32.const 1)))
        (br $l)))
    (i32.add (i32.add (local.get $s) (i32.mul (local.get $c) (i32.const 1000))) (local.get $p)))
  (func $ret (export "ret") (param $n i32) (result i32)
    (local $i i32) (local $j i32)
    (local.set $j (i32.const 40))
    (block $out
      (loop $l
        (if (i32.eq (i32.load8_u (local.get $i)) (local.get $n)) (then (return (local.get $j))))
        (br_if $out (i32.eqz (i32.load8_u (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.sub (local.get $i) (i32.const 1000)))
  (func $twoback (export "twoback") (param $a i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32) (local $c i32)
    (local.set $i (local.get $a))
    (local.set $j (local.get $a))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $i)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (if (i32.and (local.get $c) (i32.const 1))
          (then (i32.store8 (i32.const 200) (local.get $c)) (br $l)))
        (local.set $s (i32.add (local.get $s) (local.get $c)))
        (br $l)))
    (i32.add (i32.mul (local.get $s) (i32.const 100)) (local.get $j)))
  (func $twowaysrot (export "twowaysrot") (param $a i32) (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (if (local.get $a)
      (then (local.set $j (i32.const 5)) (i32.store8 (i32.const 201) (local.get $a)))
      (else (local.set $i (i32.const 2)) (local.set $j (i32.const 7)) (i32.store8 (i32.const 202) (local.get $n))))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 7)) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 4096))))
  (func $nested (export "nested") (param $n i32) (result i32)
    (local $a i32) (local $b i32) (local $c i32) (local $d i32) (local $t i32)
    (local.set $b (i32.const 30))
    (block $out
      (loop $o
        (br_if $out (i32.ge_u (local.get $a) (local.get $n)))
        (local.set $c (i32.const 0))
        (local.set $d (i32.const 9))
        (block $in
          (loop $l
            (br_if $in (i32.le_u (i32.load8_u offset=48 (local.get $c)) (local.get $a)))
            (local.set $c (i32.add (local.get $c) (i32.const 1)))
            (local.set $d (i32.add (local.get $d) (i32.const 1)))
            (br $l)))
        (local.set $t (i32.add (i32.mul (local.get $t) (i32.const 11)) (local.get $d)))
        (local.set $a (i32.add (local.get $a) (i32.const 1)))
        (local.set $b (i32.add (local.get $b) (i32.const 1)))
        (br $o)))
    (i32.add (local.get $t) (i32.mul (local.get $b) (i32.const 100000))))
  (func $sibling (export "sibling") (param $a i32) (result i32)
    (local $i i32) (local $j i32) (local $k i32) (local $m i32)
    (local.set $j (i32.const 7))
    (block $o1
      (loop $l1
        (br_if $o1 (i32.ge_u (local.get $i) (local.get $a)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l1)))
    (local.set $k (local.get $j))
    (local.set $m (local.get $j))
    (block $o2
      (loop $l2
        (br_if $o2 (i32.load8_u (local.get $k)))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (local.set $m (i32.add (local.get $m) (i32.const 1)))
        (br $l2)))
    (i32.add (i32.add (i32.mul (local.get $m) (i32.const 3)) (local.get $k)) (i32.mul (local.get $i) (i32.const 1000))))
  (func $many (export "many") (param $n i32) (result i32)
    (local $c0 i32) (local $c1 i32) (local $c2 i32) (local $c3 i32) (local $c4 i32) (local $c5 i32) (local $c6 i32) (local $c7 i32) (local $c8 i32) (local $c9 i32) (local $c10 i32) (local $c11 i32) (local $c12 i32) (local $c13 i32) (local $c14 i32) (local $c15 i32) (local $c16 i32) (local $i i32) (local $s i32)
    (local.set $c0 (i32.const 1))
    (local.set $c1 (i32.const 2))
    (local.set $c2 (i32.const 3))
    (local.set $c3 (i32.const 4))
    (local.set $c4 (i32.const 5))
    (local.set $c5 (i32.const 6))
    (local.set $c6 (i32.const 7))
    (local.set $c7 (i32.const 8))
    (local.set $c8 (i32.const 9))
    (local.set $c9 (i32.const 10))
    (local.set $c10 (i32.const 11))
    (local.set $c11 (i32.const 12))
    (local.set $c12 (i32.const 13))
    (local.set $c13 (i32.const 14))
    (local.set $c14 (i32.const 15))
    (local.set $c15 (i32.const 16))
    (local.set $c16 (i32.const 17))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (i32.load (i32.const 208)) (local.get $n)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $c0)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $c1)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $c2)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $c3)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $c4)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $c5)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $c6)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $c7)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $c8)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $c9)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $c10)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $c11)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $c12)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $c13)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $c14)))
        (local.set $c0 (i32.add (local.get $c0) (i32.const 1)))
        (local.set $c1 (i32.add (local.get $c1) (i32.const 1)))
        (local.set $c2 (i32.add (local.get $c2) (i32.const 1)))
        (local.set $c3 (i32.add (local.get $c3) (i32.const 1)))
        (local.set $c4 (i32.add (local.get $c4) (i32.const 1)))
        (local.set $c5 (i32.add (local.get $c5) (i32.const 1)))
        (local.set $c6 (i32.add (local.get $c6) (i32.const 1)))
        (local.set $c7 (i32.add (local.get $c7) (i32.const 1)))
        (local.set $c8 (i32.add (local.get $c8) (i32.const 1)))
        (local.set $c9 (i32.add (local.get $c9) (i32.const 1)))
        (local.set $c10 (i32.add (local.get $c10) (i32.const 1)))
        (local.set $c11 (i32.add (local.get $c11) (i32.const 1)))
        (local.set $c12 (i32.add (local.get $c12) (i32.const 1)))
        (local.set $c13 (i32.add (local.get $c13) (i32.const 1)))
        (local.set $c14 (i32.add (local.get $c14) (i32.const 1)))
        (local.set $c15 (i32.add (local.get $c15) (i32.const 1)))
        (local.set $c16 (i32.add (local.get $c16) (i32.const 1)))
        (i32.store (i32.const 208) (i32.add (i32.load (i32.const 208)) (i32.const 1)))
        (br $l)))
    (i32.store (i32.const 208) (i32.const 0))
    (i32.add (local.get $s) (i32.add (i32.mul (local.get $c15) (i32.const 7)) (i32.mul (local.get $c16) (i32.const 1000)))))
  (func $twoways (export "twoways") (param $a i32) (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (if (local.get $a)
      (then (local.set $i (i32.const 0)) (local.set $j (i32.const 5)) (i32.store8 (i32.const 201) (local.get $a)))
      (else (local.set $i (i32.const 2)) (local.set $j (i32.const 7)) (i32.store8 (i32.const 201) (local.get $n))))
    (loop $l
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 7)) (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 4096))))
  (func $twowayswrap (export "twowayswrap") (param $a i32) (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (if (local.get $a)
      (then (local.set $i (i32.const 20)) (local.set $j (i32.const 37)) (i32.store8 (i32.const 217) (local.get $a)))
      (else (local.set $i (i32.const -16)) (local.set $j (i32.const 1)) (i32.store8 (i32.const 217) (local.get $n))))
    (loop $l
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 7)) (i32.add (i32.and (local.get $i) (i32.const 15)) (i32.const 1))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 4096))))
  (func $leadlater (export "leadlater") (param $n i32) (result i32)
    (local $j i32) (local $i i32) (local $k i32) (local $s i32)
    (local.set $j (i32.const 5))
    (local.set $k (i32.const 9))
    (block $out
      (loop $l
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (br_if $out (i32.eqz (i32.load8_u (local.get $i))))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br $l)))
    (i32.add (i32.add (i32.mul (local.get $j) (i32.const 100)) (i32.mul (local.get $k) (i32.const 10000))) (local.get $s)))
  (func $wrapzero (export "wrapzero") (param $n i32) (result i32)
    (local $j i32) (local $i i32) (local $s i32)
    (local.set $j (i32.const -1))
    (block $out
      (loop $l
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (br_if $out (i32.eqz (i32.load8_u (local.get $i))))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $j) (i32.const 100)) (local.get $s)))
  (func $stripadd (export "stripadd") (param $x i32) (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (local.set $i (i32.add (local.get $x) (i32.const 3)))
    (local.set $j (i32.sub (local.get $x) (i32.const 2)))
    (loop $l
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 7)) (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 4096))))
  (func $mergeoff (export "mergeoff") (param $a i32) (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (if (local.get $a)
      (then (local.set $i (i32.and (local.get $n) (i32.const 3)))
            (local.set $j (i32.add (i32.and (local.get $n) (i32.const 3)) (i32.const 5)))
            (i32.store8 (i32.const 223) (local.get $a)))
      (else (local.set $i (i32.and (local.get $n) (i32.const 1)))
            (local.set $j (i32.add (i32.and (local.get $n) (i32.const 1)) (i32.const 5)))
            (i32.store8 (i32.const 224) (local.get $n))))
    (local.set $j (i32.add (local.get $j) (i32.const 3)))
    (loop $l
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 7)) (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 4096))))
  (func $goneclosed (export "goneclosed") (param $a i32) (result i32)
    (local $i i32) (local $j i32) (local $k i32) (local $t i32)
    (local.set $i (local.get $a))
    (local.set $j (i32.add (local.get $a) (i32.const 40)))
    (block $o1
      (loop $l
        (br_if $o1 (i32.eqz (i32.load8_u (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (local.set $k (i32.const 16))
    (block $out
      (loop $m
        (br_if $out (i32.eqz (i32.load8_u (local.get $k))))
        (local.set $t (i32.add (i32.mul (local.get $t) (i32.const 3)) (local.get $j)))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br $m)))
    (i32.add (local.get $t) (local.get $i)))
  (func $outret (export "outret") (param $n i32) (result i32)
    (local $i i32) (local $j i32)
    (local.set $i (i32.const 38))
    (local.set $j (i32.const 70))
    (loop $l
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (local.set $j (i32.sub (local.get $j) (i32.const 1)))
      (br_if $l (i32.gt_u (i32.load8_u (local.get $i)) (local.get $n))))
    (local.get $j))
  (func $outbrif (export "outbrif") (param $n i32) (result i32)
    (local $i i32) (local $j i32)
    (local.set $i (i32.const 38))
    (local.set $j (i32.const 4))
    (loop $l
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (local.set $j (i32.sub (local.get $j) (i32.const 1)))
      (br_if $l (i32.gt_u (i32.load8_u (local.get $i)) (local.get $n))))
    (if (result i32) (local.get $j)
      (then (i32.store8 (i32.const 210) (local.get $n)) (i32.const 111))
      (else (i32.store8 (i32.const 210) (local.get $i)) (i32.mul (local.get $i) (i32.const 3)))))
  (func $outedge (export "outedge") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $x i32)
    (local.set $i (i32.const 38))
    (local.set $j (i32.const 90))
    (local.set $x (i32.const 5))
    (loop $l
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (local.set $j (i32.sub (local.get $j) (i32.const 1)))
      (br_if $l (i32.gt_u (i32.load8_u (local.get $i)) (local.get $n))))
    (if (i32.and (local.get $n) (i32.const 1))
      (then (local.set $x (local.get $j)) (i32.store8 (i32.const 211) (local.get $n))))
    (i32.add (local.get $x) (i32.mul (local.get $i) (i32.const 1000))))
  (func $outloop (export "outloop") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $k i32) (local $t i32)
    (local.set $i (i32.const 38))
    (local.set $j (i32.const 50))
    (loop $l
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (local.set $j (i32.sub (local.get $j) (i32.const 1)))
      (br_if $l (i32.gt_u (i32.load8_u (local.get $i)) (local.get $n))))
    (local.set $k (i32.const 16))
    (block $out
      (loop $m
        (br_if $out (i32.eqz (i32.load8_u (local.get $k))))
        (local.set $t (i32.add (i32.mul (local.get $t) (i32.const 3)) (local.get $j)))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br $m)))
    (i32.add (local.get $t) (local.get $i)))
  (func $ehread (export "ehread") (param $n i32) (result i32)
    (local $i i32) (local $j i32)
    (local.set $j (i32.const 20))
    (block $h
      (try_table (catch_all $h)
        (block $out
          (loop $l
            (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
            (call_indirect (type $t1) (local.get $i) (i32.load8_u (i32.const 220)))
            (local.set $i (i32.add (local.get $i) (i32.const 1)))
            (local.set $j (i32.add (local.get $j) (i32.const 1)))
            (br $l)))
        (return (i32.add (local.get $j) (i32.const 1000)))))
    (i32.mul (local.get $j) (i32.const 7)))
  (func $ehblock (export "ehblock") (param $n i32) (param $c i32) (result i32)
    (local $i i32) (local $j i32) (local $r i32)
    (local.set $i (i32.const 38))
    (local.set $j (i32.const 70))
    (loop $l
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (local.set $j (i32.sub (local.get $j) (i32.const 1)))
      (br_if $l (i32.gt_u (i32.load8_u (local.get $i)) (local.get $n))))
    (block $h
      (try_table (catch_all $h)
        (if (local.get $c)
          (then (call_indirect (type $t1) (local.get $c) (i32.load8_u (i32.const 220)))
                (local.set $r (local.get $j)))
          (else (i32.store8 (i32.const 225) (local.get $n)) (local.set $r (i32.const 5))))
        (return (i32.add (local.get $r) (i32.mul (local.get $i) (i32.const 1000))))))
    (i32.const -7))
  (func $types (export "types") (param $n i32) (result i64)
    (local $i i32) (local $j i64)
    (local.set $j (i64.const 0x700000005))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i64.add (local.get $j) (i64.const 1)))
        (br $l)))
    (i64.xor (local.get $j) (i64.shl (i64.extend_i32_u (local.get $i)) (i64.const 20))))
  (func $steps (export "steps") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (local.set $j (i32.const 4))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 2)))
        (br $l)))
    (i32.add (i32.mul (local.get $j) (i32.const 1000)) (local.get $s)))
  (func $runtime (export "runtime") (param $i i32) (param $j i32) (result i32)
    (block $out
      (loop $l
        (br_if $out (i32.eqz (i32.load8_u (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $j) (i32.const 1000)) (local.get $i)))
  (func $halfconst (export "halfconst") (param $i i32) (result i32)
    (local $j i32)
    (local.set $j (i32.const 6))
    (block $out
      (loop $l
        (br_if $out (i32.eqz (i32.load8_u (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $j) (i32.const 1000)) (local.get $i)))
  (func $allread (export "allread") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (local.set $j (i32.const 11))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (i32.mul (local.get $i) (local.get $j))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (local.get $j)))
  (func $stepread (export "stepread") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (local.set $j (i32.const 11))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 3)) (local.get $j)))
        (br $l)))
    (i32.add (local.get $s) (local.get $j)))
  (func $brifon (export "brifon") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (local.set $i (local.get $n))
    (local.set $j (local.get $n))
    (loop $l
      (if (local.get $j)
        (then
          (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (i32.load8_u (local.get $i))))
          (local.set $j (i32.sub (local.get $j) (i32.const 1)))
          (local.set $i (i32.sub (local.get $i) (i32.const 1)))
          (br $l))))
    (i32.add (local.get $s) (i32.const 7)))
  (func $brifstep (export "brifstep") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (local.set $i (local.get $n))
    (local.set $j (local.get $n))
    (loop $l
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 5)) (i32.load8_u (local.get $i))))
      (local.set $j (i32.sub (local.get $j) (i32.const 1)))
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (br_if $l (local.get $j)))
    (i32.add (local.get $s) (i32.const 9)))
  (func $inneredge (export "inneredge") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $x i32)
    (local.set $j (i32.const 50))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (if (i32.and (i32.load8_u (local.get $i)) (i32.const 1))
          (then (local.set $x (local.get $j)) (i32.store8 (i32.const 203) (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $x) (i32.const 100)) (local.get $j)))
  (func $prev (export "prev") (param $n i32) (result i32)
    (local $c i32) (local $j i32) (local $p i32) (local $t i32)
    (local.set $j (i32.const 7))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $c) (local.get $n)))
        (br_if $out (i32.eqz (i32.load8_u (local.get $c))))
        (local.set $t (i32.add (i32.mul (local.get $t) (i32.const 3)) (i32.xor (local.get $c) (local.get $p))))
        (local.set $c (i32.add (local.get $c) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (local.set $p (local.get $j))
        (br $l)))
    (i32.add (local.get $t) (local.get $n)))
  (func $twobackdiff (export "twobackdiff") (param $a i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32) (local $c i32)
    (local.set $i (local.get $a))
    (local.set $j (local.get $a))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $i)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (if (i32.and (local.get $c) (i32.const 1))
          (then (local.set $j (i32.add (local.get $j) (i32.const 1)))
                (i32.store8 (i32.const 204) (local.get $c)) (br $l)))
        (local.set $j (i32.add (local.get $j) (i32.const 2)))
        (local.set $s (i32.add (local.get $s) (local.get $c)))
        (br $l)))
    (i32.add (i32.mul (local.get $s) (i32.const 100)) (local.get $j)))
  (func $twowaysrotdiff (export "twowaysrotdiff") (param $a i32) (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (if (local.get $a)
      (then (local.set $j (i32.const 5)) (i32.store8 (i32.const 205) (local.get $a)))
      (else (local.set $i (i32.const 2)) (local.set $j (i32.const 11)) (i32.store8 (i32.const 206) (local.get $n))))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 7)) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 4096))))
  (func $twowaysdiff (export "twowaysdiff") (param $a i32) (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (if (local.get $a)
      (then (local.set $i (i32.const 0)) (local.set $j (i32.const 5)) (i32.store8 (i32.const 205) (local.get $a)))
      (else (local.set $i (i32.const 2)) (local.set $j (i32.const 11)) (i32.store8 (i32.const 205) (local.get $n))))
    (loop $l
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 7)) (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 4096))))
  (func $xorstep (export "xorstep") (param $n i32) (result i32)
    (local $i i32) (local $q i32)
    (local.set $i (i32.const 38))
    (local.set $q (i32.const 6))
    (block $out
      (loop $l
        (br_if $out (i32.le_u (i32.load8_u (local.get $i)) (local.get $n)))
        (local.set $i (i32.sub (local.get $i) (i32.const 1)))
        (local.set $q (i32.xor (local.get $q) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $q) (i32.const 1000)) (local.get $i)))
  (func $updown (export "updown") (param $n i32) (result i32)
    (local $i i32) (local $j i32)
    (local.set $j (i32.const 100))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (br_if $out (i32.eqz (i32.load8_u (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $j (i32.sub (local.get $j) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $j) (i32.const 1000)) (local.get $i)))
  (func $outloopedge (export "outloopedge") (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $k i32) (local $x i32)
    (local.set $i (i32.const 38))
    (local.set $j (i32.const 50))
    (loop $l
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (local.set $j (i32.sub (local.get $j) (i32.const 1)))
      (br_if $l (i32.gt_u (i32.load8_u (local.get $i)) (local.get $n))))
    (local.set $k (i32.const 16))
    (block $out
      (loop $m
        (br_if $out (i32.eqz (i32.load8_u (local.get $k))))
        (if (i32.and (local.get $k) (i32.const 1))
          (then (local.set $x (local.get $j)) (i32.store8 (i32.const 213) (local.get $k))))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br $m)))
    (i32.add (local.get $x) (i32.mul (local.get $i) (i32.const 100))))
  (func $stripand (export "stripand") (param $x i32) (param $n i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32)
    (local.set $i (i32.and (local.get $x) (i32.const 15)))
    (local.set $j (local.get $x))
    (loop $l
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 7)) (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (local.set $j (i32.add (local.get $j) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (local.get $s) (i32.mul (local.get $j) (i32.const 4096))))
  (func $swap (export "swap") (param $n i32) (result i32)
    (local $c i32) (local $q i32) (local $p i32)
    (local.set $q (i32.const 7))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $c) (local.get $n)))
        (br_if $out (i32.eqz (i32.load8_u (local.get $c))))
        (local.set $c (i32.add (local.get $c) (i32.const 1)))
        (local.set $p (i32.add (local.get $q) (i32.const 1)))
        (local.set $q (i32.add (local.get $q) (i32.const 2)))
        (br $l)))
    (i32.add (i32.add (i32.mul (local.get $p) (i32.const 100)) (local.get $c)) (i32.mul (local.get $q) (i32.const 10000))))
  (func $condstep (export "condstep") (param $a i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32) (local $c i32)
    (local.set $i (local.get $a))
    (local.set $j (local.get $a))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $i)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (if (i32.and (local.get $c) (i32.const 1))
          (then (local.set $j (i32.add (local.get $j) (i32.const 1)))
                (i32.store8 (i32.const 219) (local.get $c)) (br $l)))
        (local.set $s (i32.add (local.get $s) (local.get $c)))
        (br $l)))
    (i32.add (i32.mul (local.get $s) (i32.const 100)) (local.get $j)))
  (func $condstep2 (export "condstep2") (param $a i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32) (local $c i32)
    (local.set $i (local.get $a))
    (local.set $j (local.get $a))
    (block $out
      (loop $l
        (local.set $c (i32.load8_u (local.get $i)))
        (br_if $out (i32.eqz (local.get $c)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (if (i32.and (local.get $c) (i32.const 1))
          (then
                (i32.store8 (i32.const 222) (local.get $c)) (br $l)))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (local.set $s (i32.add (local.get $s) (local.get $c)))
        (br $l)))
    (i32.add (i32.mul (local.get $s) (i32.const 100)) (local.get $j)))
  (func $rsub (export "rsub") (param $n i32) (result i32)
    (local $i i32) (local $p i32)
    (local.set $p (i32.const 3))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $p (i32.sub (i32.const 1) (local.get $p)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $p) (i32.const 100)) (local.get $i)))
  (func $varstep (export "varstep") (param $n i32) (result i32)
    (local $i i32) (local $p i32)
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $p (i32.add (local.get $p) (local.get $n)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (i32.mul (local.get $p) (i32.const 100)) (local.get $i)))
  (func $late (export "late") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32)
    (loop $l
      (local.set $o (local.get $i))
      (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (i32.mul (local.get $o) (i32.const 100)) (local.get $s)))
  (func $late64 (export "late64") (param $n i64) (result i64)
    (local $i i64) (local $s i64) (local $o i64)
    (local.set $i (i64.const 38))
    (loop $l
      (local.set $o (local.get $i))
      (local.set $s (i64.add (local.get $s) (i64.load8_u (i32.wrap_i64 (local.get $i)))))
      (local.set $i (i64.sub (local.get $i) (i64.const 1)))
      (br_if $l (i64.lt_u (local.get $s) (local.get $n))))
    (i64.add (i64.shl (local.get $o) (i64.const 32)) (local.get $s)))
  (func $lateafter (export "lateafter") (param $n i32) (result i32)
    (local $i i32) (local $q i32) (local $s i32) (local $o i32)
    (loop $l
      (local.set $o (local.get $i))
      (local.set $q (i32.add (local.get $i) (i32.const 1)))
      (local.set $s (i32.add (local.get $s) (i32.mul (i32.load8_u (local.get $q)) (local.get $i))))
      (local.set $i (local.get $q))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (i32.mul (local.get $o) (i32.const 100)) (local.get $s)))
  (func $lateabove (export "lateabove") (param $n i32) (param $m i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32) (local $r i32)
    (local.set $r
      (block $b (result i32)
        (return
          (i32.add (i32.const 5000)
            (block $a (result i32)
              (loop $l
                (drop (br_if $a (local.get $i) (i32.eq (i32.load8_u (local.get $i)) (local.get $m))))
                (local.set $o (local.get $i))
                (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
                (local.set $i (i32.add (local.get $i) (i32.const 1)))
                (drop (br_if $b (local.get $o) (i32.ge_u (local.get $s) (local.get $n))))
                (br $l))
              (unreachable))))))
    (i32.add (i32.mul (local.get $r) (i32.const 100)) (local.get $s)))
  (func $latebrif (export "latebrif") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32)
    (local.set $i (local.get $n))
    (loop $l
      (local.set $o (local.get $i))
      (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (i32.load8_u (local.get $i))))
      (local.set $i (i32.sub (local.get $i) (i32.const 1)))
      (br_if $l (local.get $o)))
    (i32.add (local.get $o) (local.get $s)))
  (func $lateinner (export "lateinner") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32) (local $x i32)
    (local.set $x (i32.const 77))
    (loop $l
      (local.set $o (local.get $i))
      (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (if (i32.and (local.get $s) (i32.const 1))
        (then (local.set $x (local.get $o)) (i32.store8 (i32.const 207) (local.get $s))))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (i32.add (i32.mul (local.get $o) (i32.const 100)) (local.get $s)) (i32.mul (local.get $x) (i32.const 10000))))
  (func $latebeside (export "latebeside") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32) (local $x i32)
    (loop $l
      (if (i32.and (i32.load8_u (local.get $i)) (i32.const 1))
        (then (local.set $x (i32.add (local.get $x) (local.get $i))) (i32.store8 (i32.const 209) (local.get $x))))
      (local.set $o (local.get $i))
      (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (i32.add (i32.mul (local.get $o) (i32.const 100)) (local.get $s)) (i32.mul (local.get $x) (i32.const 10000))))
  (func $lateret (export "lateret") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32)
    (loop $l
      (local.set $o (local.get $i))
      (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (local.get $o))
  (func $lateclosed (export "lateclosed") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32) (local $k i32) (local $t i32)
    (loop $l
      (local.set $o (local.get $i))
      (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (local.set $k (i32.const 16))
    (block $out
      (loop $m
        (br_if $out (i32.eqz (i32.load8_u (local.get $k))))
        (local.set $t (i32.add (i32.mul (local.get $t) (i32.const 3)) (local.get $o)))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br $m)))
    (i32.add (local.get $t) (local.get $s)))
  (func $lateoutloop (export "lateoutloop") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32) (local $k i32) (local $t i32) (local $r i32)
    (local.set $r
      (block $x (result i32)
        (loop $l
          (local.set $o (local.get $i))
          (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (drop (br_if $x (local.get $o) (i32.ge_u (local.get $s) (local.get $n))))
          (i32.store8 (i32.const 218) (local.get $s))
          (drop (br_if $x (local.get $o) (i32.eq (local.get $s) (i32.const 11))))
          (br $l))
        (unreachable)))
    (local.set $k (i32.const 16))
    (block $out
      (loop $m
        (br_if $out (i32.eqz (i32.load8_u (local.get $k))))
        (local.set $t (i32.add (i32.mul (local.get $t) (i32.const 3)) (local.get $r)))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br $m)))
    (i32.add (local.get $t) (local.get $s)))
  (func $lateafterblk (export "lateafterblk") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32)
    (loop $l
      (local.set $o (local.get $i))
      (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (if (i32.and (local.get $s) (i32.const 1))
        (then (i32.store8 (i32.const 214) (local.get $o))))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (i32.mul (local.get $o) (i32.const 100)) (i32.add (local.get $s) (i32.load8_u (i32.const 214)))))
  (func $latebrabove (export "latebrabove") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32)
    (loop $l
      (if (local.get $i) (then (i32.store8 (i32.const 215) (local.get $s))))
      (local.set $o (local.get $i))
      (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (i32.mul (local.get $o) (i32.const 100)) (local.get $s)))
  (func $lateedgeabove (export "lateedgeabove") (param $n i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32) (local $x i32)
    (loop $l
      (if (i32.and (i32.load8_u (local.get $i)) (i32.const 1))
        (then (local.set $x (local.get $i)) (i32.store8 (i32.const 216) (local.get $i))))
      (local.set $o (local.get $i))
      (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (i32.add (i32.mul (local.get $o) (i32.const 100)) (local.get $s)) (i32.mul (local.get $x) (i32.const 10000))))
  (func $lateaboveret (export "lateaboveret") (param $n i32) (param $m i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32)
    (loop $l
      (if (i32.eq (i32.load8_u (local.get $i)) (local.get $m)) (then (return (local.get $i))))
      (local.set $o (local.get $i))
      (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $s) (local.get $n))))
    (i32.add (i32.mul (local.get $o) (i32.const 100)) (local.get $s)))
  (func $latemerge (export "latemerge") (param $n i32) (param $m i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32)
    (i32.add (i32.const 7000)
      (block $y (result i32)
        (return
          (i32.add (local.get $m)
            (block $x (result i32)
              (loop $l
                (drop (br_if $x (local.get $i) (i32.eq (i32.load8_u (local.get $i)) (local.get $m))))
                (local.set $o (local.get $i))
                (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
                (local.set $i (i32.add (local.get $i) (i32.const 1)))
                (drop (br_if $x (local.get $o) (i32.ge_u (local.get $s) (local.get $n))))
                (drop (br_if $y (local.get $o) (i32.eq (local.get $s) (i32.const 11))))
                (br $l))
              (unreachable)))))))
  (func $latemergeret (export "latemergeret") (param $n i32) (param $m i32) (result i32)
    (local $i i32) (local $s i32) (local $o i32)
    (i32.add (i32.const 7000)
      (block $y (result i32)
        (return
          (block $x (result i32)
            (loop $l
              (drop (br_if $x (local.get $i) (i32.eq (i32.load8_u (local.get $i)) (local.get $m))))
              (local.set $o (local.get $i))
              (local.set $s (i32.add (local.get $s) (i32.load8_u (local.get $i))))
              (local.set $i (i32.add (local.get $i) (i32.const 1)))
              (drop (br_if $x (local.get $o) (i32.ge_u (local.get $s) (local.get $n))))
              (drop (br_if $y (local.get $o) (i32.eq (local.get $s) (i32.const 11))))
              (br $l))
            (unreachable))))))
  (func $addr (export "addr") (param $b i32) (param $n i32) (result i32)
    (local $i i32) (local $q i32) (local $s i32)
    (loop $l
      (local.set $q (i32.add (local.get $i) (i32.const 1)))
      (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 3)) (i32.add (i32.add (local.get $b) (local.get $i)) (i32.const 1))))
      (local.set $i (local.get $q))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (i32.add (local.get $s) (local.get $i)))
  (func $addrswap (export "addrswap") (param $b i32) (param $n i32) (result i32)
    (local $i i32) (local $q i32) (local $s i32)
    (loop $l
      (local.set $q (i32.add (local.get $i) (i32.const 1)))
      (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 3)) (i32.add (i32.add (local.get $i) (i32.mul (local.get $b) (i32.const 3))) (i32.const 1))))
      (local.set $i (local.get $q))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (i32.add (local.get $s) (local.get $i)))
  (func $addrk (export "addrk") (param $b i32) (param $n i32) (result i32)
    (local $i i32) (local $q i32) (local $s i32)
    (loop $l
      (local.set $q (i32.add (local.get $i) (i32.const 1)))
      (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 3)) (i32.add (i32.add (local.get $b) (local.get $i)) (i32.const 2))))
      (local.set $i (local.get $q))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (i32.add (local.get $s) (local.get $i)))
  (func $addrtwo (export "addrtwo") (param $b i32) (param $n i32) (result i32)
    (local $i i32) (local $q i32) (local $s i32)
    (loop $l
      (local.set $q (i32.add (local.get $i) (i32.const 1)))
      (local.set $s (i32.xor (i32.xor (i32.mul (local.get $s) (i32.const 3))
                                      (i32.shl (i32.add (local.get $b) (local.get $i)) (i32.const 8)))
                             (i32.add (i32.add (local.get $b) (local.get $i)) (i32.const 1))))
      (local.set $i (local.get $q))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (i32.add (local.get $s) (local.get $i)))
  (func $addrself (export "addrself") (param $b i32) (param $n i32) (result i32)
    (local $i i32) (local $q i32) (local $s i32)
    (loop $l
      (local.set $q (i32.add (local.get $i) (i32.const 1)))
      (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 3)) (i32.add (i32.add (local.get $i) (local.get $i)) (i32.const 1))))
      (local.set $i (local.get $q))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (i32.add (local.get $s) (local.get $i)))
  (func $addrxor (export "addrxor") (param $b i32) (param $n i32) (result i32)
    (local $i i32) (local $q i32) (local $s i32)
    (loop $l
      (local.set $q (i32.add (local.get $i) (i32.const 1)))
      (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 3)) (i32.xor (i32.add (local.get $b) (local.get $i)) (i32.const 1))))
      (local.set $i (local.get $q))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (i32.add (local.get $s) (local.get $i)))
  (func $addrsubr (export "addrsubr") (param $b i32) (param $n i32) (result i32)
    (local $i i32) (local $q i32) (local $s i32)
    (loop $l
      (local.set $q (i32.add (local.get $i) (i32.const 1)))
      (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 3)) (i32.add (i32.sub (local.get $b) (local.get $i)) (i32.const 1))))
      (local.set $i (local.get $q))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (i32.add (local.get $s) (local.get $i)))
  (func $addrgone (export "addrgone") (param $n i32) (param $x i32) (result i32)
    (local $i i32) (local $j i32) (local $o i32) (local $r i32)
    (local.set $i (i32.const 38))
    (local.set $j (i32.const 600))
    (local.set $r
      (block $out (result i32)
        (loop $l
          (local.set $o (local.get $j))
          (local.set $j (i32.sub (local.get $j) (i32.const 1)))
          (local.set $i (i32.sub (local.get $i) (i32.const 1)))
          (drop (br_if $out (local.get $o) (i32.le_u (i32.load8_u (local.get $i)) (local.get $n))))
          (i32.store8 (i32.const 221) (local.get $i))
          (drop (br_if $out (local.get $o) (i32.eq (local.get $i) (i32.const 35))))
          (br $l))
        (unreachable)))
    (i32.add (i32.add (i32.add (local.get $r) (local.get $x)) (i32.const -1))
             (i32.mul (local.get $i) (i32.const 100000))))
  (func $addrbrif (export "addrbrif") (param $b i32) (result i32)
    (local $i i32) (local $q i32) (local $s i32) (local $r i32)
    (loop $l
      (local.set $q (i32.add (local.get $i) (i32.const 1)))
      (local.set $r (i32.add (local.get $b) (local.get $i)))
      (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 3)) (i32.add (local.get $r) (i32.const 1))))
      (local.set $i (local.get $q))
      (br_if $l (local.get $r)))
    (i32.add (local.get $s) (local.get $i)))
  (func $addrnest (export "addrnest") (param $n i32) (result i32)
    (local $i i32) (local $q i32) (local $j i32) (local $k i32) (local $t i32)
    (loop $o
      (local.set $q (i32.add (local.get $i) (i32.const 1)))
      (local.set $j (i32.const 0))
      (local.set $k (i32.const 48))
      (loop $l
        (local.set $t (i32.xor (i32.mul (local.get $t) (i32.const 3))
                               (i32.add (i32.add (local.get $i) (local.get $j)) (i32.const 1))))
        (local.set $j (i32.add (local.get $j) (i32.const 1)))
        (local.set $k (i32.add (local.get $k) (i32.const 1)))
        (br_if $l (i32.load8_u (local.get $k))))
      (local.set $i (local.get $q))
      (br_if $o (i32.lt_u (local.get $i) (local.get $n))))
    (i32.add (local.get $t) (local.get $i)))
  (func $addrearly (export "addrearly") (param $b i32) (param $n i32) (result i32)
    (local $i i32) (local $s i32)
    (loop $l
      (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 3))
                             (i32.add (i32.add (local.get $b) (local.get $i)) (i32.const 1))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (i32.add (local.get $s) (local.get $i)))
  (func $addresc (export "addresc") (param $b i32) (param $n i32) (result i32)
    (local $i i32) (local $q i32) (local $s i32) (local $r i32)
    (loop $l
      (local.set $q (i32.add (local.get $i) (i32.const 1)))
      (local.set $r (i32.add (local.get $b) (local.get $i)))
      (local.set $s (i32.xor (i32.mul (local.get $s) (i32.const 3)) (i32.add (local.get $r) (i32.const 1))))
      (local.set $i (local.get $q))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (i32.add (i32.add (local.get $s) (local.get $i)) (i32.mul (local.get $r) (i32.const 1000))))
)
