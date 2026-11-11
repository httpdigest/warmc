"""A lane narrower than its values' type: its compares, min and max and
sign extensions (opt.c v_mask, v_build), and saturating arithmetic on bytes
and halfwords as LLVM writes it for wasm, vectorised in the elements' own
lanes: opt.c's lane fits by ir_known_bits
and ir_sign_ge, the saturating forms (v_sat_form), the saturating shift
(v_shlsat) and product (v_top_lane, v_mulsat), the range tested before the
loop (v_guard), a narrow lane's extensions, wraps and shifts down, and the
copies held to the loads in flight; isel_ops.inc's narrowing into the
result's own register.  Killed by test/cases_opt/vec_sat.wat's answers,
its count and its shapes, vec_sat_nofold.wat's and vec_narrow.wat's.

Not mutated: a64.c's uqshl, which test/a64_test.c has the encodings of."""
O, I = 'src/opt.c', 'src/isel_ops.inc'
def m(old, new, path=O): return (path, [(old, new)])
KB = ('  if (((ValType)in->type == VT_I32 || (ValType)in->type == VT_I64) &&\n'
      '      ir_known_bits(f, v, KB_DEPTH) < (1ull << bits))\n    return true;')
USAT = '    if (so == I_VADDSATU || so == I_VSUBSATU) return true;'
SG = '  if (ir_sign_ge(f, v, (t == VT_I64 ? 64u : 32u) - bits + 1)) return true;'
SSAT = '  return so == I_VADDSATS || so == I_VSUBSATS;'
WRAPPED = ('      else if (ca[0] == o && ca[1] == oa[0] && (ci->op == I_GTU || ci->op == I_LEU))\n'
           '        r = ci->op == I_GTU ? I_LTU : I_GEU;\n'
           '      else if (ca[1] == o && ca[0] == oa[0] && (ci->op == I_LTU || ci->op == I_GEU))\n'
           '        r = (IrOp)ci->op;')
SHIFT = ('           ((ij->op == I_SHRU && op0 == I_SHRU && v_fits_lane(f, ir_args(f, tuple[j])[0], c->lane)) ||\n'
         '            (ij->op == I_SHRS && op0 == I_SHRS && v_fits_lane_signed(f, ir_args(f, tuple[j])[0], c->lane)));')
THRU = '  if (narrow && (op0 == I_EXTENDS || op0 == I_EXTENDU || op0 == I_WRAP) && nargs0 == 1) {'
# v_shlsat
S_DN = '    if (!is_const(f, ja[1], &dn) || dn != w - bits) return REF_NONE;'
S_LOST = '    uint32_t lost = ci->op == I_NE ? 0 : 1;'
S_ONES = ('    if (!is_const(f, sa[lost], &ones) ||\n'
          '        ((w == 64 ? ones : ones & 0xffffffffu) >> (w - bits)) != (1ull << bits) - 1)\n      return REF_NONE;')
S_BACK = ('    if (bi->op != I_SHRU || bi->nargs != 2 || ir_args(f, back)[0] != sv || ir_args(f, back)[1] != n)\n'
          '      return REF_NONE;')
S_UP = ('    if (ti->op != I_SHL || ti->nargs != 2 || !is_const(f, ir_args(f, v)[1], &up) || up != w - bits)\n'
        '      return REF_NONE;')
S_LOOP = '  if (f->insts[amt].op != I_CONST && c->inloop[f->insts[amt].block]) return REF_NONE;'
S_AMT = ('        : vemit(f, c->vguard, I_AND, at, amt, ir_emit(f, c->vguard, I_CONST, at, NULL, 0, w - 1, 0), 0, 0);')
# v_top_lane
T_SHL = '  if (ai->op == I_SHL && ai->nargs == 2 && is_const(f, ir_args(f, a)[1], &cv) && (cv & 31) == up)'
T_MASK = '    if (!is_const(f, aa[s], &mask) || (mask & 0xffffffffu) != ((0xffffffffu << up) & 0xffffffffu)) continue;'
T_SH = '    if (sh >= up) continue;'
T_FIT = '    if (!v_fits_lane_signed(f, x, lane)) continue;'
# v_mulsat
M_UP = "        !is_const(f, ir_args(f, tuple[j])[1], &cv) || (cv & 31) != up)\n      return REF_NONE;\n    Ref sel"
M_OVER = '    uint32_t over = ci->op == I_NE ? 0 : 1;'
M_SIGN31 = ("      if (xi->op == I_SHRS && xi->nargs == 2 && ir_args(f, ca[s])[0] == low &&\n"
            "          is_const(f, ir_args(f, ca[s])[1], &cv) && (cv & 31) == 31)")
