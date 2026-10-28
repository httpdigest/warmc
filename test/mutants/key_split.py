"""opt.c pass_key_split: below an edge whose way fixes a value, the code that
tests it again copied with the value a constant.  Killed by
test/cases_opt/key_split.wat (made by test/gen_keysplit.py) -- its answers
at every level and how many copies -O1 and -O2 make -- and
test/run_levels.sh."""
O, MAIN = 'src/opt.c', 'src/main.c'
def m(old, new, path=O): return (path, [(old, new)])
M = {
 # when
 'off': m('    if (o->keysplit && RUN(14, "keysplit", pass_key_split(f))) changed = true;\n', ''),
 'flag': m('      else if (!strcmp(name, "key-split")) opt.keysplit = on;',
           '      else if (!strcmp(name, "key-split")) opt.keysplit = true;', MAIN),
 # the key
 'key_eqz': m('    if (ci->op == I_EQZ && ci->nargs == 1) y = ir_args(f, b->term_arg)[0];\n    else if (ci->op >= I_EQ && ci->op <= I_GEU && ci->nargs == 2 &&\n             is_const(f, ir_args(f, b->term_arg)[1], &k))\n      y = ir_args(f, b->term_arg)[0];\n    else return REF_NONE;\n  } else if (b->term == T_BRTABLE) {',
              '    if (ci->op >= I_EQ && ci->op <= I_GEU && ci->nargs == 2 &&\n             is_const(f, ir_args(f, b->term_arg)[1], &k))\n      y = ir_args(f, b->term_arg)[0];\n    else return REF_NONE;\n  } else if (b->term == T_BRTABLE) {'),
 'key_const': m('    else if (ci->op >= I_EQ && ci->op <= I_GEU && ci->nargs == 2 &&\n             is_const(f, ir_args(f, b->term_arg)[1], &k))\n      y = ir_args(f, b->term_arg)[0];\n    else return REF_NONE;\n  } else if (b->term == T_BRTABLE) {',
                '    else if (ci->op >= I_EQ && ci->op <= I_GEU && ci->nargs == 2)\n      y = ir_args(f, b->term_arg)[0];\n    else return REF_NONE;\n  } else if (b->term == T_BRTABLE) {'),
 'key_table': m('  } else if (b->term == T_BRTABLE) {\n    y = b->term_arg;\n  } else {\n    return REF_NONE;\n  }\n  const Inst *yi = &f->insts[y];\n  Ref x = (yi->op',
                '  } else {\n    return REF_NONE;\n  }\n  const Inst *yi = &f->insts[y];\n  Ref x = (yi->op'),
 'key_plus': m('  Ref x = (yi->op == I_ADD || yi->op == I_SUB) && yi->nargs == 2 &&\n          is_const(f, ir_args(f, y)[1], &k) ? ir_args(f, y)[0] : y;\n  ValType t = (ValType)f->insts[x].type;\n  return t == VT_I32',
               '  Ref x = y;\n  (void)yi;\n  ValType t = (ValType)f->insts[x].type;\n  return t == VT_I32'),
 'tests': m('#define KS_TESTS 4', '#define KS_TESTS 3'),
 'tests_hi': m('#define KS_TESTS 4', '#define KS_TESTS 6'),
 # which edges
 'exact': m('        if (kway.n != 1 || kway.lo[0] != kway.hi[0]) continue;', '        if (kway.n != 1) continue;'),
 'same': m('#define KS_SAME 2', '#define KS_SAME 8'),
 'same_lo': m('#define KS_SAME 2', '#define KS_SAME 0'),
 # the walk
 'way_true': m('      if (yes.lo[i] <= k && k <= yes.hi[i]) return 0;\n    return 1;', '      if (yes.lo[i] <= k && k <= yes.hi[i]) return 1;\n    return 0;'),
 'way_index': m('  uint64_t idx = (k + d) & mask;\n  uint32_t n = b->nsuccs - 1;', '  uint64_t idx = k & mask;\n  uint32_t n = b->nsuccs - 1;'),
 'way_default': m('  return mask == 0xffffffffu && idx < n ? (uint32_t)idx : n;', '  return mask == 0xffffffffu && idx < n ? (uint32_t)idx : 0;'),
 'walk_only': m('      if (only != UINT32_MAX && s != only) continue;\n      uint32_t t = yb->succs[s].target;', '      uint32_t t = yb->succs[s].target;'),
 'header': m('  for (uint32_t p = 0; p < b->npreds; p++)\n    if (f->blocks[b->preds[p]].rpo >= b->rpo) return false;\n  for (uint32_t i = 0; i < b->ninsts; i++) {\n    uint16_t op = f->insts[b->insts[i]].op;\n    if (is_call_op(op)',
             '  for (uint32_t i = 0; i < b->ninsts; i++) {\n    uint16_t op = f->insts[b->insts[i]].op;\n    if (is_call_op(op)'),
 'kept_tests': m('  for (uint32_t x2 = 0; x2 < nb; x2++) if (reach[x2] == 3) keep[x2] = 1;\n', ''),
 'kept_all': m('      if (!reach[y] || keep[y]) continue;\n      const Block *yb = &f->blocks[y];\n      for (uint32_t s = 0; s < yb->nsuccs; s++)\n        if (keep[yb->succs[s].target] && reach[yb->succs[s].target]) {',
               '      if (!reach[y] || keep[y]) continue;\n      const Block *yb = &f->blocks[y];\n      for (uint32_t s = 0; s < yb->nsuccs; s++)\n        if (1) {'),
 'decided': m('#define KS_DECIDED 2', '#define KS_DECIDED 1'),
 'decided_hi': m('#define KS_DECIDED 2', '#define KS_DECIDED 4'),
 'copy_hi': m('#define KS_COPY 1024', '#define KS_COPY 2048'),
 'copy_lo': m('#define KS_COPY 1024', '#define KS_COPY 512'),
 'grow': m('#define KS_GROW 2', '#define KS_GROW 1'),
 # the copy
 'sub_off': m('    for (uint32_t y = 0; y < nb0; y++) {\n      if (!keep[y]) continue;\n      Block *cb = &f->blocks[bcopy[y]];\n      for (uint32_t i = 0; i < cb->ninsts; i++) {',
              '    for (uint32_t y = 0; y < nb0 && 0; y++) {\n      if (!keep[y]) continue;\n      Block *cb = &f->blocks[bcopy[y]];\n      for (uint32_t i = 0; i < cb->ninsts; i++) {'),
 'sub_plus': m('  if (rt == (ValType)f->insts[x].type && kt_offset(f, r, x, &d)) {\n    *c = (k + d) & mask;',
               '  if (rt == (ValType)f->insts[x].type && kt_offset(f, r, x, &d)) {\n    *c = k & mask;'),
 'sub_cmp': m('  if (!cmp || !kt_test(f, r, x, true, mask, &yes)) return false;', '  if (1) return false;'),
 'sub_cmp_val': m('    if (yes.lo[i] <= k && k <= yes.hi[i]) *c = 1;\n  *t = VT_I32;', '    if (yes.lo[i] <= k && k <= yes.hi[i]) *c = 0;\n  *t = VT_I32;'),
 'jumps_off': m('      if (!keep[y] || reach[y] != 3) continue;\n      Block *cb = &f->blocks[bcopy[y]];\n      cb->succs[0] = cb->succs[way[y]];',
                '      if (1) continue;\n      Block *cb = &f->blocks[bcopy[y]];\n      cb->succs[0] = cb->succs[way[y]];'),
 'inside': m('    uint32_t nesc = escaping_values(f, keep, nb0, ni0, true, esc, eslot, &euse);',
             '    uint32_t nesc = escaping_values(f, keep, nb0, ni0, false, esc, eslot, &euse);'),
 'inside_self': m('        in[f->insts[r_].block] && f->insts[r_].block != x) {                  \\',
                  '        in[f->insts[r_].block]) {                                             \\'),
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('levels', ['./test/run_levels.sh'], {}))
