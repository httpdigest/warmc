;; Two pairs compared by their first members and, where those are equal, by
;; their second -- `a.m == b.m ? a.n < b.n : a.m < b.m`, a select of two
;; compares on a third -- where neither pair is the bits of one value: on
;; arm64 a compare of the seconds and a subtract with carry of the firsts,
;; whose flags are the whole difference's (isel.c find_cmp_chains' cch_lex,
;; isel_ops.inc emit_lex).  From test/gen_lex_sbc.py, which works out every
;; answer from the test as it is written.
;;
;;   four       four i32s, the firsts and the seconds less, unsigned: cmp,
;;             sbcs and the borrow
;;   ltu_ltu    the firsts lt u, the seconds lt u
;;   ltu_leu    the firsts lt u, the seconds le u
;;   ltu_gtu    the firsts lt u, the seconds gt u
;;   ltu_geu    the firsts lt u, the seconds ge u
;;   leu_ltu    the firsts le u, the seconds lt u
;;   leu_leu    the firsts le u, the seconds le u
;;   leu_gtu    the firsts le u, the seconds gt u
;;   leu_geu    the firsts le u, the seconds ge u
;;   gtu_ltu    the firsts gt u, the seconds lt u
;;   gtu_leu    the firsts gt u, the seconds le u
;;   gtu_gtu    the firsts gt u, the seconds gt u
;;   gtu_geu    the firsts gt u, the seconds ge u
;;   geu_ltu    the firsts ge u, the seconds lt u
;;   geu_leu    the firsts ge u, the seconds le u
;;   geu_gtu    the firsts ge u, the seconds gt u
;;   geu_geu    the firsts ge u, the seconds ge u
;;   lts_ltu    the firsts lt s, the seconds lt u
;;   lts_leu    the firsts lt s, the seconds le u
;;   lts_gtu    the firsts lt s, the seconds gt u
;;   lts_geu    the firsts lt s, the seconds ge u
;;   les_ltu    the firsts le s, the seconds lt u
;;   les_leu    the firsts le s, the seconds le u
;;   les_gtu    the firsts le s, the seconds gt u
;;   les_geu    the firsts le s, the seconds ge u
;;   gts_ltu    the firsts gt s, the seconds lt u
;;   gts_leu    the firsts gt s, the seconds le u
;;   gts_gtu    the firsts gt s, the seconds gt u
;;   gts_geu    the firsts gt s, the seconds ge u
;;   ges_ltu    the firsts ge s, the seconds lt u
;;   ges_leu    the firsts ge s, the seconds le u
;;   ges_gtu    the firsts ge s, the seconds gt u
;;   ges_geu    the firsts ge s, the seconds ge u
;;   ne         on a ne, its arms the other way round
;;   turned     the firsts' compare with its operands the other way round
;;             from the test for equality
;;   wide       four i64s, two 128-bit numbers in halves, signed: the same in
;;             x registers
;;   wideu      unsigned, and not strict
;;   m64n32     the firsts i64s and the seconds i32s: each compare in its own
;;             width
;;   m32n64     the firsts i32s and the seconds i64s
;;   nsigned    the seconds signed i32s that may be negative: no borrow says
;;             which is less, and the select stays
;;   npos       the seconds signed, each masked to 31 bits: never negative,
;;             the unsigned compare
;;   constn     the one second a constant, the compare's immediate
;;   constnle   the same not strict: the constant is what the other is taken
;;             from, in a register
;;   constneg   a negative constant, the immediate of a cmn
;;   constm     the one first a constant: in a register for the subtract
;;   usesm      the firsts' compare read once more: it is made, and the
;;             select of it stays
;;   usesn      the seconds' compare read once more
;;   usesc      the test for equality read once more
;;   twice      the select itself read twice, as a value: one cmp and sbcs,
;;             and a cset
;;   apart      the three compares made ahead of an arm that stores and the
;;             select where the arm ends, in another block: they are values
;;             by then, and the select stays
;;   pick       read by a select: a csel on the flags, no 0 or 1 made
;;   picks      read by two selects, one after the other: both on the same
;;             flags
;;   branch     read by a branch: a b.hs on the flags
;;   andcmp     anded with another compare for a select: cmp, sbcs and a ccmp
;;   orcmp      ored with another compare for a branch
;;   rot        the pairs each an i64's halves: the fold's one compare of the
;;             two rotated, no sbcs
;;   brs        the test as three branches: they stay
;;   recs       two records in memory, a signed key and an unsigned number
;;             each, loaded and compared
;;   sortrec    an insertion sort of the records in a copy, by that order:
;;             the comparator in its inner loop is a cmp, an sbcs and a branch
;;
;; stats -O2 a64: lex_sbcs=52 ccmp_chains=4
;; shape: four \bsbcs\b=1
;; shape: four \bcmp\b=1
;; shape: four \bcset\b=1
;; shape: four \bcsel\b=0
;; shape: wide \bsbcs\s+xzr=1
;; shape: wide \bcmp\s+x=1
;; shape: m64n32 \bsbcs\s+xzr=1
;; shape: m64n32 \bcmp\s+w=1
;; shape: m32n64 \bsbcs\s+wzr=1
;; shape: m32n64 \bcmp\s+x=1
;; shape: constn \bcmp\s+w\d+, #0x64=1
;; shape: constn \bsbcs\b=1
;; shape: constnle \bsbcs\b=1
;; shape: constneg \bcmn\s+w\d+, #0x5=1
;; shape: constneg \bsbcs\b=1
;; shape: twice \bsbcs\b=1
;; shape: twice \bcset\b=1
;; shape: twice \bcmp\b=1
;; shape: pick \bsbcs\b=1
;; shape: pick \bcsel\b=1
;; shape: pick \bcset\b=0
;; shape: pick \bcmp\b=1
;; shape: picks \bsbcs\b=1
;; shape: picks \bcsel\b=2
;; shape: picks \bcset\b=0
;; shape: picks \bcmp\b=1
;; shape: branch \bsbcs\b=1
;; shape: branch \bb\.hs\b=1
;; shape: branch \bcset\b=0
;; shape: branch \bcmp\b=1
;; shape: andcmp \bsbcs\b=1
;; shape: andcmp \bccmp\b=1
;; shape: andcmp \bcset\b=0
;; shape: andcmp \bcsel\b=1
;; shape: orcmp \bsbcs\b=1
;; shape: orcmp \bccmp\b=1
;; shape: orcmp \bcset\b=0
;; shape: rot \bsbcs\b=0
;; shape: rot \bror\b=2
;; shape: recs \bsbcs\b=1
;; shape: recs \bcset\b=1
;; loop: sortrec \bsbcs\b=1
;; loop: sortrec \bcset\b=0
;;
;; run: four 1 1 1 1 => i32:100
;; run: four 1 5 2 3 => i32:101
;; run: four 2 3 1 5 => i32:100
;; run: four 1 3 1 5 => i32:101
;; run: four 1 5 1 3 => i32:100
;; run: four 2147483648 0 1 0 => i32:100
;; run: four 1 0 2147483648 0 => i32:101
;; run: four 7 2147483648 7 1 => i32:100
;; run: four 7 1 7 2147483648 => i32:101
;; run: four 4294967295 4294967295 4294967295 4294967295 => i32:100
;; run: four 0 4294967295 1 0 => i32:101
;; run: four 1 0 0 4294967295 => i32:100
;; run: four 2147483648 2147483648 2147483648 2147483647 => i32:100
;; run: four 305419896 2596069104 305419896 2596069104 => i32:100
;; run: four 305419896 2596069104 2596069104 305419896 => i32:101
;; run: four 2596069104 305419896 305419896 2596069104 => i32:100
;; run: ltu_ltu 1 1 1 1 => i32:0
;; run: ltu_ltu 1 5 2 3 => i32:1
;; run: ltu_ltu 2 3 1 5 => i32:0
;; run: ltu_ltu 1 3 1 5 => i32:1
;; run: ltu_ltu 1 5 1 3 => i32:0
;; run: ltu_ltu 2147483648 0 1 0 => i32:0
;; run: ltu_ltu 1 0 2147483648 0 => i32:1
;; run: ltu_ltu 7 2147483648 7 1 => i32:0
;; run: ltu_ltu 7 1 7 2147483648 => i32:1
;; run: ltu_ltu 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: ltu_ltu 0 4294967295 1 0 => i32:1
;; run: ltu_ltu 1 0 0 4294967295 => i32:0
;; run: ltu_ltu 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: ltu_ltu 305419896 2596069104 305419896 2596069104 => i32:0
;; run: ltu_ltu 305419896 2596069104 2596069104 305419896 => i32:1
;; run: ltu_ltu 2596069104 305419896 305419896 2596069104 => i32:0
;; run: ltu_leu 1 1 1 1 => i32:1
;; run: ltu_leu 1 5 2 3 => i32:1
;; run: ltu_leu 2 3 1 5 => i32:0
;; run: ltu_leu 1 3 1 5 => i32:1
;; run: ltu_leu 1 5 1 3 => i32:0
;; run: ltu_leu 2147483648 0 1 0 => i32:0
;; run: ltu_leu 1 0 2147483648 0 => i32:1
;; run: ltu_leu 7 2147483648 7 1 => i32:0
;; run: ltu_leu 7 1 7 2147483648 => i32:1
;; run: ltu_leu 4294967295 4294967295 4294967295 4294967295 => i32:1
;; run: ltu_leu 0 4294967295 1 0 => i32:1
;; run: ltu_leu 1 0 0 4294967295 => i32:0
;; run: ltu_leu 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: ltu_leu 305419896 2596069104 305419896 2596069104 => i32:1
;; run: ltu_leu 305419896 2596069104 2596069104 305419896 => i32:1
;; run: ltu_leu 2596069104 305419896 305419896 2596069104 => i32:0
;; run: ltu_gtu 1 1 1 1 => i32:0
;; run: ltu_gtu 1 5 2 3 => i32:1
;; run: ltu_gtu 2 3 1 5 => i32:0
;; run: ltu_gtu 1 3 1 5 => i32:0
;; run: ltu_gtu 1 5 1 3 => i32:1
;; run: ltu_gtu 2147483648 0 1 0 => i32:0
;; run: ltu_gtu 1 0 2147483648 0 => i32:1
;; run: ltu_gtu 7 2147483648 7 1 => i32:1
;; run: ltu_gtu 7 1 7 2147483648 => i32:0
;; run: ltu_gtu 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: ltu_gtu 0 4294967295 1 0 => i32:1
;; run: ltu_gtu 1 0 0 4294967295 => i32:0
;; run: ltu_gtu 2147483648 2147483648 2147483648 2147483647 => i32:1
;; run: ltu_gtu 305419896 2596069104 305419896 2596069104 => i32:0
;; run: ltu_gtu 305419896 2596069104 2596069104 305419896 => i32:1
;; run: ltu_gtu 2596069104 305419896 305419896 2596069104 => i32:0
;; run: ltu_geu 1 1 1 1 => i32:1
;; run: ltu_geu 1 5 2 3 => i32:1
;; run: ltu_geu 2 3 1 5 => i32:0
;; run: ltu_geu 1 3 1 5 => i32:0
;; run: ltu_geu 1 5 1 3 => i32:1
;; run: ltu_geu 2147483648 0 1 0 => i32:0
;; run: ltu_geu 1 0 2147483648 0 => i32:1
;; run: ltu_geu 7 2147483648 7 1 => i32:1
;; run: ltu_geu 7 1 7 2147483648 => i32:0
;; run: ltu_geu 4294967295 4294967295 4294967295 4294967295 => i32:1
;; run: ltu_geu 0 4294967295 1 0 => i32:1
;; run: ltu_geu 1 0 0 4294967295 => i32:0
;; run: ltu_geu 2147483648 2147483648 2147483648 2147483647 => i32:1
;; run: ltu_geu 305419896 2596069104 305419896 2596069104 => i32:1
;; run: ltu_geu 305419896 2596069104 2596069104 305419896 => i32:1
;; run: ltu_geu 2596069104 305419896 305419896 2596069104 => i32:0
;; run: leu_ltu 1 1 1 1 => i32:0
;; run: leu_ltu 1 5 2 3 => i32:1
;; run: leu_ltu 2 3 1 5 => i32:0
;; run: leu_ltu 1 3 1 5 => i32:1
;; run: leu_ltu 1 5 1 3 => i32:0
;; run: leu_ltu 2147483648 0 1 0 => i32:0
;; run: leu_ltu 1 0 2147483648 0 => i32:1
;; run: leu_ltu 7 2147483648 7 1 => i32:0
;; run: leu_ltu 7 1 7 2147483648 => i32:1
;; run: leu_ltu 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: leu_ltu 0 4294967295 1 0 => i32:1
;; run: leu_ltu 1 0 0 4294967295 => i32:0
;; run: leu_ltu 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: leu_ltu 305419896 2596069104 305419896 2596069104 => i32:0
;; run: leu_ltu 305419896 2596069104 2596069104 305419896 => i32:1
;; run: leu_ltu 2596069104 305419896 305419896 2596069104 => i32:0
;; run: leu_leu 1 1 1 1 => i32:1
;; run: leu_leu 1 5 2 3 => i32:1
;; run: leu_leu 2 3 1 5 => i32:0
;; run: leu_leu 1 3 1 5 => i32:1
;; run: leu_leu 1 5 1 3 => i32:0
;; run: leu_leu 2147483648 0 1 0 => i32:0
;; run: leu_leu 1 0 2147483648 0 => i32:1
;; run: leu_leu 7 2147483648 7 1 => i32:0
;; run: leu_leu 7 1 7 2147483648 => i32:1
;; run: leu_leu 4294967295 4294967295 4294967295 4294967295 => i32:1
;; run: leu_leu 0 4294967295 1 0 => i32:1
;; run: leu_leu 1 0 0 4294967295 => i32:0
;; run: leu_leu 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: leu_leu 305419896 2596069104 305419896 2596069104 => i32:1
;; run: leu_leu 305419896 2596069104 2596069104 305419896 => i32:1
;; run: leu_leu 2596069104 305419896 305419896 2596069104 => i32:0
;; run: leu_gtu 1 1 1 1 => i32:0
;; run: leu_gtu 1 5 2 3 => i32:1
;; run: leu_gtu 2 3 1 5 => i32:0
;; run: leu_gtu 1 3 1 5 => i32:0
;; run: leu_gtu 1 5 1 3 => i32:1
;; run: leu_gtu 2147483648 0 1 0 => i32:0
;; run: leu_gtu 1 0 2147483648 0 => i32:1
;; run: leu_gtu 7 2147483648 7 1 => i32:1
;; run: leu_gtu 7 1 7 2147483648 => i32:0
;; run: leu_gtu 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: leu_gtu 0 4294967295 1 0 => i32:1
;; run: leu_gtu 1 0 0 4294967295 => i32:0
;; run: leu_gtu 2147483648 2147483648 2147483648 2147483647 => i32:1
;; run: leu_gtu 305419896 2596069104 305419896 2596069104 => i32:0
;; run: leu_gtu 305419896 2596069104 2596069104 305419896 => i32:1
;; run: leu_gtu 2596069104 305419896 305419896 2596069104 => i32:0
;; run: leu_geu 1 1 1 1 => i32:1
;; run: leu_geu 1 5 2 3 => i32:1
;; run: leu_geu 2 3 1 5 => i32:0
;; run: leu_geu 1 3 1 5 => i32:0
;; run: leu_geu 1 5 1 3 => i32:1
;; run: leu_geu 2147483648 0 1 0 => i32:0
;; run: leu_geu 1 0 2147483648 0 => i32:1
;; run: leu_geu 7 2147483648 7 1 => i32:1
;; run: leu_geu 7 1 7 2147483648 => i32:0
;; run: leu_geu 4294967295 4294967295 4294967295 4294967295 => i32:1
;; run: leu_geu 0 4294967295 1 0 => i32:1
;; run: leu_geu 1 0 0 4294967295 => i32:0
;; run: leu_geu 2147483648 2147483648 2147483648 2147483647 => i32:1
;; run: leu_geu 305419896 2596069104 305419896 2596069104 => i32:1
;; run: leu_geu 305419896 2596069104 2596069104 305419896 => i32:1
;; run: leu_geu 2596069104 305419896 305419896 2596069104 => i32:0
;; run: gtu_ltu 1 1 1 1 => i32:0
;; run: gtu_ltu 1 5 2 3 => i32:0
;; run: gtu_ltu 2 3 1 5 => i32:1
;; run: gtu_ltu 1 3 1 5 => i32:1
;; run: gtu_ltu 1 5 1 3 => i32:0
;; run: gtu_ltu 2147483648 0 1 0 => i32:1
;; run: gtu_ltu 1 0 2147483648 0 => i32:0
;; run: gtu_ltu 7 2147483648 7 1 => i32:0
;; run: gtu_ltu 7 1 7 2147483648 => i32:1
;; run: gtu_ltu 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: gtu_ltu 0 4294967295 1 0 => i32:0
;; run: gtu_ltu 1 0 0 4294967295 => i32:1
;; run: gtu_ltu 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: gtu_ltu 305419896 2596069104 305419896 2596069104 => i32:0
;; run: gtu_ltu 305419896 2596069104 2596069104 305419896 => i32:0
;; run: gtu_ltu 2596069104 305419896 305419896 2596069104 => i32:1
;; run: gtu_leu 1 1 1 1 => i32:1
;; run: gtu_leu 1 5 2 3 => i32:0
;; run: gtu_leu 2 3 1 5 => i32:1
;; run: gtu_leu 1 3 1 5 => i32:1
;; run: gtu_leu 1 5 1 3 => i32:0
;; run: gtu_leu 2147483648 0 1 0 => i32:1
;; run: gtu_leu 1 0 2147483648 0 => i32:0
;; run: gtu_leu 7 2147483648 7 1 => i32:0
;; run: gtu_leu 7 1 7 2147483648 => i32:1
;; run: gtu_leu 4294967295 4294967295 4294967295 4294967295 => i32:1
;; run: gtu_leu 0 4294967295 1 0 => i32:0
;; run: gtu_leu 1 0 0 4294967295 => i32:1
;; run: gtu_leu 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: gtu_leu 305419896 2596069104 305419896 2596069104 => i32:1
;; run: gtu_leu 305419896 2596069104 2596069104 305419896 => i32:0
;; run: gtu_leu 2596069104 305419896 305419896 2596069104 => i32:1
;; run: gtu_gtu 1 1 1 1 => i32:0
;; run: gtu_gtu 1 5 2 3 => i32:0
;; run: gtu_gtu 2 3 1 5 => i32:1
;; run: gtu_gtu 1 3 1 5 => i32:0
;; run: gtu_gtu 1 5 1 3 => i32:1
;; run: gtu_gtu 2147483648 0 1 0 => i32:1
;; run: gtu_gtu 1 0 2147483648 0 => i32:0
;; run: gtu_gtu 7 2147483648 7 1 => i32:1
;; run: gtu_gtu 7 1 7 2147483648 => i32:0
;; run: gtu_gtu 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: gtu_gtu 0 4294967295 1 0 => i32:0
;; run: gtu_gtu 1 0 0 4294967295 => i32:1
;; run: gtu_gtu 2147483648 2147483648 2147483648 2147483647 => i32:1
;; run: gtu_gtu 305419896 2596069104 305419896 2596069104 => i32:0
;; run: gtu_gtu 305419896 2596069104 2596069104 305419896 => i32:0
;; run: gtu_gtu 2596069104 305419896 305419896 2596069104 => i32:1
;; run: gtu_geu 1 1 1 1 => i32:1
;; run: gtu_geu 1 5 2 3 => i32:0
;; run: gtu_geu 2 3 1 5 => i32:1
;; run: gtu_geu 1 3 1 5 => i32:0
;; run: gtu_geu 1 5 1 3 => i32:1
;; run: gtu_geu 2147483648 0 1 0 => i32:1
;; run: gtu_geu 1 0 2147483648 0 => i32:0
;; run: gtu_geu 7 2147483648 7 1 => i32:1
;; run: gtu_geu 7 1 7 2147483648 => i32:0
;; run: gtu_geu 4294967295 4294967295 4294967295 4294967295 => i32:1
;; run: gtu_geu 0 4294967295 1 0 => i32:0
;; run: gtu_geu 1 0 0 4294967295 => i32:1
;; run: gtu_geu 2147483648 2147483648 2147483648 2147483647 => i32:1
;; run: gtu_geu 305419896 2596069104 305419896 2596069104 => i32:1
;; run: gtu_geu 305419896 2596069104 2596069104 305419896 => i32:0
;; run: gtu_geu 2596069104 305419896 305419896 2596069104 => i32:1
;; run: geu_ltu 1 1 1 1 => i32:0
;; run: geu_ltu 1 5 2 3 => i32:0
;; run: geu_ltu 2 3 1 5 => i32:1
;; run: geu_ltu 1 3 1 5 => i32:1
;; run: geu_ltu 1 5 1 3 => i32:0
;; run: geu_ltu 2147483648 0 1 0 => i32:1
;; run: geu_ltu 1 0 2147483648 0 => i32:0
;; run: geu_ltu 7 2147483648 7 1 => i32:0
;; run: geu_ltu 7 1 7 2147483648 => i32:1
;; run: geu_ltu 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: geu_ltu 0 4294967295 1 0 => i32:0
;; run: geu_ltu 1 0 0 4294967295 => i32:1
;; run: geu_ltu 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: geu_ltu 305419896 2596069104 305419896 2596069104 => i32:0
;; run: geu_ltu 305419896 2596069104 2596069104 305419896 => i32:0
;; run: geu_ltu 2596069104 305419896 305419896 2596069104 => i32:1
;; run: geu_leu 1 1 1 1 => i32:1
;; run: geu_leu 1 5 2 3 => i32:0
;; run: geu_leu 2 3 1 5 => i32:1
;; run: geu_leu 1 3 1 5 => i32:1
;; run: geu_leu 1 5 1 3 => i32:0
;; run: geu_leu 2147483648 0 1 0 => i32:1
;; run: geu_leu 1 0 2147483648 0 => i32:0
;; run: geu_leu 7 2147483648 7 1 => i32:0
;; run: geu_leu 7 1 7 2147483648 => i32:1
;; run: geu_leu 4294967295 4294967295 4294967295 4294967295 => i32:1
;; run: geu_leu 0 4294967295 1 0 => i32:0
;; run: geu_leu 1 0 0 4294967295 => i32:1
;; run: geu_leu 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: geu_leu 305419896 2596069104 305419896 2596069104 => i32:1
;; run: geu_leu 305419896 2596069104 2596069104 305419896 => i32:0
;; run: geu_leu 2596069104 305419896 305419896 2596069104 => i32:1
;; run: geu_gtu 1 1 1 1 => i32:0
;; run: geu_gtu 1 5 2 3 => i32:0
;; run: geu_gtu 2 3 1 5 => i32:1
;; run: geu_gtu 1 3 1 5 => i32:0
;; run: geu_gtu 1 5 1 3 => i32:1
;; run: geu_gtu 2147483648 0 1 0 => i32:1
;; run: geu_gtu 1 0 2147483648 0 => i32:0
;; run: geu_gtu 7 2147483648 7 1 => i32:1
;; run: geu_gtu 7 1 7 2147483648 => i32:0
;; run: geu_gtu 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: geu_gtu 0 4294967295 1 0 => i32:0
;; run: geu_gtu 1 0 0 4294967295 => i32:1
;; run: geu_gtu 2147483648 2147483648 2147483648 2147483647 => i32:1
;; run: geu_gtu 305419896 2596069104 305419896 2596069104 => i32:0
;; run: geu_gtu 305419896 2596069104 2596069104 305419896 => i32:0
;; run: geu_gtu 2596069104 305419896 305419896 2596069104 => i32:1
;; run: geu_geu 1 1 1 1 => i32:1
;; run: geu_geu 1 5 2 3 => i32:0
;; run: geu_geu 2 3 1 5 => i32:1
;; run: geu_geu 1 3 1 5 => i32:0
;; run: geu_geu 1 5 1 3 => i32:1
;; run: geu_geu 2147483648 0 1 0 => i32:1
;; run: geu_geu 1 0 2147483648 0 => i32:0
;; run: geu_geu 7 2147483648 7 1 => i32:1
;; run: geu_geu 7 1 7 2147483648 => i32:0
;; run: geu_geu 4294967295 4294967295 4294967295 4294967295 => i32:1
;; run: geu_geu 0 4294967295 1 0 => i32:0
;; run: geu_geu 1 0 0 4294967295 => i32:1
;; run: geu_geu 2147483648 2147483648 2147483648 2147483647 => i32:1
;; run: geu_geu 305419896 2596069104 305419896 2596069104 => i32:1
;; run: geu_geu 305419896 2596069104 2596069104 305419896 => i32:0
;; run: geu_geu 2596069104 305419896 305419896 2596069104 => i32:1
;; run: lts_ltu 1 1 1 1 => i32:0
;; run: lts_ltu 1 5 2 3 => i32:1
;; run: lts_ltu 2 3 1 5 => i32:0
;; run: lts_ltu 1 3 1 5 => i32:1
;; run: lts_ltu 1 5 1 3 => i32:0
;; run: lts_ltu 2147483648 0 1 0 => i32:1
;; run: lts_ltu 1 0 2147483648 0 => i32:0
;; run: lts_ltu 7 2147483648 7 1 => i32:0
;; run: lts_ltu 7 1 7 2147483648 => i32:1
;; run: lts_ltu 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: lts_ltu 0 4294967295 1 0 => i32:1
;; run: lts_ltu 1 0 0 4294967295 => i32:0
;; run: lts_ltu 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: lts_ltu 305419896 2596069104 305419896 2596069104 => i32:0
;; run: lts_ltu 305419896 2596069104 2596069104 305419896 => i32:0
;; run: lts_ltu 2596069104 305419896 305419896 2596069104 => i32:1
;; run: lts_leu 1 1 1 1 => i32:1
;; run: lts_leu 1 5 2 3 => i32:1
;; run: lts_leu 2 3 1 5 => i32:0
;; run: lts_leu 1 3 1 5 => i32:1
;; run: lts_leu 1 5 1 3 => i32:0
;; run: lts_leu 2147483648 0 1 0 => i32:1
;; run: lts_leu 1 0 2147483648 0 => i32:0
;; run: lts_leu 7 2147483648 7 1 => i32:0
;; run: lts_leu 7 1 7 2147483648 => i32:1
;; run: lts_leu 4294967295 4294967295 4294967295 4294967295 => i32:1
;; run: lts_leu 0 4294967295 1 0 => i32:1
;; run: lts_leu 1 0 0 4294967295 => i32:0
;; run: lts_leu 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: lts_leu 305419896 2596069104 305419896 2596069104 => i32:1
;; run: lts_leu 305419896 2596069104 2596069104 305419896 => i32:0
;; run: lts_leu 2596069104 305419896 305419896 2596069104 => i32:1
;; run: lts_gtu 1 1 1 1 => i32:0
;; run: lts_gtu 1 5 2 3 => i32:1
;; run: lts_gtu 2 3 1 5 => i32:0
;; run: lts_gtu 1 3 1 5 => i32:0
;; run: lts_gtu 1 5 1 3 => i32:1
;; run: lts_gtu 2147483648 0 1 0 => i32:1
;; run: lts_gtu 1 0 2147483648 0 => i32:0
;; run: lts_gtu 7 2147483648 7 1 => i32:1
;; run: lts_gtu 7 1 7 2147483648 => i32:0
;; run: lts_gtu 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: lts_gtu 0 4294967295 1 0 => i32:1
;; run: lts_gtu 1 0 0 4294967295 => i32:0
;; run: lts_gtu 2147483648 2147483648 2147483648 2147483647 => i32:1
;; run: lts_gtu 305419896 2596069104 305419896 2596069104 => i32:0
;; run: lts_gtu 305419896 2596069104 2596069104 305419896 => i32:0
;; run: lts_gtu 2596069104 305419896 305419896 2596069104 => i32:1
;; run: lts_geu 1 1 1 1 => i32:1
;; run: lts_geu 1 5 2 3 => i32:1
;; run: lts_geu 2 3 1 5 => i32:0
;; run: lts_geu 1 3 1 5 => i32:0
;; run: lts_geu 1 5 1 3 => i32:1
;; run: lts_geu 2147483648 0 1 0 => i32:1
;; run: lts_geu 1 0 2147483648 0 => i32:0
;; run: lts_geu 7 2147483648 7 1 => i32:1
;; run: lts_geu 7 1 7 2147483648 => i32:0
;; run: lts_geu 4294967295 4294967295 4294967295 4294967295 => i32:1
;; run: lts_geu 0 4294967295 1 0 => i32:1
;; run: lts_geu 1 0 0 4294967295 => i32:0
;; run: lts_geu 2147483648 2147483648 2147483648 2147483647 => i32:1
;; run: lts_geu 305419896 2596069104 305419896 2596069104 => i32:1
;; run: lts_geu 305419896 2596069104 2596069104 305419896 => i32:0
;; run: lts_geu 2596069104 305419896 305419896 2596069104 => i32:1
;; run: les_ltu 1 1 1 1 => i32:0
;; run: les_ltu 1 5 2 3 => i32:1
;; run: les_ltu 2 3 1 5 => i32:0
;; run: les_ltu 1 3 1 5 => i32:1
;; run: les_ltu 1 5 1 3 => i32:0
;; run: les_ltu 2147483648 0 1 0 => i32:1
;; run: les_ltu 1 0 2147483648 0 => i32:0
;; run: les_ltu 7 2147483648 7 1 => i32:0
;; run: les_ltu 7 1 7 2147483648 => i32:1
;; run: les_ltu 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: les_ltu 0 4294967295 1 0 => i32:1
;; run: les_ltu 1 0 0 4294967295 => i32:0
;; run: les_ltu 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: les_ltu 305419896 2596069104 305419896 2596069104 => i32:0
;; run: les_ltu 305419896 2596069104 2596069104 305419896 => i32:0
;; run: les_ltu 2596069104 305419896 305419896 2596069104 => i32:1
;; run: les_leu 1 1 1 1 => i32:1
;; run: les_leu 1 5 2 3 => i32:1
;; run: les_leu 2 3 1 5 => i32:0
;; run: les_leu 1 3 1 5 => i32:1
;; run: les_leu 1 5 1 3 => i32:0
;; run: les_leu 2147483648 0 1 0 => i32:1
;; run: les_leu 1 0 2147483648 0 => i32:0
;; run: les_leu 7 2147483648 7 1 => i32:0
;; run: les_leu 7 1 7 2147483648 => i32:1
;; run: les_leu 4294967295 4294967295 4294967295 4294967295 => i32:1
;; run: les_leu 0 4294967295 1 0 => i32:1
;; run: les_leu 1 0 0 4294967295 => i32:0
;; run: les_leu 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: les_leu 305419896 2596069104 305419896 2596069104 => i32:1
;; run: les_leu 305419896 2596069104 2596069104 305419896 => i32:0
;; run: les_leu 2596069104 305419896 305419896 2596069104 => i32:1
;; run: les_gtu 1 1 1 1 => i32:0
;; run: les_gtu 1 5 2 3 => i32:1
;; run: les_gtu 2 3 1 5 => i32:0
;; run: les_gtu 1 3 1 5 => i32:0
;; run: les_gtu 1 5 1 3 => i32:1
;; run: les_gtu 2147483648 0 1 0 => i32:1
;; run: les_gtu 1 0 2147483648 0 => i32:0
;; run: les_gtu 7 2147483648 7 1 => i32:1
;; run: les_gtu 7 1 7 2147483648 => i32:0
;; run: les_gtu 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: les_gtu 0 4294967295 1 0 => i32:1
;; run: les_gtu 1 0 0 4294967295 => i32:0
;; run: les_gtu 2147483648 2147483648 2147483648 2147483647 => i32:1
;; run: les_gtu 305419896 2596069104 305419896 2596069104 => i32:0
;; run: les_gtu 305419896 2596069104 2596069104 305419896 => i32:0
;; run: les_gtu 2596069104 305419896 305419896 2596069104 => i32:1
;; run: les_geu 1 1 1 1 => i32:1
;; run: les_geu 1 5 2 3 => i32:1
;; run: les_geu 2 3 1 5 => i32:0
;; run: les_geu 1 3 1 5 => i32:0
;; run: les_geu 1 5 1 3 => i32:1
;; run: les_geu 2147483648 0 1 0 => i32:1
;; run: les_geu 1 0 2147483648 0 => i32:0
;; run: les_geu 7 2147483648 7 1 => i32:1
;; run: les_geu 7 1 7 2147483648 => i32:0
;; run: les_geu 4294967295 4294967295 4294967295 4294967295 => i32:1
;; run: les_geu 0 4294967295 1 0 => i32:1
;; run: les_geu 1 0 0 4294967295 => i32:0
;; run: les_geu 2147483648 2147483648 2147483648 2147483647 => i32:1
;; run: les_geu 305419896 2596069104 305419896 2596069104 => i32:1
;; run: les_geu 305419896 2596069104 2596069104 305419896 => i32:0
;; run: les_geu 2596069104 305419896 305419896 2596069104 => i32:1
;; run: gts_ltu 1 1 1 1 => i32:0
;; run: gts_ltu 1 5 2 3 => i32:0
;; run: gts_ltu 2 3 1 5 => i32:1
;; run: gts_ltu 1 3 1 5 => i32:1
;; run: gts_ltu 1 5 1 3 => i32:0
;; run: gts_ltu 2147483648 0 1 0 => i32:0
;; run: gts_ltu 1 0 2147483648 0 => i32:1
;; run: gts_ltu 7 2147483648 7 1 => i32:0
;; run: gts_ltu 7 1 7 2147483648 => i32:1
;; run: gts_ltu 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: gts_ltu 0 4294967295 1 0 => i32:0
;; run: gts_ltu 1 0 0 4294967295 => i32:1
;; run: gts_ltu 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: gts_ltu 305419896 2596069104 305419896 2596069104 => i32:0
;; run: gts_ltu 305419896 2596069104 2596069104 305419896 => i32:1
;; run: gts_ltu 2596069104 305419896 305419896 2596069104 => i32:0
;; run: gts_leu 1 1 1 1 => i32:1
;; run: gts_leu 1 5 2 3 => i32:0
;; run: gts_leu 2 3 1 5 => i32:1
;; run: gts_leu 1 3 1 5 => i32:1
;; run: gts_leu 1 5 1 3 => i32:0
;; run: gts_leu 2147483648 0 1 0 => i32:0
;; run: gts_leu 1 0 2147483648 0 => i32:1
;; run: gts_leu 7 2147483648 7 1 => i32:0
;; run: gts_leu 7 1 7 2147483648 => i32:1
;; run: gts_leu 4294967295 4294967295 4294967295 4294967295 => i32:1
;; run: gts_leu 0 4294967295 1 0 => i32:0
;; run: gts_leu 1 0 0 4294967295 => i32:1
;; run: gts_leu 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: gts_leu 305419896 2596069104 305419896 2596069104 => i32:1
;; run: gts_leu 305419896 2596069104 2596069104 305419896 => i32:1
;; run: gts_leu 2596069104 305419896 305419896 2596069104 => i32:0
;; run: gts_gtu 1 1 1 1 => i32:0
;; run: gts_gtu 1 5 2 3 => i32:0
;; run: gts_gtu 2 3 1 5 => i32:1
;; run: gts_gtu 1 3 1 5 => i32:0
;; run: gts_gtu 1 5 1 3 => i32:1
;; run: gts_gtu 2147483648 0 1 0 => i32:0
;; run: gts_gtu 1 0 2147483648 0 => i32:1
;; run: gts_gtu 7 2147483648 7 1 => i32:1
;; run: gts_gtu 7 1 7 2147483648 => i32:0
;; run: gts_gtu 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: gts_gtu 0 4294967295 1 0 => i32:0
;; run: gts_gtu 1 0 0 4294967295 => i32:1
;; run: gts_gtu 2147483648 2147483648 2147483648 2147483647 => i32:1
;; run: gts_gtu 305419896 2596069104 305419896 2596069104 => i32:0
;; run: gts_gtu 305419896 2596069104 2596069104 305419896 => i32:1
;; run: gts_gtu 2596069104 305419896 305419896 2596069104 => i32:0
;; run: gts_geu 1 1 1 1 => i32:1
;; run: gts_geu 1 5 2 3 => i32:0
;; run: gts_geu 2 3 1 5 => i32:1
;; run: gts_geu 1 3 1 5 => i32:0
;; run: gts_geu 1 5 1 3 => i32:1
;; run: gts_geu 2147483648 0 1 0 => i32:0
;; run: gts_geu 1 0 2147483648 0 => i32:1
;; run: gts_geu 7 2147483648 7 1 => i32:1
;; run: gts_geu 7 1 7 2147483648 => i32:0
;; run: gts_geu 4294967295 4294967295 4294967295 4294967295 => i32:1
;; run: gts_geu 0 4294967295 1 0 => i32:0
;; run: gts_geu 1 0 0 4294967295 => i32:1
;; run: gts_geu 2147483648 2147483648 2147483648 2147483647 => i32:1
;; run: gts_geu 305419896 2596069104 305419896 2596069104 => i32:1
;; run: gts_geu 305419896 2596069104 2596069104 305419896 => i32:1
;; run: gts_geu 2596069104 305419896 305419896 2596069104 => i32:0
;; run: ges_ltu 1 1 1 1 => i32:0
;; run: ges_ltu 1 5 2 3 => i32:0
;; run: ges_ltu 2 3 1 5 => i32:1
;; run: ges_ltu 1 3 1 5 => i32:1
;; run: ges_ltu 1 5 1 3 => i32:0
;; run: ges_ltu 2147483648 0 1 0 => i32:0
;; run: ges_ltu 1 0 2147483648 0 => i32:1
;; run: ges_ltu 7 2147483648 7 1 => i32:0
;; run: ges_ltu 7 1 7 2147483648 => i32:1
;; run: ges_ltu 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: ges_ltu 0 4294967295 1 0 => i32:0
;; run: ges_ltu 1 0 0 4294967295 => i32:1
;; run: ges_ltu 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: ges_ltu 305419896 2596069104 305419896 2596069104 => i32:0
;; run: ges_ltu 305419896 2596069104 2596069104 305419896 => i32:1
;; run: ges_ltu 2596069104 305419896 305419896 2596069104 => i32:0
;; run: ges_leu 1 1 1 1 => i32:1
;; run: ges_leu 1 5 2 3 => i32:0
;; run: ges_leu 2 3 1 5 => i32:1
;; run: ges_leu 1 3 1 5 => i32:1
;; run: ges_leu 1 5 1 3 => i32:0
;; run: ges_leu 2147483648 0 1 0 => i32:0
;; run: ges_leu 1 0 2147483648 0 => i32:1
;; run: ges_leu 7 2147483648 7 1 => i32:0
;; run: ges_leu 7 1 7 2147483648 => i32:1
;; run: ges_leu 4294967295 4294967295 4294967295 4294967295 => i32:1
;; run: ges_leu 0 4294967295 1 0 => i32:0
;; run: ges_leu 1 0 0 4294967295 => i32:1
;; run: ges_leu 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: ges_leu 305419896 2596069104 305419896 2596069104 => i32:1
;; run: ges_leu 305419896 2596069104 2596069104 305419896 => i32:1
;; run: ges_leu 2596069104 305419896 305419896 2596069104 => i32:0
;; run: ges_gtu 1 1 1 1 => i32:0
;; run: ges_gtu 1 5 2 3 => i32:0
;; run: ges_gtu 2 3 1 5 => i32:1
;; run: ges_gtu 1 3 1 5 => i32:0
;; run: ges_gtu 1 5 1 3 => i32:1
;; run: ges_gtu 2147483648 0 1 0 => i32:0
;; run: ges_gtu 1 0 2147483648 0 => i32:1
;; run: ges_gtu 7 2147483648 7 1 => i32:1
;; run: ges_gtu 7 1 7 2147483648 => i32:0
;; run: ges_gtu 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: ges_gtu 0 4294967295 1 0 => i32:0
;; run: ges_gtu 1 0 0 4294967295 => i32:1
;; run: ges_gtu 2147483648 2147483648 2147483648 2147483647 => i32:1
;; run: ges_gtu 305419896 2596069104 305419896 2596069104 => i32:0
;; run: ges_gtu 305419896 2596069104 2596069104 305419896 => i32:1
;; run: ges_gtu 2596069104 305419896 305419896 2596069104 => i32:0
;; run: ges_geu 1 1 1 1 => i32:1
;; run: ges_geu 1 5 2 3 => i32:0
;; run: ges_geu 2 3 1 5 => i32:1
;; run: ges_geu 1 3 1 5 => i32:0
;; run: ges_geu 1 5 1 3 => i32:1
;; run: ges_geu 2147483648 0 1 0 => i32:0
;; run: ges_geu 1 0 2147483648 0 => i32:1
;; run: ges_geu 7 2147483648 7 1 => i32:1
;; run: ges_geu 7 1 7 2147483648 => i32:0
;; run: ges_geu 4294967295 4294967295 4294967295 4294967295 => i32:1
;; run: ges_geu 0 4294967295 1 0 => i32:0
;; run: ges_geu 1 0 0 4294967295 => i32:1
;; run: ges_geu 2147483648 2147483648 2147483648 2147483647 => i32:1
;; run: ges_geu 305419896 2596069104 305419896 2596069104 => i32:1
;; run: ges_geu 305419896 2596069104 2596069104 305419896 => i32:1
;; run: ges_geu 2596069104 305419896 305419896 2596069104 => i32:0
;; run: ne 1 1 1 1 => i32:1
;; run: ne 1 5 2 3 => i32:0
;; run: ne 2 3 1 5 => i32:1
;; run: ne 1 3 1 5 => i32:1
;; run: ne 1 5 1 3 => i32:0
;; run: ne 2147483648 0 1 0 => i32:0
;; run: ne 1 0 2147483648 0 => i32:1
;; run: ne 7 2147483648 7 1 => i32:0
;; run: ne 7 1 7 2147483648 => i32:1
;; run: ne 4294967295 4294967295 4294967295 4294967295 => i32:1
;; run: ne 0 4294967295 1 0 => i32:0
;; run: ne 1 0 0 4294967295 => i32:1
;; run: ne 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: ne 305419896 2596069104 305419896 2596069104 => i32:1
;; run: ne 305419896 2596069104 2596069104 305419896 => i32:1
;; run: ne 2596069104 305419896 305419896 2596069104 => i32:0
;; run: turned 1 1 1 1 => i32:0
;; run: turned 1 5 2 3 => i32:1
;; run: turned 2 3 1 5 => i32:0
;; run: turned 1 3 1 5 => i32:1
;; run: turned 1 5 1 3 => i32:0
;; run: turned 2147483648 0 1 0 => i32:0
;; run: turned 1 0 2147483648 0 => i32:1
;; run: turned 7 2147483648 7 1 => i32:0
;; run: turned 7 1 7 2147483648 => i32:1
;; run: turned 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: turned 0 4294967295 1 0 => i32:1
;; run: turned 1 0 0 4294967295 => i32:0
;; run: turned 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: turned 305419896 2596069104 305419896 2596069104 => i32:0
;; run: turned 305419896 2596069104 2596069104 305419896 => i32:1
;; run: turned 2596069104 305419896 305419896 2596069104 => i32:0
;; run: wide 4294967297 4294967297 4294967297 4294967297 => i32:200
;; run: wide 4294967297 21474836485 8589934594 12884901891 => i32:201
;; run: wide 8589934594 12884901891 4294967297 21474836485 => i32:200
;; run: wide 4294967297 12884901891 4294967297 21474836485 => i32:201
;; run: wide 4294967297 21474836485 4294967297 12884901891 => i32:200
;; run: wide 18446744071562067968 0 4294967297 0 => i32:201
;; run: wide 4294967297 0 18446744071562067968 0 => i32:200
;; run: wide 30064771079 18446744071562067968 30064771079 4294967297 => i32:200
;; run: wide 30064771079 4294967297 30064771079 18446744071562067968 => i32:201
;; run: wide 9223372041149743103 9223372041149743103 9223372041149743103 9223372041149743103 => i32:200
;; run: wide 0 9223372041149743103 4294967297 0 => i32:201
;; run: wide 4294967297 0 0 9223372041149743103 => i32:200
;; run: wide 18446744071562067968 18446744071562067968 18446744071562067968 9223372034707292159 => i32:200
;; run: wide 1311768465173141112 16520084209029406448 1311768465173141112 16520084209029406448 => i32:200
;; run: wide 1311768465173141112 16520084209029406448 16520084209029406448 1311768465173141112 => i32:200
;; run: wide 16520084209029406448 1311768465173141112 1311768465173141112 16520084209029406448 => i32:201
;; run: wide 9223372036854775808 0 1 0 => i32:201
;; run: wide 1 0 9223372036854775808 0 => i32:200
;; run: wide 7 9223372036854775808 7 1 => i32:200
;; run: wide 7 1 7 9223372036854775808 => i32:201
;; run: wide 18446744073709551615 18446744073709551615 18446744073709551615 18446744073709551614 => i32:200
;; run: wideu 4294967297 4294967297 4294967297 4294967297 => i32:1
;; run: wideu 4294967297 21474836485 8589934594 12884901891 => i32:0
;; run: wideu 8589934594 12884901891 4294967297 21474836485 => i32:1
;; run: wideu 4294967297 12884901891 4294967297 21474836485 => i32:0
;; run: wideu 4294967297 21474836485 4294967297 12884901891 => i32:1
;; run: wideu 18446744071562067968 0 4294967297 0 => i32:1
;; run: wideu 4294967297 0 18446744071562067968 0 => i32:0
;; run: wideu 30064771079 18446744071562067968 30064771079 4294967297 => i32:1
;; run: wideu 30064771079 4294967297 30064771079 18446744071562067968 => i32:0
;; run: wideu 9223372041149743103 9223372041149743103 9223372041149743103 9223372041149743103 => i32:1
;; run: wideu 0 9223372041149743103 4294967297 0 => i32:0
;; run: wideu 4294967297 0 0 9223372041149743103 => i32:1
;; run: wideu 18446744071562067968 18446744071562067968 18446744071562067968 9223372034707292159 => i32:1
;; run: wideu 1311768465173141112 16520084209029406448 1311768465173141112 16520084209029406448 => i32:1
;; run: wideu 1311768465173141112 16520084209029406448 16520084209029406448 1311768465173141112 => i32:0
;; run: wideu 16520084209029406448 1311768465173141112 1311768465173141112 16520084209029406448 => i32:1
;; run: wideu 9223372036854775808 0 1 0 => i32:1
;; run: wideu 1 0 9223372036854775808 0 => i32:0
;; run: wideu 7 9223372036854775808 7 1 => i32:1
;; run: wideu 7 1 7 9223372036854775808 => i32:0
;; run: wideu 18446744073709551615 18446744073709551615 18446744073709551615 18446744073709551614 => i32:1
;; run: m64n32 4294967297 1 4294967297 1 => i32:301
;; run: m64n32 4294967297 5 8589934594 3 => i32:301
;; run: m64n32 8589934594 3 4294967297 5 => i32:300
;; run: m64n32 4294967297 3 4294967297 5 => i32:301
;; run: m64n32 4294967297 5 4294967297 3 => i32:300
;; run: m64n32 9223372039002259456 0 4294967297 0 => i32:301
;; run: m64n32 4294967297 0 9223372039002259456 0 => i32:300
;; run: m64n32 30064771079 2147483648 30064771079 1 => i32:300
;; run: m64n32 30064771079 1 30064771079 2147483648 => i32:301
;; run: m64n32 18446744073709551615 4294967295 18446744073709551615 4294967295 => i32:301
;; run: m64n32 0 4294967295 4294967297 0 => i32:301
;; run: m64n32 4294967297 0 0 4294967295 => i32:300
;; run: m64n32 9223372039002259456 2147483648 9223372039002259456 2147483647 => i32:300
;; run: m64n32 1311768465173141112 2596069104 1311768465173141112 2596069104 => i32:301
;; run: m64n32 1311768465173141112 2596069104 11150031902432091888 305419896 => i32:300
;; run: m64n32 11150031902432091888 305419896 1311768465173141112 2596069104 => i32:301
;; run: m64n32 9223372036854775808 5 1 5 => i32:301
;; run: m64n32 4294967296 1 0 2 => i32:300
;; run: m32n64 1 4294967297 1 4294967297 => i32:400
;; run: m32n64 1 21474836485 2 12884901891 => i32:400
;; run: m32n64 2 12884901891 1 21474836485 => i32:401
;; run: m32n64 1 12884901891 1 21474836485 => i32:400
;; run: m32n64 1 21474836485 1 12884901891 => i32:401
;; run: m32n64 2147483648 0 1 0 => i32:401
;; run: m32n64 1 0 2147483648 0 => i32:400
;; run: m32n64 7 9223372039002259456 7 4294967297 => i32:401
;; run: m32n64 7 4294967297 7 9223372039002259456 => i32:400
;; run: m32n64 4294967295 18446744073709551615 4294967295 18446744073709551615 => i32:400
;; run: m32n64 0 18446744073709551615 1 0 => i32:400
;; run: m32n64 1 0 0 18446744073709551615 => i32:401
;; run: m32n64 2147483648 9223372039002259456 2147483648 9223372034707292159 => i32:401
;; run: m32n64 305419896 11150031902432091888 305419896 11150031902432091888 => i32:400
;; run: m32n64 305419896 11150031902432091888 2596069104 1311768465173141112 => i32:400
;; run: m32n64 2596069104 1311768465173141112 305419896 11150031902432091888 => i32:401
;; run: m32n64 5 9223372036854775808 5 1 => i32:401
;; run: m32n64 5 4294967296 5 2 => i32:401
;; run: nsigned 1 1 1 1 => i32:0
;; run: nsigned 1 5 2 3 => i32:1
;; run: nsigned 2 3 1 5 => i32:0
;; run: nsigned 1 3 1 5 => i32:1
;; run: nsigned 1 5 1 3 => i32:0
;; run: nsigned 2147483648 0 1 0 => i32:0
;; run: nsigned 1 0 2147483648 0 => i32:1
;; run: nsigned 7 2147483648 7 1 => i32:1
;; run: nsigned 7 1 7 2147483648 => i32:0
;; run: nsigned 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: nsigned 0 4294967295 1 0 => i32:1
;; run: nsigned 1 0 0 4294967295 => i32:0
;; run: nsigned 2147483648 2147483648 2147483648 2147483647 => i32:1
;; run: nsigned 305419896 2596069104 305419896 2596069104 => i32:0
;; run: nsigned 305419896 2596069104 2596069104 305419896 => i32:1
;; run: nsigned 2596069104 305419896 305419896 2596069104 => i32:0
;; run: npos 1 1 1 1 => i32:0
;; run: npos 1 5 2 3 => i32:1
;; run: npos 2 3 1 5 => i32:0
;; run: npos 1 3 1 5 => i32:1
;; run: npos 1 5 1 3 => i32:0
;; run: npos 2147483648 0 1 0 => i32:0
;; run: npos 1 0 2147483648 0 => i32:1
;; run: npos 7 2147483648 7 1 => i32:1
;; run: npos 7 1 7 2147483648 => i32:0
;; run: npos 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: npos 0 4294967295 1 0 => i32:1
;; run: npos 1 0 0 4294967295 => i32:0
;; run: npos 2147483648 2147483648 2147483648 2147483647 => i32:1
;; run: npos 305419896 2596069104 305419896 2596069104 => i32:0
;; run: npos 305419896 2596069104 2596069104 305419896 => i32:1
;; run: npos 2596069104 305419896 305419896 2596069104 => i32:0
;; run: constn 1 1 1 1 => i32:501
;; run: constn 1 5 2 3 => i32:501
;; run: constn 2 3 1 5 => i32:500
;; run: constn 1 3 1 5 => i32:501
;; run: constn 1 5 1 3 => i32:501
;; run: constn 2147483648 0 1 0 => i32:500
;; run: constn 1 0 2147483648 0 => i32:501
;; run: constn 7 2147483648 7 1 => i32:500
;; run: constn 7 1 7 2147483648 => i32:501
;; run: constn 4294967295 4294967295 4294967295 4294967295 => i32:500
;; run: constn 0 4294967295 1 0 => i32:501
;; run: constn 1 0 0 4294967295 => i32:500
;; run: constn 2147483648 2147483648 2147483648 2147483647 => i32:500
;; run: constn 305419896 2596069104 305419896 2596069104 => i32:500
;; run: constn 305419896 2596069104 2596069104 305419896 => i32:501
;; run: constn 2596069104 305419896 305419896 2596069104 => i32:500
;; run: constn 5 99 5 0 => i32:501
;; run: constn 5 100 5 0 => i32:500
;; run: constn 5 101 5 0 => i32:500
;; run: constn 7 0 9 0 => i32:501
;; run: constn 7 0 7 0 => i32:501
;; run: constn 8 0 7 0 => i32:500
;; run: constnle 1 1 1 1 => i32:601
;; run: constnle 1 5 2 3 => i32:601
;; run: constnle 2 3 1 5 => i32:600
;; run: constnle 1 3 1 5 => i32:601
;; run: constnle 1 5 1 3 => i32:601
;; run: constnle 2147483648 0 1 0 => i32:600
;; run: constnle 1 0 2147483648 0 => i32:601
;; run: constnle 7 2147483648 7 1 => i32:600
;; run: constnle 7 1 7 2147483648 => i32:601
;; run: constnle 4294967295 4294967295 4294967295 4294967295 => i32:600
;; run: constnle 0 4294967295 1 0 => i32:601
;; run: constnle 1 0 0 4294967295 => i32:600
;; run: constnle 2147483648 2147483648 2147483648 2147483647 => i32:600
;; run: constnle 305419896 2596069104 305419896 2596069104 => i32:600
;; run: constnle 305419896 2596069104 2596069104 305419896 => i32:601
;; run: constnle 2596069104 305419896 305419896 2596069104 => i32:600
;; run: constnle 5 99 5 0 => i32:601
;; run: constnle 5 100 5 0 => i32:601
;; run: constnle 5 101 5 0 => i32:600
;; run: constnle 7 0 9 0 => i32:601
;; run: constnle 7 0 7 0 => i32:601
;; run: constnle 8 0 7 0 => i32:600
;; run: constneg 1 1 1 1 => i32:701
;; run: constneg 1 5 2 3 => i32:701
;; run: constneg 2 3 1 5 => i32:700
;; run: constneg 1 3 1 5 => i32:701
;; run: constneg 1 5 1 3 => i32:701
;; run: constneg 2147483648 0 1 0 => i32:700
;; run: constneg 1 0 2147483648 0 => i32:701
;; run: constneg 7 2147483648 7 1 => i32:701
;; run: constneg 7 1 7 2147483648 => i32:701
;; run: constneg 4294967295 4294967295 4294967295 4294967295 => i32:700
;; run: constneg 0 4294967295 1 0 => i32:701
;; run: constneg 1 0 0 4294967295 => i32:700
;; run: constneg 2147483648 2147483648 2147483648 2147483647 => i32:701
;; run: constneg 305419896 2596069104 305419896 2596069104 => i32:701
;; run: constneg 305419896 2596069104 2596069104 305419896 => i32:701
;; run: constneg 2596069104 305419896 305419896 2596069104 => i32:700
;; run: constneg 5 99 5 0 => i32:701
;; run: constneg 5 100 5 0 => i32:701
;; run: constneg 5 101 5 0 => i32:701
;; run: constneg 7 0 9 0 => i32:701
;; run: constneg 7 0 7 0 => i32:701
;; run: constneg 8 0 7 0 => i32:700
;; run: constneg 5 4294967291 5 0 => i32:700
;; run: constneg 5 4294967290 5 0 => i32:701
;; run: constneg 5 4294967292 5 0 => i32:700
;; run: constneg 5 3 5 0 => i32:701
;; run: constm 1 1 1 1 => i32:1
;; run: constm 1 5 2 3 => i32:1
;; run: constm 2 3 1 5 => i32:1
;; run: constm 1 3 1 5 => i32:1
;; run: constm 1 5 1 3 => i32:1
;; run: constm 2147483648 0 1 0 => i32:1
;; run: constm 1 0 2147483648 0 => i32:1
;; run: constm 7 2147483648 7 1 => i32:0
;; run: constm 7 1 7 2147483648 => i32:1
;; run: constm 4294967295 4294967295 4294967295 4294967295 => i32:1
;; run: constm 0 4294967295 1 0 => i32:1
;; run: constm 1 0 0 4294967295 => i32:1
;; run: constm 2147483648 2147483648 2147483648 2147483647 => i32:1
;; run: constm 305419896 2596069104 305419896 2596069104 => i32:0
;; run: constm 305419896 2596069104 2596069104 305419896 => i32:0
;; run: constm 2596069104 305419896 305419896 2596069104 => i32:1
;; run: constm 5 99 5 0 => i32:1
;; run: constm 5 100 5 0 => i32:1
;; run: constm 5 101 5 0 => i32:1
;; run: constm 7 0 9 0 => i32:0
;; run: constm 7 0 7 0 => i32:0
;; run: constm 8 0 7 0 => i32:0
;; run: constm 7 1 0 2 => i32:1
;; run: constm 7 2 0 1 => i32:0
;; run: constm 4294967287 0 0 0 => i32:1
;; run: usesm 1 1 1 1 => i32:0
;; run: usesm 1 5 2 3 => i32:2
;; run: usesm 2 3 1 5 => i32:0
;; run: usesm 1 3 1 5 => i32:1
;; run: usesm 1 5 1 3 => i32:0
;; run: usesm 2147483648 0 1 0 => i32:0
;; run: usesm 1 0 2147483648 0 => i32:2
;; run: usesm 7 2147483648 7 1 => i32:0
;; run: usesm 7 1 7 2147483648 => i32:1
;; run: usesm 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: usesm 0 4294967295 1 0 => i32:2
;; run: usesm 1 0 0 4294967295 => i32:0
;; run: usesm 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: usesm 305419896 2596069104 305419896 2596069104 => i32:0
;; run: usesm 305419896 2596069104 2596069104 305419896 => i32:2
;; run: usesm 2596069104 305419896 305419896 2596069104 => i32:0
;; run: usesn 1 1 1 1 => i32:0
;; run: usesn 1 5 2 3 => i32:1
;; run: usesn 2 3 1 5 => i32:1
;; run: usesn 1 3 1 5 => i32:2
;; run: usesn 1 5 1 3 => i32:0
;; run: usesn 2147483648 0 1 0 => i32:0
;; run: usesn 1 0 2147483648 0 => i32:1
;; run: usesn 7 2147483648 7 1 => i32:0
;; run: usesn 7 1 7 2147483648 => i32:2
;; run: usesn 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: usesn 0 4294967295 1 0 => i32:1
;; run: usesn 1 0 0 4294967295 => i32:1
;; run: usesn 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: usesn 305419896 2596069104 305419896 2596069104 => i32:0
;; run: usesn 305419896 2596069104 2596069104 305419896 => i32:1
;; run: usesn 2596069104 305419896 305419896 2596069104 => i32:1
;; run: usesc 1 1 1 1 => i32:1
;; run: usesc 1 5 2 3 => i32:1
;; run: usesc 2 3 1 5 => i32:0
;; run: usesc 1 3 1 5 => i32:2
;; run: usesc 1 5 1 3 => i32:1
;; run: usesc 2147483648 0 1 0 => i32:0
;; run: usesc 1 0 2147483648 0 => i32:1
;; run: usesc 7 2147483648 7 1 => i32:1
;; run: usesc 7 1 7 2147483648 => i32:2
;; run: usesc 4294967295 4294967295 4294967295 4294967295 => i32:1
;; run: usesc 0 4294967295 1 0 => i32:1
;; run: usesc 1 0 0 4294967295 => i32:0
;; run: usesc 2147483648 2147483648 2147483648 2147483647 => i32:1
;; run: usesc 305419896 2596069104 305419896 2596069104 => i32:1
;; run: usesc 305419896 2596069104 2596069104 305419896 => i32:1
;; run: usesc 2596069104 305419896 305419896 2596069104 => i32:0
;; run: twice 1 1 1 1 => i32:16
;; run: twice 1 5 2 3 => i32:16
;; run: twice 2 3 1 5 => i32:2
;; run: twice 1 3 1 5 => i32:1
;; run: twice 1 5 1 3 => i32:16
;; run: twice 2147483648 0 1 0 => i32:-2147483648
;; run: twice 1 0 2147483648 0 => i32:16
;; run: twice 7 2147483648 7 1 => i32:22
;; run: twice 7 1 7 2147483648 => i32:7
;; run: twice 4294967295 4294967295 4294967295 4294967295 => i32:14
;; run: twice 0 4294967295 1 0 => i32:17
;; run: twice 1 0 0 4294967295 => i32:1
;; run: twice 2147483648 2147483648 2147483648 2147483647 => i32:-2147483631
;; run: twice 305419896 2596069104 305419896 2596069104 => i32:305419913
;; run: twice 305419896 2596069104 2596069104 305419896 => i32:305419913
;; run: twice 2596069104 305419896 305419896 2596069104 => i32:-1698898192
;; run: apart 1 1 1 1 => i32:0
;; run: apart 1 5 2 3 => i32:1
;; run: apart 2 3 1 5 => i32:0
;; run: apart 1 3 1 5 => i32:1
;; run: apart 1 5 1 3 => i32:0
;; run: apart 2147483648 0 1 0 => i32:0
;; run: apart 1 0 2147483648 0 => i32:1
;; run: apart 7 2147483648 7 1 => i32:0
;; run: apart 7 1 7 2147483648 => i32:1
;; run: apart 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: apart 0 4294967295 1 0 => i32:1
;; run: apart 1 0 0 4294967295 => i32:0
;; run: apart 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: apart 305419896 2596069104 305419896 2596069104 => i32:0
;; run: apart 305419896 2596069104 2596069104 305419896 => i32:1
;; run: apart 2596069104 305419896 305419896 2596069104 => i32:0
;; run: pick 1 1 1 1 => i32:1
;; run: pick 1 5 2 3 => i32:3
;; run: pick 2 3 1 5 => i32:3
;; run: pick 1 3 1 5 => i32:3
;; run: pick 1 5 1 3 => i32:3
;; run: pick 2147483648 0 1 0 => i32:0
;; run: pick 1 0 2147483648 0 => i32:0
;; run: pick 7 2147483648 7 1 => i32:1
;; run: pick 7 1 7 2147483648 => i32:1
;; run: pick 4294967295 4294967295 4294967295 4294967295 => i32:-1
;; run: pick 0 4294967295 1 0 => i32:0
;; run: pick 1 0 0 4294967295 => i32:0
;; run: pick 2147483648 2147483648 2147483648 2147483647 => i32:2147483647
;; run: pick 305419896 2596069104 305419896 2596069104 => i32:-1698898192
;; run: pick 305419896 2596069104 2596069104 305419896 => i32:-1698898192
;; run: pick 2596069104 305419896 305419896 2596069104 => i32:-1698898192
;; run: picks 1 1 1 1 => i32:0
;; run: picks 1 5 2 3 => i32:3
;; run: picks 2 3 1 5 => i32:3
;; run: picks 1 3 1 5 => i32:2
;; run: picks 1 5 1 3 => i32:2
;; run: picks 2147483648 0 1 0 => i32:-2147483648
;; run: picks 1 0 2147483648 0 => i32:-2147483648
;; run: picks 7 2147483648 7 1 => i32:-6
;; run: picks 7 1 7 2147483648 => i32:-6
;; run: picks 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: picks 0 4294967295 1 0 => i32:-2
;; run: picks 1 0 0 4294967295 => i32:-2
;; run: picks 2147483648 2147483648 2147483648 2147483647 => i32:-1
;; run: picks 305419896 2596069104 305419896 2596069104 => i32:-2004318088
;; run: picks 305419896 2596069104 2596069104 305419896 => i32:0
;; run: picks 2596069104 305419896 305419896 2596069104 => i32:0
;; run: branch 1 1 1 1 => i32:7
;; run: branch 1 5 2 3 => i32:9
;; run: branch 2 3 1 5 => i32:7
;; run: branch 1 3 1 5 => i32:9
;; run: branch 1 5 1 3 => i32:7
;; run: branch 2147483648 0 1 0 => i32:7
;; run: branch 1 0 2147483648 0 => i32:9
;; run: branch 7 2147483648 7 1 => i32:7
;; run: branch 7 1 7 2147483648 => i32:9
;; run: branch 4294967295 4294967295 4294967295 4294967295 => i32:7
;; run: branch 0 4294967295 1 0 => i32:9
;; run: branch 1 0 0 4294967295 => i32:7
;; run: branch 2147483648 2147483648 2147483648 2147483647 => i32:7
;; run: branch 305419896 2596069104 305419896 2596069104 => i32:7
;; run: branch 305419896 2596069104 2596069104 305419896 => i32:9
;; run: branch 2596069104 305419896 305419896 2596069104 => i32:7
;; run: andcmp 1 1 1 1 => i32:1
;; run: andcmp 1 5 2 3 => i32:1
;; run: andcmp 2 3 1 5 => i32:5
;; run: andcmp 1 3 1 5 => i32:1
;; run: andcmp 1 5 1 3 => i32:3
;; run: andcmp 2147483648 0 1 0 => i32:0
;; run: andcmp 1 0 2147483648 0 => i32:1
;; run: andcmp 7 2147483648 7 1 => i32:1
;; run: andcmp 7 1 7 2147483648 => i32:7
;; run: andcmp 4294967295 4294967295 4294967295 4294967295 => i32:-1
;; run: andcmp 0 4294967295 1 0 => i32:0
;; run: andcmp 1 0 0 4294967295 => i32:-1
;; run: andcmp 2147483648 2147483648 2147483648 2147483647 => i32:2147483647
;; run: andcmp 305419896 2596069104 305419896 2596069104 => i32:-1698898192
;; run: andcmp 305419896 2596069104 2596069104 305419896 => i32:305419896
;; run: andcmp 2596069104 305419896 305419896 2596069104 => i32:-1698898192
;; run: orcmp 1 1 1 1 => i32:7
;; run: orcmp 1 5 2 3 => i32:7
;; run: orcmp 2 3 1 5 => i32:9
;; run: orcmp 1 3 1 5 => i32:7
;; run: orcmp 1 5 1 3 => i32:9
;; run: orcmp 2147483648 0 1 0 => i32:7
;; run: orcmp 1 0 2147483648 0 => i32:9
;; run: orcmp 7 2147483648 7 1 => i32:9
;; run: orcmp 7 1 7 2147483648 => i32:7
;; run: orcmp 4294967295 4294967295 4294967295 4294967295 => i32:7
;; run: orcmp 0 4294967295 1 0 => i32:7
;; run: orcmp 1 0 0 4294967295 => i32:7
;; run: orcmp 2147483648 2147483648 2147483648 2147483647 => i32:7
;; run: orcmp 305419896 2596069104 305419896 2596069104 => i32:7
;; run: orcmp 305419896 2596069104 2596069104 305419896 => i32:7
;; run: orcmp 2596069104 305419896 305419896 2596069104 => i32:7
;; run: rot 4294967297 4294967297 => i32:0
;; run: rot 21474836481 12884901890 => i32:1
;; run: rot 12884901890 21474836481 => i32:0
;; run: rot 12884901889 21474836481 => i32:1
;; run: rot 21474836481 12884901889 => i32:0
;; run: rot 2147483648 1 => i32:0
;; run: rot 1 2147483648 => i32:1
;; run: rot 9223372036854775815 4294967303 => i32:0
;; run: rot 4294967303 9223372036854775815 => i32:1
;; run: rot 18446744073709551615 18446744073709551615 => i32:0
;; run: rot 18446744069414584320 1 => i32:1
;; run: rot 1 18446744069414584320 => i32:0
;; run: rot 9223372039002259456 9223372034707292160 => i32:0
;; run: rot 11150031900141442680 11150031900141442680 => i32:0
;; run: rot 11150031900141442680 1311768467463790320 => i32:1
;; run: rot 1311768467463790320 11150031900141442680 => i32:0
;; run: brs 1 1 1 1 => i32:22
;; run: brs 1 5 2 3 => i32:11
;; run: brs 2 3 1 5 => i32:22
;; run: brs 1 3 1 5 => i32:11
;; run: brs 1 5 1 3 => i32:22
;; run: brs 2147483648 0 1 0 => i32:22
;; run: brs 1 0 2147483648 0 => i32:11
;; run: brs 7 2147483648 7 1 => i32:22
;; run: brs 7 1 7 2147483648 => i32:11
;; run: brs 4294967295 4294967295 4294967295 4294967295 => i32:22
;; run: brs 0 4294967295 1 0 => i32:11
;; run: brs 1 0 0 4294967295 => i32:22
;; run: brs 2147483648 2147483648 2147483648 2147483647 => i32:22
;; run: brs 305419896 2596069104 305419896 2596069104 => i32:22
;; run: brs 305419896 2596069104 2596069104 305419896 => i32:11
;; run: brs 2596069104 305419896 305419896 2596069104 => i32:22
;; run: recs 0 0 => i32:0
;; run: recs 0 3 => i32:0
;; run: recs 0 7 => i32:0
;; run: recs 0 9 => i32:1
;; run: recs 0 15 => i32:0
;; run: recs 1 0 => i32:1
;; run: recs 1 3 => i32:0
;; run: recs 1 7 => i32:0
;; run: recs 1 9 => i32:1
;; run: recs 1 15 => i32:0
;; run: recs 2 0 => i32:1
;; run: recs 2 3 => i32:0
;; run: recs 2 7 => i32:0
;; run: recs 2 9 => i32:1
;; run: recs 2 15 => i32:0
;; run: recs 3 0 => i32:1
;; run: recs 3 3 => i32:0
;; run: recs 3 7 => i32:1
;; run: recs 3 9 => i32:1
;; run: recs 3 15 => i32:1
;; run: recs 4 0 => i32:0
;; run: recs 4 3 => i32:0
;; run: recs 4 7 => i32:0
;; run: recs 4 9 => i32:1
;; run: recs 4 15 => i32:0
;; run: recs 5 0 => i32:1
;; run: recs 5 3 => i32:0
;; run: recs 5 7 => i32:0
;; run: recs 5 9 => i32:1
;; run: recs 5 15 => i32:0
;; run: recs 6 0 => i32:1
;; run: recs 6 3 => i32:0
;; run: recs 6 7 => i32:0
;; run: recs 6 9 => i32:1
;; run: recs 6 15 => i32:0
;; run: recs 7 0 => i32:1
;; run: recs 7 3 => i32:0
;; run: recs 7 7 => i32:0
;; run: recs 7 9 => i32:1
;; run: recs 7 15 => i32:1
;; run: recs 8 0 => i32:0
;; run: recs 8 3 => i32:0
;; run: recs 8 7 => i32:0
;; run: recs 8 9 => i32:1
;; run: recs 8 15 => i32:0
;; run: recs 9 0 => i32:0
;; run: recs 9 3 => i32:0
;; run: recs 9 7 => i32:0
;; run: recs 9 9 => i32:0
;; run: recs 9 15 => i32:0
;; run: recs 10 0 => i32:1
;; run: recs 10 3 => i32:0
;; run: recs 10 7 => i32:0
;; run: recs 10 9 => i32:1
;; run: recs 10 15 => i32:0
;; run: recs 11 0 => i32:1
;; run: recs 11 3 => i32:0
;; run: recs 11 7 => i32:0
;; run: recs 11 9 => i32:1
;; run: recs 11 15 => i32:0
;; run: recs 12 0 => i32:0
;; run: recs 12 3 => i32:0
;; run: recs 12 7 => i32:0
;; run: recs 12 9 => i32:0
;; run: recs 12 15 => i32:0
;; run: recs 13 0 => i32:1
;; run: recs 13 3 => i32:0
;; run: recs 13 7 => i32:0
;; run: recs 13 9 => i32:1
;; run: recs 13 15 => i32:0
;; run: recs 14 0 => i32:1
;; run: recs 14 3 => i32:0
;; run: recs 14 7 => i32:0
;; run: recs 14 9 => i32:1
;; run: recs 14 15 => i32:0
;; run: recs 15 0 => i32:1
;; run: recs 15 3 => i32:0
;; run: recs 15 7 => i32:0
;; run: recs 15 9 => i32:1
;; run: recs 15 15 => i32:0
;; run: sortrec 0 => i32:0
;; run: sortrec 1 => i32:68
;; run: sortrec 2 => i32:874
;; run: sortrec 5 => i32:-2141140794
;; run: sortrec 9 => i32:1718850208
;; run: sortrec 16 => i32:-1233526608

