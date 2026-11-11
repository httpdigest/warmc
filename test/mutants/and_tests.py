"""A branch on type tests made a branch a test (opt.c pass_test_chain), what
an and that held says of its sides (pass_gc_nullchk), and a test's value 0
or 1 (ir.c's known bits).  Killed by test/cases_opt/and_tests.wat's answers,
shapes and counts, and layout_bias.wat's andmix and andmix2 (a null test
that is a side of an and).  Not mutated: tc_movable's `in this block` -- by
then every test is in its branch's block (pass_cmp_sink)."""
O, I = 'src/opt.c', 'src/ir.c'
def m(old, new, path=O): return (path, [(old, new)])
KB1 = ('    case I_FEQ: case I_FNE: case I_FLT: case I_FGT: case I_FLE: case I_FGE:\n'
       '    case I_REF_IS_NULL: case I_REF_TEST: case I_REF_SLOT:\n'
       '      return 1;\n'
       '    default: return all;\n')
KB2 = ('    case I_FEQ: case I_FNE: case I_FLT: case I_FGT: case I_FLE: case I_FGE:\n'
       '    case I_REF_IS_NULL: case I_REF_TEST: case I_REF_SLOT:\n'
       '      return 1;\n'
       '\n'
       '    case I_CLZ: case I_CTZ: case I_POPCNT:')
M = {
 'off': m('  if (o->tchain && TP("test_chain", pass_test_chain(f)))', '  if (0 && o->tchain && TP("test_chain", pass_test_chain(f)))'),
 # which branches
 'and_bool': m('      if (is_and && ((ir_known_bits(f, x, KB_DEPTH) | ir_known_bits(f, y, KB_DEPTH)) & ~1ull))',
               '      if (0 && is_and && ((ir_known_bits(f, x, KB_DEPTH) | ir_known_bits(f, y, KB_DEPTH)) & ~1ull))'),
 'has_test': m('  return tc_has_test(f, a[0], 0) || tc_has_test(f, a[1], 0);', '  return true;'),
 'one_reader': m('      if (c >= nuses || uses[c] != 1 || !tc_of_tests(f, c)) continue;',
                 '      if (c >= nuses || !tc_of_tests(f, c)) continue;'),
 'side_reader': m('  if (y >= nuses || uses[y] != 1 || f->insts[y].block != u) return false;',
                  '  if (y >= nuses || f->insts[y].block != u) return false;'),
 'side_effect': m('  return ir_has_no_effect(in) && in->op != I_PARAM && in->op != I_CONST && in->op != I_NOP &&',
                  '  return in->op != I_PARAM && in->op != I_CONST && in->op != I_NOP &&'),
 'swap': m('        if (!tc_movable(f, uses, nuses, x, u)) continue;\n        Ref t = x; x = y; y = t;',
           '        continue;'),
 'rounds': m('  for (int round = 0; round < 4; round++) {\n    uint32_t *uses = count_uses(f);',
             '  for (int round = 0; round < 1; round++) {\n    uint32_t *uses = count_uses(f);'),
 'tree': m('    if (tc_movable(f, uses, nuses, args[k], u))\n      args[k] = tc_move(f, uses, nuses, args[k], u, n, depth + 1);',
           '    ;'),
 # where they go
 'and_way': m('      Edge *go = &b->succs[is_and ? 0 : 1];', '      Edge *go = &b->succs[1];'),
 'or_way': m('      Edge *go = &b->succs[is_and ? 0 : 1];', '      Edge *go = &b->succs[0];'),
 'first_side': m('      b->chained = true;\n      b->term_arg = x;', '      b->chained = true;\n      b->term_arg = y;'),
 # a null test that is a side of an and
 'chained_new': m('      f->blocks[n].chained = true;', '      f->blocks[n].chained = false;'),
 'chained_old': m('      b->chained = true;\n      b->term_arg = x;', '      b->term_arg = x;'),
 'chained_read': m('  if (b->term != T_BRIF || b->chained) return false;', '  if (b->term != T_BRIF) return false;', I),
 # what the and says of its sides
 'facts': m('        bool and_on = !warmc_getenv("WARMC_NO_AND_FACTS");', '        bool and_on = false;'),
 'facts_way': m('            (f->insts[cond].op == (taken ? I_AND : I_OR)) && nq + 2 <= GC_AND_MAX) {',
                '            (f->insts[cond].op == (taken ? I_OR : I_AND)) && nq + 2 <= GC_AND_MAX) {'),
 'facts_and': m('            (f->insts[cond].op == (taken ? I_AND : I_OR)) && nq + 2 <= GC_AND_MAX) {',
                '            (f->insts[cond].op == (taken ? I_AND : I_AND)) && nq + 2 <= GC_AND_MAX) {'),
 'facts_or': m('            (f->insts[cond].op == (taken ? I_AND : I_OR)) && nq + 2 <= GC_AND_MAX) {',
               '            (f->insts[cond].op == (taken ? I_OR : I_OR)) && nq + 2 <= GC_AND_MAX) {'),
 'facts_side': m('          cq[nq] = ir_args(f, cond)[1]; tq[nq++] = taken;\n          continue;',
                 '          continue;'),
 'facts_deep': m('#define GC_AND_MAX 8', '#define GC_AND_MAX 3'),
 # a test is 0 or 1
 'bits_max': m(KB1, KB1.replace(' case I_REF_TEST: case I_REF_SLOT:', ''), I),
 'bits_known': m(KB2, KB2.replace(' case I_REF_TEST: case I_REF_SLOT:', ''), I),
}
EQUIVALENT = {}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}))
