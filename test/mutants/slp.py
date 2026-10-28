"""opt.c pass_slp: a block's stores of alike lanes to adjacent addresses as
one vector computation and one vector store; isel's vec_base for the
vector's offset and vbyel, a splat multiplied in by lane.  Killed by
test/cases_opt/slp.wat (made by test/gen_slp.py)."""
O, I, IO = 'src/opt.c', 'src/isel.c', 'src/isel_ops.inc'
def m(old, new, path=O): return (path, [(old, new)])
M = {
 # the pass
 'slp_off': m('  if (o->vectorize && o->guard_pages && !o->exact_trap && TP("slp", pass_slp(f))) {',
              '  if (0) {'),
 'slp_exact': m('  if (o->vectorize && o->guard_pages && !o->exact_trap && TP("slp", pass_slp(f))) {',
                '  if (o->vectorize && o->guard_pages && TP("slp", pass_slp(f))) {'),
 # addresses
 'a_ptr': m('  if (pi->op != I_MEM_ADDR && pi->op != I_MEM_PTR) return a;',
            '  if (pi->op != I_MEM_ADDR) return a;'),
 'a_offset': m('  if (pi->op == I_MEM_ADDR) a.d += pi->imm;\n', ''),
 'a_add': m('''  if (f->insts[a.x].op == I_ADD && f->insts[a.x].type == VT_I32 &&
      is_const(f, ir_args(f, a.x)[1], &c)) {''', '''  if (0) {'''),
 'a_add_type': m('  if (f->insts[a.x].op == I_ADD && f->insts[a.x].type == VT_I32 &&',
                 '  if (f->insts[a.x].op == I_ADD &&'),
 'adj_x': m('  return p.x == p0.x && p.c >= p0.c && p.c + p.d',
            '  return p.c >= p0.c && p.c + p.d'),
 'adj_c': m('  return p.x == p0.x && p.c >= p0.c && p.c + p.d',
            '  return p.x == p0.x && p.c + p.d'),
 'adj_off': m('p.c + p.d == p0.c + p0.d + ((uint64_t)j << sz);', 'true;'),
 # the tree
 'tr_nodes': m('#define SLP_MAX_NODES 64\n', '#define SLP_MAX_NODES 66\n'),
 'tr_uniform': m('    if (l[j] != l[0]) uniform = false;', ''),
 'tr_consts': m('    if (f->insts[l[j]].op != I_CONST) consts = false;', ''),
 'tr_splat_cost': m('''    nd->kind = consts ? SLP_CONST : SLP_SPLAT;
    S->saved -= 1;''', '''    nd->kind = consts ? SLP_CONST : SLP_SPLAT;'''),
 'tr_same_op': m('    if (f->insts[l[j]].op != i0->op) same_op = false;', ''),
 'tr_ld_size': m('    if ((1u << sz) * k != 16) return -1;', ''),
 'tr_ld_flags': m('      if (f->insts[l[j]].imm2 != i0->imm2 || !slp_adj(f, l[j], l[0], j, sz) ||',
                  '      if (!slp_adj(f, l[j], l[0], j, sz) ||'),
 'tr_ld_adj': m('      if (f->insts[l[j]].imm2 != i0->imm2 || !slp_adj(f, l[j], l[0], j, sz) ||',
                '      if (f->insts[l[j]].imm2 != i0->imm2 ||'),
 'tr_ld_block': m('''          S->blk[l[j]] != S->blk[l[0]])
        return -1;''', '''          0)
        return -1;'''),
 'tr_ld_before': m('    bool before = f->blocks[S->blk[l[0]]].loop_depth < f->blocks[S->bi].loop_depth;',
                   '    bool before = false;'),
 'tr_ld_before1': m('    bool before = f->blocks[S->blk[l[0]]].loop_depth < f->blocks[S->bi].loop_depth;',
                    '    bool before = true;'),
 'tr_ld_save': m('      if (!before && S->uses[l[j]] == 1) S->saved += 1;',
                 '      if (!before) S->saved += 1;'),
 'tr_ld_cost': m('''    nd->kind = SLP_LOAD;
    if (!before) S->saved -= 1;''', '''    nd->kind = SLP_LOAD;'''),
 'tr_op_uses': m('    if (S->uses[l[j]] != 1 || S->blk[l[j]] != S->bi) return -1;',
                 '    if (S->blk[l[j]] != S->bi) return -1;'),
 'tr_op_block': m('    if (S->uses[l[j]] != 1 || S->blk[l[j]] != S->bi) return -1;',
                  '    if (S->uses[l[j]] != 1) return -1;'),
 'tr_op_save': m('  S->saved += (int)k - 1;\n  Ref a[2][4];', '  Ref a[2][4];'),
 'tr_swap_off': m('    bool sw = comm && j > 0 && !slp_fits(S, aj[0], a[0][0], j, 1);',
                  '    bool sw = false;'),
 'tr_swap_comm': m('    bool sw = comm && j > 0 && !slp_fits(S, aj[0], a[0][0], j, 1);',
                   '    bool sw = j > 0 && !slp_fits(S, aj[0], a[0][0], j, 1);'),
 'tr_swap_fits': m('    bool sw = comm && j > 0 && !slp_fits(S, aj[0], a[0][0], j, 1);',
                   '    bool sw = comm && j > 0;'),
 'fit_same': m('  if (x == x0) return true;\n  if (a->op', '  if (a->op'),
 'fit_op': m('  if (a->op != b->op) return false;\n  if (a->op == I_LOAD)', '  if (a->op == I_LOAD)'),
 'fit_flags': m('  if (a->op == I_LOAD) return a->imm2 == b->imm2 && slp_adj(f, x, x0, j, MEMF_SIZE(b->imm2));',
                '  if (a->op == I_LOAD) return slp_adj(f, x, x0, j, MEMF_SIZE(b->imm2));'),
 'fit_adj': m('  if (a->op == I_LOAD) return a->imm2 == b->imm2 && slp_adj(f, x, x0, j, MEMF_SIZE(b->imm2));',
              '  if (a->op == I_LOAD) return a->imm2 == b->imm2;'),
 'fit_shallow': m('  if (depth == 0 || !a->nargs) return true;', '  return true;'),
 'fit_deep_none': m('''  for (uint32_t i = 0; i < a->nargs; i++)
    if (slp_fits(S, ir_args(f, x)[i], ir_args(f, x0)[0], j, depth - 1)) return true;
  return false;''', '''  return false;'''),
 'fit_first_only': m('  for (uint32_t i = 0; i < a->nargs; i++)\n    if (slp_fits(S, ir_args(f, x)[i],',
                     '  for (uint32_t i = 0; i < 1; i++)\n    if (slp_fits(S, ir_args(f, x)[i],'),
 # the groups found by the index
 'grp_type': m('        (1u << sz) != (vt_is_64(t) ? 8u : 4u))\n      continue;\n    SlpAddr a',
               '        0)\n      continue;\n    SlpAddr a'),
 'grp_mem': m('      if (ix[e + j].mem == ix[e].mem && ix[e + j].sz == sz && slp_adj(f, ix[e + j].st, s0, j, sz))',
              '      if (ix[e + j].sz == sz && slp_adj(f, ix[e + j].st, s0, j, sz))'),
 'grp_size': m('      if (ix[e + j].mem == ix[e].mem && ix[e + j].sz == sz && slp_adj(f, ix[e + j].st, s0, j, sz))',
               '      if (ix[e + j].mem == ix[e].mem && slp_adj(f, ix[e + j].st, s0, j, sz))'),
 'grp_adj': m('      if (ix[e + j].mem == ix[e].mem && ix[e + j].sz == sz && slp_adj(f, ix[e + j].st, s0, j, sz))',
              '      if (ix[e + j].mem == ix[e].mem && ix[e + j].sz == sz)'),
 'grp_cost': m('    if (S.saved <= 0) { SLPREJ("no fewer instructions"); continue; }',
               '    if (S.saved < 0) { SLPREJ("no fewer instructions"); continue; }'),
 'grp_clash': m('      clash = lo <= R->g[g].last && R->g[g].lo <= last;', '      clash = false;'),
 'grp_clash_loads': m('      if (lb == bi && l0 < lo) lo = l0;\n', ''),
 'grp_addr_made': m('      ok = blk[la] != lb || pos[la] < l0;', '      ok = true;'),
 'grp_addr_block': m('      ok = blk[la] != lb || pos[la] < l0;', '      ok = pos[la] < l0;'),
 'grp_call': m('      if (ok && call[lf] != REF_NONE) {', '      if (0) {'),
 'grp_call_add': m('''          if (f->insts[r].op == I_ADD && f->insts[r].type == VT_I32 &&
              is_const(f, ir_args(f, r)[1], &c))
            r = ir_args(f, r)[0];''', '''          (void)c;'''),
 'grp_call_last': m('    if (is_call_op(f->insts[b->insts[i]].op)) last = b->insts[i];', ''),
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
 'x_store_down': m('''      ok = slp_cross(f, b, pos[st[j]] + 1, last, ir_args(f, st[j])[0], sj->imm, sz,
                     MEMF_MEM(sj->imm2), true);''', '''      ok = true;'''),
 'x_load_up': m('''        ok = slp_cross(f, &f->blocks[lb], l0 + 1, pos[nd->lane[j]], ir_args(f, nd->lane[j])[0],
                       lj->imm, MEMF_SIZE(lj->imm2), MEMF_MEM(lj->imm2), false);''',
                '''        ok = true;'''),
 'x_load_block': m('        ok = slp_cross(f, &f->blocks[lb], l0 + 1,', '        ok = slp_cross(f, b, l0 + 1,'),
 'x_loads': m('      if (in->op == I_LOAD && !store) continue;\n', ''),
 'x_alias': m('''      if (may_alias(f, addr, imm, size, mem, a[0], in->imm, MEMF_SIZE(in->imm2),
                    MEMF_MEM(in->imm2)))
        return false;''', '''      (void)a;'''),
 'x_effect': m('    if (!ir_has_no_effect(in)) return false;\n  }\n  return true;',
               '  }\n  return true;'),
 'x_vload': m('    if ((in->op == I_VLOAD || in->op == I_VLDN) && store) return false;\n', ''),
 # emitting
 'e_const_hi': m('      f->insts[nd->val].imm2 = w[1];\n      return nd->val;', '      return nd->val;'),
 'e_nop_loads': m('    if (uses[nd->lane[j]] == 1) { f->insts[nd->lane[j]].op = I_NOP; continue; }',
                  '    { f->insts[nd->lane[j]].op = I_NOP; continue; }'),
 'e_order': m('    for (uint32_t y = x; y > 0 && n[ord[y]].first > n[ord[y - 1]].first; y--) {',
              '    for (uint32_t y = x; y > 0 && n[ord[y]].first < n[ord[y - 1]].first; y--) {'),
 'e_outer_order': m('  return a->first < b->first ? 1 : a->first > b->first ? -1 : 0;',
                    '  return a->first < b->first ? -1 : a->first > b->first ? 1 : 0;'),
 'e_share': m('    if (made) slp_share(f, bi, new0);\n', ''),
 'e_share_outer': m('          slp_share(f, out[x].blk, new0);\n', ''),
 'e_share_writes': m('    if (in->op != I_LOAD && !ir_has_no_effect(in)) writes++;\n', ''),
 'e_kept': m('      if (!uses[kept.r[x]]) f->insts[kept.r[x]].op = I_NOP;', '      (void)kept.r[x];'),
 'e_kept_nops': m('        if (in->op == I_NOP) continue;\n        for (uint32_t j = 0; j < in->nargs; j++) uses[',
                  '        for (uint32_t j = 0; j < in->nargs; j++) uses['),
 # isel: a splat multiplied in by lane
 'be_off': m('      if (in->op == I_VFMUL && s->vbyel && s->vbyel[v]) {', '      if (0) {', IO),
 'be_count': m('            else if (by[a[slot]] == uses[a[slot]]) {', '            else if (by[a[slot]]) {', I),
 'be_lane': m('            if (ei->op != I_VSPLAT || ei->imm2 != in->imm2 || ei->block != bi ||',
              '            if (ei->op != I_VSPLAT || ei->block != bi ||', I),
 'be_block': m('            if (ei->op != I_VSPLAT || ei->imm2 != in->imm2 || ei->block != bi ||',
               '            if (ei->op != I_VSPLAT || ei->imm2 != in->imm2 ||', I),
 'be_two': m('''            if (ei->op != I_VSPLAT || ei->imm2 != in->imm2 || ei->block != bi ||
                f->insts[a[slot ^ 1]].op == I_VSPLAT)''',
             '''            if (ei->op != I_VSPLAT || ei->imm2 != in->imm2 || ei->block != bi)''', I),
 'be_slot': m('              s->vbyel[u] = (uint8_t)(slot + 1);', '              s->vbyel[u] = 1;', I),
 'be_size': m('        a64_vfmul_elem(B, in->imm2 == VLANE_F64, d, x, y, 0);',
              '        a64_vfmul_elem(B, 1, d, x, y, 0);', IO),
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
 'a_add_type':
  "only a 64-bit memory's addresses are 64-bit adds, and each of its accesses comes after its own bounds check, which no group's store moves past (slp.wat's wide64)",
 'x_vload':
  "I_VLOAD comes only from the loop vectoriser, whose vector body has no scalar stores to group",
}
