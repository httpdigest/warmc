"""isel.c sh_plan: an i8x16.shuffle as the cheapest a64 sequence found --
one permute (dup, rev, ext, zip, uzp, trn), a base with lanes put in, two
permutes -- or else a table lookup; its emission (isel_ops.inc) and the
index vectors it leaves unmade.  Killed by test/cases_opt/shuffle.wat
(made by test/gen_shuffle.py)."""
I, IO = 'src/isel.c', 'src/isel_ops.inc'
def m(old, new, path=I): return (path, [(old, new)])
M = {
 # what each instruction makes
 'ap_dup': m('      for (uint32_t i = 0; i < 16; i++) out[i] = A[st->k * E + i % E];',
             '      for (uint32_t i = 0; i < 16; i++) out[i] = A[st->k * E];'),
 'ap_rev': m('        out[i] = A[c0 + (ne - 1 - w / E) * E + w % E];', '        out[i] = A[c0 + (ne - 1 - w / E) * E];'),
 'ap_ext': m('        out[i] = j < 16 ? A[j] : Bm[j - 16];', '        out[i] = j < 16 ? A[j] : A[j - 16];'),
 'ap_zip2': m('            s = (st->op == SH_ZIP2 ? n / 2 : 0) + L / 2;', '            s = L / 2;'),
 'ap_zipab': m('''            S = L & 1 ? Bm : A;
            s = (st->op == SH_ZIP2''', '''            S = L & 1 ? A : Bm;
            s = (st->op == SH_ZIP2'''),
 'ap_uzp2': m('            s = 2 * L + (st->op == SH_UZP2);', '            s = 2 * L;'),
 'ap_uzpab': m('            S = s < n ? A : Bm;', '            S = A;'),
 'ap_trn2': m('            s = (L & ~1u) + (st->op == SH_TRN2);', '            s = (L & ~1u);'),
 # what is tried
 'gen_dup': m('        for (uint32_t l = 0; l < (16u >> e); l++) SH_ADD(SH_DUP, e, a, a, l);\n', ''),
 'gen_rev': m('        for (uint32_t c = e + 1; c <= 3; c++) SH_ADD(SH_REV, e, a, a, c);\n', ''),
 'gen_trn': m('        for (uint8_t op = SH_ZIP1; op <= SH_TRN2; op++) SH_ADD(op, e, a, b, 0);',
              '        for (uint8_t op = SH_ZIP1; op <= SH_UZP2; op++) SH_ADD(op, e, a, b, 0);'),
 'gen_ext': m('      for (uint32_t k = 1; k < 16; k++) SH_ADD(SH_EXT, 0, a, b, k);',
              '      for (uint32_t k = 1; k < 8; k++) SH_ADD(SH_EXT, 0, a, b, k);'),
 'ins_align': m('      ok = b0 % E == 0 && b0 / 16 < nsrc && cnt < SH_MAX_INS;',
                '      ok = b0 / 16 < nsrc && cnt < SH_MAX_INS;'),
 'ins_bytes': m('      for (uint32_t b = 1; b < E && ok; b++) ok = want[L * E + b] == b0 + b;\n', ''),
 'ins_max': m('#define SH_MAX_INS 4\n', '#define SH_MAX_INS 5\n'),
 # which is taken
 'pl_one': m('    want[i] = one ? want0[i] & 15 : want0[i];', '    want[i] = want0[i];'),
 'pl_tbl2': m('  if (from == 3) { best->kind = SHK_TBL2; best->cost = 7; }',
              '  if (from == 3) { best->kind = SHK_TBL2; best->cost = 3; }'),
 'pl_tbl1': m('  else { best->kind = SHK_TBL1; best->st[0].a = from == 2; best->cost = 5; }',
              '  else { best->kind = SHK_TBL1; best->st[0].a = from == 2; best->cost = 3; }'),
 'pl_tbly': m('  else { best->kind = SHK_TBL1; best->st[0].a = from == 2; best->cost = 5; }',
              '  else { best->kind = SHK_TBL1; best->st[0].a = 0; best->cost = 5; }'),
 'pl_ins': m('    if (!sh_ins(want, base, nsrc, &p) || 2u * p.n >= best->cost) continue;',
             '    if (1) continue;'),
 'pl_two_steps': m('  if (best->cost <= 4) return;', '  return;'),
 'pl_memo': m('  if (memo[h].key == key && memo[h].lo == in->imm && memo[h].hi == in->imm2) {',
              '  if (memo[h].lo == in->imm && memo[h].hi == in->imm2) {'),
 # emission
 'em_work': m('            if (reg[p.st[i].a] == d) work = FTMP;\n', '', IO),
 'em_tbl2_pair': m('          if (reg[1] == reg[0] + 1) {', '          if (0) {', IO),
 'em_need_b': m('          if (st->op >= SH_EXT && st->op <= SH_TRN2 && st->b < 2) need[st->b] = true;\n', '', IO),
 # the index vectors left unmade
 'fold_off': m('              s->folded[a[j]] = 1;\n', ''),
 'fold_needed': m('              if (j == 2 && (p.kind == SHK_TBL1 || p.kind == SHK_TBL2)) needed[a[j]] = 1;\n', ''),
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('simd', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}))
EQUIVALENT = {
 'ins_max':
  "lanes put in cost an instruction each: more than two never cost less than the table lookup, so a fifth is never taken; the limit only bounds the plan's steps",
}
