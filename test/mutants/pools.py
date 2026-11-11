"""The pools of constant expressions and of root lists (module_data.c
emit_const_expr, root_run_intern, ro_hash_tails).  Killed by
test/run_pools.py: the words an expression made twice shares, the root
entries that recording the tails shares, and the answers.  Not mutated:
which bytes a tail's hash takes, so long as a run's and a tail's of the
same bytes agree -- any that do is a hash."""
D = 'src/module_data.c'
def m(old, new, path=D): return (path, [(old, new)])
M = {
 # an expression that is there
 'const_look': m('  uint64_t h = ro_hash(p, n);\n  if (g_cp_ix_cap) {', '  uint64_t h = ro_hash(p, n);\n  if (0) {'),
 'const_record': m('  cp_record(first, (uint32_t)n, h);', '  ;'),
 'const_len': m('  cp_record(first, (uint32_t)n, h);', '  cp_record(first, (uint32_t)n - CONST_UNIT, h);'),
 'const_hash': m('  cp_record(first, (uint32_t)n, h);', '  cp_record(first, (uint32_t)n, h + 1);'),
 # a list's tails
 'tails_one': m('  for (uint32_t i = k; i-- > 0;) {\n    while (j > at[i]) { x ^= p[--j]; x *= 1099511628211ull; }',
                '  for (uint32_t i = 0; i < k; i++) {\n    while (j > at[i]) { x ^= p[--j]; x *= 1099511628211ull; }'),
 'tails_place': m('    h[i] = x;\n  }\n}', '    h[i] = x + i;\n  }\n}'),
 'run_hash': m('  uint64_t h = th[0];', '  uint64_t h = th[n - 1];'),
 'run_tail_hash': m('    if (!rr_record(pool, first + off[s], n - s, len - off[s], th[s]) && g_pool_tails < 2) break;',
                    '    if (!rr_record(pool, first + off[s], n - s, len - off[s], th[0]) && g_pool_tails < 2) break;'),
 'run_tail_len': m('    if (!rr_record(pool, first + off[s], n - s, len - off[s], th[s]) && g_pool_tails < 2) break;',
                   '    if (!rr_record(pool, first + off[s], n - s, len, th[s]) && g_pool_tails < 2) break;'),
 'run_tail_n': m('    if (!rr_record(pool, first + off[s], n - s, len - off[s], th[s]) && g_pool_tails < 2) break;',
                 '    if (!rr_record(pool, first + off[s], n, len - off[s], th[s]) && g_pool_tails < 2) break;'),
 'stop_new': m('    if (!rr_record(pool, first + off[s], n - s, len - off[s], th[s]) && g_pool_tails < 2) break;',
               '    if (rr_record(pool, first + off[s], n - s, len - off[s], th[s]) && g_pool_tails < 2) break;'),
 'stop_never': m('    if (!rr_record(pool, first + off[s], n - s, len - off[s], th[s]) && g_pool_tails < 2) break;',
                 '    rr_record(pool, first + off[s], n - s, len - off[s], th[s]);'),
 'tails_off': m('  uint32_t upto = g_pool_tails < 0 ? 1 : n;', '  uint32_t upto = 1;'),
 'tails_knob': m('  uint32_t upto = g_pool_tails < 0 ? 1 : n;', '  uint32_t upto = n;'),
}
EQUIVALENT = {
 'stop_never': "every tail looked up, as WARMC_POOL_ALL_TAILS does: the same object, which the test's tails check asks of that switch",
}
SUITES = (('pools', ['python3', 'test/run_pools.py'], {}),
          ('gc', ['./test/run_gc.sh'], {}))