M_HALF = ("      if ((hi2->op != I_SHRU && hi2->op != I_SHRS) || hi2->nargs != 2 || ir_args(f, hs)[0] != p ||\n"
          "          !is_const(f, ir_args(f, hs)[1], &cv) || (cv & 63) != 32)")
M_BOUND = '      if (is_const(f, ta[s], &cv) && ((int64_t)(int32_t)(uint32_t)cv >> up) == hi) sg = ta[s ^ 1];'
M_SG1 = ("    if (gi->op == I_SHRS && gi->nargs == 2 && ir_args(f, sg)[0] == high &&\n"
         "        is_const(f, ir_args(f, sg)[1], &cv) && (cv & 31) == 31) {")
M_SG2 = ("      sign = wi->op == I_SHRS && wi->nargs == 2 && ir_args(f, ws)[0] == p &&\n"
         "             is_const(f, ir_args(f, ws)[1], &cv) && (cv & 63) == 63;")
M_RANGE = '      if ((int64_t)mc < lo || (int64_t)mc > hi || (j && mc != mk)) return REF_NONE;'
M_VAR = '      if (!v_fits_lane_signed(f, m, c->lane)) return REF_NONE;'
M_DOWN = '  if (down) x = vemit(f, c->vbody, I_VSHRS, VT_V128, x, REF_NONE, down, c->lane);'
M_GUARD = '    v_guard(c, vemit(f, c->vguard, I_LTU, VT_I32, sum, span, 0, 0));'
M_LO = '  Ref pl = vemit(f, c->vbody, I_VEXTMUL, VT_V128, x, yv, 1, c->lane + 1);'
M_HI = '  Ref ph = vemit(f, c->vbody, I_VEXTMUL, VT_V128, x, yv, 3, c->lane + 1);'
M_NARROW = '  return vemit(f, c->vbody, I_VNARROW, VT_V128, pl, ph, 1, c->lane);'
GOK = '  c->gok = c->gok == REF_NONE ? cond : vemit(c->f, c->vguard, I_AND, VT_I32, c->gok, cond, 0, 0);'
AND = '  if (ctx.gok != REF_NONE) ok = vemit(f, vguard, I_AND, VT_I32, ok, ctx.gok, 0, 0);'
CAP = '    while (R > 1 && nld * R > 4 * ir_vec_copies()) R >>= 1;'
NARROW = '      int t = d == y ? FTMP : d;'
CMP_US = '        us = us && v_fits_lane(f, aj[0], ml) && v_fits_lane(f, aj[1], ml);'
CMP_SG = '        sg = sg && v_fits_lane_signed(f, aj[0], ml) && v_fits_lane_signed(f, aj[1], ml);'
CMP_MAP = ('        if (vop == I_GTS) vop = I_GTU;\n'
           '        else if (vop == I_GES) vop = I_GEU;')
