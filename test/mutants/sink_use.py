"""opt.c pass_sink: a cheap value made in the block above all its readers,
where its operand is read too.  Killed by test/cases_opt/sink_use.wat's
answers, its count and its shape, or by the verifier, where a mutant leaves
a reader of what was taken away.

Not mutated: WARMC_NO_SINK, which no case sets; the test for a handler's
edge that hands the value on, which no module the compiler makes has; the
bound on the operand (x >= n0), which keeps the index in its array."""
O = 'src/opt.c'
def m(old, new): return (O, [(old, new)])
OPS = ('        case I_ADD: case I_SUB: case I_AND: case I_OR: case I_XOR:\n'
       '        case I_SHL: case I_SHRU: case I_SHRS:\n          break;\n        default:\n          continue;\n      }\n'
       '      /* (an operand this pass made')
OPERANDS = '      if (x >= n0 || !is_const(f, ir_args(f, v)[1], &c) || is_const(f, x, &c)) continue;'
SCAN = ('          if (ir_args(f, r)[a] == v) at = k;\n'
        '          else if (ir_args(f, r)[a] == x) there = true;')
M = {
 'off': m('  if (warmc_getenv("WARMC_NO_SINK")) return false;\n  bool changed = false;\n  uint32_t n0 = f->ninsts;\n  UseIdx ui = use_index(f);\n  LsEsc xi = ls_esc_index(f);\n  for (uint32_t oi = 0; oi < f->norder; oi++) {\n    uint32_t bi = f->order[oi];\n    for (uint32_t i = 0; i < f->blocks[bi].ninsts; i++) {\n      Ref v = f->blocks[bi].insts[i];\n      if (v >= n0) continue;',
          '  return false;\n  bool changed = false;\n  uint32_t n0 = f->ninsts;\n  UseIdx ui = use_index(f);\n  LsEsc xi = ls_esc_index(f);\n  for (uint32_t oi = 0; oi < f->norder; oi++) {\n    uint32_t bi = f->order[oi];\n    for (uint32_t i = 0; i < f->blocks[bi].ninsts; i++) {\n      Ref v = f->blocks[bi].insts[i];\n      if (v >= n0) continue;'),
 # which values
 'any_op': m(OPS, OPS.replace('        default:\n          continue;\n', '        default:\n          if (in->nargs != 2) continue;\n')),
 'no_sub': m(OPS, OPS.replace('case I_ADD: case I_SUB: case I_AND:', 'case I_ADD: case I_AND:')),
 'no_or': m(OPS, OPS.replace('case I_AND: case I_OR: case I_XOR:', 'case I_AND: case I_XOR:')),
 'no_shrs': m(OPS, OPS.replace('case I_SHL: case I_SHRU: case I_SHRS:', 'case I_SHL: case I_SHRU:')),
 'two_values': m(OPERANDS, '      if (x >= n0 || is_const(f, x, &c)) continue;'),
 # where
 'above_first': m('    uint32_t d = f->blocks[*lower].idom;\n    if (d == REF_NONE || d == *lower) return f->entry;\n    *lower = d;\n  }\n  return a;',
                  '    uint32_t d = f->blocks[*lower].idom;\n    if (d == REF_NONE || d == *lower) return f->entry;\n    *lower = d;\n    break;\n  }\n  return a;'),
 'above_lower': m('    uint32_t *lower = f->blocks[a].rpo > f->blocks[b].rpo ? &a : &b;',
                  '    uint32_t *lower = f->blocks[a].rpo > f->blocks[b].rpo ? &b : &a;'),
 'own_block': m('      if (ub == UINT32_MAX || ub == bi || eh) continue;', '      if (ub == UINT32_MAX || eh) continue;'),
 'into_loop': m('      if (f->blocks[ub].loop_depth > f->blocks[bi].loop_depth) continue;\n', ''),
 'insts_only': m('        ub = ub == UINT32_MAX ? xi.blk[u] : sink_above(f, ub, xi.blk[u]);\n', ''),
 'edges_only': m('        ub = ub == UINT32_MAX ? rb : sink_above(f, ub, rb);\n', '        (void)rb;\n'),
 # the operand
 'operand_anywhere': m('      if (!there) continue;\n      /* (read by the block', '      /* (read by the block'),
 'operand_by_inst': m('        there = xi.blk[u] == ub;\n', '        there = false;\n'),
 'operand_by_edge': m(SCAN, '          if (ir_args(f, r)[a] == v) at = k;'),
 'no_parameter': m('          if (pe->args[a] == x) { x = U->params[a]; break; }', '          if (0) { x = U->params[a]; break; }'),
 'parameter_many_ways': m('      if (U->npreds == 1) {\n        const Edge *pe', '      if (U->npreds >= 1) {\n        const Edge *pe'),
 # made, and read
 'at_end': m(SCAN, '          if (ir_args(f, r)[a] == x) there = true;'),
 'at_last_reader': m('      for (uint32_t k = U->ninsts; k-- > 0;) {\n        Ref r = U->insts[k];\n        for (uint32_t a = 0; a < f->insts[r].nargs; a++) {\n          if (ir_args(f, r)[a] == v) at = k;',
                     '      for (uint32_t k = 0; k < U->ninsts; k++) {\n        Ref r = U->insts[k];\n        for (uint32_t a = 0; a < f->insts[r].nargs; a++) {\n          if (ir_args(f, r)[a] == v) at = k;'),
 'insts_kept': m('          if (ir_args(f, r)[a] == v) ir_args(f, r)[a] = w;', '          {}'),
 'branch_kept': m('        if ((b->term == T_BRIF || b->term == T_BRTABLE) && b->term_arg == v) b->term_arg = w;\n', ''),
 'return_kept': m('          if (b->ret_args[k] == v) b->ret_args[k] = w;', '          {}'),
 'edge_kept': m('            if (b->succs[e].args[a] == v) b->succs[e].args[a] = w;', '            {}'),
 'after_handler_call': m('      if (at == U->ninsts && ir_block_has_eh(f, ub)) continue;\n', ''),
 'still_made': m('      ir_make_nop(&f->insts[v]);\n      g_stats.sunk++;', '      g_stats.sunk++;'),
 'count': m('      ir_make_nop(&f->insts[v]);\n      g_stats.sunk++;', '      ir_make_nop(&f->insts[v]);'),
}
EQUIVALENT = {
 'still_made': 'what nothing reads is not selected, and dead-code elimination takes it away after the pass: the mark is there so that a reader left behind fails verification',
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py', 'sink_use'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}))
