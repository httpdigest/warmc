"""isel.c find_cincs: an add of a compare's 0 or 1 made as the compare and a
cinc of the other operand, and isel_ops.inc keeps_flags, which no longer
counts such an add among the instructions that leave a select's flags.
Killed by test/cases_opt/cinc.wat -- its answers, each function's cincs and
csets and the count -- and ccmp_chain.wat."""
I, O = 'src/isel.c', 'src/isel_ops.inc'
def m(old, new, path=I): return (path, [(old, new)])
M = {
 'off': m('  if (warmc_getenv("WARMC_NO_CINC")) return;', '  return;'),
 'not_fa': m('      if (in->op != I_ADD || in->nargs != 2 || s->folded[u] || s->fa[u]) continue;',
             '      if (in->op != I_ADD || in->nargs != 2 || s->folded[u]) continue;'),
 'folded_add': m('      if (in->op != I_ADD || in->nargs != 2 || s->folded[u] || s->fa[u]) continue;',
                 '      if (in->op != I_ADD || in->nargs != 2 || s->fa[u]) continue;'),
 'no_ext': m('        if (f->insts[c].op == I_EXTENDU && in->type == VT_I64) {',
             '        if (0) {'),
 'ext_uses': m('          if (uses[e] != 1) continue;', ''),
 'no_float': m('        if (!(op == I_EQZ || (op >= I_EQ && op <= I_GEU) || (op >= I_FEQ && op <= I_FGE)))',
               '        if (!(op == I_EQZ || (op >= I_EQ && op <= I_GEU)))'),
 'no_eqz': m('        if (!(op == I_EQZ || (op >= I_EQ && op <= I_GEU) || (op >= I_FEQ && op <= I_FGE)))',
             '        if (!((op >= I_EQ && op <= I_GEU) || (op >= I_FEQ && op <= I_FGE)))'),
 'any_op': m('        if (!(op == I_EQZ || (op >= I_EQ && op <= I_GEU) || (op >= I_FEQ && op <= I_FGE)))\n          continue;',
             '        (void)op;'),
 'uses': m('        if (uses[c] != 1 || f->insts[c].block != bi) continue;',
           '        if (f->insts[c].block != bi) continue;'),
 'block': m('        if (uses[c] != 1 || f->insts[c].block != bi) continue;',
            '        if (uses[c] != 1) continue;'),
 'const_x': m('        if (f->insts[x].op == I_CONST || s->folded[x]) continue;',
              '        if (s->folded[x]) continue;'),
 'folded_x': m('        if (f->insts[x].op == I_CONST || s->folded[x]) continue;',
               '        if (f->insts[x].op == I_CONST) continue;'),
 'slot0': m('      for (int slot = 1; slot >= 0; slot--) {\n        Ref c = a[slot], x = a[slot ^ 1], e = REF_NONE;',
            '      for (int slot = 1; slot >= 1; slot--) {\n        Ref c = a[slot], x = a[slot ^ 1], e = REF_NONE;'),
 # the emitter
 'sense': m('        a64_csinc(B, sf, d, x, x, cc_invert(cc));', '        a64_csinc(B, sf, d, x, x, cc);', O),
 'operand': m('        int x = rd(s, a[2 - s->cinc[v]], S0);', '        int x = rd(s, a[s->cinc[v] - 1], S0);', O),
 'width': m('        a64_csinc(B, sf, d, x, x, cc_invert(cc));', '        a64_csinc(B, SF32, d, x, x, cc_invert(cc));', O),
 'flags': m('  if (s->cinc && s->cinc[u]) return false;\n', '', O),
}
EQUIVALENT = {
 'not_fa': "emit_inst makes a full adder (emit_full_adder) before it reaches the add's "
           "case, so a cinc mark on one is never read, and the compare it would fold "
           "the full adder reads as a 0 or 1 that rd makes all the same: the limbs "
           "case comes out the same, adcs and its two csets",
 'folded_add': "no fold ahead of find_cincs takes an add of a compare and a value: an "
               "address folds its mem_addr, the csinc arm and the madd fold adds of a "
               "constant or a multiply -- over every corpus none is folded, the full "
               "adders aside (not_fa); the test keeps the rule should one come",
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),)
