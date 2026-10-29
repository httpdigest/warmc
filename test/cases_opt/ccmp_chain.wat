;; arm64 makes an and or or of compares, which a select or a branch tests,
;; as a cmp and a ccmp (isel.c find_cmp_chains, isel_ops.inc emit_cmp_chain):
;; the ccmp's compare is made where the first side leaves the answer open,
;; and where that closes it the ccmp sets flags that say so.  Each chain is
;; tested by a select (s_), by a branch to stores (b_) and returned as a 0
;; or 1 (v_, a cset); the near misses keep their csets.  The shapes count
;; the ccmps, ccmns and the ccmps of a register: the compare a chain makes
;; as its ccmp is one whose constant takes no register where it can be.
;; Made by test/gen_ccmp.py, which works out every answer.
;;
;; stats -O2 a64: ccmp_chains=91
;;
;; shape: s_eqz_and \sccmp\s=1
;; shape: s_eqz_and \sccmn\s=0
;; shape: s_eqz_and \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_eqz_and \scset\s=0
;; shape: b_eqz_and \sccmp\s=1
;; shape: b_eqz_and \sccmn\s=0
;; shape: b_eqz_and \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: b_eqz_and \scset\s=0
;; shape: v_eqz_and \sccmp\s=1
;; shape: v_eqz_and \sccmn\s=0
;; shape: v_eqz_and \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_swap \sccmp\s=1
;; shape: s_swap \sccmn\s=0
;; shape: s_swap \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_swap \scset\s=0
;; shape: b_swap \sccmp\s=1
;; shape: b_swap \sccmn\s=0
;; shape: b_swap \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: b_swap \scset\s=0
;; shape: v_swap \sccmp\s=1
;; shape: v_swap \sccmn\s=0
;; shape: v_swap \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_ccmn \sccmp\s=0
;; shape: s_ccmn \sccmn\s=1
;; shape: s_ccmn \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_ccmn \scset\s=0
;; shape: b_ccmn \sccmp\s=0
;; shape: b_ccmn \sccmn\s=1
;; shape: b_ccmn \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: b_ccmn \scset\s=0
;; shape: v_ccmn \sccmp\s=0
;; shape: v_ccmn \sccmn\s=1
;; shape: v_ccmn \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_imm31 \sccmp\s=1
;; shape: s_imm31 \sccmn\s=0
;; shape: s_imm31 \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_imm31 \scset\s=0
;; shape: b_imm31 \sccmp\s=1
;; shape: b_imm31 \sccmn\s=0
;; shape: b_imm31 \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: b_imm31 \scset\s=0
;; shape: v_imm31 \sccmp\s=1
;; shape: v_imm31 \sccmn\s=0
;; shape: v_imm31 \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_big \sccmp\s=1
;; shape: s_big \sccmn\s=0
;; shape: s_big \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: s_big \scset\s=0
;; shape: b_big \sccmp\s=1
;; shape: b_big \sccmn\s=0
;; shape: b_big \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: b_big \scset\s=0
;; shape: v_big \sccmp\s=1
;; shape: v_big \sccmn\s=0
;; shape: v_big \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: s_nest2 \sccmp\s=2
;; shape: s_nest2 \sccmn\s=0
;; shape: s_nest2 \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: s_nest2 \scset\s=0
;; shape: b_nest2 \sccmp\s=2
;; shape: b_nest2 \sccmn\s=0
;; shape: b_nest2 \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: b_nest2 \scset\s=0
;; shape: v_nest2 \sccmp\s=2
;; shape: v_nest2 \sccmn\s=0
;; shape: v_nest2 \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: s_nest4 \sccmp\s=3
;; shape: s_nest4 \sccmn\s=0
;; shape: s_nest4 \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_nest4 \scset\s=0
;; shape: b_nest4 \sccmp\s=3
;; shape: b_nest4 \sccmn\s=0
;; shape: b_nest4 \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: b_nest4 \scset\s=0
;; shape: v_nest4 \sccmp\s=3
;; shape: v_nest4 \sccmn\s=0
;; shape: v_nest4 \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_nest5 \sccmp\s=3
;; shape: s_nest5 \sccmn\s=0
;; shape: s_nest5 \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_nest5 \scset\s=2
;; shape: b_nest5 \sccmp\s=3
;; shape: b_nest5 \sccmn\s=0
;; shape: b_nest5 \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: b_nest5 \scset\s=2
;; shape: v_nest5 \sccmp\s=3
;; shape: v_nest5 \sccmn\s=0
;; shape: v_nest5 \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_wide \sccmp\s=1
;; shape: s_wide \sccmn\s=0
;; shape: s_wide \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_wide \scset\s=0
;; shape: b_wide \sccmp\s=1
;; shape: b_wide \sccmn\s=0
;; shape: b_wide \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: b_wide \scset\s=0
;; shape: v_wide \sccmp\s=1
;; shape: v_wide \sccmn\s=0
;; shape: v_wide \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_mixed \sccmp\s=1
;; shape: s_mixed \sccmn\s=0
;; shape: s_mixed \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: s_mixed \scset\s=0
;; shape: b_mixed \sccmp\s=1
;; shape: b_mixed \sccmn\s=0
;; shape: b_mixed \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: b_mixed \scset\s=0
;; shape: v_mixed \sccmp\s=1
;; shape: v_mixed \sccmn\s=0
;; shape: v_mixed \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: s_wneg \sccmp\s=0
;; shape: s_wneg \sccmn\s=1
;; shape: s_wneg \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_wneg \scset\s=0
;; shape: b_wneg \sccmp\s=0
;; shape: b_wneg \sccmn\s=1
;; shape: b_wneg \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: b_wneg \scset\s=0
;; shape: v_wneg \sccmp\s=0
;; shape: v_wneg \sccmn\s=1
;; shape: v_wneg \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_flt \sccmp\s=1
;; shape: s_flt \sccmn\s=0
;; shape: s_flt \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: s_flt \scset\s=0
;; shape: b_flt \sccmp\s=1
;; shape: b_flt \sccmn\s=0
;; shape: b_flt \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: b_flt \scset\s=0
;; shape: v_flt \sccmp\s=1
;; shape: v_flt \sccmn\s=0
;; shape: v_flt \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: s_fltr \sccmp\s=1
;; shape: s_fltr \sccmn\s=0
;; shape: s_fltr \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: s_fltr \scset\s=0
;; shape: b_fltr \sccmp\s=1
;; shape: b_fltr \sccmn\s=0
;; shape: b_fltr \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: b_fltr \scset\s=0
;; shape: v_fltr \sccmp\s=1
;; shape: v_fltr \sccmn\s=0
;; shape: v_fltr \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: s_fltle \sccmp\s=1
;; shape: s_fltle \sccmn\s=0
;; shape: s_fltle \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: s_fltle \scset\s=0
;; shape: b_fltle \sccmp\s=1
;; shape: b_fltle \sccmn\s=0
;; shape: b_fltle \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: b_fltle \scset\s=0
;; shape: v_fltle \sccmp\s=1
;; shape: v_fltle \sccmn\s=0
;; shape: v_fltle \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: s_tstl \sccmp\s=1
;; shape: s_tstl \sccmn\s=0
;; shape: s_tstl \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: s_tstl \scset\s=0
;; shape: b_tstl \sccmp\s=1
;; shape: b_tstl \sccmn\s=0
;; shape: b_tstl \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: b_tstl \scset\s=0
;; shape: v_tstl \sccmp\s=1
;; shape: v_tstl \sccmn\s=0
;; shape: v_tstl \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: s_tstr \sccmp\s=1
;; shape: s_tstr \sccmn\s=0
;; shape: s_tstr \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: s_tstr \scset\s=0
;; shape: b_tstr \sccmp\s=1
;; shape: b_tstr \sccmn\s=0
;; shape: b_tstr \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: b_tstr \scset\s=0
;; shape: v_tstr \sccmp\s=1
;; shape: v_tstr \sccmn\s=0
;; shape: v_tstr \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: s_kfirst \sccmp\s=1
;; shape: s_kfirst \sccmn\s=0
;; shape: s_kfirst \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: s_kfirst \scset\s=0
;; shape: b_kfirst \sccmp\s=1
;; shape: b_kfirst \sccmn\s=0
;; shape: b_kfirst \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: b_kfirst \scset\s=0
;; shape: v_kfirst \sccmp\s=1
;; shape: v_kfirst \sccmn\s=0
;; shape: v_kfirst \sccmp\s+[wx]\d+, [wx](\d+|zr),=1
;; shape: s_all_ops \sccmp\s=2
;; shape: s_all_ops \sccmn\s=1
;; shape: s_all_ops \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_all_ops \scset\s=0
;; shape: b_all_ops \sccmp\s=2
;; shape: b_all_ops \sccmn\s=1
;; shape: b_all_ops \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: b_all_ops \scset\s=0
;; shape: v_all_ops \sccmp\s=2
;; shape: v_all_ops \sccmn\s=1
;; shape: v_all_ops \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_le_ne \sccmp\s=1
;; shape: s_le_ne \sccmn\s=0
;; shape: s_le_ne \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_le_ne \scset\s=0
;; shape: b_le_ne \sccmp\s=1
;; shape: b_le_ne \sccmn\s=0
;; shape: b_le_ne \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: b_le_ne \scset\s=0
;; shape: v_le_ne \sccmp\s=1
;; shape: v_le_ne \sccmn\s=0
;; shape: v_le_ne \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_hi_ls \sccmp\s=1
;; shape: s_hi_ls \sccmn\s=0
;; shape: s_hi_ls \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_hi_ls \scset\s=0
;; shape: b_hi_ls \sccmp\s=1
;; shape: b_hi_ls \sccmn\s=0
;; shape: b_hi_ls \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: b_hi_ls \scset\s=0
;; shape: v_hi_ls \sccmp\s=1
;; shape: v_hi_ls \sccmn\s=0
;; shape: v_hi_ls \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_costly \sccmp\s=1
;; shape: s_costly \sccmn\s=0
;; shape: s_costly \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_costly \scset\s=0
;; shape: b_costly \sccmp\s=1
;; shape: b_costly \sccmn\s=0
;; shape: b_costly \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: b_costly \scset\s=0
;; shape: v_costly \sccmp\s=1
;; shape: v_costly \sccmn\s=0
;; shape: v_costly \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_costneg \sccmp\s=0
;; shape: s_costneg \sccmn\s=1
;; shape: s_costneg \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_costneg \scset\s=0
;; shape: b_costneg \sccmp\s=0
;; shape: b_costneg \sccmn\s=1
;; shape: b_costneg \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: b_costneg \scset\s=0
;; shape: v_costneg \sccmp\s=0
;; shape: v_costneg \sccmn\s=1
;; shape: v_costneg \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_fltboth \sccmp\s=0
;; shape: s_fltboth \sccmn\s=0
;; shape: s_fltboth \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_fltboth \scset\s=2
;; shape: b_fltboth \sccmp\s=0
;; shape: b_fltboth \sccmn\s=0
;; shape: b_fltboth \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: b_fltboth \scset\s=2
;; shape: v_fltboth \sccmp\s=0
;; shape: v_fltboth \sccmn\s=0
;; shape: v_fltboth \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_tst2 \sccmp\s=0
;; shape: s_tst2 \sccmn\s=0
;; shape: s_tst2 \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_tst2 \scset\s=2
;; shape: b_tst2 \sccmp\s=0
;; shape: b_tst2 \sccmn\s=0
;; shape: b_tst2 \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: b_tst2 \scset\s=2
;; shape: v_tst2 \sccmp\s=0
;; shape: v_tst2 \sccmn\s=0
;; shape: v_tst2 \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_xor \sccmp\s=0
;; shape: s_xor \sccmn\s=0
;; shape: s_xor \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: s_xor \scset\s=2
;; shape: b_xor \sccmp\s=0
;; shape: b_xor \sccmn\s=0
;; shape: b_xor \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: b_xor \scset\s=2
;; shape: v_xor \sccmp\s=0
;; shape: v_xor \sccmn\s=0
;; shape: v_xor \sccmp\s+[wx]\d+, [wx](\d+|zr),=0
;; shape: two \sccmp\s=1
;; shape: two \scset\s=0
;; shape: clob \sccmp\s=1
;; shape: clob \scset\s=2
;; shape: apart \sccmp\s=1
;; shape: apart \scset\s=1
;; shape: armsel \sccmp\s=1
;; shape: armsel \scmp\s=1
;; shape: shared \sccm[pn]\s=0
;; shape: cross \sccm[pn]\s=0
;; shape: tstand \sccm[pn]\s=0
;;
;; run: s_eqz_and 0 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_eqz_and 0 1 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_eqz_and 0 1 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_eqz_and 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: b_eqz_and 0 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_eqz_and 0 1 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_eqz_and 0 1 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_eqz_and 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: v_eqz_and 0 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_eqz_and 0 1 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_eqz_and 0 1 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_eqz_and 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: s_swap 5 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_swap 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: s_swap 0 1001 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: s_swap 5 1001 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: b_swap 5 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_swap 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: b_swap 0 1001 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: b_swap 5 1001 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: v_swap 5 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_swap 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: v_swap 0 1001 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: v_swap 5 1001 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: s_ccmn 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_ccmn 4294967293 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_ccmn 4294967293 100000 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_ccmn 0 100000 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: b_ccmn 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_ccmn 4294967293 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_ccmn 4294967293 100000 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_ccmn 0 100000 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: v_ccmn 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_ccmn 4294967293 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_ccmn 4294967293 100000 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_ccmn 0 100000 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: s_imm31 1 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_imm31 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: s_imm31 0 31 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: s_imm31 1 31 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: b_imm31 1 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_imm31 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: b_imm31 0 31 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: b_imm31 1 31 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: v_imm31 1 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_imm31 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: v_imm31 0 31 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: v_imm31 1 31 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: s_big 0 2147483648 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_big 32 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_big 32 2147483648 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_big 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: b_big 0 2147483648 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_big 32 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_big 32 2147483648 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_big 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: v_big 0 2147483648 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_big 32 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_big 32 2147483648 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_big 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: s_nest2 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_nest2 0 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_nest2 1 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_nest2 2 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_nest2 1 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: s_nest2 2 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: b_nest2 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_nest2 0 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_nest2 1 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_nest2 2 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_nest2 1 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: b_nest2 2 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: v_nest2 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_nest2 0 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_nest2 1 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_nest2 2 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_nest2 1 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: v_nest2 2 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: s_nest4 0 7 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_nest4 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: s_nest4 0 0 15 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: s_nest4 0 7 15 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: s_nest4 1 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: s_nest4 1 0 15 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: s_nest4 1 7 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: b_nest4 0 7 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_nest4 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: b_nest4 0 0 15 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: b_nest4 0 7 15 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: b_nest4 1 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: b_nest4 1 0 15 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: b_nest4 1 7 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: v_nest4 0 7 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_nest4 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: v_nest4 0 0 15 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: v_nest4 0 7 15 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: v_nest4 1 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: v_nest4 1 0 15 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: v_nest4 1 7 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: s_nest5 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_nest5 0 0 15 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_nest5 0 3 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_nest5 1 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_nest5 1 0 15 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_nest5 7 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_nest5 7 0 15 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_nest5 0 0 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: b_nest5 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_nest5 0 0 15 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_nest5 0 3 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_nest5 1 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_nest5 1 0 15 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_nest5 7 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_nest5 7 0 15 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_nest5 0 0 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: v_nest5 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_nest5 0 0 15 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_nest5 0 3 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_nest5 1 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_nest5 1 0 15 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_nest5 7 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_nest5 7 0 15 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_nest5 0 0 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: s_wide 0 0 0 0 1 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_wide 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: s_wide 0 0 0 0 0 10 0x0000000000000000 0x0000000000000000 => i32:7
;; run: s_wide 0 0 0 0 1 10 0x0000000000000000 0x0000000000000000 => i32:7
;; run: b_wide 0 0 0 0 1 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_wide 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: b_wide 0 0 0 0 0 10 0x0000000000000000 0x0000000000000000 => i32:70
;; run: b_wide 0 0 0 0 1 10 0x0000000000000000 0x0000000000000000 => i32:70
;; run: v_wide 0 0 0 0 1 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_wide 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: v_wide 0 0 0 0 0 10 0x0000000000000000 0x0000000000000000 => i32:1
;; run: v_wide 0 0 0 0 1 10 0x0000000000000000 0x0000000000000000 => i32:1
;; run: s_mixed 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_mixed 0 1 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_mixed 0 1 0 0 0 1 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_mixed 0 0 0 0 0 1 0x0000000000000000 0x0000000000000000 => i32:7
;; run: b_mixed 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_mixed 0 1 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_mixed 0 1 0 0 0 1 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_mixed 0 0 0 0 0 1 0x0000000000000000 0x0000000000000000 => i32:70
;; run: v_mixed 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_mixed 0 1 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_mixed 0 1 0 0 0 1 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_mixed 0 0 0 0 0 1 0x0000000000000000 0x0000000000000000 => i32:1
;; run: s_wneg 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_wneg 0 0 0 0 18446744073709551615 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_wneg 0 0 0 0 18446744073709551615 9223372036854775808 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_wneg 0 0 0 0 0 9223372036854775808 0x0000000000000000 0x0000000000000000 => i32:7
;; run: b_wneg 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_wneg 0 0 0 0 18446744073709551615 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_wneg 0 0 0 0 18446744073709551615 9223372036854775808 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_wneg 0 0 0 0 0 9223372036854775808 0x0000000000000000 0x0000000000000000 => i32:70
;; run: v_wneg 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_wneg 0 0 0 0 18446744073709551615 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_wneg 0 0 0 0 18446744073709551615 9223372036854775808 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_wneg 0 0 0 0 0 9223372036854775808 0x0000000000000000 0x0000000000000000 => i32:1
;; run: s_flt 0 0 0 0 0 0 0x3ff8000000000000 0x3ff8000000000000 => i32:9
;; run: s_flt 0 1 0 0 0 0 0x3ff8000000000000 0x3ff8000000000000 => i32:9
;; run: s_flt 0 1 0 0 0 0 0x3ff8000000000000 0x4000000000000000 => i32:9
;; run: s_flt 0 0 0 0 0 0 0x3ff8000000000000 0x4000000000000000 => i32:7
;; run: b_flt 0 0 0 0 0 0 0x3ff8000000000000 0x3ff8000000000000 => i32:9
;; run: b_flt 0 1 0 0 0 0 0x3ff8000000000000 0x3ff8000000000000 => i32:9
;; run: b_flt 0 1 0 0 0 0 0x3ff8000000000000 0x4000000000000000 => i32:9
;; run: b_flt 0 0 0 0 0 0 0x3ff8000000000000 0x4000000000000000 => i32:70
;; run: v_flt 0 0 0 0 0 0 0x3ff8000000000000 0x3ff8000000000000 => i32:0
;; run: v_flt 0 1 0 0 0 0 0x3ff8000000000000 0x3ff8000000000000 => i32:0
;; run: v_flt 0 1 0 0 0 0 0x3ff8000000000000 0x4000000000000000 => i32:0
;; run: v_flt 0 0 0 0 0 0 0x3ff8000000000000 0x4000000000000000 => i32:1
;; run: s_fltr 0 1 0 0 0 0 0x3ff8000000000000 0x3ff8000000000000 => i32:9
;; run: s_fltr 0 0 0 0 0 0 0x3ff8000000000000 0x3ff8000000000000 => i32:7
;; run: s_fltr 0 0 0 0 0 0 0x3ff8000000000000 0x4000000000000000 => i32:7
;; run: s_fltr 0 1 0 0 0 0 0x3ff8000000000000 0x4000000000000000 => i32:7
;; run: b_fltr 0 1 0 0 0 0 0x3ff8000000000000 0x3ff8000000000000 => i32:9
;; run: b_fltr 0 0 0 0 0 0 0x3ff8000000000000 0x3ff8000000000000 => i32:70
;; run: b_fltr 0 0 0 0 0 0 0x3ff8000000000000 0x4000000000000000 => i32:70
;; run: b_fltr 0 1 0 0 0 0 0x3ff8000000000000 0x4000000000000000 => i32:70
;; run: v_fltr 0 1 0 0 0 0 0x3ff8000000000000 0x3ff8000000000000 => i32:0
;; run: v_fltr 0 0 0 0 0 0 0x3ff8000000000000 0x3ff8000000000000 => i32:1
;; run: v_fltr 0 0 0 0 0 0 0x3ff8000000000000 0x4000000000000000 => i32:1
;; run: v_fltr 0 1 0 0 0 0 0x3ff8000000000000 0x4000000000000000 => i32:1
;; run: s_fltle 0 0 0 0 0 0 0x3ff8000000000000 0x8000000000000000 => i32:9
;; run: s_fltle 0 0 0 1 0 0 0x3ff8000000000000 0x8000000000000000 => i32:9
;; run: s_fltle 0 0 0 1 0 0 0x3ff8000000000000 0x3ff8000000000000 => i32:9
;; run: s_fltle 0 0 0 0 0 0 0x3ff8000000000000 0x3ff8000000000000 => i32:7
;; run: b_fltle 0 0 0 0 0 0 0x3ff8000000000000 0x8000000000000000 => i32:9
;; run: b_fltle 0 0 0 1 0 0 0x3ff8000000000000 0x8000000000000000 => i32:9
;; run: b_fltle 0 0 0 1 0 0 0x3ff8000000000000 0x3ff8000000000000 => i32:9
;; run: b_fltle 0 0 0 0 0 0 0x3ff8000000000000 0x3ff8000000000000 => i32:70
;; run: v_fltle 0 0 0 0 0 0 0x3ff8000000000000 0x8000000000000000 => i32:0
;; run: v_fltle 0 0 0 1 0 0 0x3ff8000000000000 0x8000000000000000 => i32:0
;; run: v_fltle 0 0 0 1 0 0 0x3ff8000000000000 0x3ff8000000000000 => i32:0
;; run: v_fltle 0 0 0 0 0 0 0x3ff8000000000000 0x3ff8000000000000 => i32:1
;; run: s_tstl 0 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_tstl 8 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_tstl 8 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_tstl 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: b_tstl 0 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_tstl 8 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_tstl 8 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_tstl 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: v_tstl 0 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_tstl 8 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_tstl 8 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_tstl 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: s_tstr 8 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_tstr 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: s_tstr 0 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: s_tstr 8 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: b_tstr 8 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_tstr 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: b_tstr 0 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: b_tstr 8 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: v_tstr 8 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_tstr 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: v_tstr 0 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: v_tstr 8 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: s_kfirst 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_kfirst 0 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_kfirst 6 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_kfirst 6 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: b_kfirst 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_kfirst 0 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_kfirst 6 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_kfirst 6 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: v_kfirst 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_kfirst 0 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_kfirst 6 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_kfirst 6 0 1 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: s_all_ops 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_all_ops 0 0 0 4 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_all_ops 0 0 3 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_all_ops 1 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_all_ops 1 0 0 4 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_all_ops 1 0 3 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_all_ops 1 0 3 4 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_all_ops 0 0 3 4 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: b_all_ops 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_all_ops 0 0 0 4 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_all_ops 0 0 3 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_all_ops 1 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_all_ops 1 0 0 4 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_all_ops 1 0 3 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_all_ops 1 0 3 4 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_all_ops 0 0 3 4 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: v_all_ops 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_all_ops 0 0 0 4 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_all_ops 0 0 3 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_all_ops 1 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_all_ops 1 0 0 4 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_all_ops 1 0 3 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_all_ops 1 0 3 4 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_all_ops 0 0 3 4 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: s_le_ne 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_le_ne 1 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_le_ne 1 0 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_le_ne 0 0 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: b_le_ne 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_le_ne 1 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_le_ne 1 0 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_le_ne 0 0 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: v_le_ne 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_le_ne 1 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_le_ne 1 0 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_le_ne 0 0 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: s_hi_ls 0 0 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_hi_ls 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: s_hi_ls 18 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: s_hi_ls 18 0 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: b_hi_ls 0 0 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_hi_ls 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: b_hi_ls 18 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: b_hi_ls 18 0 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: v_hi_ls 0 0 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_hi_ls 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: v_hi_ls 18 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: v_hi_ls 18 0 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: s_costly 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_costly 0 1001 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_costly 3 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_costly 3 1001 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: b_costly 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_costly 0 1001 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_costly 3 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_costly 3 1001 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: v_costly 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_costly 0 1001 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_costly 3 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_costly 3 1001 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: s_costneg 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_costneg 0 1001 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_costneg 4294967291 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_costneg 4294967291 1001 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: b_costneg 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_costneg 0 1001 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_costneg 4294967291 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_costneg 4294967291 1001 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: v_costneg 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_costneg 0 1001 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_costneg 4294967291 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_costneg 4294967291 1001 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: s_fltboth 0 0 0 0 0 0 0x3ff8000000000000 0x8000000000000000 => i32:9
;; run: s_fltboth 0 0 0 0 0 0 0x3ff8000000000000 0x4000000000000000 => i32:9
;; run: s_fltboth 0 0 0 0 0 0 0x3ff8000000000000 0x7ff8000000000000 => i32:9
;; run: s_fltboth 0 0 0 0 0 0 0x3ff8000000000000 0x3ff8000000000000 => i32:7
;; run: b_fltboth 0 0 0 0 0 0 0x3ff8000000000000 0x8000000000000000 => i32:9
;; run: b_fltboth 0 0 0 0 0 0 0x3ff8000000000000 0x4000000000000000 => i32:9
;; run: b_fltboth 0 0 0 0 0 0 0x3ff8000000000000 0x7ff8000000000000 => i32:9
;; run: b_fltboth 0 0 0 0 0 0 0x3ff8000000000000 0x3ff8000000000000 => i32:70
;; run: v_fltboth 0 0 0 0 0 0 0x3ff8000000000000 0x8000000000000000 => i32:0
;; run: v_fltboth 0 0 0 0 0 0 0x3ff8000000000000 0x4000000000000000 => i32:0
;; run: v_fltboth 0 0 0 0 0 0 0x3ff8000000000000 0x7ff8000000000000 => i32:0
;; run: v_fltboth 0 0 0 0 0 0 0x3ff8000000000000 0x3ff8000000000000 => i32:1
;; run: s_tst2 0 16 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_tst2 8 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_tst2 8 16 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_tst2 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: b_tst2 0 16 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_tst2 8 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_tst2 8 16 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_tst2 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: v_tst2 0 16 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_tst2 8 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_tst2 8 16 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_tst2 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: s_xor 0 0 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_xor 0 1 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: s_xor 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: s_xor 0 1 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: b_xor 0 0 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_xor 0 1 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: b_xor 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: b_xor 0 1 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:70
;; run: v_xor 0 0 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_xor 0 1 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: v_xor 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: v_xor 0 1 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: two 0 0 1 3 0 0 0x0000000000000000 0x0000000000000000 => i32:18
;; run: two 0 1 0 100 0 0 0x0000000000000000 0x0000000000000000 => i32:18
;; run: two 0 1 1 4294967295 0 0 0x0000000000000000 0x0000000000000000 => i32:18
;; run: two 0 0 0 5 0 0 0x0000000000000000 0x0000000000000000 => i32:-9
;; run: clob 0 0 1 3 0 0 0x0000000000000000 0x0000000000000000 => i32:19
;; run: clob 0 1 0 100 0 0 0x0000000000000000 0x0000000000000000 => i32:19
;; run: clob 0 1 1 4294967295 0 0 0x0000000000000000 0x0000000000000000 => i32:18
;; run: clob 0 0 0 5 0 0 0x0000000000000000 0x0000000000000000 => i32:6
;; run: apart 0 0 1 3 0 0 0x0000000000000000 0x0000000000000000 => i32:10
;; run: apart 0 1 0 100 0 0 0x0000000000000000 0x0000000000000000 => i32:10
;; run: apart 0 1 1 4294967295 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: apart 0 0 0 5 0 0 0x0000000000000000 0x0000000000000000 => i32:8
;; run: armsel 0 0 1 3 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: armsel 0 1 0 100 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: armsel 0 1 1 4294967295 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: armsel 0 0 0 5 0 0 0x0000000000000000 0x0000000000000000 => i32:2
;; run: shared 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:1
;; run: shared 0 1 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: shared 0 1 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:0
;; run: shared 0 0 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:2
;; run: cross 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: cross 0 1 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: cross 0 1 0 2147483648 0 0 0x0000000000000000 0x0000000000000000 => i32:9
;; run: cross 0 0 0 2147483648 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: tstand 0 0 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: tstand 0 1 0 0 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: tstand 0 1 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:7
;; run: tstand 0 0 0 1 0 0 0x0000000000000000 0x0000000000000000 => i32:9

