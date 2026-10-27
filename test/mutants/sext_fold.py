"""opt.c ir_sext32: an i64 that already is the sign extension of its low 32
bits is not extended again.  Killed by test/cases_opt/sext_fold.wat."""
O = 'src/opt.c'
def m(old, new): return (O, [(old, new)])
M = {
 'sx_const': m('  if (is_const(f, x, &c)) return (uint64_t)(int64_t)(int32_t)(uint32_t)c == c;', '  if (is_const(f, x, &c)) return true;'),
 'sx_kb': m('  if ((ir_known_bits(f, x, KB_DEPTH) >> 31) == 0) return true;', '  if ((ir_known_bits(f, x, KB_DEPTH) >> 32) == 0) return true;'),
 'sx_kb_off': m('  if ((ir_known_bits(f, x, KB_DEPTH) >> 31) == 0) return true;', ''),
 'sx_load_narrow': m('      return MEMF_SIZE(in->imm2) < 2 || (MEMF_SIZE(in->imm2) == 2 && (in->imm2 & MEMF_SIGNED));', '      return MEMF_SIZE(in->imm2) <= 2;'),
 'sx_load_wide': m('      return MEMF_SIZE(in->imm2) < 2 || (MEMF_SIZE(in->imm2) == 2 && (in->imm2 & MEMF_SIGNED));', '      return MEMF_SIZE(in->imm2) <= 3 && (in->imm2 & MEMF_SIGNED);'),
 'sx_load_off': m('      return MEMF_SIZE(in->imm2) < 2 || (MEMF_SIZE(in->imm2) == 2 && (in->imm2 & MEMF_SIGNED));', '      return false;'),
 'sx_ext_off': m('    case I_EXT8S: case I_EXT16S: case I_EXT32S: case I_EXTENDS: return true;\n    case I_LOAD:', '    case I_LOAD:'),
 'sx_bits_one': m('      return in->nargs == 2 && sext32_rec(f, a[0], depth - 1, asked, nasked) &&\n             sext32_rec(f, a[1], depth - 1, asked, nasked);\n    case I_SELECT:', '      return in->nargs == 2 && sext32_rec(f, a[0], depth - 1, asked, nasked);\n    case I_SELECT:'),
 'sx_sel_one': m('      return in->nargs == 3 && sext32_rec(f, a[0], depth - 1, asked, nasked) &&\n             sext32_rec(f, a[1], depth - 1, asked, nasked);', '      return in->nargs == 3 && sext32_rec(f, a[0], depth - 1, asked, nasked);'),
 'sx_sel_cond': m('      return in->nargs == 3 && sext32_rec(f, a[0], depth - 1, asked, nasked) &&\n             sext32_rec(f, a[1], depth - 1, asked, nasked);', '      return in->nargs == 3 && sext32_rec(f, a[0], depth - 1, asked, nasked) &&\n             sext32_rec(f, a[2], depth - 1, asked, nasked);'),
 'sx_cycle_no': m('        if (asked[i] == x) return true;', '        if (asked[i] == x) return false;'),
 'sx_param_any': m('          if (e->nargs != b->nparams ||\n              !sext32_rec(f, e->args[q], depth - 1, asked, nasked + 1))\n            return false;', '          if (e->nargs != b->nparams)\n            return false;'),
 'sx_type': m('  if (in->type != VT_I64) return false;\n  uint64_t c;', '  uint64_t c;'),
 'fold_ext32s': m('        if (in->op == I_EXT32S && in->type == VT_I64 && ir_sext32(f, a[0])) {', '        if (0) {'),
 'fold_extends': m('          if (f->insts[a[0]].op == I_WRAP && f->insts[a[0]].nargs == 1 &&\n              ir_sext32(f, ir_args(f, a[0])[0])) {', '          if (0) {'),
 'fold_extends_any': m('          if (f->insts[a[0]].op == I_WRAP && f->insts[a[0]].nargs == 1 &&\n              ir_sext32(f, ir_args(f, a[0])[0])) {', '          if (f->insts[a[0]].op == I_WRAP && f->insts[a[0]].nargs == 1) {'),
 'sx_param_off': m('    case I_PARAM: {\n      for (uint32_t i = 0; i < nasked; i++)', '    case 9999: {\n      for (uint32_t i = 0; i < nasked; i++)'),
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}),
          ('stdgo', ['python3', 'test/gobench.py', '--corpus', 'stdgo', '--check'], {}),
          ('gobench', ['python3', 'test/gobench.py', '--corpus', 'gobench', '--check'], {}),
          ('cbench', ['python3', 'test/gobench.py', '--corpus', 'cbench', '--check'], {}))
