"""A loop's counters that step together (opt.c pass_iv_lockstep): the ones
taken out, the readers handed a counter's stepped value, the addresses.
Killed by test/cases_opt/lockstep.wat's answers, its counts and its shapes,
or by the verifier, where a mutant leaves a reader of what was taken out --
lockstep_nodce.wat for what dead-code elimination would have taken away.

Not mutated: WARMC_NO_LOCKSTEP, which no case sets; the tests for a handler's
edge that hands a value on (e->eh, ed->eh), which no module the compiler
makes has; the tests that a loop is one its header alone leads
into (inloop[ways[w]], inloop[f->entry]) and that its header is no pad,
which every loop of a wasm function is; e->nargs != np, which the verifier
keeps; the types a counter has, an addition's being of integers; and that
the ways into a header agree among themselves on how far apart two start
(ls_apart's k != *dk): a header with more than one is what an unrolled or
vectorised copy leaves for the trips that remain, where the copy's way out
hands on what the way round it did."""
O = 'src/opt.c'
def m(old, new): return (O, [(old, new)])
STEP = ('      if ((si->op != I_ADD && si->op != I_SUB) || ir_args(f, st)[0] != p ||\n'
        '          !is_const(f, ir_args(f, st)[1], &c))\n')
DELTA = '                        .delta = (si->op == I_ADD ? c : (uint64_t)0 - c) & ls_mask(t) };'
LEAD = ('        if (!iv[d].gone && iv[d].t == iv[c].t && iv[d].delta == iv[c].delta &&\n'
        '            (iv[d].real || iv[d].out || d < c) && ls_apart(f, hb, ways, nways, &iv[c], &iv[d], &dk[c]))')
STRIP = '      off += (in->op == I_ADD) != (side == 1) ? c : (uint64_t)0 - c;'
R2 = ('        if (f->insts[r2].op != I_ADD || !is_const(f, ir_args(f, r2)[1], &k2) ||\n'
      '            (k2 & ls_mask(v->t)) != v->delta || !ls_before(f, v->s, r2))')
LATE_IN = '        else if (after[rb] || (rb == sb && !ls_before(f, r, v->s))) all = false;'
LATE_OUT_I = '        if (!inloop[rb]) all = dominates(f, sb, rb) || ls_left_before(f, inloop, after, sb, rb);'
LATE_OUT_B = '          all = dominates(f, sb, bi) || ls_left_before(f, inloop, after, sb, bi);'
LATE_EDGE = ('            if (b->succs[e].args[a] == v->p && (inloop[b->succs[e].target] || !dominates(f, sb, bi)))\n'
             '              all = false;')
