"""isel.c's s->bfi: an or of a bitfield made from x and a value whose bits
in the field are zero is bfi or bfxil.  Killed by
test/cases_opt/bitfield_insert.wat."""
O, I, IS, IR, IX = 'src/opt.c', 'src/isel_ops.inc', 'src/isel.c', 'src/ir.c', 'src/isel_x64_ops.inc'
M = {
 'bfi_field': (IS, [('''        uint64_t field = bf->imms >= bf->immr
                             ? (2ull << (bf->imms - bf->immr)) - 1
                             : ((2ull << bf->imms) - 1) << (width - bf->immr);''', '''        uint64_t field = bf->imms >= bf->immr
                             ? (2ull << bf->imms) - 1
                             : ((2ull << bf->imms) - 1) << (width - bf->immr);''')]),
 'bfi_field2': (IS, [('''                             : ((2ull << bf->imms) - 1) << (width - bf->immr);''', '''                             : ((2ull << bf->imms) - 1);''')]),
 'bfi_known': (IS, [('        if (ir_known_bits(f, o, 4) & field) continue;\n', '')]),
 'bfi_sign': (IS, [('        if (!bf->ok || bf->sign || uses[c] != 1', '        if (!bf->ok || uses[c] != 1')]),
 'bfi_uses': (IS, [('        if (!bf->ok || bf->sign || uses[c] != 1 || blocked[c]', '        if (!bf->ok || bf->sign || blocked[c]')]),
 'bfi_block': (IS, [('            f->insts[c].block != bi)\n          continue;', '            0)\n          continue;')]),
 'bfi_const': (IS, [('        if (f->insts[o].op == I_CONST || s->folded[o]) continue;', '        if (s->folded[o]) continue;')]),
 'bfi_folded': (IS, [('        if (f->insts[o].op == I_CONST || s->folded[o]) continue;', '        if (f->insts[o].op == I_CONST) continue;')]),
 'bfi_tmp': (I, [('        int t = (d == x && d != o) ? S2 : d;', '        int t = d;')]),
 'bfi_mov': (I, [('        if (t != o) a64_mov_reg(B, sf, t, o);\n', '')]),
}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}),
          ('traps', ['./test/run_traps.sh'], {}),
          ('gobench', ['python3', 'test/gobench.py', '--corpus', 'gobench', '--check'], {}),
          ('rsbench', ['python3', 'test/gobench.py', '--corpus', 'rsbench', '--check'], {}),
          ('cbench', ['python3', 'test/gobench.py', '--corpus', 'cbench', '--check'], {}))
EQUIVALENT = {
 'bfi_folded':
  "an orn's not is the only operand folded there, and known bits cannot see through a not",
}
