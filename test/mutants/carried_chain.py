"""opt.c carried_chain: the value a loop hands its parameter x on a back
edge, x op c1 op c2 ..., is x op c.  Killed by test/cases_opt/addchain.wat."""
O = 'src/opt.c'
def m(old, new): return (O, [(old, new)])
LINK = '''    if (d == CHAIN_DEPTH || xi->op != in->op || xi->nargs != 2 ||
        !is_const(f, ir_args(f, x)[1], &k) || !fold_int_binop(in->op, in->type, k, *c, c))'''
EDGE = '''      if (pb->succs[si].target == hb && pb->succs[si].args[q] == v) {'''
M = {
 'ch_off': m('      if (carried_chain(f, v, uses, &base, &c)) {', '      if (0) {'),
 'ch_depth_more': m('    if (d == CHAIN_DEPTH || xi->op', '    if (d == CHAIN_DEPTH + 1 || xi->op'),
 'ch_depth_less': m('#define CHAIN_DEPTH 16\n', '#define CHAIN_DEPTH 15\n'),
 'ch_op_any': m(LINK, LINK.replace('xi->op != in->op || ', '')),
 'ch_nargs': m(LINK, LINK.replace('xi->nargs != 2 || ', '')),
 'ch_var_link': m(LINK, LINK.replace('!is_const(f, ir_args(f, x)[1], &k)', '(!is_const(f, ir_args(f, x)[1], &k) && (k = 0, 0))')),
 'ch_fold_add': m(LINK, LINK.replace('fold_int_binop(in->op, in->type,', 'fold_int_binop(I_ADD, in->type,')),
 'ch_fold_i64': m(LINK, LINK.replace('fold_int_binop(in->op, in->type,', 'fold_int_binop(in->op, VT_I64,')),
 'ch_uses_off': m('  if (uses[x] < 2) return false;\n', ''),
 'ch_uses_3': m('  if (uses[x] < 2) return false;\n', '  if (uses[x] < 3) return false;\n'),
 'ch_q_notfound': m('  while (q < h->nparams && h->params[q] != x) q++;\n  if (q == h->nparams) return false;\n', '  while (q < h->nparams && h->params[q] != x) q++;\n'),
 'ch_any_target': m(EDGE, EDGE.replace('pb->succs[si].target == hb && ', '')),
 'ch_any_arg': m(EDGE, '''      for (uint32_t k = 0; k < pb->succs[si].nargs; k++) if (pb->succs[si].target == hb && pb->succs[si].args[k] == v) {'''),
 'ch_first_pred': m('''        *base = x;
        return true;
      }
  }
  return false;''', '''        *base = x;
        return true;
      }
    return false;
  }
  return false;'''),
 'ch_c_old': m('        Ref m = ir_emit(f, bi, I_CONST, t, NULL, 0, c, 0);\n        b->ninsts--;\n        for (uint32_t j = b->ninsts; j > i; j--) b->insts[j] = b->insts[j - 1];\n        b->insts[i] = m;\n        b->ninsts++;\n        in = &f->insts[v]; a = ir_args(f, v);\n        a[0] = base;',
               '        Ref m = ir_emit(f, bi, I_CONST, t, NULL, 0, y, 0);\n        b->ninsts--;\n        for (uint32_t j = b->ninsts; j > i; j--) b->insts[j] = b->insts[j - 1];\n        b->insts[i] = m;\n        b->ninsts++;\n        in = &f->insts[v]; a = ir_args(f, v);\n        a[0] = base;'),
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}),
          ('stdgo', ['python3', 'test/gobench.py', '--corpus', 'stdgo', '--check'], {}),
          ('gobench', ['python3', 'test/gobench.py', '--corpus', 'gobench', '--check'], {}),
          ('cbench', ['python3', 'test/gobench.py', '--corpus', 'cbench', '--check'], {}))
