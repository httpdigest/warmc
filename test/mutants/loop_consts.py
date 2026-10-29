"""codegen.c loop_consts: on arm64, a constant of two or more instructions a
natural loop reads, read from a copy made in the header's immediate
dominator.  Killed by test/cases_opt/loop_consts.wat -- its answers and how
many copies -O2 makes."""
C = 'src/codegen.c'
def m(old, new, path=C): return (path, [(old, new)])
M = {
 'off': m('  if (g_target != &warmc_target_a64 || warmc_getenv("WARMC_NO_LOOP_CONSTS")) return;', '  return;'),
 'natural': m('    if (!loop || !natural) continue;', '    if (!loop) continue;\n    (void)natural;'),
 'cost': m('         ir_const_cost(ci->imm, ci->type == VT_I64) >= 2;', '         ir_const_cost(ci->imm, ci->type == VT_I64) >= 1;'),
 'cost3': m('         ir_const_cost(ci->imm, ci->type == VT_I64) >= 2;', '         ir_const_cost(ci->imm, ci->type == VT_I64) >= 3;'),
 'i64_off': m('  return ci->op == I_CONST && (ci->type == VT_I32 || ci->type == VT_I64) &&',
              '  return ci->op == I_CONST && ci->type == VT_I32 &&'),
 'floats': m('  return ci->op == I_CONST && (ci->type == VT_I32 || ci->type == VT_I64) &&',
             '  return ci->op == I_CONST && ci->type != VT_V128 &&'),
 'outermost': m('      if (inner[x] == UINT32_MAX || f->blocks[inner[x]].loop_depth < hb->loop_depth) inner[x] = h;',
                '      if (inner[x] == UINT32_MAX) inner[x] = h;'),
 'self_loop': m('      if (seen[l] != h + 1) { seen[l] = h + 1; stk[sp++] = l; }',
                '      if (l != h && seen[l] != h + 1) { seen[l] = h + 1; stk[sp++] = l; }\n      if (l == h) loop = false;'),
 'edges_off': m('''    for (uint32_t e = 0; e < f->blocks[x].nsuccs; e++)
      for (uint32_t j = 0; j < f->blocks[x].succs[e].nargs; j++) {''', '''    for (uint32_t e = 0; e < 0; e++)
      for (uint32_t j = 0; j < f->blocks[x].succs[e].nargs; j++) {'''),
 'one_copy': m('  uint64_t k = ((uint64_t)h << 32 | c) + 1;', '  uint64_t k = ((uint64_t)0 << 32 | c) + 1;'),
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),)
