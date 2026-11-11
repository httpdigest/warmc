"""Code inside one 4 KB page (pagefit.c, the rule; codegen.c page_fit and
its callers; isel.c's stretches, FuncOut.fit; link.c place; the text
section's alignment).  Killed by test/pagefit_test.c, the rule on stretches
and code made to measure, and by test/run_pagefit.py: the rule worked out
again from the lists WARMC_PAGE_FIT_DEBUG=1 prints, the disassembly of the
object and of the programs, and the programs' sums.

Not mutated: `tiny ||` ahead of the walk that finds a small loop, a
shortcut to the same answer; isel.c's own reading of WARMC_NO_PAGE_FIT,
whose stretches nothing then reads; elf.c's alignment, which only a Linux
run reads."""
G, I, L, R = 'src/codegen.c', 'src/isel.c', 'src/link.c', 'src/pagefit.c'
def m(old, new, path=G): return (path, [(old, new)])
MODE = '    x = !warmc_getenv("WARMC_NO_PAGE_FIT") && !g_target->is_x64;'
OVER = '  return b > a && a / PAGEFIT_PAGE != (b - 1) / PAGEFIT_PAGE;'
WHOLE = '  int whole = len - lead <= PAGEFIT_MAX;'
FN = '    if (whole && over_page(p + lead, p + len)) from = p + lead;'
ST = '      if (over_page(p + fit[2 * i], p + fit[2 * i + 1])) from = p + fit[2 * i];'
LFIT = '  bool fit = k == K_TEXT && L->fmt != TGT_PE && !warmc_getenv("WARMC_NO_PAGE_FIT");'
CFN = '    if (size <= PAGEFIT_MAX && over_page(p, p + size)) from = p;'
CCC = '  if ((w & 0xff000010u) == 0x54000000u || (w & 0x7e000000u) == 0x34000000u)'
CGO = '      if (over_page(p + o - d, p + o + 4)) from = p + o - d;'
CSKIP = '      if (!d || d + 4 > PAGEFIT_MAX || d > o) continue;'
STEP = '  return (need + step - 1) / step * step;'
M = {
 # the switch
 'off': m(MODE, '    x = 0;'),
 'knob': m(MODE, '    x = !g_target->is_x64;'),
 # a boundary
 'over_end': m(OVER, '  return b > a && a / PAGEFIT_PAGE != b / PAGEFIT_PAGE;', R),
 'over_none': m(OVER, '  return 0;', R),
 'page': m('#define PAGEFIT_PAGE 4096u', '#define PAGEFIT_PAGE 8192u', 'src/pagefit.h'),
 'most_less': m('#define PAGEFIT_MAX 256u', '#define PAGEFIT_MAX 252u', 'src/pagefit.h'),
 'most_more': m('#define PAGEFIT_MAX 256u', '#define PAGEFIT_MAX 260u', 'src/pagefit.h'),
 # the move
 'round': m(STEP, '  return need;', R),
 'round_down': m(STEP, '  return need / step * step;', R),
 'need': m('  uint64_t need = PAGEFIT_PAGE - from % PAGEFIT_PAGE;', '  uint64_t need = PAGEFIT_PAGE - from % PAGEFIT_PAGE - 4;', R),
 # a function by its size
 'fn_lead': m(WHOLE, '  int whole = len <= PAGEFIT_MAX;', R),
 'fn_over': m(FN, FN.replace('over_page(p + lead, ', 'over_page(p, '), R),
 'fn_from': m(FN, FN.replace('from = p + lead;', 'from = p;'), R),
 'fn_only': m('  if (!whole && !nfit) return at;', '  if (!whole) return at;', R),
 'fn_never': m(FN, '    ;', R),
 # a stretch
 'st_first': m('    for (uint32_t i = 0; i < nfit && from == UINT64_MAX; i++)', '    for (uint32_t i = 1; i < nfit && from == UINT64_MAX; i++)', R),
 'st_end': m(ST, ST.replace('p + fit[2 * i + 1])', 'p + fit[2 * i] + 4)'), R),
 'st_from': m(ST, ST.replace('from = p + fit[2 * i];', 'from = p;'), R),
 'st_last': m('    p += to_boundary(from, step);\n  }\n  return at;\n}\n\n/* how far back',
              '    p += to_boundary(from, step);\n  }\n  return p;\n}\n\n/* how far back', R),
 # code of another's making
 'c_page': m('  if (align >= PAGEFIT_PAGE || !code || (size & 3)) return at;', '  if (!code || (size & 3)) return at;', R),
 'c_odd': m('  if (align >= PAGEFIT_PAGE || !code || (size & 3)) return at;', '  if (align >= PAGEFIT_PAGE || !code) return at;', R),
 'c_step': m('  uint64_t step = align < 4 ? 4 : align;', '  uint64_t step = 4;', R),
 'c_step1': m('  uint64_t step = align < 4 ? 4 : align;', '  uint64_t step = align ? align : 4;', R),
 'c_fn_never': m(CFN, '    ;', R),
 'c_fn_any': m(CFN, CFN.replace('size <= PAGEFIT_MAX && ', ''), R),
 'c_bcc': m(CCC, CCC.replace('(w & 0xff000010u) == 0x54000000u || ', ''), R),
 'c_cbz': m(CCC, CCC.replace(' || (w & 0x7e000000u) == 0x34000000u', ''), R),
 'c_cbz_wide': m(CCC, CCC.replace('(w & 0x7e000000u) == 0x34000000u', '(w & 0xfe000000u) == 0x34000000u'), R),
 'c_tbz': m('  else if ((w & 0x7e000000u) == 0x36000000u)\n    d = (int64_t)((uint64_t)((w >> 5) & 0x3fffu) << 50) >> 50;\n', '', R),
 'c_tbz_wide': m('  else if ((w & 0x7e000000u) == 0x36000000u)', '  else if ((w & 0xfe000000u) == 0x36000000u)', R),
 'c_tbz_19': m('    d = (int64_t)((uint64_t)((w >> 5) & 0x3fffu) << 50) >> 50;', '    d = (int64_t)((uint64_t)((w >> 5) & 0x7ffffu) << 45) >> 45;', R),
 'c_b': m('  else if ((w & 0xfc000000u) == 0x14000000u)\n    d = (int64_t)((uint64_t)(w & 0x3ffffffu) << 38) >> 38;\n', '', R),
 'c_sign': m('    d = (int64_t)((uint64_t)((w >> 5) & 0x7ffffu) << 45) >> 45;\n  else if ((w & 0x7e000000u) == 0x36000000u)',
             '    d = (int64_t)((w >> 5) & 0x7ffffu);\n  else if ((w & 0x7e000000u) == 0x36000000u)', R),
 'c_forward': m('  return d < 0 ? (uint64_t)-d * 4 : 0;', '  return d < 0 ? (uint64_t)-d * 4 : (uint64_t)d * 4;', R),
 'c_words': m('  return d < 0 ? (uint64_t)-d * 4 : 0;', '  return d < 0 ? (uint64_t)-d : 0;', R),
 'c_most': m(CSKIP, CSKIP.replace('d + 4 > PAGEFIT_MAX', 'd > PAGEFIT_MAX'), R),
 'c_any': m(CSKIP, CSKIP.replace(' || d + 4 > PAGEFIT_MAX', ''), R),
 'c_outside': m(CSKIP, CSKIP.replace(' || d > o', ''), R),
 'c_first': m(CSKIP, CSKIP.replace('d > o', 'd >= o'), R),
 'c_from': m(CGO, CGO.replace('from = p + o - d;', 'from = p;'), R),
 'c_top': m(CGO, CGO.replace('over_page(p + o - d, p + o + 4)', 'over_page(p + o, p + o + 4)'), R),
 'c_branch': m(CGO, CGO.replace('over_page(p + o - d, p + o + 4)', 'over_page(p + o - d, p + o)'), R),
 # the compiler's use of it
 'step': m('  return fo->want_align ? ALIGN_BYTES : fo->align8 ? 8u : 4u;', '  return 4u;'),
 'lead': m('  return pagefit_stretches(at, step, fo->entry_off, fo->code_len, fo->fit, fo->nfit);',
           '  return pagefit_stretches(at, step, 0, fo->code_len, fo->fit, fo->nfit);'),
 'no_stretches': m('  return pagefit_stretches(at, step, fo->entry_off, fo->code_len, fo->fit, fo->nfit);',
                   '  return pagefit_stretches(at, step, fo->entry_off, fo->code_len, fo->fit, 0);'),
 'pad': m('      while (obj->text.len < want) g_target->pad_nop(&obj->text);', '      ;'),
 'count': m('      g_stats.page_fits++;\n', ''),
 'count_bytes': m('      g_stats.page_fit_bytes += want - obj->text.len;\n', ''),
 'count_swap': m('          (unsigned long long)g_stats.loop_phases, (unsigned long long)g_stats.page_fits,\n'
                 '          (unsigned long long)g_stats.page_fit_bytes, (unsigned long long)g_stats.next_jumps,',
                 '          (unsigned long long)g_stats.loop_phases, (unsigned long long)g_stats.page_fit_bytes,\n'
                 '          (unsigned long long)g_stats.page_fits, (unsigned long long)g_stats.next_jumps,', 'src/main.c'),
 # a start and its full body
 'sw_start': m('  uint64_t start = page_fit(obj->text.len, fo, fit_step(fo));', '  uint64_t start = obj->text.len;'),
 'sw_body': m('  p = page_fit(p, x, fit_step(x));\n', ''),
 # the stretches (isel.c)
 'loop_insts': m('#define PAGE_LOOP_INSTS 96u', '#define PAGE_LOOP_INSTS 12u', I),
 'loop_calls': m('(tiny || loop_within(s, f, lh, PAGE_LOOP_INSTS, true))', '(tiny || loop_within(s, f, lh, PAGE_LOOP_INSTS, false))', I),
 'loop_none': m('      if (pfit && lh != UINT32_MAX && (tiny || loop_within(s, f, lh, PAGE_LOOP_INSTS, true))) {',
                '      if (0 && pfit && lh != UINT32_MAX && (tiny || loop_within(s, f, lh, PAGE_LOOP_INSTS, true))) {', I),
 'loop_any': m('      if (!span || span > PAGEFIT_MAX) continue;', '      if (!span) continue;', I),
 'loop_less': m('      if (!span || span > PAGEFIT_MAX) continue;', '      if (!span || span >= PAGEFIT_MAX) continue;', I),
 'ends': m('    if (pend) pend[bi] = (uint32_t)B->len;\n', '', I),
 'span_end': m('      pfr[2 * keep + 1] = top + span;', '      pfr[2 * keep + 1] = top + 4;', I),
 'at_lead': m('fout->fit[i] = pfr[i] + (uint32_t)(pro.len + lead.len);', 'fout->fit[i] = pfr[i] + (uint32_t)pro.len;', I),
 'at_prologue': m('fout->fit[i] = pfr[i] + (uint32_t)(pro.len + lead.len);', 'fout->fit[i] = pfr[i] + (uint32_t)lead.len;', I),
 # the text's alignment
 'macho': m('      12, S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS, &reloc[0], rel_off[0] },',
            '      6, S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS, &reloc[0], rel_off[0] },', 'src/macho.c'),
 # the linker's use of it
 'l_off': m(LFIT, '  bool fit = false;', L),
 'l_knob': m(LFIT, '  bool fit = k == K_TEXT && L->fmt != TGT_PE;', L),
 'l_align': m('        uint64_t to = pagefit_code(is->data, is->size, is->align, at);', '        uint64_t to = pagefit_code(is->data, is->size, 4, at);', L),
 'l_use': m('        at = to;\n', '', L),
}
EQUIVALENT = {
 'c_page': "code aligned to a page moves by pages, from which what runs over a boundary still does: the test saves reading it",
}
TARGETS = ['build/warmc', 'build/pagefit_test']
# (a mutant that moves by nothing never ends: the rule's own test has 20 s)
SUITES = (('rule', ['python3', '-c', 'import os, subprocess, sys; '
                    'sys.exit(subprocess.run([os.environ["BUILD"] + "/pagefit_test"], timeout=20).returncode)'], {}),
          ('pagefit', ['python3', 'test/run_pagefit.py'], {}))
