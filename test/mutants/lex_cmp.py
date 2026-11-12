"""opt.c lx_match, fold_lex_select, lx_branch and ir.c ir_lex_pair, which
they read the test with: two pairs compared by their first members and then
by their second, as one compare of two keys.  Killed
by test/cases_opt/lex_cmp.wat's answers, its counts and its shapes, where a
mutant makes a key of the wrong values, of a value no block before it
makes, or of another width.

Not mutated: WARMC_NO_LEX_CMP, which no case sets; the bound on how far a
value is followed to the bits it is made of (lx_field's depth), which is not
a rule; the tests that keep the use counts within the instructions they
were made for (fold_n), and an and's mask from being 0, which the count of
its bits is undefined for; the test that a shift leaves any of a value's
bits, which only keeps the arithmetic after it in range; and the tests that
the two blocks end in branches, which are what say their two ways out may
be read at all."""
O = 'src/opt.c'
I = 'src/ir.c'
def m(old, new): return (O, [(old, new)])
def mi(old, new): return (I, [(old, new)])

ROT = '  if (!fm.sh && fn.sh == fm.w && fn.sh + fn.w == W) {\n    s->kind = LX_ROT;\n    s->c = W - fm.w;'
SAME = '  } else if (!fn.sh && fm.sh == fn.w && fm.sh + fm.w == W) {\n    s->kind = LX_SAME;\n    s->c = fn.w;'
FIRSTS = '  if (!(ma[0] == ea[0] && ma[1] == ea[1]) && !(ma[0] == ea[1] && ma[1] == ea[0])) return false;\n'
MKNOWN = ('  if (mg && !((ir_known_bits(f, ma[0], KB_MAXD) | ir_known_bits(f, ma[1], KB_MAXD)) >> (wm - 1)))\n'
          '    mg = false;\n')
NKNOWN = ('  if (ng && ((ir_known_bits(f, na[0], KB_MAXD) | ir_known_bits(f, na[1], KB_MAXD)) >> (wn - 1)))\n'
          '    return false;\n')
OP = '  x->op = p.sgn ? (p.strict ? I_LTS : I_LES) : (p.strict ? I_LTU : I_LEU);'
INV = '  if (negn) { nl = !nl; ns = !ns; }'
LAYOUT = '    if (s->kind >= LX_ROT && s->c != x->c) s->kind = LX_GEN;'
USES = ('  if (c >= f->fold_n || cn >= f->fold_n || cm >= f->fold_n ||\n'
        '      uses[c] != 1 || uses[cn] != 1 || uses[cm] != 1)\n    return false;')
PLAIN = ('      case I_CONST: case I_WRAP: case I_EXTENDU: case I_SHRU: case I_AND:\n'
         '      case I_EQ: case I_NE: case I_LTS: case I_LTU: case I_GTS: case I_GTU:\n'
         '      case I_LES: case I_LEU: case I_GES: case I_GEU:\n        break;\n      default:\n        return false;')
HANDED = "    Ref pa = lx_handed(f, bi, p->args[j], b0, b1), qa = lx_handed(f, bi, q->args[j], b0, b1);"
EDGE = ('    if (pa == qa) continue;\n'
        '    if (!is_const(f, pa, &x) || !is_const(f, qa, &y) || x != y) return false;')
BRING = '  if (in.block != b0 && in.block != b1) return v;'
BLOCKS = '  if (E->term != T_BRIF || N->term != T_BRIF || !lx_plain(f, E) || !lx_plain(f, N)) return false;'
WAYS = ('  if (!same && !(lx_same_edge(f, bi, &N->succs[0], &E->succs[1], se, sn) &&\n'
        '                 lx_same_edge(f, bi, &N->succs[1], &E->succs[0], se, sn)))\n    return false;\n')
COST = ('    if (s->kind >= LX_ROT ? !lx_outside(f, s->src, bi, se, sn)\n'
        '        : s->kind == LX_GEN && (!lx_outside(f, s->m, bi, se, sn) || !lx_outside(f, s->n, bi, se, sn)))')

