"""How many of a value's top bits are copies of its sign (ir.c ir_sign_ge),
and the fold that drops a sign extension of a value that is one already
(opt.c's I_EXT8S, I_EXT16S and I_EXT32S).  Killed by
test/cases_opt/sign_bits.wat's answers and shapes, with sext_fold.wat's and
wide_mul.wat's."""
I, O = 'src/ir.c', 'src/opt.c'
def m(old, new, path=I): return (path, [(old, new)])
CONST = '    return (c >> (w - n)) == 0;\n  }\n  /* zeros'
KB = '  if ((ir_known_bits(f, x, KB_MAXD) >> (w - n)) == 0) return true;'
LOAD = '      return (in->imm2 & MEMF_SIGNED) && n <= w - bits + 1;'
FROM = '  if (from && n <= w - from + 1) return true;'
SHRS = '      return n <= k || SG(a[0], n - k);'
SHL = '      return SG(a[0], n + (uint32_t)(f->insts[a[1]].imm % w));'
BITS = '      return in->nargs == 2 && SG(a[0], n) && SG(a[1], n);'
SEL = '      return in->nargs == 3 && SG(a[0], n) && SG(a[1], n);'
ADD = '      return in->nargs == 2 && SG(a[0], n + 1) && SG(a[1], n + 1);'
MUL = '      return SG(a[0], each) && SG(a[1], each);'
ASK = '        if (asked[i].x == x && asked[i].n >= n) return true;'
EDGE = '          if (e->nargs != b->nparams ||\n              !sign_ge(f, e->args[q], n, depth - 1, asked, nasked + 1))\n            return false;'
FOLD = '        if (ir_sign_ge(f, a[0], (in->type == VT_I64 ? 64u : 32u) - from + 1)) {'
M = {
 # the ends
 'one_more': m('  if (n <= 1) return true;\n  if (n > w) return false;', '  if (n <= 2) return true;\n  if (n > w) return false;'),
 'const_any': m(CONST, '    return true;\n  }\n  /* zeros'),
 'const_less': m(CONST, '    return (c >> (w - n + 1)) == 0;\n  }\n  /* zeros'),
 'const_more': m(CONST, '    return (c >> (w - n - 1)) == 0;\n  }\n  /* zeros'),
 'const_unsigned': m('    if ((int64_t)c < 0) c = ~c;\n', ''),
 'const_i32_wide': m('    uint64_t c = w == 64 ? in->imm : (uint64_t)(int64_t)(int32_t)(uint32_t)in->imm;', '    uint64_t c = in->imm;'),
 'kb_off': m(KB, ''),
 'kb_less': m(KB, '  if ((ir_known_bits(f, x, KB_MAXD) >> (w - n + 1)) == 0) return true;'),
 'kb_more': m(KB, '  if ((ir_known_bits(f, x, KB_MAXD) >> (w - n - 1)) == 0) return true;'),
 'width_64': m('  uint32_t w = in->type == VT_I64 ? 64 : 32;\n  if (n <= 1)', '  uint32_t w = 64;\n  if (n <= 1)'),
 # extensions and loads
 'extends_34': m('    case I_EXTENDS: if (n <= 33) return true; break;', '    case I_EXTENDS: if (n <= 34) return true; break;'),
 'extends_32': m('    case I_EXTENDS: if (n <= 33) return true; break;', '    case I_EXTENDS: if (n <= 32) return true; break;'),
 'extends_own': m('    case I_EXTENDS: return in->nargs == 1 && SG(a[0], n - 32);', '    case I_EXTENDS: return false;'),
 'extends_own_more': m('    case I_EXTENDS: return in->nargs == 1 && SG(a[0], n - 32);', '    case I_EXTENDS: return in->nargs == 1 && SG(a[0], n - 33);'),
 'ext8_as16': m('    case I_EXT8S: from = 8; break;', '    case I_EXT8S: from = 16; break;'),
 'ext16_as8': m('    case I_EXT16S: from = 16; break;', '    case I_EXT16S: from = 8; break;'),
 'ext16_as32': m('    case I_EXT16S: from = 16; break;', '    case I_EXT16S: from = 32; break;'),
 'ext32_as16': m('    case I_EXT32S: from = 32; break;', '    case I_EXT32S: from = 16; break;'),
 'from_more': m(FROM, '  if (from && n <= w - from + 2) return true;'),
 'from_less': m(FROM, '  if (from && n <= w - from) return true;'),
 'load_unsigned': m(LOAD, '      return n <= w - bits + 1;'),
 'load_more': m(LOAD, '      return (in->imm2 & MEMF_SIGNED) && n <= w - bits + 2;'),
 'load_less': m(LOAD, '      return (in->imm2 & MEMF_SIGNED) && n <= w - bits;'),
 'load_off': m(LOAD, '      return false;'),
 # the walk
 'wrap_same': m('    case I_WRAP: return in->nargs == 1 && SG(a[0], n + 32);', '    case I_WRAP: return in->nargs == 1 && SG(a[0], n + 31);'),
 'wrap_off': m('    case I_WRAP: return in->nargs == 1 && SG(a[0], n + 32);', '    case I_WRAP: return false;'),
 'shrs_more': m(SHRS, '      return n <= k + 2 || SG(a[0], n - k);'),
 'shrs_alone': m(SHRS, '      return n <= k;'),
 'shrs_rec_more': m(SHRS, '      return n <= k + 1 || SG(a[0], n - k - 1);'),
 'shrs_rec_less': m(SHRS, '      return n <= k || SG(a[0], n - k + 1);'),
 'shl_none': m(SHL, '      return SG(a[0], n);'),
 'shl_less': m(SHL, '      return SG(a[0], n + (uint32_t)(f->insts[a[1]].imm % w) - 1);'),
 'shl_off': m(SHL, '      return false;'),
 'bits_first': m(BITS, '      return in->nargs == 2 && SG(a[0], n);'),
 'bits_second': m(BITS, '      return in->nargs == 2 && SG(a[1], n);'),
 'sel_first': m(SEL, '      return in->nargs == 3 && SG(a[0], n);'),
 'sel_second': m(SEL, '      return in->nargs == 3 && SG(a[1], n);'),
 'sel_cond': m(SEL, '      return in->nargs == 3 && SG(a[0], n) && SG(a[1], n) && SG(a[2], n);'),
 'add_same': m(ADD, '      return in->nargs == 2 && SG(a[0], n) && SG(a[1], n);'),
 'add_first_same': m(ADD, '      return in->nargs == 2 && SG(a[0], n) && SG(a[1], n + 1);'),
 'add_second_same': m(ADD, '      return in->nargs == 2 && SG(a[0], n + 1) && SG(a[1], n);'),
 'add_first': m(ADD, '      return in->nargs == 2 && SG(a[0], n + 1);'),
 'add_second': m(ADD, '      return in->nargs == 2 && SG(a[1], n + 1);'),
 'add_off': m(ADD, '      return false;'),
 'mul_less': m('      uint32_t each = (n + w + 2) / 2;', '      uint32_t each = (n + w) / 2;'),
 'mul_first': m(MUL, '      return SG(a[0], each);'),
 'mul_second': m(MUL, '      return SG(a[1], each);'),
 'mul_off': m(MUL, '      return false;'),
 # a block's parameter
 'ask_any_n': m(ASK, '        if (asked[i].x == x) return true;'),
 'ask_never': m(ASK, '        if (0) return true;'),
 'edge_any': m(EDGE, '          if (e->nargs != b->nparams)\n            return false;'),
 'param_off': m('    case I_PARAM: {\n      for (uint32_t i = 0; i < nasked; i++)\n        if (asked[i].x == x', '    case I_NOP: {\n      for (uint32_t i = 0; i < nasked; i++)\n        if (asked[i].x == x'),
 # the fold
 'fold_off': m(FOLD, '        if (0) {', O),
 'fold_one_less': m(FOLD, '        if (ir_sign_ge(f, a[0], (in->type == VT_I64 ? 64u : 32u) - from)) {', O),
 'fold_one_more': m(FOLD, '        if (ir_sign_ge(f, a[0], (in->type == VT_I64 ? 64u : 32u) - from + 2)) {', O),
 'fold_width': m(FOLD, '        if (ir_sign_ge(f, a[0], 64u - from + 1)) {', O),
 'fold_from8': m('        uint32_t from = in->op == I_EXT8S ? 8u : in->op == I_EXT16S ? 16u : 32u;', '        uint32_t from = in->op == I_EXT8S ? 16u : in->op == I_EXT16S ? 16u : 32u;', O),
 'fold_from16': m('        uint32_t from = in->op == I_EXT8S ? 8u : in->op == I_EXT16S ? 16u : 32u;', '        uint32_t from = in->op == I_EXT8S ? 8u : in->op == I_EXT16S ? 32u : 32u;', O),
 'fold_from32': m('        uint32_t from = in->op == I_EXT8S ? 8u : in->op == I_EXT16S ? 16u : 32u;', '        uint32_t from = in->op == I_EXT8S ? 8u : in->op == I_EXT16S ? 16u : 16u;', O),
 'rewiden_32': m('              ir_sign_ge(f, ir_args(f, a[0])[0], 33)) {', '              ir_sign_ge(f, ir_args(f, a[0])[0], 32)) {', O),
 'rewiden_34': m('              ir_sign_ge(f, ir_args(f, a[0])[0], 33)) {', '              ir_sign_ge(f, ir_args(f, a[0])[0], 34)) {', O),
 'depth_less': m('#define SG_DEPTH 8', '#define SG_DEPTH 7'),
 'depth_more': m('#define SG_DEPTH 8', '#define SG_DEPTH 9'),
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'] + ['sign_bits', 'sext_fold', 'wide_mul'], {}),)
