"""opt.c ifc_try's latency rule and its one exemption, ifc_one_bit: in a
loop, a test of one bit of a value it computes takes the select however
late the bit is; and fold_cmp_never, eq and ne against a constant with a bit
the value never has.  Killed by test/cases_opt/ifc_bits.wat (made by
test/gen_ifcbits.py) and cmp_never.wat."""
O = 'src/opt.c'
def m(old, new): return (O, [(old, new)])
X = '        !(b->loop_depth && !(dcl & IFC_LOADED) && ifc_one_bit(f, cond)))\n'
M = {
 # the exemption and what it asks
 'ex_off': m(X, '        !(0 && b->loop_depth && !(dcl & IFC_LOADED) && ifc_one_bit(f, cond)))\n'),
 'ex_loop': m(X, '        !(!(dcl & IFC_LOADED) && ifc_one_bit(f, cond)))\n'),
 'ex_loaded': m(X, '        !(b->loop_depth && ifc_one_bit(f, cond)))\n'),
 'ex_bit': m(X, '        !(b->loop_depth && !(dcl & IFC_LOADED)))\n'),
 'rule_off': m('    if (hi + 1 - lo > (uint32_t)g_ifc.crit &&\n', '    if (0 && hi + 1 - lo > (uint32_t)g_ifc.crit &&\n'),
 # what one bit is
 'bit_pow2': m('         m && !(m & (m - 1)) && (k == 0 || k == m);', '         m && (k == 0 || k == m);'),
 'bit_ne': m('  if ((in->op == I_NE || in->op == I_EQ) && in->nargs == 2 &&', '  if (in->op == I_EQ && in->nargs == 2 &&'),
 'bit_eq': m('  if ((in->op == I_NE || in->op == I_EQ) && in->nargs == 2 &&', '  if (in->op == I_NE && in->nargs == 2 &&'),
 'bit_zero': m('         m && !(m & (m - 1)) && (k == 0 || k == m);', '         m && !(m & (m - 1)) && k == m;'),
 'bit_self': m('         m && !(m & (m - 1)) && (k == 0 || k == m);', '         m && !(m & (m - 1)) && k == 0;'),
 'bit_any': m('         m && !(m & (m - 1)) && (k == 0 || k == m);', '         m && !(m & (m - 1));'),
 'bit_eqz': m('  else if (in->op == I_EQZ && in->nargs == 1)\n    c = ir_resolve(f, ir_args(f, c)[0]);\n', ''),
 'bit_and': m('  return in->op == I_AND && in->nargs == 2 && is_const', '  return in->nargs == 2 && is_const'),
 # decided compares
 'never_off': m('  replace_with_const(f, v, in->op == I_NE);\n  return true;', '  return false;'),
 'never_eq': m('        else if (in->op == I_EQ && fold_cmp_never(f, v)) { changed = true; g_stats.folded++; }\n', ''),
 'never_ne': m('        else if (in->op == I_NE && fold_cmp_never(f, v)) { changed = true; g_stats.folded++; }\n', ''),
 'never_bits': m('!(c & ~ir_known_bits(f, a[0], KB_DEPTH))', '!(c & ~ir_known_bits(f, a[0], KB_DEPTH) & 0xffffffffu)'),
 'never_any': m('!(c & ~ir_known_bits(f, a[0], KB_DEPTH))', '!(c & ~0ull)'),
 'never_val': m('  replace_with_const(f, v, in->op == I_NE);', '  replace_with_const(f, v, in->op == I_EQ);'),
}
EQUIVALENT = {
 'bit_any':
  "x & 2^k compared with a constant other than 0 and 2^k is decided by fold_cmp_never before if-conversion runs, so no such compare reaches it",
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}))
