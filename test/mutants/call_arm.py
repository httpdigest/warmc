"""A branch's arm that only calls, emitted after the rest of the function,
out of the hot path.  Killed by test/cases_opt/call_arm.wat's `;; late:`
shapes."""
O, I, IS, IR, IX = 'src/opt.c', 'src/isel_ops.inc', 'src/isel.c', 'src/ir.c', 'src/isel_x64_ops.inc'
M = {
 'arm_loop': (IR, [('  if (!b->loop_depth || bi == f->entry || b->npreds != 1) return false;\n  const Block *p', '  if (bi == f->entry || b->npreds != 1) return false;\n  const Block *p')]),
 'arm_npreds': (IR, [('  if (!b->loop_depth || bi == f->entry || b->npreds != 1) return false;\n  const Block *p', '  if (!b->loop_depth || bi == f->entry || !b->npreds) return false;\n  const Block *p')]),
 'arm_other': (IR, [('    if (t == other) return calls;', '    if (t == other || 1) return calls;')]),
 'arm_calls': (IR, [('    if (t == other) return calls;', '    if (t == other) return true;')]),
 'arm_chain': (IR, [('  for (int steps = 0; steps < 8; steps++) {\n    for (uint32_t i = 0; i < b->ninsts && !calls; i++) {', '  for (int steps = 0; steps < 1; steps++) {\n    for (uint32_t i = 0; i < b->ninsts && !calls; i++) {')]),
 'arm_chain_npreds': (IR, [('    b = &f->blocks[t];\n    if (b->npreds != 1) return false;', '    b = &f->blocks[t];')]),
 'arm_hook': (IR, [('    bool out = b->outline || call_arm(f, bi);', '    bool out = b->outline;')]),
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}),
          ('traps', ['./test/run_traps.sh'], {}),
          ('gobench', ['python3', 'test/gobench.py', '--corpus', 'gobench', '--check'], {}),
          ('rsbench', ['python3', 'test/gobench.py', '--corpus', 'rsbench', '--check'], {}),
          ('cbench', ['python3', 'test/gobench.py', '--corpus', 'cbench', '--check'], {}))
