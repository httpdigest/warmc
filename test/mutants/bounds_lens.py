"""The bounds checker's 2026-11-05 rules: CSE takes an instruction's
operands to their twins before it looks it up (pass_cse); an unsigned
compare above a constant is a lower bound where the value is not negative
(rp_cond's RF_UGE, rp_ge_known); a loop inside a versioned loop's copy is
versioned in turn (bv_loop); a bounds check takes its array's length as an
operand where it is known without a load (pass_bound_lens, GCF_LENARG, the
backends' compares, bl_place); an immutable global's array has its
initialiser's length, and a merge of arrays an exact length only where its
ways in agree (array_len_of); CSE takes no mem_ptr's operands to their twins;
and a versioned loop's
copy proves an access at the counter plus an invariant, or at a second
counter, itself (bv_loop's symbolic groups, bv_sym_index); a counter that
steps up or stays (bv_mono) is versioned with the others in one copy, and
an array read anew from a field of an invariant object (bv_field_array)
that no struct.set writes (compile.c module_field_writes); and, in the
register allocator, a constant does not take the callee-saved registers
from the top, which a loop's own values crossing its calls need
(regalloc.inc cs_top).  Killed by
test/cases_opt/cse_chain.wat (its shapes), gc_range_bounds.wat (bce_ranges,
the near misses' traps), bce_version_nested.wat (bce_versioned, the short
rows' traps), bound_lens.wat (its counts, its traps, the IR verified as
SSA, irr's irreducible loop among them), cse_memptr.wat (two adds),
bce_version_sym.wat (its counts, shape and near misses' traps),
known_len_merge.wat (a merge's lengths), bce_version_merge.wat (its count:
which loops are copied), cs_order.wat (run2's sum off x22, x23) and the
answers of the cases."""
O = 'src/opt.c'
def m(old, new, path=O): return (path, [(old, new)])
M = {
 'cse_resolve': m('        for (uint32_t j = 0; j < in->nargs; j++) a[j] = ir_resolve(f, a[j]);\n', '        (void)a;\n'),
 'cse_memptr': m('      if (in->op != I_MEM_PTR)\n        for (uint32_t j', '      if (1)\n        for (uint32_t j'),

 'uge_off': m('          rp_add(p, tmp, RF_UGE, a, REF_NONE, op == I_GTU ? k0 + 1 : k0, blk, pos);', '          (void)0;'),
 'uge_plus': m('RF_UGE, a, REF_NONE, op == I_GTU ? k0 + 1 : k0, blk', 'RF_UGE, a, REF_NONE, op == I_GTU ? k0 + 2 : k0, blk'),
 'uge_geu': m('RF_UGE, a, REF_NONE, op == I_GTU ? k0 + 1 : k0, blk', 'RF_UGE, a, REF_NONE, op == I_GTU ? k0 + 1 : k0 + 1, blk'),
 'uge_nonneg': m('        ok = q->b == REF_NONE && q->k >= kk && rp_ge(p, c, q->orig, 0, d - 1);',
                 '        ok = q->b == REF_NONE && q->k >= kk;'),
 'uge_k': m('        ok = q->b == REF_NONE && q->k >= kk && rp_ge(p, c, q->orig, 0, d - 1);',
            '        ok = q->b == REF_NONE && q->k + 1 >= kk && rp_ge(p, c, q->orig, 0, d - 1);'),

 'bv_cold': m('  if (f->blocks[hb].bver || f->blocks[hb].cold) return false;', '  if (f->blocks[hb].bver) return false;'),
 'bv_header': m('  f->blocks[hb].bver = f->blocks[bcopy[hb]].bver = true;', '  (void)0;'),
 'bv_all_marked': m('  f->blocks[hb].bver = f->blocks[bcopy[hb]].bver = true;',
                    '  for (uint32_t bi = 0; bi < nb0; bi++)\n'
                    '    if (inloop[bi]) f->blocks[bi].bver = f->blocks[bcopy[bi]].bver = true;'),

 'sym_off': m('      bool sym_on = !mono && !down && pt == VT_I32 && c0 == 0 && !warmc_getenv("WARMC_NO_BV_SYM");',
               '      bool sym_on = false;'),
 'sym_down': m('      bool sym_on = !mono && !down && pt == VT_I32 && c0 == 0 && !warmc_getenv("WARMC_NO_BV_SYM");',
                '      bool sym_on = !mono && pt == VT_I32 && c0 == 0 && !warmc_getenv("WARMC_NO_BV_SYM");'),
 'sym_c0': m('      bool sym_on = !mono && !down && pt == VT_I32 && c0 == 0 && !warmc_getenv("WARMC_NO_BV_SYM");',
              '      bool sym_on = !mono && !down && pt == VT_I32 && !warmc_getenv("WARMC_NO_BV_SYM");'),
 'sym_nx': m('      BV_TEST(g, ir_emit(f, g, I_LES, VT_I32, xa, 2, 0, 0), 0);',
             '      (void)xa; BV_TEST(g, ir_emit(f, g, I_CONST, VT_I32, NULL, 0, 1, 0), 0);'),
 'sym_lo': m('        BV_TEST(g, ir_emit(f, g, I_GES, VT_I32, ga, 2, 0, 0), 0);',
             '        (void)ga; BV_TEST(g, ir_emit(f, g, I_CONST, VT_I32, NULL, 0, 1, 0), 0);'),
 'sym_hi': m('        BV_TEST(g, ir_emit(f, g, I_LTS, VT_I32, lt, 2, 0, 0), 0);',
             '        BV_TEST(g, ir_emit(f, g, I_LES, VT_I32, lt, 2, 0, 0), 0);'),
 'sym_derived': m('          Ref da[2] = { lo0, S };', '          Ref da[2] = { S, lo0 };'),
 'sym_qstep': m('    if (rp_lin(f, back->args[k], &sq) != q || sq != step) continue;',
                '    if (rp_lin(f, back->args[k], &sq) != q) continue;'),
 'sym_inv': m('    if ((yb < nb0 && inloop[yb]) || f->insts[y].op == I_CONST) continue;',
              '    if (f->insts[y].op == I_CONST) continue;'),
 'sym_mark': m('      f->insts[map[jobs[jj].sacc[s]]].imm2 |= GCF_NOBOUND;', '      (void)map;'),

 'mono_off': m('      if (warmc_getenv("WARMC_NO_BV_MONO") ||', '      if (1 ||'),
 'mono_down': m('  if (rp_lin(f, x, &c) == p && c >= 0 && c <= 64) return true;',
                '  if (rp_lin(f, x, &c) == p && c >= -64 && c <= 64) return true;'),
 'mono_bottom': m('        if (mono || tb != latch || on != hb) continue;', '        if (tb != latch || on != hb) continue;'),
 'jobs_one': m('  for (uint32_t pi = 0; pi < h->nparams && njobs < BV_JOBS; pi++) {',
               '  for (uint32_t pi = 0; pi < h->nparams && njobs < 1; pi++) {'),
 'field_arr_imm': m('  if ((xb < nb0 && inloop[xb]) || !gc_field_immutable(f, o)) return REF_NONE;',
                    '  if (xb < nb0 && inloop[xb]) return REF_NONE;'),
 'field_unwritten': m('         module_field_unwritten(f->m, vt_type_index(t), j);', '         0;'),
 'field_writes_mark': m('        w[base[root] + k] = 1;', '        (void)root;', path='src/compile.c'),
 'field_root_super': m('    uint32_t s = canon[c->super];', '    uint32_t s = canon[c->super]; break;', path='src/compile.c'),
 'field_root_canon': m('  uint32_t t = canon[ti];', '  uint32_t t = ti;', path='src/compile.c'),

 'cs_top_const': m('          f->insts[cur->value].op != I_CONST) {', '          1) {', path='src/regalloc.inc'),

 'bl_off': m('  if (on < 0) on = warmc_getenv("WARMC_NO_BOUND_LENS") == NULL;', '  on = 0;'),
 'bl_known': m('      if (k && k <= 0xfff) {', '      if (0) {'),
 'bl_dom': m('            if (dominates(f, lb, bi)) L = l;', '            L = l;'),
 'bl_order': m('          for (uint32_t j = 0; j < i; j++)\n            if (f->blocks[bi].insts[j] == l) { L = l; break; }',
               '          for (uint32_t j = 0; j < f->blocks[bi].ninsts; j++)\n            if (f->blocks[bi].insts[j] == l) { L = l; break; }'),
 'bl_nullable': m('        uint32_t place = L == REF_NONE && f->blocks[bi].loop_depth && vt_is_ref(at) &&\n                         !vt_nullable(at) && ab < f->nblocks ? bl_place(f, bi, ab) : REF_NONE;',
                  '        uint32_t place = L == REF_NONE && f->blocks[bi].loop_depth && vt_is_ref(at) &&\n                         ab < f->nblocks ? bl_place(f, bi, ab) : REF_NONE;'),
 'bl_place_dom': m('    if (!dominates(f, ab, d)) break;', '    (void)ab;'),
 'bl_outer': m('    if (f->blocks[d].loop_depth < depth) {', '    if (f->blocks[d].loop_depth < depth && best == REF_NONE) {'),
 'exact_merge': m('        if (!l || (exact && best && l != best)) return 0;', '        if (!l) return 0;'),
 'bl_isel': m('          Ref L = a[in->nargs - 1];', '          Ref L = a[1];', path='src/isel_ops.inc'),
 'bl_isel_imm': m('            a64_cmp_imm(B, SF32, idx, (uint32_t)(imm_of(s, L) & 0xfff));',
                  '            a64_cmp_imm(B, SF32, idx, (uint32_t)(imm_of(s, L) & 0xfff) + 1);', path='src/isel_ops.inc'),
 'bl_mask': m('  return ir_args((IrFunc *)f, v)[in->nargs - 1 - ((in->imm2 & GCF_LENARG) != 0)];',
              '  return ir_args((IrFunc *)f, v)[in->nargs - 1];', path='src/isel_core.inc'),

 'glob_mutable': m('      if (g->mutable_ || g->imported || !g->init.n) return 0;', '      if (g->imported || !g->init.n) return 0;'),
 'glob_fixed': m('      if (e[n - 1].op == IOP_ARRAY_NEW_FIXED) return (uint32_t)e[n - 1].hi;',
                 '      if (e[n - 1].op == IOP_ARRAY_NEW_FIXED) return (uint32_t)e[n - 1].hi + 1;'),
 'glob_new': m('        return (uint32_t)e[n - 2].lo;', '        return (uint32_t)e[n - 2].lo + 1;'),

}
EQUIVALENT = {}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {}))
