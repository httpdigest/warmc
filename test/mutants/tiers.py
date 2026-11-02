"""src/jit.c's tiers: a module compiled at -O0 first and sent on to its
optimised code once that is in place, under running frames.  Killed by
test/capi/api.c's test_tiers (the tier-up held back until a host import
asks for it, under -O0 frames that go on to call the optimised code, take
its exceptions and hold a reference across its collections; a trap in it
under an -O0 frame; an unwinding after the tiered modules are gone), by
cases-jit-tier2 (every module's optimised code entered through its -O0
entries) and by test/run_tiers.py (Go's, GHC's and the other producers'
programs with the tier-up under their frames)."""
J = 'src/jit.c'
C = 'src/capi.c'
def m(old, new, path=J): return (path, [(old, new)])
M = {
 # the optimised code's frames unknown to the unwinder and the collector
 'no_desc': m('    warmc_rt_add_desc(job.code2->md);\n', '    (void)0;\n'),
 # a function the optimised compile left out sent to the stub that says so
 'entries_unreached': m('if (m->funcs[j].imported || (m->reached && !m->reached[j]) || !m->funcs[j].sym ||',
                        'if (m->funcs[j].imported || !m->funcs[j].sym ||'),
 # the first tier's instance without the guard pages the second's code needs
 'tier0_guard': m('    opt.guard_pages = o2.guard_pages;\n', ''),
 # Go's and GHC's signatures not the second tier's
 'tier0_gosp': m('    opt.go_sp = o2.go_sp;\n', ''),
 'tier0_tramp': m('    opt.trampoline = o2.trampoline;\n', ''),
 # the branch one instruction past the optimised code's entry
 'patch_target': m('  int64_t d = (int64_t)(to - at);\n  if ((d & 3)', '  int64_t d = (int64_t)(to + 4 - at);\n  if ((d & 3)'),
 # the optimised code's descriptor kept past its module
 'forget_md2': m('  if (forget && md2) warmc_rt_forget_desc((const ModuleDesc *)md2);\n', '', C),
}
EQUIVALENT = {}
TARGETS = ['build/warmc', 'jit']
SUITES = (
    ('capi', ['python3', 'test/capi/run.py'], {}),
    ('cases-jit-tier2', ['env', 'WARMC_JIT=1', 'WARMC_TIERED=wait', './test/run_tests.sh'], {}),
    ('tiers', ['python3', 'test/run_tiers.py'], {}),
)
