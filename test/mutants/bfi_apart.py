"""regalloc.inc's coalescer: a bfi whose field is the parameter the result
is carried back into (isel.c ra_bfi_field) is kept apart from it.  Killed
by test/cases_opt/bfi_carry.wat -- its answers, and how many results the
rule keeps apart at -O2 on AArch64."""
R, ISEL = 'src/regalloc.inc', 'src/isel.c'
def m(old, new, path=R): return (path, [(old, new)])
M = {
 'off': m('          if (ra_bfi_field(s, arg) != REF_NONE && FIND(ra_bfi_field(s, arg)) == rp) {',
          '          if (0) {'),
 'any_bfi': m('          if (ra_bfi_field(s, arg) != REF_NONE && FIND(ra_bfi_field(s, arg)) == rp) {',
              '          if (ra_bfi_field(s, arg) != REF_NONE) {'),
 'any_arg': m('          if (ra_bfi_field(s, arg) != REF_NONE && FIND(ra_bfi_field(s, arg)) == rp) {',
              '          if (ra_bfi_field(s, arg) != REF_NONE || 1) {'),
 'field_none': m('  return s->bfi && s->bfi[v] ? s->bfx[ir_args(s->f, v)[s->bfi[v] - 1]].src : REF_NONE;',
                 '  return REF_NONE;', ISEL),
 'field_base': m('  return s->bfi && s->bfi[v] ? s->bfx[ir_args(s->f, v)[s->bfi[v] - 1]].src : REF_NONE;',
                 '  return s->bfi && s->bfi[v] ? ir_args(s->f, v)[(s->bfi[v] - 1) ^ 1] : REF_NONE;', ISEL),
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),)
