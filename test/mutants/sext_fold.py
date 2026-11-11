"""An i64 that already is the sign extension of its low 32 bits is not
narrowed and extended again (opt.c's I_EXTENDS of a wrap).  Killed by
test/cases_opt/sext_fold.wat.  The question itself, ir.c ir_sign_ge, and the
fold of an extension from 8, 16 or 32 bits are test/mutants/sign_bits.py's."""
O = 'src/opt.c'
def m(old, new): return (O, [(old, new)])
M = {
 'fold_extends': m('          if (f->insts[a[0]].op == I_WRAP && f->insts[a[0]].nargs == 1 &&\n              ir_sign_ge(f, ir_args(f, a[0])[0], 33)) {', '          if (0) {'),
 'fold_extends_any': m('          if (f->insts[a[0]].op == I_WRAP && f->insts[a[0]].nargs == 1 &&\n              ir_sign_ge(f, ir_args(f, a[0])[0], 33)) {', '          if (f->insts[a[0]].op == I_WRAP && f->insts[a[0]].nargs == 1) {'),
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}),
          ('stdgo', ['python3', 'test/gobench.py', '--corpus', 'stdgo', '--check'], {}),
          ('gobench', ['python3', 'test/gobench.py', '--corpus', 'gobench', '--check'], {}),
          ('cbench', ['python3', 'test/gobench.py', '--corpus', 'cbench', '--check'], {}))