MM_US = '        us = us && v_fits_lane(f, aj[0], c->lane) && v_fits_lane(f, aj[1], c->lane);'
MM_SG = '        sg = sg && v_fits_lane_signed(f, aj[0], c->lane) && v_fits_lane_signed(f, aj[1], c->lane);'
MM_MAP = '        if (us) vop = vop == I_VSMIN ? I_VUMIN : vop == I_VSMAX ? I_VUMAX : vop;'
EXT_FROM = '    uint32_t from = op0 == I_EXT8S ? 8u : op0 == I_EXT16S ? 16u : 32u, lb = c->width * 8u;'
EXT_UP = '      out = vemit(f, c->vbody, I_VSHL, VT_V128, out, REF_NONE, lb - from, c->lane);'
EXT_DN = '      out = vemit(f, c->vbody, I_VSHRS, VT_V128, out, REF_NONE, lb - from, c->lane);'
M = {
 # a narrow lane's compares, min and max, and sign extensions (vec_narrow.wat)
 'cmp_any': m('        if (!us) return REF_NONE;\n        if (vop == I_GTS) vop = I_GTU;', '        if (vop == I_GTS) vop = I_GTU;'),
 'cmp_no_unsigned': m(CMP_US, '        us = false;'),
 'cmp_no_signed': m(CMP_SG, '        sg = false;'),
 'cmp_us_first': m(CMP_US, '        us = us && v_fits_lane(f, aj[0], ml);'),
 'cmp_us_second': m(CMP_US, '        us = us && v_fits_lane(f, aj[1], ml);'),
 'cmp_sg_first': m(CMP_SG, '        sg = sg && v_fits_lane_signed(f, aj[0], ml);'),
 'cmp_sg_second': m(CMP_SG, '        sg = sg && v_fits_lane_signed(f, aj[1], ml);'),
 'cmp_gts_stays': m(CMP_MAP, '        if (vop == I_GES) vop = I_GEU;'),
 'cmp_ges_stays': m(CMP_MAP, '        if (vop == I_GTS) vop = I_GTU;'),
 'cmp_map_signed': m('      if (!sg) {\n        if (!us) return REF_NONE;', '      if (!sg || us) {\n        if (!us) return REF_NONE;'),
 'mm_narrow_off': m('    vop = (flt || c->width > 4) ? I_NOP : v_minmax_of(f, tuple[0]);', '    vop = (flt || narrow || c->width > 4) ? I_NOP : v_minmax_of(f, tuple[0]);'),
 'mm_any': m('        else vop = I_NOP;\n      }\n    }\n    if (vop != I_NOP) {', '      }\n    }\n    if (vop != I_NOP) {'),
 'mm_no_unsigned': m(MM_US, '        us = false;'),
 'mm_no_signed': m(MM_SG, '        sg = false;'),
 'mm_us_first': m(MM_US, '        us = us && v_fits_lane(f, aj[0], c->lane);'),
 'mm_us_second': m(MM_US, '        us = us && v_fits_lane(f, aj[1], c->lane);'),
 'mm_sg_first': m(MM_SG, '        sg = sg && v_fits_lane_signed(f, aj[0], c->lane);'),
 'mm_sg_second': m(MM_SG, '        sg = sg && v_fits_lane_signed(f, aj[1], c->lane);'),
 'mm_signed_stays': m(MM_MAP, '        if (us) vop = vop;'),
 'mm_min_stays': m(MM_MAP, '        if (us) vop = vop == I_VSMAX ? I_VUMAX : vop;'),
 'mm_max_stays': m(MM_MAP, '        if (us) vop = vop == I_VSMIN ? I_VUMIN : vop;'),
 'ext_off': m('      case I_EXT8S: case I_EXT16S: case I_EXT32S:\n      case I_NOP: case I_PARAM:', '      case I_NOP: case I_PARAM:'),
 'ext8_as16': m(EXT_FROM, EXT_FROM.replace('op0 == I_EXT8S ? 8u', 'op0 == I_EXT8S ? 16u')),
 'ext16_as8': m(EXT_FROM, EXT_FROM.replace('op0 == I_EXT16S ? 16u', 'op0 == I_EXT16S ? 8u')),
 'ext32_as8': m(EXT_FROM, EXT_FROM.replace(': 32u, lb', ': 8u, lb')),
 'ext_bits_width': m(EXT_FROM, EXT_FROM.replace('lb = c->width * 8u;', 'lb = c->width * 4u;')),
 'ext_always': m('    if (from < lb) {\n' + EXT_UP, '    if (from <= lb) {\n' + EXT_UP),
 'ext_never': m('    if (from < lb) {\n' + EXT_UP, '    if (0) {\n' + EXT_UP),
 'ext_up_only': m(EXT_DN, ''),
 'ext_down_only': m(EXT_UP, ''),
 'ext_unsigned': m(EXT_DN, EXT_DN.replace('I_VSHRS', 'I_VSHRU')),
 # what a lane holds
 'kb_off': m(KB, ''),
 'kb_one_more': m(KB, KB.replace('< (1ull << bits))', '<= (1ull << bits))')),
 'usat_off': m(USAT, ''),
 'usat_add_only': m(USAT, '    if (so == I_VADDSATU) return true;'),
 'usat_sub_only': m(USAT, '    if (so == I_VSUBSATU) return true;'),
 'sg_one_less': m(SG, '  if (ir_sign_ge(f, v, (t == VT_I64 ? 64u : 32u) - bits)) return true;'),
 'sg_one_more': m(SG, '  if (ir_sign_ge(f, v, (t == VT_I64 ? 64u : 32u) - bits + 2)) return true;'),
 'sg_off': m(SG, ''),
 'ssat_off': m(SSAT, '  return false;'),
 'ssat_add_only': m(SSAT, '  return so == I_VADDSATS;'),
 'ssat_sub_only': m(SSAT, '  return so == I_VSUBSATS;'),
 # a difference that did not wrap
 'wrapped_off': m(WRAPPED, ''),
 'wrapped_ge': m(WRAPPED, WRAPPED.replace('(ci->op == I_GTU || ci->op == I_LEU))', '(ci->op == I_GTU || ci->op == I_GEU || ci->op == I_LEU))')),
 'wrapped_swapped': m(WRAPPED, WRAPPED.replace('r = ci->op == I_GTU ? I_LTU : I_GEU;', 'r = ci->op == I_GTU ? I_GEU : I_LTU;')),
 'wrapped_mirror': m(WRAPPED, WRAPPED.replace('        r = (IrOp)ci->op;', '        r = v_mirror((IrOp)ci->op);')),
 # a narrow lane's shifts down, extensions and wraps
 'shrs_any': m(SHIFT, SHIFT.replace('op0 == I_SHRS && v_fits_lane_signed(f, ir_args(f, tuple[j])[0], c->lane)', 'op0 == I_SHRS')),
 'shrs_unsigned': m(SHIFT, SHIFT.replace('op0 == I_SHRS && v_fits_lane_signed(', 'op0 == I_SHRS && v_fits_lane(')),
 'shru_signed': m(SHIFT, SHIFT.replace('op0 == I_SHRU && v_fits_lane(', 'op0 == I_SHRU && v_fits_lane_signed(')),
 'shrs_off': m(SHIFT, SHIFT.replace('(ij->op == I_SHRS && op0 == I_SHRS', '(0 && ij->op == I_SHRS && op0 == I_SHRS')),
 'thru_off': m(THRU, '  if (0) {'),
 # the saturating shift
 'shl_off': m('  if (narrow && op0 == I_SHRU && c->half < 0 && ir_vec_shl_sat()) {', '  if (0) {'),
 'shl_down_any': m(S_DN, '    if (!is_const(f, ja[1], &dn)) return REF_NONE;'),
 'shl_arms': m(S_LOST, '    uint32_t lost = ci->op == I_NE ? 1 : 0;'),
 'shl_ne_only': m(S_LOST, '    uint32_t lost = 0;'),
 'shl_bound_any': m(S_ONES, '    if (!is_const(f, sa[lost], &ones)) return REF_NONE;'),
 'shl_bound_all': m(S_ONES, S_ONES.replace('((w == 64 ? ones : ones & 0xffffffffu) >> (w - bits)) != (1ull << bits) - 1', '(w == 64 ? ones : ones & 0xffffffffu) != (w == 64 ? ~0ull : 0xffffffffu)')),
 'shl_back_any': m(S_BACK, '    if (bi->op != I_SHRU || bi->nargs != 2 || ir_args(f, back)[0] != sv)\n      return REF_NONE;'),
 'shl_up_any': m(S_UP, '    if (ti->op != I_SHL || ti->nargs != 2 || !is_const(f, ir_args(f, v)[1], &up))\n      return REF_NONE;'),
 'shl_in_loop': m(S_LOOP, ''),
 'shl_amount_whole': m(S_AMT, '        : amt;'),
 'shl_amount_lane': m(S_AMT, S_AMT.replace('w - 1, 0), 0, 0);', 'bits - 1, 0), 0, 0);')),
 # the value at the top of its i32
 'top_shift_any': m(T_SHL, '  if (ai->op == I_SHL && ai->nargs == 2 && is_const(f, ir_args(f, a)[1], &cv))'),
 'top_mask_any': m(T_MASK, '    if (!is_const(f, aa[s], &mask)) continue;'),
 'top_long_any': m(T_SH, ''),
 'top_unsigned': m(T_FIT, ''),
 'top_no_down': m('    *down = up - sh;\n    return x;', '    return x;'),
 # the saturating product
 'mul_off': m('  if (narrow && op0 == I_SHRS && c->half < 0) {\n    out = v_mulsat(c, tuple, k, st, depth);', '  if (0) {\n    out = v_mulsat(c, tuple, k, st, depth);'),
 'mul_down_any': m(M_UP, "        !is_const(f, ir_args(f, tuple[j])[1], &cv))\n      return REF_NONE;\n    Ref sel"),
 'mul_arms': m(M_OVER, '    uint32_t over = ci->op == I_NE ? 1 : 0;'),
 'mul_ne_only': m(M_OVER, '    uint32_t over = 0;'),
 'mul_sign_any': m(M_SIGN31, M_SIGN31.replace(' && (cv & 31) == 31)', ')')),
 'mul_half_any': m(M_HALF, M_HALF.replace(' || (cv & 63) != 32)', ')')),
 'mul_bound_any': m(M_BOUND, '      if (is_const(f, ta[s], &cv)) sg = ta[s ^ 1];'),
 'mul_sg_any': m(M_SG1, M_SG1.replace(' && (cv & 31) == 31) {', ') {')),
 'mul_sg63_any': m(M_SG2, M_SG2.replace(' && (cv & 63) == 63;', ';')),
 'mul_range_any': m(M_RANGE, '      if (j && mc != mk) return REF_NONE;'),
 'mul_range_hi': m(M_RANGE, M_RANGE.replace('(int64_t)mc > hi', '(int64_t)mc > hi + 1')),
 'mul_range_lo': m(M_RANGE, M_RANGE.replace('(int64_t)mc < lo', '(int64_t)mc < lo - 1')),
 'mul_var_any': m(M_VAR, ''),
 'mul_no_down': m(M_DOWN, ''),
 'mul_no_guard': m(M_GUARD, ''),
 'mul_guard_span': m('    Ref span = ir_emit(f, c->vguard, I_CONST, VT_I64, NULL, 0, 1ull << bits, 0);', '    Ref span = ir_emit(f, c->vguard, I_CONST, VT_I64, NULL, 0, (1ull << bits) + 1, 0);'),
 'mul_guard_bias': m('    Ref bias = ir_emit(f, c->vguard, I_CONST, VT_I64, NULL, 0, 1ull << (bits - 1), 0);', '    Ref bias = ir_emit(f, c->vguard, I_CONST, VT_I64, NULL, 0, 0, 0);'),
 'mul_unsigned': m(M_LO, M_LO.replace('x, yv, 1,', 'x, yv, 0,')),
 'mul_low_twice': m(M_HI, M_HI.replace('x, yv, 3,', 'x, yv, 1,')),
 'mul_narrow_unsigned': m(M_NARROW, M_NARROW.replace('pl, ph, 1,', 'pl, ph, 0,')),
 'mul_narrow_swapped': m(M_NARROW, M_NARROW.replace('pl, ph, 1,', 'ph, pl, 1,')),
 # the guard
 'guard_last': m(GOK, '  c->gok = cond;'),
 'guard_unread': m(AND, ''),
 # the copies, and the narrowing
 'copies_any': m(CAP, ''),
 'copies_fewer': m(CAP, '    while (R > 1 && nld * R > 2 * ir_vec_copies()) R >>= 1;'),
 'narrow_direct': m(NARROW, '      int t = d;', I),
 'narrow_scratch': m(NARROW, '      int t = FTMP;', I),
}
EQUIVALENT = {
 'cmp_map_signed': 'values that are the lane\'s both ways have its top bit clear: the signed compare of them is the unsigned one',

 'narrow_scratch': 'the result by way of the scratch register and a move: the same value, an instruction more',
 'narrow_direct': 'the scan gives a result the register of an operand that dies there only where it is hinted to it, '
                  'and a vector has no hint: the second allocator may',
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py', 'vec_sat', 'vec_sat_nofold', 'vec_narrow'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}))
