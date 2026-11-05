"""opt.c merge_blocks -- a block that jumps to one whose only way in it is
becomes one block with it -- and ifc_try's counter rule: a branch on a
test of the loop's counter alone that picks a value the loop carries keeps
its branch (ifc_counter, ifc_counter_cond, ifc_carried).  Killed by
test/cases_opt/way_facts.wat (its answers, which thousands of merges
compile, and blocks_merged), ifc_counter.wat (counter's branch, stored's
and hashed's selects) and the answers of the cases (test/run_tests.sh)."""
O = 'src/opt.c'
def m(old, new, path=O): return (path, [(old, new)])
M = {
 'merge_off': m('if (on < 0) on = warmc_getenv("WARMC_NO_BLOCK_MERGE") == NULL;', 'on = 0;'),
 'merge_jump': m('      if (p->term != T_JUMP || p->nsuccs != 1) break;', '      if (p->nsuccs != 1) break;'),
 'merge_preds': m('      if (sb->npreds != 1 || sb->preds[0] != pi) break;',
                  '      if (sb->preds[0] != pi) break;'),
 'merge_params': m('          ir_replace(f, sb->params[i], p->succs[0].args[i]);\n', ''),
 'merge_block_of': m('        f->insts[sb->insts[i]].block = pi;\n', ''),
 'merge_preds_fix': m('          if (tb->preds[q] == si) tb->preds[q] = pi;', '          (void)q;'),
 'merge_chain': m('    for (int guard = 0; guard < 64; guard++) {', '    for (int guard = 0; guard < 1; guard++) {'),
 'ifc_off': m('    if (b->loop_depth && ifc_counter_cond(f, cond, 4) && ifc_carried(f, ji, p) &&',
              '    if (0 && ifc_counter_cond(f, cond, 4) && ifc_carried(f, ji, p) &&'),
 'ifc_counter_step': m("""      if ((ai->op != I_ADD && ai->op != I_SUB) || ai->nargs != 2 ||
          ir_resolve(f, ir_args(f, a)[0]) != x || !is_const(f, ir_args(f, a)[1], &c))
        return false;""", """      (void)ai; (void)c;"""),
 'ifc_counter_param': m('  if (in->op == I_PARAM) return ifc_counter(f, c);', '  if (in->op == I_PARAM) return true;'),
 'ifc_cond_ops': m('    case I_AND: case I_OR: case I_XOR: case I_SHL: case I_SHRU: case I_SHRS:',
                   '    case I_OR: case I_XOR: case I_SHL: case I_SHRU: case I_SHRS:'),
 'ifc_carried_follow': m('        if (tb->npreds == 1) next = cb->succs[s].target;', '        (void)tb;'),
 'ifc_carried_any': m("""        for (uint32_t k = 0; k < nd; k++)
          if (der[k] == v) return true;
      }
    }
    if (next == UINT32_MAX) break;""", """        (void)v;
      }
    }
    if (next == UINT32_MAX) break;"""),
}
EQUIVALENT = {}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {}))
