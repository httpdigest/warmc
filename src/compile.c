#include "ir.h"
#include "opcodes.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#ifdef _WIN32
unsigned long __stdcall GetActiveProcessorCount(unsigned short group);
static long host_ncpus(void) {
  unsigned long n = GetActiveProcessorCount((unsigned short)0xffff);
  return n ? (long)n : 1;
}
#else
static long host_ncpus(void) { return sysconf(_SC_NPROCESSORS_ONLN); }
#endif

static double now_ms(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

typedef struct {
  Module *m; const OptOpts *opts; ObjBuilder *o; uint32_t out_bytes;
  FuncOut *outs; uint32_t first, n;
  OptStats *slots; _Atomic uint32_t *slot_next;
  _Atomic uint32_t *next;

  const uint32_t *take;
} Worker;

/* Under WARMC_TIME, each function's time from build to emitted code, so the
   report can name the ones a compile waits for. */
static double *g_ftime;

static void ftime_begin(bool tm, uint32_t n) {
  g_ftime = tm ? xcalloc(n ? n : 1, sizeof(double)) : NULL;
}

static void ftime_report(const Module *m, uint32_t first, uint32_t n) {
  if (!g_ftime) return;
  uint32_t top[6], nt = 0;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t k = nt < 6 ? nt++ : 6;
    while (k > 0 && g_ftime[top[k - 1]] < g_ftime[i]) {
      if (k < 6) top[k] = top[k - 1];
      k--;
    }
    if (k < 6) top[k] = i;
  }
  fprintf(stderr, "slowest:");
  for (uint32_t k = 0; k < nt; k++) {
    const Func *fn = &m->funcs[first + top[k]];
    fprintf(stderr, " f%u", first + top[k]);
    if (fn->name || fn->sym) fprintf(stderr, " (%s)", fn->name ? fn->name : fn->sym);
    fprintf(stderr, " %.1fms %ub%s", g_ftime[top[k]],
            (uint32_t)(fn->body_end - fn->body), k + 1 < nt ? "," : "");
  }
  fprintf(stderr, "\n");
  xfree(g_ftime);
  g_ftime = NULL;
}

static void *compile_worker(void *arg) {
  Worker *w = arg;
  uint32_t me = atomic_fetch_add(w->slot_next, 1);
  for (;;) {
    uint32_t k = atomic_fetch_add(w->next, 1);
    if (k >= w->n) break;
    uint32_t i = w->take ? w->take[k] : k;
    if (w->m->reached && !w->m->reached[w->first + i]) continue;
    double t0 = g_ftime ? now_ms() : 0;
    IrFunc *f = ir_build(w->m, w->first + i, w->opts);
    ir_verify(f, "post-build");
    ir_optimize(f, w->opts);
    ir_produce_function(f, w->o, w->opts, w->out_bytes, &w->outs[i]);
    ir_free(f);
    if (g_ftime) g_ftime[i] = now_ms() - t0;
  }

  w->slots[me] = g_stats;
  inl_memos_release();
  xcache_release();
  return NULL;
}

typedef struct { const Module *m; uint32_t first; } SizeCtx;
static SizeCtx g_size_ctx;

static uint32_t body_bytes(const Module *m, uint32_t fi) {
  if (fi >= m->nfuncs || !m->funcs[fi].body) return 0;
  if (m->funcs[fi].body_end <= m->funcs[fi].body) return 0;
  return (uint32_t)(m->funcs[fi].body_end - m->funcs[fi].body);
}

static int by_size_desc(const void *pa, const void *pb) {
  uint32_t a = *(const uint32_t *)pa, b = *(const uint32_t *)pb;
  uint32_t sa = body_bytes(g_size_ctx.m, g_size_ctx.first + a);
  uint32_t sb = body_bytes(g_size_ctx.m, g_size_ctx.first + b);
  if (sa != sb) return sa > sb ? -1 : 1;
  return a < b ? -1 : (a > b ? 1 : 0);
}

static uint32_t *take_order(const Module *m, uint32_t first, uint32_t n) {
  uint32_t *t = xmalloc((n ? n : 1) * sizeof(uint32_t));
  for (uint32_t i = 0; i < n; i++) t[i] = i;
  g_size_ctx.m = m;
  g_size_ctx.first = first;
  qsort(t, n, sizeof(uint32_t), by_size_desc);
  return t;
}

