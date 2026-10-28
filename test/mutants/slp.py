"""opt.c pass_slp: a block's stores of alike lanes to adjacent addresses as
one vector computation and one vector store; isel.c's vec_base for the
vector's offset.  Killed by test/cases_opt/slp.wat (made by
test/gen_slp.py)."""
O, IO = 'src/opt.c', 'src/isel_ops.inc'
def m(old, new, path=O): return (path, [(old, new)])
M = {
 # the pass
 'slp_off': m('  if (o->vectorize && o->guard_pages && !o->exact_trap && TP("slp", pass_slp(f))) {',
              '  if (0) {'),
 'slp_exact': m('  if (o->vectorize && o->guard_pages && !o->exact_trap && TP("slp", pass_slp(f))) {',
                '  if (o->vectorize && o->guard_pages && TP("slp", pass_slp(f))) {'),
 # groups
 'tr_depth': m('  if (depth == 0 || S->nn == SLP_MAX_NODES) return -1;',
               '  if (S->nn == SLP_MAX_NODES) return -1;'),
 'tr_depth1': m('#define SLP_DEPTH 6\n', '#define SLP_DEPTH 7\n'),
 'tr_uniform': m('    if (l[j] != l[0]) uniform = false;', ''),
 'tr_consts': m('    if (f->insts[l[j]].op != I_CONST) consts = false;', ''),
 'tr_splat_cost': m('''    nd->kind = consts ? SLP_CONST : SLP_SPLAT;
    S->saved -= 1;''', '''    nd->kind = consts ? SLP_CONST : SLP_SPLAT;'''),
 'tr_same_op': m('    if (f->insts[l[j]].op != i0->op) same_op = false;', ''),
 'tr_ld_size': m('    if ((1u << sz) * k != 16) return -1;', ''),
 'tr_ld_ptr': m('      if (ir_args(f, l[j])[0] != ptr || ij->imm2 != i0->imm2 ||',
                '      if (ij->imm2 != i0->imm2 ||'),
 'tr_ld_off': m('          ij->imm != i0->imm + ((uint64_t)j << sz) || S->pos[l[j]] == UINT32_MAX)',
                '          S->pos[l[j]] == UINT32_MAX)'),
 'tr_ld_block': m('          ij->imm != i0->imm + ((uint64_t)j << sz) || S->pos[l[j]] == UINT32_MAX)',
                  '          ij->imm != i0->imm + ((uint64_t)j << sz))'),
 'tr_ld_save': m('      if (S->uses[l[j]] == 1) S->saved += 1;', ''),
 'tr_ld_cost': m('''    nd->kind = SLP_LOAD;
    S->saved -= 1;''', '''    nd->kind = SLP_LOAD;'''),
 'tr_op_uses': m('    if (S->uses[l[j]] != 1) return -1;\n  uint32_t me', '    if (0) return -1;\n  uint32_t me'),
 'tr_op_save': m('  S->saved += (int)k - 1;\n  Ref a[2][4];', '  Ref a[2][4];'),
 'tr_swap_off': m('  for (int swap = 0; swap < (comm ? 2 : 1); swap++) {',
                  '  for (int swap = 0; swap < 1; swap++) {'),
 'tr_swap_all': m('  for (int swap = 0; swap < (comm ? 2 : 1); swap++) {',
                  '  for (int swap = 0; swap < 2; swap++) {'),
 'tr_swap_fits': m('      bool sw = swap && j > 0 && !slp_fits(S, aj[0], a[0][0], j, 1);',
                   '      bool sw = swap && j > 0;'),
 'fit_same': m('  if (x == x0) return true;\n  if (a->op', '  if (a->op'),
 'fit_op': m('  if (a->op != b->op) return false;\n  if (a->op == I_LOAD)', '  if (a->op == I_LOAD)'),
 'fit_ptr': m('    return ir_args(f, x)[0] == ir_args(f, x0)[0] && a->imm2 == b->imm2 &&',
              '    return a->imm2 == b->imm2 &&'),
 'fit_off': m('           a->imm == b->imm + ((uint64_t)j << MEMF_SIZE(b->imm2));', '           true;'),
 'fit_shallow': m('  if (depth == 0 || !a->nargs) return true;', '  return true;'),
 'fit_deep_none': m('''  for (uint32_t i = 0; i < a->nargs; i++)
    if (slp_fits(S, ir_args(f, x)[i], ir_args(f, x0)[0], j, depth - 1)) return true;
  return false;''', '''  return false;'''),
 'fit_first_only': m('  for (uint32_t i = 0; i < a->nargs; i++)\n    if (slp_fits(S, ir_args(f, x)[i],',
                     '  for (uint32_t i = 0; i < 1; i++)\n    if (slp_fits(S, ir_args(f, x)[i],'),
 # the tree, and the groups found by the index
 'grp_type': m('        (1u << sz) != (vt_is_64(t) ? 8u : 4u))\n      continue;\n    ix[nix++]',
               '        0)\n      continue;\n    ix[nix++]'),
 'grp_ptr': m('      if (ix[e + j].ptr == ptr && ix[e + j].sz == sz &&', '      if (ix[e + j].sz == sz &&'),
 'grp_size': m('      if (ix[e + j].ptr == ptr && ix[e + j].sz == sz &&', '      if (ix[e + j].ptr == ptr &&'),
 'grp_off': m('          ix[e + j].imm == ix[e].imm + ((uint64_t)j << sz))', '          1)'),
 'grp_cost': m('    if (S.saved <= 0) { SLPREJ("no fewer instructions"); continue; }',
               '    if (S.saved < 0) { SLPREJ("no fewer instructions"); continue; }'),
 'grp_clash': m('      clash = lo <= (*out)[g].last && (*out)[g].lo <= last;', '      clash = false;'),
 'grp_clash_loads': m('      if (l0 < lo) lo = l0;\n', ''),
 'grp_order': m('  return a->lo < b->lo ? 1 : a->lo > b->lo ? -1 : 0;',
                '  return a->lo < b->lo ? -1 : a->lo > b->lo ? 1 : 0;'),
 # the vector ops
 'op_sub': m('    case I_SUB: return fl ? 0 : I_VSUB;', '    case I_SUB: return fl ? 0 : I_VADD;'),
 'op_and': m('    case I_AND: return fl ? 0 : I_VAND;', '    case I_AND: return fl ? 0 : I_VOR;'),
 'op_or': m('    case I_OR:  return fl ? 0 : I_VOR;', '    case I_OR:  return fl ? 0 : I_VXOR;'),
 'op_xor': m('    case I_XOR: return fl ? 0 : I_VXOR;', '    case I_XOR: return fl ? 0 : I_VAND;'),
 'op_mul64': m('    case I_MUL: return t == VT_I32 ? I_VMUL : 0;', '    case I_MUL: return fl ? 0 : I_VMUL;'),
 'op_fdiv': m('    case I_FDIV: return fl ? I_VFDIV : 0;', '    case I_FDIV: return fl ? I_VFMUL : 0;'),
 'op_fmin': m('    case I_FMIN: return fl ? I_VFMIN : 0;', '    case I_FMIN: return fl ? I_VFMAX : 0;'),
 'op_fmax': m('    case I_FMAX: return fl ? I_VFMAX : 0;', '    case I_FMAX: return fl ? I_VFMIN : 0;'),
 'op_fsub_comm': m('''  *comm = false;
  switch (op) {
    case I_SUB: return fl ? 0 : I_VSUB;
    case I_FSUB: return fl ? I_VFSUB : 0;''', '''  *comm = false;
  switch (op) {
    case I_SUB: return fl ? 0 : I_VSUB;
    case I_FSUB: *comm = true; return fl ? I_VFSUB : 0;'''),
 # what a move may cross
 'x_store_down': m('''      ok = slp_cross(f, b, pos[st[j]] + 1, last, ptr, sj->imm, sz, MEMF_MEM(sj->imm2),
                     true, skip);''', '''      ok = true;'''),
 'x_load_up': m('''        ok = slp_cross(f, b, l0 + 1, pos[S.n[n].lane[j]], la, lj->imm, MEMF_SIZE(lj->imm2),
                       MEMF_MEM(lj->imm2), false, skip);''', '''        ok = true;'''),
 'x_loads': m('      if (in->op == I_LOAD && !store) continue;\n', ''),
 'x_alias': m('''      if (may_alias(f, addr, imm, size, mem, a[0], in->imm, MEMF_SIZE(in->imm2),
                    MEMF_MEM(in->imm2)))
        return false;''', '''      (void)a;'''),
 'x_effect': m('    if (!ir_has_no_effect(in)) return false;\n  }\n  return true;',
               '  }\n  return true;'),
 'x_vload': m('    if ((in->op == I_VLOAD || in->op == I_VLDN) && store) return false;\n', ''),
 'e_const_hi': m('      f->insts[nd->val].imm2 = w[1];\n      return nd->val;', '      return nd->val;'),
 # emitting
 'e_at_last': m('    uint32_t at = G->lfirst[ord[x]];', '    uint32_t at = G->last;'),
 'e_nop_loads': m('      if (uses[nd->lane[j]] == 1) f->insts[nd->lane[j]].op = I_NOP;',
                  '      f->insts[nd->lane[j]].op = I_NOP;'),
 'e_order': m('    for (uint32_t y = x; y > 0 && G->lfirst[ord[y]] > G->lfirst[ord[y - 1]]; y--) {',
              '    for (uint32_t y = x; y > 0 && G->lfirst[ord[y]] < G->lfirst[ord[y - 1]]; y--) {'),
 # isel
 'vb_unscaled': m('  if (((off & 15u) == 0 && (off >> 4) <= 0xfffu) || off <= 0xffu) {',
                  '  if ((off & 15u) == 0 && (off >> 4) <= 0xfffu) {', IO),
 'vb_load': m('''        if (size == 4) p = vec_base(s, p, in->imm, &disp);
        if (fp) a64_fldr_imm(B, size, d, p, disp);''', '''        if (fp) a64_fldr_imm(B, size, d, p, disp);''', IO),
 'vb_store': m('''        if (size == 4) p = vec_base(s, p, in->imm, &disp);
        if (fp) a64_fstr_imm(B, size, val, p, disp);''', '''        if (fp) a64_fstr_imm(B, size, val, p, disp);''', IO),
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}),
          ('cppbench', ['python3', 'test/gobench.py', '--corpus', 'cppbench', '--check'], {}),
          ('cbench', ['python3', 'test/gobench.py', '--corpus', 'cbench', '--check'], {}),
          ('rsbench', ['python3', 'test/gobench.py', '--corpus', 'rsbench', '--check'], {}))
EQUIVALENT = {
 'slp_exact':
  "-fexact-trap differs only in what memory holds when a trap ends the program, and no suite looks at memory after a trap",
 'x_vload':
  "I_VLOAD comes only from the loop vectoriser, whose vector body has no scalar stores to group",
 'tr_ld_block':
  "a load of another block has no place in this one: without the test its place is UINT32_MAX and the check's scan from it runs out of the block, stopping by chance at the first instruction with an effect -- only AddressSanitizer would see the difference",
}
