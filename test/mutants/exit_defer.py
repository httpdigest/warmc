"""ir.c dfs_defer_exit: the layout takes a loop's exit to a block that the
loop's other way out also reaches from that other way, which then falls
into it -- in the order the code is emitted in alone (ir_emit_order); and
arm64's two-way branch with moves both ways, one of them into the next
block, puts the other's moves out of line (isel_ops.inc, stubs_out).  Killed by test/cases_opt/exit_defer.wat -- its answers, where
find's and near's found blocks lie (after the first ret or not), and the
count of exits taken so (exit_defers), which twoexit's and twopred's
near misses would add to; near's unconditional branches and stubs_out, and
test/cases_opt/loop_phase.wat's rot, whose loop's way round, the likelier,
would branch out to a stub and back."""
O = 'src/isel_ops.inc'
I = 'src/ir.c'
def m(old, new, path=I): return (path, [(old, new)])
M = {
 'emit_only': m('  if (defer_on && f->reducible && f->cap_loop_head >= f->nblocks && f->norder) {', '  if (0) {'),
 'emit_order': m('      order = deferred;\n', ''),
 'stub_off': m('  s->no_stub_out = warmc_getenv("WARMC_NO_STUB_OUT") != NULL;', '  s->no_stub_out = true;', 'src/isel.c'),
 'stub_likelier': m('''                 (t->target == next_bi
                      ? ir_succ_score(f, b, (uint32_t)(t - b->succs)) >= ir_succ_score(f, b, (uint32_t)(e - b->succs))
                      : ir_succ_score(f, b, (uint32_t)(e - b->succs)) >= ir_succ_score(f, b, (uint32_t)(t - b->succs)))) {''',
                    '''                 1) {''', O),
 'stub_swap': m('''        if (t->target == next_bi) {
          Edge *tmp = t; t = e; e = tmp;''', '''        if (0) {
          Edge *tmp = t; t = e; e = tmp;''', O),
 'stub_falls': m('''        st->site = br_cc(s, kind, cc, zsf, zreg);
        parallel_copy(s, em, en);
        g_stats.stubs_out++;''', '''        st->site = br_cc(s, kind, cc, zsf, zreg);
        g_stats.stubs_out++;''', O),
 'off': m('if (defer_on < 0) defer_on = warmc_getenv("WARMC_NO_EXIT_DEFER") == NULL;', 'defer_on = 0;'),
 'exits_only': m('if (tb->loop_depth >= d || t == b || b >= f->cap_loop_head || f->loop_head[b] == UINT32_MAX)',
                 'if (t == b || b >= f->cap_loop_head || f->loop_head[b] == UINT32_MAX)'),
 'unseen': m('    if (q == b || q >= f->nblocks || seen[q] || !f->blocks[q].reachable ||',
             '    if (q == b || q >= f->nblocks || !f->blocks[q].reachable ||'),
 'forward': m('        f->blocks[q].rpo >= tb->rpo)\n      continue;', '        0)\n      continue;'),
 'outside': m('    if (qb->loop_depth < d && qb->npreds == 1 && qb->preds[0] < f->cap_loop_head &&',
              '    if (qb->npreds == 1 && qb->preds[0] < f->cap_loop_head &&'),
 'one_pred': m('    if (qb->loop_depth < d && qb->npreds == 1 && qb->preds[0] < f->cap_loop_head &&',
               '    if (qb->loop_depth < d && qb->preds[0] < f->cap_loop_head &&'),
 'same_loop': m('        f->loop_head[qb->preds[0]] == f->loop_head[b]) {', '        1) {'),
 'count': m('      f->ord_defers++;\n', ''),
 'innermost': m('if (f->loop_head[k] == UINT32_MAX || f->blocks[t].rpo > f->blocks[f->loop_head[k]].rpo)',
                'if (f->loop_head[k] == UINT32_MAX || f->blocks[t].rpo < f->blocks[f->loop_head[k]].rpo)'),
}
EQUIVALENT = {
 'exits_only': "without it the rule also takes a way into another loop's header, of the same "
               "depth or deeper; two ways out of a loop never meet at one -- wasm enters a loop "
               "only by falling into it, and LICM gives every header a preheader of its own -- "
               "and the corpora's 325 modules compile to the same objects with it and without",
 'unseen': "a predecessor the search has reached has pushed the block already: the search "
           "finishes the predecessor's descendants, the block among them, before it goes on, so "
           "the block is seen by the time the exit is, and not taking it there changes nothing",
 'forward': "a predecessor that reaches the block by a back edge is dominated by it, and so is "
            "its one predecessor in the exit's loop, and the loop's header: the block is then "
            "the exit's ancestor, seen before the exit is taken",
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),)
