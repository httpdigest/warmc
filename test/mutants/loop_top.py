"""ir.c rotate_loops: a loop's one latch laid out above its header where no
block falls into it or three or more reach it, and the back ends' loop
alignment at that latch (ir_loop_header_at, isel.c loop_span and
loop_phase).  Killed by test/cases_opt/loop_top.wat -- its answers, how many
loops -O2 lays out so and how many small loops it aligns -- and
test/cases_opt/loop_phase.wat, whose scan's outer loop goes up and whose
rot's phase is for a loop that starts at its latch."""
IR, I = 'src/ir.c', 'src/isel.c'
def m(old, new, path=IR): return (path, [(old, new)])
M = {
 'off': m('  if (warmc_getenv("WARMC_NO_LOOP_TOP")) return;', '  return;'),
 # which loops
 'nl': m('    if (nl != 1 || pos[l] == UINT32_MAX ||', '    if (nl < 1 || pos[l] == UINT32_MAX ||'),
 'unemitted': m(' || pos[l] == UINT32_MAX || pos[l] < i ||', ' || pos[l] < i ||'),
 'before': m(' || pos[l] == UINT32_MAX || pos[l] < i ||', ' || pos[l] == UINT32_MAX ||'),
 'late': m(' || pos[l] < i || late[l] != late[h]) continue;', ' || pos[l] < i) continue;'),
 'jump_off': m('    if (lb->term != T_JUMP && lb->term != T_BRIF) continue;',
               '    if (lb->term != T_BRIF) continue;'),
 'brif_off': m('    if (lb->term != T_JUMP && lb->term != T_BRIF) continue;',
               '    if (lb->term != T_JUMP) continue;'),
 'term_any': m('    if (lb->term != T_JUMP && lb->term != T_BRIF) continue;\n', ''),
 'header': m('      header |= f->blocks[lb->preds[p]].rpo >= lb->rpo;', '      (void)p;'),
 'falls_off': m('    for (uint32_t p = 0; p < lb->npreds; p++) falls |= lb->preds[p] == eo[pos[l] - 1];\n',
                ''),
 'falls2': m('    if (falls && lb->npreds < 3) continue;', '    if (falls && lb->npreds < 2) continue;'),
 'falls4': m('    if (falls && lb->npreds < 3) continue;', '    if (falls && lb->npreds < 4) continue;'),
 'falls_any': m('    if (falls && lb->npreds < 3) continue;', '    if (falls) continue;'),
 # where the loop starts
 'top_none': m('  for (uint32_t k = 0; k < b->nsuccs; k++)\n    if (f->loop_top[b->succs[k].target] == t) return b->succs[k].target;\n',
               ''),
 'top_header': m('  if (!b->loop_depth || f->loop_top[t] != UINT32_MAX) return UINT32_MAX;',
                 '  if (!b->loop_depth) return UINT32_MAX;'),
 'span_header': m('  return blk_end[last] - s->block_off[eo[oi]];', '  return blk_end[last] - s->block_off[h];', I),
 'phase_header': m('            hdr[f->loop_top[b] != UINT32_MAX ? f->loop_top[b] : b] = 1;',
                   '            hdr[b] = 1;', I),
}
EQUIVALENT = {
 'unemitted': "every block a reachable block's predecessors name is in the order, and the one "
              "block left out of it, the dead return, branches nowhere",
 'before': "the order is reverse postorder but for late blocks, which the test beside it takes: "
           "a latch, a predecessor numbered after its header, comes after it",
 'late': "a late block is a loop's call arm, which goes back where its branch's other edge goes, "
         "or Go's resume table and what only it reaches; neither is ever a header's one latch "
         "while the header is not late -- none in any corpus, compiled with a report of them",
 'term_any': "a header's one latch that is not the header ends in a jump or a test: no other "
             "in any corpus, compiled with a report of them (br_tables of eight edges or fewer "
             "are compare trees from -O1), and none the cases could hold",
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),)