static void *warm_worker(void *arg) {
  Worker *w = arg;
  for (;;) {
    uint32_t k = atomic_fetch_add(w->next, 1);
    if (k >= w->n) break;
    opt_effects_scan(w->m, w->opts, w->first + (w->take ? w->take[k] : k));
  }
  xcache_release();
  return NULL;
}

static void run_pool(void *(*fn)(void *), Worker *w, uint32_t nthreads) {
  pthread_t *th = xmalloc(nthreads * sizeof(pthread_t));
  uint32_t started = 0;
  for (uint32_t i = 0; i < nthreads; i++)
    if (pthread_create(&th[started], NULL, fn, w) == 0) started++;
  if (started == 0) fn(w);
  for (uint32_t i = 0; i < started; i++) pthread_join(th[i], NULL);
  xfree(th);
}

static long g_jobs;

void compile_set_jobs(long n) { g_jobs = n; }

static uint32_t want_threads(Module *m, uint32_t nfuncs) {
  long n = g_jobs;
  if (n <= 0) {
    const char *e = warmc_getenv("WARMC_JOBS");
    n = e ? atol(e) : 0;
  }
  long cores = host_ncpus();
  if (cores < 1) cores = 1;
  if (n <= 0) {
    uint64_t kb = 0;
    for (uint32_t i = 0; i < m->nfuncs; i++)
      if (m->funcs[i].body && m->funcs[i].body_end > m->funcs[i].body)
        kb += (uint64_t)(m->funcs[i].body_end - m->funcs[i].body);
    kb /= 1024;
    n = 1;
    while ((uint64_t)(n + 1) * (n + 1) <= kb) n++;
    if (n > cores) n = cores;
  }

  if ((uint32_t)n > nfuncs / 8) n = (long)(nfuncs / 8);
  if (n < 1) n = 1;
  if (n > 64) n = 64;
  return (uint32_t)n;
}

uint32_t compile_want_threads(Module *m, uint32_t nfuncs) {
  return want_threads(m, nfuncs);
}

typedef struct {
  bool *fn;    uint32_t *fstack; uint32_t fntop; uint32_t nfuncs;
  bool *glob;  uint32_t *gstack; uint32_t gntop; uint32_t nglobals;

  bool *ty;    uint32_t ntypes;

  bool *tyent;

  bool *dat;   uint32_t ndatas;

  bool *fld;   uint32_t *fldbase;
  const Module *m;
} Reach;

static bool field_read(const Reach *R, uint32_t ti, uint32_t k) {
  if (!R->fld) return true;
  for (;;) {
    if (ti >= R->ntypes || k >= R->m->types[ti].nfields) return false;
    if (R->fld[R->fldbase[ti] + k]) return true;
    if (!R->m->types[ti].has_super) return false;
    ti = R->m->types[ti].super;
  }
}

static void reach_fn(Reach *R, uint32_t fi) {
  if (fi >= R->nfuncs || R->fn[fi]) return;
  R->fn[fi] = true;
  R->fstack[R->fntop++] = fi;
}

static void reach_glob(Reach *R, uint32_t gi) {
  if (gi >= R->nglobals || R->glob[gi]) return;
  R->glob[gi] = true;
  R->gstack[R->gntop++] = gi;
}

static void reach_type(uint32_t ti, void *ctx) {
  Reach *R = ctx;
  if (ti < R->ntypes) R->ty[ti] = true;
}

static void reach_type_entry(Reach *R, uint32_t ti) {
  if (ti >= R->ntypes) return;
  R->ty[ti] = true;
  R->tyent[ti] = true;
}

static void reach_data(Reach *R, uint32_t di) {
  if (di < R->ndatas) R->dat[di] = true;
}

#define REACH_CSTACK 64

