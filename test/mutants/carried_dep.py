"""opt.c pass_reassoc_carried's terms that read the carried value themselves
(rc_dependents, rc_leaves): added after it.  Killed by
test/cases_opt/carried_dep.wat's answers, its count and its shapes.

Not mutated: WARMC_NO_CARRIED_DEP, which no case sets; RC_LINKS, the most
terms, which no case reaches; a term's type, which is its chain's."""
O = 'src/opt.c'
def m(old, new): return (O, [(old, new)])
LEAF = ('  if (v < nuses && uses[v] == 1 && in->op == ri->op && in->type == ri->type && in->imm2 == ri->imm2 &&\n'
        '      in->nargs == 2 && f->blocks[in->block].rpo >= lo)')
M = {
 'off': m('        if (!warmc_getenv("WARMC_NO_CARRIED_DEP")) {', '        if (0) {'),
 # which chains
 'path_of_two': m('        if (!n) continue;\n        bool twice = false;', '        if (n < 2) continue;\n        bool twice = false;'),
 'all_to_caller': m('          if (d != RC_NONE) continue;', '          if (d == RC_DONE) continue;'),
 # the terms
 'leaf_read_elsewhere': m(LEAF, LEAF.replace('uses[v] == 1 && ', '')),
 'leaf_any_op': m(LEAF, LEAF.replace('in->op == ri->op && ', '')),
 'leaf_any_lane': m(LEAF, LEAF.replace(' && in->imm2 == ri->imm2', '')),
 'leaf_before_loop': m(LEAF, LEAF.replace(' && f->blocks[in->block].rpo >= lo', '')),
 'leaf_first_only': m('    return rc_leaves(f, ir_args(f, v)[0], ri, uses, nuses, lo, leaf, nl) &&\n'
                      '           rc_leaves(f, ir_args(f, v)[1], ri, uses, nuses, lo, leaf, nl);',
                      '    return rc_leaves(f, ir_args(f, v)[0], ri, uses, nuses, lo, leaf, nl);'),
 # which read the carried value
 'none_read': m('    for (uint32_t a = 0; a < li->nargs && !dep[k]; a++) dep[k] = ir_args(f, leaf[k])[a] == p;',
                '    (void)li;'),
 'first_operand': m('    for (uint32_t a = 0; a < li->nargs && !dep[k]; a++) dep[k] = ir_args(f, leaf[k])[a] == p;',
                    '    for (uint32_t a = 0; a < li->nargs && a < 1 && !dep[k]; a++) dep[k] = ir_args(f, leaf[k])[a] == p;'),
 'twice': m('    if (leaf[k] == p) return RC_ALL;\n', ''),
 'none_done': m('  if (!ndep) return RC_NONE;\n', ''),
 'all_done': m('  if (ndep == nl) return RC_ALL;\n', ''),
 # the chain made
 'dependent_among': m('  for (uint32_t k = 0; k < nl; k++) {\n    if (dep[k]) continue;\n    if (!nk && is_const',
                      '  for (uint32_t k = 0; k < nl; k++) {\n    if (!nk && is_const'),
 'const_last': m('      if (!have_k) kv = c;\n      else fold_int_binop(op, ty, kv, c, &kv);\n      have_k = true;\n      continue;\n    }\n    Ref pr[2] = { acc, leaf[k] };',
                 '      kv = c;\n      have_k = true;\n      continue;\n    }\n    Ref pr[2] = { acc, leaf[k] };'),
 'const_dropped': m('  if (have_k) {\n    Ref pr[2] = { t, emit_before', '  if (0) {\n    Ref pr[2] = { t, emit_before'),
 'const_a_term': m('    if (!nk && is_const(f, leaf[k], &c)) {\n      if (!have_k) kv = c;\n      else fold_int_binop(op, ty, kv, c, &kv);\n      have_k = true;\n      continue;\n    }\n    Ref pr[2] = { acc, leaf[k] };',
                   '    Ref pr[2] = { acc, leaf[k] };'),
 'others_lane': m('    f->insts[acc].imm2 = acc == leaf[k] ? f->insts[acc].imm2 : lane;\n', ''),
 'carried_lane': m('    t = emit_before(f, rb, &at, (IrOp)op, ty, pr, 2, 0);\n    f->insts[t].imm2 = lane;\n  }\n  if (have_k) {',
                   '    t = emit_before(f, rb, &at, (IrOp)op, ty, pr, 2, 0);\n  }\n  if (have_k) {'),
 'carried_dropped': m('  Ref t = p;\n  if (acc != REF_NONE) {', '  Ref t = p;\n  if (0) {'),
 'last_not_placed': m('    if (--left) {\n      t = emit_before(f, rb, &at, (IrOp)op, ty, pr, 2, 0);\n      f->insts[t].imm2 = lane;\n    } else {',
                      '    if (left--) {\n      t = emit_before(f, rb, &at, (IrOp)op, ty, pr, 2, 0);\n      f->insts[t].imm2 = lane;\n    } else {'),
 'dependent_lane': m('    if (--left) {\n      t = emit_before(f, rb, &at, (IrOp)op, ty, pr, 2, 0);\n      f->insts[t].imm2 = lane;\n    } else {',
                     '    if (--left) {\n      t = emit_before(f, rb, &at, (IrOp)op, ty, pr, 2, 0);\n    } else {'),
}
EQUIVALENT = {}
SUITES = (('optcases', ['python3', 'test/run_optcases.py', 'carried_dep'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}))
