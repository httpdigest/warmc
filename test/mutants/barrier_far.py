"""The slot a load barrier's slow path heals, where the field or element is
past an add's immediate and the load wrote the register its object was in
(isel.c emit_gc_load_stubs).  Killed by test/cases_gc/kidx.wat's far,
collected at every allocation with every object moved."""
I = 'src/isel.c'
def m(old, new, path=I): return (path, [(old, new)])
M = {
 'offset_reg': m('      int t = bf->base == S3 ? TMP0 : S3;', '      int t = S3;'),
 'offset_tmp': m('      a64_add_reg(B, SF64, S3, bf->base, t);', '      a64_add_reg(B, SF64, S3, bf->base, S3);'),
}
EQUIVALENT = {}
SUITES = (('gc', ['./test/run_gc.sh'], {}),)
