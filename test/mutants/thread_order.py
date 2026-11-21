"""ir.c order_threaded and ir_order_threaded: the ordering after a jump
threading made from the one before it and what the threading says it did
(ir_thread_copy, ir_thread_edge; opt.c thread_edge, thread_edge_copy,
thread_path_copy).  Killed by the counts of the two kinds of ordering that
test/cases_opt/thread_order.wat, thread_known.wat and thread_eval.wat
give, by what a wrong order makes of their code, or under
WARMC_VERIFY_GATES=1, which makes a whole ordering after each, compares
every block's facts, and goes on from what the one after threading left.
bce_version.wat and gc_range_bounds.wat are run for their loops' copies,
whose headers a test's edge enters with no block between.

Not mutated: what the callers make sure of and order_threaded asks all the
same, falling back to a whole ordering -- the copies noted in the order
made, of old blocks in reach, with ways out their models have, each with
one way in, the edges from old blocks in reach to copies of what they left
or of the block that jumps to it; what a block out of reach is left with
besides its flag (its place and its dominator, which nothing reads of
one); the signature's invalidation, which a graph that changed fails
anyway; f->dom_stale, which only WARMC_CHECK_DOM reads; and where a copy
goes beside its model, before or after, either of which is an order of the
kind.  Nor which loops the second search of a graph not reducible finds
(ir_order's OK_DOMS against OK_ALL), which only the code of Go's large
functions shows: 95 of the corpus' modules compile to other bytes."""
I = 'src/ir.c'
O = 'src/opt.c'
def mi(old, new): return (I, [(old, new)])
def mo(old, new): return (O, [(old, new)])
NOTE1 = '  ir_thread_copy(f, x, B);\n  ir_thread_edge(f, pi, was, x);\n  td_push(made, x);\n  g_stats.branch_simplified++;\n  return true;\n}\n\nstatic void thread_repair(IrFunc *f, uint32_t B, TdVec *made);'
NOTE2 = '  f->blocks[x].cold = f->blocks[pi].cold;\n  f->blocks[pi].succs[s].target = x;\n  ir_thread_copy(f, x, B);\n  ir_thread_edge(f, pi, was, x);'
NOTE3 = '  ir_thread_copy(f, y2, Y);\n  ir_thread_copy(f, b2, B);\n  ir_thread_edge(f, P, Y, y2);'
KEY = '    uint32_t kk = f->blocks[C[2 * k + 1]].rpo, j = k;'
START = '    if (ob->rpo < start) start = ob->rpo;'
DEAD = ('    mark[bi] |= 2;\n    if (dead0 == UINT32_MAX) dead0 = i;\n'
        '    for (uint32_t s = 0; s < b->nsuccs; s++) pred_drop(&f->blocks[b->succs[s].target], bi);')
DROP = ('  for (uint32_t i = 0; i < t->npreds; i++)\n'
        '    if (t->preds[i] == p) {\n'
        '      memmove(t->preds + i, t->preds + i + 1, (t->npreds - i - 1) * sizeof(uint32_t));\n'
        '      t->npreds--;\n'
        '      return true;\n'
        '    }\n'
        '  return false;')