static void reach_init(Reach *R, const InitExpr *e) {
  uint32_t fn[REACH_CSTACK];
  uint32_t sp = 0;
  bool bail = false;
  for (uint32_t i = 0; i < e->n; i++) {
    const InitInsn *p = &e->ins[i];
    uint32_t pops = 0, pushes = 1, keep_from = 0;
    switch (p->op) {
      case IOP_GLOBAL_GET: reach_glob(R, (uint32_t)p->lo); break;
      case IOP_REF_FUNC: break;
      case IOP_I32_ADD: case IOP_I32_SUB: case IOP_I32_MUL:
      case IOP_I64_ADD: case IOP_I64_SUB: case IOP_I64_MUL: pops = 2; break;
      case IOP_REF_I31: case IOP_REF_CONVERT: pops = 1; break;
      case IOP_ARRAY_NEW: pops = 2; break;
      case IOP_ARRAY_NEW_DEFAULT: pops = 1; break;
      case IOP_STRUCT_NEW: case IOP_ARRAY_NEW_FIXED: pops = UINT32_MAX; break;
      default: break;
    }
    if (bail) continue;
    if (p->op == IOP_STRUCT_NEW || p->op == IOP_ARRAY_NEW_FIXED) {
      uint32_t ti = (uint32_t)p->lo;
      uint32_t n = p->op == IOP_STRUCT_NEW
                     ? (ti < R->ntypes ? R->m->types[ti].nfields : 0)
                     : (uint32_t)p->hi;
      if (n > sp) { bail = true; continue; }
      sp -= n;
      for (uint32_t j = 0; j < n; j++)
        if (fn[sp + j] != UINT32_MAX) {

          if (p->op != IOP_STRUCT_NEW || field_read(R, ti, j))
            reach_fn(R, fn[sp + j]);
        }
    } else {
      if (pops > sp) { bail = true; continue; }

      for (uint32_t j = keep_from; j < pops; j++)
        if (fn[sp - pops + j] != UINT32_MAX) reach_fn(R, fn[sp - pops + j]);
      sp -= pops;
    }
    if (pushes) {
      if (sp >= REACH_CSTACK) { bail = true; continue; }
      fn[sp++] = p->op == IOP_REF_FUNC ? (uint32_t)p->lo : UINT32_MAX;
    }
  }

  if (bail) {
    for (uint32_t i = 0; i < e->n; i++)
      if (e->ins[i].op == IOP_REF_FUNC) reach_fn(R, (uint32_t)e->ins[i].lo);
  } else {
    for (uint32_t j = 0; j < sp; j++)
      if (fn[j] != UINT32_MAX) reach_fn(R, fn[j]);
  }
}

static void reach_init_types(Reach *R, const InitExpr *e) {
  for (uint32_t i = 0; i < e->n; i++)
    switch (e->ins[i].op) {
      case IOP_STRUCT_NEW: case IOP_STRUCT_NEW_DEFAULT:
      case IOP_ARRAY_NEW: case IOP_ARRAY_NEW_DEFAULT: case IOP_ARRAY_NEW_FIXED:

        reach_type_entry(R, (uint32_t)e->ins[i].lo);
        break;
      default: break;
    }
}

static bool reach_body(const Module *m, uint32_t fi, Reach *R) {
  const Func *f = &m->funcs[fi];
  if (!f->body || f->body_end <= f->body) return true;
  Rd r = rd_make(f->body, f->body_end, m->bin);
  while (!rd_eof(&r)) {
    uint32_t op = wasm_read_op(&r);
    if (op == 0x10 || op == 0x12) {
      reach_fn(R, rd_u32(&r));
    } else if (op == 0xd2) {
      reach_fn(R, rd_u32(&r));
    } else if (op == 0x23 || op == 0x24) {
      reach_glob(R, rd_u32(&r));
    } else if (op >= OP_PREFIXED(0xfb, 2) && op <= OP_PREFIXED(0xfb, 4)) {

      uint32_t ti = rd_u32(&r), k = rd_u32(&r);
      reach_type(ti, R);
      if (R->fld && ti < R->ntypes && k < m->types[ti].nfields)
        R->fld[R->fldbase[ti] + k] = true;
    } else if (wasm_op_wants_gctype(op) || wasm_op_data_seg(op) >= 0) {

      Rd save = r;
      int seg = wasm_op_data_seg(op);
      uint32_t imm0 = rd_u32(&save);
      uint32_t imm1 = seg == 1 ? rd_u32(&save) : 0;
      if (wasm_op_wants_gctype(op)) reach_type_entry(R, imm0);
      if (seg >= 0) reach_data(R, seg == 0 ? imm0 : imm1);
      if (!wasm_walk_imm(&r, op, reach_type, R)) return false;
    } else if (!wasm_walk_imm(&r, op, reach_type, R)) {
      return false;
    }
  }
  return true;
}

