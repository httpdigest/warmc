"""A 64-bit multiply of two 32-bit values extended alike, umull or smull,
and a sum with one, umaddl (isel.c's rule ahead of the folds into adds;
isel_ops.inc's I_MUL and its add of a folded multiply; a64.c a64_maddl).
Killed by test/cases_opt/wide_mul.wat's answers, its count and its shapes.
The two questions it asks of an operand are ir_known_bits's and
ir_sign_ge's (test/mutants/sign_bits.py).

Not mutated: WARMC_NO_WIDE_MUL, which no case sets."""
N, O, A = 'src/isel.c', 'src/isel_ops.inc', 'src/a64.c'
def m(old, new, path=N): return (path, [(old, new)])
DROP = ('        drop[slot] = (xi->op == I_EXTENDU || xi->op == I_EXTENDS || xi->op == I_EXT32S) &&\n'
        '                     uses[a[slot]] == (a[0] == a[1] ? 2u : 1u);')
def drop(ops='(xi->op == I_EXTENDU || xi->op == I_EXTENDS || xi->op == I_EXT32S)',
         uses='uses[a[slot]] == (a[0] == a[1] ? 2u : 1u)', blk=''):
    return m(DROP, '        drop[slot] = %s &&\n                     %s%s;' % (ops, uses, blk))
