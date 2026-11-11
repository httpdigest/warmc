"""The hot way's blocks copied for the rare ways back into it (opt.c
pass_hot_tails, hot_set_build), what LICM leaves in a rare block, and the
two readings of the rules they take (ir.c ir_unlikely_way_sure,
ir_test_bare_null).  Killed by test/cases_opt/hot_tails.wat's answers,
shapes and counts; `unmap` by the SSA check every case is compiled under."""
O, R = 'src/opt.c', 'src/ir.c'
def m(old, new, path=O): return (path, [(old, new)])
M = {
 'off': m('  if (o->tails && TP("tails", pass_hot_tails(f))) {', '  if (0 && o->tails && TP("tails", pass_hot_tails(f))) {'),
 # the hot set
 'rare_way': m('  return e->eh || e->cold || t->cold || t->is_pad || way[p] == s;', '  return e->eh || e->cold || t->cold || t->is_pad;'),
 'forced_exit': m('          if (hasx[c] == it || !hot[c]) continue;', '          if (1) continue;'),
 'tests_only': m('  if (!hs.tests) { hot_set_free(&hs); return false; }\n', ''),
 # which blocks are copied
 'header_seed': m('        if (inR[t] || !hot[t] || HT_HEAD(t) || t == f->entry || f->blocks[t].diverges) continue;',
                  '        if (inR[t] || !hot[t] || t == f->entry || f->blocks[t].diverges) continue;'),
 'header_closure': m('        if (!hot[t] || HT_HEAD(t) || t == f->entry || f->blocks[t].diverges || inR[t]) continue;',
                     '        if (!hot[t] || t == f->entry || f->blocks[t].diverges || inR[t]) continue;'),
 # a bare null test's rare way
 'strict_off': m('    if (!any_way || any_only || nR <= 2 * nrare) break;', '    break;'),
 'ratio': m('    if (!any_way || any_only || nR <= 2 * nrare) break;', '    if (!any_way || any_only || nR <= 20 * nrare) break;'),
 'word_null': m("                       (any_way || way[p] != (s) || !ir_test_bare_null(f, &f->blocks[p])))",
                "                       (any_way || way[p] != (s)))"),
 'slow_call': m('      for (uint32_t i = 0; i < cb->ninsts && !sl; i++) sl = is_call_op(f->insts[cb->insts[i]].op);\n', ''),
 'slow_spread': m('        if (t < nb0 && !hot[t]) slow[t] = 1;\n', ''),
 'bare_null': m('  return c < f->ninsts && f->insts[c].op == I_REF_IS_NULL;', '  return false;', R),
 # the copies wired in
 'redirect': m('      if (hot[p] ? HT_WORD(p, s) : slow[p]) pb->succs[s].target = bcopy[t];',
               '      if (!hot[p] && slow[p]) pb->succs[s].target = bcopy[t];'),
 'unmap': m("      if (o_ != REF_NONE && f->insts[o_].block != x) {                        \\",
            "      if (0) {                                                                \\"),
 'const_copy': m('    if (!esc[v] || f->insts[v].op != I_CONST) continue;', '    continue;'),
 'pre': m('    if (nfwd[h] < 2 || !entered[h]) continue;', '    continue;'),
 # LICM
 'licm_rare': m('      if (licm_nocold && (b->cold || (rare_stay && !lhs.hot[bi])) && bi != hb) continue;',
                '      if (licm_nocold && b->cold && bi != hb) continue;'),
 'licm_inner': m('      if (inner && lsize <= US_LOOP_MAX) rare_stay = false;\n', ''),
 'licm_inner_size': m('      if (inner && lsize <= US_LOOP_MAX) rare_stay = false;', '      if (lsize <= US_LOOP_MAX) rare_stay = false;'),
 'licm_sure': m('  bool lhs_ok = licm_nocold && ir_order_current(f) && hot_set_build(f, &lhs, true);',
                '  bool lhs_ok = licm_nocold && ir_order_current(f) && hot_set_build(f, &lhs, false);'),
 'sure_tag': m('    if (strong >= 2 && (rhs_const || lhs->op == I_CONST)) return 0;\n', '', R),
}
EQUIVALENT = {}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}))
