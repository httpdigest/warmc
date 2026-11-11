"""A result takes the register it is hinted to from a value that dies
where it is made, and a bfi writes into its other operand in place
(regalloc.inc, the scan's ties ahead of its hint; RA_DIE_TIE and RA_BFI,
arm64).  Killed by test/cases_opt/die_tie.wat's answers, its count and its
shapes, with coalesce_exact.wat's.

Not mutated: WARMC_NO_DIE_TIE, which no case sets."""
R, I = 'src/regalloc.inc', 'src/isel.c'
def m(old, new, path=R): return (path, [(old, new)])
HEAD = ('      if (RA_DIE_TIE && !no_die_tie && chosen < 0 && want >= 0 && owner[want] >= 0 &&\n'
        '          sorted[owner[want]].end == cur->start) {')
LATE = '        if (late == REF_NONE || web[late] != dx) {'
OK = ('          if (in_pool && !((barred >> want) & 1) && nc == 1 && vic == owner[want] &&\n'
      '              (!cur->crosses_call || cs || (rare_only && want != RA_ALLOC_RES_REG))) {\n'
      '            chosen = want;\n            g_stats.die_ties++;')
D0 = '      Ref d0 = mem_head[cur->value] != UINT32_MAX ? mem_head[cur->value] : cur->value;'
BFI = ('        if (ta == REF_NONE && RA_BFI && s->bfi && s->bfi[d0])\n'
       '          ta = ir_args(f, d0)[(s->bfi[d0] - 1) ^ 1];')
M = {
 'off': m('#define RA_DIE_TIE 1', '#define RA_DIE_TIE 0', I),
 # which operand
 'lives_on': m(HEAD, HEAD.replace('          sorted[owner[want]].end == cur->start) {', '          sorted[owner[want]].end >= cur->start) {')),
 'late_too': m(LATE, '        if (true) {'),
 'late_value': m(LATE, '        if (late == REF_NONE || late != dx) {'),
 # which result
 'named_by': m(D0, '      Ref d0 = cur->value;'),
 'crosses': m(OK, OK.replace('              (!cur->crosses_call || cs || (rare_only && want != RA_ALLOC_RES_REG))) {', '              true) {')),
 'others_there': m(OK, OK.replace('nc == 1 && vic == owner[want] &&', 'nc >= 1 &&')),
 'count': m('            chosen = want;\n            g_stats.die_ties++;', '            chosen = want;'),
 'count_trials': m('      g_stats.die_ties = st_dt0;\n', ''),
 # a bfi's operands
 'bfi_untied': m(BFI, '        if (0)\n          ta = ir_args(f, d0)[(s->bfi[d0] - 1) ^ 1];'),
 'bfi_field': m(BFI, '        if (ta == REF_NONE && RA_BFI && s->bfi && s->bfi[d0])\n          ta = ir_args(f, d0)[(s->bfi[d0] - 1)];'),
 'tie_rep': m('        Ref ta = tied_operand_of(f, d0);', '        Ref ta = tied_operand_of(f, cur->value);'),
}
EQUIVALENT = {
 'others_there': 'a second value in the register while the result lives would be live where the '
                 'operand that dies there is: the scan never has two there',
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py', 'die_tie', 'coalesce_exact'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}))
