;; A block that does nothing but test a value against constants, reached
;; by an edge along whose way the branches taken decide that test, is
;; passed by: the edge goes where the test would (opt.c thread_known;
;; TinyGo's switch on strings tests a length an earlier case's test fixed,
;; gobench/strswitch).  The way's facts: equal or not, unsigned and signed
;; ranges, through the value less a constant, a br_table's case and its
;; default, i32 and i64, deciding a branch or a br_table's case; a parameter the edge hands a constant or a compare
;; the way decides; an edge through a block that only hands its parameters
;; on.  And a parameter every edge hands one compare -- itself, or a
;; constant its way says it is -- is that compare (param_compares), not where
;; the edges hand two compares or a constant the way leaves open.  Not a
;; loop's header, which an edge past its test would enter in the middle,
;; nor where the way does not decide it.  Answers worked out in Python.
;;
;; stats -O1: threaded_known=26 param_compares=1
;; stats -O2: threaded_known=26 param_compares=1
;; stats -O3: threaded_known=26 param_compares=1
;;
;; run: eqway 0 0 => i32:0
;; run: rangeway 0 0 => i32:0
;; run: signed 0 0 => i32:2
;; run: fwd 0 0 => i32:2
;; run: pcmp 0 0 => i32:0
;; run: eqway 0 1 => i32:0
;; run: rangeway 0 1 => i32:0
;; run: signed 0 1 => i32:2
;; run: fwd 0 1 => i32:0
;; run: pcmp 0 1 => i32:1
;; run: eqway 0 7 => i32:0
;; run: rangeway 0 7 => i32:0
;; run: signed 0 7 => i32:2
;; run: fwd 0 7 => i32:0
;; run: pcmp 0 7 => i32:7
;; run: eqway 0 98 => i32:0
;; run: rangeway 0 98 => i32:0
;; run: signed 0 98 => i32:2
;; run: fwd 0 98 => i32:0
;; run: pcmp 0 98 => i32:98
;; run: eqway 0 99 => i32:0
;; run: rangeway 0 99 => i32:0
;; run: signed 0 99 => i32:2
;; run: fwd 0 99 => i32:0
;; run: pcmp 0 99 => i32:99
;; run: eqway 0 -1 => i32:0
;; run: rangeway 0 -1 => i32:0
;; run: signed 0 -1 => i32:2
;; run: fwd 0 -1 => i32:0
;; run: pcmp 0 -1 => i32:-1
;; run: brtab 0 => i32:0
;; run: eqway 3 0 => i32:0
;; run: rangeway 3 0 => i32:0
;; run: signed 3 0 => i32:0
;; run: fwd 3 0 => i32:2
;; run: pcmp 3 0 => i32:0
;; run: eqway 3 1 => i32:0
;; run: rangeway 3 1 => i32:0
;; run: signed 3 1 => i32:0
;; run: fwd 3 1 => i32:0
;; run: pcmp 3 1 => i32:1
;; run: eqway 3 7 => i32:0
;; run: rangeway 3 7 => i32:0
;; run: signed 3 7 => i32:0
;; run: fwd 3 7 => i32:0
;; run: pcmp 3 7 => i32:7
;; run: eqway 3 98 => i32:0
;; run: rangeway 3 98 => i32:0
;; run: signed 3 98 => i32:0
;; run: fwd 3 98 => i32:0
;; run: pcmp 3 98 => i32:98
;; run: eqway 3 99 => i32:0
;; run: rangeway 3 99 => i32:0
;; run: signed 3 99 => i32:0
;; run: fwd 3 99 => i32:0
;; run: pcmp 3 99 => i32:99
;; run: eqway 3 -1 => i32:0
;; run: rangeway 3 -1 => i32:0
;; run: signed 3 -1 => i32:0
;; run: fwd 3 -1 => i32:0
;; run: pcmp 3 -1 => i32:-1
;; run: brtab 3 => i32:0
;; run: eqway 4 0 => i32:0
;; run: rangeway 4 0 => i32:0
;; run: signed 4 0 => i32:0
;; run: fwd 4 0 => i32:2
;; run: pcmp 4 0 => i32:0
;; run: eqway 4 1 => i32:0
;; run: rangeway 4 1 => i32:1
;; run: signed 4 1 => i32:0
;; run: fwd 4 1 => i32:0
;; run: pcmp 4 1 => i32:1
;; run: eqway 4 7 => i32:0
;; run: rangeway 4 7 => i32:0
;; run: signed 4 7 => i32:0
;; run: fwd 4 7 => i32:0
;; run: pcmp 4 7 => i32:7
;; run: eqway 4 98 => i32:0
;; run: rangeway 4 98 => i32:0
;; run: signed 4 98 => i32:0
;; run: fwd 4 98 => i32:0
;; run: pcmp 4 98 => i32:98
;; run: eqway 4 99 => i32:0
;; run: rangeway 4 99 => i32:0
;; run: signed 4 99 => i32:0
;; run: fwd 4 99 => i32:0
;; run: pcmp 4 99 => i32:99
;; run: eqway 4 -1 => i32:0
;; run: rangeway 4 -1 => i32:0
;; run: signed 4 -1 => i32:0
;; run: fwd 4 -1 => i32:0
;; run: pcmp 4 -1 => i32:-1
;; run: brtab 4 => i32:0
;; run: eqway 5 0 => i32:3
;; run: rangeway 5 0 => i32:0
;; run: signed 5 0 => i32:0
;; run: fwd 5 0 => i32:12
;; run: pcmp 5 0 => i32:0
;; run: eqway 5 1 => i32:4
;; run: rangeway 5 1 => i32:1
;; run: signed 5 1 => i32:0
;; run: fwd 5 1 => i32:11
;; run: pcmp 5 1 => i32:1
;; run: eqway 5 7 => i32:10
;; run: rangeway 5 7 => i32:0
;; run: signed 5 7 => i32:0
;; run: fwd 5 7 => i32:11
;; run: pcmp 5 7 => i32:7
;; run: eqway 5 98 => i32:1
;; run: rangeway 5 98 => i32:0
;; run: signed 5 98 => i32:0
;; run: fwd 5 98 => i32:1
;; run: pcmp 5 98 => i32:98
;; run: eqway 5 99 => i32:102
;; run: rangeway 5 99 => i32:0
;; run: signed 5 99 => i32:0
;; run: fwd 5 99 => i32:11
;; run: pcmp 5 99 => i32:99
;; run: eqway 5 -1 => i32:2
;; run: rangeway 5 -1 => i32:0
;; run: signed 5 -1 => i32:0
;; run: fwd 5 -1 => i32:11
;; run: pcmp 5 -1 => i32:-1
;; run: brtab 5 => i32:0
;; run: eqway 6 0 => i32:0
;; run: rangeway 6 0 => i32:0
;; run: signed 6 0 => i32:0
;; run: fwd 6 0 => i32:2
;; run: pcmp 6 0 => i32:0
;; run: eqway 6 1 => i32:0
;; run: rangeway 6 1 => i32:1
;; run: signed 6 1 => i32:0
;; run: fwd 6 1 => i32:0
;; run: pcmp 6 1 => i32:1
;; run: eqway 6 7 => i32:0
;; run: rangeway 6 7 => i32:0
;; run: signed 6 7 => i32:0
;; run: fwd 6 7 => i32:0
;; run: pcmp 6 7 => i32:7
;; run: eqway 6 98 => i32:0
;; run: rangeway 6 98 => i32:0
;; run: signed 6 98 => i32:0
;; run: fwd 6 98 => i32:0
;; run: pcmp 6 98 => i32:98
;; run: eqway 6 99 => i32:0
;; run: rangeway 6 99 => i32:0
;; run: signed 6 99 => i32:0
;; run: fwd 6 99 => i32:0
;; run: pcmp 6 99 => i32:99
;; run: eqway 6 -1 => i32:0
;; run: rangeway 6 -1 => i32:0
;; run: signed 6 -1 => i32:0
;; run: fwd 6 -1 => i32:0
;; run: pcmp 6 -1 => i32:-1
;; run: brtab 6 => i32:0
;; run: eqway 7 0 => i32:0
;; run: rangeway 7 0 => i32:0
;; run: signed 7 0 => i32:0
;; run: fwd 7 0 => i32:2
;; run: pcmp 7 0 => i32:10
;; run: eqway 7 1 => i32:0
;; run: rangeway 7 1 => i32:0
;; run: signed 7 1 => i32:0
;; run: fwd 7 1 => i32:0
;; run: pcmp 7 1 => i32:11
;; run: eqway 7 7 => i32:0
;; run: rangeway 7 7 => i32:0
;; run: signed 7 7 => i32:0
;; run: fwd 7 7 => i32:0
;; run: pcmp 7 7 => i32:17
;; run: eqway 7 98 => i32:0
;; run: rangeway 7 98 => i32:0
;; run: signed 7 98 => i32:0
;; run: fwd 7 98 => i32:0
;; run: pcmp 7 98 => i32:108
;; run: eqway 7 99 => i32:2
;; run: rangeway 7 99 => i32:0
;; run: signed 7 99 => i32:0
;; run: fwd 7 99 => i32:0
;; run: pcmp 7 99 => i32:109
;; run: eqway 7 -1 => i32:0
;; run: rangeway 7 -1 => i32:0
;; run: signed 7 -1 => i32:0
;; run: fwd 7 -1 => i32:0
;; run: pcmp 7 -1 => i32:9
;; run: brtab 7 => i32:0
;; run: eqway 9 0 => i32:0
;; run: rangeway 9 0 => i32:9
;; run: signed 9 0 => i32:0
;; run: fwd 9 0 => i32:2
;; run: pcmp 9 0 => i32:0
;; run: eqway 9 1 => i32:0
;; run: rangeway 9 1 => i32:9
;; run: signed 9 1 => i32:0
;; run: fwd 9 1 => i32:0
;; run: pcmp 9 1 => i32:1
;; run: eqway 9 7 => i32:0
;; run: rangeway 9 7 => i32:9
;; run: signed 9 7 => i32:0
;; run: fwd 9 7 => i32:0
;; run: pcmp 9 7 => i32:7
;; run: eqway 9 98 => i32:0
;; run: rangeway 9 98 => i32:9
;; run: signed 9 98 => i32:0
;; run: fwd 9 98 => i32:0
;; run: pcmp 9 98 => i32:98
;; run: eqway 9 99 => i32:0
;; run: rangeway 9 99 => i32:9
;; run: signed 9 99 => i32:0
;; run: fwd 9 99 => i32:0
;; run: pcmp 9 99 => i32:99
;; run: eqway 9 -1 => i32:0
;; run: rangeway 9 -1 => i32:9
;; run: signed 9 -1 => i32:0
;; run: fwd 9 -1 => i32:0
;; run: pcmp 9 -1 => i32:-1
;; run: brtab 9 => i32:0
;; run: eqway 10 0 => i32:0
;; run: rangeway 10 0 => i32:0
;; run: signed 10 0 => i32:0
;; run: fwd 10 0 => i32:2
;; run: pcmp 10 0 => i32:0
;; run: eqway 10 1 => i32:0
;; run: rangeway 10 1 => i32:0
;; run: signed 10 1 => i32:0
;; run: fwd 10 1 => i32:0
;; run: pcmp 10 1 => i32:1
;; run: eqway 10 7 => i32:0
;; run: rangeway 10 7 => i32:0
;; run: signed 10 7 => i32:0
;; run: fwd 10 7 => i32:0
;; run: pcmp 10 7 => i32:7
;; run: eqway 10 98 => i32:0
;; run: rangeway 10 98 => i32:0
;; run: signed 10 98 => i32:0
;; run: fwd 10 98 => i32:0
;; run: pcmp 10 98 => i32:98
;; run: eqway 10 99 => i32:0
;; run: rangeway 10 99 => i32:0
;; run: signed 10 99 => i32:0
;; run: fwd 10 99 => i32:0
;; run: pcmp 10 99 => i32:99
;; run: eqway 10 -1 => i32:0
;; run: rangeway 10 -1 => i32:0
;; run: signed 10 -1 => i32:0
;; run: fwd 10 -1 => i32:0
;; run: pcmp 10 -1 => i32:-1
;; run: brtab 10 => i32:100
;; run: eqway 11 0 => i32:0
;; run: rangeway 11 0 => i32:0
;; run: signed 11 0 => i32:0
;; run: fwd 11 0 => i32:2
;; run: pcmp 11 0 => i32:0
;; run: eqway 11 1 => i32:0
;; run: rangeway 11 1 => i32:0
;; run: signed 11 1 => i32:0
;; run: fwd 11 1 => i32:0
;; run: pcmp 11 1 => i32:1
;; run: eqway 11 7 => i32:0
;; run: rangeway 11 7 => i32:0
;; run: signed 11 7 => i32:0
;; run: fwd 11 7 => i32:0
;; run: pcmp 11 7 => i32:7
;; run: eqway 11 98 => i32:0
;; run: rangeway 11 98 => i32:0
;; run: signed 11 98 => i32:0
;; run: fwd 11 98 => i32:0
;; run: pcmp 11 98 => i32:98
;; run: eqway 11 99 => i32:0
;; run: rangeway 11 99 => i32:0
;; run: signed 11 99 => i32:0
;; run: fwd 11 99 => i32:0
;; run: pcmp 11 99 => i32:99
;; run: eqway 11 -1 => i32:0
;; run: rangeway 11 -1 => i32:0
;; run: signed 11 -1 => i32:0
;; run: fwd 11 -1 => i32:0
;; run: pcmp 11 -1 => i32:-1
;; run: brtab 11 => i32:201
;; run: eqway 12 0 => i32:0
;; run: rangeway 12 0 => i32:0
;; run: signed 12 0 => i32:0
;; run: fwd 12 0 => i32:2
;; run: pcmp 12 0 => i32:0
;; run: eqway 12 1 => i32:0
;; run: rangeway 12 1 => i32:0
;; run: signed 12 1 => i32:0
;; run: fwd 12 1 => i32:0
;; run: pcmp 12 1 => i32:1
;; run: eqway 12 7 => i32:0
;; run: rangeway 12 7 => i32:0
;; run: signed 12 7 => i32:0
;; run: fwd 12 7 => i32:0
;; run: pcmp 12 7 => i32:7
;; run: eqway 12 98 => i32:0
;; run: rangeway 12 98 => i32:0
;; run: signed 12 98 => i32:0
;; run: fwd 12 98 => i32:0
;; run: pcmp 12 98 => i32:98
;; run: eqway 12 99 => i32:0
;; run: rangeway 12 99 => i32:0
;; run: signed 12 99 => i32:0
;; run: fwd 12 99 => i32:0
;; run: pcmp 12 99 => i32:99
;; run: eqway 12 -1 => i32:0
;; run: rangeway 12 -1 => i32:0
;; run: signed 12 -1 => i32:0
;; run: fwd 12 -1 => i32:0
;; run: pcmp 12 -1 => i32:-1
;; run: brtab 12 => i32:0
;; run: eqway -1 0 => i32:0
;; run: rangeway -1 0 => i32:0
;; run: signed -1 0 => i32:2
;; run: fwd -1 0 => i32:2
;; run: pcmp -1 0 => i32:0
;; run: eqway -1 1 => i32:0
;; run: rangeway -1 1 => i32:0
;; run: signed -1 1 => i32:2
;; run: fwd -1 1 => i32:0
;; run: pcmp -1 1 => i32:1
;; run: eqway -1 7 => i32:0
;; run: rangeway -1 7 => i32:0
;; run: signed -1 7 => i32:2
;; run: fwd -1 7 => i32:0
;; run: pcmp -1 7 => i32:7
;; run: eqway -1 98 => i32:0
;; run: rangeway -1 98 => i32:0
;; run: signed -1 98 => i32:2
;; run: fwd -1 98 => i32:0
;; run: pcmp -1 98 => i32:98
;; run: eqway -1 99 => i32:0
;; run: rangeway -1 99 => i32:0
;; run: signed -1 99 => i32:2
;; run: fwd -1 99 => i32:0
;; run: pcmp -1 99 => i32:99
;; run: eqway -1 -1 => i32:0
;; run: rangeway -1 -1 => i32:0
;; run: signed -1 -1 => i32:2
;; run: fwd -1 -1 => i32:0
;; run: pcmp -1 -1 => i32:-1
;; run: brtab -1 => i32:0
;; run: eqway -4 0 => i32:0
;; run: rangeway -4 0 => i32:0
;; run: signed -4 0 => i32:1
;; run: fwd -4 0 => i32:2
;; run: pcmp -4 0 => i32:0
;; run: eqway -4 1 => i32:0
;; run: rangeway -4 1 => i32:0
;; run: signed -4 1 => i32:2
;; run: fwd -4 1 => i32:0
;; run: pcmp -4 1 => i32:1
;; run: eqway -4 7 => i32:0
;; run: rangeway -4 7 => i32:0
;; run: signed -4 7 => i32:2
;; run: fwd -4 7 => i32:0
;; run: pcmp -4 7 => i32:7
;; run: eqway -4 98 => i32:0
;; run: rangeway -4 98 => i32:0
;; run: signed -4 98 => i32:2
;; run: fwd -4 98 => i32:0
;; run: pcmp -4 98 => i32:98
;; run: eqway -4 99 => i32:0
;; run: rangeway -4 99 => i32:0
;; run: signed -4 99 => i32:2
;; run: fwd -4 99 => i32:0
;; run: pcmp -4 99 => i32:99
;; run: eqway -4 -1 => i32:0
;; run: rangeway -4 -1 => i32:0
;; run: signed -4 -1 => i32:2
;; run: fwd -4 -1 => i32:0
;; run: pcmp -4 -1 => i32:-1
;; run: brtab -4 => i32:0
;; run: eqway -5 0 => i32:0
;; run: rangeway -5 0 => i32:0
;; run: signed -5 0 => i32:1
;; run: fwd -5 0 => i32:2
;; run: pcmp -5 0 => i32:0
;; run: eqway -5 1 => i32:0
;; run: rangeway -5 1 => i32:0
;; run: signed -5 1 => i32:2
;; run: fwd -5 1 => i32:0
;; run: pcmp -5 1 => i32:1
;; run: eqway -5 7 => i32:0
;; run: rangeway -5 7 => i32:0
;; run: signed -5 7 => i32:2
;; run: fwd -5 7 => i32:0
;; run: pcmp -5 7 => i32:7
;; run: eqway -5 98 => i32:0
;; run: rangeway -5 98 => i32:0
;; run: signed -5 98 => i32:2
;; run: fwd -5 98 => i32:0
;; run: pcmp -5 98 => i32:98
;; run: eqway -5 99 => i32:0
;; run: rangeway -5 99 => i32:0
;; run: signed -5 99 => i32:2
;; run: fwd -5 99 => i32:0
;; run: pcmp -5 99 => i32:99
;; run: eqway -5 -1 => i32:0
;; run: rangeway -5 -1 => i32:0
;; run: signed -5 -1 => i32:2
;; run: fwd -5 -1 => i32:0
;; run: pcmp -5 -1 => i32:-1
;; run: brtab -5 => i32:0
;; run: eqway 100 0 => i32:0
;; run: rangeway 100 0 => i32:0
;; run: signed 100 0 => i32:0
;; run: fwd 100 0 => i32:2
;; run: pcmp 100 0 => i32:0
;; run: eqway 100 1 => i32:0
;; run: rangeway 100 1 => i32:0
;; run: signed 100 1 => i32:0
;; run: fwd 100 1 => i32:0
;; run: pcmp 100 1 => i32:1
;; run: eqway 100 7 => i32:0
;; run: rangeway 100 7 => i32:0
;; run: signed 100 7 => i32:0
;; run: fwd 100 7 => i32:0
;; run: pcmp 100 7 => i32:7
;; run: eqway 100 98 => i32:0
;; run: rangeway 100 98 => i32:0
;; run: signed 100 98 => i32:0
;; run: fwd 100 98 => i32:0
;; run: pcmp 100 98 => i32:98
;; run: eqway 100 99 => i32:0
;; run: rangeway 100 99 => i32:0
;; run: signed 100 99 => i32:0
;; run: fwd 100 99 => i32:0
;; run: pcmp 100 99 => i32:99
;; run: eqway 100 -1 => i32:0
;; run: rangeway 100 -1 => i32:0
;; run: signed 100 -1 => i32:0
;; run: fwd 100 -1 => i32:0
;; run: pcmp 100 -1 => i32:-1
;; run: brtab 100 => i32:0
;; run: wide 0 0 => i32:0
;; run: wide 0 1 => i32:0
;; run: wide 1 0 => i32:0
;; run: wide 1 1 => i32:0
;; run: wide 4294967295 0 => i32:0
;; run: wide 4294967295 1 => i32:0
;; run: wide 4294967296 0 => i32:2
;; run: wide 4294967296 1 => i32:1
;; run: wide 4294967297 0 => i32:2
;; run: wide 4294967297 1 => i32:2
;; run: wide -1 0 => i32:2
;; run: wide -1 1 => i32:2
;; run: loophead 1 0 => i32:0
;; run: loophead 1 99 => i32:0
;; run: loophead 1 100 => i32:-1
;; run: loophead 1 -1 => i32:0
;; run: loophead 5 0 => i32:10
;; run: loophead 5 99 => i32:10
;; run: loophead 5 100 => i32:-1
;; run: loophead 5 -1 => i32:10
;; run: loophead 10 0 => i32:45
;; run: loophead 10 99 => i32:45
;; run: loophead 10 100 => i32:-1
;; run: loophead 10 -1 => i32:45
;; run: flag 0 0 => i32:1
;; run: flag 0 1 => i32:0
;; run: flag 0 7 => i32:0
;; run: flag 0 -1 => i32:0
;; run: flag 3 0 => i32:1
;; run: flag 3 1 => i32:2
;; run: flag 3 7 => i32:1
;; run: flag 3 -1 => i32:0
;; run: flag 4 0 => i32:1
;; run: flag 4 1 => i32:0
;; run: flag 4 7 => i32:0
;; run: flag 4 -1 => i32:0
;; run: flag -1 0 => i32:1
;; run: flag -1 1 => i32:0
;; run: flag -1 7 => i32:0
;; run: flag -1 -1 => i32:0
;; run: pcmp2 0 0 => i32:0
;; run: pcmp3 0 0 => i32:10
;; run: pcmp4 0 0 => i32:0
;; run: pcmp2 0 1 => i32:1
;; run: pcmp3 0 1 => i32:1
;; run: pcmp4 0 1 => i32:1
;; run: pcmp2 0 5 => i32:5
;; run: pcmp3 0 5 => i32:5
;; run: pcmp4 0 5 => i32:5
;; run: pcmp2 3 0 => i32:0
;; run: pcmp3 3 0 => i32:10
;; run: pcmp4 3 0 => i32:0
;; run: pcmp2 3 1 => i32:1
;; run: pcmp3 3 1 => i32:1
;; run: pcmp4 3 1 => i32:1
;; run: pcmp2 3 5 => i32:5
;; run: pcmp3 3 5 => i32:5
;; run: pcmp4 3 5 => i32:5
;; run: pcmp2 7 0 => i32:0
;; run: pcmp3 7 0 => i32:10
;; run: pcmp4 7 0 => i32:0
;; run: pcmp2 7 1 => i32:11
;; run: pcmp3 7 1 => i32:11
;; run: pcmp4 7 1 => i32:11
;; run: pcmp2 7 5 => i32:15
;; run: pcmp3 7 5 => i32:15
;; run: pcmp4 7 5 => i32:15
;; run: pcmp2 8 0 => i32:10
;; run: pcmp3 8 0 => i32:10
;; run: pcmp4 8 0 => i32:0
;; run: pcmp2 8 1 => i32:1
;; run: pcmp3 8 1 => i32:1
;; run: pcmp4 8 1 => i32:1
;; run: pcmp2 8 5 => i32:5
;; run: pcmp3 8 5 => i32:5
;; run: pcmp4 8 5 => i32:5
;; run: pcmp2 -1 0 => i32:0
;; run: pcmp3 -1 0 => i32:10
;; run: pcmp4 -1 0 => i32:0
;; run: pcmp2 -1 1 => i32:1
;; run: pcmp3 -1 1 => i32:1
;; run: pcmp4 -1 1 => i32:1
;; run: pcmp2 -1 5 => i32:5
;; run: pcmp3 -1 5 => i32:5
;; run: pcmp4 -1 5 => i32:5
;; run: flag2 0 0 => i32:1
;; run: gtu 0 0 => i32:2
;; run: flag2 0 1 => i32:2
;; run: gtu 0 1 => i32:2
;; run: flag2 0 7 => i32:1
;; run: gtu 0 7 => i32:2
;; run: flag2 0 9 => i32:10
;; run: gtu 0 9 => i32:2
;; run: brtab2 0 => i32:0
;; run: flag2 3 0 => i32:1
;; run: gtu 3 0 => i32:2
;; run: flag2 3 1 => i32:2
;; run: gtu 3 1 => i32:2
;; run: flag2 3 7 => i32:1
;; run: gtu 3 7 => i32:2
;; run: flag2 3 9 => i32:10
;; run: gtu 3 9 => i32:2
;; run: brtab2 3 => i32:0
;; run: flag2 4 0 => i32:1
;; run: gtu 4 0 => i32:2
;; run: flag2 4 1 => i32:2
;; run: gtu 4 1 => i32:2
;; run: flag2 4 7 => i32:8
;; run: gtu 4 7 => i32:2
;; run: flag2 4 9 => i32:10
;; run: gtu 4 9 => i32:2
;; run: brtab2 4 => i32:0
;; run: flag2 5 0 => i32:1
;; run: gtu 5 0 => i32:2
;; run: flag2 5 1 => i32:2
;; run: gtu 5 1 => i32:1
;; run: flag2 5 7 => i32:1
;; run: gtu 5 7 => i32:2
;; run: flag2 5 9 => i32:10
;; run: gtu 5 9 => i32:2
;; run: brtab2 5 => i32:0
;; run: flag2 6 0 => i32:1
;; run: gtu 6 0 => i32:0
;; run: flag2 6 1 => i32:2
;; run: gtu 6 1 => i32:0
;; run: flag2 6 7 => i32:1
;; run: gtu 6 7 => i32:0
;; run: flag2 6 9 => i32:10
;; run: gtu 6 9 => i32:0
;; run: brtab2 6 => i32:0
;; run: flag2 9 0 => i32:1
;; run: gtu 9 0 => i32:0
;; run: flag2 9 1 => i32:2
;; run: gtu 9 1 => i32:0
;; run: flag2 9 7 => i32:1
;; run: gtu 9 7 => i32:0
;; run: flag2 9 9 => i32:10
;; run: gtu 9 9 => i32:0
;; run: brtab2 9 => i32:0
;; run: flag2 10 0 => i32:1
;; run: gtu 10 0 => i32:0
;; run: flag2 10 1 => i32:0
;; run: gtu 10 1 => i32:0
;; run: flag2 10 7 => i32:1
;; run: gtu 10 7 => i32:0
;; run: flag2 10 9 => i32:0
;; run: gtu 10 9 => i32:0
;; run: brtab2 10 => i32:100
;; run: flag2 11 0 => i32:1
;; run: gtu 11 0 => i32:0
;; run: flag2 11 1 => i32:0
;; run: gtu 11 1 => i32:0
;; run: flag2 11 7 => i32:1
;; run: gtu 11 7 => i32:0
;; run: flag2 11 9 => i32:0
;; run: gtu 11 9 => i32:0
;; run: brtab2 11 => i32:200
;; run: flag2 12 0 => i32:1
;; run: gtu 12 0 => i32:0
;; run: flag2 12 1 => i32:0
;; run: gtu 12 1 => i32:0
;; run: flag2 12 7 => i32:1
;; run: gtu 12 7 => i32:0
;; run: flag2 12 9 => i32:0
;; run: gtu 12 9 => i32:0
;; run: brtab2 12 => i32:7
;; run: flag2 13 0 => i32:1
;; run: gtu 13 0 => i32:0
;; run: flag2 13 1 => i32:0
;; run: gtu 13 1 => i32:0
;; run: flag2 13 7 => i32:1
;; run: gtu 13 7 => i32:0
;; run: flag2 13 9 => i32:0
;; run: gtu 13 9 => i32:0
;; run: brtab2 13 => i32:0
;; run: flag2 19 0 => i32:1
;; run: gtu 19 0 => i32:0
;; run: flag2 19 1 => i32:0
;; run: gtu 19 1 => i32:0
;; run: flag2 19 7 => i32:1
;; run: gtu 19 7 => i32:0
;; run: flag2 19 9 => i32:0
;; run: gtu 19 9 => i32:0
;; run: brtab2 19 => i32:0
;; run: flag2 20 0 => i32:1
;; run: gtu 20 0 => i32:0
;; run: flag2 20 1 => i32:0
;; run: gtu 20 1 => i32:0
;; run: flag2 20 7 => i32:0
;; run: gtu 20 7 => i32:0
;; run: flag2 20 9 => i32:0
;; run: gtu 20 9 => i32:0
;; run: brtab2 20 => i32:0
;; run: flag2 25 0 => i32:1
;; run: gtu 25 0 => i32:0
;; run: flag2 25 1 => i32:0
;; run: gtu 25 1 => i32:0
;; run: flag2 25 7 => i32:0
;; run: gtu 25 7 => i32:0
;; run: flag2 25 9 => i32:0
;; run: gtu 25 9 => i32:0
;; run: brtab2 25 => i32:0
;; run: flag2 -1 0 => i32:1
;; run: gtu -1 0 => i32:0
;; run: flag2 -1 1 => i32:0
;; run: gtu -1 1 => i32:0
;; run: flag2 -1 7 => i32:0
;; run: gtu -1 7 => i32:0
;; run: flag2 -1 9 => i32:0
;; run: gtu -1 9 => i32:0
;; run: brtab2 -1 => i32:0
;; run: flag3 0 0 => i32:1
;; run: twopoint 0 0 => i32:0
;; run: flag3 0 1 => i32:2
;; run: twopoint 0 1 => i32:0
;; run: flag3 0 7 => i32:1
;; run: twopoint 0 7 => i32:0
;; run: flag3 0 9 => i32:10
;; run: twopoint 0 9 => i32:0
;; run: flag3 3 0 => i32:1
;; run: twopoint 3 0 => i32:0
;; run: flag3 3 1 => i32:2
;; run: twopoint 3 1 => i32:1
;; run: flag3 3 7 => i32:1
;; run: twopoint 3 7 => i32:0
;; run: flag3 3 9 => i32:10
;; run: twopoint 3 9 => i32:0
;; run: flag3 4 0 => i32:1
;; run: twopoint 4 0 => i32:0
;; run: flag3 4 1 => i32:2
;; run: twopoint 4 1 => i32:0
;; run: flag3 4 7 => i32:8
;; run: twopoint 4 7 => i32:0
;; run: flag3 4 9 => i32:10
;; run: twopoint 4 9 => i32:0
;; run: flag3 5 0 => i32:1
;; run: twopoint 5 0 => i32:0
;; run: flag3 5 1 => i32:2
;; run: twopoint 5 1 => i32:0
;; run: flag3 5 7 => i32:1
;; run: twopoint 5 7 => i32:0
;; run: flag3 5 9 => i32:10
;; run: twopoint 5 9 => i32:0
;; run: flag3 6 0 => i32:1
;; run: twopoint 6 0 => i32:0
;; run: flag3 6 1 => i32:2
;; run: twopoint 6 1 => i32:0
;; run: flag3 6 7 => i32:1
;; run: twopoint 6 7 => i32:0
;; run: flag3 6 9 => i32:10
;; run: twopoint 6 9 => i32:0
;; run: flag3 7 0 => i32:1
;; run: twopoint 7 0 => i32:7
;; run: flag3 7 1 => i32:2
;; run: twopoint 7 1 => i32:1
;; run: flag3 7 7 => i32:1
;; run: twopoint 7 7 => i32:7
;; run: flag3 7 9 => i32:10
;; run: twopoint 7 9 => i32:7
;; run: flag3 8 0 => i32:1
;; run: twopoint 8 0 => i32:0
;; run: flag3 8 1 => i32:2
;; run: twopoint 8 1 => i32:0
;; run: flag3 8 7 => i32:1
;; run: twopoint 8 7 => i32:0
;; run: flag3 8 9 => i32:10
;; run: twopoint 8 9 => i32:0
;; run: flag3 29 0 => i32:1
;; run: twopoint 29 0 => i32:0
;; run: flag3 29 1 => i32:2
;; run: twopoint 29 1 => i32:0
;; run: flag3 29 7 => i32:1
;; run: twopoint 29 7 => i32:0
;; run: flag3 29 9 => i32:10
;; run: twopoint 29 9 => i32:0
;; run: flag3 30 0 => i32:0
;; run: twopoint 30 0 => i32:0
;; run: flag3 30 1 => i32:0
;; run: twopoint 30 1 => i32:0
;; run: flag3 30 7 => i32:1
;; run: twopoint 30 7 => i32:0
;; run: flag3 30 9 => i32:0
;; run: twopoint 30 9 => i32:0
;; run: flag3 39 0 => i32:0
;; run: twopoint 39 0 => i32:0
;; run: flag3 39 1 => i32:0
;; run: twopoint 39 1 => i32:0
;; run: flag3 39 7 => i32:1
;; run: twopoint 39 7 => i32:0
;; run: flag3 39 9 => i32:0
;; run: twopoint 39 9 => i32:0
;; run: flag3 40 0 => i32:0
;; run: twopoint 40 0 => i32:0
;; run: flag3 40 1 => i32:0
;; run: twopoint 40 1 => i32:0
;; run: flag3 40 7 => i32:0
;; run: twopoint 40 7 => i32:0
;; run: flag3 40 9 => i32:0
;; run: twopoint 40 9 => i32:0
;; run: flag3 -1 0 => i32:0
;; run: twopoint -1 0 => i32:0
;; run: flag3 -1 1 => i32:0
;; run: twopoint -1 1 => i32:0
;; run: flag3 -1 7 => i32:0
;; run: twopoint -1 7 => i32:0
;; run: flag3 -1 9 => i32:0
;; run: twopoint -1 9 => i32:0
;; run: swtest 0 0 => i32:10
;; run: swtest 0 1 => i32:10
;; run: swtest 1 0 => i32:11
;; run: swtest 1 1 => i32:11
;; run: swtest 2 0 => i32:12
;; run: swtest 2 1 => i32:100
;; run: swtest 3 0 => i32:0
;; run: swtest 3 1 => i32:0
;; run: swtest 4 0 => i32:0
;; run: swtest 4 1 => i32:0
;; run: swtest 5 0 => i32:0
;; run: swtest 5 1 => i32:0
;; run: swtest 9 0 => i32:0
;; run: swtest 9 1 => i32:0
;; run: swtest -1 0 => i32:0
;; run: swtest -1 1 => i32:0

