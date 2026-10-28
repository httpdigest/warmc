"""opt.c pass_acc_tail: at -O3, a recursion whose result is its own call's
op something else, carried as a loop with an accumulator.  Killed by
test/cases_opt/acc_tail.wat (made by test/gen_acctail.py) -- its answers at
every level and how many functions -O3 takes -- test/run_levels.sh and the
traps at -O3 (not the cases, whose float rows fma changes there)."""
O, MAIN = 'src/opt.c', 'src/main.c'
def m(old, new, path=O): return (path, [(old, new)])
M = {
 # when
 'level': m('  o->fma = o->dead_frames = o->acc_tail = level >= 3;',
            '  o->fma = o->dead_frames = level >= 3;\n  o->acc_tail = level >= 2;', MAIN),
 'exact': m('  if (opt.exact_trap) opt.trampoline = opt.go_sp = opt.dead_frames = opt.acc_tail = false;',
            '  if (opt.exact_trap) opt.trampoline = opt.go_sp = opt.dead_frames = false;', MAIN),
 'flag': m('      else if (!strcmp(name, "acc-tail")) opt.acc_tail = on;',
           '      else if (!strcmp(name, "acc-tail")) opt.acc_tail = false;', MAIN),
 # which functions
 'pads': m('  if (f->npads || f->sig->nresults != 1) return false;\n  uint32_t size',
           '  if (f->sig->nresults != 1) return false;\n  uint32_t size'),
 'results': m('  if (f->npads || f->sig->nresults != 1) return false;\n  uint32_t size',
              '  if (f->npads || !f->sig->nresults) return false;\n  uint32_t size'),
 'cap_off': m('  if (size > ACC_TAIL_MAX) return false;\n', ''),
 'cap_low': m('#define ACC_TAIL_MAX 64', '#define ACC_TAIL_MAX 16'),
 'entry_params': m('  if (f->blocks[f->entry].nparams != np) return false;\n  for (uint32_t i = 0; i < f->ninsts; i++) {\n    uint16_t op',
                   '  for (uint32_t i = 0; i < f->ninsts; i++) {\n    uint16_t op'),
 'retcall': m('    if (op == I_RETURN_CALL || op == I_RETURN_CALL_INDIRECT || op == I_RETURN_CALL_REF)\n      return false;',
              '    if (op == I_RETURN_CALL_INDIRECT || op == I_RETURN_CALL_REF)\n      return false;'),
 # what is an op's identity
 'id_add': m('    case I_ADD: case I_OR: case I_XOR: *id = 0; return true;', '    case I_OR: case I_XOR: *id = 0; return true;'),
 'id_mul': m('    case I_MUL: *id = 1; return true;', '    case I_MUL: *id = 0; return true;'),
 'id_and': m('    case I_AND: *id = all; return true;', '    case I_AND: *id = 0xffffffffu; return true;'),
 'id_xor': m('    case I_ADD: case I_OR: case I_XOR: *id = 0; return true;', '    case I_ADD: case I_OR: *id = 0; return true;\n    case I_XOR: *id = 1; return true;'),
 'id_sub': m('    case I_MUL: *id = 1; return true;', '    case I_MUL: *id = 1; return true;\n    case I_SUB: *id = 0; return true;'),
 # where the result goes
 'res_insts': m('  if (r->ninsts || r->nparams != 1 || r->term != T_RETURN || r->nret != 1 ||',
                '  if (r->nparams != 1 || r->term != T_RETURN || r->nret != 1 ||'),
 'res_param': m('      r->ret_args[0] != r->params[0])\n    return REF_NONE;', '      0)\n    return REF_NONE;'),
 'res_jump': m('  if (b->term != T_JUMP || b->nsuccs != 1 || b->succs[0].nargs != 1) return REF_NONE;',
               '  return REF_NONE;'),
 'res_ret': m('  if (b->term == T_RETURN && b->nret == 1) return b->ret_args[0];', ''),
 # the site
 'same_op': m('    if (accop != I_NOP && vi->op != accop) continue;\n', ''),
 'self': m('      if (ci->op != I_CALL || ci->imm != f->func_index || ci->nargs != np ||',
           '      if (ci->op != I_CALL || ci->nargs != np ||'),
 'call_op': m('      if (ci->op != I_CALL || ci->imm != f->func_index || ci->nargs != np ||',
              '      if (ci->imm != f->func_index || ci->nargs != np ||'),
 'call_block': m('          ci->block != bi || uses[va[k]] != 1)', '          uses[va[k]] != 1)'),
 'call_uses': m('          ci->block != bi || uses[va[k]] != 1)', '          ci->block != bi)'),
 'pure': m('        ok = ir_is_pure(&f->insts[u]);', '        ok = ir_has_no_effect(&f->insts[u]);'),
 'pure_any': m('        ok = ir_is_pure(&f->insts[u]);', '        ok = true;'),
 'first_call': m('      if (ok) { c = va[k]; x = va[1 - k]; }', '      c = va[k]; x = va[1 - k];'),
 # the rewrite
 'x_param': m('      if (x == eparam[p]) x = hparam[p];', '      if (0) x = hparam[p];'),
 'ret_acc': m('    b->ret_args[0] = ir_emit(f, bi, (IrOp)accop, t, a2, 2, 0, 0);', '    (void)a2;'),
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('levels', ['./test/run_levels.sh'], {}),
          ('traps', ['./test/run_traps.sh'], {'WARMC_FLAGS': '-O3'}))
EQUIVALENT = {
 'pads':
  "a call a try reaches ends its block, which unwinds to the handler, so it is never in its op's block; the function's other handlers keep their blocks",
 'results':
  "a function of two or more results returns two values or more, and the sites take a return of one",
 'entry_params':
  "the builder gives the entry block the signature's parameters, and nothing before this pass adds or takes one",
 'res_param':
  "a block of one parameter that returns something else leaves the parameter unread, and prune_params, run first, drops it",
}
