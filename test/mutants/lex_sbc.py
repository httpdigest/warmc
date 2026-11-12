"""isel.c find_cmp_chains' cch_lex and isel_ops.inc emit_lex, cond_of: two
pairs in order that are no one value's bits, as a cmp of the seconds and an
sbcs of the firsts on arm64.  Killed by test/cases_opt/lex_sbc.wat's
answers, its counts and its shapes, or by the compiler itself where a
mutant leaves a value to be read that nothing made.

The test of the three compares themselves (ir.c ir_lex_pair) is
test/mutants/lex_cmp.py's.  Not mutated: WARMC_NO_LEX_SBC, which no case
sets; the mask of an i32 constant to its 32 bits, which constants are kept
in; and the register the immediate compare reads its first operand from,
never the zero register, whose number is sp's there."""
S = 'src/isel.c'
O = 'src/isel_ops.inc'
def ms(old, new): return (S, [(old, new)])
def mo(old, new): return (O, [(old, new)])
PARTS = '    if (uses[part[k]] != 1 || f->insts[part[k]].block != bi) return false;'
MARK = '        s->folded[a[0]] = s->folded[a[1]] = s->folded[a[2]] = 1;\n        continue;'
CC = '    return x.strict ? (x.sgn ? CC_LT : CC_LO) : (x.sgn ? CC_GE : CC_HS);'
REFS = '  Ref n0 = x.n[lo], n1 = x.n[1 - lo], m0 = x.m[lo], m1 = x.m[1 - lo];'
WIDTHS = '  int sfn = is64(f, n0) ? SF64 : SF32, sfm = is64(f, m0) ? SF64 : SF32;'
IMM = ('    if (neg) a64_adds_imm(B, sfn, RZR, p, i12, lsl);\n'
       '    else a64_subs_imm(B, sfn, RZR, p, i12, lsl);')
M = {
 # which selects
 'cond_read_twice': ms(PARTS, PARTS.replace('uses[part[k]] != 1', '(k != 0 && uses[part[k]] != 1)')),
 'first_read_twice': ms(PARTS, PARTS.replace('uses[part[k]] != 1', '(k != 1 && uses[part[k]] != 1)')),
 'second_read_twice': ms(PARTS, PARTS.replace('uses[part[k]] != 1', '(k != 2 && uses[part[k]] != 1)')),
 'other_block': ms(PARTS, PARTS.replace(' || f->insts[part[k]].block != bi', '')),
 'any_select': ms('  return ir_lex_pair(f, part[0], part[1], part[2], false, &x) && !warmc_getenv("WARMC_NO_LEX_SBC");',
                  '  (void)x;\n  return !warmc_getenv("WARMC_NO_LEX_SBC");'),
 'off': ms('      if (in->op == I_SELECT && cch_lex(s, uses, v, bi)) {', '      if (0 && in->op == I_SELECT && cch_lex(s, uses, v, bi)) {'),
 # a chain
 'no_length': ms('        len[v] = 2;\n        s->cchain[v] = CCH_LEX;', '        s->cchain[v] = CCH_LEX;'),
 'chain_of_compares': ms('        len[v] = 2;\n        s->cchain[v] = CCH_LEX;', '        len[v] = 2;\n        s->cchain[v] = 1;'),
 'second_made': ms(MARK, MARK.replace('s->folded[a[0]] = s->folded[a[1]] = s->folded[a[2]] = 1;', 's->folded[a[1]] = s->folded[a[2]] = 1;')),
 'first_made': ms(MARK, MARK.replace('s->folded[a[0]] = s->folded[a[1]] = s->folded[a[2]] = 1;', 's->folded[a[0]] = s->folded[a[2]] = 1;')),
 'cond_made': ms(MARK, MARK.replace('s->folded[a[0]] = s->folded[a[1]] = s->folded[a[2]] = 1;', 's->folded[a[0]] = s->folded[a[1]] = 1;\n        s->folded[a[2]] = 0;')),
 'uncounted': ms('    g_stats.lex_sbcs += s->cchain[i] == CCH_LEX;\n', ''),
 'counted_a_ccmp': ms('    g_stats.ccmp_chains += s->cchain[i] && s->cchain[i] != CCH_LEX;', '    g_stats.ccmp_chains += s->cchain[i] != 0;'),
 # the condition
 'cc_unsigned': mo(CC, '    return x.strict ? CC_LO : CC_HS;'),
 'cc_signed': mo(CC, '    return x.strict ? CC_LT : CC_GE;'),
 'cc_strict': mo(CC, '    return x.sgn ? CC_LT : CC_LO;'),
 'cc_loose': mo(CC, '    return x.sgn ? CC_GE : CC_HS;'),
 'cc_of_compare': mo('  if (s->cchain && s->cchain[v] == CCH_LEX) {\n    IrLex x = lex_of(s, v);', '  if (0 && s->cchain && s->cchain[v] == CCH_LEX) {\n    IrLex x = lex_of(s, v);'),
 # the two instructions
 'pairs_as_strict': mo('  int lo = x.strict ? 0 : 1;', '  int lo = 0;'),
 'pairs_as_loose': mo('  int lo = x.strict ? 0 : 1;', '  int lo = 1;'),
 'seconds_turned': mo(REFS, '  Ref n0 = x.n[1 - lo], n1 = x.n[lo], m0 = x.m[lo], m1 = x.m[1 - lo];'),
 'firsts_turned': mo(REFS, '  Ref n0 = x.n[lo], n1 = x.n[1 - lo], m0 = x.m[1 - lo], m1 = x.m[lo];'),
 'seconds_width': mo(WIDTHS, '  int sfn = is64(f, m0) ? SF64 : SF32, sfm = is64(f, m0) ? SF64 : SF32;'),
 'firsts_width': mo(WIDTHS, '  int sfn = is64(f, n0) ? SF64 : SF32, sfm = is64(f, n0) ? SF64 : SF32;'),
 'no_immediate': mo('  if (k->op == I_CONST && recomp_addsub_imm(sfn ? k->imm : (k->imm & 0xffffffffu), sfn, &i12, &lsl, &neg)) {',
                    '  if (0 && k->op == I_CONST && recomp_addsub_imm(sfn ? k->imm : (k->imm & 0xffffffffu), sfn, &i12, &lsl, &neg)) {'),
 'cmn_as_cmp': mo(IMM, '    (void)neg;\n    a64_subs_imm(B, sfn, RZR, p, i12, lsl);'),
 'cmp_as_cmn': mo(IMM, '    (void)neg;\n    a64_adds_imm(B, sfn, RZR, p, i12, lsl);'),
 'no_borrow': mo('  a64_sbcs(B, sfm, RZR, p, q);', '  a64_subs_reg(B, sfm, RZR, p, q);'),
 'not_emitted': mo('  if (s->cchain[v] == CCH_LEX) { emit_lex(s, v); return; }\n', ''),
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py', 'lex_sbc'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}))