(module
  (type $t1 (func (param i32)))
  (memory 1)
  (table 1 funcref)
  (elem (i32.const 0) $thrower)
  (tag $e)
  (data (i32.const 0) "\05\00\00\00\09\00\00\00\05\00\00\00\03\00\00\00\02\00\00\00\ff\ff\ff\ff\00\00\00\80\01\00\00\00\05\00\00\00\09\00\00\00\00\00\00\00\00\00\00\00\ff\ff\ff\ff\ff\ff\ff\ff\00\00\00\80\00\00\00\80\07\00\00\00\ff\ff\ff\7f\07\00\00\00\00\00\00\80\01\00\00\00\02\00\00\00\02\00\00\00\01\00\00\00\ff\ff\ff\7f\05\00\00\00\03\00\00\00\03\00\00\00\05\00\00\00\04\00\00\00\fe\ff\ff\ff\00\00\00\00")
  ;; called through the table at an index read from memory, which keeps it out of line
  (func $thrower (type $t1) (if (i32.eq (local.get 0) (i32.const 0x7fffff03)) (then (throw $e))))
  (func $four (export "four") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (i32.add (select (i32.lt_u (local.get $n) (local.get $q)) (i32.lt_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))) (i32.const 100)))
  (func $ltu_ltu (export "ltu_ltu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.lt_u (local.get $n) (local.get $q)) (i32.lt_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $ltu_leu (export "ltu_leu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.le_u (local.get $n) (local.get $q)) (i32.lt_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $ltu_gtu (export "ltu_gtu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.gt_u (local.get $n) (local.get $q)) (i32.lt_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $ltu_geu (export "ltu_geu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.ge_u (local.get $n) (local.get $q)) (i32.lt_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $leu_ltu (export "leu_ltu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.lt_u (local.get $n) (local.get $q)) (i32.le_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $leu_leu (export "leu_leu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.le_u (local.get $n) (local.get $q)) (i32.le_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $leu_gtu (export "leu_gtu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.gt_u (local.get $n) (local.get $q)) (i32.le_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $leu_geu (export "leu_geu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.ge_u (local.get $n) (local.get $q)) (i32.le_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $gtu_ltu (export "gtu_ltu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.lt_u (local.get $n) (local.get $q)) (i32.gt_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $gtu_leu (export "gtu_leu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.le_u (local.get $n) (local.get $q)) (i32.gt_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $gtu_gtu (export "gtu_gtu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.gt_u (local.get $n) (local.get $q)) (i32.gt_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $gtu_geu (export "gtu_geu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.ge_u (local.get $n) (local.get $q)) (i32.gt_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $geu_ltu (export "geu_ltu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.lt_u (local.get $n) (local.get $q)) (i32.ge_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $geu_leu (export "geu_leu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.le_u (local.get $n) (local.get $q)) (i32.ge_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $geu_gtu (export "geu_gtu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.gt_u (local.get $n) (local.get $q)) (i32.ge_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $geu_geu (export "geu_geu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.ge_u (local.get $n) (local.get $q)) (i32.ge_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $lts_ltu (export "lts_ltu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.lt_u (local.get $n) (local.get $q)) (i32.lt_s (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $lts_leu (export "lts_leu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.le_u (local.get $n) (local.get $q)) (i32.lt_s (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $lts_gtu (export "lts_gtu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.gt_u (local.get $n) (local.get $q)) (i32.lt_s (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $lts_geu (export "lts_geu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.ge_u (local.get $n) (local.get $q)) (i32.lt_s (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $les_ltu (export "les_ltu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.lt_u (local.get $n) (local.get $q)) (i32.le_s (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $les_leu (export "les_leu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.le_u (local.get $n) (local.get $q)) (i32.le_s (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $les_gtu (export "les_gtu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.gt_u (local.get $n) (local.get $q)) (i32.le_s (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $les_geu (export "les_geu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.ge_u (local.get $n) (local.get $q)) (i32.le_s (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $gts_ltu (export "gts_ltu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.lt_u (local.get $n) (local.get $q)) (i32.gt_s (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $gts_leu (export "gts_leu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.le_u (local.get $n) (local.get $q)) (i32.gt_s (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $gts_gtu (export "gts_gtu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.gt_u (local.get $n) (local.get $q)) (i32.gt_s (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $gts_geu (export "gts_geu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.ge_u (local.get $n) (local.get $q)) (i32.gt_s (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $ges_ltu (export "ges_ltu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.lt_u (local.get $n) (local.get $q)) (i32.ge_s (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $ges_leu (export "ges_leu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.le_u (local.get $n) (local.get $q)) (i32.ge_s (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $ges_gtu (export "ges_gtu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.gt_u (local.get $n) (local.get $q)) (i32.ge_s (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $ges_geu (export "ges_geu") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.ge_u (local.get $n) (local.get $q)) (i32.ge_s (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $ne (export "ne") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.gt_s (local.get $m) (local.get $p)) (i32.le_u (local.get $n) (local.get $q)) (i32.ne (local.get $m) (local.get $p))))
  (func $turned (export "turned") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.lt_u (local.get $n) (local.get $q)) (i32.gt_u (local.get $p) (local.get $m)) (i32.eq (local.get $m) (local.get $p))))
  (func $wide (export "wide") (param $m i64) (param $n i64) (param $p i64) (param $q i64) (result i32)
    (i32.add (select (i64.lt_u (local.get $n) (local.get $q)) (i64.lt_s (local.get $m) (local.get $p)) (i64.eq (local.get $m) (local.get $p))) (i32.const 200)))
  (func $wideu (export "wideu") (param $m i64) (param $n i64) (param $p i64) (param $q i64) (result i32)
    (select (i64.ge_u (local.get $n) (local.get $q)) (i64.ge_u (local.get $m) (local.get $p)) (i64.eq (local.get $m) (local.get $p))))
  (func $m64n32 (export "m64n32") (param $m i64) (param $n i32) (param $p i64) (param $q i32) (result i32)
    (i32.add (select (i32.le_u (local.get $n) (local.get $q)) (i64.lt_s (local.get $m) (local.get $p)) (i64.eq (local.get $m) (local.get $p))) (i32.const 300)))
  (func $m32n64 (export "m32n64") (param $m i32) (param $n i64) (param $p i32) (param $q i64) (result i32)
    (i32.add (select (i64.gt_u (local.get $n) (local.get $q)) (i32.gt_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))) (i32.const 400)))
  (func $nsigned (export "nsigned") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.lt_s (local.get $n) (local.get $q)) (i32.lt_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $npos (export "npos") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.lt_s (i32.and (local.get $n) (i32.const 0x7fffffff)) (i32.and (local.get $q) (i32.const 0x7fffffff))) (i32.lt_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $constn (export "constn") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (i32.add (select (i32.lt_u (local.get $n) (i32.const 100)) (i32.lt_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))) (i32.const 500)))
  (func $constnle (export "constnle") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (i32.add (select (i32.le_u (local.get $n) (i32.const 100)) (i32.lt_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))) (i32.const 600)))
  (func $constneg (export "constneg") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (i32.add (select (i32.lt_u (local.get $n) (i32.const -5)) (i32.lt_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))) (i32.const 700)))
  (func $constm (export "constm") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.lt_u (local.get $n) (local.get $q)) (i32.lt_s (local.get $m) (i32.const 7)) (i32.eq (local.get $m) (i32.const 7))))
  (func $usesm (export "usesm") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (local $c i32)
    (local.set $c (i32.lt_u (local.get $m) (local.get $p)))
    (i32.add (local.get $c) (select (i32.lt_u (local.get $n) (local.get $q)) (local.get $c) (i32.eq (local.get $m) (local.get $p)))))
  (func $usesn (export "usesn") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (local $c i32)
    (local.set $c (i32.lt_u (local.get $n) (local.get $q)))
    (i32.add (local.get $c) (select (local.get $c) (i32.lt_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p)))))
  (func $usesc (export "usesc") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (local $c i32)
    (local.set $c (i32.eq (local.get $m) (local.get $p)))
    (i32.add (local.get $c) (select (i32.lt_u (local.get $n) (local.get $q)) (i32.lt_u (local.get $m) (local.get $p)) (local.get $c))))
  (func $twice (export "twice") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (local $v i32)
    (local.set $v (select (i32.ge_u (local.get $n) (local.get $q)) (i32.lt_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
    (i32.add (i32.shl (local.get $v) (i32.const 4)) (i32.xor (local.get $v) (local.get $m))))
  (func $apart (export "apart") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (local $a i32) (local $b i32) (local $c i32)
    (local.set $a (i32.lt_u (local.get $m) (local.get $p))) (local.set $b (i32.lt_u (local.get $n) (local.get $q))) (local.set $c (i32.eq (local.get $m) (local.get $p)))
    (if (local.get $n) (then (i32.store (i32.const 512) (local.get $q))))
    (select (local.get $b) (local.get $a) (local.get $c)))
  (func $pick (export "pick") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (local.get $n) (local.get $q) (select (i32.lt_u (local.get $n) (local.get $q)) (i32.gt_s (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p)))))
  (func $picks (export "picks") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (local $v i32)
    (local.set $v (select (i32.le_u (local.get $n) (local.get $q)) (i32.le_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
    (i32.sub (select (local.get $n) (local.get $q) (local.get $v)) (select (local.get $p) (local.get $m) (local.get $v))))
  (func $branch (export "branch") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (if (select (i32.ge_u (local.get $n) (local.get $q)) (i32.ge_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))) (then (i32.store (i32.const 512) (i32.const 1)) (return (i32.const 7))))
    (i32.store (i32.const 516) (i32.const 2))
    (i32.const 9))
  (func $andcmp (export "andcmp") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (local.get $m) (local.get $q) (i32.and (select (i32.lt_u (local.get $n) (local.get $q)) (i32.lt_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))) (i32.ne (local.get $n) (local.get $p)))))
  (func $orcmp (export "orcmp") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (if (i32.or (select (i32.le_u (local.get $n) (local.get $q)) (i32.lt_s (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))) (i32.eq (local.get $n) (local.get $p))) (then (i32.store (i32.const 512) (i32.const 1)) (return (i32.const 7))))
    (i32.store (i32.const 516) (i32.const 2))
    (i32.const 9))
  (func $rot (export "rot") (param $a i64) (param $b i64) (result i32)
    (select (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32))) (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))))
  (func $brs (export "brs") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (block $f
      (block $t
        (if (i32.eq (local.get $m) (local.get $p))
          (then (br_if $t (i32.lt_u (local.get $n) (local.get $q))) (br $f))
          (else (br_if $t (i32.lt_u (local.get $m) (local.get $p))) (br $f))))
      (i32.store (i32.const 512) (i32.const 1))
      (return (i32.const 11)))
    (i32.store (i32.const 516) (i32.const 2))
    (i32.const 22))
  (func $recs (export "recs") (param $x i32) (param $y i32) (result i32)
    (local $p i32) (local $q i32)
    (local.set $p (i32.shl (local.get $x) (i32.const 3)))
    (local.set $q (i32.shl (local.get $y) (i32.const 3)))
    (select (i32.lt_u (i32.load offset=4 (local.get $p)) (i32.load offset=4 (local.get $q)))
            (i32.lt_s (i32.load (local.get $p)) (i32.load (local.get $q)))
            (i32.eq (i32.load (local.get $p)) (i32.load (local.get $q)))))
  (func $sortrec (export "sortrec") (param $cnt i32) (result i32)
    (local $i i32) (local $j i32) (local $s i32) (local $k i32) (local $id i32) (local $pk i32) (local $pid i32)
    (memory.copy (i32.const 2048) (i32.const 0) (i32.shl (local.get $cnt) (i32.const 3)))
    (local.set $i (i32.const 1))
    (block $done
      (loop $outer
        (br_if $done (i32.ge_u (local.get $i) (local.get $cnt)))
        (local.set $k (i32.load (i32.add (i32.const 2048) (i32.shl (local.get $i) (i32.const 3)))))
        (local.set $id (i32.load (i32.add (i32.const 2052) (i32.shl (local.get $i) (i32.const 3)))))
        (local.set $j (local.get $i))
        (block $placed
          (loop $inner
            (br_if $placed (i32.eqz (local.get $j)))
            (local.set $pk (i32.load (i32.add (i32.const 2040) (i32.shl (local.get $j) (i32.const 3)))))
            (local.set $pid (i32.load (i32.add (i32.const 2044) (i32.shl (local.get $j) (i32.const 3)))))
            (br_if $placed (select (i32.ge_u (local.get $id) (local.get $pid))
                                   (i32.ge_s (local.get $k) (local.get $pk))
                                   (i32.eq (local.get $k) (local.get $pk))))
            (i32.store (i32.add (i32.const 2048) (i32.shl (local.get $j) (i32.const 3))) (local.get $pk))
            (i32.store (i32.add (i32.const 2052) (i32.shl (local.get $j) (i32.const 3))) (local.get $pid))
            (local.set $j (i32.sub (local.get $j) (i32.const 1)))
            (br $inner)))
        (i32.store (i32.add (i32.const 2048) (i32.shl (local.get $j) (i32.const 3))) (local.get $k))
        (i32.store (i32.add (i32.const 2052) (i32.shl (local.get $j) (i32.const 3))) (local.get $id))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $outer)))
    (local.set $i (i32.const 0))
    (block $summed
      (loop $sum
        (br_if $summed (i32.ge_u (local.get $i) (local.get $cnt)))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
          (i32.add (i32.load (i32.add (i32.const 2048) (i32.shl (local.get $i) (i32.const 3))))
                   (i32.mul (i32.load (i32.add (i32.const 2052) (i32.shl (local.get $i) (i32.const 3)))) (i32.const 7)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $sum)))
    (local.get $s))
)
