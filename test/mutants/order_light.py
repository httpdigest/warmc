"""The ordering between jump threadings (ir.c ir_order_light): what it finds
of the graph is what ir_order finds, and a graph that is not reducible is
ordered whole.  Killed by the check WARMC_VERIFY_GATES makes of each light
ordering against ir_order, over the cases and the Go-shaped modules, whose
graphs are not reducible, and by test/run_same.py.  Not mutated: the search
it takes (either gives the facts), the signature left invalid (a graph that
jump threading changed has another signature anyway), and the whole
ordering pass_thread makes before it prunes parameters, which is there so
that the pruning walks the blocks in ir_order's order as it did."""
I = 'src/ir.c'
def m(old, new, path=I): return (path, [(old, new)])
M = {
 # a graph with an edge back that is not natural is ordered as ir_order orders it
 'irreducible': m('  if (!reducible) {\n    mark_diverging(f);\n    order_once(f, true, false, false);\n  }\n  f->order_sig_valid = false;',
                  '  f->order_sig_valid = false;'),
 'natural': m('        if (f->blocks[t].rpo <= b->rpo && !block_dominates(f, t, bi)) all = false;', '        ;'),
}
EQUIVALENT = {}
SUITES = (('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2', 'WARMC_VERIFY_GATES': '1'}),
          ('gosp', ['python3', 'test/run_gosp.py'], {'WARMC_VERIFY_GATES': '1'}),
          ('same', ['python3', 'test/run_same.py'], {}),
          ('optcases', ['python3', 'test/run_optcases.py'], {'WARMC_VERIFY_GATES': '1'}))
