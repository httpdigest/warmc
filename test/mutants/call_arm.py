"""A branch's arm that only calls, emitted after the rest of the function,
out of the hot path.  Killed by test/cases_opt/call_arm.wat's `;; late:`
shapes.

Not mutated: WARMC_CALL_ARM_ALL, which no case sets."""
O, I, IS, IR, IX = 'src/opt.c', 'src/isel_ops.inc', 'src/isel.c', 'src/ir.c', 'src/isel_x64_ops.inc'
M = {
 'arm_loop': (IR, [('  if (!b->loop_depth || bi == f->entry || b->npreds != 1) return false;\n  const Block *p', '  if (bi == f->entry || b->npreds != 1) return false;\n  const Block *p')]),
 'arm_npreds': (IR, [('  if (!b->loop_depth || bi == f->entry || b->npreds != 1) return false;\n  const Block *p', '  if (!b->loop_depth || bi == f->entry || !b->npreds) return false;\n  const Block *p')]),
 'arm_other': (IR, [('    if (t == other) return calls;', '    if (t == other || 1) return calls;')]),
 'arm_calls': (IR, [('    if (t == other) return calls;', '    if (t == other) return true;')]),
 'arm_chain': (IR, [('  for (int steps = 0; steps < 8; steps++) {\n    for (uint32_t i = 0; i < b->ninsts && !calls; i++) {', '  for (int steps = 0; steps < 1; steps++) {\n    for (uint32_t i = 0; i < b->ninsts && !calls; i++) {')]),
 'arm_chain_npreds': (IR, [('    b = &f->blocks[t];\n    if (b->npreds != 1) return false;', '    b = &f->blocks[t];')]),
 'arm_self': (IR, [('      if (op == I_CALL && in->imm == f->func_index && !all) return false;\n', '')]),
 'arm_nonzero': (IR, [('    if (c < f->ninsts && !zero) {\n      uint16_t op = f->insts[c].op;', '    if (0) {\n      uint16_t op = f->insts[c].op;')]),
 'arm_zero_too': (IR, [('    if (c < f->ninsts && !zero) {\n      uint16_t op = f->insts[c].op;', '    if (c < f->ninsts) {\n      uint16_t op = f->insts[c].op;')]),
 'arm_tests_too': (IR, [('      if (op != I_EQ && op != I_NE && !(op >= I_LTS && op <= I_GEU)) return false;', '      (void)op; return false;')]),
 'arm_eq_too': (IR, [('      if (op != I_EQ && op != I_NE && !(op >= I_LTS && op <= I_GEU)) return false;', '      if (op != I_NE && !(op >= I_LTS && op <= I_GEU)) return false;')]),
 'arm_less_too': (IR, [('      if (op != I_EQ && op != I_NE && !(op >= I_LTS && op <= I_GEU)) return false;', '      if (op != I_EQ && op != I_NE) return false;')]),
 'arm_no_flip': (IR, [('{ c = f->argpool[f->insts[c].args_off]; zero = !zero; }\n    if (c < f->ninsts && !zero) {', '{ c = f->argpool[f->insts[c].args_off]; }\n    if (c < f->ninsts && !zero) {')]),
 'arm_either_succ': (IR, [('    bool zero = p->succs[0].target != bi;', '    bool zero = false;')]),
 'arm_hook': (IR, [('    uint32_t flagged = b->outline || (cold_late && b->cold) || call_arm(f, bi);', '    uint32_t flagged = b->outline || (cold_late && b->cold);')]),
}
EQUIVALENT = {
 'arm_chain': "a block that only jumps to one nothing else reaches is one block with it by then "
              "(opt.c merge_blocks, in every pass_branch), so an arm is one block and the walk's "
              "second step runs only with block merging off, which no case's code is checked under",
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py', 'call_arm'], {}),
          ('optcases-all', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}),
          ('traps', ['./test/run_traps.sh'], {}),
          ('gobench', ['python3', 'test/gobench.py', '--corpus', 'gobench', '--check'], {}),
          ('rsbench', ['python3', 'test/gobench.py', '--corpus', 'rsbench', '--check'], {}),
          ('cbench', ['python3', 'test/gobench.py', '--corpus', 'cbench', '--check'], {}))
