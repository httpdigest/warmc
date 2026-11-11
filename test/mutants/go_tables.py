"""Go's resume dispatch pruned through a block the pass makes (opt.c
pass_go_resume): the dispatch taken again where the new block left the
function's blocks.  Killed by test/cases_opt/go_tables.wat's answers, in
the functions of 8 and of 16 blocks."""
O = 'src/opt.c'
def m(old, new, path=O): return (path, [(old, new)])
M = {
 'moved': m('  hb = &f->blocks[h];\n  if (split) {\n    Ref two = ir_emit(f, h, I_CONST, VT_I32, NULL, 0, 2, 0);',
            '  if (split) {\n    Ref two = ir_emit(f, h, I_CONST, VT_I32, NULL, 0, 2, 0);'),
}
EQUIVALENT = {}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),)
