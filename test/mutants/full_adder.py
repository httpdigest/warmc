"""isel.c find_full_adders: GMP's generic mpn_add_n and mpn_sub_n limbs as
adcs and sbcs, the carry handed on in the flag; what it takes, and its
emission (isel_ops.inc emit_full_adder).  Killed by
test/cases_opt/full_adder.wat (made by test/gen_fulladder.py)."""
I, IO = 'src/isel.c', 'src/isel_ops.inc'
def m(old, new, path=I): return (path, [(old, new)])
M = {
 # what a compare is
 'lt_gt': m('  *x = a[in->op == I_GTU];\n  *y = a[in->op != I_GTU];', '  *x = a[0];\n  *y = a[1];'),
 'lt_le': m('  if (in->nargs != 2 || (in->op != I_LTU && in->op != I_GTU)) return false;',
            '  if (in->nargs != 2 || (in->op != I_LTU && in->op != I_GTU && in->op != I_LEU)) return false;'),
 # what is claimed already
 'own_all': m('''  return !s->folded[v] && !s->fused[v] && !s->imm_fold[v] && !s->addr_fuse[v] &&
         !s->ptr_fuse[v] && !s->zx_fuse[v] && !s->bfx[v].ok && !s->csinc[v] && !s->fa[v];''',
              '  return true;'),
 'own_fa': m(' && !s->csinc[v] && !s->fa[v];', ' && !s->csinc[v];'),
 'operand_u': m('fa_operand(s, ua[0]) && fa_operand(s, ua[1]) && fa_operand(s, cin)',
                'fa_operand(s, ua[1]) && fa_operand(s, cin)'),
 'operand_v': m('fa_operand(s, ua[0]) && fa_operand(s, ua[1]) && fa_operand(s, cin)',
                'fa_operand(s, ua[0]) && fa_operand(s, cin)'),
 'operand_cin': m('fa_operand(s, ua[0]) && fa_operand(s, ua[1]) && fa_operand(s, cin)',
                  'fa_operand(s, ua[0]) && fa_operand(s, ua[1])'),
 # the limb's shape
 'add_s': m('if (f->insts[t].op != I_ADD || f->insts[t].nargs != 2 || (y != t && y != o)) continue;',
            'if (f->insts[t].nargs != 2 || (y != t && y != o)) continue;'),
 'add_y': m('if (f->insts[t].op != I_ADD || f->insts[t].nargs != 2 || (y != t && y != o)) continue;',
            'if (f->insts[t].op != I_ADD || f->insts[t].nargs != 2) continue;'),
 'sub_rform': m('    if (yi->op == I_SUB && yi->nargs == 2 && ir_args(f, y)[0] == sv) { r = y; cin = ir_args(f, y)[1]; }',
                '    if (0) { r = y; cin = ir_args(f, y)[1]; }'),
 'sub_search': m('            ir_args(f, t)[0] == sv && ir_args(f, t)[1] == y)', '            0)'),
 'sv_none': m('  if (r == REF_NONE || sv == REF_NONE) return false;', '  if (r == REF_NONE) return false;'),
 'c1_add': m('                 : !(x == sv && (y == ua[0] || y == ua[1])))', '                 : !(x == sv))'),
 'c1_sub': m('  if (k & FA_SUB ? !(x == ua[0] && (y == ua[1] || y == sv))', '  if (k & FA_SUB ? !(x == ua[0])'),
 'c1_subs': m('  if (k & FA_SUB ? !(x == ua[0] && (y == ua[1] || y == sv))', '  if (k & FA_SUB ? !(x == ua[0] && y == ua[1])'),
 'kb': m('  if (ir_known_bits(f, cin, 4) & ~1ull) return false;\n', ''),
 'comb_or': m('      if ((oin->op != I_OR && oin->op != I_XOR && oin->op != I_ADD) || oin->nargs != 2 ||',
              '      if ((oin->op != I_XOR && oin->op != I_ADD) || oin->nargs != 2 ||'),
 'comb_xor': m('      if ((oin->op != I_OR && oin->op != I_XOR && oin->op != I_ADD) || oin->nargs != 2 ||',
               '      if ((oin->op != I_OR && oin->op != I_ADD) || oin->nargs != 2 ||'),
 'comb_add': m('      if ((oin->op != I_OR && oin->op != I_XOR && oin->op != I_ADD) || oin->nargs != 2 ||',
               '      if ((oin->op != I_OR && oin->op != I_XOR) || oin->nargs != 2 ||'),
 'ext_find': m('        if (s->folded[cin] && f->insts[cin].op == I_EXTENDU && f->insts[cin].nargs == 1)\n          cin = ir_args(f, cin)[0];\n', ''),
 # what else may read it, and where
 'uses_c1': m('        bool ok = uses[c1] == 1 && uses[c2] == 1 && uses[sv] == refs &&',
              '        bool ok = uses[c2] == 1 && uses[sv] == refs &&'),
 'uses_c2': m('        bool ok = uses[c1] == 1 && uses[c2] == 1 && uses[sv] == refs &&',
              '        bool ok = uses[c1] == 1 && uses[sv] == refs &&'),
 'uses_s': m('        bool ok = uses[c1] == 1 && uses[c2] == 1 && uses[sv] == refs &&',
             '        bool ok = uses[c1] == 1 && uses[c2] == 1 &&'),
 'refs_c': m('        for (uint32_t m = 0; m < 2; m++) refs += (ir_args(f, c1)[m] == sv) + (ir_args(f, c2)[m] == sv);\n', ''),
 'block': m('          ok = f->insts[own[m]].block == bi && fa_own(s, own[m]);', '          ok = fa_own(s, own[m]);'),
 'pos': m('        if (!ok || pos[r] > i) continue;', '        if (!ok) continue;'),
 'reorder': m('    memcpy(b->insts, out, m * sizeof *out);\n', ''),
 # emission
 'em_held': m('  if (s->fa_flag != cin || s->fa_epoch != B->flags_epoch || s->fa_sub != sub) {',
              '  if (1) {', IO),
 'em_epoch': m('  if (s->fa_flag != cin || s->fa_epoch != B->flags_epoch || s->fa_sub != sub) {',
               '  if (s->fa_flag != cin || s->fa_sub != sub) {', IO),
 'em_sense': m('  if (s->fa_flag != cin || s->fa_epoch != B->flags_epoch || s->fa_sub != sub) {',
               '  if (s->fa_flag != cin || s->fa_epoch != B->flags_epoch) {', IO),
 'em_cmp1': m('    else a64_cmp_imm(B, SF32, c, 1);', '    else a64_cmp_imm(B, SF32, c, 0);', IO),
 'em_cmpzr': m('    if (sub) a64_cmp_reg(B, SF32, RZR, c);', '    if (sub) a64_cmp_reg(B, SF32, c, RZR);', IO),
 'em_cc': m('    a64_cset(B, SF32, d, sub ? CC_LO : CC_HS);', '    a64_cset(B, SF32, d, sub ? CC_HS : CC_LO);', IO),
 'em_flag': m('    commit(s, v, d);\n    s->fa_flag = v;', '    commit(s, v, d);\n    s->fa_flag = REF_NONE;', IO),
 'em_ext': m('  if (f->insts[cin].op == I_EXTENDU && f->insts[cin].nargs == 1) cin = ir_args(f, cin)[0];',
             '', IO),
 'em_reset': m('  s->fa_flag = REF_NONE;\n  uint32_t pck', '  uint32_t pck', IO),
}
EQUIVALENT = {
 'own_all':
  "no other fusion takes a full adder's parts: its compares and s are read by it alone, its or is of two compares, and r, read by a compare, is no address; the check stands against one made later",
 'own_fa':
  "an instruction is in one limb at most: a compare has one reader, s is folded by the first, and a second limb through the same r would read the same carry",
 'operand_u':
  "nothing folds an add's or a sub's first operand into it: shifts, extends and multiplies fold as the second, the only one I_ADD's emission reads so",
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}))
