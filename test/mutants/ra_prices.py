"""What the scan is told of the selection's reads (regalloc.inc ra_cuse_cost,
ra_cs_worth, the use-cluster pieces' free reads), the one-move constants an
array.new may have made again (isel.c analyse_immediates), a branch on a
compare's flags (isel_ops.inc T_BRIF, br_flags) and an array index read as
a w register (ir.c ir_reads_low32).  Killed by
test/cases_opt/ra_prices.wat's shapes, counts and answers.  Not here: a
priced constant of several moves keeping its whole weight, no discount
(regalloc.inc's remat sweep) -- what it changes is which value an eviction
picks under pressure, seen in gobench/embed's instruction count (5% more
without it), and no small module found shows it."""
R, I, O, C = 'src/regalloc.inc', 'src/isel.c', 'src/isel_ops.inc', 'src/ir.c'
def m(path, old, new): return (path, [(old, new)])
M = {
 # the constants' price
 'cuse_off': m(R, '  if (!warmc_getenv("WARMC_RA_CUSE_ALL")) {', '  if (0) {'),
 'cuse_calls': m(R, '      inplace = true;\n', '      inplace = false;\n'),
 'cuse_callee': m(R, '      inplace = idx + 1 < in->nargs;', '      inplace = true;'),
 'cuse_imm': m(R, '  if (ir_imm_ok(f, user, idx, c->imm, (ValType)c->type)) return 0;\n', ''),
 'cuse_edge': m(R, '(b->succs[sidx].eh ? 0 : 1));', '0);'),
 'cuse_ret': m(R, "vt_is_64((ValType)f->insts[x].type)) - 1);", "vt_is_64((ValType)f->insts[x].type)));"),
 # a callee-saved register saved for a constant alone
 'cs_off': m(R, '  if (cs_any || !remat_ok(s, X->value)) return true;',
             '  return true;'),
 'cs_remat': m(R, '  if (cs_any || !remat_ok(s, X->value)) return true;',
               '  if (cs_any) return true;'),
 'cs_bound': m(R, ' >= 2.0) return true;', ' >= 0.0) return true;'),
 'cs_scan': m(R, '      if (chosen >= 0 && !ra_cs_worth(s, cur, chosen, wuse, cs_any)) {',
              '      if (0) {'),
 'cs_refill': m(R, '            if (!ra_cs_worth(s, X, rr, wuse, cs_any)) continue;\n', ''),
 # a piece for reads that read no register
 'piece_reads': m(R, '          if (cfree && l == LOC_NONE && ra_read_free(s, euu[j], eui[j], u)) continue;\n', ''),
 # an array.new's constants
 'anew_remat': m(I, '        if (via_moves && !(in->op == I_ARRAY_NEW && remat_one_move(f, a[j])))',
                 '        if (via_moves)'),
 # the branch on flags
 'brf_off': m(O, '      } else if (s->cmp_ref == c && s->cmp_epoch == B->flags_epoch &&',
              '      } else if (0 &&'),
 'brf_epoch': m(O, '      } else if (s->cmp_ref == c && s->cmp_epoch == B->flags_epoch &&',
                '      } else if (s->cmp_ref == c &&'),
 # the array index's low half
 'arr_low32': m(C, '      return (j == 1 || ((in->imm2 & GCF_BIASARG)', '      return (false || ((in->imm2 & GCF_BIASARG)'),
 # a constant no read of which a register would save anything takes none
 'cnone_off': m(R, '    bool cn = !warmc_getenv("WARMC_RA_CNONE_OFF");', '    bool cn = false;'),
 'cnone_any': m(R, '        if (cn && cw[i] <= 0) {', '        if (cn) {'),
 'cnone_scan': m(R, '      if (ra_zero_const(s, cur->value, cur->cls) || (cnone && cnone[cur->value])) {',
                 '      if (ra_zero_const(s, cur->value, cur->cls)) {'),
 'cnone_refill': m(R, '        if (ra_zero_const(s, X->value, X->cls) || (cnone && cnone[X->value])) continue;',
                   '        if (ra_zero_const(s, X->value, X->cls)) continue;'),
}
EQUIVALENT = {}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}))
