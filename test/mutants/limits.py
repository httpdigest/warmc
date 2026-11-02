"""What a cgroup v2 hierarchy limits the process to (rt/cgroup.inc): the
processors src/limits.c gives the compiler's and the JIT's threads, and the
heap rt.c's gc_heap_from_cgroup reserves.  Killed by test/run_limits.sh,
whose trees laid out by hand each take one of these rules to read right,
the heap's in a Linux container (podman)."""
CG = 'rt/cgroup.inc'
LI = 'src/limits.c'
R = 'rt/rt.c'
def m(old, new, path=CG): return (path, [(old, new)])
M = {
 # the process's own cgroup alone, not those above it
 'no_walk': m('    long n = cg_len(dir);\n    if (n <= topn) break;',
              '    long n = cg_len(dir);\n    if (n >= 0) break;'),
 # the mount's root taken for /, wherever it is
 'mount_root': m('  if (!(rn == 1 && mroot[0] == \'/\')) {', '  if (0) {'),
 # mountinfo's \\040 left as it is
 'no_unescape': m("    if (c == '\\\\' && p[0] >= '0'", "    if (0 && p[0] >= '0'"),
 # a cgroup v1 mount taken for the v2 one
 'fstype': m("dash[4] != 'u' || dash[5] != 'p' || dash[6] != '2' || dash[7] != ' ')",
             "dash[4] != 'u' || dash[5] != 'p')"),
 # memory.high not read
 'no_high': m('static const char *const memf[2] = { "/memory.max", "/memory.high" };\n    for (int k = 0; k < 2; k++)',
              'static const char *const memf[2] = { "/memory.max", "/memory.high" };\n    for (int k = 0; k < 1; k++)'),
 # "max" a limit
 'max_limit': m('        if (v != ~0ull && v && (!out->mem || v < out->mem)) out->mem = v;',
                '        if (v && (!out->mem || v < out->mem)) out->mem = v;'),
 # a quota of 1.5 one processor
 'quota_floor': m('    quota = (long)((lim.cpu_milli + 999) / 1000);',
                  '    quota = (long)(lim.cpu_milli / 1000);', LI),
 # the quota not heeded
 'no_quota': m('    if (quota < n) n = quota;', '    (void)quota;', LI),
 # the whole limit the heap's
 'heap_whole': m('    u64 h = lim.mem / 2;', '    u64 h = lim.mem;', R),
 # a heap under the least
 'heap_floor': m('    if (h < (4ull << 20)) h = 4ull << 20;\n', '', R),
}
EQUIVALENT = {}
TARGETS = ['build/warmc', 'build/warmrt.a']
SOURCES = ['src', 'rt']
OWN = ['rt', 'warmrt.a']
SUITES = (
    ('limits', ['./test/run_limits.sh'], {}),
)