static bool *module_reach(Module *m, bool closed) {
  Reach R = {
    xcalloc(m->nfuncs ? m->nfuncs : 1, sizeof(bool)),
    xcalloc(m->nfuncs ? m->nfuncs : 1, sizeof(uint32_t)), 0, m->nfuncs,
    xcalloc(m->nglobals ? m->nglobals : 1, sizeof(bool)),
    xcalloc(m->nglobals ? m->nglobals : 1, sizeof(uint32_t)), 0, m->nglobals,
    xcalloc(m->ntypes ? m->ntypes : 1, sizeof(bool)), m->ntypes,
    xcalloc(m->ntypes ? m->ntypes : 1, sizeof(bool)),
    xcalloc(m->ndatas ? m->ndatas : 1, sizeof(bool)), m->ndatas,
    NULL, NULL, m,
  };

  for (uint32_t i = 0; i < m->ndatas; i++)
    if (m->datas[i].mode == SEG_ACTIVE) R.dat[i] = true;

  R.fldbase = xcalloc(m->ntypes ? m->ntypes : 1, sizeof(uint32_t));
  uint32_t nfld = 0;
  for (uint32_t i = 0; i < m->ntypes; i++) {
    R.fldbase[i] = nfld;
    nfld += m->types[i].nfields;
  }
  R.fld = closed ? xcalloc(nfld ? nfld : 1, sizeof(bool)) : NULL;
  /* A dead global's initialiser is never evaluated (the runtime skips
     GLOBAL_NOINIT slots), so the types its struct.new/array.new name are
     seeded per LIVE global in the rounds below, not here: Kotlin modules
     carry hundreds of dead vtable globals whose types this used to keep.
     Table inits and segment offsets always run at instantiation, so their
     types are unconditional; declarative elems never run at all. */
  for (uint32_t i = 0; i < m->ntables; i++) reach_init_types(&R, &m->tables[i].init);
  for (uint32_t i = 0; i < m->ndatas; i++) reach_init_types(&R, &m->datas[i].offset);
  for (uint32_t i = 0; i < m->nelems; i++) {
    reach_init_types(&R, &m->elems[i].offset);
    if (m->elems[i].mode == SEG_DECLARATIVE) continue;
    for (uint32_t k = 0; k < m->elems[i].n; k++)
      reach_init_types(&R, &m->elems[i].items[k]);
  }

  for (uint32_t i = 0; i < m->nimported_funcs; i++) R.fn[i] = true;

  for (uint32_t i = 0; i < m->nimported_globals; i++) R.glob[i] = true;
  for (uint32_t i = 0; i < m->nexports; i++) {
    if (m->exports[i].kind == EK_FUNC) reach_fn(&R, m->exports[i].index);
    else if (m->exports[i].kind == EK_GLOBAL) reach_glob(&R, m->exports[i].index);
  }
  if (m->has_start) reach_fn(&R, m->start_func);

  for (uint32_t i = 0; i < m->ntables; i++) reach_init(&R, &m->tables[i].init);
  for (uint32_t i = 0; i < m->ndatas; i++) reach_init(&R, &m->datas[i].offset);
  for (uint32_t i = 0; i < m->nelems; i++) {
    reach_init(&R, &m->elems[i].offset);

    if (m->elems[i].mode == SEG_DECLARATIVE) continue;
    for (uint32_t k = 0; k < m->elems[i].n; k++)
      reach_init(&R, &m->elems[i].items[k]);
  }

  bool ok = true;

  for (uint32_t round = 0; ok; round++) {
    bool moved = true;
    while (ok && moved) {
      moved = false;
      while (R.fntop) {
        moved = true;
        uint32_t fi = R.fstack[--R.fntop];
        if (fi < m->nimported_funcs) continue;
        if (!reach_body(m, fi, &R)) { ok = false; break; }
      }
      while (ok && R.gntop) {
        moved = true;
        uint32_t gi = R.gstack[--R.gntop];
        if (gi >= m->nimported_globals) {
          reach_init(&R, &m->globals[gi].init);
          reach_init_types(&R, &m->globals[gi].init);
        }
      }
    }
    if (!ok) break;
    for (uint32_t gi = m->nimported_globals; gi < m->nglobals; gi++)
      if (R.glob[gi]) {
        reach_init(&R, &m->globals[gi].init);
        reach_init_types(&R, &m->globals[gi].init);
      }
    if (!R.fntop && !R.gntop) break;

    if (round > m->ntypes + 8u) { ok = false; break; }
  }

  for (uint32_t i = 0; ok && i < m->nfuncs; i++)
    if (R.fn[i]) reach_type(m->funcs[i].type_index, &R);
  xfree(R.fstack); xfree(R.gstack); xfree(R.fld); xfree(R.fldbase);
  R.fld = NULL;
  if (!ok) {
    xfree(R.fn); xfree(R.ty); xfree(R.tyent); xfree(R.dat); xfree(R.glob);
    return NULL;
  }

  if (warmc_getenv("WARMC_REACH_STATS")) {
    uint32_t nf = 0, nt = 0, ne = 0, ng = 0, nd = 0;
    for (uint32_t i = 0; i < m->nfuncs; i++) nf += R.fn[i];
    for (uint32_t i = 0; i < m->ntypes; i++) { nt += R.ty[i]; ne += R.tyent[i]; }
    for (uint32_t i = 0; i < m->nglobals; i++) ng += R.glob[i];
    for (uint32_t i = 0; i < m->ndatas; i++) nd += R.dat[i];
    fprintf(stderr, "reach: %u/%u functions, %u/%u types (%u entries), "
                    "%u/%u globals, %u/%u data segments\n",
            nf, m->nfuncs, nt, m->ntypes, ne, ng, m->nglobals, nd, m->ndatas);
  }
  m->type_live = R.ty;
  m->type_entry = R.tyent;
  m->glob_read = R.glob;
  m->data_read = R.dat;
  return R.fn;
}

