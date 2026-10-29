"""arm64's compare chains: an and or or of compares made as cmp and ccmp
(isel.c find_cmp_chains, isel_ops.inc emit_cmp_chain), and the selects
that read a compare's flags with flag-keeping instructions between them
(isel_ops.inc flags_only).  Killed by test/cases_opt/ccmp_chain.wat (made
by test/gen_ccmp.py) -- its answers, how many chains -O2 makes and each
function's ccmps, ccmns, ccmps of a register and csets -- and by
test/run_a64_test.sh, the encodings against llvm-mc."""
I, O, A = 'src/isel.c', 'src/isel_ops.inc', 'src/a64.c'
def m(old, new, path=I): return (path, [(old, new)])
M = {
 # which chains
 'off': m('  find_cmp_chains(s, uses, blocked);\n', ''),
 'and_only': m('      if ((in->op != I_AND && in->op != I_OR) || in->nargs != 2 || in->type != VT_I32) continue;',
               '      if (in->op != I_AND || in->nargs != 2 || in->type != VT_I32) continue;'),
 'own': m('      if (!fa_own(s, v)) continue;\n', ''),
 'leaf_uses': m('  if (uses[c] != 1 || blocked[c] || !fa_own(s, c) || f->insts[c].block != bi) return false;',
                '  if (blocked[c] || !fa_own(s, c) || f->insts[c].block != bi) return false;'),
 'leaf_block': m('  if (uses[c] != 1 || blocked[c] || !fa_own(s, c) || f->insts[c].block != bi) return false;',
                 '  if (uses[c] != 1 || blocked[c] || !fa_own(s, c)) return false;'),
 'leaf_float_any': m('  if (!icmp && !(first && op >= I_FEQ && op <= I_FGE)) return false;',
                     '  if (!icmp && !(op >= I_FEQ && op <= I_FGE)) return false;'),
 'leaf_float_none': m('  if (!icmp && !(first && op >= I_FEQ && op <= I_FGE)) return false;',
                      '  if (!icmp) return false;'),
 'leaf_tst': m('  return first || !(s->folded[x] && f->insts[x].op == I_AND);',
               '  (void)x;\n  return true;'),
 'cost_none': m('  return k <= 31 || ((0 - k) & m) <= 31 ? 0 : 1;', '  return 0;'),
 'cost_neg': m('  return k <= 31 || ((0 - k) & m) <= 31 ? 0 : 1;', '  return k <= 31 ? 0 : 1;'),
 'cost_eqz': m('  if (f->insts[c].op == I_EQZ) return 0;\n', ''),
 'best_ties': m('        if (best < 0 || cch_cost(f, a[r]) < cch_cost(f, a[best])) best = r;',
                '        if (best < 0 || cch_cost(f, a[r]) <= cch_cost(f, a[best])) best = r;'),
 'max3': m('#define CCH_MAX 4', '#define CCH_MAX 3'),
 'max5': m('#define CCH_MAX 4', '#define CCH_MAX 5'),
 'nest_off': m('        bool chain = len[l] && uses[l] == 1 && !blocked[l] && f->insts[l].block == bi &&',
               '        bool chain = 0 && uses[l] == 1 && !blocked[l] && f->insts[l].block == bi &&'),
 'sel_fold': m('        s->folded[c] = 1;\n    }\n    if (b->term == T_BRIF', '        (void)0;\n    }\n    if (b->term == T_BRIF'),
 'br_fuse': m('      s->fused[b->term_arg] = 1;\n  }\n  xfree(len);', '      (void)0;\n  }\n  xfree(len);'),
 # how they are made
 'nzcv': m('  while (nzcv < 15 && cc_holds(ccc, nzcv) == and) nzcv++;',
           '  while (nzcv < 15 && cc_holds(ccc, nzcv) != and) nzcv++;', O),
 'open': m('  int open = and ? lcc : cc_invert(lcc);', '  int open = and ? cc_invert(lcc) : lcc;', O),
 'cond_left': m('  if (s->cchain && s->cchain[v]) return cond_of(s, ir_args(s->f, v)[s->cchain[v] - 1]);',
                '  if (s->cchain && s->cchain[v]) return cond_of(s, ir_args(s->f, v)[2 - s->cchain[v]]);', O),
 'holds_hi': m('    case 4: r = c && !z; break;', '    case 4: r = c; break;', O),
 'holds_gt': m('    case 6: r = !z && n == v; break;', '    case 6: r = n == v; break;', O),
 'holds_ge': m('    case 5: r = n == v; break;', '    case 5: r = n; break;', O),
 'eqz_imm': m('  if (f->insts[c].op == I_EQZ) { a64_ccmp_imm(B, sf, x, 0, nzcv, open); return; }',
              '  if (f->insts[c].op == I_EQZ) { a64_ccmp_imm(B, sf, x, 1, nzcv, open); return; }', O),
 'imm30': m('    if (k <= 31) { a64_ccmp_imm(', '    if (k <= 30) { a64_ccmp_imm(', O),
 'ccmn_off': m('    if (((0 - k) & m) <= 31) { a64_ccmn_imm(', '    if (0) { a64_ccmn_imm(', O),
 'sf32': m('  int sf = is64(f, ca[0]) ? SF64 : SF32;\n  /* an immediate form',
           '  int sf = SF32;\n  /* an immediate form', O),
 'value_cset': m('    if (!bare) a64_cset(B, SF32, d, cc);\n    commit(s, v, d);\n    s->cmp_ref = v;',
                 '    if (bare) a64_cset(B, SF32, d, cc);\n    commit(s, v, d);\n    s->cmp_ref = v;', O),
 'value_flags': m('    s->cmp_ref = v; s->cmp_cc = cc; s->cmp_epoch = B->flags_epoch;\n    s->cmp_bare = bare ? v : REF_NONE;\n    return;',
                  '    s->cmp_bare = bare ? v : REF_NONE;\n    return;', O),
 'sel_value': m('  if (s->cchain && s->cchain[c]) return false;\n', '', O),
 # the selects after a compare
 'keeps_sub': m('    case I_ADD: case I_SUB: case I_MUL:', '    case I_ADD: case I_MUL:', O),
 'keeps_all': m('  switch (s->f->insts[u].op) {\n    case I_ADD: case I_SUB:', '  return true;\n  switch (s->f->insts[u].op) {\n    case I_ADD: case I_SUB:', O),
 # encodings
 'ccmn_op': m('  condcmp(b, sf, 0, rn, imm5, 1, nzcv, cond);', '  condcmp(b, sf, 1, rn, imm5, 1, nzcv, cond);', A),
 'reg_bit': m('  condcmp(b, sf, 1, rn, (uint32_t)rm, 0, nzcv, cond);', '  condcmp(b, sf, 1, rn, (uint32_t)rm, 1, nzcv, cond);', A),
}
EQUIVALENT = {
 'cost_eqz': "reads past an eqz's one operand, a Ref that is the next instruction's operand and "
             "next to never a constant above 31 -- and the eqz the ccmp either way where it is the "
             "second operand",
 'holds_hi': "the search takes the least flags that give the answer: 0 for hi's false and 2 for its "
             "true, neither of which has z set, the half of hi the mutant drops",
 'holds_gt': "the search takes the least flags that give the answer: 0 for gt's true and 1 for its "
             "false, neither of which has z set, the half of gt the mutant drops",
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('a64', ['./test/run_a64_test.sh'], {}))