FOR_K = '          (k_) = dk[c_] + ((x_) == v_->s ? v_->delta : 0) - (stepped_ ? l_->delta : 0); \\'
FOR_ST = '          bool stepped_ = dominates(f, f->insts[l_->s].block, (q_));                    \\'
FOR_LATE = '        } else if (late[c_] && (x_) == v_->p && dominates(f, f->insts[v_->s].block, (q_))) { \\'
M = {
 'off': m('  if (warmc_getenv("WARMC_NO_LOCKSTEP")) return false;\n  bool changed = false;',
          '  return false;\n  bool changed = false;'),
 # which loops
 'ways_one': m('#define LS_WAYS 8', '#define LS_WAYS 1'),
 'max_15': m('#define LS_MAX 16', '#define LS_MAX 15'),
 'max_17': m('#define LS_MAX 16', '#define LS_MAX 17'),
 'dirty_header': m('    if (dirty[hb]) { *again = true; continue; }\n', ''),
 'no_wait': m('    if (wait) { *again = true; continue; }', '    if (0) { *again = true; continue; }'),
 'one_run': m('    for (int run = 0; run < 4 && again; run++) {', '    for (int run = 0; run < 1 && again; run++) {'),
 # which parameters are counters
 'any_way_round': m('        else one = x == st;', '        else one = true;'),
 'step_op': m(STEP, '      if (ir_args(f, st)[0] != p ||\n          !is_const(f, ir_args(f, st)[1], &c))\n'),
 'step_of_other': m(STEP, '      if ((si->op != I_ADD && si->op != I_SUB) ||\n          !is_const(f, ir_args(f, st)[1], &c))\n'),
 'step_any_value': m(STEP, '      if ((si->op != I_ADD && si->op != I_SUB) || ir_args(f, st)[0] != p ||\n'
                           '          (!is_const(f, ir_args(f, st)[1], &c) && ((c = 1), 0)))\n'),
 'sub_as_add': m(DELTA, '                        .delta = c & ls_mask(t) };'),
 'add_as_sub': m(DELTA, '                        .delta = ((uint64_t)0 - c) & ls_mask(t) };'),
 'delta_unmasked': m(DELTA, '                        .delta = (si->op == I_ADD ? c : (uint64_t)0 - c) };'),
 # who reads a counter
 'reads_in_loop': m('          if (inloop[rb]) v->real++;', '          if (inloop[rb]) {}'),
 'reads_in_other_loop': m('          else if (f->blocks[rb].loop_depth >= hd) v->out = true;', ''),
 'hands_in_other_loop': m('            if (b->loop_depth >= hd || ir_block_has_eh(f, bi)) v->out = true;',
                          '            if (ir_block_has_eh(f, bi)) v->out = true;'),
 'hands_past_handler_call': m('            if (b->loop_depth >= hd || ir_block_has_eh(f, bi)) v->out = true;',
                              '            if (b->loop_depth >= hd) v->out = true;'),
 'branch_in_loop': m('          if ((b->term == T_BRIF || b->term == T_BRTABLE) && b->term_arg == x) v->real++;', ''),
 'other_parameter': m('              if (ed->target == hb) { if (a != v->k) v->real++; }',
                      '              if (ed->target == hb) { }'),
 'edge_in_loop': m('              else if (inloop[ed->target]) v->real++;', '              else if (0) v->real++;'),
 'index_branches': m('    if (b->term == T_BRIF || b->term == T_BRTABLE) LS_READ(b->term_arg);\n', ''),
 'index_returns': m('    for (uint32_t i = 0; i < b->nret; i++) LS_READ(b->ret_args[i]);\n', ''),
 'index_edges': m('      for (uint32_t j = 0; j < b->succs[e].nargs; j++) LS_READ(b->succs[e].args[j]);\n',
                  '      {}\n'),
 # which are taken out
 'read_in_loop_goes': m('      if (iv[c].real || iv[c].out) continue;', '      if (iv[c].out) continue;'),
 'out_goes': m('      if (iv[c].real || iv[c].out) continue;', '      if (iv[c].real) continue;'),
 'lead_gone': m(LEAD, LEAD.replace('!iv[d].gone && ', '')),
 'lead_type': m(LEAD, LEAD.replace('iv[d].t == iv[c].t && ', '')),
 'lead_delta': m(LEAD, LEAD.replace(' && iv[d].delta == iv[c].delta', '')),
 'lead_any_unread': m(LEAD, LEAD.replace('|| d < c)', '|| d != c)')),
 'lead_no_unread': m(LEAD, LEAD.replace(' || d < c)', ')')),
 'lead_any_start': m(LEAD, LEAD.replace('ls_apart(f, hb, ways, nways, &iv[c], &iv[d], &dk[c])', 'true')),
 # how far apart two start
 'apart_no_merge': m('    if (!ls_diff(f, e->args[c->k], e->args[d->k], 2, &k)) return false;',
                     '    if (!ls_diff(f, e->args[c->k], e->args[d->k], 0, &k)) return false;'),
 'diff_same': m('  if (a == b) { *k = off & mask; return true; }\n', ''),
 'diff_consts': m('  if (is_const(f, a, &ca) && is_const(f, b, &cb)) { *k = (off + ca - cb) & mask; return true; }\n', ''),
 'diff_consts_swapped': m('{ *k = (off + ca - cb) & mask; return true; }', '{ *k = (off + cb - ca) & mask; return true; }'),
 'diff_unmasked': m('{ *k = (off + ca - cb) & mask; return true; }', '{ *k = (off + ca - cb); return true; }'),
 'diff_no_strip': m('    for (int i = 0; i < 4; i++) {\n      const Inst *in = &f->insts[*x];',
                    '    for (int i = 0; i < 0; i++) {\n      const Inst *in = &f->insts[*x];'),
 'diff_strip_sign': m(STRIP, '      off += (in->op == I_ADD) == (side == 1) ? c : (uint64_t)0 - c;'),
 'diff_strip_side': m(STRIP, '      off += (in->op == I_ADD) ? c : (uint64_t)0 - c;'),
 'diff_strip_any_op': m('      if ((in->op != I_ADD && in->op != I_SUB) || !is_const(f, ir_args(f, *x)[1], &c)) break;',
                        '      if (in->nargs != 2 || !is_const(f, ir_args(f, *x)[1], &c)) break;'),
 'diff_off_dropped': m('  *k = (off + k0) & mask;\n  return true;', '  *k = k0 & mask;\n  return true;'),
 'diff_ways_agree': m('    if (w && kw != k0) return false;\n', ''),
 # addresses
 'addr_gone': m('      if (v->gone) continue;\n      for (uint32_t u = ui.first[v->p]; u != UINT32_MAX; u = ui.next[u]) {\n        Ref r = ui.user[u];\n        if (r == v->s',
                '      for (uint32_t u = ui.first[v->p]; u != UINT32_MAX; u = ui.next[u]) {\n        Ref r = ui.user[u];\n        if (r == v->s'),
 'addr_any_op': m('        if (r == v->s || f->insts[r].op != I_ADD) continue;',
                  '        if (r == v->s || f->insts[r].nargs != 2) continue;'),
 'addr_two_readers': m('        if (u2 == UINT32_MAX || ui.next[u2] != UINT32_MAX || xi.first[r] != UINT32_MAX) continue;',
                       '        if (u2 == UINT32_MAX || xi.first[r] != UINT32_MAX) continue;'),
 'addr_leaves': m('        if (u2 == UINT32_MAX || ui.next[u2] != UINT32_MAX || xi.first[r] != UINT32_MAX) continue;',
                  '        if (u2 == UINT32_MAX || ui.next[u2] != UINT32_MAX) continue;'),
 'addr_any_op2': m(R2, R2.replace('f->insts[r2].op != I_ADD', 'f->insts[r2].nargs != 2')),
 'addr_any_const': m(R2, R2.replace('(k2 & ls_mask(v->t)) != v->delta || ', '')),
 'addr_before_step': m(R2, R2.replace(' || !ls_before(f, v->s, r2))', ')')),
 'addr_self': m('        if ((a1[0] == v->p) == (a1[1] == v->p)) continue;',
                '        if (a1[0] != v->p && a1[1] != v->p) continue;'),
 'addr_untouched': m('        if (x < ni0) touched[x] = 1;\n', ''),
 'addr_count': m('        changed = true;\n        g_stats.lockstep_addrs++;', '        changed = true;'),
 # the stepped value for the counter
 'late_out': m('      if (v->gone || v->out) continue;\n      uint32_t sb', '      if (v->gone) continue;\n      uint32_t sb'),
 'late_read_after_block': m(LATE_IN, '        else if (rb == sb && !ls_before(f, r, v->s)) all = false;'),
 'late_read_after_step': m(LATE_IN, '        else if (after[rb]) all = false;'),
 'late_past_any': m(LATE_OUT_I, '        if (!inloop[rb]) all = true;'),
 'late_past_below_only': m(LATE_OUT_I, '        if (!inloop[rb]) all = dominates(f, sb, rb);'),
 'late_hands_any': m(LATE_OUT_B, '          all = true;'),
 'late_hands_below_only': m(LATE_OUT_B, '          all = dominates(f, sb, bi);'),
 'left_many_ways': m('      return t->npreds == 1 && !after[d];', '      return !after[d];'),
 'late_all_blocks': m('        if (bi != sb && !after[bi]) continue;\n', ''),
 'late_not_own_block': m('        if (bi != sb && !after[bi]) continue;\n', '        if (!after[bi]) continue;\n'),
 'late_branch': m('        if ((b->term == T_BRIF || b->term == T_BRTABLE) && b->term_arg == v->p) all = false;\n', ''),
 'late_edge_in_loop': m(LATE_EDGE, LATE_EDGE.replace('(inloop[b->succs[e].target] || !dominates(f, sb, bi))', '!dominates(f, sb, bi)')),
 'after_through_header': m('          if (t == hb || t >= nb0 || !inloop[t] || after[t]) continue;',
                           '          if (t >= nb0 || !inloop[t] || after[t]) continue;'),
 # what a reader is handed
 'never_stepped': m(FOR_ST, '          bool stepped_ = false;                                                        \\'),
 'always_stepped': m(FOR_ST, '          bool stepped_ = true;                                                         \\'),
 'for_no_dk': m(FOR_K, FOR_K.replace('dk[c_] + ', '')),
 'for_step_of_gone': m(FOR_K, FOR_K.replace('((x_) == v_->s ? v_->delta : 0)', '0')),
 'for_step_of_lead': m(FOR_K, FOR_K.replace(' - (stepped_ ? l_->delta : 0)', '')),
 'for_late_above': m(FOR_LATE, '        } else if (late[c_] && (x_) == v_->p) { \\'),
 'for_late_sign': m('          (k_) = (uint64_t)0 - v_->delta;                                               \\',
                    '          (k_) = v_->delta;                                                             \\'),
 'for_unmasked': m('        (k_) &= ls_mask(v_->t);                                                         \\',
                   '                                                                                        \\'),
 'exit_always_block': m('          if (!make && adds) {', '          if (!make) {'),
 'exit_never_block': m('          if (!make && adds) {', '          if (0) {'),
 'exit_edges_in_loop': m('        if (tg < nb0 && inloop[tg]) continue;\n        uint32_t at = UINT32_MAX;',
                         '        uint32_t at = UINT32_MAX;'),
 'exit_clean': m('            dirty[q] = 1;\n            if (tg < nb0) dirty[tg] = 1;', ''),
 'past_readers_in_loop': m('          if (f->insts[r].op == I_NOP || inloop[rb]) continue;\n          Ref base;',
                           '          if (f->insts[r].op == I_NOP) continue;\n          Ref base;'),
 'past_only_old': m('      for (int w = 0; w < 2; w++) {\n        Ref x = w ? iv[c].s : iv[c].p;\n        for (uint32_t u = ui.first[x]; u != UINT32_MAX; u = ui.next[u]) {\n          Ref r = ui.user[u];\n          uint32_t rb = f->insts[r].block;\n          if (f->insts[r].op == I_NOP || inloop[rb]) continue;',
                    '      for (int w = 0; w < 1; w++) {\n        Ref x = w ? iv[c].s : iv[c].p;\n        for (uint32_t u = ui.first[x]; u != UINT32_MAX; u = ui.next[u]) {\n          Ref r = ui.user[u];\n          uint32_t rb = f->insts[r].block;\n          if (f->insts[r].op == I_NOP || inloop[rb]) continue;'),
 'past_branch_kept': m('          if ((b->term == T_BRIF || b->term == T_BRTABLE) && b->term_arg == x) b->term_arg = y;\n', ''),
 'past_return_kept': m('            if (b->ret_args[i] == x) b->ret_args[i] = y;', '            {}'),
 'past_edge_kept': m('              if (b->succs[e].args[a] == x) b->succs[e].args[a] = y;', '              {}'),
 'step_stays': m('      ir_make_nop(&f->insts[iv[c].s]);\n      g_stats.lockstep++;', '      g_stats.lockstep++;'),
 'count': m('      ir_make_nop(&f->insts[iv[c].s]);\n      g_stats.lockstep++;', '      ir_make_nop(&f->insts[iv[c].s]);'),
 'exits_count': m('            oe->args[a] = y;\n            if (exit_s) g_stats.lockstep_exits++;', '            oe->args[a] = y;'),
}
EQUIVALENT = {}
SUITES = (('optcases', ['python3', 'test/run_optcases.py', 'lockstep', 'lockstep_nodce'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}))
