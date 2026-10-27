"""ir.c ir_not_int_min and ir_nonzero_with: a signed division's overflow
test dropped where the dividend cannot be the least value, and the
divisor's zero test where the source tested that value already.  Killed by
test/cases_opt/div_tests.wat."""
O, I, IS, IR, IX = 'src/opt.c', 'src/isel_ops.inc', 'src/isel.c', 'src/ir.c', 'src/isel_x64_ops.inc'
M = {
 'nm_kb': (IR, [('  if (!(ir_known_bits(f, v, 4) & sign)) return true;\n', '')]),
 'nm_or': (IR, [('      if (f->insts[a[k]].op == I_CONST && (f->insts[a[k]].imm & all & ~sign)) return true;', '      if (0) return true;')]),
 'nm_or_sign': (IR, [('      if (f->insts[a[k]].op == I_CONST && (f->insts[a[k]].imm & all & ~sign)) return true;', '      if (f->insts[a[k]].op == I_CONST && (f->insts[a[k]].imm & all)) return true;')]),
 'nm_ext': (IR, [('  return in->op == I_EXTENDS;\n}', '  return false;\n}')]),
 'nz_dext': (IR, [('  return (dext && ir_args(f, d)[0] == c) || (cext && ir_args(f, c)[0] == d);', '  return (cext && ir_args(f, c)[0] == d);')]),
 'nz_cext': (IR, [('  return (dext && ir_args(f, d)[0] == c) || (cext && ir_args(f, c)[0] == d);', '  return (dext && ir_args(f, d)[0] == c);')]),
 'nz_same': (IR, [('bool ir_nonzero_with(IrFunc *f, Ref c, Ref d) {\n  if (c == d) return true;', 'bool ir_nonzero_with(IrFunc *f, Ref c, Ref d) {\n  if (0) return true;')]),
 'div_hook': (I, [('      !ir_not_int_min(f, a[0]) && !div_guarded_earlier(s, v, true)) {', '      !div_guarded_earlier(s, v, true)) {')]),
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}),
          ('traps', ['./test/run_traps.sh'], {}),
          ('gobench', ['python3', 'test/gobench.py', '--corpus', 'gobench', '--check'], {}),
          ('rsbench', ['python3', 'test/gobench.py', '--corpus', 'rsbench', '--check'], {}),
          ('cbench', ['python3', 'test/gobench.py', '--corpus', 'cbench', '--check'], {}))
