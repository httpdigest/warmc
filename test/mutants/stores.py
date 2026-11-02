"""Stores side by side (rt/store.h, src/capi.c): each wasm_store_t a store
of the runtime's own, entered by the thread that runs its wasm, under a
lock of its own -- and the collector's page placement that a store's small
heap showed wanting.  Killed by test/capi/stores.c (two hundred threads in
their own stores at once, a store they share, imports and references of
another store refused), by run_gc.sh's dead_medium_page and by
run_gcthread.sh's placement test.  Runs on macOS: the runtimes it builds
are macOS's."""
C = 'src/capi.c'
R = 'rt/rt.c'
WA = 'rt/wasi.c'
def m(old, new, path=C): return (path, [(old, new)])
M = {
 # every store under one lock: no two threads in wasm at once
 'one_lock': (C, [('static void store_lock(wasm_store_t *s) { pthread_mutex_lock(&s->lock); }',
                   'static pthread_mutex_t one_lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;\n'
                   'static void store_lock(wasm_store_t *s) { (void)s; pthread_mutex_lock(&one_lock); }'),
                  ('static void store_unlock(wasm_store_t *s) { pthread_mutex_unlock(&s->lock); }',
                   'static void store_unlock(wasm_store_t *s) { (void)s; pthread_mutex_unlock(&one_lock); }')]),
 # a store's calls from two threads not taken in turn
 'no_lock': (C, [('static void store_lock(wasm_store_t *s) { pthread_mutex_lock(&s->lock); }',
                  'static void store_lock(wasm_store_t *s) { (void)s; }'),
                 ('static void store_unlock(wasm_store_t *s) { pthread_mutex_unlock(&s->lock); }',
                  'static void store_unlock(wasm_store_t *s) { (void)s; }')]),
 # every store the first one's runtime store
 'one_rt': m('static struct Store *store_rt(wasm_store_t *s) { return s->rt; }',
             'static struct Store *store_rt(wasm_store_t *s) {\n'
             '  static struct Store *_Atomic first;\n'
             '  struct Store *none = NULL;\n'
             '  atomic_compare_exchange_strong(&first, &none, s->rt);\n'
             '  return atomic_load(&first);\n}'),
 # an import, and a reference in a table, of another store's taken
 'cross_import': m('    if (e->store && e->store != s) {\n      why = msgf("import',
                   '    if (0) {\n      why = msgf("import'),
 'cross_funcref': m('if (e->kind != K_FUNC || (e->store && e->store != s)) return false;',
                    'if (e->kind != K_FUNC) return false;'),
 # a thread back from another store's wasm still in that store
 'leave_stays': m('void warmc_rt_store_leave(Store *prev) { warmc_store = prev; }',
                  'void warmc_rt_store_leave(Store *prev) { (void)prev; }', R),
 # a store entered with no stack limit for its instances
 'enter_no_limit': m('  warmc_store = w;\n  warmc_rt_thread_enter(limit);\n',
                     '  warmc_store = w;\n  (void)limit;\n', R),
 # a store deleted under its collector's thread
 'no_join': m('    gc_answer_outstanding(0);\n    gcth_nudge();\n    gcth_join();\n',
              '    gc_answer_outstanding(0);\n    gcth_nudge();\n', R),
 # every store's WASI the first's
 'one_wasi': m('#define WS ((WasiStore *)ST->wasi)',
               'static WasiStore *one_wasi;\n'
               '#define WS (one_wasi ? one_wasi : (one_wasi = (WasiStore *)ST->wasi))', WA),
 # the page an allocation found too full kept as the one allocation bumps into
 'bump_keeps': m('      *pg = (u32)-1;\n      return 0;\n', '      return 0;\n', R),
 # a page of one granule into the first free granule, a run of eight or not
 'first_fit': m('    u64 g = gc_gran_take_hole();\n', '    u64 g = (u64)-1;\n', R),
 # equivalent, below
 'cap': m('  if (__atomic_add_fetch(&gcth_running, 1u, __ATOMIC_ACQ_REL) >\n'
          '      __atomic_load_n(&gcth_max, __ATOMIC_RELAXED)) {',
          '  if (__atomic_add_fetch(&gcth_running, 1u, __ATOMIC_ACQ_REL) > ~0u) {', R),
 'typed': m('  if (typed_stores(md, 0)) return 0;\n', '', R),
 'descs_gen': m('      if (u || __atomic_load_n(&descs_gen, __ATOMIC_ACQUIRE) == g) return u;',
                '      return u;', R),
 'ranges_seq': m('      if (__atomic_load_n(&s->seq, __ATOMIC_RELAXED) != q) continue;\n', '', R),
}
EQUIVALENT = {
 'cap': 'the bound on stores\' collector threads at once (gcth_max) shows in no '
        'result: a store over it collects on its own thread, as one with '
        'WARMC_GC_CONC=0 does, and the GC suites run both ways',
 'typed': 'a module\'s descriptor kept while a store holds its heap types '
          '(typed_stores): through the C API a module is held by its instances, '
          'and objects of its types outlive them only through an export, which '
          'keeps the instance',
 'descs_gen': 'a reader of the descriptors looking again when a release moved '
              'one under it: a race between two threads\' instructions that no '
              'test can schedule',
 'ranges_seq': 'the fault handler\'s reading of a slot again when it changed under '
               'it: likewise a race no test can schedule',
}
TARGETS = ['build/warmc', 'jit', 'build/warmrt-macos.a', 'build/warmgcth-macos.a']
SOURCES = ['src', 'rt']
OWN = ['rt', 'warmrt-macos.a', 'warmgcth-macos.a']
SUITES = (
    ('capi', ['python3', 'test/capi/run.py'], {}),
    ('gc', ['./test/run_gc.sh'], {}),
    ('gcthread', ['./test/run_gcthread.sh'], {}),
)
