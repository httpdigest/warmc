"""What the allocator makes of a call on a way the rules call rare
(regalloc.inc's rareb and rare_only, opt.c ir_rare_blocks).  Killed by
test/cases_opt/rare_calls.wat's shapes and count."""
R, O = 'src/regalloc.inc', 'src/opt.c'
def m(path, old, new): return (path, [(old, new)])
M = {
 'off': m(R, '  uint8_t *rareb = warmc_getenv("WARMC_NO_RARE_BLOCKS") ? NULL : ir_rare_blocks(f);',
          '  uint8_t *rareb = NULL;'),
 # which blocks
 'rule': m(O, '  if (hs.tests) {\n    rare = xcalloc(f->nblocks ? f->nblocks : 1, 1);',
           '  if (0) {\n    rare = xcalloc(f->nblocks ? f->nblocks : 1, 1);'),
 'not_hot': m(O, '    for (uint32_t b = 0; b < f->nblocks; b++) rare[b] = f->blocks[b].reachable && !hs.hot[b];',
              '    for (uint32_t b = 0; b < f->nblocks; b++) rare[b] = f->blocks[b].reachable && hs.hot[b];'),
 # a call there is a rare one, a call elsewhere one every run makes
 'call_cold': m(R, '        call_cold[ncalls] = b->cold || (rareb && rareb[bi]) ? 2',
                '        call_cold[ncalls] = b->cold ? 2'),
 'share': m(R, '        call_share[ncalls] = rareb ? 1.0f : bshare[bi];', '        call_share[ncalls] = bshare[bi];'),
 # a constant handed on from there
 'const_edge': m(R, '               (rareb && rareb[src])))', '               0))'),
 # a caller-saved register first for what crosses rare calls alone
 'rare_first': m(R, '  bool rare_csa = !warmc_getenv("WARMC_NO_RARE_CSA");', '  bool rare_csa = false;'),
 'rare_hot': m(R, '                       !csa_crosses_hot(cur, R, num[cur->value], call_pos, call_cold, ncalls);',
               '                       true;'),
 'rare_pass': m(R, '          bool want_cs = cur->crosses_call && !rare_only ? true : (pass == 1);',
                '          bool want_cs = cur->crosses_call ? true : (pass == 1);'),
 'rare_skip': m(R, '          if (cur->crosses_call && !rare_only && (v128 || !cs)) continue;',
                '          if (cur->crosses_call && (v128 || !cs)) continue;'),
}
EQUIVALENT = {}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}),
          ('gc', ['./test/run_gc.sh'], {}))
