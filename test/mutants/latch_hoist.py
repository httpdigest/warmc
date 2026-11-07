"""isel_ops.inc T_BRIF's latch hoist: a latch's way round's moves made ahead
of its branch, where the way out moves none (latch_hoists) or moves values
of its own (latch_both).  Killed by test/cases_opt/latch_hoist.wat's counts,
shapes and answers."""
IO = 'src/isel_ops.inc'
M = {
 # which latches
 'one_none': (IO, [('      if (((tn != 0) != (en != 0) || both) && s->lv_in',
                    '      if ((both) && s->lv_in')]),
 'both_none': (IO, [('      bool both = tn && en && !warmc_getenv("WARMC_NO_LATCH_BOTH");',
                     '      bool both = false;')]),
 'both_side': (IO, [('        bool mt = both ? f->blocks[t->target].rpo <= b->rpo : tn != 0;',
                     '        bool mt = tn != 0;')]),
 'score': (IO, [('        bool ok = ir_succ_score(f, b, mi) >= ir_succ_score(f, b, oi) &&\n',
                 '        bool ok = ((void)mi, (void)oi, 1) &&\n')]),
 'back_edge': (IO, [('                  f->blocks[me->target].rpo <= b->rpo;\n        for (int i = 0; i < mn && ok; i++) {',
                     '                  true;\n        for (int i = 0; i < mn && ok; i++) {')]),
 # what the way round moves
 'remat': (IO, [('          if (mm[i].remat || IS_SLOT(mm[i].dst)) ok = false;',
                 '          if (IS_SLOT(mm[i].dst)) ok = false;')]),
 'slot_dst': (IO, [('          if (mm[i].remat || IS_SLOT(mm[i].dst)) ok = false;',
                    '          if (mm[i].remat) ok = false;')]),
 'branch_reg': (IO, [('          else if (kind != 0 && !mm[i].fp && mm[i].dst == zreg) ok = false;\n        }\n        for (int j = 0; j < on && ok; j++) {',
                      '        }\n        for (int j = 0; j < on && ok; j++) {')]),
 # what the way out moves
 'out_reads': (IO, [('            if (same_reg(om[j].src, om[j].fp, mm[i].dst, mm[i].fp)) ok = false;',
                     '            if (0) ok = false;')]),
 # one register file for the other
 'reg_class': (IO, [('  return a == b && afp == bfp;', '  return a == b;')]),
 # what the way out keeps
 'out_live': (IO, [('            if (same_reg(mm[i].dst, mm[i].fp, l, is_fp_value(f, x))) ok = false;',
                    '            if (0) ok = false;')]),
 'out_params': (IO, [('            if (same_reg(mm[i].dst, mm[i].fp, l, fp)) ok = false;',
                      '            if (0) ok = false;')]),
 'out_rewritten': (IO, [('          for (int j = 0; j < on && outdst && !rewritten; j++) rewritten = same_reg(om[j].dst, om[j].fp, l, fp);',
                         '')]),
}
EQUIVALENT = {}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}),
          ('gobench', ['python3', 'test/gobench.py', '--corpus', 'gobench', '--check'], {}))
