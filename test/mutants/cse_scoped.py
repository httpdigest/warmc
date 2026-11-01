"""opt.c pass_cse: the walk down the dominator tree, whose table holds only
what the blocks from the entry down to the current one put there, each
block's entries leaving it, the last in first, once the walk is past the
block's subtree -- also when the table grew in between.  Killed by
test/cases_opt/cse_scoped.wat: its answers, the verifier's check that every
use is dominated, and dom's one multiply."""
O = 'src/opt.c'
def m(old, new, path=O): return (path, [(old, new)])
POP = '''    while (sp && f->blocks[open[sp - 1]].dom_out < b->dom_in) {
      sp--;
      while (nlog > mark[sp]) { tab[log[--nlog]].key = 0; count--; }
    }'''
M = {
 # a sibling's twin taken: never leaving a subtree
 'keep_subtrees': m(POP, POP.replace('while (sp &&', 'while (0 && sp &&') + '\n    if (sp == n) sp = 0;'),
 # a dominating twin not taken: leaving every block at once
 'leave_all': m(POP, POP.replace('f->blocks[open[sp - 1]].dom_out < b->dom_in', '1')),
 'leave_one_more': m(POP, POP + '\n    if (sp) { sp--; while (nlog > mark[sp]) { tab[log[--nlog]].key = 0; count--; } }'),
 # a subtree's end: dom_out against the block's dom_in
 'end_test': m('f->blocks[open[sp - 1]].dom_out < b->dom_in', 'f->blocks[open[sp - 1]].dom_in < b->dom_in'),
 # leaving: the block's own entries, from its mark
 'mark': m('    mark[sp++] = nlog;', '    mark[sp++] = 0;'),
 # the growth moves every entry, and the log must follow it
 'grow_log': m('    t[s] = e;\n    log[i] = s;', '    t[s] = e;'),
 # an entry is made only where no twin was found
 'insert_twin': m('''        g_stats.cse_removed++;
        changed = true;
        continue;''', '''        g_stats.cse_removed++;
        changed = true;'''),
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),)
