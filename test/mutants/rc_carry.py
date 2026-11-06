"""The 2026-11-06 rule of the reload cache: it goes on into a block whose
only way in is the end of the block emitted just before it, a value stored
with a read there to come put in it (isel_core.inc rc_carries and
used_soon, isel_ops.inc emit_block).  Killed by test/cases_opt/rc_carry.wat
(carry's shape, merge's and apart's answers) and the answers of the cases
and the collector's tests."""
O = 'src/isel_ops.inc'
C = 'src/isel_core.inc'
def m(path, old, new): return (path, [(old, new)])
M = {
 'rc_off': m(C, '  return bb->npreds == 1 && bb->preds[0] == p && !warmc_getenv("WARMC_NO_RC_CARRY");',
             '  return false;'),
 'rc_npreds': m(C, '  return bb->npreds == 1 && bb->preds[0] == p && !warmc_getenv("WARMC_NO_RC_CARRY");',
                '  return bb->preds[0] == p && !warmc_getenv("WARMC_NO_RC_CARRY");'),
 'rc_pred': m(C, '  return bb->npreds == 1 && bb->preds[0] == p && !warmc_getenv("WARMC_NO_RC_CARRY");',
              '  return bb->npreds == 1 && !warmc_getenv("WARMC_NO_RC_CARRY");'),
 'rc_block': m(O, '  if (!rc_carries(s, bi, s->prev_bi)) rc_clear(s);', '  rc_clear(s);'),
 'us_off': m(C, '  if (nb >= f->nblocks || !rc_carries(s, nb, s->cur_block)) return false;',
             '  return false;'),
 'us_next': m(O, '  s->cur_next = next_bi;', '  s->cur_next = bi;'),
 'us_call': m(C, '''    for (uint32_t j = 0; j < in->nargs; j++) if (a[j] == v) return true;
    if (inst_clobbers_caller_saved(f, u)) return false;
  }
  return false;
}''', '''    for (uint32_t j = 0; j < in->nargs; j++) if (a[j] == v) return true;
  }
  return false;
}'''),
}
EQUIVALENT = {
 'us_call': "past a call the cache is gone (emit_inst's rc_clear): the value put in it is only a move too many",
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('gc', ['./test/run_gc.sh'], {}),
          ('cases', ['./test/run_tests.sh'], {}))
