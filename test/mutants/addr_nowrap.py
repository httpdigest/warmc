"""-O3's addr-nowrap: an access at x + c, x the C stack pointer or made from
it, taken as x at offset c (opt.c pass_addr_nowrap), and any x + c at
offset k as x at offset k + c to the memory pass (same_access_after).
Killed by test/cases_opt/addr_nowrap.wat -- its answers at every level,
-O3's traps and loads where wasm's addresses wrap, and how many accesses it
folds at -O2 and -O3 -- and test/run_levels.sh, which counts them with
-faddr-nowrap alone."""
O, MAIN = 'src/opt.c', 'src/main.c'
def m(old, new, path=O): return (path, [(old, new)])
M = {
 # when
 'level': m('  o->fma = o->dead_frames = o->acc_tail = o->float_reassoc = o->addr_nowrap = level >= 3;',
            '  o->fma = o->dead_frames = o->acc_tail = o->float_reassoc = level >= 3;\n  o->addr_nowrap = level >= 2;', MAIN),
 'flag': m('      else if (!strcmp(name, "addr-nowrap")) opt.addr_nowrap = on;',
           '      else if (!strcmp(name, "addr-nowrap")) opt.addr_nowrap = true;', MAIN),
 'fold_off': m('    TP("addr_nowrap", pass_addr_nowrap(f));', '    (void)pass_addr_nowrap;'),
 'fold_no_sp': m('  if (o->addr_nowrap && df_sp != UINT32_MAX && fx_mod == f->m)',
                 '  if (o->addr_nowrap && fx_mod == f->m)'),
 'scan': m('  df_func = o->dead_frames || o->addr_nowrap\n', '  df_func = o->dead_frames\n'),
 # which addresses
 'root_any': m('      if (r == REF_NONE || f->insts[r].op != I_GLOBAL_GET || f->insts[r].imm != df_sp) continue;',
               '      if (r == REF_NONE) continue;'),
 'root_global': m('      if (r == REF_NONE || f->insts[r].op != I_GLOBAL_GET || f->insts[r].imm != df_sp) continue;',
                  '      if (r == REF_NONE || f->insts[r].op != I_GLOBAL_GET) continue;'),
 'i64': m('        if (xi->op != I_ADD || xi->nargs != 2 || xi->type != VT_I32 ||',
          '        if (xi->op != I_ADD || xi->nargs != 2 ||'),
 'sub': m('        if (xi->op != I_ADD || xi->nargs != 2 || xi->type != VT_I32 ||',
          '        if ((xi->op != I_ADD && xi->op != I_SUB) || xi->nargs != 2 || xi->type != VT_I32 ||'),
 'limit_off': m('            !is_const(f, ir_args(f, x)[1], &c) ||\n            (c & 0xffffffffu) + in->imm >= ADDR_NOWRAP_MAX)',
                '            !is_const(f, ir_args(f, x)[1], &c))'),
 'max': m('#define ADDR_NOWRAP_MAX 0x10000', '#define ADDR_NOWRAP_MAX 0x100000000ull'),
 'max_small': m('#define ADDR_NOWRAP_MAX 0x10000', '#define ADDR_NOWRAP_MAX 0x8'),
 # the rewrite
 'keep_x': m('        ir_args(f, v)[0] = ir_args(f, x)[0];\n        g_stats.addr_nowrap++;', '        g_stats.addr_nowrap++;'),
 'once': m('        g_stats.addr_nowrap++;\n        changed = true;\n      }', '        g_stats.addr_nowrap++;\n        changed = true;\n        break;\n      }'),
 # the memory pass's equality
 'eq_off': m('  if (ran && ran->nowrap && (ca & 0xffffffffull) < ADDR_NOWRAP_MAX &&',
             '  if (0 && ran && ran->nowrap && (ca & 0xffffffffull) < ADDR_NOWRAP_MAX &&'),
 'eq_always': m('  if (ran && ran->nowrap && (ca & 0xffffffffull) < ADDR_NOWRAP_MAX &&',
                '  if (ran && (ca & 0xffffffffull) < ADDR_NOWRAP_MAX &&'),
 'eq_level': m('      MemRan ran = { ent, nent, pend, npend, o->addr_nowrap };',
               '      MemRan ran = { ent, nent, pend, npend, false };'),
 'eq_limit': m('  if (ran && ran->nowrap && (ca & 0xffffffffull) < ADDR_NOWRAP_MAX &&\n      (cb & 0xffffffffull) < ADDR_NOWRAP_MAX)',
               '  if (ran && ran->nowrap)'),
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('levels', ['./test/run_levels.sh'], {}),
          ('traps', ['./test/run_traps.sh'], {'WARMC_FLAGS': '-O3'}))
EQUIVALENT = {
 'fold_no_sp':
  "no global's index is UINT32_MAX, so with no stack pointer the pass folds nothing; the test saves its walk",
 'i64':
  "df_sp is an i32 global (df_stackish), and the adds access_root walked from an address to it are of its type",
}