static void remap_init(InitExpr *e, const uint32_t *map) {
  for (uint32_t i = 0; i < e->n; i++)
    if (e->ins[i].op == IOP_REF_FUNC) {
      uint32_t to = map[e->ins[i].lo];
      if (to == UINT32_MAX) { e->ins[i].op = IOP_REF_NULL; e->ins[i].lo = 0; }
      else e->ins[i].lo = to;
    }
}

static void module_compact_funcs(Module *m) {
  uint32_t *map = xmalloc((m->nfuncs ? m->nfuncs : 1) * sizeof(uint32_t));
  uint32_t n = 0;
  for (uint32_t i = 0; i < m->nfuncs; i++)
    map[i] = m->reached[i] ? n++ : UINT32_MAX;
  if (n == m->nfuncs) { xfree(map); return; }

  for (uint32_t i = 0; i < m->nfuncs; i++)
    if (map[i] != UINT32_MAX) m->funcs[map[i]] = m->funcs[i];
  m->nfuncs = n;

  if (m->has_start) m->start_func = map[m->start_func];
  for (uint32_t i = 0; i < m->nexports; i++)
    if (m->exports[i].kind == EK_FUNC) m->exports[i].index = map[m->exports[i].index];
  for (uint32_t i = 0; i < m->nglobals; i++) remap_init(&m->globals[i].init, map);
  for (uint32_t i = 0; i < m->ntables; i++) remap_init(&m->tables[i].init, map);
  for (uint32_t i = 0; i < m->nelems; i++) {
    remap_init(&m->elems[i].offset, map);
    for (uint32_t k = 0; k < m->elems[i].n; k++)
      remap_init(&m->elems[i].items[k], map);
  }
  m->func_map = map;

  xfree(m->reached);
  m->reached = NULL;
}

static void root_fn(const Module *m, bool *live, uint32_t *stack,
                    uint32_t *top, uint32_t fi) {
  if (fi < m->nimported_funcs || fi >= m->nfuncs || live[fi]) return;
  live[fi] = true;
  stack[(*top)++] = fi;
}