(module
  (memory 1)
  ;; musl decfloat's k == a && !x[k]: a register compare, then eqz as ccmp #0
  (func $s_eqz_and (export "s_eqz_and") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.and (i32.eq (local.get $a) (local.get $b)) (i32.eqz (local.get $c)))))
  (func $b_eqz_and (export "b_eqz_and") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.and (i32.eq (local.get $a) (local.get $b)) (i32.eqz (local.get $c)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_eqz_and (export "v_eqz_and") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.and (i32.eq (local.get $a) (local.get $b)) (i32.eqz (local.get $c))))
  ;; the ccmp the compare whose constant takes no register: lt_s a, 5, the first operand
  (func $s_swap (export "s_swap") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.or (i32.gt_u (local.get $b) (i32.const 1000)) (i32.lt_s (local.get $a) (i32.const 5)))))
  (func $b_swap (export "b_swap") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.or (i32.gt_u (local.get $b) (i32.const 1000)) (i32.lt_s (local.get $a) (i32.const 5)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_swap (export "v_swap") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.or (i32.gt_u (local.get $b) (i32.const 1000)) (i32.lt_s (local.get $a) (i32.const 5))))
  ;; a negative constant: ccmn #3; 100000 a register in the cmp
  (func $s_ccmn (export "s_ccmn") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.and (i32.ge_s (local.get $b) (i32.const 100000)) (i32.ne (local.get $a) (i32.const -3)))))
  (func $b_ccmn (export "b_ccmn") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.and (i32.ge_s (local.get $b) (i32.const 100000)) (i32.ne (local.get $a) (i32.const -3)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_ccmn (export "v_ccmn") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.and (i32.ge_s (local.get $b) (i32.const 100000)) (i32.ne (local.get $a) (i32.const -3))))
  ;; ccmp #31, the largest immediate
  (func $s_imm31 (export "s_imm31") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.or (i32.eqz (local.get $a)) (i32.eq (local.get $b) (i32.const 31)))))
  (func $b_imm31 (export "b_imm31") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.or (i32.eqz (local.get $a)) (i32.eq (local.get $b) (i32.const 31)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_imm31 (export "v_imm31") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.or (i32.eqz (local.get $a)) (i32.eq (local.get $b) (i32.const 31))))
  ;; no 5-bit immediate either way: -32 made in a register
  (func $s_big (export "s_big") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.and (i32.lt_u (local.get $a) (i32.const 32)) (i32.gt_s (local.get $b) (i32.const -32)))))
  (func $b_big (export "b_big") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.and (i32.lt_u (local.get $a) (i32.const 32)) (i32.gt_s (local.get $b) (i32.const -32)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_big (export "v_big") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.and (i32.lt_u (local.get $a) (i32.const 32)) (i32.gt_s (local.get $b) (i32.const -32))))
  ;; a chain the first side of a chain
  (func $s_nest2 (export "s_nest2") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.and (i32.or (i32.eq (local.get $a) (i32.const 1)) (i32.eq (local.get $a) (i32.const 2))) (i32.ne (local.get $b) (local.get $c)))))
  (func $b_nest2 (export "b_nest2") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.and (i32.or (i32.eq (local.get $a) (i32.const 1)) (i32.eq (local.get $a) (i32.const 2))) (i32.ne (local.get $b) (local.get $c)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_nest2 (export "v_nest2") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.and (i32.or (i32.eq (local.get $a) (i32.const 1)) (i32.eq (local.get $a) (i32.const 2))) (i32.ne (local.get $b) (local.get $c))))
  ;; four compares, CCH_MAX
  (func $s_nest4 (export "s_nest4") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.or (i32.or (i32.or (i32.eq (local.get $a) (i32.const 1)) (i32.eq (local.get $a) (i32.const 3))) (i32.lt_u (local.get $b) (i32.const 7))) (i32.ge_s (local.get $c) (i32.const 15)))))
  (func $b_nest4 (export "b_nest4") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.or (i32.or (i32.or (i32.eq (local.get $a) (i32.const 1)) (i32.eq (local.get $a) (i32.const 3))) (i32.lt_u (local.get $b) (i32.const 7))) (i32.ge_s (local.get $c) (i32.const 15)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_nest4 (export "v_nest4") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.or (i32.or (i32.or (i32.eq (local.get $a) (i32.const 1)) (i32.eq (local.get $a) (i32.const 3))) (i32.lt_u (local.get $b) (i32.const 7))) (i32.ge_s (local.get $c) (i32.const 15))))
  ;; five: the four first a chain, the and of it and the fifth an and
  (func $s_nest5 (export "s_nest5") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.and (i32.or (i32.or (i32.or (i32.eq (local.get $a) (i32.const 1)) (i32.ne (local.get $b) (i32.const 3))) (i32.eq (local.get $a) (i32.const 7))) (i32.eq (local.get $c) (i32.const 15))) (i32.ne (local.get $b) (local.get $d)))))
  (func $b_nest5 (export "b_nest5") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.and (i32.or (i32.or (i32.or (i32.eq (local.get $a) (i32.const 1)) (i32.ne (local.get $b) (i32.const 3))) (i32.eq (local.get $a) (i32.const 7))) (i32.eq (local.get $c) (i32.const 15))) (i32.ne (local.get $b) (local.get $d)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_nest5 (export "v_nest5") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.and (i32.or (i32.or (i32.or (i32.eq (local.get $a) (i32.const 1)) (i32.ne (local.get $b) (i32.const 3))) (i32.eq (local.get $a) (i32.const 7))) (i32.eq (local.get $c) (i32.const 15))) (i32.ne (local.get $b) (local.get $d))))
  ;; i64 compares
  (func $s_wide (export "s_wide") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.or (i64.eqz (local.get $x)) (i64.gt_u (local.get $y) (i64.const 9)))))
  (func $b_wide (export "b_wide") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.or (i64.eqz (local.get $x)) (i64.gt_u (local.get $y) (i64.const 9)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_wide (export "v_wide") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.or (i64.eqz (local.get $x)) (i64.gt_u (local.get $y) (i64.const 9))))
  ;; an i64 compare and an i32 one
  (func $s_mixed (export "s_mixed") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.and (i64.lt_s (local.get $x) (local.get $y)) (i32.eq (local.get $a) (local.get $b)))))
  (func $b_mixed (export "b_mixed") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.and (i64.lt_s (local.get $x) (local.get $y)) (i32.eq (local.get $a) (local.get $b)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_mixed (export "v_mixed") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.and (i64.lt_s (local.get $x) (local.get $y)) (i32.eq (local.get $a) (local.get $b))))
  ;; i64 ccmn
  (func $s_wneg (export "s_wneg") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.and (i64.ne (local.get $x) (i64.const -1)) (i64.le_s (local.get $y) (i64.const -31)))))
  (func $b_wneg (export "b_wneg") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.and (i64.ne (local.get $x) (i64.const -1)) (i64.le_s (local.get $y) (i64.const -31)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_wneg (export "v_wneg") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.and (i64.ne (local.get $x) (i64.const -1)) (i64.le_s (local.get $y) (i64.const -31))))
  ;; an fcmp first
  (func $s_flt (export "s_flt") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.and (f64.lt (local.get $p) (local.get $q)) (i32.eq (local.get $a) (local.get $b)))))
  (func $b_flt (export "b_flt") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.and (f64.lt (local.get $p) (local.get $q)) (i32.eq (local.get $a) (local.get $b)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_flt (export "v_flt") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.and (f64.lt (local.get $p) (local.get $q)) (i32.eq (local.get $a) (local.get $b))))
  ;; an fcmp second, made first: f64.ne holds where p or q is a NaN
  (func $s_fltr (export "s_fltr") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.or (i32.eq (local.get $a) (local.get $b)) (f64.ne (local.get $p) (local.get $q)))))
  (func $b_fltr (export "b_fltr") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.or (i32.eq (local.get $a) (local.get $b)) (f64.ne (local.get $p) (local.get $q)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_fltr (export "v_fltr") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.or (i32.eq (local.get $a) (local.get $b)) (f64.ne (local.get $p) (local.get $q))))
  ;; fcmp's ls, which fails on a NaN
  (func $s_fltle (export "s_fltle") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.and (f64.le (local.get $p) (local.get $q)) (i32.ge_u (local.get $c) (local.get $d)))))
  (func $b_fltle (export "b_fltle") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.and (f64.le (local.get $p) (local.get $q)) (i32.ge_u (local.get $c) (local.get $d)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_fltle (export "v_fltle") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.and (f64.le (local.get $p) (local.get $q)) (i32.ge_u (local.get $c) (local.get $d))))
  ;; tst first
  (func $s_tstl (export "s_tstl") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.and (i32.eqz (i32.and (local.get $a) (i32.const 8))) (i32.eq (local.get $b) (local.get $c)))))
  (func $b_tstl (export "b_tstl") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.and (i32.eqz (i32.and (local.get $a) (i32.const 8))) (i32.eq (local.get $b) (local.get $c)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_tstl (export "v_tstl") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.and (i32.eqz (i32.and (local.get $a) (i32.const 8))) (i32.eq (local.get $b) (local.get $c))))
  ;; a tst second, made first
  (func $s_tstr (export "s_tstr") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.or (i32.eq (local.get $b) (local.get $c)) (i32.eqz (i32.and (local.get $a) (i32.const 8))))))
  (func $b_tstr (export "b_tstr") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.or (i32.eq (local.get $b) (local.get $c)) (i32.eqz (i32.and (local.get $a) (i32.const 8))))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_tstr (export "v_tstr") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.or (i32.eq (local.get $b) (local.get $c)) (i32.eqz (i32.and (local.get $a) (i32.const 8)))))
  ;; the constant first
  (func $s_kfirst (export "s_kfirst") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.and (i32.lt_u (i32.const 5) (local.get $a)) (i32.ne (local.get $b) (local.get $c)))))
  (func $b_kfirst (export "b_kfirst") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.and (i32.lt_u (i32.const 5) (local.get $a)) (i32.ne (local.get $b) (local.get $c)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_kfirst (export "v_kfirst") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.and (i32.lt_u (i32.const 5) (local.get $a)) (i32.ne (local.get $b) (local.get $c))))
  ;; le_u, gt_s, ge_u and lt_s, and and or together
  (func $s_all_ops (export "s_all_ops") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.or (i32.and (i32.and (i32.le_u (local.get $a) (local.get $b)) (i32.gt_s (local.get $c) (i32.const 2))) (i32.ge_u (local.get $d) (i32.const 4))) (i32.lt_s (local.get $a) (i32.const -5)))))
  (func $b_all_ops (export "b_all_ops") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.or (i32.and (i32.and (i32.le_u (local.get $a) (local.get $b)) (i32.gt_s (local.get $c) (i32.const 2))) (i32.ge_u (local.get $d) (i32.const 4))) (i32.lt_s (local.get $a) (i32.const -5)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_all_ops (export "v_all_ops") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.or (i32.and (i32.and (i32.le_u (local.get $a) (local.get $b)) (i32.gt_s (local.get $c) (i32.const 2))) (i32.ge_u (local.get $d) (i32.const 4))) (i32.lt_s (local.get $a) (i32.const -5))))
  ;; le_s #0, false where the flags say z
  (func $s_le_ne (export "s_le_ne") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.and (i32.ne (local.get $c) (local.get $d)) (i32.le_s (local.get $a) (i32.const 0)))))
  (func $b_le_ne (export "b_le_ne") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.and (i32.ne (local.get $c) (local.get $d)) (i32.le_s (local.get $a) (i32.const 0)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_le_ne (export "v_le_ne") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.and (i32.ne (local.get $c) (local.get $d)) (i32.le_s (local.get $a) (i32.const 0))))
  ;; gt_u's hi, true where the flags say c and not z
  (func $s_hi_ls (export "s_hi_ls") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.or (i32.ge_s (local.get $c) (local.get $d)) (i32.gt_u (local.get $a) (i32.const 17)))))
  (func $b_hi_ls (export "b_hi_ls") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.or (i32.ge_s (local.get $c) (local.get $d)) (i32.gt_u (local.get $a) (i32.const 17)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_hi_ls (export "v_hi_ls") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.or (i32.ge_s (local.get $c) (local.get $d)) (i32.gt_u (local.get $a) (i32.const 17))))
  ;; the ccmp the first operand, whose constant is an immediate: 1000 would take a register
  (func $s_costly (export "s_costly") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.and (i32.eq (local.get $a) (i32.const 3)) (i32.gt_u (local.get $b) (i32.const 1000)))))
  (func $b_costly (export "b_costly") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.and (i32.eq (local.get $a) (i32.const 3)) (i32.gt_u (local.get $b) (i32.const 1000)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_costly (export "v_costly") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.and (i32.eq (local.get $a) (i32.const 3)) (i32.gt_u (local.get $b) (i32.const 1000))))
  ;; the ccmp the first operand, a ccmn
  (func $s_costneg (export "s_costneg") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.and (i32.eq (local.get $a) (i32.const -5)) (i32.gt_u (local.get $b) (i32.const 1000)))))
  (func $b_costneg (export "b_costneg") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.and (i32.eq (local.get $a) (i32.const -5)) (i32.gt_u (local.get $b) (i32.const 1000)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_costneg (export "v_costneg") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.and (i32.eq (local.get $a) (i32.const -5)) (i32.gt_u (local.get $b) (i32.const 1000))))
  ;; two fcmps: no ccmp takes a float
  (func $s_fltboth (export "s_fltboth") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.and (f64.le (local.get $p) (local.get $q)) (f64.ge (local.get $p) (local.get $q)))))
  (func $b_fltboth (export "b_fltboth") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.and (f64.le (local.get $p) (local.get $q)) (f64.ge (local.get $p) (local.get $q)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_fltboth (export "v_fltboth") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.and (f64.le (local.get $p) (local.get $q)) (f64.ge (local.get $p) (local.get $q))))
  ;; two tsts: no ccmp takes one
  (func $s_tst2 (export "s_tst2") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.and (i32.eqz (i32.and (local.get $a) (i32.const 8))) (i32.eqz (i32.and (local.get $b) (i32.const 16))))))
  (func $b_tst2 (export "b_tst2") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.and (i32.eqz (i32.and (local.get $a) (i32.const 8))) (i32.eqz (i32.and (local.get $b) (i32.const 16))))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_tst2 (export "v_tst2") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.and (i32.eqz (i32.and (local.get $a) (i32.const 8))) (i32.eqz (i32.and (local.get $b) (i32.const 16)))))
  ;; an xor
  (func $s_xor (export "s_xor") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9) (i32.xor (i32.eq (local.get $a) (local.get $b)) (i32.lt_s (local.get $c) (local.get $d)))))
  (func $b_xor (export "b_xor") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (if (i32.xor (i32.eq (local.get $a) (local.get $b)) (i32.lt_s (local.get $c) (local.get $d)))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  (func $v_xor (export "v_xor") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (i32.xor (i32.eq (local.get $a) (local.get $b)) (i32.lt_s (local.get $c) (local.get $d))))
  ;; two selects on one chain, a sub between them: both read its flags, and no cset
  (func $two (export "two") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (local $t i32)
    (local.set $t (i32.and (i32.eq (local.get $a) (local.get $b)) (i32.eqz (local.get $c))))
    (i32.add (select (local.get $a) (i32.const 7) (local.get $t))
             (select (i32.sub (local.get $b) (i32.const 9)) (i32.const 11) (local.get $t))))
  ;; two selects on one chain, a compare between them: the second tests the cset
  (func $clob (export "clob") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (local $t i32)
    (local.set $t (i32.and (i32.eq (local.get $a) (local.get $b)) (i32.eqz (local.get $c))))
    (i32.add (select (local.get $a) (i32.const 7) (local.get $t))
             (i32.add (i32.lt_s (local.get $b) (local.get $d))
                      (select (local.get $d) (i32.const 11) (local.get $t)))))
  ;; a compare between a chain and the select on it: the chain is made at the csel
  (func $apart (export "apart") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (local $t i32)
    (local.set $t (i32.and (i32.eq (local.get $a) (local.get $b)) (i32.eqz (local.get $c))))
    (i32.add (i32.lt_s (local.get $b) (local.get $d))
             (select (i32.const 7) (i32.const 9) (local.get $t))))
  ;; a chain that is a select's arm as well as its condition: the csel reads the flags
  (func $armsel (export "armsel") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (local $t i32)
    (local.set $t (i32.and (i32.eq (local.get $a) (local.get $b)) (i32.eqz (local.get $c))))
    (i32.add (select (local.get $t) (local.get $b) (local.get $t)) (local.get $t)))
  ;; a compare the add reads as well as the and: no chain's
  (func $shared (export "shared") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (local $t i32)
    (i32.add (i32.and (local.tee $t (i32.eq (local.get $a) (local.get $b)))
                      (i32.lt_s (local.get $c) (local.get $d)))
             (local.get $t)))
  ;; a compare made in the block before the and's: no chain's
  (func $cross (export "cross") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (local $t i32)
    (local.set $t (i32.eq (local.get $a) (local.get $b)))
    (if (i32.lt_s (local.get $c) (i32.const 0))
      (then (i32.store (i32.const 8) (local.get $c))))
    (select (i32.const 7) (i32.const 9)
            (i32.and (local.get $t) (i32.lt_s (local.get $d) (local.get $c)))))
  ;; eqz of an and of compares, which a tst takes: no chain
  (func $tstand (export "tstand") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (param $x i64) (param $y i64) (param $p f64) (param $q f64) (result i32)
    (select (i32.const 7) (i32.const 9)
            (i32.eqz (i32.and (i32.eq (local.get $a) (local.get $b))
                              (i32.lt_s (local.get $c) (local.get $d))))))
)
