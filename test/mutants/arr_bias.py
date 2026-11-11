"""An element at its array's address plus a value times its size (opt.c
pass_array_bias, the operand it gives an access; ir.c ir_reads_low32, what
the wraps make of that operand; isel_ops.inc's array.get and array.set,
which take the value's register, and isel.c rd_peek, which finds it).  Killed by test/cases_opt/arr_bias.wat's answers, its counts of
the operands given and the accesses made by them, and its shapes.

Not mutated: the test of the target (ir_addr_index_disp), which is false
wherever these run; the header being a whole number of elements, which 8
bytes are of every size an integer has; the tests that the value is an
integer and the index no immediate, which hold of every access the pass
finds (a difference's operand; a constant less a constant is folded)."""
O, I, C = 'src/opt.c', 'src/isel_ops.inc', 'src/ir.c'
def m(old, new, path=O): return (path, [(old, new)])
LOW = '      return (j == 1 || ((in->imm2 & GCF_BIASARG) && j == GCF_BIAS_AT(in->op))) && !off;'
IDX = '      int idx = kidx || (by >= 0 && (in->imm2 & GCF_NOBOUND)) ? -1 : rd(s, a[1], S1);'
M = {
 'off': m('  if (o->arrbias && TP("arr_bias", pass_array_bias(f)))', '  if (0 && o->arrbias && TP("arr_bias", pass_array_bias(f)))'),
 'flag': m('  if (o->arrbias && TP("arr_bias", pass_array_bias(f)))', '  if (TP("arr_bias", pass_array_bias(f)))'),
 # which accesses
 'k_shift': m('      uint32_t k = hdr >> size;', '      uint32_t k = hdr;'),
 'ints': m('      if (et != VT_I32 && et != VT_I64) continue;', '      if (0) continue;'),
 'ints_get': m('      ValType et = in->op == I_ARRAY_SET ? (ValType)f->insts[ir_args(f, v)[2]].type : (ValType)in->type;',
               '      ValType et = in->op == I_ARRAY_SET ? (ValType)f->insts[ir_args(f, v)[2]].type : VT_I32;'),
 'ints_set': m('      ValType et = in->op == I_ARRAY_SET ? (ValType)f->insts[ir_args(f, v)[2]].type : (ValType)in->type;',
               '      ValType et = in->op == I_ARRAY_SET ? VT_I32 : (ValType)in->type;'),
 'wrap': m('      if (f->insts[t].op == I_WRAP) t = ir_args(f, t)[0];', '      ;'),
 'add': m('      if ((ti->op != I_SUB && ti->op != I_ADD) || ti->nargs != 2', '      if ((ti->op != I_SUB) || ti->nargs != 2'),
 'add_neg': m('      if (ti->op == I_ADD) c = 0 - c;', '      ;'),
 'sub_neg': m('      if (ti->op == I_ADD) c = 0 - c;', '      c = 0 - c;'),
 'k_match': m('      if ((c & 0xffffffffu) != k) continue;', '      if (0) continue;'),
 'k_low': m('      if ((c & 0xffffffffu) != k) continue;', '      if (c != k) continue;'),
 # the operand
 'again': m('      if ((in->op != I_ARRAY_GET && in->op != I_ARRAY_SET) || (in->imm2 & GCF_BIASARG)) continue;',
            '      if ((in->op != I_ARRAY_GET) || (in->imm2 & GCF_BIASARG)) continue;'),
 'gets': m('      if ((in->op != I_ARRAY_GET && in->op != I_ARRAY_SET) || (in->imm2 & GCF_BIASARG)) continue;',
           '      if ((in->op != I_ARRAY_SET) || (in->imm2 & GCF_BIASARG)) continue;'),
 'mark': m('      f->insts[v].imm2 |= GCF_BIASARG;', '      ;'),
 'at_index': m('      ab_insert_arg(f, v, GCF_BIAS_AT(f->insts[v].op), y);', '      ab_insert_arg(f, v, 1, y);'),
 'at_set': m('#define GCF_BIAS_AT(op) ((op) == I_ARRAY_SET ? 3u : 2u)', '#define GCF_BIAS_AT(op) 2u', 'src/ir.h'),
 'ins_before': m('  for (uint32_t k = 0; k < at; k++) f->argpool[f->nargpool++] = f->argpool[in->args_off + k];',
                 '  for (uint32_t k = 0; k + 1 < at; k++) f->argpool[f->nargpool++] = f->argpool[in->args_off + k];'),
 'ins_value': m('  f->argpool[f->nargpool++] = x;\n  for (uint32_t k = at; k < n; k++)',
                '  f->argpool[f->nargpool++] = f->argpool[in->args_off + 1];\n  for (uint32_t k = at; k < n; k++)'),
 # the wraps
 'low32': m(LOW, '      return j == 1 && !off;', C),
 # the selection
 'peek_none': m('        by = rd_peek(s, a[GCF_BIAS_AT(in->op)]);', '        by = -1;', I),
 'peek_index': m('        by = rd_peek(s, a[GCF_BIAS_AT(in->op)]);', '        by = rd_peek(s, a[1]);', I),
 'peek_slot': m('  if (!IS_SLOT(l)) return l;\n  if (s->rc_reg[0])', '  if (!IS_SLOT(l)) return -1;\n  if (s->rc_reg[0])', 'src/isel.c'),
 'peek_alias': m('static int rd_peek(Isel *s, Ref v) {\n  while (s->walias && s->walias[v]) v = ir_args(s->f, v)[0];',
                 'static int rd_peek(Isel *s, Ref v) {', 'src/isel.c'),
 'check_gone': m(IDX, '      int idx = kidx || by >= 0 ? -1 : rd(s, a[1], S1);', I),
 'scale': m('        int sc = size ? 1 : 0;', '        int sc = 0;', I),
 'scale_bytes': m('        int sc = size ? 1 : 0;', '        int sc = 1;', I),
 'signed': m('          if (in->imm2 & GCF_SIGNED) a64_ldrs_reg(B, size, wide, d, x, by, 2, sc);\n          else a64_ldr_reg(B, size, d, x, by, 2, sc);\n          commit(s, v, d);\n        } else {\n          a64_str_reg(B, size, y, x, by, 2, sc);',
             '          a64_ldr_reg(B, size, d, x, by, 2, sc);\n          commit(s, v, d);\n        } else {\n          a64_str_reg(B, size, y, x, by, 2, sc);', I),
 'unsigned': m('          if (in->imm2 & GCF_SIGNED) a64_ldrs_reg(B, size, wide, d, x, by, 2, sc);\n          else a64_ldr_reg(B, size, d, x, by, 2, sc);\n          commit(s, v, d);\n        } else {\n          a64_str_reg(B, size, y, x, by, 2, sc);',
               '          a64_ldrs_reg(B, size, wide, d, x, by, 2, sc);\n          commit(s, v, d);\n        } else {\n          a64_str_reg(B, size, y, x, by, 2, sc);', I),
 'store_base': m('          a64_str_reg(B, size, y, x, by, 2, sc);', '          a64_str_reg(B, size, y, by, x, 2, sc);', I),
 'count': m('        g_stats.arr_biased++;\n', '', I),
}
EQUIVALENT = {}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('gc', ['./test/run_gc.sh'], {}))
