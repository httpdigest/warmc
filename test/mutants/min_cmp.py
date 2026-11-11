"""An eq or ne of a value with its width's least integer as cmp x, #1 and the
overflow flag (isel_ops.inc is_min_cmp, isel.c imm_ok_a64).  Killed by
test/cases_opt/min_cmp.wat's answers, shapes and count."""
O, I = 'src/isel_ops.inc', 'src/isel.c'
M = {
 'imm_off': (I, [('        if (!off) return true;\n      }\n      uint32_t i12;', '        if (0) return true;\n      }\n      uint32_t i12;')]),
 'imm_width': (I, [('          masked == (sf ? 0x8000000000000000ull : 0x80000000u)) {', '          masked == 0x8000000000000000ull) {')]),
 'cmp_one': (O, [('    a64_subs_imm(B, sf, RZR, rd(s, a[0], S0), 1, 0);', '    a64_subs_imm(B, sf, RZR, rd(s, a[0], S0), 0, 0);')]),
 'cond_swap': (O, [('  if (is_min_cmp(s, v)) return in->op == I_EQ ? CC_VS : CC_VC;', '  if (is_min_cmp(s, v)) return in->op == I_EQ ? CC_VC : CC_VS;')]),
 'cond_off': (O, [('  if (is_min_cmp(s, v)) return in->op == I_EQ ? CC_VS : CC_VC;\n', '')]),
 'chain_ccmp': (O, [('  if (is_min_cmp(s, c)) { a64_ccmp_imm(B, sf, x, 1, nzcv, open); return; }\n', '')]),
 'chain_imm': (O, [('  if (is_min_cmp(s, c)) { a64_ccmp_imm(B, sf, x, 1, nzcv, open); return; }', '  if (is_min_cmp(s, c)) { a64_ccmp_imm(B, sf, x, 0, nzcv, open); return; }')]),
 'width32': (O, [('  return is64(f, a[0]) ? c == 0x8000000000000000ull : (c & 0xffffffffu) == 0x80000000u;', '  return is64(f, a[0]) ? c == 0x8000000000000000ull : (c & 0xffffffffu) == 0x7fffffffu;')]),
}
EQUIVALENT = {}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}))
