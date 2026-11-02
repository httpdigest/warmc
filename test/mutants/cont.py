"""Stack switching (rt/cont.inc, its hooks in rt/rt.c, rt/start.S, and the
compiler's lowering in src/ir_build.c and src/module_data.c): each rule a
continuation lives by -- consumed once, handled by the resume above it in
the same call into wasm, its frames walked and healed by the collector,
freed when nothing reaches it, crossed by exceptions and traps, its stack
its own -- mutated.  Killed by test/run_cont.sh, test/capi/cont.c and the
stack-switching proposal's spec files.  Runs on macOS: the runtimes it
builds are macOS's."""
C = 'rt/cont.inc'
R = 'rt/rt.c'
S = 'rt/start.S'
IB = 'src/ir_build.c'
def m(old, new, path=C): return (path, [(old, new)])
M = {
 # a continuation resumed twice
 'no_consume': m('  __atomic_store_n(&c->top, (Fiber *)0, __ATOMIC_RELEASE);\n  c->bottom = 0;\n'
                 '  t->handle = 0;\n',
                 '  t->handle = 0;\n'),
 # a null continuation taken for one
 'no_null': m('  if (!v) warmc_trap(TRAP_NULL_CONT);\n  ContHandle *c = handle_of(v);',
              '  ContHandle *c = handle_of(v);'),
 # a suspension taken by a switch handler, a switch by a suspension's
 'any_kind': m('      if (hs[i].kind == kind && hs[i].tag < in->md->ntags',
               '      if (hs[i].tag < in->md->ntags'),
 # a handler found past a host function's frames, in an outer call
 'past_host': m('    if (f->depth != ST->gc_invoke_depth) return 0;\n', ''),
 # the handler a suspension goes to one on
 'handler_off': m('      p->ctx.status = 1 + k;', '      p->ctx.status = 2 + k;'),
 # a taken chain's references not healed for the epoch they missed
 'no_heal': m('    if (f->seen != ST->cont.epoch && ST->gc_base) cont_scan_fiber(f, cont_heal_slot);\n'
              '    f->seen = ST->cont.epoch;\n    return;\n',
              '    f->seen = ST->cont.epoch;\n    return;\n'),
 # a suspended chain's frames not marked with its handle
 'no_scan': m('  ContHandle *c = (ContHandle *)p;\n  Fiber *f = __atomic_load_n(&c->top, __ATOMIC_ACQUIRE);\n'
              '  if (!f) return;\n',
              '  ContHandle *c = (ContHandle *)p;\n  Fiber *f = __atomic_load_n(&c->top, __ATOMIC_ACQUIRE);\n'
              '  if (f || !f) return;\n'),
 # a chain only its top fiber of which is marked
 'scan_top_only': m('    Fiber *next = f->child;\n    if (next) {', '    Fiber *next = 0;\n    if (next) {'),
 # bound values not marked
 'no_bound_scan': m('    if (f->bound_kinds[i] == CK_REF) fn(&f->bound[2 * i]);',
                    '    if (f->bound_kinds[i] == 99) fn(&f->bound[2 * i]);'),
 # chains nothing reaches kept
 'no_sweep': m('    for (Fiber *g = f; g;) {\n      Fiber *c = g->child;\n      if (next == g) next = g->next_all;\n'
               '      fiber_free(g);\n      ST->cont.nswept++;\n      g = c;\n    }\n',
               '    (void)0;\n'),
 # a chain whose handle this cycle made, freed for its handle's clear bit
 'born_ignored': m('    if (f->born == cyc || gc_is_marked(h)) {', '    if (gc_is_marked(h)) {'),
 # a chain between two handles not the collector's root
 'no_transit': m('      ST->cont.transit = top;\n      u64 h = handle_new(top, bottom, area, 0, 0);',
                 '      u64 h = handle_new(top, bottom, area, 0, 0);'),
 # a suspension's payload not rooted while its handle is allocated
 'payload_unrooted': m('    ST->cont.xfer_n = n;\n    h = (u64)(uptr)gc_alloc(',
                       '    ST->cont.xfer_n = 0;\n    h = (u64)(uptr)gc_alloc('),
 # values in transit not the collector's roots
 'no_xfer_roots': m('    if (ST->cont.xfer_kind[i] == CK_REF) fn(cont_xfer(i));',
                    '    if (ST->cont.xfer_kind[i] == 99) fn(cont_xfer(i));'),
 # bound values after the resume's, not before
 'bound_after': m('    dst[2 * (nb + i)] = from[2 * i];\n    dst[2 * (nb + i) + 1] = from[2 * i + 1];',
                  '    dst[2 * i] = from[2 * i];\n    dst[2 * i + 1] = from[2 * i + 1];'),
 # a fiber's float arguments passed as integers
 'float_as_int': m('static int ck_float(u8 k) { return k == CK_F32 || k == CK_F64 || k == CK_V128; }',
                   'static int ck_float(u8 k) { return k == CK_F32 || k == CK_F64; }'),
 # a fiber's one result taken from the integer register
 'result_int_reg': m('    const u64 *at = ck_float(rk[0]) ? &ret[WARMC_RET_V0] : &ret[WARMC_RET_X0];',
                     '    const u64 *at = &ret[WARMC_RET_X0];'),
 # a v128's high half dropped on the way through a suspension
 'v128_half': m('      for (u32 i = 0; i < 2 * s->nin; i++) dst[i] = area[i];',
                '      for (u32 i = 0; i < 2 * s->nin; i += 2) dst[i] = area[i];'),
 # an exception thrown into a continuation that never ran, raised in it
 'throw_fresh_inside': m('      if (bottom->state == F_FRESH) {\n        /* nothing of it ran',
                         '      if (0) {\n        /* nothing of it ran'),
 # the instances' stack limit left as the last fiber's
 'no_limit': m('  ST->cont.cur = f == cont_root() ? 0 : f;\n  cont_set_limit(f->limit);',
               '  ST->cont.cur = f == cont_root() ? 0 : f;'),
 # a trap's fibers left running
 'trap_keeps': m('  while (f && f != keep && f != cont_root()) {\n    Fiber *p = f->parent;\n'
                 '    cont_zombie(f);\n    f = p;\n  }\n  ST->cont.cur = keep == cont_root() ? 0 : keep;',
                 '  (void)f;'),
 # an exception leaving a fiber not taken on into its parent
 'no_unwind_cross': m('    if (pc == FIBER_RET) {\n      /* out of a fiber\'s function: on from its resume in the parent */',
                      '    if (0) {\n      /* out of a fiber\'s function: on from its resume in the parent */', R),
 # the frame walk stopping at a fiber's end
 'walk_stops': m('  while (gc_walk_frames(pc, sp, gprloc, fn)) {', '  if (gc_walk_frames(pc, sp, gprloc, fn)) {', R),
 # no new epoch at a mark's start
 'no_epoch_mark': m('  ST->gc_fast.marking = 1;\n  __atomic_store_n(&ST->cont.epoch, ST->cont.epoch + 1u, __ATOMIC_RELEASE);',
                    '  ST->gc_fast.marking = 1;', R),
 # no new epoch at a relocation's start
 'no_epoch_reloc': m('  gc_lab_recolour();\n  __atomic_store_n(&ST->cont.epoch, ST->cont.epoch + 1u, __ATOMIC_RELEASE);',
                     '  gc_lab_recolour();', R),
 # a waiting fiber taken up with no frame pointer of its own
 'no_fp': m('    ldr     x29, [x0, #GCCTX_FP]\n', '', S),
 # its float callee-saved registers not given back
 'no_fpr': m('    ldp     d8,  d9,  [x0, #GCCTX_FPR]\n', '', S),
 # the dispatch: a handler's way taken for its neighbour's
 'dispatch_off': m('    if (kind[k] != 0) { ir_add_edge(f, from, normal, NULL, 0); continue; }',
                   '    if (kind[k] != 0 || k == 1) { ir_add_edge(f, from, normal, NULL, 0); continue; }', IB),
}
EQUIVALENT = {
 'no_fp': 'x29 taken up as it was when the fiber waited: compiled code holds '
          'nothing in it across a call -- its reload cache is cleared at every '
          'call, and every frame takes its caller\'s x29 back from its own '
          'record before it returns -- so only a backtrace by frame pointers '
          '(a debugger\'s, a profiler\'s) sees it',
}
TARGETS = ['build/warmc', 'jit', 'build/warmrt-macos.a', 'build/warmgcth-macos.a']
SOURCES = ['src', 'rt']
OWN = ['rt', 'warmrt-macos.a', 'warmgcth-macos.a', 'testrun', 'capi', 'spec']
SUITES = (
    ('cont', ['./test/run_cont.sh'], {'WARMC_FLAGS': '-O2'}),
    ('capi', ['python3', 'test/capi/run.py'], {}),
    ('spec-cont', ['python3', 'test/spec/run.py', 'cont.wast'], {}),
    ('spec-resume_throw', ['python3', 'test/spec/run.py', 'resume_throw'], {}),
    ('spec-scheduler2', ['python3', 'test/spec/run.py', 'scheduler2'], {}),
)
