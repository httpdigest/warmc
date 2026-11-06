#include "ir.h"
#include "opcodes.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>


static double now_ms(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

/* Which function a compile worker takes next.  The largest first, so that
   the last to finish is a small one -- but no more of them at once than
   their bodies' bytes fit a budget: a function's working memory, its IR and
   what the passes keep of it, is some hundreds of times its body, and a
   module's sixteen largest compiled side by side were the most memory a
   compile of Go's net/http server needed, a third more than any other time
   of it.  While the next large one does not fit, a worker takes the
   smallest left instead, and waits only when none of those is small enough
   to take beside what is in flight.  Which worker compiles a function, and
   when, is nothing its code depends on. */
typedef struct {
  pthread_mutex_t mu;
  pthread_cond_t cv;
  uint32_t front, back;       /* the take order's entries left */
  uint64_t inflight, budget, small;
  const uint32_t *cost;       /* per entry of the take order: its body's bytes */
} Sched;

static bool sched_take(Sched *s, uint32_t *k, uint64_t *c) {
  pthread_mutex_lock(&s->mu);
  for (;;) {
    if (s->front >= s->back) { pthread_mutex_unlock(&s->mu); return false; }
    uint64_t cf = s->cost[s->front];
    if (!s->inflight || s->inflight + cf <= s->budget) { *k = s->front++; *c = cf; break; }
    uint64_t cb = s->cost[s->back - 1];
    if (s->inflight + cb <= s->budget || cb <= s->small) { *k = --s->back; *c = cb; break; }
    pthread_cond_wait(&s->cv, &s->mu);
  }
  s->inflight += *c;
  pthread_mutex_unlock(&s->mu);
  return true;
}

static void sched_done(Sched *s, uint64_t c) {
  pthread_mutex_lock(&s->mu);
  s->inflight -= c;
  pthread_cond_broadcast(&s->cv);
  pthread_mutex_unlock(&s->mu);
}

typedef struct {
  Module *m; const OptOpts *opts; ObjBuilder *o; uint32_t out_bytes;
  FuncOut *outs; uint32_t first, n;
  OptStats *slots; _Atomic uint32_t *slot_next;
  _Atomic uint32_t *next;

  const uint32_t *take;
  Sched *sched;
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
    uint32_t k;
    uint64_t cost = 0;
    if (w->sched) {
      if (!sched_take(w->sched, &k, &cost)) break;
    } else {
      k = atomic_fetch_add(w->next, 1);
      if (k >= w->n) break;
    }
    uint32_t i = w->take ? w->take[k] : k;
    if (w->m->reached && !w->m->reached[w->first + i]) continue;
    double t0 = g_ftime ? now_ms() : 0;
    IrFunc *f = ir_build(w->m, w->first + i, w->opts);
    ir_verify(f, "post-build");
    ir_optimize(f, w->opts);
    ir_produce_function(f, w->o, w->opts, w->out_bytes, &w->outs[i]);
    ir_free(f);
    if (g_ftime) g_ftime[i] = now_ms() - t0;
    if (w->sched) sched_done(w->sched, cost);
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
  long cores = warmc_host_cpus();
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
  /* a ref.i31 in reached code or an initialiser that runs */
  bool i31;
  /* each function's body as reach_sum_one lists it, or NULL */
  struct RsVec *sum;
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
      case IOP_REF_I31: R->i31 = true; pops = 1; break;
      case IOP_REF_CONVERT: pops = 1; break;
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

/* What a body names, decoded for every function at once on the compile's
   threads before the walk below follows them (reach_sum_one): a list of
   tagged indices in the order reach_body meets them, which reach_body then
   applies where it would have decoded -- the same marks in the same order,
   and so the same rounds.  The walk on one thread decoding each body in
   turn was a sixteenth of compiling Go's net/http server. */
enum { RS_FN, RS_GLOB, RS_TYPE, RS_TYENT, RS_DATA, RS_FLD, RS_I31, RS_BAD };
#define RS_TAG(t, i) (((uint32_t)(t) << 29) | (i))

typedef struct RsVec { uint32_t *v; uint32_t n, cap; } RsVec;
typedef struct { const Module *m; const Reach *R; RsVec *sum; } RsJob;

static void rs_push(RsVec *s, uint32_t tag, uint32_t i) {
  if (s->n == s->cap) {
    s->cap = s->cap ? s->cap * 2 : 16;
    s->v = xrealloc(s->v, s->cap * sizeof(uint32_t));
  }
  /* an index past what a tag holds names nothing a module can have */
  s->v[s->n++] = RS_TAG(tag, i < (1u << 29) ? i : (1u << 29) - 1);
}

static void rs_type(uint32_t ti, void *ctx) { rs_push(ctx, RS_TYPE, ti); }

static void reach_sum_one(void *ctx, uint32_t k, uint32_t w) {
  (void)w;
  RsJob *j = ctx;
  const Module *m = j->m;
  uint32_t fi = m->nimported_funcs + k;
  const Func *f = &m->funcs[fi];
  RsVec *s = &j->sum[fi];
  if (!f->body || f->body_end <= f->body) return;
  Rd r = rd_make(f->body, f->body_end, m->bin);
  while (!rd_eof(&r)) {
    uint32_t op = wasm_read_op(&r);
    if (op == OP_PREFIXED(0xfb, 0x1c)) rs_push(s, RS_I31, 0);
    if (op == 0x10 || op == 0x12 || op == 0xd2) {
      rs_push(s, RS_FN, rd_u32(&r));
    } else if (op == 0x23 || op == 0x24) {
      rs_push(s, RS_GLOB, rd_u32(&r));
    } else if (op >= OP_PREFIXED(0xfb, 2) && op <= OP_PREFIXED(0xfb, 4)) {
      uint32_t ti = rd_u32(&r), fk = rd_u32(&r);
      rs_push(s, RS_TYPE, ti);
      if (j->R->fld && ti < j->R->ntypes && fk < m->types[ti].nfields)
        rs_push(s, RS_FLD, j->R->fldbase[ti] + fk);
    } else if (wasm_op_wants_gctype(op) || wasm_op_data_seg(op) >= 0) {
      Rd save = r;
      int seg = wasm_op_data_seg(op);
      uint32_t imm0 = rd_u32(&save);
      uint32_t imm1 = seg == 1 ? rd_u32(&save) : 0;
      if (wasm_op_wants_gctype(op)) rs_push(s, RS_TYENT, imm0);
      if (seg >= 0) rs_push(s, RS_DATA, seg == 0 ? imm0 : imm1);
      if (!wasm_walk_imm(&r, op, rs_type, s)) { rs_push(s, RS_BAD, 0); return; }
    } else if (!wasm_walk_imm(&r, op, rs_type, s)) {
      rs_push(s, RS_BAD, 0);
      return;
    }
  }
}

static bool reach_apply(Reach *R, const RsVec *s) {
  for (uint32_t i = 0; i < s->n; i++) {
    uint32_t v = s->v[i], x = v & ((1u << 29) - 1);
    switch (v >> 29) {
      case RS_FN: reach_fn(R, x); break;
      case RS_GLOB: reach_glob(R, x); break;
      case RS_TYPE: reach_type(x, R); break;
      case RS_TYENT: reach_type_entry(R, x); break;
      case RS_DATA: reach_data(R, x); break;
      case RS_FLD: R->fld[x] = true; break;
      case RS_I31: R->i31 = true; break;
      default: return false;
    }
  }
  return true;
}

static bool reach_body(const Module *m, uint32_t fi, Reach *R) {
  const Func *f = &m->funcs[fi];
  if (R->sum) return reach_apply(R, &R->sum[fi]);
  if (!f->body || f->body_end <= f->body) return true;
  Rd r = rd_make(f->body, f->body_end, m->bin);
  while (!rd_eof(&r)) {
    uint32_t op = wasm_read_op(&r);
    if (op == OP_PREFIXED(0xfb, 0x1c)) R->i31 = true;
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
    NULL, NULL, m, false, NULL,
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
  /* every body's references, listed at once on the compile's threads */
  {
    uint32_t nd = m->nfuncs - m->nimported_funcs;
    R.sum = xcalloc(m->nfuncs ? m->nfuncs : 1, sizeof(RsVec));
    RsJob job = { m, &R, R.sum };
    warmc_par_for(nd, compile_want_threads(m, nd), reach_sum_one, &job);
  }
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
  for (uint32_t i = 0; i < m->nfuncs; i++) xfree(R.sum[i].v);
  xfree(R.sum);
  R.sum = NULL;
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
  m->gc_no_i31 = !R.i31;
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
               take_order(m, first, n), NULL };

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
  /* the budget: twice the largest body in flight, as many of the small at
     its side as there are workers for */
  Sched sched;
  memset(&sched, 0, sizeof sched);
  pthread_mutex_init(&sched.mu, NULL);
  pthread_cond_init(&sched.cv, NULL);
  uint32_t *cost = xmalloc((n ? n : 1) * sizeof(uint32_t));
  for (uint32_t k = 0; k < n; k++) cost[k] = body_bytes(m, first + w.take[k]);
  sched.cost = cost;
  sched.back = n;
  uint64_t largest = n ? cost[0] : 0;
  const char *be = warmc_getenv("WARMC_COMPILE_BUDGET");
  sched.budget = be ? (uint64_t)atoll(be) : 2 * largest;
  if (sched.budget < largest) sched.budget = largest;
  sched.small = sched.budget / 32;
  w.sched = &sched;
  opt_effects_workers_running = true;
  ftime_begin(tm, n);
  run_pool(compile_worker, &w, nthreads);
  ftime_report(m, first, n);
  opt_effects_workers_running = false;
  o->frozen = false;
  xfree(cost);
  pthread_mutex_destroy(&sched.mu);
  pthread_cond_destroy(&sched.cv);
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

static bool sig_has_ref(const Module *m, uint32_t ti) {
  const CompType *t = &m->types[ti];
  for (uint32_t j = 0; j < t->nparams; j++)
    if (vt_is_ref(t->params[j])) return true;
  for (uint32_t j = 0; j < t->nresults; j++)
    if (vt_is_ref(t->results[j])) return true;
  return false;
}

/* Whether no object of another module's can reach this module's code.  One
   comes in as a reference across its boundary -- an imported function's
   result, an imported global, table or tag, an export's argument, what is
   stored into an exported table or global or thrown with an exported tag --
   and any reference going out -- an import's argument, an export's result,
   an exported global -- can carry one of the module's own functions, which
   another module may then call with its objects.  So none may cross either
   way, a funcref no more than any other.  What the code then meets are its
   own objects, whose header holds the instance's type base plus a slot of
   the module's numbering (codegen_rtest_slots).  An answer wrong here could
   not confuse a type: another module's ids lie outside this instance's
   range, so its objects fail every slot test -- those of a structurally
   equal type too, which ref.test must pass. */
static bool module_gc_sealed(const Module *m) {
  for (uint32_t i = 0; i < m->nfuncs; i++)
    if (m->funcs[i].imported && sig_has_ref(m, m->funcs[i].type_index)) return false;
  for (uint32_t i = 0; i < m->nglobals; i++)
    if (m->globals[i].imported && vt_is_ref(m->globals[i].type)) return false;
  for (uint32_t i = 0; i < m->ntables; i++)
    if (m->tables[i].imported) return false;
  for (uint32_t i = 0; i < m->ntags; i++)
    if (m->tags[i].imported && sig_has_ref(m, m->tags[i].type_index)) return false;
  for (uint32_t i = 0; i < m->nexports; i++) {
    const Export *e = &m->exports[i];
    switch (e->kind) {
      case EK_FUNC:
        if (sig_has_ref(m, m->funcs[e->index].type_index)) return false;
        break;
      case EK_TABLE: return false;
      case EK_GLOBAL:
        if (vt_is_ref(m->globals[e->index].type)) return false;
        break;
      case EK_TAG:
        if (sig_has_ref(m, m->tags[e->index].type_index)) return false;
        break;
      case EK_MEM: break;
    }
  }
  return true;
}

/* The topmost type up ti's supertypes, canonical, that has field k: one
   field for every type that shares it */
static uint32_t field_root(const Module *m, const uint32_t *canon, uint32_t ti, uint32_t k) {
  uint32_t t = canon[ti];
  for (uint32_t guard = 0; guard < m->ntypes; guard++) {
    const CompType *c = &m->types[t];
    if (!c->has_super || c->super >= m->ntypes) break;
    uint32_t s = canon[c->super];
    if (m->types[s].kind != CT_STRUCT || k >= m->types[s].nfields) break;
    t = s;
  }
  return t;
}

/* Module.fld_written: the fields some struct.set of the module's writes.
   In a sealed module no code outside it meets its objects, and struct.set
   is all that writes a field after struct.new, so a field none writes
   keeps its value: opt.c's gc_field_immutable takes it as immutable --
   TeaVM's Java arrays' data, a mutable field nothing sets, read anew past
   a loop's tests (pass_bce_version's bv_field_array).  A write of a
   field counts for every type that shares it by subtyping or by being
   canonically the same type (field_root); a body that does not decode
   leaves nothing known (WARMC_NO_FIELD_WRITES: nothing known). */
static void module_field_writes(Module *m) {
  xfree(m->fld_written); xfree(m->fld_base); xfree(m->fld_canon);
  m->fld_written = NULL; m->fld_base = NULL; m->fld_canon = NULL;
  if (!m->gc_sealed || !m->ntypes || warmc_getenv("WARMC_NO_FIELD_WRITES")) return;
  uint32_t *base = xmalloc(m->ntypes * sizeof(uint32_t)), n = 0;
  for (uint32_t t = 0; t < m->ntypes; t++) {
    base[t] = n;
    if (m->types[t].kind == CT_STRUCT) n += m->types[t].nfields;
  }
  uint32_t *canon = module_type_canon(m);
  uint8_t *w = xcalloc(n ? n : 1, 1);
  for (uint32_t fi = 0; fi < m->nfuncs; fi++) {
    const Func *f = &m->funcs[fi];
    if (f->imported || !f->body || f->body_end <= f->body) continue;
    Rd r = rd_make(f->body, f->body_end, m->bin);
    while (!rd_eof(&r)) {
      uint32_t op = wasm_read_op(&r);
      if (op == OP_PREFIXED(0xfb, 5)) {
        uint32_t ti = rd_u32(&r), k = rd_u32(&r);
        if (ti >= m->ntypes || m->types[ti].kind != CT_STRUCT || k >= m->types[ti].nfields) goto unknown;
        uint32_t root = field_root(m, canon, ti, k);
        w[base[root] + k] = 1;
      } else if (!wasm_walk_imm(&r, op, NULL, NULL)) {
        goto unknown;
      }
    }
  }
  m->fld_written = w; m->fld_base = base; m->fld_canon = canon;
  return;
unknown:
  xfree(w); xfree(base); xfree(canon);
}

bool module_field_unwritten(const Module *m, uint32_t ti, uint32_t k) {
  if (!m->fld_written || ti >= m->ntypes || m->types[ti].kind != CT_STRUCT ||
      k >= m->types[ti].nfields)
    return false;
  uint32_t root = field_root(m, m->fld_canon, ti, k);
  return !m->fld_written[m->fld_base[root] + k];
}

static void escape_init(Module *m, const InitExpr *e) {
  for (uint32_t i = 0; i < e->n; i++)
    if (e->ins[i].op == IOP_REF_FUNC && e->ins[i].lo < m->nfuncs)
      m->escapes[e->ins[i].lo] = 1;
}

/* Module.escapes: the functions a reference can be made to -- exported, or
   named by a segment or by a global's or a table's initialiser, the set a
   ref.func must name (validate.c) -- which is how a call from C or from
   another instance finds one. */
static void module_escape_facts(Module *m) {
  xfree(m->escapes);
  m->escapes = xcalloc(m->nfuncs ? m->nfuncs : 1, 1);
  for (uint32_t i = 0; i < m->nexports; i++)
    if (m->exports[i].kind == EK_FUNC && m->exports[i].index < m->nfuncs)
      m->escapes[m->exports[i].index] = 1;
  for (uint32_t i = 0; i < m->nelems; i++)
    for (uint32_t k = 0; k < m->elems[i].n; k++)
      escape_init(m, &m->elems[i].items[k]);
  for (uint32_t i = 0; i < m->nglobals; i++)
    if (!m->globals[i].imported) escape_init(m, &m->globals[i].init);
  for (uint32_t i = 0; i < m->ntables; i++)
    if (m->tables[i].has_init) escape_init(m, &m->tables[i].init);
}

void compile_module(Module *m, ObjBuilder *o, const OptOpts *opts) {
  g_guard_pages = opts->guard_pages;
  g_det_profile = opts->det_profile;
  g_profile = opts->profile;
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
  m->gc_sealed = opts->sealed && module_gc_sealed(m);
  module_field_writes(m);
  m->sole_instance = opts->sole_instance;
  module_escape_facts(m);
  xfree(m->pinl);
  m->pinl = xcalloc(m->nfuncs ? m->nfuncs : 1, 1);
  uint32_t out_bytes = codegen_tail_out_bytes(m);
  bool tm = warmc_getenv("WARMC_TIME") != NULL;
  module_table0_facts(m);
  module_go_resume_facts(m);
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

/* What an -O level decides; every other pass is on at every level but -O0,
   which skips the IR optimiser, and is the -f flags' to change.  Each -O
   sets all of these, so the last one given is the level. */
void compile_set_level(OptOpts *o, int level) {
  o->level = level;
  o->align = o->split = o->trampoline = o->go_sp = level >= 1;
  o->guard_pages = o->inl = o->devirt = o->unroll = o->gcpure = level >= 2;
  /* -O3: also the passes that change what a program can see */
  o->fma = o->dead_frames = o->acc_tail = o->float_reassoc = o->addr_nowrap = level >= 3;
}

/* The options every -O level starts from: the CLI's defaults, which a JIT
   (src/jit.c) compiles with as well. */
void compile_default_opts(OptOpts *o) {
  memset(o, 0, sizeof *o);
  compile_set_level(o, 2);
  o->fold = o->dce = o->cse = o->licm = o->branch = true;
  o->thread = o->nodesplit = o->keysplit = true;
  o->gssa = true;
  o->small_loop_align = true;
  o->memopt = o->bce = o->strength = true;

  o->rotate = true;
  o->addroff = true;
  o->memptr = true;
  o->regalloc = true;
  o->vectorize = true;
  o->edgetests = true;
  o->selftail = true;
  o->ldcomb = true;
  o->escape = true;
  o->pescape = true;
  o->gloop = true;
  o->loopclose = true;
  o->cpool = true;
  o->ictab = true;
  o->reassoc = true;
  o->labcache = true;
  o->barelide = true;
  o->reach = true;
  o->prune = true;
  o->reinline = true;
  o->throwjump = true;
  o->ifconv = true;
  o->mulwide = true;
  o->closed_world = true;
  o->sealed = true;
  o->once = true;
  o->go_resume = o->go_noret = true;
  o->wrap_params = true;
}

/* -O<n> or -f[no-]<pass>, as the CLI takes them: true when a is one of them
   (an unknown pass name is a fatal diagnostic), false when it is not. */
bool compile_parse_flag(OptOpts *o, const char *a) {
  if (!strcmp(a, "--wasm=3.0")) { o->wasm30 = true; return true; }
  if (a[0] == '-' && a[1] == 'O' && a[2] >= '0' && a[2] <= '3' && !a[3]) {
    compile_set_level(o, a[2] - '0');
    return true;
  }
  if (strncmp(a, "-f", 2)) return false;
  if (!strcmp(a, "-fno-loadbar")) { codegen_set_load_barrier(false); return true; }
  if (!strcmp(a, "-floadbar")) { codegen_set_load_barrier(true); return true; }
  const char *name = a + 2;
  bool on = true;
  if (!strncmp(name, "no-", 3)) { on = false; name += 3; }
  if (!strcmp(name, "fold")) o->fold = on;
  else if (!strcmp(name, "dce")) o->dce = on;
  else if (!strcmp(name, "cse")) o->cse = on;
  else if (!strcmp(name, "licm")) o->licm = on;
  else if (!strcmp(name, "branch")) o->branch = on;
  else if (!strcmp(name, "thread")) o->thread = on;
  else if (!strcmp(name, "nodesplit")) o->nodesplit = on;
  else if (!strcmp(name, "key-split")) o->keysplit = on;
  else if (!strcmp(name, "mem")) o->memopt = on;
  else if (!strcmp(name, "bce")) o->bce = on;
  else if (!strcmp(name, "strength")) o->strength = on;
  else if (!strcmp(name, "inline")) o->inl = on;
  else if (!strcmp(name, "reassoc")) o->reassoc = on;
  else if (!strcmp(name, "rotate")) o->rotate = on;
  else if (!strcmp(name, "addroff")) o->addroff = on;
  else if (!strcmp(name, "memptr")) o->memptr = on;
  else if (!strcmp(name, "align")) o->align = on;
  else if (!strcmp(name, "guard-pages")) o->guard_pages = on;
  else if (!strcmp(name, "regalloc")) o->regalloc = on;
  else if (!strcmp(name, "split")) o->split = on;
  else if (!strcmp(name, "vectorize")) o->vectorize = on;
  else if (!strcmp(name, "edge-tests")) o->edgetests = on;
  else if (!strcmp(name, "selftail")) o->selftail = on;
  else if (!strcmp(name, "ldcomb")) o->ldcomb = on;
  else if (!strcmp(name, "escape")) o->escape = on;
  else if (!strcmp(name, "partial-escape")) o->pescape = on;
  else if (!strcmp(name, "gloop")) o->gloop = on;
  else if (!strcmp(name, "loopclose")) o->loopclose = on;
  else if (!strcmp(name, "cpool")) o->cpool = on;
  else if (!strcmp(name, "ictab")) o->ictab = on;
  else if (!strcmp(name, "trampoline")) o->trampoline = on;
  else if (!strcmp(name, "go-sp")) o->go_sp = on;
  else if (!strcmp(name, "go-resume")) o->go_resume = on;
  else if (!strcmp(name, "go-noret")) o->go_noret = on;
  else if (!strcmp(name, "wrap-params")) o->wrap_params = on;
  else if (!strcmp(name, "gssa")) o->gssa = on;
  else if (!strcmp(name, "gssa-all")) { o->gssa_all = on; if (on) o->gssa = true; }
  else if (!strcmp(name, "small-loop-align")) o->small_loop_align = on;
  else if (!strcmp(name, "labcache")) o->labcache = on;
  else if (!strcmp(name, "barelide")) o->barelide = on;
  else if (!strcmp(name, "merge")) codegen_set_merge(on);
  else if (!strcmp(name, "compress-data")) codegen_set_compress_data(on);
  else if (!strcmp(name, "reach")) o->reach = on;
  else if (!strcmp(name, "prune")) o->prune = on;
  else if (!strcmp(name, "devirt")) o->devirt = on;
  else if (!strcmp(name, "reinline")) o->reinline = on;
  else if (!strcmp(name, "throw-jump")) o->throwjump = on;
  else if (!strcmp(name, "unroll")) o->unroll = on;
  else if (!strcmp(name, "ifconv")) o->ifconv = on;
  else if (!strcmp(name, "mulwide")) o->mulwide = on;
  else if (!strcmp(name, "fma")) o->fma = on;
  else if (!strcmp(name, "dead-frames")) o->dead_frames = on;
  else if (!strcmp(name, "acc-tail")) o->acc_tail = on;
  else if (!strcmp(name, "float-reassoc")) o->float_reassoc = on;
  else if (!strcmp(name, "addr-nowrap")) o->addr_nowrap = on;
  else if (!strcmp(name, "exact-trap")) o->exact_trap = on;
  else if (!strcmp(name, "gcpure")) o->gcpure = on;
  else if (!strcmp(name, "closed-world")) o->closed_world = on;
  else if (!strcmp(name, "sealed")) o->sealed = on;
  else if (!strcmp(name, "once")) o->once = on;
  else warmc_fatal("unknown pass %s", name);
  return true;
}

/* Everything the compiler keeps once a compile is over -- of the module it
   compiled, which the next compile would free, and on the calling thread
   its caches -- given back: a JIT compiles in a process that goes on
   running (src/jit.c), where what a CLI's exit would free stays. */
void compile_release(void) {
  opt_effects_release();
  codegen_release();
  validate_release();
  inl_memos_release();
  xcache_release();
}

/* What the flags together decide, once all are read: -fexact-trap keeps
   what a trap would see where it cannot see it, a global in an argument or
   a frame's stores left out, whatever their own flags and their order say. */
void compile_finish_opts(OptOpts *o) {
  if (o->exact_trap) o->trampoline = o->go_sp = o->dead_frames = o->acc_tail = false;
}