(module
  (memory 1)
  (func $eqway (export "eqway") (param $n i32) (param $b i32) (result i32)
    (block $next
      (br_if $next (i32.ne (local.get $n) (i32.const 5)))
      (br_if $next (i32.ne (local.get $b) (i32.const 98)))
      (return (i32.const 1)))
    (block $n2
      (br_if $n2 (i32.ne (local.get $n) (i32.const 7)))
      (br_if $n2 (i32.ne (local.get $b) (i32.const 99)))
      (return (i32.const 2)))
    (block $n3
      (br_if $n3 (i32.ne (local.get $n) (i32.const 5)))
      (return (i32.add (i32.const 3) (local.get $b))))
    (i32.const 0))
  (func $rangeway (export "rangeway") (param $n i32) (param $b i32) (result i32)
    (block $next
      (br_if $next (i32.ge_u (i32.sub (local.get $n) (i32.const 4)) (i32.const 3)))
      (br_if $next (i32.ne (local.get $b) (i32.const 1)))
      (return (i32.const 1)))
    (block $c
      (br_if $c (i32.ne (local.get $n) (i32.const 9)))
      (return (i32.const 9)))
    (i32.const 0))
  (func $brtab (export "brtab") (param $n i32) (result i32)
    (local $r i32)
    (block $d
      (block $c1
        (block $c0
          (br_table $c0 $c1 $d (i32.sub (local.get $n) (i32.const 10))))
        (local.set $r (i32.const 100))
        (br $d))
      (local.set $r (i32.const 200)))
    (if (result i32) (i32.eq (local.get $n) (i32.const 11))
      (then (i32.add (local.get $r) (i32.const 1)))
      (else (local.get $r))))
  (func $signed (export "signed") (param $n i32) (param $b i32) (result i32)
    (block $next
      (br_if $next (i32.ge_s (local.get $n) (i32.const -3)))
      (br_if $next (i32.ne (local.get $b) (i32.const 0)))
      (return (i32.const 1)))
    (block $c
      (br_if $c (i32.gt_s (local.get $n) (i32.const 0)))
      (return (i32.const 2)))
    (i32.const 0))
  (func $wide (export "wide") (param $n i64) (param $b i32) (result i32)
    (block $next
      (br_if $next (i64.ne (local.get $n) (i64.const 0x100000000)))
      (br_if $next (i32.ne (local.get $b) (i32.const 1)))
      (return (i32.const 1)))
    (block $c
      (br_if $c (i64.lt_u (local.get $n) (i64.const 0x100000000)))
      (return (i32.const 2)))
    (i32.const 0))
  (func $flag (export "flag") (param $n i32) (param $b i32) (result i32)
    (local $f i32)
    (block $next
      (local.set $f (i32.const 1))
      (br_if $next (i32.eqz (local.get $b)))
      (local.set $f (i32.eq (local.get $n) (i32.const 3)))
      (br_if $next (i32.ne (local.get $n) (i32.const 3)))
      (br_if $next (i32.ne (local.get $b) (i32.const 7)))
      (return (i32.const 1)))
    (if (local.get $f) (then (i32.store (i32.const 0) (local.get $b))))
    (i32.add (i32.load (i32.const 0)) (local.get $f)))
  (func $fwd (export "fwd") (param $n i32) (param $b i32) (result i32)
    (local $r i32)
    (block $join
      (local.set $r (i32.const 2))
      (br_if $join (i32.eqz (local.get $b)))
      (block $inner
        (local.set $r (i32.const 0))
        (br_if $inner (i32.ne (local.get $n) (i32.const 5)))
        (local.set $r (i32.const 1))
        (br_if $inner (i32.ne (local.get $b) (i32.const 98)))
        (return (i32.const 1)))
      (br $join))
    (if (result i32) (i32.eq (local.get $n) (i32.const 5))
      (then (i32.add (local.get $r) (i32.const 10)))
      (else (local.get $r))))
  (func $pcmp (export "pcmp") (param $n i32) (param $b i32) (result i32)
    (local $f i32)
    (block $j
      (block $a
        (br_if $a (i32.eq (local.get $n) (i32.const 3)))
        (local.set $f (i32.eq (local.get $n) (i32.const 7)))
        (br $j))
      (local.set $f (i32.const 0)))
    (i32.add (i32.mul (local.get $f) (i32.const 10)) (local.get $b)))
  (func $loophead (export "loophead") (param $n i32) (param $lim i32) (result i32)
    (local $i i32) (local $s i32)
    (if (i32.ge_s (local.get $lim) (i32.const 100)) (then (return (i32.const -1))))
    (block $out
      (loop $l
        (br_if $out (i32.ge_s (local.get $lim) (i32.const 100)))
        (local.set $s (i32.add (local.get $s) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_u (local.get $i) (local.get $n)))))
    (local.get $s))
  (func $pcmp2 (export "pcmp2") (param $n i32) (param $b i32) (result i32)
    (local $f i32)
    (block $j
      (block $a
        (br_if $a (i32.eqz (local.get $b)))
        (local.set $f (i32.eq (local.get $n) (i32.const 7)))
        (br $j))
      (local.set $f (i32.eq (local.get $n) (i32.const 8))))
    (i32.add (i32.mul (local.get $f) (i32.const 10)) (local.get $b)))
  (func $pcmp3 (export "pcmp3") (param $n i32) (param $b i32) (result i32)
    (local $f i32)
    (block $j
      (block $a
        (br_if $a (i32.eqz (local.get $b)))
        (local.set $f (i32.eq (local.get $n) (i32.const 7)))
        (br $j))
      (local.set $f (i32.const 1)))
    (i32.add (i32.mul (local.get $f) (i32.const 10)) (local.get $b)))
  (func $pcmp4 (export "pcmp4") (param $n i32) (param $b i32) (result i32)
    (local $f i32)
    (block $j
      (block $a
        (br_if $a (i32.eqz (local.get $b)))
        (local.set $f (i32.eq (local.get $n) (i32.const 7)))
        (br $j))
      (local.set $f (i32.const 0)))
    (i32.add (i32.mul (local.get $f) (i32.const 10)) (local.get $b)))
  (func $flag2 (export "flag2") (param $n i32) (param $b i32) (result i32)
    (local $f i32)
    (block $next
      (local.set $f (i32.const 1))
      (br_if $next (i32.eqz (local.get $b)))
      (local.set $f (i32.lt_u (local.get $n) (i32.const 10)))
      (br_if $next (i32.eq (local.get $n) (i32.const 4)))
      (br_if $next (i32.ge_u (local.get $n) (i32.const 20)))
      (br_if $next (i32.ne (local.get $b) (i32.const 7)))
      (return (i32.const 1)))
    (if (local.get $f) (then (i32.store (i32.const 0) (local.get $b))))
    (i32.add (i32.load (i32.const 0)) (local.get $f)))
  (func $gtu (export "gtu") (param $n i32) (param $b i32) (result i32)
    (block $next
      (br_if $next (i32.ne (local.get $n) (i32.const 5)))
      (br_if $next (i32.ne (local.get $b) (i32.const 1)))
      (return (i32.const 1)))
    (block $c
      (br_if $c (i32.gt_u (local.get $n) (i32.const 5)))
      (return (i32.const 2)))
    (i32.const 0))
  (func $brtab2 (export "brtab2") (param $n i32) (result i32)
    (local $r i32)
    (block $d
      (block $c1
        (block $c0
          (br_table $c0 $c1 $d (i32.sub (local.get $n) (i32.const 10))))
        (local.set $r (i32.const 100))
        (br $d))
      (local.set $r (i32.const 200)))
    (if (result i32) (i32.eq (local.get $n) (i32.const 12))
      (then (i32.add (local.get $r) (i32.const 7)))
      (else (local.get $r))))
  (func $flag3 (export "flag3") (param $n i32) (param $b i32) (result i32)
    (local $f i32)
    (block $next
      (local.set $f (i32.lt_u (local.get $n) (i32.const 10)))
      (br_if $next (i32.eq (local.get $n) (i32.const 4)))
      (local.set $f (i32.lt_u (local.get $n) (i32.const 30)))
      (br_if $next (i32.ge_u (local.get $n) (i32.const 40)))
      (br_if $next (i32.ne (local.get $b) (i32.const 7)))
      (return (i32.const 1)))
    (if (local.get $f) (then (i32.store (i32.const 0) (local.get $b))))
    (i32.add (i32.load (i32.const 0)) (local.get $f)))
  (func $twopoint (export "twopoint") (param $n i32) (param $b i32) (result i32)
    (block $next
      (br_if $next (i32.lt_u (local.get $n) (i32.const 3)))
      (br_if $next (i32.gt_u (local.get $n) (i32.const 7)))
      (br_if $next (i32.lt_u (i32.sub (local.get $n) (i32.const 4)) (i32.const 3)))
      (br_if $next (i32.ne (local.get $b) (i32.const 1)))
      (return (i32.const 1)))
    (block $c
      (br_if $c (i32.ne (local.get $n) (i32.const 7)))
      (return (i32.const 7)))
    (i32.const 0))
  (func $swtest (export "swtest") (param $n i32) (param $b i32) (result i32)
    (block $next
      (br_if $next (i32.ge_u (local.get $n) (i32.const 5)))
      (br_if $next (i32.ne (local.get $n) (i32.const 2)))
      (br_if $next (i32.ne (local.get $b) (i32.const 1)))
      (return (i32.const 100)))
    (block $d
      (block $c2
        (block $c1
          (block $c0
            (br_table $c0 $c1 $c2 $d (local.get $n)))
          (return (i32.const 10)))
        (return (i32.const 11)))
      (return (i32.const 12)))
    (i32.const 0))
)
