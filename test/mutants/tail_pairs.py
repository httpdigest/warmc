"""Two functions that tail-call each other, one loop (inline.c
inline_tail_pairs; opt.c's call of it ahead of pass_self_tail).  Killed by
test/cases_opt/tail_pairs.wat's answers, its counts and its shapes.

Not mutated: WARMC_NO_TAIL_PAIRS, which no case sets; the test that the
callee is no import, whose body there is none to build of."""
N, O = 'src/inline.c', 'src/opt.c'
def m(old, new, path=N): return (path, [(old, new)])
CALL = '  if (o->selftail && o->inl && inline_tail_pairs(f, o)) ir_verify(f, "post-tail-pairs");'
SKIP = '    if (!back || g->npads || n > TAIL_PAIR_MAX) {'
BACK = '        if (in->op == I_RETURN_CALL && (uint32_t)in->imm == f->func_index) back = true;'
M = {
 'off': m(CALL, '  if (0 && o->selftail && o->inl && inline_tail_pairs(f, o)) ir_verify(f, "post-tail-pairs");', O),
 'no_inline': m(CALL, '  if (o->selftail && inline_tail_pairs(f, o)) ir_verify(f, "post-tail-pairs");', O),
 'no_selftail': m(CALL, '  if (o->inl && inline_tail_pairs(f, o)) ir_verify(f, "post-tail-pairs");', O),
 # which callees
 'any_callee': m(SKIP, '    if (g->npads || n > TAIL_PAIR_MAX) {'),
 'callee_pads': m(SKIP, '    if (!back || n > TAIL_PAIR_MAX) {'),
 'any_size': m(SKIP, '    if (!back || g->npads) {'),
 'max_less': m('#define TAIL_PAIR_MAX 96u', '#define TAIL_PAIR_MAX 8u'),
 'back_any': m(BACK, '        if (in->op == I_RETURN_CALL) back = true;'),
 'back_call': m(BACK, '        if ((in->op == I_RETURN_CALL || in->op == I_CALL) && (uint32_t)in->imm == f->func_index) back = true;'),
 'itself': m('    if (gi == f->func_index || m->funcs[gi].imported) continue;', '    if (m->funcs[gi].imported) continue;'),
 'caller_pads': m('  if (f->npads || warmc_getenv("WARMC_NO_TAIL_PAIRS")) return false;', '  if (warmc_getenv("WARMC_NO_TAIL_PAIRS")) return false;'),
 'not_last': m('    if (f->insts[call].op != I_RETURN_CALL) continue;\n    uint32_t gi', '    if (f->insts[call].op != I_RETURN_CALL && f->insts[call].op != I_CALL) continue;\n    uint32_t gi'),
 # the splice
 'call_stays': m('    f->blocks[bi].ninsts--;\n    ir_make_nop(&f->insts[call]);\n', ''),
 'no_results': m('    cb->nret = cb->cap_ret = sig->nresults;', '    cb->nret = cb->cap_ret = 0;'),
 'results_swapped': m('    for (uint32_t i = 0; i < sig->nresults; i++) ret[i] = ir_add_param(f, cont, sig->results[i]);\n    Block *cb',
                      '    for (uint32_t i = 0; i < sig->nresults; i++) ret[sig->nresults - 1 - i] = ir_add_param(f, cont, sig->results[i]);\n    Block *cb'),
 'count': m('    g_stats.tail_pairs++;\n', ''),
}
EQUIVALENT = {}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'] + ['tail_pairs'], {}),)