NONE = '      if (!drop[0] && !drop[1]) continue;'
ZX = '        if (ir_known_bits(f, x, 4) > 0xffffffffull) kind &= ~1u;'
SX = '        if (!ir_sign_ge(f, x, 33)) kind &= ~2u;'
MARK = '        if (drop[slot]) s->folded[a[slot]] = 1;'
KIND = '      s->wmul[u] = kind & 1 ? 1 : 2;'
MUL = '        if (s->wmul[v] == 2) a64_smull(B, d, x, y); else a64_umull(B, d, x, y);'
MADD = '          a64_maddl(B, s->wmul[a[1]] == 2, in->op == I_SUB, d, y, z, x);'
SRC = '  return s->folded[x] ? ir_args(s->f, x)[0] : x;'
ENC = '  dp3(b, 1, sign ? 1 : 5, rd, rn, rm, sub ? 1 : 0, ra);'
M = {
 'off': m('      if (!kind) continue;\n      for (int slot = 0; slot < 2; slot++)\n' + MARK,
          '      continue;\n      for (int slot = 0; slot < 2; slot++)\n' + MARK),
 # which extensions go
 'no_ext32': drop(ops='(xi->op == I_EXTENDU || xi->op == I_EXTENDS)'),
 'no_extends': drop(ops='(xi->op == I_EXTENDU || xi->op == I_EXT32S)'),
 'no_extendu': drop(ops='(xi->op == I_EXTENDS || xi->op == I_EXT32S)'),
 'ext16_too': drop(ops='(xi->op == I_EXTENDU || xi->op == I_EXTENDS || xi->op == I_EXT32S || xi->op == I_EXT16S)'),
 'read_twice': drop(uses='uses[a[slot]] >= 1u'),
 'no_square': drop(uses='uses[a[slot]] == 1u'),
 'square_thrice': drop(uses='(a[0] == a[1] ? uses[a[slot]] >= 2u : uses[a[slot]] == 1u)'),
 'two_for_one': drop(uses='(uses[a[slot]] == 2u || uses[a[slot]] == 1u)'),
 'same_block': drop(blk=' && xi->block == bi'),
 'first_only': m(MARK, '        if (drop[slot] && slot == 0) s->folded[a[slot]] = 1;'),
 'ext_stays': m(MARK, '        ;'),
 'all_marked': m(MARK, '        if (f->insts[a[slot]].op != I_CONST) s->folded[a[slot]] = 1;'),
 # which multiplies
 'nothing_to_drop': m(NONE, ''),
 'both_to_drop': m(NONE, '      if (!drop[0] || !drop[1]) continue;'),
 'zx_any': m(ZX, ''),
 'zx_33': m(ZX, '        if (ir_known_bits(f, x, 4) > 0x1ffffffffull) kind &= ~1u;'),
 'zx_31': m(ZX, '        if (ir_known_bits(f, x, 4) > 0x7fffffffull) kind &= ~1u;'),
 'sx_any': m(SX, ''),
 'sx_32': m(SX, '        if (!ir_sign_ge(f, x, 32)) kind &= ~2u;'),
 'sx_34': m(SX, '        if (!ir_sign_ge(f, x, 34)) kind &= ~2u;'),
 'zx_first': m(ZX, '        if (slot == 0 && ir_known_bits(f, x, 4) > 0xffffffffull) kind &= ~1u;'),
 'zx_second': m(ZX, '        if (slot == 1 && ir_known_bits(f, x, 4) > 0xffffffffull) kind &= ~1u;'),
 'sx_first': m(SX, '        if (slot == 0 && !ir_sign_ge(f, x, 33)) kind &= ~2u;'),
 'sx_second': m(SX, '        if (slot == 1 && !ir_sign_ge(f, x, 33)) kind &= ~2u;'),
 'kind_signed_first': m(KIND, '      s->wmul[u] = kind & 2 ? 2 : 1;'),
 'kind_one': m(KIND, '      s->wmul[u] = 1;'),
 'kind_two': m(KIND, '      s->wmul[u] = 2;'),
 'count': m(KIND + '\n      g_stats.mull_folds++;', KIND),
 # what is emitted
 'mul_swapped': m(MUL, '        if (s->wmul[v] != 2) a64_smull(B, d, x, y); else a64_umull(B, d, x, y);', O),
 'mul_plain': m(MUL, '        a64_mul(B, 1, d, x, y);', O),
 'madd_sign': m(MADD, '          a64_maddl(B, s->wmul[a[1]] != 2, in->op == I_SUB, d, y, z, x);', O),
 'madd_unsigned': m(MADD, '          a64_maddl(B, false, in->op == I_SUB, d, y, z, x);', O),
 'madd_adds': m(MADD, '          a64_maddl(B, s->wmul[a[1]] == 2, false, d, y, z, x);', O),
 'madd_subs': m(MADD, '          a64_maddl(B, s->wmul[a[1]] == 2, in->op != I_SUB, d, y, z, x);', O),
 'madd_plain': m(MADD, '          if (in->op == I_ADD) a64_madd(B, sf, d, y, z, x); else a64_msub(B, sf, d, y, z, x);', O),
 'madd_square': m('          int y = rd(s, wmul_src(s, ca[0]), S1), z = rd(s, wmul_src(s, ca[1]), S3);',
                  '          int y = rd(s, wmul_src(s, ca[0]), S1), z = rd(s, wmul_src(s, ca[0]), S3);', O),
 'mul_square': m('        int x = rd(s, wmul_src(s, a[0]), S0), y = rd(s, wmul_src(s, a[1]), S1);',
                 '        int x = rd(s, wmul_src(s, a[0]), S0), y = rd(s, wmul_src(s, a[0]), S1);', O),
 'src_itself': m(SRC, '  return x;'),
 'src_operand': m(SRC, '  return s->f->insts[x].op == I_CONST ? x : ir_args(s->f, x)[0];'),
 # the encoding
 'enc_sign': m(ENC, '  dp3(b, 1, sign ? 5 : 1, rd, rn, rm, sub ? 1 : 0, ra);', A),
 'enc_sub': m(ENC, '  dp3(b, 1, sign ? 1 : 5, rd, rn, rm, sub ? 0 : 1, ra);', A),
 'enc_add_only': m(ENC, '  dp3(b, 1, sign ? 1 : 5, rd, rn, rm, 0, ra);', A),
 'enc_no_acc': m(ENC, '  dp3(b, 1, sign ? 1 : 5, rd, rn, rm, sub ? 1 : 0, RZR);', A),
 'enc_operands': m(ENC, '  dp3(b, 1, sign ? 1 : 5, rd, rn, rn, sub ? 1 : 0, ra);', A),
 # no fold ahead of this one takes a multiply or one's operand
 'mul_folded': m('      if (in->op != I_MUL || s->folded[u]) continue;\n      Ref *a = ir_args(f, u);\n      /* The extensions to drop.',
                 '      if (in->op != I_MUL) continue;\n      Ref *a = ir_args(f, u);\n      /* The extensions to drop.'),
 'operand_folded': m('        if (s->folded[x]) kind = 0;\n', ''),
}
EQUIVALENT = {
 'mul_folded': 'no fold ahead of this one takes a multiply',
 'operand_folded': 'no fold ahead of this one takes a value a multiply reads',
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'] + ['wide_mul'], {}),)
