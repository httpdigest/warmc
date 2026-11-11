"""A callee's likely ways taken into its caller (inline.c pinl_build's hot
start, pinl_idem_write, the starts' own share of a caller's growth; opt.c
ir_pinl_prep), a branch a branch above decided (opt.c thread_decided), and
the blocks a splice gives the caller (inline.c splice).
Killed by test/cases_opt/hot_starts.wat's answers and counts.  Not mutated:
the look for a test with a rare way before a callee is made ready -- one
made ready for nothing has no hot start either -- and an idempotent write's
reads counted only ahead of it: the forwarding of the cleanup leaves no
read of a field after its write in one block."""
N, O = 'src/inline.c', 'src/opt.c'
def m(path, old, new): return (path, [(old, new)])
KEEP = ('        if (ret[bi] && pdin[bi] + pdout[bi] > g_inl.partial_max && !(hotw[bi] && hotret[bi])) ret[bi] = 0;')
WAY = ('            if (s != rway[bi] && !b->succs[s].cold && !g->blocks[t].cold && ret[t]) hotw[t] = 1;')
M = {
 'off': m(N, '  if (!prep) hot_on = false;', '  hot_on = false;'),
 # the callee made ready
 'prep': m(N, '    if (prep) ir_pinl_prep(g, o);', '    ;'),
 'prep_tails': m(O, '  if (o->tails && pass_hot_tails(g)) opt_cleanup(g, o);\n  ir_order(g);\n}', '  ir_order(g);\n}'),
 'prep_size': m(N, '#define PINLINE_PREP_MAX 4000', '#define PINLINE_PREP_MAX 40'),
 # which blocks
 'rare_way': m(N, WAY, '            if (ret[t]) hotw[t] = 1;'),
 'to_return': m(N, KEEP, KEEP.replace('!(hotw[bi] && hotret[bi])', '!hotw[bi]')),
 'likely_on': m(N, '            if (s != rway[bi] && hotw[b->succs[s].target] && hotret[b->succs[s].target]) hotret[bi] = 1;',
                '            if (hotw[b->succs[s].target] && hotret[b->succs[s].target]) hotret[bi] = 1;'),
 'size_more': m(N, '#define PINLINE_HOT_MAX 160', '#define PINLINE_HOT_MAX 1600'),
 'size_less': m(N, '#define PINLINE_HOT_MAX 160', '#define PINLINE_HOT_MAX 60'),
 'size_hot': m(N, '  if (!ncut || !any_ret || n > (hot_on ? g_inl.partial_hot : g_inl.partial_max)) {',
               '  if (!ncut || !any_ret || n > g_inl.partial_max) {'),
 # a write on the way to a cut
 'idem_off': m(N, '  if (w->op != I_FIELD_SET || warmc_getenv("WARMC_NO_PINL_IDEM")) return false;', '  return false;'),
 'idem_read': m(N, '      if (in->op == I_FIELD_GET && in->imm == w->imm) return false;', '      ;'),
 'idem_offset': m(N, '      if (in->op == I_FIELD_GET && in->imm == w->imm) return false;',
                  '      if (in->op == I_FIELD_GET && in->imm != w->imm) return false;'),
 # the starts' share of the growth
 'share_more': m(N, '#define PINLINE_HOT_GROWTH 4000', '#define PINLINE_HOT_GROWTH 40000'),
 'share_less': m(N, '#define PINLINE_HOT_GROWTH 4000', '#define PINLINE_HOT_GROWTH 400'),
 'share_own': m(N, '  if (partial && !nehe && memo_at(memo, fidx)->phot) budget = pbudget;', '  ;'),
 'share_hot': m(N, '    if (hot) *hot = hot_on && n > g_inl.partial_max;', '    if (hot) *hot = hot_on;'),
 # the blocks a splice makes: the ones the callee's entry reaches
 'splice_all': m(N, '    if (!reach[i]) continue;\n    bmap[i] = ir_new_block(f);', '    bmap[i] = ir_new_block(f);'),
 'splice_knob': m(N, '  if (warmc_getenv("WARMC_SPLICE_ALL")) memset(reach, 1, g->nblocks);', '  if (1) memset(reach, 1, g->nblocks);'),
 'splice_walk': m(N, '        if (!reach[t]) { reach[t] = 1; stk[sp++] = t; }', '        if (!reach[t]) { reach[t] = 1; }'),
 # a branch a branch above decided
 'decided_off': m(O, '  bool decided = thread_decided(f);', '  bool decided = false;'),
 'decided_way': m(O, '    b->succs[0] = b->succs[r ? 0 : 1];\n    b->nsuccs = 1;\n    b->term = T_JUMP;\n    g_stats.branch_decided++;',
                  '    b->succs[0] = b->succs[r ? 1 : 0];\n    b->nsuccs = 1;\n    b->term = T_JUMP;\n    g_stats.branch_decided++;'),
}
EQUIVALENT = {}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}),
          ('gc', ['./test/run_gc.sh'], {}))
