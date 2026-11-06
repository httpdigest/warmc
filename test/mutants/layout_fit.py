"""isel.c produce_body's placing of loops by their hot way round
(loop_hot_span, hot_pad, ends_dead), its leaving out of jumps to the next
instruction, and the counters of a run made again whole, which count
nothing (stats_in).  Killed by test/cases_opt/layout_fit.wat's hot_fits,
dead_pads and next_jumps counts, its shape and its answers, call_arm.wat's
hot_fits, exit_defer.wat's and bfi_carry.wat's counts, and loop_phase.wat's
shapes."""
IS, IC = 'src/isel.c', 'src/isel_core.inc'
M = {
 # placing by the hot way round
 'hot_none': (IS, [('      bool hot = header && !small && hot_fit && !loop_calls(f, lh);',
                    '      bool hot = false;')]),
 'hot_calls': (IS, [('      bool hot = header && !small && hot_fit && !loop_calls(f, lh);',
                     '      bool hot = header && !small && hot_fit;')]),
 'calls_any': (IS, [('    calls = block_calls(f, &f->blocks[x]);', '    calls = false;')]),
 'calls_ahead': (IS, [('      if (in[q] || (x == h && f->blocks[q].rpo < hrpo)) continue;',
                       '      if (in[q]) continue;')]),
 'row_depth': (IS, [('    if (bb->loop_depth != d) return 0;', '    (void)d;')]),
 'row_top': (IS, [('      if (bb->succs[si].target == top) return blk_end[b] - s->block_off[top];',
                   '      if (bb->succs[si].target == h) return blk_end[b] - s->block_off[top];')]),
 'pad_any': (IS, [('  return p <= 16 || dead ? p : (align - (at & (align - 1))) & (align - 1);',
                   '  return p;')]),
 'pad_nodead': (IS, [('  return p <= 16 || dead ? p : (align - (at & (align - 1))) & (align - 1);',
                      '  return p <= 16 ? p : (align - (at & (align - 1))) & (align - 1);')]),
 'pad_16': (IS, [('  return p <= 16 || dead ? p : (align - (at & (align - 1))) & (align - 1);',
                  '  return p <= 12 || dead ? p : (align - (at & (align - 1))) & (align - 1);')]),
 'dead_never': (IS, [('  return (w & 0xfc000000u) == 0x14000000u;', '  return false;')]),
 'dead_branch': (IS, [('        if (pad > 16 && !dead) {', '        if (pad > 16) {')]),
 'hot_span': (IS, [('      uint32_t span = (rhot[k] & 1) ? loop_hot_span(s, lh, eo, neo, rec[4 * k + 1], blk_end)',
                    '      uint32_t span = 0 ? loop_hot_span(s, lh, eo, neo, rec[4 * k + 1], blk_end)')]),
 'refit_hot': (IS, [('      uint32_t np = (rhot[k] & 1) ? hot_pad(a, spans[h], lalign, rhot[k] & 2) : fit_pad(a, spans[h], lalign);',
                     '      uint32_t np = fit_pad(a, spans[h], lalign);')]),
 # jumps to the next instruction
 'next_none': (IS, [('    if (!keep_next && lo_k > 0 && B->len >= 4)', '    if (0)')]),
 'next_target': (IS, [('          if ((*pp)->is19 == 0 && (*pp)->target == bi) jp = pp;',
                       '          if ((*pp)->is19 == 0) jp = pp;')]),
 'next_kind': (IS, [('          if ((*pp)->is19 == 0 && (*pp)->target == bi) jp = pp;',
                     '          if ((*pp)->target == bi) jp = pp;')]),
 'next_empties': (IS, [('    while (lo_k > 0 && s->block_off[eo[lo_k - 1]] == B->len) lo_k--;', '')]),
 'next_moved': (IS, [('      for (uint32_t k = oi; k-- > 0 && s->block_off[eo[k]] == old;) s->block_off[eo[k]] = (uint32_t)B->len;', '')]),
 # a run made again whole counts nothing
 'stats_refit': (IS, [('    split_ret = 4;\n    g_stats = stats_in;', '    split_ret = 4;')]),
}
EQUIVALENT = {
 'refit_hot': 'the run again places a loop by hot_pad either way: one asked for where '
              'hot_pad would keep the plain alignment makes the same code',
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}),
          ('gobench', ['python3', 'test/gobench.py', '--corpus', 'gobench', '--check'], {}))