DONE = '  f->nthr_edges = f->nthr_copies = 0;\n  f->ord_nblocks = nb;\n  f->dom_stale = true;'
M = {
 # when
 'not_reducible': mi('  if (!f->reducible || nb - nb0 != nc) return false;', '  if (nb - nb0 != nc) return false;'),
 'model_heads': mi('    if (OT_HEADS(m)) return false;', '    (void)OT_HEADS(m);'),
 'old_heads': mi('    if (OT_HEADS(old)) return false;', '    (void)OT_HEADS(old);'),
 'off': mi('  if (!g_order_light || !g_order_inc || !order_threaded(f)) {', '  if (1) {'),
 # the predecessors
 'edge_stays': mi('    if (!pred_drop(&f->blocks[old], src)) return false;', '    (void)old;'),
 'copy_no_pred': mi('    zb->preds[zb->npreds++] = src;', '    (void)src;'),
 'drops_every': mi(DROP, ('  bool any = false;\n'
                          '  for (uint32_t i = 0; i < t->npreds; i++)\n'
                          '    if (t->preds[i] == p) {\n'
                          '      memmove(t->preds + i, t->preds + i + 1, (t->npreds - i - 1) * sizeof(uint32_t));\n'
                          '      t->npreds--;\n'
                          '      i--;\n'
                          '      any = true;\n'
                          '    }\n'
                          '  return any;')),
 'drops_last': mi('      memmove(t->preds + i, t->preds + i + 1, (t->npreds - i - 1) * sizeof(uint32_t));\n',
                  '      t->preds[i] = t->preds[t->npreds - 1];\n'),
 # the order
 'copies_last': mi(KEY, '    uint32_t kk = n - 1, j = k;'),
 'copies_at_start': mi(KEY, '    uint32_t kk = 1, j = k;'),
 'order_short': mi('  n += nc;\n  f->norder = n;', '  f->norder = n;'),
 # the walk
 'start_none': mi(START, ''),
 'start_last': mi(START, '    if (start == UINT32_MAX || ob->rpo > start) start = ob->rpo;'),
 'start_model': mi(START, '    if (f->blocks[m].rpo < start) start = f->blocks[m].rpo;'),
 'edge_back_counts': mi('      if (f->blocks[pi].rpo >= i) continue;       /* an edge back */\n', ''),
 'first_pred': mi('      newidom = x;\n      stamp[x] = cur;\n    }\n    if (newidom != REF_NONE) { b->idom = newidom; continue; }',
                  '      stamp[x] = cur;\n    }\n    if (newidom != REF_NONE) { b->idom = newidom; continue; }'),
 'idom_kept': mi('    if (newidom != REF_NONE) { b->idom = newidom; continue; }', '    if (newidom != REF_NONE) continue;'),
 # out of reach
 'reach_kept': mi(DEAD, DEAD.replace('    mark[bi] |= 2;\n', '')),
 'dead_edges_stay': mi(DEAD, DEAD.replace('    for (uint32_t s = 0; s < b->nsuccs; s++) pred_drop(&f->blocks[b->succs[s].target], bi);', '')),
 'dead_flag': mi('        b->reachable = false;\n        b->rpo = 0xffffffffu;', '        b->rpo = 0xffffffffu;'),
 'dead_listed': mi('    f->norder = k;\n  }\n  f->nthr_edges', '  }\n  f->nthr_edges'),
 'dead_unnumbered': mi('      f->order[k] = bi;\n      b->rpo = k++;', '      f->order[k++] = bi;'),
 # for the next one
 'journal_kept': mi(DONE, DONE.replace('  f->nthr_edges = f->nthr_copies = 0;\n', '')),
 'blocks_not_noted': mi(DONE, DONE.replace('  f->ord_nblocks = nb;\n', '')),
 'whole_keeps_journal': mi('  f->nthr_edges = f->nthr_copies = 0;\n  f->ord_nblocks = nb;\n  f->dom_stale = false;',
                           '  f->ord_nblocks = nb;\n  f->dom_stale = false;'),
 'whole_blocks_not_noted': mi('  f->nthr_edges = f->nthr_copies = 0;\n  f->ord_nblocks = nb;\n  f->dom_stale = false;',
                              '  f->nthr_edges = f->nthr_copies = 0;\n  f->dom_stale = false;'),
 # what threading says
 'edge_unnoted': mo(NOTE1, NOTE1.replace('  ir_thread_edge(f, pi, was, x);\n', '')),
 'fresh_unnoted': mo(NOTE1, NOTE1.replace('  ir_thread_copy(f, x, B);\n', '')),
 'forwarder_as_merge': mo('          if (!thread_edge(f, B, pi, qi, s, pick, &made)) f->blocks[qi].succs[s] = was;',
                          '          if (!thread_edge(f, B, B, qi, s, pick, &made)) f->blocks[qi].succs[s] = was;'),
 'copy_edge_unnoted': mo(NOTE2, NOTE2.replace('  ir_thread_edge(f, pi, was, x);', '')),
 'copy_unnoted': mo(NOTE2, NOTE2.replace('  ir_thread_copy(f, x, B);\n', '')),
 'copy_forwarder_as_merge': mo('          if (thread_edge_copy(f, B, pi, qi, s, pick, &made, &cps)) *budget -= nc;',
                               '          if (thread_edge_copy(f, B, B, qi, s, pick, &made, &cps)) *budget -= nc;'),
 'path_second_unnoted': mo(NOTE3, NOTE3.replace('  ir_thread_copy(f, b2, B);\n', '')),
 'path_models_swapped': mo(NOTE3, '  ir_thread_copy(f, y2, B);\n  ir_thread_copy(f, b2, Y);\n  ir_thread_edge(f, P, Y, y2);'),
 'path_edge_left_merge': mo(NOTE3, NOTE3.replace('ir_thread_edge(f, P, Y, y2);', 'ir_thread_edge(f, P, B, y2);')),
 'path_edge_unnoted': mo(NOTE3, NOTE3.replace('\n  ir_thread_edge(f, P, Y, y2);', '')),
}
EQUIVALENT = {
 'old_heads': "a block an edge left is the copy's model, asked just before, or a block that only jumps to the model; "
              "one of those that heads a loop is the model's only way in (another would be a second way into the loop), "
              "and the two are one block before any threading",
}
CASES = ['python3', 'test/run_optcases.py', 'thread_order', 'thread_known', 'thread_eval', 'thread_sound',
         'bce_version', 'gc_range_bounds', 'go_resume', 'go_tables', 'key_split']
SUITES = (('optcases', CASES, {}),
          ('gates', CASES, {'WARMC_VERIFY_GATES': '1'}))
