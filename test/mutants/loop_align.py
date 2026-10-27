"""isel.c small_loop: a small loop aligned to 64 bytes, but not one that
calls.  Killed by test/cases_opt/call_arm.wat's small_loops count."""
O, I, IS, IR, IX = 'src/opt.c', 'src/isel_ops.inc', 'src/isel.c', 'src/ir.c', 'src/isel_x64_ops.inc'
M = {
 'small_head': (IS, [('  bool ok = !block_calls(f, hb);', '  bool ok = true;')]),
 'small_body': (IS, [('    if (n > SMALL_LOOP_INSTS || x == f->entry || block_calls(f, xb)) ok = false;', '    if (n > SMALL_LOOP_INSTS || x == f->entry) ok = false;')]),
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}),
          ('traps', ['./test/run_traps.sh'], {}),
          ('gobench', ['python3', 'test/gobench.py', '--corpus', 'gobench', '--check'], {}),
          ('rsbench', ['python3', 'test/gobench.py', '--corpus', 'rsbench', '--check'], {}),
          ('cbench', ['python3', 'test/gobench.py', '--corpus', 'cbench', '--check'], {}))
