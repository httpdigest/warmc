"""opt.c fold_trunc_exact and fcmp_exact: a float computation of integers
that rounds nowhere, truncated, is integer arithmetic, and its compares
with constants are decided by its bounds.  Killed by
test/cases_opt/trunc_exact.wat (made by test/gen_fexact.py)."""
O, I, IS, IR, IX = 'src/opt.c', 'src/isel_ops.inc', 'src/isel.c', 'src/ir.c', 'src/isel_x64_ops.inc'
M = {
 'fx_const_int': (O, [('if (!ts_fconst(f, v, f32, &c) || c != __builtin_trunc(c)) return false;', 'if (!ts_fconst(f, v, f32, &c)) return false;')]),
 'fx_conv_s': (O, [('if (in->op == I_CONVERTS && m >= half) { *lo = -half; *hi = half - 1; }', 'if (0) { *lo = -half; *hi = half - 1; }')]),
 'fx_sub': (O, [('else if (in->op == I_FSUB) { *lo = l0 - h1; *hi = h0 - l1; }', 'else if (in->op == I_FSUB) { *lo = l0 - l1; *hi = h0 - h1; }')]),
 'fx_mul': (O, [('for (int k = 1; k < 4; k++) {', 'for (int k = 3; k < 4; k++) {')]),
 'fx_neg': (O, [('      *lo = -h0;\n      *hi = -l0;', '      *lo = -l0;\n      *hi = -h0;')]),
 'fx_lim32': (O, [('double lim = f32 ? 16777216.0 : 9007199254740992.0;', 'double lim = f32 ? 9007199254740992.0 : 9007199254740992.0;')]),
 'fx_strict': (O, [('  return *lo > -lim && *hi < lim;', '  return *lo >= -lim && *hi <= lim;')]),
 'fx_emit_ext': (O, [('in->op == I_CONVERTS ? I_EXTENDS : I_EXTENDU, VT_I64', 'in->op == I_CONVERTS ? I_EXTENDU : I_EXTENDS, VT_I64')]),
 'fx_emit_neg': (O, [('Ref ops[2] = {emit_before(f, bi, idx, I_CONST, t, NULL, 0, 0), fx_emit(f, bi, idx, a[0], t)};', 'Ref ops[2] = {fx_emit(f, bi, idx, a[0], t), emit_before(f, bi, idx, I_CONST, t, NULL, 0, 0)};')]),
 'fx_emit_sub': (O, [('IrOp op = in->op == I_FADD ? I_ADD : in->op == I_FSUB ? I_SUB : I_MUL;', 'IrOp op = in->op == I_FADD ? I_ADD : in->op == I_FSUB ? I_ADD : I_MUL;')]),
 'tr_sgn_range': (O, [('if (sgn ? lo < -half || hi >= half : lo < 0 || hi >= 2 * half) return false;', 'if (sgn ? lo < -half || hi > half : lo < 0 || hi >= 2 * half) return false;')]),
 'tr_u_range': (O, [('if (sgn ? lo < -half || hi >= half : lo < 0 || hi >= 2 * half) return false;', 'if (sgn ? lo < -half || hi >= half : lo < -1 || hi >= 2 * half) return false;')]),
 'tr_sgn_kind': (O, [('bool sgn = in->op == I_TRUNCS || in->op == I_TRUNC_SATS;', 'bool sgn = in->op == I_TRUNC_SATS;')]),
 'fc_self': (O, [('    return op == I_FEQ || op == I_FLE || op == I_FGE;', '    return op == I_FEQ;')]),
 'fc_swap': (O, [('    op = op == I_FLT ? I_FGT : op == I_FGT ? I_FLT : op == I_FLE ? I_FGE : op == I_FGE ? I_FLE : op;', '    op = op;')]),
 'fc_nan': (O, [('  if (c != c) return op == I_FNE;', '  if (c != c) return 0;')]),
 'fc_eqfrac': (O, [('int eq = c != __builtin_trunc(c) || c < lo || c > hi ? 0 : lo == hi ? 1 : -1;', 'int eq = c < lo || c > hi ? 0 : lo == hi ? 1 : -1;')]),
 'fc_eqout': (O, [('int eq = c != __builtin_trunc(c) || c < lo || c > hi ? 0 : lo == hi ? 1 : -1;', 'int eq = c != __builtin_trunc(c) ? 0 : lo == hi ? 1 : -1;')]),
 'fc_eqpoint': (O, [('int eq = c != __builtin_trunc(c) || c < lo || c > hi ? 0 : lo == hi ? 1 : -1;', 'int eq = c != __builtin_trunc(c) || c < lo || c > hi ? 0 : -1;')]),
 'fc_ne': (O, [('      return eq < 0 ? -1 : op == I_FEQ ? eq : !eq;', '      return eq;')]),
 'fc_lt': (O, [('    case I_FLT: return hi < c ? 1 : lo >= c ? 0 : -1;', '    case I_FLT: return hi <= c ? 1 : lo >= c ? 0 : -1;')]),
 'fc_lt0': (O, [('    case I_FLT: return hi < c ? 1 : lo >= c ? 0 : -1;', '    case I_FLT: return hi < c ? 1 : lo > c ? 0 : -1;')]),
 'fc_le': (O, [('    case I_FLE: return hi <= c ? 1 : lo > c ? 0 : -1;', '    case I_FLE: return hi < c ? 1 : lo > c ? 0 : -1;')]),
 'fc_le0': (O, [('    case I_FLE: return hi <= c ? 1 : lo > c ? 0 : -1;', '    case I_FLE: return hi <= c ? 1 : lo >= c ? 0 : -1;')]),
 'fc_gt': (O, [('    case I_FGT: return lo > c ? 1 : hi <= c ? 0 : -1;', '    case I_FGT: return lo >= c ? 1 : hi <= c ? 0 : -1;')]),
 'fc_gt0': (O, [('    case I_FGT: return lo > c ? 1 : hi <= c ? 0 : -1;', '    case I_FGT: return lo > c ? 1 : hi < c ? 0 : -1;')]),
 'fc_ge': (O, [('    default:    return lo >= c ? 1 : hi < c ? 0 : -1;', '    default:    return lo > c ? 1 : hi < c ? 0 : -1;')]),
 'fc_ge0': (O, [('    default:    return lo >= c ? 1 : hi < c ? 0 : -1;', '    default:    return lo >= c ? 1 : hi <= c ? 0 : -1;')]),
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}),
          ('traps', ['./test/run_traps.sh'], {}),
          ('gobench', ['python3', 'test/gobench.py', '--corpus', 'gobench', '--check'], {}),
          ('rsbench', ['python3', 'test/gobench.py', '--corpus', 'rsbench', '--check'], {}),
          ('cbench', ['python3', 'test/gobench.py', '--corpus', 'cbench', '--check'], {}))
