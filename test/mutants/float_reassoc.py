"""-O3's float-reassoc: float sums and products a loop carries in vector
lanes (opt.c vectorize_one), and float chains rebalanced (pass_reassoc,
pass_reassoc_carried).  Killed by test/cases_opt/float_reassoc.wat (made
by test/gen_floatreassoc.py) -- its answers at every level and its counts
at -O2 and -O3 -- and test/run_levels.sh."""
O, MAIN = 'src/opt.c', 'src/main.c'
def m(old, new, path=O): return (path, [(old, new)])
M = {
 # when
 'level': m('  o->fma = o->dead_frames = o->acc_tail = o->float_reassoc = o->addr_nowrap = level >= 3;',
            '  o->fma = o->dead_frames = o->acc_tail = o->addr_nowrap = level >= 3;\n  o->float_reassoc = level >= 2;', MAIN),
 'flag': m('      else if (!strcmp(name, "float-reassoc")) opt.float_reassoc = on;',
           '      else if (!strcmp(name, "float-reassoc")) opt.float_reassoc = true;', MAIN),
 'vec_off': m('TP("vectorize", pass_vectorize(f, o->exact_trap, o->float_reassoc))',
              'TP("vectorize", pass_vectorize(f, o->exact_trap, false))'),
 'chain_off': m('TP("reassoc", pass_reassoc(f, o->float_reassoc))', 'TP("reassoc", pass_reassoc(f, false))'),
 'carried_off': m('TP("reassoc_carried", pass_reassoc_carried(f, o->float_reassoc))',
                  'TP("reassoc_carried", pass_reassoc_carried(f, false))'),
 # the vectoriser
 'vec_sum': m('    bool is_fsum = fre && vt_is_float(pt) && (n->op == I_FADD || n->op == I_FMUL);',
              '    bool is_fsum = fre && vt_is_float(pt) && n->op == I_FMUL;'),
 'vec_prod': m('    bool is_fsum = fre && vt_is_float(pt) && (n->op == I_FADD || n->op == I_FMUL);',
               '    bool is_fsum = fre && vt_is_float(pt) && n->op == I_FADD;'),
 'vec_sub': m('    bool is_fsum = fre && vt_is_float(pt) && (n->op == I_FADD || n->op == I_FMUL);',
              '    bool is_fsum = fre && vt_is_float(pt) && (n->op == I_FADD || n->op == I_FMUL || n->op == I_FSUB);'),
 'vec_redop': m('    if (!inloop[n->block] ||\n        (!is_mm && !is_fmm && !is_fsum && n->op != I_ADD',
                '    if (!inloop[n->block] ||\n        (!is_mm && !is_fmm && n->op != I_ADD'),
 'vop_add': m('              : redop[r] == I_FADD ? I_VFADD\n', ''),
 'vop_mul': m('              : redop[r] == I_FMUL ? I_VFMUL\n', '              : redop[r] == I_FMUL ? I_VFADD\n'),
 'id_add': m('                 : redop[r] == I_FADD ? (w32 ? 0x80000000u : 0x8000000000000000ull)\n', ''),
 'id_add0': m('                 : redop[r] == I_FADD ? (w32 ? 0x80000000u : 0x8000000000000000ull)\n',
              '                 : redop[r] == I_FADD ? (w32 ? 0x3f800000u : 0x3ff0000000000000ull)\n'),
 'id_mul': m('                 : redop[r] == I_FMUL ? (w32 ? 0x3f800000u : 0x3ff0000000000000ull)\n', ''),
 'exit_lanes': m('    } else if (redop[ri] == I_FMIN || redop[ri] == I_FMAX || redop[ri] == I_FADD ||\n               redop[ri] == I_FMUL) {',
                 '    } else if (redop[ri] == I_FMIN || redop[ri] == I_FMAX) {'),
 # the chains
 'assoc_mul': m('  if (vt_is_float((ValType)in->type)) return fre && (in->op == I_FADD || in->op == I_FMUL);',
                '  if (vt_is_float((ValType)in->type)) return fre && in->op == I_FADD;'),
 'assoc_any': m('  if (vt_is_float((ValType)in->type)) return fre && (in->op == I_FADD || in->op == I_FMUL);',
                '  if (vt_is_float((ValType)in->type)) return fre;'),
 'carried_consts': m('        bool nk = vec || flt;', '        bool nk = vec;'),
 'carried_sub': m('                               : (ty == VT_F32 || ty == VT_F64) && (op == I_FADD || op == I_FMUL));',
                  '                               : (ty == VT_F32 || ty == VT_F64) && (op == I_FADD || op == I_FMUL || op == I_FSUB));'),
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('levels', ['./test/run_levels.sh'], {}),
          ('traps', ['./test/run_traps.sh'], {'WARMC_FLAGS': '-O3'}))
