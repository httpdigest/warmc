"""regalloc.inc web_interferes and live_after: the coalescer's exact test,
by liveness, of a block parameter's web and its argument's where their
intervals overlap.  Killed by test/cases_opt/coalesce_exact.wat."""
R = 'src/regalloc.inc'
def m(old, new): return (R, [(old, new)])
M = {
 'off': m('                           web_interferes(&la, iv, mem_head[ra], mem_head[rp], mem_next)))',
          '                           1))'),
 'all': m('                           web_interferes(&la, iv, mem_head[ra], mem_head[rp], mem_next)))',
          '                           0))'),
 'one_way': m('''      if (live_after(la, x, f->insts[y].block, la->num[y]) ||
          live_after(la, y, f->insts[x].block, la->num[x]))''',
              '''      if (live_after(la, x, f->insts[y].block, la->num[y]))'''),
 'other_way': m('''      if (live_after(la, x, f->insts[y].block, la->num[y]) ||
          live_after(la, y, f->insts[x].block, la->num[x]))''',
                '''      if (live_after(la, y, f->insts[x].block, la->num[x]))'''),
 'samepos': m('    if (la->num[x] >= p) return la->num[x] == p;', '    if (la->num[x] >= p) return false;'),
 'later_def': m('    if (la->num[x] >= p) return la->num[x] == p;', '    if (la->num[x] == p) return true;'),
 'livein': m('''  } else if (!live_in_has(s, bi, x)) {
    return false;
  }''', '  }'),
 'liveout': m('  if (live_out_has(s, bi, x)) return true;\n  Block *b', '  Block *b'),
 'uses': m('      if (la->eu[j] == x) return true;\n  }', '      if (0) return true;\n  }'),
 'before': m('    if (la->num[r] <= p) continue;', '    if (la->num[r] < p) continue;'),
 'fwd': m('          if (by_exact && (!exact || sb->rpo < b->rpo ||', '          if (by_exact && (!exact ||'),
 'length': m('  if (4 * (int64_t)(ea - sa) > (int64_t)(eb - sb)) return true;\n', ''),
 'length4': m('  if (4 * (int64_t)(ea - sa) > (int64_t)(eb - sb)) return true;',
              '  if (2 * (int64_t)(ea - sa) > (int64_t)(eb - sb)) return true;'),
 'pairs': m('#define LA_PAIRS 16', '#define LA_PAIRS 0'),
}
EQUIVALENT = {
 'livein':
  "a value neither made in a block nor live into it is read nowhere in it, so the scan finds nothing: the test only saves it",
 'later_def':
  "of two values made in one block whose intervals overlap, the earlier is live past the later's definition, so the later's answer never decides",
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}))
