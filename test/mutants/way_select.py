"""A select whose condition each way into its block decides is a parameter
of the block (opt.c pass_bit_facts).  Killed by
test/cases_opt/way_select.wat's answers, its count and its shapes.

Not mutated: WARMC_NO_WAY_SELECTS, which no case sets; the walks' budget,
BF_WALKS, which no case reaches."""
O = 'src/opt.c'
def m(old, new): return (O, [(old, new)])
HEAD = '        if (op == I_SELECT && in->nargs == 3 && sels) {'
ABOVE = '            ok &= f->insts[sa[k]].block != bi || f->insts[sa[k]].op == I_CONST;'
DEC = '              bool zero = e.n && !e.lo[0], only = e.n == 1 && !e.hi[0];'
UND = '              if (zero && !only) { ok = false; break; }'
TAKE = '              d.q[d.ne] = q; d.s[d.ne] = s; d.take[d.ne] = zero ? 1 : 0;'
LOOP = '          for (int k = 0; k < 2 && differ; k++)\n'
CLONE = ('          if (f->insts[g].block == d->bi)\n'
         '            g = emit_at_end(f, d->q[e], I_CONST, (ValType)f->insts[g].type, NULL, 0, f->insts[g].imm);')
M = {
 'off': m(HEAD, '        if (0 && op == I_SELECT && in->nargs == 3 && sels) {'),
 # which selects
 'in_block': m(ABOVE, '            ok &= true;'),
 'no_consts': m(ABOVE, '            ok &= f->insts[sa[k]].block != bi;'),
 'first_only': m(LOOP + ABOVE, '          for (int k = 0; k < 1 && differ; k++)\n' + ABOVE),
 'second_only': m(LOOP + ABOVE, '          for (int k = 1; k < 2 && differ; k++)\n' + ABOVE),
 'agreed_above': m(LOOP + ABOVE, '          for (int k = 0; k < 2; k++)\n' + ABOVE),
 'always_param': m('          d.param = differ;', '          d.param = true;'),
 'never_param': m('          d.param = differ;', '          d.param = false;'),
 'differ_last': m('              differ |= d.take[d.ne] != d.take[0];', '              differ = d.take[d.ne] != d.take[0];'),
 'agreed_second': m('      Ref to = arm[d->take[0]];', '      Ref to = arm[1];'),
 'agreed_first': m('      Ref to = arm[d->take[0]];', '      Ref to = arm[0];'),
 'untested': m('          if (!tested[x] && (f->insts[x].op == I_PARAM || !bloom[bf_key(f, x)])) continue;\n          BfSel d;',
               '          BfSel d;'),
 'fewer_ways': m('#define BF_SEL_WAYS 8', '#define BF_SEL_WAYS 7'),
 # what a way decides
 'undecided_zero': m(UND, ''),
 'undecided_only': m(DEC, '              bool zero = e.n && !e.lo[0], only = e.n >= 1 && !e.lo[0];'),
 'zero_first': m(DEC, '              bool zero = e.n && !e.lo[e.n - 1], only = e.n == 1 && !e.hi[0];'),
 'only_low': m(DEC, '              bool zero = e.n && !e.lo[0], only = e.n == 1 && !e.lo[0];'),
 'swapped': m(TAKE, '              d.q[d.ne] = q; d.s[d.ne] = s; d.take[d.ne] = zero ? 0 : 1;'),
 'take_first': m(TAKE, '              d.q[d.ne] = q; d.s[d.ne] = s; d.take[d.ne] = 0;'),
 'mask_low': m('              kw->mask = 0xffffffffu;\n              kw->def = f->insts[x].block;', '              kw->mask = 0xffu;\n              kw->def = f->insts[x].block;'),
 # what is made of it
 'const_shared': m(CLONE, ''),
 'const_first_way': m(CLONE, CLONE.replace('d->q[e], I_CONST', 'd->q[0], I_CONST')),
 'const_after_call': m(CLONE, CLONE.replace('emit_at_end(f, d->q[e], I_CONST, (ValType)f->insts[g].type, NULL, 0, f->insts[g].imm)',
                                            'ir_emit(f, d->q[e], I_CONST, (ValType)f->insts[g].type, NULL, 0, f->insts[g].imm, 0)')),
 'one_edge': m('        for (uint32_t e = 0; e < d->ne; e++) {\n          Ref g = arm[d->take[e]];', '        for (uint32_t e = 0; e < 1; e++) {\n          Ref g = arm[d->take[e]];'),
 'select_stays': m('      ir_replace(f, d->v, to);\n      ir_make_nop(&f->insts[d->v]);\n      g_stats.way_selects++;', '      g_stats.way_selects++;'),
 'count': m('      ir_make_nop(&f->insts[d->v]);\n      g_stats.way_selects++;', '      ir_make_nop(&f->insts[d->v]);'),
 'no_subst': m('    if (nfl || nsl) ir_apply_subst(f);', '    if (nfl) ir_apply_subst(f);'),
 'unreachable_way': m('            if (q >= f->nblocks || !f->blocks[q].reachable) { ok = false; break; }\n            for (uint32_t s = 0; s < f->blocks[q].nsuccs && ok; s++) {\n              if (f->blocks[q].succs[s].target != bi) continue;\n              if (d.ne',
                      '            for (uint32_t s = 0; s < f->blocks[q].nsuccs && ok; s++) {\n              if (f->blocks[q].succs[s].target != bi) continue;\n              if (d.ne'),
}
EQUIVALENT = {
 'untested': 'a value no branch tests is one no way in decides: the walks are saved, nothing else',

 'unreachable_way': 'a block no run reaches is among no block\'s ways in once the passes before this one have run',
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py', 'way_select'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}))
