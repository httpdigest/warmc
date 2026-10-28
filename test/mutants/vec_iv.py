"""opt.c vectorize_one: a loop that touches no memory vectorised for what it
carries round, and a vector of a counter's values for lanes that hold the
counter.  Killed by test/cases_opt/vec_iv.wat (made by test/gen_veciv.py)."""
O = 'src/opt.c'
def m(old, new, path=O): return (path, [(old, new)])
M = {
 # a loop of no memory
 'nm_off': m('  if (nomem && !width) VREJ("no memory access");', '  if (nomem) VREJ("no memory access");'),
 'nm_stride': m('  uint64_t S = nomem ? width : 0;', '  uint64_t S = 0;'),
 'nm_lane0': m('        int l = k == 1 ? 0 : v_lane_of_term(&ctx, redterm[nred][t], VEC_DEPTH);',
               '        int l = v_lane_of_term(&ctx, redterm[nred][t], VEC_DEPTH);'),
 # which counters
 'iv_backward': m('  if (k == 1 && !backward)\n    for (uint32_t p = 0; p < np; p++)\n      if (isiv[p]',
                  '  if (k == 1)\n    for (uint32_t p = 0; p < np; p++)\n      if (isiv[p]'),
 'iv_k': m('  if (k == 1 && !backward)\n    for (uint32_t p = 0; p < np; p++)\n      if (isiv[p]',
           '  if (!backward)\n    for (uint32_t p = 0; p < np; p++)\n      if (isiv[p]'),
 'iv_width': m('      if (isiv[p] && v_natural_width((ValType)f->insts[hparam[p]].type) == width)',
               '      if (isiv[p])'),
 # their vectors
 'iv_build': m('  /* a counter, the lanes one iteration each */\n  if (op0 == I_PARAM)',
               '  /* a counter, the lanes one iteration each */\n  if (0)'),
 'iv_iota': m('      w[(j * bits) / 64] |= ((uint64_t)j * sp & m) << ((j * bits) % 64);',
              '      w[(j * bits) / 64] |= ((uint64_t)j & m) << ((j * bits) % 64);'),
 'iv_step': m('      Ref sc = ir_emit(f, vguard, I_CONST, pt, NULL, 0, (uint64_t)c * U * sp & m, 0);',
              '      Ref sc = ir_emit(f, vguard, I_CONST, pt, NULL, 0, (uint64_t)c * sp & m, 0);'),
 'iv_copy': m('        ctx.ivvec[q][c] = vemit(f, vb, I_VADD, VT_V128, B->piv[q], vivstep[q][c], 0, il);',
              '        ctx.ivvec[q][c] = B->piv[q];'),
 'iv_next': m('      B->nextiv[q] = vemit(f, vb, I_VADD, VT_V128, B->piv[q], vivstep[q][RC], 0, il);',
              '      B->nextiv[q] = vemit(f, vb, I_VADD, VT_V128, B->piv[q], vivstep[q][1], 0, il);'),
 'iv_used': m('        c->ivused[q] = true;\n', ''),
 'iv_unused': m('''  for (uint32_t q = 0; q < ctx.niv; q++)
    if (!ctx.ivused[q])
      for (uint32_t bi = 0; bi < (R > 1 ? 2u : 1u); bi++) bod[bi].nextiv[q] = bod[bi].piv[q];
''', ''),
 'iv_mid': m('    for (uint32_t q = 0; q < ctx.niv; q++) vb_args[nvb++] = bod[0].nextiv[q];\n',
             '    for (uint32_t q = 0; q < ctx.niv; q++) vb_args[nvb++] = vivinit[q];\n'),
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}),
          ('vecbench', ['python3', 'test/gobench.py', '--corpus', 'vecbench', '--check'], {}),
          ('gobench', ['python3', 'test/gobench.py', '--corpus', 'gobench', '--check'], {}))