static void root_init(const Module *m, bool *live, uint32_t *stack,
                      uint32_t *top, const InitExpr *e) {
  for (uint32_t i = 0; i < e->n; i++)
    if (e->ins[i].op == IOP_REF_FUNC) root_fn(m, live, stack, top, e->ins[i].lo);
}

static void place_functions(Module *m, ObjBuilder *o, const OptOpts *opts,
                            FuncOut *outs, uint32_t first, uint32_t n) {
  if (!opts->prune) {
    for (uint32_t i = 0; i < n; i++)
      if (outs[i].sym) codegen_commit_function(o, &outs[i]);
    return;
  }
  bool *live = xcalloc(m->nfuncs ? m->nfuncs : 1, sizeof(bool));
  uint32_t *stack = xmalloc((m->nfuncs ? m->nfuncs : 1) * sizeof(uint32_t));
  uint32_t top = 0;

  for (uint32_t i = 0; i < m->nexports; i++)
    if (m->exports[i].kind == EK_FUNC)
      root_fn(m, live, stack, &top, m->exports[i].index);
  if (m->has_start) root_fn(m, live, stack, &top, m->start_func);
  for (uint32_t i = 0; i < m->nglobals; i++)
    root_init(m, live, stack, &top, &m->globals[i].init);
  for (uint32_t i = 0; i < m->ntables; i++)
    root_init(m, live, stack, &top, &m->tables[i].init);
  for (uint32_t i = 0; i < m->nelems; i++) {
    root_init(m, live, stack, &top, &m->elems[i].offset);
    if (m->elems[i].mode == SEG_DECLARATIVE) continue;
    for (uint32_t k = 0; k < m->elems[i].n; k++)
      root_init(m, live, stack, &top, &m->elems[i].items[k]);
  }

  while (top) {
    const FuncOut *fo = &outs[stack[--top] - first];
    for (uint32_t k = 0; k < fo->nrefs; k++)
      root_fn(m, live, stack, &top, fo->refs[k]);
  }

  for (uint32_t i = 0; i < n; i++) {
    if (!outs[i].sym) continue;
    if (live[first + i]) codegen_commit_function(o, &outs[i]);
    else codegen_drop_function(o, &outs[i]);
  }
  xfree(stack);

  for (uint32_t i = 0; i < first; i++) live[i] = true;

  xfree(m->reached);
  m->reached = live;
}

static bool compile_parallel(Module *m, ObjBuilder *o, const OptOpts *opts,
                             uint32_t out_bytes, bool tm) {
  uint32_t first = m->nimported_funcs;
  if (m->nfuncs <= first) return false;
  uint32_t n = m->nfuncs - first;
  uint32_t nthreads = want_threads(m, n);

  if (nthreads < 2 || opts->print_ir) return false;

  double t0 = tm ? now_ms() : 0;
  opt_init_tunables();
  FuncOut *outs = xcalloc(n, sizeof(FuncOut));
  _Atomic uint32_t cursor = 0, slot_next = 0;
  OptStats *slots = xcalloc(nthreads + 1, sizeof(OptStats));
  Worker w = { m, opts, o, out_bytes, outs, first, n, slots, &slot_next, &cursor,
               take_order(m, first, n) };

  opt_effects_begin(m, opts);
  Worker warm = w;
  warm.first = 0;
  warm.n = m->nfuncs;
  warm.take = take_order(m, 0, m->nfuncs);
  cursor = 0;
  run_pool(warm_worker, &warm, nthreads);
  double t_scan = tm ? now_ms() : 0;
  opt_effects_end(m, opts);
  xfree((void *)warm.take);
  double t_warm = tm ? now_ms() : 0;
  if (tm) fprintf(stderr, "warm: scan %.1f resolve %.1f (peak %.0f MB)\n",
                  t_scan - t0, t_warm - t_scan, peak_rss_mb());

  cursor = 0;
  o->frozen = true;
  opt_effects_workers_running = true;
  ftime_begin(tm, n);
  run_pool(compile_worker, &w, nthreads);
  ftime_report(m, first, n);
  opt_effects_workers_running = false;
  o->frozen = false;
  for (uint32_t i = 0; i <= nthreads; i++) opt_stats_add(&g_stats, &slots[i]);
  xfree(slots);
  double t1 = tm ? now_ms() : 0;

  place_functions(m, o, opts, outs, first, n);
  xfree(outs);
  xfree((void *)w.take);
  double t2 = tm ? now_ms() : 0;
  codegen_emit_module_data(m, o);
  if (tm)
    fprintf(stderr, "time: %u threads, warm %.0f produce %.0f place %.0f moddata %.0f "
                    "(peak %.0f MB)\n",
            nthreads, t_warm - t0, t1 - t_warm, t2 - t1, now_ms() - t2, peak_rss_mb());
  return true;
}

