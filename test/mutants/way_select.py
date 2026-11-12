"""A select whose condition each way into its block decides is a parameter
of the block (opt.c pass_bit_facts).  Killed by
test/cases_opt/way_select.wat's answers, its count and its shapes.

Not mutated: WARMC_NO_WAY_SELECTS and WARMC_NO_WAY_SELECT_MADE, which no
case sets; the walks' budget, BF_WALKS, which no case reaches; the test
that what is made on a way reads no result of the call that ends the way,
a block the compiler does not make: the block after a call that a handler
stands behind begins where the call returns."""
O = 'src/opt.c'
def m(old, new): return (O, [(old, new)])
HEAD = '        if (op == I_SELECT && in->nargs == 3 && sels) {'
OWN = '            if (f->insts[sa[k]].block != bi || f->insts[sa[k]].op == I_CONST) continue;'
MADE = '            ok = bf_sel_made(f, sa[k], bi) && wuses[sa[k]] == 1 && !warmc_getenv("WARMC_NO_WAY_SELECT_MADE");'
DEC = '              bool zero = e.n && !e.lo[0], only = e.n == 1 && !e.hi[0];'
UND = '              if (zero && !only) { ok = false; break; }'
TAKE = '              d.q[d.ne] = q; d.s[d.ne] = s; d.take[d.ne] = zero ? 1 : 0;'
LOOP = '          for (int k = 0; k < 2 && differ && ok; k++) {\n'
KCLONE = '              g = emit_at_end(f, d->q[e], I_CONST, gt, NULL, 0, gi->imm);'
ACLONE = ('                if (f->insts[ga[a]].block == d->bi)\n'
          '                  ga[a] = emit_at_end(f, d->q[e], I_CONST, (ValType)f->insts[ga[a]].type, NULL, 0, f->insts[ga[a]].imm);')
GCLONE = '              g = emit_at_end(f, d->q[e], (IrOp)gop, gt, ga, gn, gimm);'
OPS = ('    case I_ADD: case I_SUB: case I_AND: case I_OR: case I_XOR:\n'
       '    case I_SHL: case I_SHRU: case I_SHRS:\n      break;\n    default:\n      return false;\n  }\n'
       '  for (uint32_t a = 0; a < 2; a++) {\n    const Inst *ai = &f->insts[ir_args(f, v)[a]];')
M = {
 'off': m(HEAD, '        if (0 && op == I_SELECT && in->nargs == 3 && sels) {'),
 # which selects
 # which selects
 'in_block': m(MADE, '            ok = true;'),
 'no_consts': m(OWN, '            if (f->insts[sa[k]].block != bi) continue;'),
 'first_only': m(LOOP + OWN, '          for (int k = 0; k < 1 && differ && ok; k++) {\n' + OWN),
 'second_only': m(LOOP + OWN, '          for (int k = 1; k < 2 && differ && ok; k++) {\n' + OWN),
 'agreed_above': m(LOOP + OWN, '          for (int k = 0; k < 2 && ok; k++) {\n' + OWN),
 # what the block makes for the select
 'made_any_op': m(OPS, OPS.replace('    default:\n      return false;\n', '    default:\n      break;\n')),
 'made_no_shift': m(OPS, OPS.replace('    case I_SHL: case I_SHRU: case I_SHRS:\n', '    case I_SHRU: case I_SHRS:\n')),
 'made_no_xor': m(OPS, OPS.replace(' case I_OR: case I_XOR:', ' case I_OR:')),
 'made_of_block': m('    if (ai->block == bi && ai->op != I_CONST) return false;', ''),
 'made_read_twice': m(MADE, MADE.replace(' && wuses[sa[k]] == 1', '')),
 'made_none': m(MADE, '            ok = false;'),
 'made_after_call': m(GCLONE, '              g = ir_emit(f, d->q[e], (IrOp)gop, gt, ga, gn, gimm, 0);'),
 'made_const_shared': m(ACLONE, ''),
 'made_first_way': m(GCLONE, GCLONE.replace('d->q[e]', 'd->q[0]')),
 'made_count': m('            ir_make_nop(&f->insts[arm[k]]);\n            g_stats.way_select_made++;', '            ir_make_nop(&f->insts[arm[k]]);'),
 # a constant of the block's own
 'const_shared': m('            if (gi->op == I_CONST) {\n' + KCLONE, '            if (gi->op == I_CONST) {'),
 'const_first_way': m(KCLONE, KCLONE.replace('d->q[e]', 'd->q[0]')),
 'const_after_call': m(KCLONE, '              g = ir_emit(f, d->q[e], I_CONST, gt, NULL, 0, gi->imm, 0);'),
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