M = {
 # a value as bits of another
 'no_peel': m('  if (depth == 4 || !in->nargs) return r;', '  if (depth == 0 || !in->nargs) return r;'),
 'peel_one': m('  if (depth == 4 || !in->nargs) return r;', '  if (depth == 1 || !in->nargs) return r;'),
 'wrap_wide': m('      if (s.w > 32) s.w = 32;\n', ''),
 'any_and': m('!is_const(f, a[1], &c) || !c || (c & (c + 1))) return r;', '!is_const(f, a[1], &c) || !c) return r;'),
 'and_width': m('      if (s.w > 64u - (uint32_t)__builtin_clzll(c)) s.w = 64u - (uint32_t)__builtin_clzll(c);\n', ''),
 'shift_mod': m('      c &= W - 1;\n', ''),
 'shift_from': m('      s.sh += (uint32_t)c;\n', ''),
 'shift_width': m('      s.w -= (uint32_t)c;\n', ''),
 # a pair as one value's bits
 'one_const': m('  if (is_const(f, s->m, &s->cm) && is_const(f, s->n, &s->cn)) {', '  if (is_const(f, s->m, &s->cm) || is_const(f, s->n, &s->cn)) {'),
 'any_source': m('  if (fm.src != fn.src) return;\n', ''),
 'rot_first_shifted': m(ROT, ROT.replace('if (!fm.sh && fn.sh', 'if (fn.sh')),
 'rot_gap': m(ROT, ROT.replace('fn.sh == fm.w && ', '')),
 'rot_short': m(ROT, ROT.replace(' && fn.sh + fn.w == W', '')),
 'rot_width': m(ROT, ROT.replace('s->c = W - fm.w;', 's->c = fm.w;')),
 'rot_is_same': m(ROT, ROT.replace('LX_ROT', 'LX_SAME')),
 'same_second_shifted': m(SAME, SAME.replace('if (!fn.sh && fm.sh', 'if (fm.sh')),
 'same_gap': m(SAME, SAME.replace('fm.sh == fn.w && ', '')),
 'same_short': m(SAME, SAME.replace(' && fm.sh + fm.w == W', '')),
 'same_width': m(SAME, SAME.replace('s->c = fn.w;', 's->c = W - fn.w;')),
 'same_is_rot': m(SAME, SAME.replace('LX_SAME', 'LX_ROT')),
 # the compares
 'ltu_greater': mi('case I_LTU: *less = true;  *strict = true; ', 'case I_LTU: *less = false; *strict = true; '),
 'leu_strict': mi('case I_LEU: *less = true;  *strict = false;', 'case I_LEU: *less = true;  *strict = true; '),
 'gtu_less': mi('case I_GTU: *less = false; *strict = true; ', 'case I_GTU: *less = true;  *strict = true; '),
 'gtu_loose': mi('case I_GTU: *less = false; *strict = true; ', 'case I_GTU: *less = false; *strict = false;'),
 'geu_strict': mi('case I_GEU: *less = false; *strict = false;', 'case I_GEU: *less = false; *strict = true; '),
 'lts_unsigned': mi('case I_LTS: *less = true;  *strict = true;  *sgn = true; ', 'case I_LTS: *less = true;  *strict = true;  *sgn = false;'),
 'les_unsigned': mi('case I_LES: *less = true;  *strict = false; *sgn = true; ', 'case I_LES: *less = true;  *strict = false; *sgn = false;'),
 'les_greater': mi('case I_LES: *less = true;  *strict = false;', 'case I_LES: *less = false; *strict = false;'),
 'gts_less': mi('case I_GTS: *less = false; *strict = true; ', 'case I_GTS: *less = true;  *strict = true; '),
 'gts_unsigned': mi('case I_GTS: *less = false; *strict = true;  *sgn = true; ', 'case I_GTS: *less = false; *strict = true;  *sgn = false;'),
 'ges_less': mi('case I_GES: *less = false; *strict = false;', 'case I_GES: *less = true;  *strict = false;'),
 'ges_unsigned': mi('case I_GES: *less = false; *strict = false; *sgn = true; ', 'case I_GES: *less = false; *strict = false; *sgn = false;'),
 # the keys' layout
 'signed_anywhere': m('  if (sgn && wm != x->W - x->c) return false;\n', ''),
 'other_layout': m(LAYOUT, '    (void)0;'),
 'second_wide': m('    if (ir_known_bits(f, s->n, KB_DEPTH) >> x->c) return false;\n', ''),
 'first_wide': m('    if (ir_known_bits(f, s->m, KB_DEPTH) >> (x->W - x->c)) return false;\n', ''),
 'first_layout_only': m('  for (int k = 0; k < 2; k++) {\n    if ((k ? s1 : s0).kind < LX_ROT) continue;',
                        '  for (int k = 0; k < 1; k++) {\n    if ((k ? s1 : s0).kind < LX_ROT) continue;'),
 'any_layout': m('    if ((k ? s1 : s0).kind < LX_ROT) continue;\n', ''),
 'sides_kept': m('    x->s[0] = s0;\n    x->s[1] = s1;\n', ''),
 # the test
 'firsts_other': mi(FIRSTS, ''),
 'firsts_same_way': mi(FIRSTS, '  if (!(ma[0] == ea[0] && ma[1] == ea[1])) return false;\n'),
 'firsts_other_way': mi(FIRSTS, '  if (!(ma[0] == ea[1] && ma[1] == ea[0])) return false;\n'),
 'no_inverse': mi(INV, '  (void)negn;'),
 'inverse_as_strict': mi(INV, '  if (negn) { nl = !nl; }'),
 'inverse_same_way': mi(INV, '  if (negn) { ns = !ns; }'),
 'firsts_signed_always': mi(MKNOWN, ''),
 'firsts_one_known': mi(MKNOWN, MKNOWN.replace('(ir_known_bits(f, ma[0], KB_MAXD) | ir_known_bits(f, ma[1], KB_MAXD))', 'ir_known_bits(f, ma[0], KB_MAXD)')),
 'firsts_other_known': mi(MKNOWN, MKNOWN.replace('(ir_known_bits(f, ma[0], KB_MAXD) | ir_known_bits(f, ma[1], KB_MAXD))', 'ir_known_bits(f, ma[1], KB_MAXD)')),
 'seconds_signed': mi(NKNOWN, ''),
 'seconds_one_known': mi(NKNOWN, NKNOWN.replace('(ir_known_bits(f, na[0], KB_MAXD) | ir_known_bits(f, na[1], KB_MAXD))', 'ir_known_bits(f, na[0], KB_MAXD)')),
 'seconds_other_known': mi(NKNOWN, NKNOWN.replace('(ir_known_bits(f, na[0], KB_MAXD) | ir_known_bits(f, na[1], KB_MAXD))', 'ir_known_bits(f, na[1], KB_MAXD)')),
 'firsts_order': mi('  x->m[0] = ma[ml ? 0 : 1]; x->m[1] = ma[ml ? 1 : 0];', '  x->m[0] = ma[0]; x->m[1] = ma[1];'),
 'seconds_order': mi('  x->n[0] = na[nl ? 0 : 1]; x->n[1] = na[nl ? 1 : 0];', '  x->n[0] = na[0]; x->n[1] = na[1];'),
 'strict_as_firsts': mi('  x->strict = ns;', '  x->strict = ms;'),
 'always_unsigned': m(OP, '  x->op = p.strict ? I_LTU : I_LEU;'),
 'always_signed': m(OP, '  x->op = p.strict ? I_LTS : I_LES;'),
 # the keys
 'gen_cheap': m('  return s->kind == LX_GEN ? 2 : s->kind == LX_ROT ? 1 : 0;', '  return s->kind == LX_GEN ? 1 : s->kind == LX_ROT ? 1 : 0;'),
 'rot_free': m('  return s->kind == LX_GEN ? 2 : s->kind == LX_ROT ? 1 : 0;', '  return s->kind == LX_GEN ? 2 : 0;'),
 'rot_dear': m('  return s->kind == LX_GEN ? 2 : s->kind == LX_ROT ? 1 : 0;', '  return s->kind == LX_GEN ? 2 : s->kind == LX_ROT ? 2 : 0;'),
 'no_widen': m('  if (t == VT_I64 && (ValType)f->insts[v].type == VT_I32)\n    return emit_before(f, bi, idx, I_EXTENDU, VT_I64, &v, 1, 0);\n', ''),
 'rot_amount': m('I_CONST, t, NULL, 0, x->W - x->c) };', 'I_CONST, t, NULL, 0, x->c) };'),
 'rot_left': m('      return emit_before(f, bi, idx, I_ROTR, t, ra, 2, 0);', '      return emit_before(f, bi, idx, I_ROTL, t, ra, 2, 0);'),
 'const_order': m('(s->cm << x->c) | s->cn);', '(s->cn << x->c) | s->cm);'),
 'gen_shift': m('      sa[1] = emit_before(f, bi, idx, I_CONST, t, NULL, 0, x->c);', '      sa[1] = emit_before(f, bi, idx, I_CONST, t, NULL, 0, x->W - x->c);'),
 'gen_order': m('      sa[0] = lx_member(f, bi, idx, s->m, t);\n      oa[1] = lx_member(f, bi, idx, s->n, t);',
                '      sa[0] = lx_member(f, bi, idx, s->n, t);\n      oa[1] = lx_member(f, bi, idx, s->m, t);'),
 # the select
 'select_off': m('  if (!uses || !ir_lex_select(f, v, &c, &cm, &cn)) return false;', '  if (!uses || !ir_lex_select(f, v, &c, &cm, &cn) || 1) return false;'),
 'eq_only': mi('  if (cop != I_EQ && cop != I_NE) return false;\n  *eq = a[2];', '  if (cop != I_EQ) return false;\n  *eq = a[2];'),
 'arms_swapped': mi('  *cn = a[cop == I_EQ ? 0 : 1];\n  *cm = a[cop == I_EQ ? 1 : 0];', '  *cn = a[cop == I_EQ ? 1 : 0];\n  *cm = a[cop == I_EQ ? 0 : 1];'),
 'cond_read_twice': m(USES, USES.replace('uses[c] != 1 || ', '')),
 'second_read_twice': m(USES, USES.replace('uses[cn] != 1 || ', '')),
 'first_read_twice': m(USES, USES.replace(' || uses[cm] != 1', '')),
 'keys_swapped': m('  a[0] = k0;\n  a[1] = k1;\n  g_stats.lex_cmps++;', '  a[0] = k1;\n  a[1] = k0;\n  g_stats.lex_cmps++;'),
 'select_uncounted': m('  a[1] = k1;\n  g_stats.lex_cmps++;', '  a[1] = k1;'),
 # the two blocks
 'plain_any': m(PLAIN, PLAIN.replace('      default:\n        return false;', '      default:\n        break;')),
 'plain_no_const': m(PLAIN, PLAIN.replace('case I_CONST: ', '')),
 'plain_no_wrap': m(PLAIN, PLAIN.replace('case I_WRAP: ', '')),
 'plain_no_extend': m(PLAIN, PLAIN.replace('case I_EXTENDU: ', '')),
 'plain_no_shift': m(PLAIN, PLAIN.replace('case I_SHRU: ', '')),
 'plain_no_and': m(PLAIN, PLAIN.replace(' case I_AND:', '')),
 'plain_no_compare': m(PLAIN, PLAIN.replace('      case I_EQ: case I_NE: case I_LTS: case I_LTU: case I_GTS: case I_GTU:\n      case I_LES: case I_LEU: case I_GES: case I_GEU:\n', '')),
 'any_target': m('  if (p->target != q->target) return false;\n', ''),
 'any_values': m(EDGE, '    (void)x; (void)y;'),
 'any_constants': m(EDGE, EDGE.replace(' || x != y', '')),
 'same_values_only': m(EDGE, EDGE.replace('    if (!is_const(f, pa, &x) || !is_const(f, qa, &y) || x != y) return false;', '    (void)x; (void)y;\n    return false;')),
 'values_as_blocks_have': m(HANDED, '    Ref pa = p->args[j], qa = q->args[j];'),
 # what a block's other way in hands it
 'parameter_itself': m("  if (in->op != I_PARAM || (in->block != b0 && in->block != b1)) return v;", "  if (1) return v;"),
 'parameter_first_edge': m("  const Edge *e = &b->succs[b->succs[0].target == in->block ? 0 : 1];", "  const Edge *e = &b->succs[0];"),
 'parameter_first': m("    if (t->params[k] == v) return e->args[k];", "    if (t->params[k] == v) return e->args[0];"),
 'bring_as_made': m("  v = lx_handed(f, bi, v, b0, b1);\n  Inst in = f->insts[v];", "  Inst in = f->insts[v];"),
 'outside_as_made': m("  v = lx_handed(f, bi, v, b0, b1);\n  const Inst *in = &f->insts[v];", "  const Inst *in = &f->insts[v];"),
 'not_brought': m(BRING, '  if (1) return v;'),
 'firsts_block_not_brought': m(BRING, '  if (in.block != b0) return v;'),
 'seconds_block_not_brought': m(BRING, '  if (in.block != b1) return v;'),
 'const_inside': m('  if (in->op == I_CONST) return true;\n  if (in->block != b0 && in->block != b1)', '  if (in->block != b0 && in->block != b1)'),
 'same_loop_outside': m('    return f->blocks[in->block].loop_depth < f->blocks[bi].loop_depth;', '    return f->blocks[in->block].loop_depth <= f->blocks[bi].loop_depth;'),
 'made_there_outside': m('    if (!lx_outside(f, ir_args(f, v)[k], bi, b0, b1)) return false;\n', '    {}\n'),
 # the branch
 'br_eq_only': m('  if (cop != I_EQ && cop != I_NE) return false;\n  uint32_t se', '  if (cop != I_EQ) return false;\n  uint32_t se'),
 'br_arms_swapped': m('  uint32_t se = b->succs[cop == I_EQ ? 0 : 1].target, sn = b->succs[cop == I_EQ ? 1 : 0].target;',
                      '  uint32_t se = b->succs[cop == I_EQ ? 1 : 0].target, sn = b->succs[cop == I_EQ ? 0 : 1].target;'),
 'seconds_not_plain': m(BLOCKS, BLOCKS.replace(' || !lx_plain(f, E)', '')),
 'firsts_not_plain': m(BLOCKS, BLOCKS.replace(' || !lx_plain(f, N)', '')),
 'other_places': m(WAYS, ''),
 'same_way_round': m('  if (!lx_match(f, c, N->term_arg, E->term_arg, !same, &x)) return false;', '  if (!lx_match(f, c, N->term_arg, E->term_arg, false, &x)) return false;'),
 'other_way_round': m('  if (!lx_match(f, c, N->term_arg, E->term_arg, !same, &x)) return false;', '  if (!lx_match(f, c, N->term_arg, E->term_arg, true, &x)) return false;'),
 'rot_always_counts': m(COST, COST.replace('s->kind >= LX_ROT ? !lx_outside(f, s->src, bi, se, sn)', 's->kind >= LX_ROT ? true')),
 'gen_both_members': m(COST, COST.replace('(!lx_outside(f, s->m, bi, se, sn) || !lx_outside(f, s->n, bi, se, sn))', '(!lx_outside(f, s->m, bi, se, sn) && !lx_outside(f, s->n, bi, se, sn))')),
 'gen_free': m(COST, COST.replace(': s->kind == LX_GEN && (!lx_outside(f, s->m, bi, se, sn) || !lx_outside(f, s->n, bi, se, sn))', ': false')),
 'cost_3': m('  if (cost > 2 || warmc_getenv("WARMC_NO_LEX_CMP")) return false;', '  if (cost > 3 || warmc_getenv("WARMC_NO_LEX_CMP")) return false;'),
 'cost_1': m('  if (cost > 2 || warmc_getenv("WARMC_NO_LEX_CMP")) return false;', '  if (cost > 1 || warmc_getenv("WARMC_NO_LEX_CMP")) return false;'),
 'members_not_brought': m('    s->m = lx_bring(f, bi, &at, s->m, se, sn);\n    s->n = lx_bring(f, bi, &at, s->n, se, sn);\n', ''),
 'br_keys_swapped': m('  ka[0] = lx_key(f, bi, &at, &x, &x.s[0]);\n  ka[1] = lx_key(f, bi, &at, &x, &x.s[1]);',
                      '  ka[1] = lx_key(f, bi, &at, &x, &x.s[0]);\n  ka[0] = lx_key(f, bi, &at, &x, &x.s[1]);'),
 'seconds_ways': m('    Edge old = f->blocks[sn].succs[e];', '    Edge old = f->blocks[se].succs[e];'),
 'values_not_brought': m('      na[j] = lx_bring(f, bi, &at, old.args[j], se, sn);', '      na[j] = old.args[j];'),
 'hint': m('    ne->cold = old.cold;\n', ''),
 'branch_kept': m('  f->blocks[bi].term_arg = k;\n  g_stats.lex_brs++;', '  (void)k;\n  g_stats.lex_brs++;'),
 'branch_uncounted': m('  f->blocks[bi].term_arg = k;\n  g_stats.lex_brs++;', '  f->blocks[bi].term_arg = k;'),
 'branch_off': m('    if (lx_branch(f, f->order[oi])) changed = true;', '    if (0 && lx_branch(f, f->order[oi])) changed = true;'),
}
EQUIVALENT = {
 'signed_anywhere': 'a first member that may be negative fits no place narrower than its type, which the tests of what fits come to: known bits say of a value all that the bits it is made of do',
 'firsts_block_not_brought': 'of the firsts\' block only a constant it hands on is read, and a constant an edge hands on is made on the edge wherever it is listed',
 'any_layout': 'a side that is no one value\'s bits has no layout, a key of no bits: nothing fits beside it but members known to be 0, which are constants the fold has made of the whole test before',
 'hint': 'only where the code is put: no case has a branch hint',
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py', 'lex_cmp'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}))