void compile_module(Module *m, ObjBuilder *o, const OptOpts *opts) {
  g_guard_pages = opts->guard_pages;
  g_det_profile = opts->det_profile;
  g_profile = opts->profile;
  /* before anything reads a function's signature: it changes some */
  if (opts->trampoline) g_stats.tramp_funcs += module_untrampoline(m);
  if (opts->go_sp) g_stats.gosp_funcs += module_go_sp_args(m);
  codegen_prepare_module(m, o);

  if (opts->reach && !m->reached) {
    m->reached = module_reach(m, opts->closed_world);
    if (m->reached) module_compact_funcs(m);
    if (m->type_entry) {
      uint32_t *slot = xmalloc((m->ntypes ? m->ntypes : 1) * sizeof(uint32_t));
      uint32_t n = 0;
      for (uint32_t i = 0; i < m->ntypes; i++)
        slot[i] = m->type_entry[i] ? n++ : UINT32_MAX;
      if (n == m->ntypes) xfree(slot);
      else m->type_slot = slot;
    }
  }
  uint32_t out_bytes = codegen_tail_out_bytes(m);

  bool tm = warmc_getenv("WARMC_TIME") != NULL;

  /* Workers read table 0's facts, so they are known before any starts. */
  module_table0_facts(m);
  codegen_prewarm_symbols(m, o);
  codegen_plan_icall_tables(m, o, opts->ictab);
  if (compile_parallel(m, o, opts, out_bytes, tm)) return;

  double t_warm0 = tm ? now_ms() : 0;
  opt_effects_begin(m, opts);
  for (uint32_t i = 0; i < m->nfuncs; i++) opt_effects_scan(m, opts, i);
  opt_effects_end(m, opts);
  double t_warm = tm ? now_ms() - t_warm0 : 0;
  double t_build = 0, t_verify = 0, t_opt = 0, t_emit = 0, t_free = 0, t0;

  uint32_t first = m->nimported_funcs;
  uint32_t nout = m->nfuncs > first ? m->nfuncs - first : 0;
  FuncOut *outs = xcalloc(nout ? nout : 1, sizeof(FuncOut));
  ftime_begin(tm, nout);
  for (uint32_t i = first; i < m->nfuncs; i++) {
    if (m->reached && !m->reached[i]) continue;
    double tf = tm ? now_ms() : 0;
    t0 = tf;
    IrFunc *f = ir_build(m, i, opts);
    if (tm) { t_build += now_ms() - t0; t0 = now_ms(); }
    if (opts->print_ir) ir_print(f, "built");
    ir_verify(f, "post-build");
    if (tm) { t_verify += now_ms() - t0; t0 = now_ms(); }
    ir_optimize(f, opts);
    if (tm) { t_opt += now_ms() - t0; t0 = now_ms(); }
    ir_produce_function(f, o, opts, out_bytes, &outs[i - first]);
    if (tm) { t_emit += now_ms() - t0; t0 = now_ms(); }
    ir_free(f);
    if (tm) { t_free += now_ms() - t0; g_ftime[i - first] = now_ms() - tf; }
  }
  ftime_report(m, first, nout);
  inl_memos_release();
  place_functions(m, o, opts, outs, first, nout);
  xfree(outs);

  t0 = tm ? now_ms() : 0;
  codegen_emit_module_data(m, o);
  opt_passtime_report();
  if (tm)
    fprintf(stderr, "time: warm %.0f build %.0f verify %.0f opt %.0f emit %.0f free %.0f moddata %.0f "
                    "(peak %.0f MB)\n",
            t_warm, t_build, t_verify, t_opt, t_emit, t_free, now_ms() - t0, peak_rss_mb());
}
