"""The 2026-11-06 rules for null tests and constant indices: a branch on a
null test tests the reference itself (isel.c's fusion of a branch's
condition, isel_ops.inc's T_BRIF); an array access at a constant index
takes the element's offset as its load's or store's immediate, and a bounds
check compares the length with it (isel_core.inc arr_imm_index, isel.c
imm_ok_a64, isel_ops.inc's array get and set); a block param no way in
brings a null to is none (opt.c gc_nonnull_params); a ref.as_non_null of
what is known to be no null makes no trap (pass_gc_nullchk); and a sealed
module's type test of a reference known to be no null tests none
(codegen.c seal_tests, opt.c gc_nonnull_in, RTK_NONNULL in isel.c
emit_slot_test).
The x86-64 back end's halves of the first two, which this host does not
run, are left to the Windows rows.  Killed by test/cases_opt/null_br.wat
(its shapes and answers: twice and twice2 with a test read twice),
arr_kidx.wat (its shapes, the bounds' traps at k and k + 1, bigk's 4096,
far's and bytes' offsets past the immediate's reach, lenarg's length an
operand), null_known.wat (its counts, its shapes, the near misses' nulls:
nullside, isnullside, mergenull, passnull, walk), null_traps.wat (its
count, its shapes, nullarm's and merge's traps), test/cases_gc/kidx.wat
in run_gc.sh (every read after a collection through the barrier's slow
path, and with -fno-regalloc the array's register the element's) and the
answers of the cases."""
I = 'src/isel.c'
O = 'src/isel_ops.inc'
C = 'src/isel_core.inc'
P = 'src/opt.c'
G = 'src/codegen.c'
def m(path, old, new): return (path, [(old, new)])
M = {
 'nb_fuse': m(I, '      if (op == I_REF_IS_NULL && f->insts[c].nargs == 1) s->fused[c] = 1;\n',
              '      (void)0;\n'),
 'nb_fold': m(I, '            !blocked[r] && f->insts[r].block == bi && !s->folded[r])\n          s->folded[r] = 1;',
              '            !blocked[r] && f->insts[r].block == bi && !s->folded[r])\n          (void)0;'),
 'nb_uses': m(I, 'f->insts[r].op == I_REF_IS_NULL && f->insts[r].nargs == 1 && uses[r] == 1 &&',
              'f->insts[r].op == I_REF_IS_NULL && f->insts[r].nargs == 1 &&'),
 'nb_blocked': m(I, '            !blocked[r] && f->insts[r].block == bi && !s->folded[r])',
                 '            f->insts[r].block == bi && !s->folded[r])'),
 'nb_block': m(I, '            !blocked[r] && f->insts[r].block == bi && !s->folded[r])',
               '            !blocked[r] && !s->folded[r])'),
 'nb_kind1': m(O, '        kind = 1; zsf = SF64; zreg = rd(s, ir_args(f, c)[0], S0);',
               '        kind = 2; zsf = SF64; zreg = rd(s, ir_args(f, c)[0], S0);'),
 'nb_kind2': m(O, '          kind = 2; zsf = SF64; zreg = rd(s, ir_args(f, ca[0])[0], S0);',
               '          kind = 1; zsf = SF64; zreg = rd(s, ir_args(f, ca[0])[0], S0);'),
 'nb_sf1': m(O, '        kind = 1; zsf = SF64; zreg = rd(s, ir_args(f, c)[0], S0);',
             '        kind = 1; zsf = SF32; zreg = rd(s, ir_args(f, c)[0], S0);'),
 'nb_sf2': m(O, '          kind = 2; zsf = SF64; zreg = rd(s, ir_args(f, ca[0])[0], S0);',
             '          kind = 2; zsf = SF32; zreg = rd(s, ir_args(f, ca[0])[0], S0);'),

 'kx_on': m(C, '  if (!on || k > 0xffffffu) return false;', '  if (1) return false;'),
 'kx_scaled': m(C, '(tot >> size) > 0xfff', '(tot >> size) > 0x1fff'),
 'kx_align': m(C, ' || (tot & ((1u << size) - 1u))', ''),
 'kx_imm_ok': m(I, '      return idx == 1 && arr_imm_index(f, in, masked, NULL, true);',
                '      return idx == 1;'),
 'kx_idx': m(I, '      return idx == 1 && arr_imm_index(f, in, masked, NULL, true);',
             '      return idx == 2 && arr_imm_index(f, in, masked, NULL, true);'),
 'kx_cmp': m(O, '        a64_cmp_imm(B, SF32, L, (uint32_t)(imm_of(s, a[1]) & 0xfff));',
             '        a64_cmp_imm(B, SF32, L, (uint32_t)(imm_of(s, a[1]) & 0xfff) + 1);'),
 'kx_ls': m(O, '        trap_if_cc(s, CC_LS, TRAP_ARRAY_OOB);\n      } else if',
            '        trap_if_cc(s, CC_LO, TRAP_ARRAY_OOB);\n      } else if'),
 'kx_lenld': m(O, '        if (!(in->imm2 & GCF_LENARG)) a64_ldr_imm(B, 2, TMP0, x, 4);',
               '        if (!(in->imm2 & GCF_LENARG)) a64_ldr_imm(B, 2, TMP0, x, 0);'),
 'kx_lenimm': m(O, '          a64_load_imm(B, SF32, TMP0, imm_of(s, a[in->nargs - 1]) & 0xffffffffu);',
                '          a64_load_imm(B, SF32, TMP0, (imm_of(s, a[in->nargs - 1]) & 0xffffffffu) + 1);'),
 'kx_lenreg': m(O, '        else L = rd(s, a[in->nargs - 1], TMP0);', '        else L = TMP0;'),
 'kx_ea': m(O, '      if (kidx) { ea = x; off = koff; }', '      if (kidx) { ea = x; off = koff + 8; }'),
 'kx_pre': m(O, '          int sbase = bar ? emit_gc_load_barrier_pre(s, d, ea) : ea;',
             '          int sbase = ea;'),

 'np_off': m(P, '    if (np_on && b->nparams && b->npreds) gc_nonnull_params(f, bi, sets, counts, CAP);',
             '    (void)np_on;'),
 'np_type': m(P, '  if (vt_is_ref(at) && !vt_nullable(at)) return true;\n  Ref r = gc_ref_root(f, a);\n  if (gc_edge_nonnull',
              '  Ref r = gc_ref_root(f, a);\n  if (gc_edge_nonnull'),
 'np_edge': m(P, '  if (gc_edge_nonnull(f, q, e, r)) return true;\n  if (!sets[q]) return false;',
              '  if (!sets[q]) return false;'),
 'edge_isnull': m(P, '  if (ci->op == I_REF_IS_NULL) return !taken;\n  return taken != ((RTEST_KIND(ci->imm) & RTK_NULLABLE) != 0);',
                  '  if (ci->op == I_REF_IS_NULL) return true;\n  return taken != ((RTEST_KIND(ci->imm) & RTK_NULLABLE) != 0);'),
 'edge_test': m(P, '  if (ci->op == I_REF_IS_NULL) return !taken;\n  return taken != ((RTEST_KIND(ci->imm) & RTK_NULLABLE) != 0);',
                '  if (ci->op == I_REF_IS_NULL) return !taken;\n  return taken;'),
 'edge_taken': m(P, '  bool taken = e == &p->succs[0];\n  Ref cond = ir_resolve(f, p->term_arg);',
                 '  bool taken = e != &p->succs[0];\n  Ref cond = ir_resolve(f, p->term_arg);'),
 'edge_eqz': m(P, '    cond = ir_resolve(f, ir_args(f, cond)[0]);\n    taken = !taken;\n  }\n  Inst *ci = &f->insts[cond];\n  if ((ci->op != I_REF_IS_NULL',
               '    cond = ir_resolve(f, ir_args(f, cond)[0]);\n  }\n  Inst *ci = &f->insts[cond];\n  if ((ci->op != I_REF_IS_NULL'),
 'np_sets': m(P, '  if (!sets[q]) return false;\n  for (uint32_t k = 0; k < counts[q]; k++)\n    if (sets[q][k] == r) return true;',
              '  if (!sets[q]) return false;\n  for (uint32_t k = 0; k < counts[q]; k++)\n    if (sets[q][k] == r) return false;'),
 'np_nullable': m(P, '    if (!vt_is_ref(pt) || !vt_nullable(pt)) continue;\n    bool ok = true;',
                  '    if (!vt_is_ref(pt)) continue;\n    bool ok = true;'),

 'nn_type': m(P, '  if (vt_is_ref(t) && !vt_nullable(t)) return true;\n  Ref r = gc_ref_root(f, x);\n  for (uint32_t d = bi;',
              '  Ref r = gc_ref_root(f, x);\n  for (uint32_t d = bi;'),
 'nn_access': m(P, '      if (on && gc_ref_root(f, ir_args(f, u)[0]) == r) return true;',
                '      if (0 && on && gc_ref_root(f, ir_args(f, u)[0]) == r) return true;'),
 'nn_trapif': m(P, '          on = ui->imm == TRAP_NULL_REF && ui->nargs == 1;', '          on = false;'),
 'nn_curblock': m(P, '    uint32_t end = d == bi ? pos : b->ninsts;', '    uint32_t end = b->ninsts;'),
 'nn_edge': m(P, '      if (e && gc_edge_nonnull(f, b->preds[0], e, r)) return true;', '      (void)e;'),
 'nn_dom': m(P, '  for (uint32_t d = bi; d != REF_NONE && d < f->nblocks; d = f->blocks[d].idom) {',
             '  for (uint32_t d = bi; d != REF_NONE && d < f->nblocks; d = REF_NONE) {'),
 'nn_depth': m(P, '  return gc_nonnull_in(f, x, bi, pos, 2, &budget);', '  return gc_nonnull_in(f, x, bi, pos, 0, &budget);'),
 'nn_param': m(P, '    if (!gc_nonnull_in(f, a, q, f->blocks[q].ninsts, depth - 1, budget)) return false;',
               '    (void)a;'),
 'nn_param_edge': m(P, '    if (gc_edge_nonnull(f, q, e, gc_ref_root(f, a))) continue;\n', ''),

 'nt_off': m(P, '            if (known && nt_on) {', '            if (0) {'),
 'nt_type': m(P, '            bool known = vt_is_ref(xt) && !vt_nullable(xt);', '            bool known = false;'),
 'nt_all': m(P, '            bool known = vt_is_ref(xt) && !vt_nullable(xt);', '            bool known = true;'),
 'nt_sets': m(P, '            bool known = vt_is_ref(xt) && !vt_nullable(xt);\n            for (uint32_t k = 0; k < counts[bi] && !known; k++)\n              known = sets[bi][k] == obj;',
              '            bool known = vt_is_ref(xt) && !vt_nullable(xt);'),

 'st_off': m(G, '  if (nn_on)\n    for (uint32_t oi', '  if (0)\n    for (uint32_t oi'),
 'st_nullable': m(G, '        in->imm = (in->imm & ~(uint64_t)RTK_NULLABLE) | RTK_NONNULL;',
                  '        in->imm |= RTK_NONNULL;'),
 'st_skip': m(I, '  if (RTEST_KIND(in->imm) & RTK_NONNULL) {\n    /* known to be no null (codegen.c seal_tests): none tested */\n  } else if (RTEST_KIND(in->imm) & RTK_NULLTRAP) {\n    trap_if_zero(s, SF64, x, TRAP_NULL_REF);',
              '  if (0) {\n  } else if (RTEST_KIND(in->imm) & RTK_NULLTRAP) {\n    trap_if_zero(s, SF64, x, TRAP_NULL_REF);'),
}
EQUIVALENT = {
 'nb_blocked': 'the null test read once, by the eqz, is read by no terminator or edge',
 'nb_block': 'the null test folded from another block is made of the same reference at the branch',
 'kx_align': "every array's header is a multiple of its element's size (GC_HDR_FOR gives a v128 array 16 bytes)",
 'np_nullable': "a param of a type that takes no null has accesses the builder gave no null check",
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('gc', ['./test/run_gc.sh'], {}),
          ('cases', ['./test/run_tests.sh'], {}))
