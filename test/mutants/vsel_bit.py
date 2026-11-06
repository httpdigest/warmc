"""The 2026-11-06 rule of a vector select: it is made in whichever operand's
register its result is in -- the mask's, bsl; the false value's, bit; the
true value's, bif (isel_ops.inc's I_VSEL, a64.c a64_vbit and a64_vbif).
Killed by test/cases_opt/vsel_bit.wat (its shapes and answers) and the
answers of the cases and the collector's tests."""
O = 'src/isel_ops.inc'
A = 'src/a64.c'
def m(path, old, new): return (path, [(old, new)])
M = {
 'vb_bit_off': m(O, '      if (d != m && d == y && !warmc_getenv("WARMC_NO_VBIT")) {',
                 '      if (0) {'),
 'vb_bif_off': m(O, '      } else if (d != m && d == x && !warmc_getenv("WARMC_NO_VBIT")) {',
                 '      } else if (0) {'),
 'vb_bit_ops': m(O, '        a64_vbit(B, d, x, m);', '        a64_vbit(B, d, m, x);'),
 'vb_bif_ops': m(O, '        a64_vbif(B, d, y, m);', '        a64_vbif(B, d, x, m);'),
 'vb_bit_bif': m(O, '        a64_vbit(B, d, x, m);', '        a64_vbif(B, d, x, m);'),
 'vb_bif_bit': m(O, '        a64_vbif(B, d, y, m);', '        a64_vbit(B, d, y, m);'),
 'vb_bit_y': m(O, '      if (d != m && d == y && !warmc_getenv("WARMC_NO_VBIT")) {',
               '      if (d != m && d != x && !warmc_getenv("WARMC_NO_VBIT")) {'),
 'vb_enc_bit': m(A, '{ V3(0x6EA01C00u, rd, rn, rm); }', '{ V3(0x6EE01C00u, rd, rn, rm); }'),
 'vb_enc_bif': m(A, '{ V3(0x6EE01C00u, rd, rn, rm); }', '{ V3(0x6EA01C00u, rd, rn, rm); }'),
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('gc', ['./test/run_gc.sh'], {}),
          ('cases', ['./test/run_tests.sh'], {}))
