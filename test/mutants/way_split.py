"""The hot way split where its ways in fix a value (opt.c pass_way_split:
pass_key_split's copies by the hot way's limits).  Killed by
test/cases_opt/way_split.wat's answers and its count of the copies."""
O = 'src/opt.c'
def m(old, new, path=O): return (path, [(old, new)])
M = {
 'off': m('  if (o->waysplit && TP("way_split", pass_way_split(f))) {', '  if (0 && o->waysplit && TP("way_split", pass_way_split(f))) {'),
 # its limits
 'self': m('  KsCfg kc = { hotmode, hotmode, hotmode ? KS_HOT_TESTS : KS_TESTS,', '  KsCfg kc = { false, hotmode, hotmode ? KS_HOT_TESTS : KS_TESTS,'),
 'refs': m('  KsCfg kc = { hotmode, hotmode, hotmode ? KS_HOT_TESTS : KS_TESTS,', '  KsCfg kc = { hotmode, false, hotmode ? KS_HOT_TESTS : KS_TESTS,'),
 'tests': m('#define KS_HOT_TESTS 2', '#define KS_HOT_TESTS 3'),
 'decided': m('#define KS_HOT_DECIDED 1', '#define KS_HOT_DECIDED 2'),
 # the hot way alone, of a function a rule names a rare way in
 'rule': m('    if (hotmode && (!hs_ok || !hs.tests)) {', '    if (hotmode && !hs_ok && 0) {'),
 'hot_from': m('      if (hot && !hot[pb]) continue;', '      if (0 && hot && !hot[pb]) continue;'),
 'hot_to': m('        if (hot && !hot[S]) continue;', '        if (0 && hot && !hot[S]) continue;'),
 'hot_walk': m('      if (can[t] != 1 || (hot && !hot[t])) continue;',
              '      if (can[t] != 1) continue;'),
 # a branch on the value itself
 'self_key': m('    else if (self && ci->type == VT_I32 && !(ci->op >= I_EQ && ci->op <= I_GEU) && ci->op != I_CONST &&',
               '    else if (0 && self && ci->type == VT_I32 && !(ci->op >= I_EQ && ci->op <= I_GEU) && ci->op != I_CONST &&'),
 # whether a reference is null is no value to split on
 'null_key': m('             ci->op != I_REF_IS_NULL)\n      y = b->term_arg;', '             1)\n      y = b->term_arg;'),
}
EQUIVALENT = {}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}))
