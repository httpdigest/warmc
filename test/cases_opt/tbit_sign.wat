;; arm64 makes a branch on a sign a tbz or tbnz of the sign bit -- every
;; signed and unsigned compare that tests one, i32 and i64, each way round
;; -- and follows the bit a sign test or a one-bit test reads back through
;; the shifts, rotates, extensions and wraps made for the test alone, each
;; then no instruction (isel.c tbit_sign, tbit_chain).  The near misses:
;; compares one off a sign test, a bit no bit of the chain's start gives, a
;; shift by a variable, an extension something else reads or made in
;; another block.  Each branch stores 70 and the function's number at 0, or
;; 9 at 4, and returns the sum, so that no if-conversion takes it and no two
;; functions are one.
;; Made by test/gen_tbit_sign.py, which works out every answer.
;;
;; stats -O2 a64: bit_brs=52
;;
;; shape: i32_lt_s_0 \stb(n)?z\s=1
;; shape: i32_lt_s_0 \scm(p|n)\s=0
;; shape: i32_lt_s_0 \stb(n)?z\s+w\d+, #0x1f=1
;; shape: i32_le_s_m1 \stb(n)?z\s=1
;; shape: i32_le_s_m1 \scm(p|n)\s=0
;; shape: i32_le_s_m1 \stb(n)?z\s+w\d+, #0x1f=1
;; shape: i32_ge_s_0 \stb(n)?z\s=1
;; shape: i32_ge_s_0 \scm(p|n)\s=0
;; shape: i32_ge_s_0 \stb(n)?z\s+w\d+, #0x1f=1
;; shape: i32_gt_s_m1 \stb(n)?z\s=1
;; shape: i32_gt_s_m1 \scm(p|n)\s=0
;; shape: i32_gt_s_m1 \stb(n)?z\s+w\d+, #0x1f=1
;; shape: i32_ge_u_h \stb(n)?z\s=1
;; shape: i32_ge_u_h \scm(p|n)\s=0
;; shape: i32_ge_u_h \stb(n)?z\s+w\d+, #0x1f=1
;; shape: i32_gt_u_hm1 \stb(n)?z\s=1
;; shape: i32_gt_u_hm1 \scm(p|n)\s=0
;; shape: i32_gt_u_hm1 \stb(n)?z\s+w\d+, #0x1f=1
;; shape: i32_lt_u_h \stb(n)?z\s=1
;; shape: i32_lt_u_h \scm(p|n)\s=0
;; shape: i32_lt_u_h \stb(n)?z\s+w\d+, #0x1f=1
;; shape: i32_le_u_hm1 \stb(n)?z\s=1
;; shape: i32_le_u_hm1 \scm(p|n)\s=0
;; shape: i32_le_u_hm1 \stb(n)?z\s+w\d+, #0x1f=1
;; shape: i32_lt_s_miss48 \stb(n)?z\s=0
;; shape: i32_le_s_miss54 \stb(n)?z\s=0
;; shape: i32_gt_s_miss60 \stb(n)?z\s=0
;; shape: i32_ge_s_miss66 \stb(n)?z\s=0
;; shape: i32_lt_u_miss72 \stb(n)?z\s=0
;; shape: i32_ge_u_miss78 \stb(n)?z\s=0
;; shape: i32_gt_u_miss84 \stb(n)?z\s=0
;; shape: i32_le_u_miss90 \stb(n)?z\s=0
;; shape: i64_lt_s_0 \stb(n)?z\s=1
;; shape: i64_lt_s_0 \scm(p|n)\s=0
;; shape: i64_lt_s_0 \stb(n)?z\s+x\d+, #0x3f=1
;; shape: i64_le_s_m1 \stb(n)?z\s=1
;; shape: i64_le_s_m1 \scm(p|n)\s=0
;; shape: i64_le_s_m1 \stb(n)?z\s+x\d+, #0x3f=1
;; shape: i64_ge_s_0 \stb(n)?z\s=1
;; shape: i64_ge_s_0 \scm(p|n)\s=0
;; shape: i64_ge_s_0 \stb(n)?z\s+x\d+, #0x3f=1
;; shape: i64_gt_s_m1 \stb(n)?z\s=1
;; shape: i64_gt_s_m1 \scm(p|n)\s=0
;; shape: i64_gt_s_m1 \stb(n)?z\s+x\d+, #0x3f=1
;; shape: i64_ge_u_h \stb(n)?z\s=1
;; shape: i64_ge_u_h \scm(p|n)\s=0
;; shape: i64_ge_u_h \stb(n)?z\s+x\d+, #0x3f=1
;; shape: i64_gt_u_hm1 \stb(n)?z\s=1
;; shape: i64_gt_u_hm1 \scm(p|n)\s=0
;; shape: i64_gt_u_hm1 \stb(n)?z\s+x\d+, #0x3f=1
;; shape: i64_lt_u_h \stb(n)?z\s=1
;; shape: i64_lt_u_h \scm(p|n)\s=0
;; shape: i64_lt_u_h \stb(n)?z\s+x\d+, #0x3f=1
;; shape: i64_le_u_hm1 \stb(n)?z\s=1
;; shape: i64_le_u_hm1 \scm(p|n)\s=0
;; shape: i64_le_u_hm1 \stb(n)?z\s+x\d+, #0x3f=1
;; shape: i64_lt_s_miss144 \stb(n)?z\s=0
;; shape: i64_le_s_miss150 \stb(n)?z\s=0
;; shape: i64_gt_s_miss156 \stb(n)?z\s=0
;; shape: i64_ge_s_miss162 \stb(n)?z\s=0
;; shape: i64_lt_u_miss168 \stb(n)?z\s=0
;; shape: i64_ge_u_miss174 \stb(n)?z\s=0
;; shape: i64_gt_u_miss180 \stb(n)?z\s=0
;; shape: i64_le_u_miss186 \stb(n)?z\s=0
;; shape: c_ext8 \stb(n)?z\s=1
;; shape: c_ext8 \stb(n)?z\s+w\d+, #0x7\b=1
;; shape: c_ext8 \ssxtb\s=0
;; shape: c_ext16 \stb(n)?z\s=1
;; shape: c_ext16 \stb(n)?z\s+w\d+, #0xf\b=1
;; shape: c_ext16 \ssxth\s=0
;; shape: c_shl24 \stb(n)?z\s=1
;; shape: c_shl24 \stb(n)?z\s+w\d+, #0x7\b=1
;; shape: c_shl24 \slsl\s=0
;; shape: c_shl31 \stb(n)?z\s=1
;; shape: c_shl31 \stb(n)?z\s+w\d+, #0x0\b=1
;; shape: c_shl31 \slsl\s=0
;; shape: c_shl35 \stb(n)?z\s=1
;; shape: c_shl35 \stb(n)?z\s+w\d+, #0x1c\b=1
;; shape: c_shl35 \slsl\s=0
;; shape: c_rotl1 \stb(n)?z\s=1
;; shape: c_rotl1 \stb(n)?z\s+w\d+, #0x1e\b=1
;; shape: c_rotl1 \sror\s=0
;; shape: c_rotr1 \stb(n)?z\s=1
;; shape: c_rotr1 \stb(n)?z\s+w\d+, #0x0\b=1
;; shape: c_rotr1 \sror\s=0
;; shape: c_shrs4 \stb(n)?z\s=1
;; shape: c_shrs4 \stb(n)?z\s+w\d+, #0x1f\b=1
;; shape: c_shrs4 \sasr\s=0
;; shape: c_twostep \stb(n)?z\s=1
;; shape: c_twostep \stb(n)?z\s+w\d+, #0xf\b=1
;; shape: c_twostep \ssxtb\s=0
;; shape: c_unsigned \stb(n)?z\s=1
;; shape: c_unsigned \stb(n)?z\s+w\d+, #0xb\b=1
;; shape: c_unsigned \slsl\s=0
;; shape: c64_ext32 \stb(n)?z\s=1
;; shape: c64_ext32 \stb(n)?z\s+w\d+, #0x1f\b=1
;; shape: c64_ext32 \ssxtw\s=0
;; shape: c64_shl40 \stb(n)?z\s=1
;; shape: c64_shl40 \stb(n)?z\s+w\d+, #0x17\b=1
;; shape: c64_shl40 \slsl\s=0
;; shape: c64_shl20 \stb(n)?z\s=1
;; shape: c64_shl20 \stb(n)?z\s+x\d+, #0x2b\b=1
;; shape: c64_shl20 \slsl\s=0
;; shape: c64_extends \stb(n)?z\s=1
;; shape: c64_extends \stb(n)?z\s+w\d+, #0x1f\b=1
;; shape: c64_extends \ssxtw\s=0
;; shape: c_wrap \stb(n)?z\s=1
;; shape: c_wrap \stb(n)?z\s+w\d+, #0x1f\b=1
;; shape: b_shru5 \stb(n)?z\s=1
;; shape: b_shru5 \stb(n)?z\s+w\d+, #0x5\b=1
;; shape: b_shru5 \slsr\s=0
;; shape: b_shru3_4 \stb(n)?z\s=1
;; shape: b_shru3_4 \stb(n)?z\s+w\d+, #0x5\b=1
;; shape: b_shru3_4 \slsr\s=0
;; shape: b_eqz_shl \stb(n)?z\s=1
;; shape: b_eqz_shl \stb(n)?z\s+w\d+, #0x1\b=1
;; shape: b_eqz_shl \slsl\s=0
;; shape: b_shrs \stb(n)?z\s=1
;; shape: b_shrs \stb(n)?z\s+w\d+, #0x1f\b=1
;; shape: b_shrs \sasr\s=0
;; shape: b_rotr \stb(n)?z\s=1
;; shape: b_rotr \stb(n)?z\s+w\d+, #0x3\b=1
;; shape: b_rotr \sror\s=0
;; shape: b_two \stb(n)?z\s=1
;; shape: b_two \stb(n)?z\s+w\d+, #0x4\b=1
;; shape: b_two \sls(l|r)\s=0
;; shape: b64_shru40 \stb(n)?z\s=1
;; shape: b64_shru40 \stb(n)?z\s+x\d+, #0x28\b=1
;; shape: b64_shru40 \slsr\s=0
;; shape: b64_shl \stb(n)?z\s=1
;; shape: b64_shl \stb(n)?z\s+w\d+, #0x1e\b=1
;; shape: b64_shl \slsl\s=0
;; shape: b64_extends \stb(n)?z\s=1
;; shape: b64_extends \stb(n)?z\s+w\d+, #0x1f\b=1
;; shape: b64_extends \ssxtw\s=0
;; shape: b64_extendu \stb(n)?z\s=1
;; shape: b64_extendu \stb(n)?z\s+w\d+, #0x6\b=1
;; shape: b_wrap \stb(n)?z\s=1
;; shape: b_wrap \stb(n)?z\s+w\d+, #0x9\b=1
;; shape: o_shru31 \stb(n)?z\s=1
;; shape: o_shru31 \stb(n)?z\s+w\d+, #0x1f\b=1
;; shape: o_shru31 \slsr\s=0
;; shape: o_eqz_shru31 \stb(n)?z\s=1
;; shape: o_eqz_shru31 \stb(n)?z\s+w\d+, #0x1f\b=1
;; shape: o_eqz_shru31 \slsr\s=0
;; shape: o_bfx \stb(n)?z\s=1
;; shape: o_bfx \stb(n)?z\s+w\d+, #0x5\b=1
;; shape: o_bfx \subfx\s=0
;; shape: o_sbfx \stb(n)?z\s=1
;; shape: o_sbfx \stb(n)?z\s+w\d+, #0xb\b=1
;; shape: o_sbfx \ssbfx\s=0
;; shape: o_sbfx_top \stb(n)?z\s=1
;; shape: o_sbfx_top \stb(n)?z\s+w\d+, #0xb\b=1
;; shape: o_sbfx_top \ssbfx\s=0
;; shape: o_ubfx_sign \stb(n)?z\s=1
;; shape: o_xor \stb(n)?z\s=0
;; shape: o64_shru63 \stb(n)?z\s=1
;; shape: o64_shru63 \stb(n)?z\s+x\d+, #0x3f\b=1
;; shape: o64_shru63 \slsr\s=0
;; shape: n_extendu_hi \stb(n)?z\s=1
;; shape: n_extendu_hi \stb(n)?z\s+x\d+, #0x28\b=1
;; shape: n_shru_past \stb(n)?z\s=1
;; shape: n_shru_past \stb(n)?z\s+w\d+, #0x4\b=1
;; shape: n_shru_past \slsr\s=1
;; shape: n_shl_low \stb(n)?z\s=1
;; shape: n_shl_low \stb(n)?z\s+w\d+, #0x4\b=1
;; shape: n_shl_low \slsl\s=1
;; shape: n_varshift \stb(n)?z\s=1
;; shape: n_varshift \stb(n)?z\s+w\d+, #0x1f\b=1
;; shape: n_varshift \slsl\s=1
;; shape: n_twouse \stb(n)?z\s+w\d+, #0x1f\b=1
;; shape: n_twouse \ssxtb\s=1
;; shape: n_otherblock \ssxtb\s=1
;;
;; run: i32_lt_s_0 -2147483648 => i32:70
;; run: i32_lt_s_0 -2147483647 => i32:70
;; run: i32_lt_s_0 -305419896 => i32:70
;; run: i32_lt_s_0 -2 => i32:70
;; run: i32_lt_s_0 -1 => i32:70
;; run: i32_lt_s_0 0 => i32:9
;; run: i32_lt_s_0 1 => i32:9
;; run: i32_lt_s_0 5 => i32:9
;; run: i32_lt_s_0 127 => i32:9
;; run: i32_lt_s_0 128 => i32:9
;; run: i32_lt_s_0 255 => i32:9
;; run: i32_lt_s_0 32767 => i32:9
;; run: i32_lt_s_0 32768 => i32:9
;; run: i32_lt_s_0 305419896 => i32:9
;; run: i32_lt_s_0 2147483647 => i32:9
;; run: i32_le_s_m1 -2147483648 => i32:71
;; run: i32_le_s_m1 -2147483647 => i32:71
;; run: i32_le_s_m1 -305419896 => i32:71
;; run: i32_le_s_m1 -2 => i32:71
;; run: i32_le_s_m1 -1 => i32:71
;; run: i32_le_s_m1 0 => i32:9
;; run: i32_le_s_m1 1 => i32:9
;; run: i32_le_s_m1 5 => i32:9
;; run: i32_le_s_m1 127 => i32:9
;; run: i32_le_s_m1 128 => i32:9
;; run: i32_le_s_m1 255 => i32:9
;; run: i32_le_s_m1 32767 => i32:9
;; run: i32_le_s_m1 32768 => i32:9
;; run: i32_le_s_m1 305419896 => i32:9
;; run: i32_le_s_m1 2147483647 => i32:9
;; run: i32_ge_s_0 -2147483648 => i32:9
;; run: i32_ge_s_0 -2147483647 => i32:9
;; run: i32_ge_s_0 -305419896 => i32:9
;; run: i32_ge_s_0 -2 => i32:9
;; run: i32_ge_s_0 -1 => i32:9
;; run: i32_ge_s_0 0 => i32:72
;; run: i32_ge_s_0 1 => i32:72
;; run: i32_ge_s_0 5 => i32:72
;; run: i32_ge_s_0 127 => i32:72
;; run: i32_ge_s_0 128 => i32:72
;; run: i32_ge_s_0 255 => i32:72
;; run: i32_ge_s_0 32767 => i32:72
;; run: i32_ge_s_0 32768 => i32:72
;; run: i32_ge_s_0 305419896 => i32:72
;; run: i32_ge_s_0 2147483647 => i32:72
;; run: i32_gt_s_m1 -2147483648 => i32:9
;; run: i32_gt_s_m1 -2147483647 => i32:9
;; run: i32_gt_s_m1 -305419896 => i32:9
;; run: i32_gt_s_m1 -2 => i32:9
;; run: i32_gt_s_m1 -1 => i32:9
;; run: i32_gt_s_m1 0 => i32:73
;; run: i32_gt_s_m1 1 => i32:73
;; run: i32_gt_s_m1 5 => i32:73
;; run: i32_gt_s_m1 127 => i32:73
;; run: i32_gt_s_m1 128 => i32:73
;; run: i32_gt_s_m1 255 => i32:73
;; run: i32_gt_s_m1 32767 => i32:73
;; run: i32_gt_s_m1 32768 => i32:73
;; run: i32_gt_s_m1 305419896 => i32:73
;; run: i32_gt_s_m1 2147483647 => i32:73
;; run: i32_ge_u_h -2147483648 => i32:74
;; run: i32_ge_u_h -2147483647 => i32:74
;; run: i32_ge_u_h -305419896 => i32:74
;; run: i32_ge_u_h -2 => i32:74
;; run: i32_ge_u_h -1 => i32:74
;; run: i32_ge_u_h 0 => i32:9
;; run: i32_ge_u_h 1 => i32:9
;; run: i32_ge_u_h 5 => i32:9
;; run: i32_ge_u_h 127 => i32:9
;; run: i32_ge_u_h 128 => i32:9
;; run: i32_ge_u_h 255 => i32:9
;; run: i32_ge_u_h 32767 => i32:9
;; run: i32_ge_u_h 32768 => i32:9
;; run: i32_ge_u_h 305419896 => i32:9
;; run: i32_ge_u_h 2147483647 => i32:9
;; run: i32_gt_u_hm1 -2147483648 => i32:75
;; run: i32_gt_u_hm1 -2147483647 => i32:75
;; run: i32_gt_u_hm1 -305419896 => i32:75
;; run: i32_gt_u_hm1 -2 => i32:75
;; run: i32_gt_u_hm1 -1 => i32:75
;; run: i32_gt_u_hm1 0 => i32:9
;; run: i32_gt_u_hm1 1 => i32:9
;; run: i32_gt_u_hm1 5 => i32:9
;; run: i32_gt_u_hm1 127 => i32:9
;; run: i32_gt_u_hm1 128 => i32:9
;; run: i32_gt_u_hm1 255 => i32:9
;; run: i32_gt_u_hm1 32767 => i32:9
;; run: i32_gt_u_hm1 32768 => i32:9
;; run: i32_gt_u_hm1 305419896 => i32:9
;; run: i32_gt_u_hm1 2147483647 => i32:9
;; run: i32_lt_u_h -2147483648 => i32:9
;; run: i32_lt_u_h -2147483647 => i32:9
;; run: i32_lt_u_h -305419896 => i32:9
;; run: i32_lt_u_h -2 => i32:9
;; run: i32_lt_u_h -1 => i32:9
;; run: i32_lt_u_h 0 => i32:76
;; run: i32_lt_u_h 1 => i32:76
;; run: i32_lt_u_h 5 => i32:76
;; run: i32_lt_u_h 127 => i32:76
;; run: i32_lt_u_h 128 => i32:76
;; run: i32_lt_u_h 255 => i32:76
;; run: i32_lt_u_h 32767 => i32:76
;; run: i32_lt_u_h 32768 => i32:76
;; run: i32_lt_u_h 305419896 => i32:76
;; run: i32_lt_u_h 2147483647 => i32:76
;; run: i32_le_u_hm1 -2147483648 => i32:9
;; run: i32_le_u_hm1 -2147483647 => i32:9
;; run: i32_le_u_hm1 -305419896 => i32:9
;; run: i32_le_u_hm1 -2 => i32:9
;; run: i32_le_u_hm1 -1 => i32:9
;; run: i32_le_u_hm1 0 => i32:77
;; run: i32_le_u_hm1 1 => i32:77
;; run: i32_le_u_hm1 5 => i32:77
;; run: i32_le_u_hm1 127 => i32:77
;; run: i32_le_u_hm1 128 => i32:77
;; run: i32_le_u_hm1 255 => i32:77
;; run: i32_le_u_hm1 32767 => i32:77
;; run: i32_le_u_hm1 32768 => i32:77
;; run: i32_le_u_hm1 305419896 => i32:77
;; run: i32_le_u_hm1 2147483647 => i32:77
;; run: i32_lt_s_miss48 -2147483648 => i32:78
;; run: i32_lt_s_miss48 -2147483647 => i32:78
;; run: i32_lt_s_miss48 -305419896 => i32:78
;; run: i32_lt_s_miss48 -2 => i32:78
;; run: i32_lt_s_miss48 -1 => i32:78
;; run: i32_lt_s_miss48 0 => i32:78
;; run: i32_lt_s_miss48 1 => i32:9
;; run: i32_lt_s_miss48 5 => i32:9
;; run: i32_lt_s_miss48 127 => i32:9
;; run: i32_lt_s_miss48 128 => i32:9
;; run: i32_lt_s_miss48 255 => i32:9
;; run: i32_lt_s_miss48 32767 => i32:9
;; run: i32_lt_s_miss48 32768 => i32:9
;; run: i32_lt_s_miss48 305419896 => i32:9
;; run: i32_lt_s_miss48 2147483647 => i32:9
;; run: i32_le_s_miss54 -2147483648 => i32:79
;; run: i32_le_s_miss54 -2147483647 => i32:79
;; run: i32_le_s_miss54 -305419896 => i32:79
;; run: i32_le_s_miss54 -2 => i32:79
;; run: i32_le_s_miss54 -1 => i32:79
;; run: i32_le_s_miss54 0 => i32:79
;; run: i32_le_s_miss54 1 => i32:9
;; run: i32_le_s_miss54 5 => i32:9
;; run: i32_le_s_miss54 127 => i32:9
;; run: i32_le_s_miss54 128 => i32:9
;; run: i32_le_s_miss54 255 => i32:9
;; run: i32_le_s_miss54 32767 => i32:9
;; run: i32_le_s_miss54 32768 => i32:9
;; run: i32_le_s_miss54 305419896 => i32:9
;; run: i32_le_s_miss54 2147483647 => i32:9
;; run: i32_gt_s_miss60 -2147483648 => i32:9
;; run: i32_gt_s_miss60 -2147483647 => i32:9
;; run: i32_gt_s_miss60 -305419896 => i32:9
;; run: i32_gt_s_miss60 -2 => i32:9
;; run: i32_gt_s_miss60 -1 => i32:9
;; run: i32_gt_s_miss60 0 => i32:9
;; run: i32_gt_s_miss60 1 => i32:80
;; run: i32_gt_s_miss60 5 => i32:80
;; run: i32_gt_s_miss60 127 => i32:80
;; run: i32_gt_s_miss60 128 => i32:80
;; run: i32_gt_s_miss60 255 => i32:80
;; run: i32_gt_s_miss60 32767 => i32:80
;; run: i32_gt_s_miss60 32768 => i32:80
;; run: i32_gt_s_miss60 305419896 => i32:80
;; run: i32_gt_s_miss60 2147483647 => i32:80
;; run: i32_ge_s_miss66 -2147483648 => i32:9
;; run: i32_ge_s_miss66 -2147483647 => i32:9
;; run: i32_ge_s_miss66 -305419896 => i32:9
;; run: i32_ge_s_miss66 -2 => i32:81
;; run: i32_ge_s_miss66 -1 => i32:81
;; run: i32_ge_s_miss66 0 => i32:81
;; run: i32_ge_s_miss66 1 => i32:81
;; run: i32_ge_s_miss66 5 => i32:81
;; run: i32_ge_s_miss66 127 => i32:81
;; run: i32_ge_s_miss66 128 => i32:81
;; run: i32_ge_s_miss66 255 => i32:81
;; run: i32_ge_s_miss66 32767 => i32:81
;; run: i32_ge_s_miss66 32768 => i32:81
;; run: i32_ge_s_miss66 305419896 => i32:81
;; run: i32_ge_s_miss66 2147483647 => i32:81
;; run: i32_lt_u_miss72 -2147483648 => i32:82
;; run: i32_lt_u_miss72 -2147483647 => i32:9
;; run: i32_lt_u_miss72 -305419896 => i32:9
;; run: i32_lt_u_miss72 -2 => i32:9
;; run: i32_lt_u_miss72 -1 => i32:9
;; run: i32_lt_u_miss72 0 => i32:82
;; run: i32_lt_u_miss72 1 => i32:82
;; run: i32_lt_u_miss72 5 => i32:82
;; run: i32_lt_u_miss72 127 => i32:82
;; run: i32_lt_u_miss72 128 => i32:82
;; run: i32_lt_u_miss72 255 => i32:82
;; run: i32_lt_u_miss72 32767 => i32:82
;; run: i32_lt_u_miss72 32768 => i32:82
;; run: i32_lt_u_miss72 305419896 => i32:82
;; run: i32_lt_u_miss72 2147483647 => i32:82
;; run: i32_ge_u_miss78 -2147483648 => i32:83
;; run: i32_ge_u_miss78 -2147483647 => i32:83
;; run: i32_ge_u_miss78 -305419896 => i32:83
;; run: i32_ge_u_miss78 -2 => i32:83
;; run: i32_ge_u_miss78 -1 => i32:83
;; run: i32_ge_u_miss78 0 => i32:9
;; run: i32_ge_u_miss78 1 => i32:9
;; run: i32_ge_u_miss78 5 => i32:9
;; run: i32_ge_u_miss78 127 => i32:9
;; run: i32_ge_u_miss78 128 => i32:9
;; run: i32_ge_u_miss78 255 => i32:9
;; run: i32_ge_u_miss78 32767 => i32:9
;; run: i32_ge_u_miss78 32768 => i32:9
;; run: i32_ge_u_miss78 305419896 => i32:9
;; run: i32_ge_u_miss78 2147483647 => i32:83
;; run: i32_gt_u_miss84 -2147483648 => i32:9
;; run: i32_gt_u_miss84 -2147483647 => i32:84
;; run: i32_gt_u_miss84 -305419896 => i32:84
;; run: i32_gt_u_miss84 -2 => i32:84
;; run: i32_gt_u_miss84 -1 => i32:84
;; run: i32_gt_u_miss84 0 => i32:9
;; run: i32_gt_u_miss84 1 => i32:9
;; run: i32_gt_u_miss84 5 => i32:9
;; run: i32_gt_u_miss84 127 => i32:9
;; run: i32_gt_u_miss84 128 => i32:9
;; run: i32_gt_u_miss84 255 => i32:9
;; run: i32_gt_u_miss84 32767 => i32:9
;; run: i32_gt_u_miss84 32768 => i32:9
;; run: i32_gt_u_miss84 305419896 => i32:9
;; run: i32_gt_u_miss84 2147483647 => i32:9
;; run: i32_le_u_miss90 -2147483648 => i32:85
;; run: i32_le_u_miss90 -2147483647 => i32:9
;; run: i32_le_u_miss90 -305419896 => i32:9
;; run: i32_le_u_miss90 -2 => i32:9
;; run: i32_le_u_miss90 -1 => i32:9
;; run: i32_le_u_miss90 0 => i32:85
;; run: i32_le_u_miss90 1 => i32:85
;; run: i32_le_u_miss90 5 => i32:85
;; run: i32_le_u_miss90 127 => i32:85
;; run: i32_le_u_miss90 128 => i32:85
;; run: i32_le_u_miss90 255 => i32:85
;; run: i32_le_u_miss90 32767 => i32:85
;; run: i32_le_u_miss90 32768 => i32:85
;; run: i32_le_u_miss90 305419896 => i32:85
;; run: i32_le_u_miss90 2147483647 => i32:85
;; run: i64_lt_s_0 -9223372036854775808 => i32:86
;; run: i64_lt_s_0 -9223372036854775807 => i32:86
;; run: i64_lt_s_0 -1099511627776 => i32:86
;; run: i64_lt_s_0 -305419896 => i32:86
;; run: i64_lt_s_0 -2 => i32:86
;; run: i64_lt_s_0 -1 => i32:86
;; run: i64_lt_s_0 0 => i32:9
;; run: i64_lt_s_0 1 => i32:9
;; run: i64_lt_s_0 5 => i32:9
;; run: i64_lt_s_0 127 => i32:9
;; run: i64_lt_s_0 128 => i32:9
;; run: i64_lt_s_0 255 => i32:9
;; run: i64_lt_s_0 32767 => i32:9
;; run: i64_lt_s_0 32768 => i32:9
;; run: i64_lt_s_0 305419896 => i32:9
;; run: i64_lt_s_0 2147483647 => i32:9
;; run: i64_lt_s_0 2147483648 => i32:9
;; run: i64_lt_s_0 1099511627776 => i32:9
;; run: i64_lt_s_0 1099511627904 => i32:9
;; run: i64_lt_s_0 9223372036854775807 => i32:9
;; run: i64_le_s_m1 -9223372036854775808 => i32:87
;; run: i64_le_s_m1 -9223372036854775807 => i32:87
;; run: i64_le_s_m1 -1099511627776 => i32:87
;; run: i64_le_s_m1 -305419896 => i32:87
;; run: i64_le_s_m1 -2 => i32:87
;; run: i64_le_s_m1 -1 => i32:87
;; run: i64_le_s_m1 0 => i32:9
;; run: i64_le_s_m1 1 => i32:9
;; run: i64_le_s_m1 5 => i32:9
;; run: i64_le_s_m1 127 => i32:9
;; run: i64_le_s_m1 128 => i32:9
;; run: i64_le_s_m1 255 => i32:9
;; run: i64_le_s_m1 32767 => i32:9
;; run: i64_le_s_m1 32768 => i32:9
;; run: i64_le_s_m1 305419896 => i32:9
;; run: i64_le_s_m1 2147483647 => i32:9
;; run: i64_le_s_m1 2147483648 => i32:9
;; run: i64_le_s_m1 1099511627776 => i32:9
;; run: i64_le_s_m1 1099511627904 => i32:9
;; run: i64_le_s_m1 9223372036854775807 => i32:9
;; run: i64_ge_s_0 -9223372036854775808 => i32:9
;; run: i64_ge_s_0 -9223372036854775807 => i32:9
;; run: i64_ge_s_0 -1099511627776 => i32:9
;; run: i64_ge_s_0 -305419896 => i32:9
;; run: i64_ge_s_0 -2 => i32:9
;; run: i64_ge_s_0 -1 => i32:9
;; run: i64_ge_s_0 0 => i32:88
;; run: i64_ge_s_0 1 => i32:88
;; run: i64_ge_s_0 5 => i32:88
;; run: i64_ge_s_0 127 => i32:88
;; run: i64_ge_s_0 128 => i32:88
;; run: i64_ge_s_0 255 => i32:88
;; run: i64_ge_s_0 32767 => i32:88
;; run: i64_ge_s_0 32768 => i32:88
;; run: i64_ge_s_0 305419896 => i32:88
;; run: i64_ge_s_0 2147483647 => i32:88
;; run: i64_ge_s_0 2147483648 => i32:88
;; run: i64_ge_s_0 1099511627776 => i32:88
;; run: i64_ge_s_0 1099511627904 => i32:88
;; run: i64_ge_s_0 9223372036854775807 => i32:88
;; run: i64_gt_s_m1 -9223372036854775808 => i32:9
;; run: i64_gt_s_m1 -9223372036854775807 => i32:9
;; run: i64_gt_s_m1 -1099511627776 => i32:9
;; run: i64_gt_s_m1 -305419896 => i32:9
;; run: i64_gt_s_m1 -2 => i32:9
;; run: i64_gt_s_m1 -1 => i32:9
;; run: i64_gt_s_m1 0 => i32:89
;; run: i64_gt_s_m1 1 => i32:89
;; run: i64_gt_s_m1 5 => i32:89
;; run: i64_gt_s_m1 127 => i32:89
;; run: i64_gt_s_m1 128 => i32:89
;; run: i64_gt_s_m1 255 => i32:89
;; run: i64_gt_s_m1 32767 => i32:89
;; run: i64_gt_s_m1 32768 => i32:89
;; run: i64_gt_s_m1 305419896 => i32:89
;; run: i64_gt_s_m1 2147483647 => i32:89
;; run: i64_gt_s_m1 2147483648 => i32:89
;; run: i64_gt_s_m1 1099511627776 => i32:89
;; run: i64_gt_s_m1 1099511627904 => i32:89
;; run: i64_gt_s_m1 9223372036854775807 => i32:89
;; run: i64_ge_u_h -9223372036854775808 => i32:90
;; run: i64_ge_u_h -9223372036854775807 => i32:90
;; run: i64_ge_u_h -1099511627776 => i32:90
;; run: i64_ge_u_h -305419896 => i32:90
;; run: i64_ge_u_h -2 => i32:90
;; run: i64_ge_u_h -1 => i32:90
;; run: i64_ge_u_h 0 => i32:9
;; run: i64_ge_u_h 1 => i32:9
;; run: i64_ge_u_h 5 => i32:9
;; run: i64_ge_u_h 127 => i32:9
;; run: i64_ge_u_h 128 => i32:9
;; run: i64_ge_u_h 255 => i32:9
;; run: i64_ge_u_h 32767 => i32:9
;; run: i64_ge_u_h 32768 => i32:9
;; run: i64_ge_u_h 305419896 => i32:9
;; run: i64_ge_u_h 2147483647 => i32:9
;; run: i64_ge_u_h 2147483648 => i32:9
;; run: i64_ge_u_h 1099511627776 => i32:9
;; run: i64_ge_u_h 1099511627904 => i32:9
;; run: i64_ge_u_h 9223372036854775807 => i32:9
;; run: i64_gt_u_hm1 -9223372036854775808 => i32:91
;; run: i64_gt_u_hm1 -9223372036854775807 => i32:91
;; run: i64_gt_u_hm1 -1099511627776 => i32:91
;; run: i64_gt_u_hm1 -305419896 => i32:91
;; run: i64_gt_u_hm1 -2 => i32:91
;; run: i64_gt_u_hm1 -1 => i32:91
;; run: i64_gt_u_hm1 0 => i32:9
;; run: i64_gt_u_hm1 1 => i32:9
;; run: i64_gt_u_hm1 5 => i32:9
;; run: i64_gt_u_hm1 127 => i32:9
;; run: i64_gt_u_hm1 128 => i32:9
;; run: i64_gt_u_hm1 255 => i32:9
;; run: i64_gt_u_hm1 32767 => i32:9
;; run: i64_gt_u_hm1 32768 => i32:9
;; run: i64_gt_u_hm1 305419896 => i32:9
;; run: i64_gt_u_hm1 2147483647 => i32:9
;; run: i64_gt_u_hm1 2147483648 => i32:9
;; run: i64_gt_u_hm1 1099511627776 => i32:9
;; run: i64_gt_u_hm1 1099511627904 => i32:9
;; run: i64_gt_u_hm1 9223372036854775807 => i32:9
;; run: i64_lt_u_h -9223372036854775808 => i32:9
;; run: i64_lt_u_h -9223372036854775807 => i32:9
;; run: i64_lt_u_h -1099511627776 => i32:9
;; run: i64_lt_u_h -305419896 => i32:9
;; run: i64_lt_u_h -2 => i32:9
;; run: i64_lt_u_h -1 => i32:9
;; run: i64_lt_u_h 0 => i32:92
;; run: i64_lt_u_h 1 => i32:92
;; run: i64_lt_u_h 5 => i32:92
;; run: i64_lt_u_h 127 => i32:92
;; run: i64_lt_u_h 128 => i32:92
;; run: i64_lt_u_h 255 => i32:92
;; run: i64_lt_u_h 32767 => i32:92
;; run: i64_lt_u_h 32768 => i32:92
;; run: i64_lt_u_h 305419896 => i32:92
;; run: i64_lt_u_h 2147483647 => i32:92
;; run: i64_lt_u_h 2147483648 => i32:92
;; run: i64_lt_u_h 1099511627776 => i32:92
;; run: i64_lt_u_h 1099511627904 => i32:92
;; run: i64_lt_u_h 9223372036854775807 => i32:92
;; run: i64_le_u_hm1 -9223372036854775808 => i32:9
;; run: i64_le_u_hm1 -9223372036854775807 => i32:9
;; run: i64_le_u_hm1 -1099511627776 => i32:9
;; run: i64_le_u_hm1 -305419896 => i32:9
;; run: i64_le_u_hm1 -2 => i32:9
;; run: i64_le_u_hm1 -1 => i32:9
;; run: i64_le_u_hm1 0 => i32:93
;; run: i64_le_u_hm1 1 => i32:93
;; run: i64_le_u_hm1 5 => i32:93
;; run: i64_le_u_hm1 127 => i32:93
;; run: i64_le_u_hm1 128 => i32:93
;; run: i64_le_u_hm1 255 => i32:93
;; run: i64_le_u_hm1 32767 => i32:93
;; run: i64_le_u_hm1 32768 => i32:93
;; run: i64_le_u_hm1 305419896 => i32:93
;; run: i64_le_u_hm1 2147483647 => i32:93
;; run: i64_le_u_hm1 2147483648 => i32:93
;; run: i64_le_u_hm1 1099511627776 => i32:93
;; run: i64_le_u_hm1 1099511627904 => i32:93
;; run: i64_le_u_hm1 9223372036854775807 => i32:93
;; run: i64_lt_s_miss144 -9223372036854775808 => i32:94
;; run: i64_lt_s_miss144 -9223372036854775807 => i32:94
;; run: i64_lt_s_miss144 -1099511627776 => i32:94
;; run: i64_lt_s_miss144 -305419896 => i32:94
;; run: i64_lt_s_miss144 -2 => i32:94
;; run: i64_lt_s_miss144 -1 => i32:94
;; run: i64_lt_s_miss144 0 => i32:94
;; run: i64_lt_s_miss144 1 => i32:9
;; run: i64_lt_s_miss144 5 => i32:9
;; run: i64_lt_s_miss144 127 => i32:9
;; run: i64_lt_s_miss144 128 => i32:9
;; run: i64_lt_s_miss144 255 => i32:9
;; run: i64_lt_s_miss144 32767 => i32:9
;; run: i64_lt_s_miss144 32768 => i32:9
;; run: i64_lt_s_miss144 305419896 => i32:9
;; run: i64_lt_s_miss144 2147483647 => i32:9
;; run: i64_lt_s_miss144 2147483648 => i32:9
;; run: i64_lt_s_miss144 1099511627776 => i32:9
;; run: i64_lt_s_miss144 1099511627904 => i32:9
;; run: i64_lt_s_miss144 9223372036854775807 => i32:9
;; run: i64_le_s_miss150 -9223372036854775808 => i32:95
;; run: i64_le_s_miss150 -9223372036854775807 => i32:95
;; run: i64_le_s_miss150 -1099511627776 => i32:95
;; run: i64_le_s_miss150 -305419896 => i32:95
;; run: i64_le_s_miss150 -2 => i32:95
;; run: i64_le_s_miss150 -1 => i32:95
;; run: i64_le_s_miss150 0 => i32:95
;; run: i64_le_s_miss150 1 => i32:9
;; run: i64_le_s_miss150 5 => i32:9
;; run: i64_le_s_miss150 127 => i32:9
;; run: i64_le_s_miss150 128 => i32:9
;; run: i64_le_s_miss150 255 => i32:9
;; run: i64_le_s_miss150 32767 => i32:9
;; run: i64_le_s_miss150 32768 => i32:9
;; run: i64_le_s_miss150 305419896 => i32:9
;; run: i64_le_s_miss150 2147483647 => i32:9
;; run: i64_le_s_miss150 2147483648 => i32:9
;; run: i64_le_s_miss150 1099511627776 => i32:9
;; run: i64_le_s_miss150 1099511627904 => i32:9
;; run: i64_le_s_miss150 9223372036854775807 => i32:9
;; run: i64_gt_s_miss156 -9223372036854775808 => i32:9
;; run: i64_gt_s_miss156 -9223372036854775807 => i32:9
;; run: i64_gt_s_miss156 -1099511627776 => i32:9
;; run: i64_gt_s_miss156 -305419896 => i32:9
;; run: i64_gt_s_miss156 -2 => i32:9
;; run: i64_gt_s_miss156 -1 => i32:9
;; run: i64_gt_s_miss156 0 => i32:9
;; run: i64_gt_s_miss156 1 => i32:96
;; run: i64_gt_s_miss156 5 => i32:96
;; run: i64_gt_s_miss156 127 => i32:96
;; run: i64_gt_s_miss156 128 => i32:96
;; run: i64_gt_s_miss156 255 => i32:96
;; run: i64_gt_s_miss156 32767 => i32:96
;; run: i64_gt_s_miss156 32768 => i32:96
;; run: i64_gt_s_miss156 305419896 => i32:96
;; run: i64_gt_s_miss156 2147483647 => i32:96
;; run: i64_gt_s_miss156 2147483648 => i32:96
;; run: i64_gt_s_miss156 1099511627776 => i32:96
;; run: i64_gt_s_miss156 1099511627904 => i32:96
;; run: i64_gt_s_miss156 9223372036854775807 => i32:96
;; run: i64_ge_s_miss162 -9223372036854775808 => i32:9
;; run: i64_ge_s_miss162 -9223372036854775807 => i32:9
;; run: i64_ge_s_miss162 -1099511627776 => i32:9
;; run: i64_ge_s_miss162 -305419896 => i32:9
;; run: i64_ge_s_miss162 -2 => i32:97
;; run: i64_ge_s_miss162 -1 => i32:97
;; run: i64_ge_s_miss162 0 => i32:97
;; run: i64_ge_s_miss162 1 => i32:97
;; run: i64_ge_s_miss162 5 => i32:97
;; run: i64_ge_s_miss162 127 => i32:97
;; run: i64_ge_s_miss162 128 => i32:97
;; run: i64_ge_s_miss162 255 => i32:97
;; run: i64_ge_s_miss162 32767 => i32:97
;; run: i64_ge_s_miss162 32768 => i32:97
;; run: i64_ge_s_miss162 305419896 => i32:97
;; run: i64_ge_s_miss162 2147483647 => i32:97
;; run: i64_ge_s_miss162 2147483648 => i32:97
;; run: i64_ge_s_miss162 1099511627776 => i32:97
;; run: i64_ge_s_miss162 1099511627904 => i32:97
;; run: i64_ge_s_miss162 9223372036854775807 => i32:97
;; run: i64_lt_u_miss168 -9223372036854775808 => i32:98
;; run: i64_lt_u_miss168 -9223372036854775807 => i32:9
;; run: i64_lt_u_miss168 -1099511627776 => i32:9
;; run: i64_lt_u_miss168 -305419896 => i32:9
;; run: i64_lt_u_miss168 -2 => i32:9
;; run: i64_lt_u_miss168 -1 => i32:9
;; run: i64_lt_u_miss168 0 => i32:98
;; run: i64_lt_u_miss168 1 => i32:98
;; run: i64_lt_u_miss168 5 => i32:98
;; run: i64_lt_u_miss168 127 => i32:98
;; run: i64_lt_u_miss168 128 => i32:98
;; run: i64_lt_u_miss168 255 => i32:98
;; run: i64_lt_u_miss168 32767 => i32:98
;; run: i64_lt_u_miss168 32768 => i32:98
;; run: i64_lt_u_miss168 305419896 => i32:98
;; run: i64_lt_u_miss168 2147483647 => i32:98
;; run: i64_lt_u_miss168 2147483648 => i32:98
;; run: i64_lt_u_miss168 1099511627776 => i32:98
;; run: i64_lt_u_miss168 1099511627904 => i32:98
;; run: i64_lt_u_miss168 9223372036854775807 => i32:98
;; run: i64_ge_u_miss174 -9223372036854775808 => i32:99
;; run: i64_ge_u_miss174 -9223372036854775807 => i32:99
;; run: i64_ge_u_miss174 -1099511627776 => i32:99
;; run: i64_ge_u_miss174 -305419896 => i32:99
;; run: i64_ge_u_miss174 -2 => i32:99
;; run: i64_ge_u_miss174 -1 => i32:99
;; run: i64_ge_u_miss174 0 => i32:9
;; run: i64_ge_u_miss174 1 => i32:9
;; run: i64_ge_u_miss174 5 => i32:9
;; run: i64_ge_u_miss174 127 => i32:9
;; run: i64_ge_u_miss174 128 => i32:9
;; run: i64_ge_u_miss174 255 => i32:9
;; run: i64_ge_u_miss174 32767 => i32:9
;; run: i64_ge_u_miss174 32768 => i32:9
;; run: i64_ge_u_miss174 305419896 => i32:9
;; run: i64_ge_u_miss174 2147483647 => i32:9
;; run: i64_ge_u_miss174 2147483648 => i32:9
;; run: i64_ge_u_miss174 1099511627776 => i32:9
;; run: i64_ge_u_miss174 1099511627904 => i32:9
;; run: i64_ge_u_miss174 9223372036854775807 => i32:99
;; run: i64_gt_u_miss180 -9223372036854775808 => i32:9
;; run: i64_gt_u_miss180 -9223372036854775807 => i32:100
;; run: i64_gt_u_miss180 -1099511627776 => i32:100
;; run: i64_gt_u_miss180 -305419896 => i32:100
;; run: i64_gt_u_miss180 -2 => i32:100
;; run: i64_gt_u_miss180 -1 => i32:100
;; run: i64_gt_u_miss180 0 => i32:9
;; run: i64_gt_u_miss180 1 => i32:9
;; run: i64_gt_u_miss180 5 => i32:9
;; run: i64_gt_u_miss180 127 => i32:9
;; run: i64_gt_u_miss180 128 => i32:9
;; run: i64_gt_u_miss180 255 => i32:9
;; run: i64_gt_u_miss180 32767 => i32:9
;; run: i64_gt_u_miss180 32768 => i32:9
;; run: i64_gt_u_miss180 305419896 => i32:9
;; run: i64_gt_u_miss180 2147483647 => i32:9
;; run: i64_gt_u_miss180 2147483648 => i32:9
;; run: i64_gt_u_miss180 1099511627776 => i32:9
;; run: i64_gt_u_miss180 1099511627904 => i32:9
;; run: i64_gt_u_miss180 9223372036854775807 => i32:9
;; run: i64_le_u_miss186 -9223372036854775808 => i32:101
;; run: i64_le_u_miss186 -9223372036854775807 => i32:9
;; run: i64_le_u_miss186 -1099511627776 => i32:9
;; run: i64_le_u_miss186 -305419896 => i32:9
;; run: i64_le_u_miss186 -2 => i32:9
;; run: i64_le_u_miss186 -1 => i32:9
;; run: i64_le_u_miss186 0 => i32:101
;; run: i64_le_u_miss186 1 => i32:101
;; run: i64_le_u_miss186 5 => i32:101
;; run: i64_le_u_miss186 127 => i32:101
;; run: i64_le_u_miss186 128 => i32:101
;; run: i64_le_u_miss186 255 => i32:101
;; run: i64_le_u_miss186 32767 => i32:101
;; run: i64_le_u_miss186 32768 => i32:101
;; run: i64_le_u_miss186 305419896 => i32:101
;; run: i64_le_u_miss186 2147483647 => i32:101
;; run: i64_le_u_miss186 2147483648 => i32:101
;; run: i64_le_u_miss186 1099511627776 => i32:101
;; run: i64_le_u_miss186 1099511627904 => i32:101
;; run: i64_le_u_miss186 9223372036854775807 => i32:101
;; run: c_ext8 -2147483648 => i32:9
;; run: c_ext8 -2147483647 => i32:9
;; run: c_ext8 -305419896 => i32:102
;; run: c_ext8 -2 => i32:102
;; run: c_ext8 -1 => i32:102
;; run: c_ext8 0 => i32:9
;; run: c_ext8 1 => i32:9
;; run: c_ext8 5 => i32:9
;; run: c_ext8 127 => i32:9
;; run: c_ext8 128 => i32:102
;; run: c_ext8 255 => i32:102
;; run: c_ext8 32767 => i32:102
;; run: c_ext8 32768 => i32:9
;; run: c_ext8 305419896 => i32:9
;; run: c_ext8 2147483647 => i32:102
;; run: c_ext16 -2147483648 => i32:103
;; run: c_ext16 -2147483647 => i32:103
;; run: c_ext16 -305419896 => i32:9
;; run: c_ext16 -2 => i32:9
;; run: c_ext16 -1 => i32:9
;; run: c_ext16 0 => i32:103
;; run: c_ext16 1 => i32:103
;; run: c_ext16 5 => i32:103
;; run: c_ext16 127 => i32:103
;; run: c_ext16 128 => i32:103
;; run: c_ext16 255 => i32:103
;; run: c_ext16 32767 => i32:103
;; run: c_ext16 32768 => i32:9
;; run: c_ext16 305419896 => i32:103
;; run: c_ext16 2147483647 => i32:9
;; run: c_shl24 -2147483648 => i32:9
;; run: c_shl24 -2147483647 => i32:9
;; run: c_shl24 -305419896 => i32:104
;; run: c_shl24 -2 => i32:104
;; run: c_shl24 -1 => i32:104
;; run: c_shl24 0 => i32:9
;; run: c_shl24 1 => i32:9
;; run: c_shl24 5 => i32:9
;; run: c_shl24 127 => i32:9
;; run: c_shl24 128 => i32:104
;; run: c_shl24 255 => i32:104
;; run: c_shl24 32767 => i32:104
;; run: c_shl24 32768 => i32:9
;; run: c_shl24 305419896 => i32:9
;; run: c_shl24 2147483647 => i32:104
;; run: c_shl31 -2147483648 => i32:9
;; run: c_shl31 -2147483647 => i32:105
;; run: c_shl31 -305419896 => i32:9
;; run: c_shl31 -2 => i32:9
;; run: c_shl31 -1 => i32:105
;; run: c_shl31 0 => i32:9
;; run: c_shl31 1 => i32:105
;; run: c_shl31 5 => i32:105
;; run: c_shl31 127 => i32:105
;; run: c_shl31 128 => i32:9
;; run: c_shl31 255 => i32:105
;; run: c_shl31 32767 => i32:105
;; run: c_shl31 32768 => i32:9
;; run: c_shl31 305419896 => i32:9
;; run: c_shl31 2147483647 => i32:105
;; run: c_shl35 -2147483648 => i32:9
;; run: c_shl35 -2147483647 => i32:9
;; run: c_shl35 -305419896 => i32:9
;; run: c_shl35 -2 => i32:106
;; run: c_shl35 -1 => i32:106
;; run: c_shl35 0 => i32:9
;; run: c_shl35 1 => i32:9
;; run: c_shl35 5 => i32:9
;; run: c_shl35 127 => i32:9
;; run: c_shl35 128 => i32:9
;; run: c_shl35 255 => i32:9
;; run: c_shl35 32767 => i32:9
;; run: c_shl35 32768 => i32:9
;; run: c_shl35 305419896 => i32:106
;; run: c_shl35 2147483647 => i32:106
;; run: c_rotl1 -2147483648 => i32:9
;; run: c_rotl1 -2147483647 => i32:9
;; run: c_rotl1 -305419896 => i32:107
;; run: c_rotl1 -2 => i32:107
;; run: c_rotl1 -1 => i32:107
;; run: c_rotl1 0 => i32:9
;; run: c_rotl1 1 => i32:9
;; run: c_rotl1 5 => i32:9
;; run: c_rotl1 127 => i32:9
;; run: c_rotl1 128 => i32:9
;; run: c_rotl1 255 => i32:9
;; run: c_rotl1 32767 => i32:9
;; run: c_rotl1 32768 => i32:9
;; run: c_rotl1 305419896 => i32:9
;; run: c_rotl1 2147483647 => i32:107
;; run: c_rotr1 -2147483648 => i32:9
;; run: c_rotr1 -2147483647 => i32:108
;; run: c_rotr1 -305419896 => i32:9
;; run: c_rotr1 -2 => i32:9
;; run: c_rotr1 -1 => i32:108
;; run: c_rotr1 0 => i32:9
;; run: c_rotr1 1 => i32:108
;; run: c_rotr1 5 => i32:108
;; run: c_rotr1 127 => i32:108
;; run: c_rotr1 128 => i32:9
;; run: c_rotr1 255 => i32:108
;; run: c_rotr1 32767 => i32:108
;; run: c_rotr1 32768 => i32:9
;; run: c_rotr1 305419896 => i32:9
;; run: c_rotr1 2147483647 => i32:108
;; run: c_shrs4 -2147483648 => i32:109
;; run: c_shrs4 -2147483647 => i32:109
;; run: c_shrs4 -305419896 => i32:109
;; run: c_shrs4 -2 => i32:109
;; run: c_shrs4 -1 => i32:109
;; run: c_shrs4 0 => i32:9
;; run: c_shrs4 1 => i32:9
;; run: c_shrs4 5 => i32:9
;; run: c_shrs4 127 => i32:9
;; run: c_shrs4 128 => i32:9
;; run: c_shrs4 255 => i32:9
;; run: c_shrs4 32767 => i32:9
;; run: c_shrs4 32768 => i32:9
;; run: c_shrs4 305419896 => i32:9
;; run: c_shrs4 2147483647 => i32:9
;; run: c_twostep -2147483648 => i32:9
;; run: c_twostep -2147483647 => i32:9
;; run: c_twostep -305419896 => i32:110
;; run: c_twostep -2 => i32:110
;; run: c_twostep -1 => i32:110
;; run: c_twostep 0 => i32:9
;; run: c_twostep 1 => i32:9
;; run: c_twostep 5 => i32:9
;; run: c_twostep 127 => i32:9
;; run: c_twostep 128 => i32:9
;; run: c_twostep 255 => i32:9
;; run: c_twostep 32767 => i32:9
;; run: c_twostep 32768 => i32:110
;; run: c_twostep 305419896 => i32:9
;; run: c_twostep 2147483647 => i32:110
;; run: c_unsigned -2147483648 => i32:9
;; run: c_unsigned -2147483647 => i32:9
;; run: c_unsigned -305419896 => i32:111
;; run: c_unsigned -2 => i32:111
;; run: c_unsigned -1 => i32:111
;; run: c_unsigned 0 => i32:9
;; run: c_unsigned 1 => i32:9
;; run: c_unsigned 5 => i32:9
;; run: c_unsigned 127 => i32:9
;; run: c_unsigned 128 => i32:9
;; run: c_unsigned 255 => i32:9
;; run: c_unsigned 32767 => i32:111
;; run: c_unsigned 32768 => i32:9
;; run: c_unsigned 305419896 => i32:9
;; run: c_unsigned 2147483647 => i32:111
;; run: c64_ext32 -9223372036854775808 => i32:9
;; run: c64_ext32 -9223372036854775807 => i32:9
;; run: c64_ext32 -1099511627776 => i32:9
;; run: c64_ext32 -305419896 => i32:112
;; run: c64_ext32 -2 => i32:112
;; run: c64_ext32 -1 => i32:112
;; run: c64_ext32 0 => i32:9
;; run: c64_ext32 1 => i32:9
;; run: c64_ext32 5 => i32:9
;; run: c64_ext32 127 => i32:9
;; run: c64_ext32 128 => i32:9
;; run: c64_ext32 255 => i32:9
;; run: c64_ext32 32767 => i32:9
;; run: c64_ext32 32768 => i32:9
;; run: c64_ext32 305419896 => i32:9
;; run: c64_ext32 2147483647 => i32:9
;; run: c64_ext32 2147483648 => i32:112
;; run: c64_ext32 1099511627776 => i32:9
;; run: c64_ext32 1099511627904 => i32:9
;; run: c64_ext32 9223372036854775807 => i32:112
;; run: c64_shl40 -9223372036854775808 => i32:9
;; run: c64_shl40 -9223372036854775807 => i32:9
;; run: c64_shl40 -1099511627776 => i32:9
;; run: c64_shl40 -305419896 => i32:113
;; run: c64_shl40 -2 => i32:113
;; run: c64_shl40 -1 => i32:113
;; run: c64_shl40 0 => i32:9
;; run: c64_shl40 1 => i32:9
;; run: c64_shl40 5 => i32:9
;; run: c64_shl40 127 => i32:9
;; run: c64_shl40 128 => i32:9
;; run: c64_shl40 255 => i32:9
;; run: c64_shl40 32767 => i32:9
;; run: c64_shl40 32768 => i32:9
;; run: c64_shl40 305419896 => i32:9
;; run: c64_shl40 2147483647 => i32:113
;; run: c64_shl40 2147483648 => i32:9
;; run: c64_shl40 1099511627776 => i32:9
;; run: c64_shl40 1099511627904 => i32:9
;; run: c64_shl40 9223372036854775807 => i32:113
;; run: c64_shl20 -9223372036854775808 => i32:114
;; run: c64_shl20 -9223372036854775807 => i32:114
;; run: c64_shl20 -1099511627776 => i32:9
;; run: c64_shl20 -305419896 => i32:9
;; run: c64_shl20 -2 => i32:9
;; run: c64_shl20 -1 => i32:9
;; run: c64_shl20 0 => i32:114
;; run: c64_shl20 1 => i32:114
;; run: c64_shl20 5 => i32:114
;; run: c64_shl20 127 => i32:114
;; run: c64_shl20 128 => i32:114
;; run: c64_shl20 255 => i32:114
;; run: c64_shl20 32767 => i32:114
;; run: c64_shl20 32768 => i32:114
;; run: c64_shl20 305419896 => i32:114
;; run: c64_shl20 2147483647 => i32:114
;; run: c64_shl20 2147483648 => i32:114
;; run: c64_shl20 1099511627776 => i32:114
;; run: c64_shl20 1099511627904 => i32:114
;; run: c64_shl20 9223372036854775807 => i32:9
;; run: c64_extends -2147483648 => i32:115
;; run: c64_extends -2147483647 => i32:115
;; run: c64_extends -305419896 => i32:115
;; run: c64_extends -2 => i32:115
;; run: c64_extends -1 => i32:115
;; run: c64_extends 0 => i32:9
;; run: c64_extends 1 => i32:9
;; run: c64_extends 5 => i32:9
;; run: c64_extends 127 => i32:9
;; run: c64_extends 128 => i32:9
;; run: c64_extends 255 => i32:9
;; run: c64_extends 32767 => i32:9
;; run: c64_extends 32768 => i32:9
;; run: c64_extends 305419896 => i32:9
;; run: c64_extends 2147483647 => i32:9
;; run: c_wrap -9223372036854775808 => i32:9
;; run: c_wrap -9223372036854775807 => i32:9
;; run: c_wrap -1099511627776 => i32:9
;; run: c_wrap -305419896 => i32:116
;; run: c_wrap -2 => i32:116
;; run: c_wrap -1 => i32:116
;; run: c_wrap 0 => i32:9
;; run: c_wrap 1 => i32:9
;; run: c_wrap 5 => i32:9
;; run: c_wrap 127 => i32:9
;; run: c_wrap 128 => i32:9
;; run: c_wrap 255 => i32:9
;; run: c_wrap 32767 => i32:9
;; run: c_wrap 32768 => i32:9
;; run: c_wrap 305419896 => i32:9
;; run: c_wrap 2147483647 => i32:9
;; run: c_wrap 2147483648 => i32:116
;; run: c_wrap 1099511627776 => i32:9
;; run: c_wrap 1099511627904 => i32:9
;; run: c_wrap 9223372036854775807 => i32:116
;; run: b_shru5 -2147483648 => i32:9
;; run: b_shru5 -2147483647 => i32:9
;; run: b_shru5 -305419896 => i32:9
;; run: b_shru5 -2 => i32:117
;; run: b_shru5 -1 => i32:117
;; run: b_shru5 0 => i32:9
;; run: b_shru5 1 => i32:9
;; run: b_shru5 5 => i32:9
;; run: b_shru5 127 => i32:117
;; run: b_shru5 128 => i32:9
;; run: b_shru5 255 => i32:117
;; run: b_shru5 32767 => i32:117
;; run: b_shru5 32768 => i32:9
;; run: b_shru5 305419896 => i32:117
;; run: b_shru5 2147483647 => i32:117
;; run: b_shru3_4 -2147483648 => i32:9
;; run: b_shru3_4 -2147483647 => i32:9
;; run: b_shru3_4 -305419896 => i32:9
;; run: b_shru3_4 -2 => i32:118
;; run: b_shru3_4 -1 => i32:118
;; run: b_shru3_4 0 => i32:9
;; run: b_shru3_4 1 => i32:9
;; run: b_shru3_4 5 => i32:9
;; run: b_shru3_4 127 => i32:118
;; run: b_shru3_4 128 => i32:9
;; run: b_shru3_4 255 => i32:118
;; run: b_shru3_4 32767 => i32:118
;; run: b_shru3_4 32768 => i32:9
;; run: b_shru3_4 305419896 => i32:118
;; run: b_shru3_4 2147483647 => i32:118
;; run: b_eqz_shl -2147483648 => i32:119
;; run: b_eqz_shl -2147483647 => i32:119
;; run: b_eqz_shl -305419896 => i32:119
;; run: b_eqz_shl -2 => i32:9
;; run: b_eqz_shl -1 => i32:9
;; run: b_eqz_shl 0 => i32:119
;; run: b_eqz_shl 1 => i32:119
;; run: b_eqz_shl 5 => i32:119
;; run: b_eqz_shl 127 => i32:9
;; run: b_eqz_shl 128 => i32:119
;; run: b_eqz_shl 255 => i32:9
;; run: b_eqz_shl 32767 => i32:9
;; run: b_eqz_shl 32768 => i32:119
;; run: b_eqz_shl 305419896 => i32:119
;; run: b_eqz_shl 2147483647 => i32:9
;; run: b_shrs -2147483648 => i32:120
;; run: b_shrs -2147483647 => i32:120
;; run: b_shrs -305419896 => i32:120
;; run: b_shrs -2 => i32:120
;; run: b_shrs -1 => i32:120
;; run: b_shrs 0 => i32:9
;; run: b_shrs 1 => i32:9
;; run: b_shrs 5 => i32:9
;; run: b_shrs 127 => i32:9
;; run: b_shrs 128 => i32:9
;; run: b_shrs 255 => i32:9
;; run: b_shrs 32767 => i32:9
;; run: b_shrs 32768 => i32:9
;; run: b_shrs 305419896 => i32:9
;; run: b_shrs 2147483647 => i32:9
;; run: b_rotr -2147483648 => i32:9
;; run: b_rotr -2147483647 => i32:9
;; run: b_rotr -305419896 => i32:121
;; run: b_rotr -2 => i32:121
;; run: b_rotr -1 => i32:121
;; run: b_rotr 0 => i32:9
;; run: b_rotr 1 => i32:9
;; run: b_rotr 5 => i32:9
;; run: b_rotr 127 => i32:121
;; run: b_rotr 128 => i32:9
;; run: b_rotr 255 => i32:121
;; run: b_rotr 32767 => i32:121
;; run: b_rotr 32768 => i32:9
;; run: b_rotr 305419896 => i32:121
;; run: b_rotr 2147483647 => i32:121
;; run: b_two -2147483648 => i32:9
;; run: b_two -2147483647 => i32:9
;; run: b_two -305419896 => i32:9
;; run: b_two -2 => i32:122
;; run: b_two -1 => i32:122
;; run: b_two 0 => i32:9
;; run: b_two 1 => i32:9
;; run: b_two 5 => i32:9
;; run: b_two 127 => i32:122
;; run: b_two 128 => i32:9
;; run: b_two 255 => i32:122
;; run: b_two 32767 => i32:122
;; run: b_two 32768 => i32:9
;; run: b_two 305419896 => i32:122
;; run: b_two 2147483647 => i32:122
;; run: b64_shru40 -9223372036854775808 => i32:9
;; run: b64_shru40 -9223372036854775807 => i32:9
;; run: b64_shru40 -1099511627776 => i32:123
;; run: b64_shru40 -305419896 => i32:123
;; run: b64_shru40 -2 => i32:123
;; run: b64_shru40 -1 => i32:123
;; run: b64_shru40 0 => i32:9
;; run: b64_shru40 1 => i32:9
;; run: b64_shru40 5 => i32:9
;; run: b64_shru40 127 => i32:9
;; run: b64_shru40 128 => i32:9
;; run: b64_shru40 255 => i32:9
;; run: b64_shru40 32767 => i32:9
;; run: b64_shru40 32768 => i32:9
;; run: b64_shru40 305419896 => i32:9
;; run: b64_shru40 2147483647 => i32:9
;; run: b64_shru40 2147483648 => i32:9
;; run: b64_shru40 1099511627776 => i32:123
;; run: b64_shru40 1099511627904 => i32:123
;; run: b64_shru40 9223372036854775807 => i32:123
;; run: b64_shl -9223372036854775808 => i32:124
;; run: b64_shl -9223372036854775807 => i32:124
;; run: b64_shl -1099511627776 => i32:124
;; run: b64_shl -305419896 => i32:9
;; run: b64_shl -2 => i32:9
;; run: b64_shl -1 => i32:9
;; run: b64_shl 0 => i32:124
;; run: b64_shl 1 => i32:124
;; run: b64_shl 5 => i32:124
;; run: b64_shl 127 => i32:124
;; run: b64_shl 128 => i32:124
;; run: b64_shl 255 => i32:124
;; run: b64_shl 32767 => i32:124
;; run: b64_shl 32768 => i32:124
;; run: b64_shl 305419896 => i32:124
;; run: b64_shl 2147483647 => i32:9
;; run: b64_shl 2147483648 => i32:124
;; run: b64_shl 1099511627776 => i32:124
;; run: b64_shl 1099511627904 => i32:124
;; run: b64_shl 9223372036854775807 => i32:9
;; run: b64_extends -2147483648 => i32:125
;; run: b64_extends -2147483647 => i32:125
;; run: b64_extends -305419896 => i32:125
;; run: b64_extends -2 => i32:125
;; run: b64_extends -1 => i32:125
;; run: b64_extends 0 => i32:9
;; run: b64_extends 1 => i32:9
;; run: b64_extends 5 => i32:9
;; run: b64_extends 127 => i32:9
;; run: b64_extends 128 => i32:9
;; run: b64_extends 255 => i32:9
;; run: b64_extends 32767 => i32:9
;; run: b64_extends 32768 => i32:9
;; run: b64_extends 305419896 => i32:9
;; run: b64_extends 2147483647 => i32:9
;; run: b64_extendu -2147483648 => i32:9
;; run: b64_extendu -2147483647 => i32:9
;; run: b64_extendu -305419896 => i32:9
;; run: b64_extendu -2 => i32:126
;; run: b64_extendu -1 => i32:126
;; run: b64_extendu 0 => i32:9
;; run: b64_extendu 1 => i32:9
;; run: b64_extendu 5 => i32:9
;; run: b64_extendu 127 => i32:126
;; run: b64_extendu 128 => i32:9
;; run: b64_extendu 255 => i32:126
;; run: b64_extendu 32767 => i32:126
;; run: b64_extendu 32768 => i32:9
;; run: b64_extendu 305419896 => i32:126
;; run: b64_extendu 2147483647 => i32:126
;; run: b_wrap -9223372036854775808 => i32:9
;; run: b_wrap -9223372036854775807 => i32:9
;; run: b_wrap -1099511627776 => i32:9
;; run: b_wrap -305419896 => i32:9
;; run: b_wrap -2 => i32:127
;; run: b_wrap -1 => i32:127
;; run: b_wrap 0 => i32:9
;; run: b_wrap 1 => i32:9
;; run: b_wrap 5 => i32:9
;; run: b_wrap 127 => i32:9
;; run: b_wrap 128 => i32:9
;; run: b_wrap 255 => i32:9
;; run: b_wrap 32767 => i32:127
;; run: b_wrap 32768 => i32:9
;; run: b_wrap 305419896 => i32:127
;; run: b_wrap 2147483647 => i32:127
;; run: b_wrap 2147483648 => i32:9
;; run: b_wrap 1099511627776 => i32:9
;; run: b_wrap 1099511627904 => i32:9
;; run: b_wrap 9223372036854775807 => i32:127
;; run: o_shru31 -2147483648 => i32:128
;; run: o_shru31 -2147483647 => i32:128
;; run: o_shru31 -305419896 => i32:128
;; run: o_shru31 -2 => i32:128
;; run: o_shru31 -1 => i32:128
;; run: o_shru31 0 => i32:9
;; run: o_shru31 1 => i32:9
;; run: o_shru31 5 => i32:9
;; run: o_shru31 127 => i32:9
;; run: o_shru31 128 => i32:9
;; run: o_shru31 255 => i32:9
;; run: o_shru31 32767 => i32:9
;; run: o_shru31 32768 => i32:9
;; run: o_shru31 305419896 => i32:9
;; run: o_shru31 2147483647 => i32:9
;; run: o_eqz_shru31 -2147483648 => i32:9
;; run: o_eqz_shru31 -2147483647 => i32:9
;; run: o_eqz_shru31 -305419896 => i32:9
;; run: o_eqz_shru31 -2 => i32:9
;; run: o_eqz_shru31 -1 => i32:9
;; run: o_eqz_shru31 0 => i32:129
;; run: o_eqz_shru31 1 => i32:129
;; run: o_eqz_shru31 5 => i32:129
;; run: o_eqz_shru31 127 => i32:129
;; run: o_eqz_shru31 128 => i32:129
;; run: o_eqz_shru31 255 => i32:129
;; run: o_eqz_shru31 32767 => i32:129
;; run: o_eqz_shru31 32768 => i32:129
;; run: o_eqz_shru31 305419896 => i32:129
;; run: o_eqz_shru31 2147483647 => i32:129
;; run: o_bfx -2147483648 => i32:9
;; run: o_bfx -2147483647 => i32:9
;; run: o_bfx -305419896 => i32:9
;; run: o_bfx -2 => i32:130
;; run: o_bfx -1 => i32:130
;; run: o_bfx 0 => i32:9
;; run: o_bfx 1 => i32:9
;; run: o_bfx 5 => i32:9
;; run: o_bfx 127 => i32:130
;; run: o_bfx 128 => i32:9
;; run: o_bfx 255 => i32:130
;; run: o_bfx 32767 => i32:130
;; run: o_bfx 32768 => i32:9
;; run: o_bfx 305419896 => i32:130
;; run: o_bfx 2147483647 => i32:130
;; run: o_sbfx -2147483648 => i32:9
;; run: o_sbfx -2147483647 => i32:9
;; run: o_sbfx -305419896 => i32:131
;; run: o_sbfx -2 => i32:131
;; run: o_sbfx -1 => i32:131
;; run: o_sbfx 0 => i32:9
;; run: o_sbfx 1 => i32:9
;; run: o_sbfx 5 => i32:9
;; run: o_sbfx 127 => i32:9
;; run: o_sbfx 128 => i32:9
;; run: o_sbfx 255 => i32:9
;; run: o_sbfx 32767 => i32:131
;; run: o_sbfx 32768 => i32:9
;; run: o_sbfx 305419896 => i32:9
;; run: o_sbfx 2147483647 => i32:131
;; run: o_sbfx_top -2147483648 => i32:9
;; run: o_sbfx_top -2147483647 => i32:9
;; run: o_sbfx_top -305419896 => i32:132
;; run: o_sbfx_top -2 => i32:132
;; run: o_sbfx_top -1 => i32:132
;; run: o_sbfx_top 0 => i32:9
;; run: o_sbfx_top 1 => i32:9
;; run: o_sbfx_top 5 => i32:9
;; run: o_sbfx_top 127 => i32:9
;; run: o_sbfx_top 128 => i32:9
;; run: o_sbfx_top 255 => i32:9
;; run: o_sbfx_top 32767 => i32:132
;; run: o_sbfx_top 32768 => i32:9
;; run: o_sbfx_top 305419896 => i32:9
;; run: o_sbfx_top 2147483647 => i32:132
;; run: o_ubfx_sign -2147483648 => i32:9
;; run: o_ubfx_sign -2147483647 => i32:9
;; run: o_ubfx_sign -305419896 => i32:9
;; run: o_ubfx_sign -2 => i32:9
;; run: o_ubfx_sign -1 => i32:9
;; run: o_ubfx_sign 0 => i32:9
;; run: o_ubfx_sign 1 => i32:9
;; run: o_ubfx_sign 5 => i32:9
;; run: o_ubfx_sign 127 => i32:9
;; run: o_ubfx_sign 128 => i32:9
;; run: o_ubfx_sign 255 => i32:9
;; run: o_ubfx_sign 32767 => i32:9
;; run: o_ubfx_sign 32768 => i32:9
;; run: o_ubfx_sign 305419896 => i32:9
;; run: o_ubfx_sign 2147483647 => i32:9
;; run: o_xor -2147483648 => i32:134
;; run: o_xor -2147483647 => i32:9
;; run: o_xor -305419896 => i32:134
;; run: o_xor -2 => i32:134
;; run: o_xor -1 => i32:9
;; run: o_xor 0 => i32:134
;; run: o_xor 1 => i32:9
;; run: o_xor 5 => i32:9
;; run: o_xor 127 => i32:9
;; run: o_xor 128 => i32:134
;; run: o_xor 255 => i32:9
;; run: o_xor 32767 => i32:9
;; run: o_xor 32768 => i32:134
;; run: o_xor 305419896 => i32:134
;; run: o_xor 2147483647 => i32:9
;; run: o64_shru63 -9223372036854775808 => i32:135
;; run: o64_shru63 -9223372036854775807 => i32:135
;; run: o64_shru63 -1099511627776 => i32:135
;; run: o64_shru63 -305419896 => i32:135
;; run: o64_shru63 -2 => i32:135
;; run: o64_shru63 -1 => i32:135
;; run: o64_shru63 0 => i32:9
;; run: o64_shru63 1 => i32:9
;; run: o64_shru63 5 => i32:9
;; run: o64_shru63 127 => i32:9
;; run: o64_shru63 128 => i32:9
;; run: o64_shru63 255 => i32:9
;; run: o64_shru63 32767 => i32:9
;; run: o64_shru63 32768 => i32:9
;; run: o64_shru63 305419896 => i32:9
;; run: o64_shru63 2147483647 => i32:9
;; run: o64_shru63 2147483648 => i32:9
;; run: o64_shru63 1099511627776 => i32:9
;; run: o64_shru63 1099511627904 => i32:9
;; run: o64_shru63 9223372036854775807 => i32:9
;; run: n_extendu_hi -2147483648 => i32:9
;; run: n_extendu_hi -2147483647 => i32:9
;; run: n_extendu_hi -305419896 => i32:9
;; run: n_extendu_hi -2 => i32:9
;; run: n_extendu_hi -1 => i32:9
;; run: n_extendu_hi 0 => i32:9
;; run: n_extendu_hi 1 => i32:9
;; run: n_extendu_hi 5 => i32:9
;; run: n_extendu_hi 127 => i32:9
;; run: n_extendu_hi 128 => i32:9
;; run: n_extendu_hi 255 => i32:9
;; run: n_extendu_hi 32767 => i32:9
;; run: n_extendu_hi 32768 => i32:9
;; run: n_extendu_hi 305419896 => i32:9
;; run: n_extendu_hi 2147483647 => i32:9
;; run: n_extendu_wrap -9223372036854775808 => i32:10
;; run: n_extendu_wrap -9223372036854775807 => i32:10
;; run: n_extendu_wrap -1099511627776 => i32:10
;; run: n_extendu_wrap -305419896 => i32:10
;; run: n_extendu_wrap -2 => i32:10
;; run: n_extendu_wrap -1 => i32:10
;; run: n_extendu_wrap 0 => i32:10
;; run: n_extendu_wrap 1 => i32:10
;; run: n_extendu_wrap 5 => i32:10
;; run: n_extendu_wrap 127 => i32:10
;; run: n_extendu_wrap 128 => i32:10
;; run: n_extendu_wrap 255 => i32:10
;; run: n_extendu_wrap 32767 => i32:10
;; run: n_extendu_wrap 32768 => i32:10
;; run: n_extendu_wrap 305419896 => i32:10
;; run: n_extendu_wrap 2147483647 => i32:10
;; run: n_extendu_wrap 2147483648 => i32:10
;; run: n_extendu_wrap 1099511627776 => i32:10
;; run: n_extendu_wrap 1099511627904 => i32:10
;; run: n_extendu_wrap 9223372036854775807 => i32:10
;; run: n_shru_past -2147483648 => i32:9
;; run: n_shru_past -2147483647 => i32:9
;; run: n_shru_past -305419896 => i32:9
;; run: n_shru_past -2 => i32:9
;; run: n_shru_past -1 => i32:9
;; run: n_shru_past 0 => i32:9
;; run: n_shru_past 1 => i32:9
;; run: n_shru_past 5 => i32:9
;; run: n_shru_past 127 => i32:9
;; run: n_shru_past 128 => i32:9
;; run: n_shru_past 255 => i32:9
;; run: n_shru_past 32767 => i32:9
;; run: n_shru_past 32768 => i32:9
;; run: n_shru_past 305419896 => i32:9
;; run: n_shru_past 2147483647 => i32:9
;; run: n_shl_low -2147483648 => i32:9
;; run: n_shl_low -2147483647 => i32:9
;; run: n_shl_low -305419896 => i32:9
;; run: n_shl_low -2 => i32:9
;; run: n_shl_low -1 => i32:9
;; run: n_shl_low 0 => i32:9
;; run: n_shl_low 1 => i32:9
;; run: n_shl_low 5 => i32:9
;; run: n_shl_low 127 => i32:9
;; run: n_shl_low 128 => i32:9
;; run: n_shl_low 255 => i32:9
;; run: n_shl_low 32767 => i32:9
;; run: n_shl_low 32768 => i32:9
;; run: n_shl_low 305419896 => i32:9
;; run: n_shl_low 2147483647 => i32:9
;; run: n_varshift -2147483648 => i32:139
;; run: n_varshift -2147483647 => i32:9
;; run: n_varshift -305419896 => i32:139
;; run: n_varshift -2 => i32:139
;; run: n_varshift -1 => i32:139
;; run: n_varshift 0 => i32:9
;; run: n_varshift 1 => i32:9
;; run: n_varshift 5 => i32:9
;; run: n_varshift 127 => i32:9
;; run: n_varshift 128 => i32:9
;; run: n_varshift 255 => i32:9
;; run: n_varshift 32767 => i32:9
;; run: n_varshift 32768 => i32:9
;; run: n_varshift 305419896 => i32:9
;; run: n_varshift 2147483647 => i32:139
;; run: n_twouse -2147483648 => i32:9
;; run: n_twouse -2147483647 => i32:10
;; run: n_twouse -305419896 => i32:-50
;; run: n_twouse -2 => i32:68
;; run: n_twouse -1 => i32:69
;; run: n_twouse 0 => i32:9
;; run: n_twouse 1 => i32:10
;; run: n_twouse 5 => i32:14
;; run: n_twouse 127 => i32:136
;; run: n_twouse 128 => i32:-58
;; run: n_twouse 255 => i32:69
;; run: n_twouse 32767 => i32:69
;; run: n_twouse 32768 => i32:9
;; run: n_twouse 305419896 => i32:129
;; run: n_twouse 2147483647 => i32:69
;; run: n_otherblock -2147483648 0 => i32:9
;; run: n_otherblock -2147483648 3 => i32:9
;; run: n_otherblock -2147483647 0 => i32:9
;; run: n_otherblock -2147483647 3 => i32:9
;; run: n_otherblock -305419896 0 => i32:70
;; run: n_otherblock -305419896 3 => i32:70
;; run: n_otherblock -2 0 => i32:70
;; run: n_otherblock -2 3 => i32:70
;; run: n_otherblock -1 0 => i32:70
;; run: n_otherblock -1 3 => i32:70
;; run: n_otherblock 0 0 => i32:9
;; run: n_otherblock 0 3 => i32:9
;; run: n_otherblock 1 0 => i32:9
;; run: n_otherblock 1 3 => i32:9
;; run: n_otherblock 5 0 => i32:9
;; run: n_otherblock 5 3 => i32:9
;; run: n_otherblock 127 0 => i32:9
;; run: n_otherblock 127 3 => i32:9
;; run: n_otherblock 128 0 => i32:70
;; run: n_otherblock 128 3 => i32:70
;; run: n_otherblock 255 0 => i32:70
;; run: n_otherblock 255 3 => i32:70
;; run: n_otherblock 32767 0 => i32:70
;; run: n_otherblock 32767 3 => i32:70
;; run: n_otherblock 32768 0 => i32:9
;; run: n_otherblock 32768 3 => i32:9
;; run: n_otherblock 305419896 0 => i32:9
;; run: n_otherblock 305419896 3 => i32:9
;; run: n_otherblock 2147483647 0 => i32:70
;; run: n_otherblock 2147483647 3 => i32:70
(module
  (memory 1)
  ;; i32.lt_s against 0: a test of the sign
  (func $i32_lt_s_0 (export "i32_lt_s_0") (param $y i32) (result i32)
    (if (i32.lt_s (local.get $y) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i32.le_s against -1: a test of the sign
  (func $i32_le_s_m1 (export "i32_le_s_m1") (param $y i32) (result i32)
    (if (i32.le_s (local.get $y) (i32.const -1))
      (then (i32.store (i32.const 0) (i32.const 71)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i32.ge_s against 0: a test of the sign
  (func $i32_ge_s_0 (export "i32_ge_s_0") (param $y i32) (result i32)
    (if (i32.ge_s (local.get $y) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 72)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i32.gt_s against -1: a test of the sign
  (func $i32_gt_s_m1 (export "i32_gt_s_m1") (param $y i32) (result i32)
    (if (i32.gt_s (local.get $y) (i32.const -1))
      (then (i32.store (i32.const 0) (i32.const 73)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i32.ge_u against -2147483648: a test of the sign
  (func $i32_ge_u_h (export "i32_ge_u_h") (param $y i32) (result i32)
    (if (i32.ge_u (local.get $y) (i32.const -2147483648))
      (then (i32.store (i32.const 0) (i32.const 74)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i32.gt_u against 2147483647: a test of the sign
  (func $i32_gt_u_hm1 (export "i32_gt_u_hm1") (param $y i32) (result i32)
    (if (i32.gt_u (local.get $y) (i32.const 2147483647))
      (then (i32.store (i32.const 0) (i32.const 75)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i32.lt_u against -2147483648: a test of the sign
  (func $i32_lt_u_h (export "i32_lt_u_h") (param $y i32) (result i32)
    (if (i32.lt_u (local.get $y) (i32.const -2147483648))
      (then (i32.store (i32.const 0) (i32.const 76)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i32.le_u against 2147483647: a test of the sign
  (func $i32_le_u_hm1 (export "i32_le_u_hm1") (param $y i32) (result i32)
    (if (i32.le_u (local.get $y) (i32.const 2147483647))
      (then (i32.store (i32.const 0) (i32.const 77)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i32.lt_s against 1: not a sign
  (func $i32_lt_s_miss48 (export "i32_lt_s_miss48") (param $y i32) (result i32)
    (if (i32.lt_s (local.get $y) (i32.const 1))
      (then (i32.store (i32.const 0) (i32.const 78)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i32.le_s against 0: not a sign
  (func $i32_le_s_miss54 (export "i32_le_s_miss54") (param $y i32) (result i32)
    (if (i32.le_s (local.get $y) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 79)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i32.gt_s against 0: not a sign
  (func $i32_gt_s_miss60 (export "i32_gt_s_miss60") (param $y i32) (result i32)
    (if (i32.gt_s (local.get $y) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 80)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i32.ge_s against -2: not a sign
  (func $i32_ge_s_miss66 (export "i32_ge_s_miss66") (param $y i32) (result i32)
    (if (i32.ge_s (local.get $y) (i32.const -2))
      (then (i32.store (i32.const 0) (i32.const 81)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i32.lt_u against -2147483647: not a sign
  (func $i32_lt_u_miss72 (export "i32_lt_u_miss72") (param $y i32) (result i32)
    (if (i32.lt_u (local.get $y) (i32.const -2147483647))
      (then (i32.store (i32.const 0) (i32.const 82)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i32.ge_u against 2147483647: not a sign
  (func $i32_ge_u_miss78 (export "i32_ge_u_miss78") (param $y i32) (result i32)
    (if (i32.ge_u (local.get $y) (i32.const 2147483647))
      (then (i32.store (i32.const 0) (i32.const 83)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i32.gt_u against -2147483648: not a sign
  (func $i32_gt_u_miss84 (export "i32_gt_u_miss84") (param $y i32) (result i32)
    (if (i32.gt_u (local.get $y) (i32.const -2147483648))
      (then (i32.store (i32.const 0) (i32.const 84)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i32.le_u against -2147483648: not a sign
  (func $i32_le_u_miss90 (export "i32_le_u_miss90") (param $y i32) (result i32)
    (if (i32.le_u (local.get $y) (i32.const -2147483648))
      (then (i32.store (i32.const 0) (i32.const 85)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i64.lt_s against 0: a test of the sign
  (func $i64_lt_s_0 (export "i64_lt_s_0") (param $y i64) (result i32)
    (if (i64.lt_s (local.get $y) (i64.const 0))
      (then (i32.store (i32.const 0) (i32.const 86)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i64.le_s against -1: a test of the sign
  (func $i64_le_s_m1 (export "i64_le_s_m1") (param $y i64) (result i32)
    (if (i64.le_s (local.get $y) (i64.const -1))
      (then (i32.store (i32.const 0) (i32.const 87)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i64.ge_s against 0: a test of the sign
  (func $i64_ge_s_0 (export "i64_ge_s_0") (param $y i64) (result i32)
    (if (i64.ge_s (local.get $y) (i64.const 0))
      (then (i32.store (i32.const 0) (i32.const 88)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i64.gt_s against -1: a test of the sign
  (func $i64_gt_s_m1 (export "i64_gt_s_m1") (param $y i64) (result i32)
    (if (i64.gt_s (local.get $y) (i64.const -1))
      (then (i32.store (i32.const 0) (i32.const 89)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i64.ge_u against -9223372036854775808: a test of the sign
  (func $i64_ge_u_h (export "i64_ge_u_h") (param $y i64) (result i32)
    (if (i64.ge_u (local.get $y) (i64.const -9223372036854775808))
      (then (i32.store (i32.const 0) (i32.const 90)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i64.gt_u against 9223372036854775807: a test of the sign
  (func $i64_gt_u_hm1 (export "i64_gt_u_hm1") (param $y i64) (result i32)
    (if (i64.gt_u (local.get $y) (i64.const 9223372036854775807))
      (then (i32.store (i32.const 0) (i32.const 91)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i64.lt_u against -9223372036854775808: a test of the sign
  (func $i64_lt_u_h (export "i64_lt_u_h") (param $y i64) (result i32)
    (if (i64.lt_u (local.get $y) (i64.const -9223372036854775808))
      (then (i32.store (i32.const 0) (i32.const 92)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i64.le_u against 9223372036854775807: a test of the sign
  (func $i64_le_u_hm1 (export "i64_le_u_hm1") (param $y i64) (result i32)
    (if (i64.le_u (local.get $y) (i64.const 9223372036854775807))
      (then (i32.store (i32.const 0) (i32.const 93)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i64.lt_s against 1: not a sign
  (func $i64_lt_s_miss144 (export "i64_lt_s_miss144") (param $y i64) (result i32)
    (if (i64.lt_s (local.get $y) (i64.const 1))
      (then (i32.store (i32.const 0) (i32.const 94)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i64.le_s against 0: not a sign
  (func $i64_le_s_miss150 (export "i64_le_s_miss150") (param $y i64) (result i32)
    (if (i64.le_s (local.get $y) (i64.const 0))
      (then (i32.store (i32.const 0) (i32.const 95)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i64.gt_s against 0: not a sign
  (func $i64_gt_s_miss156 (export "i64_gt_s_miss156") (param $y i64) (result i32)
    (if (i64.gt_s (local.get $y) (i64.const 0))
      (then (i32.store (i32.const 0) (i32.const 96)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i64.ge_s against -2: not a sign
  (func $i64_ge_s_miss162 (export "i64_ge_s_miss162") (param $y i64) (result i32)
    (if (i64.ge_s (local.get $y) (i64.const -2))
      (then (i32.store (i32.const 0) (i32.const 97)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i64.lt_u against -9223372036854775807: not a sign
  (func $i64_lt_u_miss168 (export "i64_lt_u_miss168") (param $y i64) (result i32)
    (if (i64.lt_u (local.get $y) (i64.const -9223372036854775807))
      (then (i32.store (i32.const 0) (i32.const 98)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i64.ge_u against 9223372036854775807: not a sign
  (func $i64_ge_u_miss174 (export "i64_ge_u_miss174") (param $y i64) (result i32)
    (if (i64.ge_u (local.get $y) (i64.const 9223372036854775807))
      (then (i32.store (i32.const 0) (i32.const 99)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i64.gt_u against -9223372036854775808: not a sign
  (func $i64_gt_u_miss180 (export "i64_gt_u_miss180") (param $y i64) (result i32)
    (if (i64.gt_u (local.get $y) (i64.const -9223372036854775808))
      (then (i32.store (i32.const 0) (i32.const 100)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; i64.le_u against -9223372036854775808: not a sign
  (func $i64_le_u_miss186 (export "i64_le_u_miss186") (param $y i64) (result i32)
    (if (i64.le_u (local.get $y) (i64.const -9223372036854775808))
      (then (i32.store (i32.const 0) (i32.const 101)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; a sign through a chain: bit 7 of y
  (func $c_ext8 (export "c_ext8") (param $y i32) (result i32)
    (if (i32.lt_s (i32.extend8_s (local.get $y)) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 102)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; a sign through a chain: bit 15 of y
  (func $c_ext16 (export "c_ext16") (param $y i32) (result i32)
    (if (i32.ge_s (i32.extend16_s (local.get $y)) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 103)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; a sign through a chain: bit 7 of y
  (func $c_shl24 (export "c_shl24") (param $y i32) (result i32)
    (if (i32.lt_s (i32.shl (local.get $y) (i32.const 24)) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 104)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; a sign through a chain: bit 0 of y
  (func $c_shl31 (export "c_shl31") (param $y i32) (result i32)
    (if (i32.lt_s (i32.shl (local.get $y) (i32.const 31)) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 105)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; a sign through a chain: bit 28 of y
  (func $c_shl35 (export "c_shl35") (param $y i32) (result i32)
    (if (i32.lt_s (i32.shl (local.get $y) (i32.const 35)) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 106)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; a sign through a chain: bit 30 of y
  (func $c_rotl1 (export "c_rotl1") (param $y i32) (result i32)
    (if (i32.lt_s (i32.rotl (local.get $y) (i32.const 1)) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 107)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; a sign through a chain: bit 0 of y
  (func $c_rotr1 (export "c_rotr1") (param $y i32) (result i32)
    (if (i32.lt_s (i32.rotr (local.get $y) (i32.const 1)) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 108)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; a sign through a chain: bit 31 of y
  (func $c_shrs4 (export "c_shrs4") (param $y i32) (result i32)
    (if (i32.lt_s (i32.shr_s (local.get $y) (i32.const 4)) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 109)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; a sign through a chain: bit 15 of y
  (func $c_twostep (export "c_twostep") (param $y i32) (result i32)
    (if (i32.lt_s (i32.extend8_s (i32.shr_u (local.get $y) (i32.const 8))) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 110)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; a sign through a chain: bit 11 of y
  (func $c_unsigned (export "c_unsigned") (param $y i32) (result i32)
    (if (i32.ge_u (i32.shl (local.get $y) (i32.const 20)) (i32.const 0x80000000))
      (then (i32.store (i32.const 0) (i32.const 111)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; a sign through a chain: bit 31 of y
  (func $c64_ext32 (export "c64_ext32") (param $y i64) (result i32)
    (if (i64.lt_s (i64.extend32_s (local.get $y)) (i64.const 0))
      (then (i32.store (i32.const 0) (i32.const 112)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; a sign through a chain: bit 23 of y
  (func $c64_shl40 (export "c64_shl40") (param $y i64) (result i32)
    (if (i64.lt_s (i64.shl (local.get $y) (i64.const 40)) (i64.const 0))
      (then (i32.store (i32.const 0) (i32.const 113)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; a sign through a chain: bit 43 of y
  (func $c64_shl20 (export "c64_shl20") (param $y i64) (result i32)
    (if (i64.ge_s (i64.shl (local.get $y) (i64.const 20)) (i64.const 0))
      (then (i32.store (i32.const 0) (i32.const 114)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; an i64 sign of an i32 widened with its sign: bit 31 of the i32
  (func $c64_extends (export "c64_extends") (param $y i32) (result i32)
    (if (i64.lt_s (i64.extend_i32_s (local.get $y)) (i64.const 0))
      (then (i32.store (i32.const 0) (i32.const 115)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; an i32 sign of an i64 wrapped: bit 31 of the i64
  (func $c_wrap (export "c_wrap") (param $y i64) (result i32)
    (if (i32.lt_s (i32.wrap_i64 (local.get $y)) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 116)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; one bit through a chain: bit 5 of y
  (func $b_shru5 (export "b_shru5") (param $y i32) (result i32)
    (if (i32.and (i32.shr_u (local.get $y) (i32.const 5)) (i32.const 1))
      (then (i32.store (i32.const 0) (i32.const 117)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; one bit through a chain: bit 5 of y
  (func $b_shru3_4 (export "b_shru3_4") (param $y i32) (result i32)
    (if (i32.ne (i32.and (i32.shr_u (local.get $y) (i32.const 3)) (i32.const 4)) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 118)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; one bit through a chain: bit 1 of y
  (func $b_eqz_shl (export "b_eqz_shl") (param $y i32) (result i32)
    (if (i32.eqz (i32.and (i32.shl (local.get $y) (i32.const 2)) (i32.const 8)))
      (then (i32.store (i32.const 0) (i32.const 119)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; one bit through a chain: bit 31 of y
  (func $b_shrs (export "b_shrs") (param $y i32) (result i32)
    (if (i32.and (i32.shr_s (local.get $y) (i32.const 20)) (i32.const 0x4000))
      (then (i32.store (i32.const 0) (i32.const 120)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; one bit through a chain: bit 3 of y
  (func $b_rotr (export "b_rotr") (param $y i32) (result i32)
    (if (i32.and (i32.rotr (local.get $y) (i32.const 4)) (i32.const 0x80000000))
      (then (i32.store (i32.const 0) (i32.const 121)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; one bit through a chain: bit 4 of y
  (func $b_two (export "b_two") (param $y i32) (result i32)
    (if (i32.and (i32.shr_u (i32.shl (local.get $y) (i32.const 4)) (i32.const 8)) (i32.const 1))
      (then (i32.store (i32.const 0) (i32.const 122)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; one bit through a chain: bit 40 of y
  (func $b64_shru40 (export "b64_shru40") (param $y i64) (result i32)
    (if (i64.ne (i64.and (i64.shr_u (local.get $y) (i64.const 40)) (i64.const 1)) (i64.const 0))
      (then (i32.store (i32.const 0) (i32.const 123)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; one bit through a chain: bit 30 of y
  (func $b64_shl (export "b64_shl") (param $y i64) (result i32)
    (if (i64.eqz (i64.and (i64.shl (local.get $y) (i64.const 33)) (i64.const 0x8000000000000000)))
      (then (i32.store (i32.const 0) (i32.const 124)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; bit 40 of an i32 widened with its sign: its bit 31
  (func $b64_extends (export "b64_extends") (param $y i32) (result i32)
    (if (i64.ne (i64.and (i64.extend_i32_s (local.get $y)) (i64.const 0x10000000000)) (i64.const 0))
      (then (i32.store (i32.const 0) (i32.const 125)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; bit 6 of an i32 widened without its sign: its bit 6
  (func $b64_extendu (export "b64_extendu") (param $y i32) (result i32)
    (if (i64.ne (i64.and (i64.extend_i32_u (local.get $y)) (i64.const 0x40)) (i64.const 0))
      (then (i32.store (i32.const 0) (i32.const 126)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; bit 9 of an i64 wrapped: the i64's bit 9
  (func $b_wrap (export "b_wrap") (param $y i64) (result i32)
    (if (i32.and (i32.wrap_i64 (local.get $y)) (i32.const 0x200))
      (then (i32.store (i32.const 0) (i32.const 127)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; a branch on y >> 31: its sign
  (func $o_shru31 (export "o_shru31") (param $y i32) (result i32)
    (if (i32.shr_u (local.get $y) (i32.const 31))
      (then (i32.store (i32.const 0) (i32.const 128)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; a branch on eqz of y >> 31
  (func $o_eqz_shru31 (export "o_eqz_shru31") (param $y i32) (result i32)
    (if (i32.eqz (i32.shr_u (local.get $y) (i32.const 31)))
      (then (i32.store (i32.const 0) (i32.const 129)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; a branch on a one-bit field: bit 5 of y
  (func $o_bfx (export "o_bfx") (param $y i32) (result i32)
    (if (i32.shr_u (i32.shl (local.get $y) (i32.const 26)) (i32.const 31))
      (then (i32.store (i32.const 0) (i32.const 130)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; the sign of a signed field, bits 8 to 11: bit 11 of y
  (func $o_sbfx (export "o_sbfx") (param $y i32) (result i32)
    (if (i32.lt_s (i32.shr_s (i32.shl (local.get $y) (i32.const 20)) (i32.const 28)) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 131)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; bit 4 of a signed field of bits 8 to 11, one past its top: its sign, bit 11 of y
  (func $o_sbfx_top (export "o_sbfx_top") (param $y i32) (result i32)
    (if (i32.and (i32.shr_s (i32.shl (local.get $y) (i32.const 20)) (i32.const 28)) (i32.const 16))
      (then (i32.store (i32.const 0) (i32.const 132)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; the sign of an unsigned field of bits 8 to 11: 0, the field's value tested
  (func $o_ubfx_sign (export "o_ubfx_sign") (param $y i32) (result i32)
    (if (i32.lt_s (i32.shr_u (i32.shl (local.get $y) (i32.const 20)) (i32.const 28)) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 133)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; a value of one bit no chain makes: its own value tested
  (func $o_xor (export "o_xor") (param $y i32) (result i32)
    (if (i32.xor (i32.and (local.get $y) (i32.const 1)) (i32.const 1))
      (then (i32.store (i32.const 0) (i32.const 134)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; a branch on the wrap of an i64 >> 63
  (func $o64_shru63 (export "o64_shru63") (param $y i64) (result i32)
    (if (i32.wrap_i64 (i64.shr_u (local.get $y) (i64.const 63)))
      (then (i32.store (i32.const 0) (i32.const 135)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; bit 40 of an i32 widened without its sign: no bit of the i32, the widened value tested
  (func $n_extendu_hi (export "n_extendu_hi") (param $y i32) (result i32)
    (if (i64.ne (i64.and (i64.extend_i32_u (local.get $y)) (i64.const 0x10000000000)) (i64.const 0))
      (then (i32.store (i32.const 0) (i32.const 136)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; an i32 made by a wrap widened again without its sign: its bit 40 is 0
  (func $n_extendu_wrap (export "n_extendu_wrap") (param $y i64) (result i32)
    (i32.add (call $n_extendu_hi (i32.wrap_i64 (local.get $y))) (i32.const 1)))
  ;; bit 4 of y >> 28: past y's bits, the shift's value tested
  (func $n_shru_past (export "n_shru_past") (param $y i32) (result i32)
    (if (i32.and (i32.shr_u (local.get $y) (i32.const 28)) (i32.const 16))
      (then (i32.store (i32.const 0) (i32.const 137)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; bit 4 of y << 8: a zero the shift put in, the shift's value tested
  (func $n_shl_low (export "n_shl_low") (param $y i32) (result i32)
    (if (i32.and (i32.shl (local.get $y) (i32.const 8)) (i32.const 16))
      (then (i32.store (i32.const 0) (i32.const 138)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; a sign of a shift by a variable: the shift's value tested
  (func $n_varshift (export "n_varshift") (param $y i32) (result i32)
    (if (i32.lt_s (i32.shl (local.get $y) (i32.and (local.get $y) (i32.const 3))) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 139)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
  ;; an extension something else reads too: the sign of its own value
  (func $n_twouse (export "n_twouse") (param $y i32) (result i32)
    (local $e i32)
    (if (i32.lt_s (local.tee $e (i32.extend8_s (local.get $y))) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))) (local.get $e)))
  ;; an extension made in another block: the sign of its own value
  (func $n_otherblock (export "n_otherblock") (param $y i32) (param $z i32) (result i32)
    (local $e i32)
    (local.set $e (i32.extend8_s (local.get $y)))
    (if (local.get $z) (then (i32.store (i32.const 8) (local.get $z))))
    (if (i32.lt_s (local.get $e) (i32.const 0))
      (then (i32.store (i32.const 0) (i32.const 70)))
      (else (i32.store (i32.const 4) (i32.const 9))))
    (i32.add (i32.load (i32.const 0)) (i32.load (i32.const 4))))
)
