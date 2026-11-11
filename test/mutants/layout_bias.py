"""Which way of a test is the rare one without a profile, and the blocks only
rare ways reach emitted after the rest (ir.c cond_bias, ir_unlikely_way,
ir_emit_order's late blocks: the cold ones, and those only an unlikely way
reaches).  Killed by test/cases_opt/layout_bias.wat's `;; late:` shapes,
and the call arms' and loops' layouts the other cases pin.  Not here: the
rules kept out of the allocator's order (layout_score_of's emit) -- in it
they change only what the scan spills, which no case's code counts -- and
the rules' two readers besides the layout, which test/mutants/hot_tails.py
has: the tags LICM does not take for rare (ir_unlikely_way_sure) and the
bare null tests (ir_test_bare_null)."""
R = 'src/ir.c'
def m(old, new): return (R, [(old, new)])
M = {
 # the rules themselves
 'reftest': m('    if (in->op == I_REF_TEST || in->op == I_REF_SLOT) return 1;', '    if (in->op == I_REF_TEST || in->op == I_REF_SLOT) return 0;'),
 'refeq': m('    return in->op == I_EQ ? 1 : -1;', '    return 0;'),
 'refeq_null': m("    if (strong >= 2 && (rhs_const || lhs->op == I_CONST)) return 0;\n    return in->op == I_EQ ? 1 : -1;",
                 "    if (strong >= 2 && (rhs_const || lhs->op == I_CONST)) return 0;\n    return in->op == I_EQ ? -1 : 1;"),
 'eqz_neg': m('      return -cond_bias(f, x, depth + 1, emit, strong);', '      return cond_bias(f, x, depth + 1, emit, strong);'),
 'and_rule': m('      if (in->op == I_AND) return x < 0 || y < 0 ? -1 : x > 0 || y > 0 ? 1 : 0;',
               '      if (in->op == I_AND) return x > 0 || y > 0 ? 1 : x < 0 || y < 0 ? -1 : 0;'),
 'or_rule': m('      return x > 0 || y > 0 ? 1 : x < 0 && y < 0 ? -1 : 0;', '      return 0;'),
 'or_rare': m('      return x > 0 || y > 0 ? 1 : x < 0 && y < 0 ? -1 : 0;', '      return x > 0 || y > 0 ? 1 : 0;'),
 'geu': m('    case I_GEU: case I_GTU: return gc && gc_length(f, a[1]) ? -1 : 0;', '    case I_GEU: case I_GTU: return 0;'),
 'ltu': m('    case I_LTU: case I_LEU: return gc && gc_length(f, a[1]) ? 1 : 0;', '    case I_LTU: case I_LEU: return 0;'),
 'len_any': m('    case I_GEU: case I_GTU: return gc && gc_length(f, a[1]) ? -1 : 0;', '    case I_GEU: case I_GTU: return gc ? -1 : 0;'),
 'len_select': m('    if (in->op == I_SELECT && in->nargs == 3) {', '    if (0) {'),
 'len_field': m('    if (in->op == I_ARRAY_LEN || in->op == I_FIELD_GET) return true;\n    if (in->op == I_SELECT',
                '    if (in->op == I_FIELD_GET) return true;\n    if (in->op == I_SELECT'),
 'gc_off': m('  if (on < 0) on = warmc_getenv("WARMC_NO_GC_BIAS") == NULL;', '  if (on < 0) on = 0;'),
 # which rules are strong enough to move blocks
 'strong_int': m("  if (strong && !(gc && (in->op == I_GEU || in->op == I_GTU || in->op == I_LTU || in->op == I_LEU)))\n    return 0;", ""),
 'strong_eqz': m('    return strong ? 0 : -1;', '    return -1;'),
 'strong_flag': m('uint32_t ir_unlikely_way(const IrFunc *f, const Block *b) { return unlikely_way(f, b, 1); }',
                  'uint32_t ir_unlikely_way(const IrFunc *f, const Block *b) { return unlikely_way(f, b, 0); }'),
 # an index's test ahead of a store of the element goes either way (guards_store)
 'store_guard': m('  if (bias && guards_store(f, b, b->term_arg, strong)) return UINT32_MAX;\n', ''),
 'store_set': m('      return x->op == I_ARRAY_SET ? 1 : -1;', '      return 1;'),
 'store_both': m('      return x < 0 || y < 0 ? -1 : x > 0 || y > 0 ? 1 : 0;', '      return 0;'),
 'store_read': m('      return x < 0 || y < 0 ? -1 : x > 0 || y > 0 ? 1 : 0;', '      return x > 0 || y > 0 ? 1 : x < 0 || y < 0 ? -1 : 0;'),
 # a null test alone moves no block late
 'null_late': m('      if (w != UINT32_MAX && !null_late && ir_test_bare_null(f, P)) w = UINT32_MAX;\n', ''),
 'store_idx': m('      if (strip_width(f, f->argpool[x->args_off + 1]) != idx) continue;\n', ''),
 'store_side': m('  bool inside_true = (in->op == I_LTU || in->op == I_LEU) != neg;', '  bool inside_true = (in->op == I_LTU || in->op == I_LEU) == neg;'),
 # the late blocks
 'cold_late': m('    uint32_t flagged = b->outline || (cold_late && b->cold) || call_arm(f, bi);', '    uint32_t flagged = b->outline || call_arm(f, bi);'),
 'unlikely_late': m('      if (w != UINT32_MAX && P->succs[w].target == bi && P->succs[w ^ 1].target != bi) {', '      if (0) {'),
 'unlikely_way': m('      if (w != UINT32_MAX && P->succs[w].target == bi && P->succs[w ^ 1].target != bi) {',
                   '      if (w != UINT32_MAX && P->succs[w ^ 1].target == bi && P->succs[w].target != bi) {'),
 # how late: the count of rare ways on the way to a block (test/cases_opt/hot_tails.wat's tiers)
 'late_tiers': m('    late[bi] = (uint8_t)(tiers || !least ? least : 1);', '    late[bi] = (uint8_t)(least ? 1 : 0);'),
 'late_rare_max': m('        if (late[pb] + 1u > rare) rare = late[pb] + 1u;', '        if (!rare || late[pb] + 1u < rare) rare = late[pb] + 1u;'),
 'late_emit': m('  for (uint8_t pass = 0; pass <= LATE_MAX; pass++)', '  for (uint8_t pass = 0; pass <= LATE_MAX; pass += LATE_MAX)'),
 'emit_order': m('    if ((f->ord_defers || emit_bias) && m == f->norder) {', '    if (f->ord_defers && m == f->norder) {'),
}
EQUIVALENT = {}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}))
