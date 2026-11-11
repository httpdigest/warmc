"""A branch's compare copied to the branch, and a decided test handed on as
its 0 or 1 (opt.c pass_cmp_sink); a constant on a rare way's edge made on
that edge (regalloc.inc).  Killed by test/cases_opt/cmp_sink.wat's answers,
shapes and counts."""
O, A = 'src/opt.c', 'src/regalloc.inc'
def m(old, new, path=O): return (path, [(old, new)])
M = {
 'off': m('  if (o->cmpsink && TP("cmp_sink", pass_cmp_sink(f)) && o->dce) {', '  if (0 && o->cmpsink && TP("cmp_sink", pass_cmp_sink(f)) && o->dce) {'),
 # a test handed on along an edge
 'own': m('        if (own && s < 2 && a == f->blocks[u].term_arg && (s == 1 || cs_is_bit(f, a)))',
          '        if (0 && own && s < 2 && a == f->blocks[u].term_arg && (s == 1 || cs_is_bit(f, a)))'),
 'own_true': m('        if (own && s < 2 && a == f->blocks[u].term_arg && (s == 1 || cs_is_bit(f, a)))',
               '        if (own && s < 2 && a == f->blocks[u].term_arg)'),
 'own_way': m('          val = s == 0;', '          val = s != 0;'),
 'known': m('          (!truth || cs_is_bit(f, c))) {', '          (!truth || cs_is_bit(f, c)) && 0) {'),
 'known_true': m('          (!truth || cs_is_bit(f, c))) {', '          1) {'),
 'known_way': m('        fc[nf] = c; ft[nf++] = truth;', '        fc[nf] = c; ft[nf++] = !truth;'),
 # the copies
 'copy': m('    if (!cs_cheap(f, c, u, 0)) continue;', '    continue;'),
 'tree': m('  if ((src.op == I_EQZ || src.op == I_AND || src.op == I_OR) && depth < 2)', '  if (0)'),
 'slot_depth': m('    return f->blocks[in->block].loop_depth >= f->blocks[u].loop_depth;', '    return true;'),
 # the allocator's rule
 'rare_const': m('          if (rare_const && f->insts[arg].op == I_CONST && sb->rpo < b->rpo &&',
                 '          if (0 && rare_const && f->insts[arg].op == I_CONST && sb->rpo < b->rpo &&', A),
}
EQUIVALENT = {}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}))
