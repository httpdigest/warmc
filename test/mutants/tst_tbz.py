"""arm64's tests of masks: a branch on one bit made tbz or tbnz (isel.c
find_tbits, br_cc, br_patch, the re-emission with f->no_tbz where one does
not reach), a select or branch on a wider mask a tst (find_tst_conds), and
an add or sub of a constant shifted by 12 an immediate where a compare's
is not (imm_ok_a64).  Killed by test/cases_opt/tst_tbz.wat -- its answers
and each function's tbzs, tsts, ands, compares and shifted immediates --
and test/gen_binary_cases.py's tbz_far, whose test cannot reach past the
ten thousand stores it guards."""
I, O, C = 'src/isel.c', 'src/isel_ops.inc', 'src/codegen.c'
def m(old, new, path=I): return (path, [(old, new)])
M = {
 # which branches test a bit
 'tbz_off': m('  if (b->term != T_BRIF || warmc_getenv("WARMC_NO_TBZ")) return;', '  return;'),
 'eq_any': m('''  if (ci->op == I_EQZ || ((ci->op == I_EQ || ci->op == I_NE) && ci->nargs == 2 &&
                          f->insts[ir_args(f, c)[1]].op == I_CONST &&
                          (f->insts[ir_args(f, c)[1]].imm &
                           (vt_is_64((ValType)f->insts[ir_args(f, c)[0]].type) ? ~0ull : 0xffffffffu)) == 0)) {''',
             '''  if (ci->op == I_EQZ || ((ci->op == I_EQ || ci->op == I_NE) && ci->nargs == 2 &&
                          f->insts[ir_args(f, c)[1]].op == I_CONST)) {'''),
 'unfused': m('    if (!s->fused[c]) return;\n    and = ir_args(f, c)[0];', '    and = ir_args(f, c)[0];'),
 'and_unfused': m('  } else if (ci->op != I_AND || !s->fused[c]) {', '  } else if (ci->op != I_AND) {'),
 'polarity': m('    set = ci->op == I_NE;', '    set = ci->op != I_NE;'),
 'multibit': m('  return k && !(k & (k - 1)) ? __builtin_ctzll(k) : -1;', '  return k ? __builtin_ctzll(k) : -1;'),
 'and_uses': m('    if (!s->folded[and] && (uses[and] != 1 || !fa_own(s, and))) return;',
               '    if (!s->folded[and] && !fa_own(s, and)) return;'),
 'set_bit': m('        kind = (s->tbit[c] & TBIT_SET) ? 4 : 3;', '        kind = (s->tbit[c] & TBIT_SET) ? 3 : 4;', O),
 'bit_index': m('        cc = (s->tbit[c] & ~TBIT_SET) - 1;', '        cc = (s->tbit[c] & ~TBIT_SET);', O),
 # out of reach
 'fallback_off': m('  if (kind >= 3 && (s->f->no_tbz || s->far_br)) {', '  if (kind >= 3 && s->far_br) {'),
 'fallback_cc': m('    cc = kind == 3 ? CC_EQ : CC_NE;', '    cc = kind == 3 ? CC_NE : CC_EQ;'),
 'patch_b14': m("  if ((w & 0x7e000000u) == 0x36000000u) {", "  if (0) {"),
 'reach_flag': m('    if (!a64_patch_b14_ok(B, site, target)) s->tbz_far = true;', '    a64_patch_b14_ok(B, site, target);'),
 'rerun_flag': m('    if (s->tbz_far) f->no_tbz = true;\n', ''),
 'rerun_off': m('  if (refit || s->tbz_far) {', '  if (refit) {'),
 'reruns': m('  for (int k = 0; r == 4 && k < 2; k++)', '  for (int k = 0; r == 4 && k < 1; k++)', C),
 'reset': m('  f->no_tbz = false;\n', '', C),
 # tst conditions
 'tst_off': m('  if (warmc_getenv("WARMC_NO_TSTCOND")) goto bits;', '  goto bits;'),
 'tst_selects': m('      if (!term && (f->insts[u].op != I_SELECT || f->insts[u].nargs != 3)) continue;',
                  '      if (!term) continue;'),
 'tst_branches': m('      if (term && b->term != T_BRIF) break;', '      if (term) break;'),
 'tst_uses': m('      if (ci->op != I_AND || ci->nargs != 2 || uses[c] != 1 || ci->block != bi) continue;',
               '      if (ci->op != I_AND || ci->nargs != 2 || ci->block != bi) continue;'),
 'tst_folded_args': m('      if (s->folded[ca[0]] || s->folded[ca[1]]) continue;\n      int sf = vt_is_64',
                      '      int sf = vt_is_64'),
 'tst_logical': m('''      if (f->insts[ca[1]].op == I_CONST &&
          !a64_logical_imm(sf ? f->insts[ca[1]].imm : (f->insts[ca[1]].imm & 0xffffffffu), sf, &enc))
        continue;''', '      (void)enc;'),
 'tst_cond_ne': m('  if (in->op == I_AND) return CC_NE;', '  if (in->op == I_AND) return CC_EQ;', O),
 # shifted immediates
 'lsl_cmp': m('      return !lsl || in->op == I_ADD || in->op == I_SUB;', '      return true;'),
 'lsl_none': m('      return !lsl || in->op == I_ADD || in->op == I_SUB;', '      return !lsl;'),
 'addsub_sense': m('        if ((in->op == I_ADD) != neg) a64_add_imm(B, sf, d, x, i12, lsl);',
                   '        if ((in->op == I_ADD) == neg) a64_add_imm(B, sf, d, x, i12, lsl);', O),
 'addsub_lsl': m('        if ((in->op == I_ADD) != neg) a64_add_imm(B, sf, d, x, i12, lsl);\n        else a64_sub_imm(B, sf, d, x, i12, lsl);',
                 '        if ((in->op == I_ADD) != neg) a64_add_imm(B, sf, d, x, i12, 0);\n        else a64_sub_imm(B, sf, d, x, i12, 0);', O),
 'cmp_sense': m('      if (neg) a64_adds_imm(B, sf, RZR, x, i12, lsl);\n      else a64_subs_imm(B, sf, RZR, x, i12, lsl);',
                '      if (!neg) a64_adds_imm(B, sf, RZR, x, i12, lsl);\n      else a64_subs_imm(B, sf, RZR, x, i12, lsl);', O),
 'early_neg': m('          if (neg) sub = !sub;\n', ''),
}
EQUIVALENT = {
 'reruns': "a third emission is wanted only where the first measured the loops and a test-bit "
           "branch that reached then is pushed out of reach by the padding the second puts "
           "before them, a few nops a loop",
 'reset': "no_tbz is the function's own and only ever asks for the tst and b.cond that reach "
          "anywhere: left set, a later emission of the same function is the same code or that",
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('bincases', ['./test/run_binary_cases.sh'], {}))
