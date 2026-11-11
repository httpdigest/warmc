"""A zero stored to a field or an element read as the zero register, a null
as an integer's 0 (regalloc.inc ra_zero_const; isel_ops.inc's field.set and
array.set, pair_zr).  Killed by test/cases_opt/zero_stores.wat's shapes."""
R, O = 'src/regalloc.inc', 'src/isel_ops.inc'
def m(path, old, new): return (path, [(old, new)])
M = {
 'null_zero': m(R, "      ((ValType)in->type != VT_I32 && (ValType)in->type != VT_I64 && !vt_is_ref((ValType)in->type)))",
                "      ((ValType)in->type != VT_I32 && (ValType)in->type != VT_I64))"),
 'field_zr': m(O, '      int y = t == VT_V128 || vt_is_float(t) ? rd(s, a[1], F0) : rd_zr(s, a[1], S1);',
               '      int y = t == VT_V128 || vt_is_float(t) ? rd(s, a[1], F0) : rd(s, a[1], S1);'),
 'elem_zr': m(O, '        y = yt == VT_V128 || vt_is_float(yt) ? rd(s, a[2], F0) : rd_zr(s, a[2], S2);',
              '        y = yt == VT_V128 || vt_is_float(yt) ? rd(s, a[2], F0) : rd(s, a[2], S2);'),
 'pair_null': m(O, "         ((ValType)in->type == VT_I64 || vt_is_ref((ValType)in->type) ? in->imm",
                "         ((ValType)in->type == VT_I64 ? in->imm"),
}
EQUIVALENT = {}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}),
          ('gc', ['./test/run_gc.sh'], {}))
