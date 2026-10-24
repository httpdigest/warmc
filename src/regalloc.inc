#include "sort.h"

static bool op_clobbers_caller_saved(uint16_t op) {
  switch (op) {
    case I_CALL: case I_CALL_INDIRECT: case I_CALL_REF:
    case I_THROW: case I_THROW_REF:
    case I_MEM_GROW: case I_MEM_COPY: case I_MEM_FILL: case I_MEM_INIT:
    case I_DATA_DROP: case I_TABLE_GET: case I_TABLE_SET:
    case I_TABLE_GROW: case I_TABLE_FILL: case I_TABLE_COPY:
    case I_TABLE_INIT: case I_ELEM_DROP:

    case I_TABLE_SIZE:

    case I_STRUCT_NEW: case I_ARRAY_NEW: case I_ARRAY_NEW_SEG:
    case I_ARRAY_OP: case I_REF_TEST:
    case I_ANY_OF_EXTERN: case I_EXTERN_OF_ANY:
      return true;
    default:
      return false;
  }
}

/* A throw allocates the exception on the collector's heap, and once its
   payload is in the object the runtime may collect there -- in a program that
   allocates nothing else on that heap, nowhere else would. */
static bool op_is_safepoint(uint16_t op) {
  switch (op) {
    case I_CALL: case I_CALL_INDIRECT: case I_CALL_REF:
    case I_STRUCT_NEW: case I_ARRAY_NEW: case I_ARRAY_NEW_SEG:
    case I_THROW:
      return true;
    default:
      return false;
  }
}

#if RA_HAVE_RECOMP
/* An add or sub immediate for k: the 12 bits, shifted by 12 or not, and
   whether it is the negation that fits (sub for add, add for sub). */
static bool recomp_addsub_imm(uint64_t k, bool w64, uint32_t *imm, int *lsl, bool *neg) {
  uint64_t m = w64 ? ~0ull : 0xffffffffull;
  for (int n = 0; n < 2; n++) {
    uint64_t x = (n ? (uint64_t)0 - k : k) & m;
    *neg = n != 0;
    if (x < 4096) { *imm = (uint32_t)x; *lsl = 0; return true; }
    if ((x & 0xfff) == 0 && x < (1ull << 24)) { *imm = (uint32_t)(x >> 12); *lsl = 1; return true; }
  }
  return false;
}

/* The shape of a value a read can recompute instead of reloading: one
   integer operation of a register operand x and either another, y, or an
   immediate c the instruction encodes.  Its op, or I_NOP. */
static IrOp recomp_shape(IrFunc *f, Ref v, Ref *x, Ref *y, uint64_t *c) {
  const Inst *in = &f->insts[v];
  ValType t = (ValType)in->type;
  if (in->nargs != 2 || (t != VT_I32 && t != VT_I64)) return I_NOP;
  IrOp op = (IrOp)in->op;
  const Ref *a = ir_args(f, v);
  bool k0 = f->insts[a[0]].op == I_CONST, k1 = f->insts[a[1]].op == I_CONST;
  bool w64 = t == VT_I64;
  switch (op) {
    case I_ADD: case I_SUB: case I_AND: case I_OR: case I_XOR:
    case I_SHL: case I_SHRU: case I_SHRS:
      break;
    default:
      return I_NOP;
  }
  if (k0 && k1) return I_NOP;
  bool comm = op == I_ADD || op == I_AND || op == I_OR || op == I_XOR;
  if (k0 && !comm) return I_NOP;
  *x = k0 ? a[1] : a[0];
  *y = REF_NONE;
  if (!k0 && !k1) {
    if (op == I_SHL || op == I_SHRU || op == I_SHRS) return I_NOP;
    *y = a[1];
    return op;
  }
  *c = f->insts[k0 ? a[0] : a[1]].imm & (w64 ? ~0ull : 0xffffffffull);
  uint32_t imm, enc;
  int lsl;
  bool neg;
  switch (op) {
    case I_ADD: case I_SUB:
      return recomp_addsub_imm(*c, w64, &imm, &lsl, &neg) ? op : I_NOP;
    case I_AND: case I_OR: case I_XOR:
      return a64_logical_imm(*c, w64 ? SF64 : SF32, &enc) ? op : I_NOP;
    default:
      return op;
  }
}

#endif

static bool value_needs_location(IrFunc *f, Ref v) {
  Inst *in = &f->insts[v];
  return in->type != 0;
}

static bool skip_location(Isel *s, Ref v) {
  if (s->imm_fold && s->imm_fold[v]) return true;
  if (s->fused && s->fused[v]) return true;
  if (s->addr_fuse && s->addr_fuse[v]) return true;
  if (s->ptr_fuse && s->ptr_fuse[v]) return true;
  if (s->folded && s->folded[v]) return true;
  return false;
}

static bool remat_ok(Isel *s, Ref v) {
  Inst *in = &s->f->insts[v];
  if (s->no_remat[v]) return false;

  if (in->op == I_MEM_BASE)
    return !(in->imm2 != 0 && in->imm == 0) && in->imm <= 0xfff;

  /* An immutable global's value can never change (validation refuses the
     global.set), so re-loading it IS the value: one vmctx load per use, no
     def store, no slot.  For a ref the global stays a GC root for as long
     as the module lives and the collector never moves objects, so a copy
     that the safepoint tables cannot see keeps pointing at a live, pinned
     object.  Imported globals pay a second load for the indirection and
     are left out. */
  if (in->op == I_GLOBAL_GET)
    return s->remat_glob && !s->f->m->globals[in->imm].mutable_ &&
           !s->f->m->globals[in->imm].imported;
  if (in->op != I_CONST) return false;
  if ((ValType)in->type == VT_V128)
    return s->remat_v128 && ir_vconst_cost(in->imm, in->imm2) == 1;
  {
    /* A float rematerialises through a GPR (load_imm + fmov), so it only
       stays profitable at a single move; integers take movz+movk chains up
       to the knob. */
    int cap = vt_is_float((ValType)in->type) ? 1 : s->remat_cost;
    if (ir_const_cost(in->imm, vt_is_64((ValType)in->type)) <= cap)
      return true;
  }
  if (vt_is_float((ValType)in->type) && s->remat_fp)
    return isel_fp_remat_ok(s, in);
  return false;
}

/* Weight multiplier for a remat-able value when it competes for a register:
   a discounted weight loses eviction fights, which is right because its
   "spill" costs no slot and no memory traffic.  Multi-instruction constants
   pay their sequence at every use, so they discount less. */
static double remat_discount(Isel *s, Ref v) {
  Inst *in = &s->f->insts[v];
  if (in->op == I_CONST && !vt_is_float((ValType)in->type) &&
      (ValType)in->type != VT_V128 &&
      ir_const_cost(in->imm, vt_is_64((ValType)in->type)) > 1)
    return s->remat2_w;
  return 0.5;
}

/* A constant zero is never worth a callee-saved register: that costs a
   save and a restore on every run of the function, where each use -- an
   argument, a result, a stored value -- reads the zero register or makes
   the zero in one move, no more than the copy from a register was.  Go
   passes PC_B = 0 to every call and returns 0 from nearly every function:
   12115 of stdgo/recurse's 16809 calls read that zero from x22, which half
   of its functions saved and restored for it.  So a zero across a call is
   rematerialised, and any other takes a caller-saved register or none --
   on a64, where uses read the zero register; x64 has none to read. */
static bool ra_zero_const(Isel *s, Ref v, int cls) {
  const Inst *in = &s->f->insts[v];
  if (!RA_ZERO_REG || cls || in->op != I_CONST ||
      ((ValType)in->type != VT_I32 && (ValType)in->type != VT_I64))
    return false;
  uint64_t m = (ValType)in->type == VT_I64 ? in->imm : (in->imm & 0xffffffffu);
  return m == 0 && remat_ok(s, v);
}

static uint32_t take_slot(Isel *s, Ref v, uint32_t *slot) {
  if ((ValType)s->f->insts[v].type == VT_V128) {
    if (*slot & 1) (*slot)++;
    uint32_t at = *slot;
    *slot += 2;
    return at;
  }
  return (*slot)++;
}

static void regalloc_naive(Isel *s) {
  IrFunc *f = s->f;
  uint32_t slot = 0;
  for (uint32_t i = 0; i < f->ninsts; i++)
    if (value_needs_location(f, i) && !skip_location(s, i))
      s->loc[i] = SLOT(take_slot(s, i, &slot));
  s->nslots = slot;
}

/* A value read once in a loop weighs ten; see the spare registers in
   regalloc_linear. */
#define RA_SPARE_MIN_W 10.0

static int op_tied_arg(uint16_t op) {
  switch (op) {
    case I_VSEL: return 0;
    /* A wrap whose source dies here can stay in the source's register,
       where it costs nothing when the high half is already zero -- the
       count of a clz or popcnt, the 32-bit result Go and TinyGo keep in an
       i64 -- and one in-place uxtw otherwise. */
    case I_WRAP: return 0;
    /* A lane insert whose vector dies here inserts in place; in a register
       of its own it first copies the whole vector, once per lane of a
       gather built up an insert at a time. */
    case I_VREPLACE: return 0;
    /* A multiply-add accumulates into its destination (fmla; x64's 231
       forms, scalar too), so an addend that dies here is the destination
       and needs no copy first; a64's scalar fmadd has a register of its
       own for it. */
    case I_VFMADD: return 2;
    /* umlal likewise */
    case I_VEXTMLA: return 0;
    case I_FMA: return RA_FMA_TIED ? 2 : -1;
    default: return -1;
  }
}

static Ref tied_operand_of(IrFunc *f, Ref def) {
  int ti = op_tied_arg(f->insts[def].op);
  if (ti < 0 || (uint32_t)ti >= f->insts[def].nargs) return REF_NONE;
  return ir_args(f, def)[ti];
}

typedef struct {
  int32_t lo, hi;
} Range;

typedef struct {
  Ref value;
  int32_t start, end;
  uint32_t rfirst, rcount;
  uint8_t cls;
  uint8_t crosses_call;

  uint8_t caller_saved_ok;
  int32_t reg;
} Interval;

/* How many positions an interval holds a register for: its ranges, holes
   left out. */
static double iv_span(const Interval *iv, const Range *R) {
  if (!iv->rcount) return (double)(iv->end - iv->start + 1);
  double n = 0;
  for (uint32_t y = 0; y < iv->rcount; y++) n += R[iv->rfirst + y].hi - R[iv->rfirst + y].lo + 1;
  return n > 0 ? n : 1;
}

/* The same, each position weighted by its block's frequency: cumf is the
   prefix sum of those weights over the scan's positions. */
static double iv_wspan(const Interval *iv, const Range *R, const double *cumf) {
  double n = 0;
  if (!iv->rcount) n = cumf[iv->end + 1] - cumf[iv->start];
  else
    for (uint32_t y = 0; y < iv->rcount; y++)
      n += cumf[R[iv->rfirst + y].hi + 1] - cumf[R[iv->rfirst + y].lo];
  return n > 1e-9 ? n : 1e-9;
}

/* The same over contested positions only, where more values of the class
   are live than it has registers (cumc, frequency weighted like cumf). */
static double iv_cspan(const Interval *iv, const Range *R, const double *cumc) {
  double n = 0;
  if (!iv->rcount) n = cumc[iv->end + 1] - cumc[iv->start];
  else
    for (uint32_t y = 0; y < iv->rcount; y++)
      n += cumc[R[iv->rfirst + y].hi + 1] - cumc[R[iv->rfirst + y].lo];
  return n;
}

/* Does the interval live at frequency f0 at every position it holds? */
static bool iv_uniform(const Interval *iv, const Range *R, const double *cumf,
                       double f0) {
  double sp = iv_span(iv, R), ws = iv_wspan(iv, R, cumf);
  return ws <= f0 * sp * (1 + 1e-9) && ws >= f0 * sp * (1 - 1e-9);
}

/* Eviction rules, for the scan's passes (see "the scan, under several
   eviction rules" in regalloc_linear).  ev_ratio says by how much cur
   outranks a conflicting interval V under a rule; above 1, V may be evicted
   for it, and the largest ratio picks the victim.
     EV_WEIGHT     spill weights: references, frequency weighted.
     EV_DENSITY    weight per position held, holes left out.
     EV_WDENSITY   the same with the positions frequency weighted, so a value
                   passing through a hot loop pays for the register there.
     EV_UNIFORM    density where both live at cur's frequency throughout --
                   one block, one loop body -- where a position costs the
                   same everywhere and the static frequency guess cannot
                   mislead it; weight elsewhere.
     EV_CONTESTED  frequency-weighted density over only the positions where
                   the class has more live values than registers: a value
                   holds a register through a quiet stretch for free. */
enum { EV_WEIGHT, EV_DENSITY, EV_WDENSITY, EV_UNIFORM, EV_CONTESTED };

/* A rule other than weight must price below this share of the weight rule's
   assignment to replace it. */
#define RA_TRIAL_MARGIN 0.9

static double ev_ratio(int rule, const Interval *cur, double cw,
                       const Interval *V, double w, const Range *R,
                       const double *cumf, const double *cumc) {
  if (w <= 0) return 1e300;
  double f0 = cumf[cur->start + 1] - cumf[cur->start];
  switch (rule) {
    case EV_DENSITY:
      return (cw / iv_span(cur, R)) / (w / iv_span(V, R));
    case EV_WDENSITY:
      return (cw / iv_wspan(cur, R, cumf)) / (w / iv_wspan(V, R, cumf));
    case EV_UNIFORM:
      if (iv_uniform(cur, R, cumf, f0) && iv_uniform(V, R, cumf, f0))
        return (cw / iv_span(cur, R)) / (w / iv_span(V, R));
      return cw / w;
    case EV_CONTESTED: {
      double cc = iv_cspan(cur, R, cumc);
      if (cc <= 0) return cw / w;
      double vc = iv_cspan(V, R, cumc);
      return (cw / cc) / (w / (vc > f0 ? vc : f0));
    }
    default:
      return cw / w;
  }
}

static void spill_debug(Isel *s, const char *kind, const Interval *cur,
                        double w) {
  Inst *in = &s->f->insts[cur->value];
  int cost = -1;
  if (in->op == I_CONST && !vt_is_float((ValType)in->type) &&
      (ValType)in->type != VT_V128)
    cost = ir_const_cost(in->imm, vt_is_64((ValType)in->type));
  fprintf(stderr, "%s[%u] %s len=%d w=%.0f xcall=%d ref=%d cost=%d nr=%d",
          kind, s->f->func_index, ir_op_name(in->op), cur->end - cur->start, w,
          cur->crosses_call, vt_is_ref((ValType)in->type), cost,
          s->no_remat[cur->value]);
  if (in->op == I_GLOBAL_GET) {
    const Global *g = &s->f->m->globals[in->imm];
    fprintf(stderr, " glob=%llu mut=%d imp=%d", (unsigned long long)in->imm,
            g->mutable_, g->imported);
  }
  fputc('\n', stderr);
}

static bool iv_overlaps_from(const Interval *a, const Interval *b,
                             const Range *R, uint32_t bi) {
  if (a->start > b->end || b->start > a->end) return false;
  if (!a->rcount || !b->rcount) return true;
  uint32_t i = 0, j = bi;
  while (i < a->rcount && j < b->rcount) {
    const Range *x = &R[a->rfirst + i], *y = &R[b->rfirst + j];
    if (x->lo <= y->hi && y->lo <= x->hi) return true;
    if (x->hi < y->hi) i++; else j++;
  }
  return false;
}

static bool iv_overlaps(const Interval *a, const Interval *b, const Range *R) {
  return iv_overlaps_from(a, b, R, 0);
}

#define RANGE_BUDGET ((uint64_t)8 * 1024 * 1024)

typedef struct {
  int32_t lo, hi;
  int32_t owner;
} Occ;

typedef struct {
  Occ *v;
  uint32_t n, cap, dead;
} OccHeap;

static void occ_push(OccHeap *h, int32_t lo, int32_t hi, int32_t owner) {
  if (h->n == h->cap) {
    h->cap = h->cap ? h->cap * 2 : 16;
    h->v = xrealloc(h->v, h->cap * sizeof(Occ));
  }
  uint32_t i = h->n++;
  h->v[i].lo = lo; h->v[i].hi = hi; h->v[i].owner = owner;
  while (i) {
    uint32_t p = (i - 1) / 2;
    if (h->v[p].lo <= h->v[i].lo) break;
    Occ t = h->v[p]; h->v[p] = h->v[i]; h->v[i] = t;
    i = p;
  }
}

static void occ_rebuild(OccHeap *h, const uint8_t *gone, int32_t floor_) {
  uint32_t k = 0;
  for (uint32_t i = 0; i < h->n; i++)
    if (h->v[i].hi >= floor_ && !gone[h->v[i].owner]) h->v[k++] = h->v[i];
  h->n = k;
  h->dead = 0;
  for (int32_t i = (int32_t)k / 2 - 1; i >= 0; i--) {
    uint32_t p = (uint32_t)i;
    for (;;) {
      uint32_t l = 2 * p + 1, r = l + 1, m = p;
      if (l < k && h->v[l].lo < h->v[m].lo) m = l;
      if (r < k && h->v[r].lo < h->v[m].lo) m = r;
      if (m == p) break;
      Occ t = h->v[m]; h->v[m] = h->v[p]; h->v[p] = t;
      p = m;
    }
  }
}

/* An interval's ranges are sorted and disjoint (the hole pass merges them),
   so their ends rise too: the only one that can meet [lo, hi] is the first
   that ends at or after lo.  A long-lived value in a large function has
   thousands, and this is asked once per range a register holds. */
static bool occ_hits(const Interval *cur, const Range *R, int32_t lo, int32_t hi) {
  if (!cur->rcount) return cur->start <= hi && lo <= cur->end;
  const Range *x = &R[cur->rfirst];
  uint32_t n = cur->rcount;
  if (n <= 8) {
    for (uint32_t i = 0; i < n; i++) {
      if (x[i].lo > hi) break;
      if (lo <= x[i].hi) return true;
    }
    return false;
  }
  uint32_t a = 0, b = n;
  while (a < b) {
    uint32_t m = a + (b - a) / 2;
    if (x[m].hi < lo) a = m + 1; else b = m;
  }
  return a < n && x[a].lo <= hi;
}

/* 0 = free, 1 = exactly one interval conflicts (in *victim), 2 = more.
   Ranges whose hi is behind cur, and ranges of an interval that has since been
   spilled, are dead: they are skipped here and dropped by the next rebuild. */
typedef struct { uint32_t *v; uint32_t cap; } OStack;

static int occ_conflicts(OccHeap *h, const Interval *cur, const Range *R,
                         const uint8_t *gone, OStack *os, int32_t *victim) {
  *victim = -1;
  if (!h->n) return 0;
  /* each heap node goes on the stack at most once */
  if (os->cap < h->n + 1) {
    os->cap = h->n * 2 + 2;
    os->v = xrealloc(os->v, os->cap * sizeof(uint32_t));
  }
  uint32_t *stack = os->v;
  uint32_t sp = 0, seen_dead = 0;
  stack[sp++] = 0;
  int found = 0;
  while (sp) {
    uint32_t i = stack[--sp];
    Occ *o = &h->v[i];
    if (o->lo > cur->end) continue;
    if (o->hi < cur->start || gone[o->owner]) {
      seen_dead++;
    } else if (occ_hits(cur, R, o->lo, o->hi)) {
      if (!found) { *victim = o->owner; found = 1; }
      else if (*victim != o->owner) { found = 2; break; }
    }
    uint32_t l = 2 * i + 1, r = l + 1;
    if (l < h->n) stack[sp++] = l;
    if (r < h->n) stack[sp++] = r;
  }
  h->dead += seen_dead;
  return found;
}

#define CSA_BUDGET ((uint64_t)8 * 1024 * 1024)

/* Price of keeping this interval in a caller-saved register: two frame
   operations per clobber site it is live across, loop-depth weighted the way
   spill weights are.  Allocation sites whose helper call lives in the
   out-of-line slow arm are discounted, because the fast path never pays.
   Sites are found by binary search in the position-sorted call list; ranges
   over-approximate liveness, so the price only ever errs upward. */
static double csa_crossing_cost(const Interval *cur, const Range *R,
                                int32_t def_pos, const int32_t *call_pos,
                                const uint8_t *call_depth,
                                const uint8_t *call_cold,
                                const float *call_share, uint32_t ncalls,
                                double cold_div, uint64_t *work,
                                uint8_t *deepest) {
  double c = 0;
  if (deepest) *deepest = 0;
  Range span = { cur->start, cur->end };
  uint32_t nr = cur->rcount ? cur->rcount : 1;
  for (uint32_t i = 0; i < nr; i++) {
    const Range *r = cur->rcount ? &R[cur->rfirst + i] : &span;
    int32_t lo = r->lo, hi = r->hi - 1;
    if (hi < lo) continue;
    uint32_t a = 0, b = ncalls;
    while (a < b) {
      uint32_t m = (a + b) / 2;
      if (call_pos[m] < lo) a = m + 1; else b = m;
      (*work)++;
    }
    for (; a < ncalls && call_pos[a] <= hi; a++) {
      (*work)++;
      if (call_pos[a] == def_pos) continue;
      if (deepest && call_depth[a] > *deepest) *deepest = call_depth[a];
      double w = call_share ? call_share[a] : 1.0;
      for (int k = 0; k < call_depth[a]; k++) w *= 10.0;
      if (call_cold[a]) w /= cold_div;
      c += 2.0 * w;
    }
  }
  return c;
}

typedef struct {
  const uint32_t *web;
  uint32_t *stamp;
  uint32_t stamp_ctr;
} CsaListCtx;

/* A throw never returns, so a register unread on the unwind path (pads read
   their pinned slots) needs no save; a table.size on memory 0 lowers inline
   with no call at all.  Both get empty lists, keeping the built-equals-
   emitted accounting exact. */
static void csa_list_sites(Isel *s, IrFunc *f, Ref site, const Ref *live,
                           uint32_t nlive, void *vctx) {
  CsaListCtx *ctx = vctx;
  uint16_t op = f->insts[site].op;
  if (op == I_THROW || op == I_THROW_REF) return;
  if (op == I_TABLE_SIZE && f->insts[site].imm == 0) return;
  uint32_t stamp = ++ctx->stamp_ctr;
  uint32_t first = s->ncsa_vals;
  for (uint32_t k = 0; k < nlive; k++) {
    uint32_t v = live[k];
    if (v == site) continue;
    uint32_t rep = ctx->web[v];
    if (!s->csa_on[rep] || ctx->stamp[rep] == stamp) continue;
    ctx->stamp[rep] = stamp;
    if (s->ncsa_vals == s->cap_csa_vals) {
      s->cap_csa_vals = s->cap_csa_vals ? s->cap_csa_vals * 2 : 16;
      s->csa_vals = xrealloc(s->csa_vals, s->cap_csa_vals * sizeof(Ref));
    }
    s->csa_vals[s->ncsa_vals++] = rep;
  }
  s->csa_first[site] = first;
  s->csa_n[site] = s->ncsa_vals - first;
}

/* One block's range for one value, filed under the value's web. */
typedef struct { uint32_t rep; int32_t lo, hi; } BlkRange;

#define BLK_RANGE_LESS(x, y)                                                   \
  ((x)->rep != (y)->rep ? (x)->rep < (y)->rep                                  \
   : (x)->lo != (y)->lo ? (x)->lo < (y)->lo : (x)->hi < (y)->hi)
WARMC_SORT(sort_blk_ranges, BlkRange, BLK_RANGE_LESS)

/* by start, then by value: no two compare equal, so any sort agrees */
#define INTERVAL_LESS(x, y)                                                    \
  ((x)->start != (y)->start ? (x)->start < (y)->start : (x)->value < (y)->value)
WARMC_SORT(sort_intervals, Interval, INTERVAL_LESS)

static void extend(Interval *iv, int32_t at) {
  if (at < iv->start) iv->start = at;
  if (at > iv->end) iv->end = at;
}

static Ref eff_arg(Isel *s, IrFunc *f, Ref a) {
  if (s->addr_fuse && s->addr_fuse[a]) return ir_args(f, a)[0];
  return a;
}

static uint32_t eff_uses_at(Isel *s, IrFunc *f, Ref u, Ref *out, uint32_t k,
                            uint32_t cap) {
  Inst *in = &f->insts[u];
  Ref *a = ir_args(f, u);
  for (uint32_t j = 0; j < in->nargs; j++) {
    Ref x = a[j];
    if (s->folded && s->folded[x]) {
      k = eff_uses_at(s, f, x, out, k, cap);
    } else {
      if (k >= cap) warmc_fatal("internal: effective-use buffer overflow");
      out[k++] = x;
    }
  }
  return k;
}

static uint32_t eff_uses(Isel *s, IrFunc *f, Ref u, Ref *out, uint32_t cap) {
  return eff_uses_at(s, f, u, out, 0, cap);
}

static uint32_t eff_use_cap(IrFunc *f) {
  uint32_t maxargs = 4;
  for (uint32_t i = 0; i < f->ninsts; i++)
    if (f->insts[i].nargs > maxargs) maxargs = f->insts[i].nargs;
  if (f->made_maxargs > maxargs) maxargs = f->made_maxargs;
  return maxargs * 8 + 32;
}

/* Live-in and live-out sets: per block, a sorted array of the values live on
   entry and on exit, in s->lv_in and s->lv_out at the offsets s->lv_in_off
   and s->lv_out_off give (nblocks + 1 each; blocks outside the order get
   empty sets).

   Most values die in the block that defines them, so these are small beside
   the function -- a generated adapter of 78,000 blocks has 1.3 values live
   into each on average -- while bit vectors of every value per block cost
   blocks times values in memory and again in every sweep: 2.6 GB and most of
   a second there.  So each value is followed up from each of its uses to its
   definition instead (path exploration), and the work is the size of the
   sets.  Values are taken in increasing order, so each block's array comes
   out sorted, the order a bit vector would give them in.

   The uses are the ones the bit-vector form counted: each instruction's
   effective operands at its index, and at the block's end the branch's
   operand (a fused compare's own operands), the returned values and each
   edge's arguments.  A param is defined on entry to its block, so it is
   never live into it. */
#define LV_END UINT32_MAX

typedef struct { Ref v; uint32_t b, pos; } LvUse;

static void lv_note_use(LvUse **u, uint32_t *nu, uint32_t *cap, Ref v,
                        uint32_t b, uint32_t pos) {
  if (*nu == *cap) {
    *cap = *cap ? *cap * 2 : 256;
    *u = xrealloc(*u, *cap * sizeof(LvUse));
  }
  (*u)[(*nu)++] = (LvUse){ v, b, pos };
}

static void compute_liveness_now(Isel *s) {
  IrFunc *f = s->f;
  uint32_t n = f->ninsts, nb = f->nblocks;
  uint32_t eucap = eff_use_cap(f);
  Ref *eu = xmalloc(eucap * sizeof(Ref));

  /* Where each value is defined: its block and its index there, -1 for a
     param.  A value no block in the order defines stays UINT32_MAX and is
     live all the way up to the entry, as it was in the bit vectors. */
  uint32_t *dblk = xmalloc((n ? n : 1) * sizeof(uint32_t));
  int32_t *dpos = xmalloc((n ? n : 1) * sizeof(int32_t));
  for (uint32_t i = 0; i < n; i++) dblk[i] = UINT32_MAX;
  uint32_t *poff = xcalloc((size_t)nb + 1, sizeof(uint32_t));
  LvUse *use = NULL;
  uint32_t nuse = 0, capuse = 0;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t p = 0; p < b->nparams; p++) {
      dblk[b->params[p]] = bi;
      dpos[b->params[p]] = -1;
    }
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref r2 = b->insts[i];
      uint32_t ne = eff_uses(s, f, r2, eu, eucap);
      for (uint32_t j = 0; j < ne; j++) lv_note_use(&use, &nuse, &capuse, eu[j], bi, i);
      dblk[r2] = bi;
      dpos[r2] = (int32_t)i;
    }
    if (b->term == T_BRIF && s->fused && s->fused[b->term_arg]) {
      uint32_t ne = eff_uses(s, f, b->term_arg, eu, eucap);
      for (uint32_t j = 0; j < ne; j++)
        lv_note_use(&use, &nuse, &capuse, eff_arg(s, f, eu[j]), bi, LV_END);
    } else if (b->term == T_BRIF || b->term == T_BRTABLE) {
      lv_note_use(&use, &nuse, &capuse, b->term_arg, bi, LV_END);
    }
    for (uint32_t i = 0; i < b->nret; i++)
      lv_note_use(&use, &nuse, &capuse, b->ret_args[i], bi, LV_END);
    for (uint32_t k = 0; k < b->nsuccs; k++) {
      poff[b->succs[k].target + 1]++;
      for (uint32_t j = 0; j < b->succs[k].nargs; j++)
        lv_note_use(&use, &nuse, &capuse, b->succs[k].args[j], bi, LV_END);
    }
  }

  /* Predecessors along the order's edges, one entry per edge. */
  for (uint32_t i = 0; i < nb; i++) poff[i + 1] += poff[i];
  uint32_t *pred = xmalloc((poff[nb] ? poff[nb] : 1) * sizeof(uint32_t));
  uint32_t *pcur = xmalloc(((size_t)nb + 1) * sizeof(uint32_t));
  memcpy(pcur, poff, ((size_t)nb + 1) * sizeof(uint32_t));
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t k = 0; k < b->nsuccs; k++) pred[pcur[b->succs[k].target]++] = bi;
  }

  /* The uses filed by value, in the order they were met. */
  uint32_t *uoff = xcalloc((size_t)n + 1, sizeof(uint32_t));
  for (uint32_t k = 0; k < nuse; k++) uoff[use[k].v + 1]++;
  for (uint32_t i = 0; i < n; i++) uoff[i + 1] += uoff[i];
  LvUse *byv = xmalloc((nuse ? nuse : 1) * sizeof(LvUse));
  uint32_t *ucur = xmalloc(((size_t)n + 1) * sizeof(uint32_t));
  memcpy(ucur, uoff, ((size_t)n + 1) * sizeof(uint32_t));
  for (uint32_t k = 0; k < nuse; k++) byv[ucur[use[k].v]++] = use[k];
  xfree(use); xfree(ucur);

  /* Two walks of the same paths: the first counts each block's sets, the
     second fills them. */
  uint32_t *in_mark = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  uint32_t *out_mark = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  uint32_t *in_off = xcalloc((size_t)nb + 1, sizeof(uint32_t));
  uint32_t *out_off = xcalloc((size_t)nb + 1, sizeof(uint32_t));
  uint32_t *in_cur = NULL, *out_cur = NULL;
  Ref *in = NULL, *out = NULL;
  uint32_t *stack = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  for (int pass = 0; pass < 2; pass++) {
    if (pass == 1) {
      for (uint32_t i = 0; i < nb; i++) {
        in_off[i + 1] += in_off[i];
        out_off[i + 1] += out_off[i];
      }
      in = xmalloc((in_off[nb] ? in_off[nb] : 1) * sizeof(Ref));
      out = xmalloc((out_off[nb] ? out_off[nb] : 1) * sizeof(Ref));
      in_cur = xmalloc(((size_t)nb + 1) * sizeof(uint32_t));
      out_cur = xmalloc(((size_t)nb + 1) * sizeof(uint32_t));
      memcpy(in_cur, in_off, ((size_t)nb + 1) * sizeof(uint32_t));
      memcpy(out_cur, out_off, ((size_t)nb + 1) * sizeof(uint32_t));
    }
    for (uint32_t i = 0; i < nb; i++) in_mark[i] = out_mark[i] = UINT32_MAX;

#define LV_IN(b_)  do { if (pass) in[in_cur[b_]++] = v; else in_off[(b_) + 1]++; } while (0)
#define LV_OUT(b_) do { if (pass) out[out_cur[b_]++] = v; else out_off[(b_) + 1]++; } while (0)
    for (uint32_t v = 0; v < n; v++) {
      for (uint32_t k = uoff[v]; k < uoff[v + 1]; k++) {
        uint32_t ub = byv[k].b, sp = 0;
        bool exposed;
        if (byv[k].pos == LV_END) {
          if (out_mark[ub] != v) { out_mark[ub] = v; LV_OUT(ub); }
          exposed = dblk[v] != ub;
        } else {
          exposed = dblk[v] != ub || dpos[v] >= (int32_t)byv[k].pos;
        }
        if (!exposed || in_mark[ub] == v) continue;
        in_mark[ub] = v;
        stack[sp++] = ub;
        while (sp) {
          uint32_t bb = stack[--sp];
          LV_IN(bb);
          for (uint32_t q = poff[bb]; q < poff[bb + 1]; q++) {
            uint32_t pb = pred[q];
            if (out_mark[pb] != v) { out_mark[pb] = v; LV_OUT(pb); }
            if (dblk[v] != pb && in_mark[pb] != v) {
              in_mark[pb] = v;
              stack[sp++] = pb;
            }
          }
        }
      }
    }
#undef LV_IN
#undef LV_OUT
  }

  xfree(stack); xfree(in_mark); xfree(out_mark); xfree(in_cur); xfree(out_cur);
  xfree(byv); xfree(uoff); xfree(pred); xfree(pcur); xfree(poff);
  xfree(dblk); xfree(dpos); xfree(eu);
  s->lv_in_off = in_off; s->lv_in = in;
  s->lv_out_off = out_off; s->lv_out = out;
}

static void compute_liveness(Isel *s) {
  if (!s->lv_in_off) compute_liveness_now(s);
}

static bool live_in_has(const Isel *s, uint32_t bi, Ref v) {
  uint32_t a = s->lv_in_off[bi], b = s->lv_in_off[bi + 1];
  while (a < b) {
    uint32_t m = a + (b - a) / 2;
    if (s->lv_in[m] < v) a = m + 1; else b = m;
  }
  return a < s->lv_in_off[bi + 1] && s->lv_in[a] == v;
}

/* The values live at each point of a block, walked backwards from its
   live-out set: a sparse set, so starting a block costs its live-out set
   rather than the function's size, with the members handed out sorted. */
typedef struct {
  uint32_t *idx;
  Ref *mem, *sorted;
  uint32_t nmem;
} LiveWalk;

static inline bool lw_has(const LiveWalk *w, Ref v) {
  uint32_t k = w->idx[v];
  return k < w->nmem && w->mem[k] == v;
}

static inline void lw_add(LiveWalk *w, Ref v) {
  if (lw_has(w, v)) return;
  w->idx[v] = w->nmem;
  w->mem[w->nmem++] = v;
}

static inline void lw_del(LiveWalk *w, Ref v) {
  if (!lw_has(w, v)) return;
  uint32_t k = w->idx[v];
  Ref last = w->mem[--w->nmem];
  w->mem[k] = last;
  w->idx[last] = k;
}

#define REF_LESS(x, y) (*(x) < *(y))
WARMC_SORT(sort_refs, Ref, REF_LESS)

static const Ref *lw_sorted(LiveWalk *w) {
  memcpy(w->sorted, w->mem, w->nmem * sizeof(Ref));
  sort_refs(w->sorted, w->nmem);
  return w->sorted;
}

/* Backward walk over the emission order invoking fn at every instruction
   want(op) picks, with the values live across it in increasing order (the
   site's own def is still among them; callers exclude it). */
static void for_each_live_site(Isel *s, IrFunc *f, bool (*want)(uint16_t),
                               void (*fn)(Isel *, IrFunc *, Ref, const Ref *,
                                          uint32_t, void *),
                               void *ctx) {
  uint32_t n = f->ninsts;
  compute_liveness(s);
  uint32_t eucap = eff_use_cap(f);
  Ref *eu = xmalloc(eucap * sizeof(Ref));
  LiveWalk w = { xcalloc(n ? n : 1, sizeof(uint32_t)),
                 xmalloc((n ? n : 1) * sizeof(Ref)),
                 xmalloc((n ? n : 1) * sizeof(Ref)), 0 };
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    w.nmem = 0;
    for (uint32_t k = s->lv_out_off[bi]; k < s->lv_out_off[bi + 1]; k++)
      lw_add(&w, s->lv_out[k]);
    for (int32_t i = (int32_t)b->ninsts - 1; i >= 0; i--) {
      Ref r2 = b->insts[i];
      if (want(f->insts[r2].op)) fn(s, f, r2, lw_sorted(&w), w.nmem, ctx);
      lw_del(&w, r2);
      uint32_t ne = eff_uses(s, f, r2, eu, eucap);
      for (uint32_t j = 0; j < ne; j++) lw_add(&w, eu[j]);
    }
  }
  xfree(w.idx); xfree(w.mem); xfree(w.sorted);
  xfree(eu);
}

static void for_each_clobber_site(Isel *s, IrFunc *f,
                                  void (*fn)(Isel *, IrFunc *, Ref, const Ref *,
                                             uint32_t, void *),
                                  void *ctx) {
  for_each_live_site(s, f, op_clobbers_caller_saved, fn, ctx);
}

static void mark_crosses_call(Isel *s, IrFunc *f, Ref site,
                              const Ref *live, uint32_t nlive, void *ctx) {
  Interval *iv = ctx;
  (void)s; (void)f;
  for (uint32_t k = 0; k < nlive; k++)
    if (live[k] != site) iv[live[k]].crosses_call = 1;
}

#define SPLIT_BUDGET ((uint64_t)8 * 1024 * 1024)

/* Is this register free across every one of the loop's block spans?  The
   heap holds every range assigned above (dead owners excluded); the spans
   are disjoint block extents sorted by position, so one binary search per
   held range answers it. */
static bool split_reg_free(const OccHeap *h, const uint8_t *gone,
                           const Range *spans, uint32_t nspans,
                           uint64_t *work) {
  for (uint32_t i = 0; i < h->n; i++) {
    const Occ *o = &h->v[i];
    if (gone[o->owner]) continue;
    (*work)++;
    uint32_t a = 0, b = nspans;
    while (a < b) {
      uint32_t m = (a + b) / 2;
      if (spans[m].hi < o->lo) a = m + 1; else b = m;
    }
    if (a < nspans && spans[a].lo <= o->hi) return false;
  }
  return true;
}

/* by lo: what is sorted is the ranges one register holds, or the spans of
   one loop's blocks, both disjoint, so no two start together and any sort
   gives qsort's order */
#define RANGE_LO_LESS(x, y) ((x)->lo < (y)->lo)
WARMC_SORT(sort_ranges_lo, Range, RANGE_LO_LESS)

typedef struct {
  Ref v;
  double w;
  uint8_t kind;
  uint8_t cls;
} SplitCand;

static int split_cand_cmp(const void *a, const void *b) {
  const SplitCand *x = a, *y = b;
  if (x->w != y->w) return x->w > y->w ? -1 : 1;
  return x->v < y->v ? -1 : 1;
}

/* A register's full assignment as ranges sorted by start, for the passes
   that place intervals after the scan: second chance and pieces. */
typedef struct { Range *v; uint32_t n, cap; } RList;

/* Append, or with sorted, insert in order. */
static void rl_add(RList *L, int32_t lo, int32_t hi, bool sorted) {
  if (L->n == L->cap) {
    L->cap = L->cap ? L->cap * 2 : 16;
    L->v = xrealloc(L->v, L->cap * sizeof(Range));
  }
  uint32_t z = L->n++;
  while (sorted && z && L->v[z - 1].lo > lo) { L->v[z] = L->v[z - 1]; z--; }
  L->v[z].lo = lo; L->v[z].hi = hi;
}

/* Is [lo, hi] clear of every range in L?  Ranges in one register are
   disjoint but for a tied operand's shared end, so their ends are sorted
   too and the first range ending at or after lo is the only candidate. */
static bool rl_free(const RList *L, int32_t lo, int32_t hi) {
  uint32_t a = 0, b = L->n;
  while (a < b) {
    uint32_t m = (a + b) / 2;
    if (L->v[m].hi < lo) a = m + 1; else b = m;
  }
  return !(a < L->n && L->v[a].lo <= hi);
}

typedef struct { uint32_t idx; double w; } RefillCand;

static int refill_cmp(const void *a, const void *b) {
  const RefillCand *x = a, *y = b;
  if (x->w != y->w) return x->w > y->w ? -1 : 1;
  return x->idx < y->idx ? -1 : 1;
}

typedef struct { Ref v; uint32_t i; } PieceUse;

static int piece_use_cmp(const void *a, const void *b) {
  const PieceUse *x = a, *y = b;
  if (x->v != y->v) return x->v < y->v ? -1 : 1;
  return x->i < y->i ? -1 : x->i > y->i;
}

typedef struct { Ref v; uint32_t block, lo, hi; double gain; } PieceCand;

static int piece_cand_cmp(const void *a, const void *b) {
  const PieceCand *x = a, *y = b;
  if (x->gain != y->gain) return x->gain > y->gain ? -1 : 1;
  if (x->block != y->block) return x->block < y->block ? -1 : 1;
  if (x->lo != y->lo) return x->lo < y->lo ? -1 : 1;
  return x->v < y->v ? -1 : x->v > y->v;
}

static int piece_cmp(const void *a, const void *b) {
  const struct Piece *x = a, *y = b;
  if (x->block != y->block) return x->block < y->block ? -1 : 1;
  if (x->lo != y->lo) return x->lo < y->lo ? -1 : 1;
  return x->v < y->v ? -1 : x->v > y->v;
}

#define U64_LESS(x, y) (*(x) < *(y))
WARMC_SORT(sort_u64, uint64_t, U64_LESS)

typedef struct { Ref v; uint32_t hdr; double gain; } LoopPieceCand;

static int lpc_cmp(const void *a, const void *b) {
  const LoopPieceCand *x = a, *y = b;
  if (x->gain != y->gain) return x->gain > y->gain ? -1 : 1;
  if (x->hdr != y->hdr) return x->hdr < y->hdr ? -1 : 1;
  return x->v < y->v ? -1 : x->v > y->v;
}

/* Loop pieces.  A value with no register that a loop reads and that is made
   before it -- a bound, a base, a constant hoisted out of it -- is the same
   on every trip, but a use-cluster piece needs its reads close together in
   one block, and a loop reads it once a block an iteration: each read
   reloads.  A register free across the whole loop holds it instead, filled
   once at the end of each block that enters the loop: rsbench/utf8scan's
   decode loop reloaded its end from the stack every character, the bound
   being live across an outer loop whose other inner loop needed every
   register.  Only in a loop that makes no call and has no landing pad, and
   only a register the range lists show free over each of its blocks and at
   each fill: nothing writes it there, so it holds the value until the loop
   is left, through the blocks' terminators too, whose compares read it.
   Candidates go heaviest first, an enclosing loop's before an inner one's,
   which it covers.  Appends to *pcs: one PC_LOOP piece per block of the
   loop, one PC_FILL piece per block entering it. */
static void place_loop_pieces(Isel *s, IrFunc *f, RList rl[2][32],
                              const int32_t *blk_start, const int32_t *blk_end,
                              const uint32_t *lin_off, const Ref *lin,
                              const uint32_t *web, const Interval *iv,
                              const int32_t *num, const int *gpool, int ngpool,
                              Ref *eu, uint32_t eucap, uint32_t n,
                              struct Piece **pcs, uint32_t *npcs,
                              uint32_t *cap_pcs) {
  uint32_t nb = f->nblocks;
  /* most functions spill nothing a loop could read, and remake nothing
     that costs more than a move */
  bool any = false;
  for (uint32_t v = 0; v < n && !any; v++) {
    int l = s->loc[v];
    if (num[v] < 0) continue;
    if (l != LOC_NONE) { any = IS_SLOT(l); continue; }
    const Inst *vi = &f->insts[v];
    any = vi->op == I_GLOBAL_GET ||
          (vi->op == I_CONST && ((vt_is_float((ValType)vi->type) || (ValType)vi->type == VT_V128) ||
                                 ir_const_cost(vi->imm, vt_is_64((ValType)vi->type)) > 1));
  }
  if (!any) return;
  uint8_t *member = xcalloc(nb ? nb : 1, 1);
  uint32_t *mstack = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  /* each loop's blocks, by header: lfirst[h]..lfirst[h] + lcount[h] in lblk */
  uint32_t *lfirst = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  uint32_t *lcount = xcalloc(nb ? nb : 1, sizeof(uint32_t));
  uint32_t *lblk = NULL, nlblk = 0, cap_lblk = 0;
  double *rw = xcalloc(n ? n : 1, sizeof(double));
  uint32_t *rstamp = xcalloc(n ? n : 1, sizeof(uint32_t));
  Ref *rvals = xmalloc((n ? n : 1) * sizeof(Ref));
  LoopPieceCand *cand = NULL;
  uint32_t ncand = 0, cap_cand = 0, stamp = 0;
  uint64_t work = 0;

  for (uint32_t oi = 0; oi < f->norder && work <= SPLIT_BUDGET; oi++) {
    uint32_t hb = f->order[oi];
    Block *hd = &f->blocks[hb];
    if (!hd->reachable || hd->is_pad || hb == f->entry) continue;
    bool is_hdr = false;
    for (uint32_t p = 0; p < hd->npreds && !is_hdr; p++) {
      uint32_t pb = hd->preds[p];
      is_hdr = f->blocks[pb].reachable && f->blocks[pb].rpo >= hd->rpo;
    }
    if (!is_hdr) continue;
    uint32_t first = nlblk, spx = 0;
    member[hb] = 1;
#define LP_ADD(x) do {                                                  \
      if (nlblk == cap_lblk) {                                            \
        cap_lblk = cap_lblk ? cap_lblk * 2 : 64;                          \
        lblk = xrealloc(lblk, cap_lblk * sizeof(uint32_t));               \
      }                                                                   \
      lblk[nlblk++] = (x);                                                \
    } while (0)
    LP_ADD(hb);
    for (uint32_t p = 0; p < hd->npreds; p++) {
      uint32_t pb = hd->preds[p];
      if (!f->blocks[pb].reachable || f->blocks[pb].rpo < hd->rpo || member[pb]) continue;
      member[pb] = 1; LP_ADD(pb); mstack[spx++] = pb;
    }
    while (spx) {
      Block *c = &f->blocks[mstack[--spx]];
      for (uint32_t p = 0; p < c->npreds; p++) {
        uint32_t pb = c->preds[p];
        if (member[pb] || !f->blocks[pb].reachable) continue;
        member[pb] = 1; LP_ADD(pb); mstack[spx++] = pb;
      }
    }
#undef LP_ADD
    work += nlblk - first;
    /* irreducible, or one that calls or catches: none */
    bool bad = false;
    for (uint32_t k = first; k < nlblk && !bad; k++) {
      uint32_t bi = lblk[k];
      const Block *b = &f->blocks[bi];
      bad = b->rpo < hd->rpo || bi == f->entry || b->is_pad ||
            ir_block_has_eh(f, bi) || b->term == T_RETURN || b->term == T_TRAP;
      for (uint32_t i = 0; i < b->ninsts && !bad; i++) {
        uint16_t op = f->insts[b->insts[i]].op;
        bad = op_clobbers_caller_saved(op) || op == I_RETURN_CALL ||
              op == I_RETURN_CALL_INDIRECT || op == I_RETURN_CALL_REF;
      }
      work += b->ninsts;
    }
    for (uint32_t k = first; k < nlblk; k++) member[lblk[k]] = 0;
    if (warmc_getenv("WARMC_LP_DEBUG"))
      fprintf(stderr, "lp[%u] hdr b%u blocks=%u bad=%d\n", f->func_index, hb,
              nlblk - first, bad);
    if (bad) { nlblk = first; continue; }
    lfirst[hb] = first;
    lcount[hb] = nlblk - first;

    /* what the loop reads that has no register and is live into it */
    uint32_t cur = ++stamp, nr = 0;
    for (uint32_t k = lin_off[hb]; k < lin_off[hb + 1]; k++) {
      Ref v = lin[k];
      if (v >= n || num[v] < 0 || iv[web[v]].end < 0) continue;
      if (!value_needs_location(f, v) || skip_location(s, v) || s->pinned[v]) continue;
      int l = s->loc[web[v]];
      if (l != LOC_NONE && !IS_SLOT(l)) continue;
      const Inst *vi = &f->insts[v];
      if (l == LOC_NONE && vi->op == I_CONST &&
          !vt_is_float((ValType)vi->type) && (ValType)vi->type != VT_V128 &&
          ir_const_cost(vi->imm, vt_is_64((ValType)vi->type)) <= 1)
        continue;
      if (l == LOC_NONE && vi->op != I_CONST && vi->op != I_GLOBAL_GET) continue;
      rstamp[v] = cur;
      rw[v] = 0;
    }
    for (uint32_t k = first; k < nlblk; k++) {
      const Block *b = &f->blocks[lblk[k]];
      double w = 1.0;
      for (uint32_t d = 0; d < b->loop_depth && d < 8; d++) w *= 10.0;
      if (b->cold) w = 0.01;
      for (uint32_t i = 0; i < b->ninsts; i++) {
        uint32_t ne = eff_uses(s, f, b->insts[i], eu, eucap);
        for (uint32_t j = 0; j < ne; j++)
          if (eu[j] < n && rstamp[eu[j]] == cur) {
            if (rw[eu[j]] == 0) rvals[nr++] = eu[j];
            rw[eu[j]] += w;
          }
      }
      if (b->term == T_BRIF && s->fused && s->fused[b->term_arg]) {
        uint32_t ne = eff_uses(s, f, b->term_arg, eu, eucap);
        for (uint32_t j = 0; j < ne; j++) {
          Ref u = eff_arg(s, f, eu[j]);
          if (u < n && rstamp[u] == cur) {
            if (rw[u] == 0) rvals[nr++] = u;
            rw[u] += w;
          }
        }
      } else if ((b->term == T_BRIF || b->term == T_BRTABLE) &&
                 b->term_arg < n && rstamp[b->term_arg] == cur) {
        if (rw[b->term_arg] == 0) rvals[nr++] = b->term_arg;
        rw[b->term_arg] += w;
      }
    }
    if (!nr) continue;
    /* a fill at the end of each block that enters the loop */
    double fw = 0;
    for (uint32_t p = 0; p < hd->npreds; p++) {
      const Block *pb = &f->blocks[hd->preds[p]];
      if (!pb->reachable || pb->rpo >= hd->rpo) continue;
      double w = 1.0;
      for (uint32_t d = 0; d < pb->loop_depth && d < 8; d++) w *= 10.0;
      if (pb->cold) w = 0.01;
      fw += w;
    }
    for (uint32_t k = 0; k < nr; k++) {
      Ref v = rvals[k];
      if (rw[v] <= fw) continue;
      if (ncand == cap_cand) {
        cap_cand = cap_cand ? cap_cand * 2 : 32;
        cand = xrealloc(cand, cap_cand * sizeof(LoopPieceCand));
      }
      cand[ncand++] = (LoopPieceCand){ v, hb, rw[v] - fw };
    }
  }
  if (ncand > 1) qsort(cand, ncand, sizeof(LoopPieceCand), lpc_cmp);

  for (uint32_t x = 0; x < ncand; x++) {
    Ref v = cand[x].v;
    uint32_t hb = cand[x].hdr;
    const Block *hd = &f->blocks[hb];
    /* covered by an enclosing loop's piece already */
    bool covered = false;
    for (uint32_t k = 0; k < *npcs && !covered; k++)
      covered = (*pcs)[k].v == v && (*pcs)[k].block == hb && (*pcs)[k].kind == PC_LOOP;
    if (covered) continue;
    int cl = s->cls[v];
    const int *pool = cl ? FPR_ALLOC : gpool;
    int npool = cl ? NFPR_ALLOC : ngpool;
    int got = -1;
    for (int k = 0; k < npool && got < 0; k++) {
      int rr = pool[k];
      if (cl ? (fpr_is_callee_saved(rr) && !(s->used_fpr_cs & (1u << rr)))
             : ((gpr_is_callee_saved(rr) && !(s->used_gpr_cs & (1u << rr))) ||
                rr == RA_ALLOC_RES_REG))
        continue;
      const RList *L = &rl[cl ? 1 : 0][rr];
      bool ok = true;
      for (uint32_t j = 0; j < lcount[hb] && ok; j++) {
        uint32_t bi = lblk[lfirst[hb] + j];
        ok = rl_free(L, blk_start[bi], blk_end[bi]);
      }
      for (uint32_t p = 0; p < hd->npreds && ok; p++) {
        uint32_t pb = hd->preds[p];
        if (!f->blocks[pb].reachable || f->blocks[pb].rpo >= hd->rpo) continue;
        ok = rl_free(L, blk_end[pb], blk_end[pb]);
      }
      if (ok) got = rr;
    }
    if (warmc_getenv("WARMC_LP_DEBUG"))
      fprintf(stderr, "lp[%u] v%u hdr b%u gain=%.0f reg=%d\n", f->func_index, v, hb,
              cand[x].gain, got);
    if (got < 0) continue;
    RList *L = &rl[cl ? 1 : 0][got];
    uint32_t need = lcount[hb] + hd->npreds;
    if (*npcs + need > *cap_pcs) {
      *cap_pcs = (*npcs + need) * 2;
      *pcs = xrealloc(*pcs, *cap_pcs * sizeof(struct Piece));
    }
    for (uint32_t j = 0; j < lcount[hb]; j++) {
      uint32_t bi = lblk[lfirst[hb] + j];
      rl_add(L, blk_start[bi], blk_end[bi], true);
      (*pcs)[(*npcs)++] = (struct Piece){ v, bi, 0, UINT32_MAX, got, PC_LOOP };
    }
    for (uint32_t p = 0; p < hd->npreds; p++) {
      uint32_t pb = hd->preds[p];
      if (!f->blocks[pb].reachable || f->blocks[pb].rpo >= hd->rpo) continue;
      rl_add(L, blk_end[pb], blk_end[pb], true);
      (*pcs)[(*npcs)++] = (struct Piece){ v, pb, f->blocks[pb].ninsts,
                                          f->blocks[pb].ninsts, got, PC_FILL };
    }
    g_stats.loop_pieces++;
  }
  xfree(member); xfree(mstack); xfree(lfirst); xfree(lcount); xfree(lblk);
  xfree(rw); xfree(rstamp); xfree(rvals); xfree(cand);
}

#if RA_HAVE_RECOMP
/* Recompute on read.  A value that is one cheap operation of registers
   and an immediate (recomp_shape) -- a frame address, sp plus an offset,
   most often -- costs a load at each read once spilled, where an add from
   the frame pointer, which has a register there anyway, costs less.  So a
   spilled one is recomputed at every read where each register operand
   holds it -- live there and alone in its web, past the read so no result
   can have taken its register yet, or up to it for a terminator's -- and
   reloaded nowhere.  Its definition still stores it, for the moves on
   edges, which read the slot.  A call marshals its arguments into
   registers an operand could be in, so at one the operands must be
   callee-saved; a return's epilogue restores those, so a read there rules
   it out. */
static void mark_recompute(Isel *s, IrFunc *f, const Interval *iv, const Range *R,
                           const uint32_t *web, const int32_t *num,
                           const int32_t *blk_end, Ref *eu, uint32_t eucap,
                           uint32_t n) {
  uint32_t *wcnt = xcalloc(n ? n : 1, sizeof(uint32_t));
  uint8_t *ok = xcalloc(n ? n : 1, 1);
  uint32_t ncand = 0;
  for (uint32_t i = 0; i < n; i++)
    if (iv[i].end >= 0 || (web[i] != i && iv[web[i]].end >= 0)) wcnt[web[i]]++;
  for (uint32_t v = 0; v < n; v++) {
    if (num[v] < 0 || !value_needs_location(f, v) || skip_location(s, v)) continue;
    int l = s->loc[v];
    if (l == LOC_NONE || !IS_SLOT(l)) continue;
    Ref x, y;
    uint64_t c;
    if (recomp_shape(f, v, &x, &y, &c) == I_NOP) continue;
    bool regs = true;
    for (int k = 0; k < 2 && regs; k++) {
      Ref o = k ? y : x;
      if (o == REF_NONE) continue;
      int lo = s->loc[o];
      regs = o < n && num[o] >= 0 && lo != LOC_NONE && !IS_SLOT(lo) && lo < 32 &&
             wcnt[web[o]] == 1;
    }
    if (regs) { ok[v] = 1; ncand++; }
  }
#define RC_COVERS(o, p, strict) ({                                              \
    const Interval *I_ = &iv[web[o]];                                           \
    bool c_ = false;                                                            \
    if (I_->rcount) {                                                           \
      for (uint32_t y_ = 0; y_ < I_->rcount && !c_; y_++) {                     \
        const Range *G_ = &R[I_->rfirst + y_];                                  \
        c_ = G_->lo <= (p) && ((strict) ? (p) < G_->hi : (p) <= G_->hi);        \
      }                                                                         \
    } else {                                                                    \
      c_ = I_->start <= (p) && ((strict) ? (p) < I_->end : (p) <= I_->end);     \
    }                                                                           \
    c_; })
/* where a call marshals into the argument registers, an operand must be in
   a register no marshalling writes */
#define RC_SAFE(o) gpr_is_callee_saved(s->loc[o])
#define RC_READ(w, p, strict, marsh) do {                                       \
    Ref w_ = (w);                                                               \
    if (w_ < n && ok[w_]) {                                                     \
      Ref x_, y_;                                                               \
      uint64_t c_;                                                              \
      recomp_shape(f, w_, &x_, &y_, &c_);                                       \
      if (!RC_COVERS(x_, (p), strict) ||                                        \
          (y_ != REF_NONE && !RC_COVERS(y_, (p), strict)) ||                    \
          ((marsh) && (!RC_SAFE(x_) || (y_ != REF_NONE && !RC_SAFE(y_)))))      \
        ok[w_] = 0;                                                             \
    }                                                                           \
  } while (0)
  for (uint32_t oi = 0; oi < f->norder && ncand; oi++) {
    uint32_t bi = f->order[oi];
    const Block *b = &f->blocks[bi];
    if (!b->reachable) continue;
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref r = b->insts[i];
      uint16_t op = f->insts[r].op;
      bool marshals = op_clobbers_caller_saved(op) || op == I_RETURN_CALL ||
                      op == I_RETURN_CALL_INDIRECT || op == I_RETURN_CALL_REF;
      uint32_t ne = eff_uses(s, f, r, eu, eucap);
      for (uint32_t j = 0; j < ne; j++) RC_READ(eu[j], num[r], true, marshals);
    }
    if (b->term == T_BRIF && s->fused && s->fused[b->term_arg]) {
      uint32_t ne = eff_uses(s, f, b->term_arg, eu, eucap);
      for (uint32_t j = 0; j < ne; j++) RC_READ(eff_arg(s, f, eu[j]), blk_end[bi], false, false);
    } else if (b->term == T_BRIF || b->term == T_BRTABLE) {
      RC_READ(b->term_arg, blk_end[bi], false, false);
    }
    for (uint32_t j = 0; j < b->nret; j++)
      if (b->ret_args[j] < n) ok[b->ret_args[j]] = 0;
  }
#undef RC_READ
#undef RC_SAFE
#undef RC_COVERS
  xfree(wcnt);
  bool any = false;
  for (uint32_t v = 0; v < n && !any; v++) any = ok[v];
  if (any) s->recomp = ok;
  else xfree(ok);
}

#endif

/* WARMC_VERIFY_RA: at every instruction, no two slots the scan handed out
   that were merged into one hold values live across it -- by location, or as
   a value's save around calls.  Liveness here is the exact one the safepoint
   maps are made from, not the intervals the sharing went by.  A split
   parameter aliased onto its source's slot is the same slot, and passes. */
typedef struct {
  uint32_t *stamp, *owner;
  const uint32_t *oloc, *ocsa;
  uint32_t site;
} SlotCheck;

static void slot_check_one(Isel *s, SlotCheck *c, uint32_t k, uint32_t old,
                           bool vec) {
  for (uint32_t u = 0; u < (vec ? 2u : 1u); u++) {
    if (c->stamp[k + u] == c->site && c->owner[k + u] != old)
      warmc_fatal("regalloc: slots %u and %u share slot %u while both are "
                  "live in %s", c->owner[k + u], old, k + u,
                  s->f->m->funcs[s->f->func_index].sym);
    c->stamp[k + u] = c->site;
    c->owner[k + u] = old;
  }
}

static void slot_check_site(Isel *s, IrFunc *f, SlotCheck *c, const Ref *live,
                            uint32_t nlive) {
  c->site++;
  for (uint32_t i = 0; i < nlive; i++) {
    Ref v = live[i];
    bool vec = (ValType)f->insts[v].type == VT_V128;
    if (IS_SLOT(s->loc[v]))
      slot_check_one(s, c, SLOT_OF(s->loc[v]), c->oloc[v], vec);
    if (s->csa_slot && s->csa_slot[v] >= 0)
      slot_check_one(s, c, (uint32_t)s->csa_slot[v], c->ocsa[v], vec);
  }
}

/* for_each_live_site's walk, every instruction a site, without the sorted
   copy of the live set it hands each callback: here the order does not
   matter, and a sort per instruction made a function with thousands of
   values live at once take seconds to check. */
static void slot_check_walk(Isel *s, IrFunc *f, SlotCheck *c) {
  uint32_t n = f->ninsts;
  uint32_t eucap = eff_use_cap(f);
  Ref *eu = xmalloc(eucap * sizeof(Ref));
  LiveWalk w = { xcalloc(n ? n : 1, sizeof(uint32_t)),
                 xmalloc((n ? n : 1) * sizeof(Ref)), NULL, 0 };
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    w.nmem = 0;
    for (uint32_t k = s->lv_out_off[bi]; k < s->lv_out_off[bi + 1]; k++)
      lw_add(&w, s->lv_out[k]);
    for (int32_t i = (int32_t)b->ninsts - 1; i >= 0; i--) {
      Ref r2 = b->insts[i];
      slot_check_site(s, f, c, w.mem, w.nmem);
      lw_del(&w, r2);
      uint32_t ne = eff_uses(s, f, r2, eu, eucap);
      for (uint32_t j = 0; j < ne; j++) lw_add(&w, eu[j]);
    }
  }
  xfree(w.idx); xfree(w.mem); xfree(eu);
}

/* Spill slots, renumbered so that values never live at the same time share
   one.  The scan gives every spilled value a slot of its own: in a function
   of a few thousand spills that is tens of kilobytes of frame, most of it
   dead at any moment, past the 32 KB an arm64 load reaches from sp in one
   instruction.  A slot is busy from the first position of anything placed
   in it to the last -- the value whose slot it is, a web's other members and
   a split parameter aliased onto it, which share its location, and a value
   whose register is saved there around calls -- and slots of one size whose
   spans do not meet become one, taken in order of start, each into the slot
   that fell free first.  A value with no interval holds its slot throughout.
   A block parameter's slot is written at the end of each predecessor, by
   the edge's copy, where the parameter itself is not yet live: there the
   slot is busy too, or a value living in it across the latch of a loop is
   overwritten by the phi's next value.  A landing pad's are written earlier,
   before the call that may throw (emit_eh_stores), while that call's own
   operands are still to be read.  v128 slots are pairs, kept to even slots
   of their own below the rest, as take_slot aligns them. */
typedef struct {
  int32_t lo, hi;
  uint32_t old;
  bool vec;
} SlotSpan;

static int span_cmp(const void *a, const void *b) {
  const SlotSpan *x = a, *y = b;
  if (x->lo != y->lo) return x->lo < y->lo ? -1 : 1;
  return x->old < y->old ? -1 : x->old > y->old;
}

static void span_note(SlotSpan *sp, uint32_t k, const Interval *iv, Ref v,
                      bool vec) {
  int32_t lo = iv[v].end >= 0 ? iv[v].start : INT32_MIN;
  int32_t hi = iv[v].end >= 0 ? iv[v].end : INT32_MAX;
  if (lo < sp[k].lo) sp[k].lo = lo;
  if (hi > sp[k].hi) sp[k].hi = hi;
  sp[k].vec |= vec;
}

static uint32_t share_slots(Isel *s, const Interval *iv, uint32_t n,
                            uint32_t nold, const int32_t *blk_end) {
  if (nold < 2 || warmc_getenv("WARMC_NO_SLOT_SHARE")) return nold;
  IrFunc *f = s->f;
  SlotSpan *sp = xmalloc(nold * sizeof(SlotSpan));
  for (uint32_t k = 0; k < nold; k++) {
    sp[k].lo = INT32_MAX;
    sp[k].hi = INT32_MIN;
    sp[k].old = k;
    sp[k].vec = false;
  }
  for (uint32_t v = 0; v < n; v++) {
    bool vec = (ValType)f->insts[v].type == VT_V128;
    if (IS_SLOT(s->loc[v])) span_note(sp, SLOT_OF(s->loc[v]), iv, v, vec);
    if (s->csa_slot && s->csa_slot[v] >= 0)
      span_note(sp, (uint32_t)s->csa_slot[v], iv, v, vec);
  }
  for (uint32_t bi = 0; bi < f->nblocks; bi++) {
    Block *b = &f->blocks[bi];
    if (!b->reachable) continue;
    for (uint32_t e = 0; e < b->nsuccs; e++) {
      Block *t = &f->blocks[b->succs[e].target];
      int32_t at = b->succs[e].eh && b->ninsts ? blk_end[bi] - 1 : blk_end[bi];
      for (uint32_t j = 0; j < t->nparams; j++) {
        int l = s->loc[t->params[j]];
        if (!IS_SLOT(l)) continue;
        SlotSpan *x = &sp[SLOT_OF(l)];
        if (at < x->lo) x->lo = at;
        if (blk_end[bi] > x->hi) x->hi = blk_end[bi];
      }
    }
  }
  uint32_t nused = 0;
  for (uint32_t k = 0; k < nold; k++)
    if (sp[k].lo <= sp[k].hi) sp[nused++] = sp[k];
  qsort(sp, nused, sizeof(SlotSpan), span_cmp);

  uint32_t *map = xmalloc(nold * sizeof(uint32_t));
  int32_t *hhi = xmalloc((nused ? nused : 1) * sizeof(int32_t));
  uint32_t *hslot = xmalloc((nused ? nused : 1) * sizeof(uint32_t));
  uint32_t count[2] = { 0, 0 };
  for (int c = 0; c < 2; c++) {
    /* a min-heap of the slots in use, by where they fall free */
    uint32_t hn = 0;
    for (uint32_t i = 0; i < nused; i++) {
      if (sp[i].vec != (c == 1)) continue;
      uint32_t slot;
      if (hn && hhi[0] < sp[i].lo) {
        slot = hslot[0];
        hhi[0] = hhi[--hn];
        hslot[0] = hslot[hn];
        for (uint32_t p = 0;;) {
          uint32_t l = 2 * p + 1, r = l + 1, m = p;
          if (l < hn && hhi[l] < hhi[m]) m = l;
          if (r < hn && hhi[r] < hhi[m]) m = r;
          if (m == p) break;
          int32_t th = hhi[m]; hhi[m] = hhi[p]; hhi[p] = th;
          uint32_t ts = hslot[m]; hslot[m] = hslot[p]; hslot[p] = ts;
          p = m;
        }
      } else {
        slot = count[c]++;
      }
      map[sp[i].old] = slot;
      uint32_t q = hn++;
      hhi[q] = sp[i].hi;
      hslot[q] = slot;
      while (q) {
        uint32_t p = (q - 1) / 2;
        if (hhi[p] <= hhi[q]) break;
        int32_t th = hhi[p]; hhi[p] = hhi[q]; hhi[q] = th;
        uint32_t ts = hslot[p]; hslot[p] = hslot[q]; hslot[q] = ts;
        q = p;
      }
    }
  }
  for (uint32_t i = 0; i < nused; i++)
    map[sp[i].old] = sp[i].vec ? 2 * map[sp[i].old]
                               : 2 * count[1] + map[sp[i].old];
  bool verify = warmc_getenv("WARMC_VERIFY_RA") != NULL;
  uint32_t *oloc = verify ? xmalloc((n ? n : 1) * sizeof(uint32_t)) : NULL;
  uint32_t *ocsa = verify ? xmalloc((n ? n : 1) * sizeof(uint32_t)) : NULL;
  for (uint32_t v = 0; v < n; v++) {
    if (IS_SLOT(s->loc[v])) {
      if (verify) oloc[v] = SLOT_OF(s->loc[v]);
      s->loc[v] = SLOT(map[SLOT_OF(s->loc[v])]);
    }
    if (s->csa_slot && s->csa_slot[v] >= 0) {
      if (verify) ocsa[v] = (uint32_t)s->csa_slot[v];
      s->csa_slot[v] = (int32_t)map[s->csa_slot[v]];
    }
  }
  uint32_t nnew = 2 * count[1] + count[0];
  if (verify) {
    SlotCheck c = { xcalloc(nnew + 2, sizeof(uint32_t)),
                    xmalloc((nnew + 2) * sizeof(uint32_t)), oloc, ocsa, 0 };
    slot_check_walk(s, f, &c);
    /* and on every edge, where the copies into the target's parameters
       land: nothing live into the target may hold one of their slots, nor,
       on an unwind edge, anything the call that may throw still reads */
    for (uint32_t bi = 0; bi < f->nblocks; bi++) {
      Block *b = &f->blocks[bi];
      if (!b->reachable) continue;
      for (uint32_t e = 0; e < b->nsuccs; e++) {
        uint32_t ti = b->succs[e].target;
        Block *t = &f->blocks[ti];
        c.site++;
        for (uint32_t j = 0; j < t->nparams; j++) {
          Ref p = t->params[j];
          if (IS_SLOT(s->loc[p]))
            slot_check_one(s, &c, SLOT_OF(s->loc[p]), oloc[p],
                           (ValType)f->insts[p].type == VT_V128);
        }
        uint32_t nin = s->lv_in_off[ti + 1] - s->lv_in_off[ti];
        uint32_t nops = b->succs[e].eh && b->ninsts
                            ? f->insts[b->insts[b->ninsts - 1]].nargs : 0;
        for (uint32_t k = 0; k < nin + nops; k++) {
          Ref v = k < nin ? s->lv_in[s->lv_in_off[ti] + k]
                          : ir_args(f, b->insts[b->ninsts - 1])[k - nin];
          bool vec = (ValType)f->insts[v].type == VT_V128;
          if (IS_SLOT(s->loc[v]))
            slot_check_one(s, &c, SLOT_OF(s->loc[v]), oloc[v], vec);
          if (s->csa_slot && s->csa_slot[v] >= 0)
            slot_check_one(s, &c, (uint32_t)s->csa_slot[v], ocsa[v], vec);
        }
      }
    }
    xfree(c.stamp); xfree(c.owner); xfree(oloc); xfree(ocsa);
  }
  xfree(sp); xfree(map); xfree(hhi); xfree(hslot);
  return nnew;
}

/* Whether two webs' intervals meet: two that start together, or one that
   starts before another of the other web, started earlier, ends.  Each
   web's members are kept in order of their starts (web_merge), so one walk
   in that order over both answers it: comparing every pair was a web's
   size times the other's, and a Go function's stack pointer gathers a web
   of hundreds. */
static bool web_conflict(const Interval *iv, uint32_t a, uint32_t b,
                         const uint32_t *next) {
  int32_t end_a = INT32_MIN, end_b = INT32_MIN;
  while (a != UINT32_MAX || b != UINT32_MAX) {
    if (b == UINT32_MAX || (a != UINT32_MAX && iv[a].start < iv[b].start)) {
      if (iv[a].start < end_b) return true;
      if (iv[a].end > end_a) end_a = iv[a].end;
      a = next[a];
    } else if (a == UINT32_MAX || iv[b].start < iv[a].start) {
      if (iv[b].start < end_a) return true;
      if (iv[b].end > end_b) end_b = iv[b].end;
      b = next[b];
    } else {
      return true;
    }
  }
  return false;
}

/* web b's members merged into web a's, both in order of their starts */
static void web_merge(const Interval *iv, uint32_t *head, uint32_t *tail,
                      uint32_t b, uint32_t *next) {
  uint32_t a = *head, h = UINT32_MAX, t = UINT32_MAX;
  while (a != UINT32_MAX || b != UINT32_MAX) {
    uint32_t x;
    if (b == UINT32_MAX || (a != UINT32_MAX && iv[a].start <= iv[b].start)) {
      x = a; a = next[a];
    } else {
      x = b; b = next[b];
    }
    if (t == UINT32_MAX) h = x; else next[t] = x;
    t = x;
  }
  next[t] = UINT32_MAX;
  *head = h;
  *tail = t;
}

static void regalloc_linear(Isel *s) {
  IrFunc *f = s->f;
  uint32_t n = f->ninsts;

  uint32_t eucap = eff_use_cap(f);
  Ref *eu = xmalloc(eucap * sizeof(Ref));

  int32_t *num = xmalloc(n * sizeof(int32_t));
  Interval *iv = xcalloc(n ? n : 1, sizeof(Interval));
  for (uint32_t i = 0; i < n; i++) {
    num[i] = -1;
    iv[i].value = i;
    iv[i].start = INT32_MAX;
    iv[i].end = -1;
    iv[i].cls = s->cls[i];
    iv[i].reg = -1;
  }

  int32_t *blk_start = xmalloc(f->nblocks * sizeof(int32_t));
  int32_t *blk_end = xmalloc(f->nblocks * sizeof(int32_t));
  int32_t *call_pos = xmalloc((n + 1) * sizeof(int32_t));
  uint8_t *call_depth = xmalloc(n + 1);
  uint8_t *call_cold = xmalloc(n + 1);
  float *call_share = xmalloc((n + 1) * sizeof(float));
  float *bshare = xmalloc((f->nblocks ? f->nblocks : 1) * sizeof(float));
  ir_path_share(f, bshare);
  uint32_t ncalls = 0;
  int32_t pos = 0;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    blk_start[bi] = pos;
    for (uint32_t i = 0; i < b->nparams; i++) num[b->params[i]] = pos;
    pos++;
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref r = b->insts[i];
      num[r] = pos;
      if (op_clobbers_caller_saved(f->insts[r].op)) {
        uint64_t ft;
        int64_t fl;
        call_depth[ncalls] = (uint8_t)(b->loop_depth < 8 ? b->loop_depth : 8);
        call_cold[ncalls] = gc_fastnew_shape(f, r, &ft, &fl) ? 1 : 0;
        call_share[ncalls] = bshare[bi];
        call_pos[ncalls++] = pos;
      }
      pos++;
    }
    blk_end[bi] = pos;
    pos++;
  }
  int32_t maxpos = pos;

  for (uint32_t i = 0; i < n; i++)
    if (num[i] >= 0 && value_needs_location(f, i)) extend(&iv[i], num[i]);

  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref r = b->insts[i];
      Inst *in = &f->insts[r];
      Ref *a = ir_args(f, r);
      uint32_t ne = eff_uses(s, f, r, eu, eucap);
      (void)in; (void)a;
      for (uint32_t j = 0; j < ne; j++)
        if (num[eu[j]] >= 0) extend(&iv[eu[j]], num[r]);
    }
    if (b->term == T_BRIF && s->fused && s->fused[b->term_arg]) {

      uint32_t ne = eff_uses(s, f, b->term_arg, eu, eucap);
      for (uint32_t j = 0; j < ne; j++) {
        Ref u = eff_arg(s, f, eu[j]);
        if (num[u] >= 0) extend(&iv[u], blk_end[bi]);
      }
    } else if (b->term == T_BRIF || b->term == T_BRTABLE) {
      if (num[b->term_arg] >= 0) extend(&iv[b->term_arg], blk_end[bi]);
    }
    for (uint32_t i = 0; i < b->nret; i++)
      if (num[b->ret_args[i]] >= 0) extend(&iv[b->ret_args[i]], blk_end[bi]);
    for (uint32_t sidx = 0; sidx < b->nsuccs; sidx++) {
      Edge *e = &b->succs[sidx];
      for (uint32_t j = 0; j < e->nargs; j++)
        if (num[e->args[j]] >= 0) extend(&iv[e->args[j]], blk_end[bi]);
    }
  }

  compute_liveness(s);
  const uint32_t *lin_off = s->lv_in_off, *lout_off = s->lv_out_off;
  const Ref *lin = s->lv_in, *lout = s->lv_out;
  for (uint32_t bi = 0; bi < f->nblocks; bi++) {
    if (!f->blocks[bi].reachable) continue;
    for (uint32_t k = lin_off[bi]; k < lin_off[bi + 1]; k++)
      extend(&iv[lin[k]], blk_start[bi]);
    for (uint32_t k = lout_off[bi]; k < lout_off[bi + 1]; k++)
      extend(&iv[lout[k]], blk_end[bi]);
  }

  for_each_clobber_site(s, f, mark_crosses_call, iv);

  int32_t *hint = xmalloc(n * sizeof(int32_t));
  for (uint32_t i = 0; i < n; i++) hint[i] = -1;
  {
    Block *e = &f->blocks[f->entry];
    int next_int = 1, next_flt = 0;
    for (uint32_t i = 0; i < f->sig->nparams && i < e->nparams; i++) {
      ValType t = f->sig->params[i];
      if (vt_is_vec_reg(t)) { if (next_flt < 8) hint[e->params[i]] = next_flt++; else next_flt++; }
      else { if (next_int <= 7) hint[e->params[i]] = next_int++; else next_int++; }
    }
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      Block *b = &f->blocks[f->order[oi]];
      if (b->term == T_RETURN && f->sig->nresults == 1 && b->nret == 1)
        if (hint[b->ret_args[0]] < 0) hint[b->ret_args[0]] = 0;
      for (uint32_t i = 0; i < b->ninsts; i++) {
        Ref r2 = b->insts[i];
        Inst *in = &f->insts[r2];
        if (in->op != I_CALL && in->op != I_CALL_INDIRECT &&
            in->op != I_CALL_REF) continue;
        uint32_t na = in->nargs - (in->op == I_CALL ? 0 : 1);
        Ref *a = ir_args(f, r2);
        int ni = 1, nf = 0;
        for (uint32_t j = 0; j < na; j++) {
          if (is_fp_value(f, a[j])) { if (nf < 8 && hint[a[j]] < 0) hint[a[j]] = nf; nf++; }
          else { if (ni <= 7 && hint[a[j]] < 0) hint[a[j]] = ni; ni++; }
        }
        if (in->type != 0 && hint[r2] < 0) hint[r2] = 0;
      }
    }
  }

  uint32_t *web = xmalloc(n * sizeof(uint32_t));
  for (uint32_t i = 0; i < n; i++) web[i] = i;

  uint32_t *mem_head = xmalloc(n * sizeof(uint32_t));
  uint32_t *mem_tail = xmalloc(n * sizeof(uint32_t));
  uint32_t *mem_next = xmalloc(n * sizeof(uint32_t));
  for (uint32_t i = 0; i < n; i++) {
    mem_next[i] = UINT32_MAX;
    bool live_i = iv[i].end >= 0;
    mem_head[i] = live_i ? i : UINT32_MAX;
    mem_tail[i] = live_i ? i : UINT32_MAX;
  }

  IrEdgeIdx ei = ir_edge_index(f);

  uint32_t (*find)(uint32_t *, uint32_t) = NULL;
  (void)find;
#define FIND(x) ({ uint32_t _r = (x); while (web[_r] != _r) _r = web[_r]; _r; })

  const char *co_only = warmc_getenv("WARMC_COALESCE_ONLY");
  const char *co_upto = warmc_getenv("WARMC_COALESCE_UPTO");
  const char *co_max_s = warmc_getenv("WARMC_COALESCE_MAX");
  int co_max = co_max_s ? atoi(co_max_s) : -1, co_done = 0;
  bool co_dbg = warmc_getenv("WARMC_COALESCE_DEBUG") != NULL;
  bool co_ok = !warmc_getenv("WARMC_NO_COALESCE");
  if (co_ok && co_only) co_ok = (uint32_t)atoi(co_only) == f->func_index;
  if (co_ok && co_upto) co_ok = f->func_index <= (uint32_t)atoi(co_upto);
  if (co_ok)
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t p = 0; p < b->nparams; p++) {
      Ref param = b->params[p];

      if (s->pinned[param]) continue;
      if (iv[param].end < 0) continue;

      if (iv[param].end == iv[param].start) continue;
      for (uint32_t k = ei.off[bi]; k < ei.off[bi + 1]; k++) {
        uint32_t src = ei.src[k];
        Block *sb = &f->blocks[src];
        if (!sb->reachable) continue;

        /* A vetoed param keeps its entry edges as moves: the split plan
           cut its web at the loop boundary, and re-merging would hand the
           in-loop piece straight back to the spill it just escaped.  Back
           edges still coalesce -- the cut is only ever at the entry. */
        if (s->split_sc && s->split_sc->veto && param < s->split_sc->veto_n &&
            s->split_sc->veto[param] && sb->rpo < b->rpo)
          continue;
        {
          uint32_t sidx = ei.si[k];
          Ref arg = sb->succs[sidx].args[p];
          if (iv[arg].end < 0) continue;
          if (s->cls[arg] != s->cls[param]) continue;
          uint32_t ra = FIND(arg), rp = FIND(param);
          if (ra == rp) continue;
          if (s->pinned[arg] || s->pinned[ra] || s->pinned[rp]) continue;

          if (web_conflict(iv, mem_head[ra], mem_head[rp], mem_next)) continue;
          if (co_max >= 0 && co_done >= co_max) continue;
          if (co_dbg)
            fprintf(stderr, "coalesce[%u] #%d: v%u[%d,%d) <- v%u[%d,%d)"
                            " (block %u param %u, edge from %u)\n",
                    f->func_index, co_done, rp, iv[rp].start, iv[rp].end,
                    ra, iv[ra].start, iv[ra].end, bi, p, src);
          co_done++;
          web[ra] = rp;

          if (mem_head[ra] != UINT32_MAX) {
            if (mem_head[rp] == UINT32_MAX) {
              mem_head[rp] = mem_head[ra];
              mem_tail[rp] = mem_tail[ra];
            } else {
              web_merge(iv, &mem_head[rp], &mem_tail[rp], mem_head[ra], mem_next);
            }
            mem_head[ra] = mem_tail[ra] = UINT32_MAX;
          }
          g_stats.moves_coalesced++;
        }
      }
    }
  }
  ir_edge_index_free(&ei);
  for (uint32_t i = 0; i < n; i++) web[i] = FIND(i);

  for (uint32_t i = 0; i < n; i++) {
    if (iv[i].end < 0 || web[i] == i) continue;
    uint32_t rt = web[i];
    if (iv[rt].end < 0) { iv[rt].start = iv[i].start; iv[rt].end = iv[i].end; }
    else { extend(&iv[rt], iv[i].start); extend(&iv[rt], iv[i].end); }
    if (iv[i].crosses_call) iv[rt].crosses_call = 1;
  }

  Range *R = NULL;
  uint32_t nR = 0;
  if (!warmc_getenv("WARMC_NO_HOLES")) {
    int32_t *fpos = xmalloc(n * sizeof(int32_t));
    int32_t *lpos = xmalloc(n * sizeof(int32_t));
    uint32_t *stamp = xcalloc(n ? n : 1, sizeof(uint32_t));
    uint32_t *touched = xmalloc(n * sizeof(uint32_t));
    uint32_t *rcnt = xcalloc(n ? n : 1, sizeof(uint32_t));
    uint32_t *roff = xmalloc((n + 1) * sizeof(uint32_t));
    int32_t *tail = xmalloc((n ? n : 1) * sizeof(int32_t));
    uint32_t *rstamp = xcalloc(n ? n : 1, sizeof(uint32_t));
    BlkRange *bent = xmalloc((n ? n : 1) * sizeof(BlkRange));
    uint64_t budget = 0;
    uint32_t stamp_ctr = 0;
    bool over_budget = false;

    for (int pass = 0; pass < 2 && !over_budget; pass++) {
      if (pass == 1) {
        roff[0] = 0;
        for (uint32_t i = 0; i < n; i++) roff[i + 1] = roff[i] + rcnt[i];
        nR = roff[n];
        R = xmalloc((nR ? nR : 1) * sizeof(Range));
        memset(rcnt, 0, (size_t)n * sizeof(uint32_t));
      }
      for (uint32_t oi = 0; oi < f->norder && !over_budget; oi++) {
        uint32_t bi = f->order[oi];
        Block *b = &f->blocks[bi];
        if (!b->reachable) continue;
        uint32_t cur_stamp = ++stamp_ctr, nt = 0;

#define TOUCH(v_, p_) do {                                                     \
          uint32_t v__ = (v_); int32_t p__ = (p_);                             \
          if (stamp[v__] != cur_stamp) {                                       \
            stamp[v__] = cur_stamp; fpos[v__] = lpos[v__] = p__;               \
            touched[nt++] = v__;                                               \
          } else {                                                             \
            if (p__ < fpos[v__]) fpos[v__] = p__;                              \
            if (p__ > lpos[v__]) lpos[v__] = p__;                              \
          }                                                                    \
        } while (0)

        for (uint32_t i = 0; i < b->nparams; i++) TOUCH(b->params[i], blk_start[bi]);
        for (uint32_t i = 0; i < b->ninsts; i++) {
          Ref r = b->insts[i];
          if (num[r] >= 0) TOUCH(r, num[r]);
          uint32_t ne = eff_uses(s, f, r, eu, eucap);
          for (uint32_t j = 0; j < ne; j++)
            if (num[eu[j]] >= 0) TOUCH(eu[j], num[r]);
        }
        if (b->term == T_BRIF && s->fused && s->fused[b->term_arg]) {
          uint32_t ne = eff_uses(s, f, b->term_arg, eu, eucap);
          for (uint32_t j = 0; j < ne; j++) {
            Ref u = eff_arg(s, f, eu[j]);
            if (num[u] >= 0) TOUCH(u, blk_end[bi]);
          }
        } else if (b->term == T_BRIF || b->term == T_BRTABLE) {
          if (num[b->term_arg] >= 0) TOUCH(b->term_arg, blk_end[bi]);
        }
        for (uint32_t i = 0; i < b->nret; i++)
          if (num[b->ret_args[i]] >= 0) TOUCH(b->ret_args[i], blk_end[bi]);
        for (uint32_t sidx = 0; sidx < b->nsuccs; sidx++)
          for (uint32_t j = 0; j < b->succs[sidx].nargs; j++)
            if (num[b->succs[sidx].args[j]] >= 0)
              TOUCH(b->succs[sidx].args[j], blk_end[bi]);

        for (uint32_t k = lin_off[bi]; k < lin_off[bi + 1]; k++)
          if (num[lin[k]] >= 0) TOUCH(lin[k], blk_start[bi]);
        for (uint32_t k = lout_off[bi]; k < lout_off[bi + 1]; k++)
          if (num[lout[k]] >= 0) TOUCH(lout[k], blk_end[bi]);
#undef TOUCH

        /* The block's range for each value, merged on to its web's last
           range as it comes: blocks come in position order, so only a web
           with two values here can have ranges out of order, and those are
           sorted first.  The budget still counts every range. */
        uint32_t ne = 0;
        bool dup = false;
        for (uint32_t t = 0; t < nt; t++) {
          uint32_t v = touched[t], rep = web[v] < n ? web[v] : v;
          if (iv[rep].end < 0) continue;
          if (pass == 0 && ++budget > RANGE_BUDGET) {
            over_budget = true;
            g_stats.range_budget_trips++;
            break;
          }
          if (rstamp[rep] == cur_stamp) dup = true;
          rstamp[rep] = cur_stamp;
          bent[ne++] = (BlkRange){ rep, fpos[v], lpos[v] };
        }
        if (over_budget) break;
        if (dup) sort_blk_ranges(bent, ne);
        for (uint32_t k = 0; k < ne; k++) {
          uint32_t rep = bent[k].rep;
          int32_t lo = bent[k].lo, hi = bent[k].hi;
          if (pass == 0) {
            if (rcnt[rep] && lo <= tail[rep] + 1) {
              if (hi > tail[rep]) tail[rep] = hi;
            } else {
              rcnt[rep]++;
              tail[rep] = hi;
            }
          } else {
            Range *last = rcnt[rep] ? &R[roff[rep] + rcnt[rep] - 1] : NULL;
            if (last && lo <= last->hi + 1) {
              if (hi > last->hi) last->hi = hi;
            } else {
              R[roff[rep] + rcnt[rep]++] = (Range){ lo, hi };
            }
          }
        }
      }
    }

    if (R)
      for (uint32_t i = 0; i < n; i++)
        if (rcnt[i]) {
          iv[i].rfirst = roff[i];
          iv[i].rcount = rcnt[i];
        }
    xfree(fpos); xfree(lpos); xfree(stamp); xfree(touched); xfree(rcnt); xfree(roff);
    xfree(tail); xfree(rstamp); xfree(bent);

  }

  Interval *sorted = xmalloc((n ? n : 1) * sizeof(Interval));
  uint32_t ns = 0;
  for (uint32_t i = 0; i < n; i++)
    if (iv[i].end >= 0 && value_needs_location(f, i) && web[i] == i &&
        !skip_location(s, i) && !s->pinned[i]) sorted[ns++] = iv[i];
  sort_intervals(sorted, ns);

  /* weight prices a spill the classic way, one operation per reference, and
     stays the eviction currency.  wlat prices what the emitter would really
     execute: the reload cache hands repeated loads of a slot back for free
     until a block boundary or a clobber clears it, so a spilled GPR value
     costs its definition's store plus one load per use segment, not one per
     use.  This is the number the caller-save gate compares against, because
     a value whose uses cluster after one call spills for the same two
     operations a save/restore pair costs -- without putting a load on the
     call's critical path.  The two-entry capacity of the cache is ignored,
     which can only underprice spilling and so only withhold a grant. */
  double *weight = xcalloc(n ? n : 1, sizeof(double));
  double *wlat = xcalloc(n ? n : 1, sizeof(double));
  /* wuse and wdef split weight into reads and the definition: what an
     assignment that rematerialises a value pays is per read segment, wlat
     less its definition. */
  double *wuse = xcalloc(n ? n : 1, sizeof(double));
  double *wdef = xcalloc(n ? n : 1, sizeof(double));
  uint32_t *lstamp = xcalloc(n ? n : 1, sizeof(uint32_t));
  uint32_t lseg = 0;
  /* cumf[p] sums the frequency of the positions before p, the weight a
     reference there would get, for the frequency-weighted spans of the
     density rules (iv_wspan). */
  double *cumf = xcalloc((size_t)maxpos + 2, sizeof(double));
  {
    double *pf = xcalloc((size_t)maxpos + 1, sizeof(double));
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      uint32_t bi = f->order[oi];
      uint32_t d = f->blocks[bi].loop_depth;
      double w = 1.0;
      for (uint32_t k = 0; k < d && k < 8; k++) w *= 10.0;
      if (f->blocks[bi].cold) w = 0.01;
      for (int32_t p = blk_start[bi]; p <= blk_end[bi] && p < maxpos; p++) pf[p] = w;
    }
    for (int32_t p = 0; p < maxpos; p++) cumf[p + 1] = cumf[p] + pf[p];
    xfree(pf);
  }
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    uint32_t d = f->blocks[bi].loop_depth;
    double w = 1.0;
    for (uint32_t k = 0; k < d && k < 8; k++) w *= 10.0;
    /* A cold block's references are what its loop depth says they would be
       once in a while, not every time round: the resume copy of a Go loop
       carries the same values the loop does, and pricing its uses like the
       loop's gave those values the registers the loop needed. */
    if (f->blocks[bi].cold) w = 0.01;
    lseg++;
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref r2 = b->insts[i];
      Inst *in = &f->insts[r2];
      Ref *a = ir_args(f, r2);
      if (op_clobbers_caller_saved(in->op)) lseg++;
      weight[web[r2]] += w;
      wlat[web[r2]] += w;
      wdef[web[r2]] += w;
      uint32_t ne = eff_uses(s, f, r2, eu, eucap);
      (void)a;
      for (uint32_t j = 0; j < ne; j++) {
        uint32_t rep = web[eu[j]];
        weight[rep] += w;
        wuse[rep] += w;
        if (s->cls[eu[j]] == 0) {
          if (lstamp[rep] != lseg) { lstamp[rep] = lseg; wlat[rep] += w; }
        } else {
          wlat[rep] += w;
        }
      }
    }
    if (b->term == T_BRIF || b->term == T_BRTABLE) {
      weight[web[b->term_arg]] += w;
      wlat[web[b->term_arg]] += w;
      wuse[web[b->term_arg]] += w;
    }
    for (uint32_t i = 0; i < b->nret; i++) {
      weight[web[b->ret_args[i]]] += w;
      wlat[web[b->ret_args[i]]] += w;
      wuse[web[b->ret_args[i]]] += w;
    }
    for (uint32_t sidx = 0; sidx < b->nsuccs; sidx++)
      for (uint32_t j = 0; j < b->succs[sidx].nargs; j++) {
        weight[web[b->succs[sidx].args[j]]] += w;
        wlat[web[b->succs[sidx].args[j]]] += w;
        wuse[web[b->succs[sidx].args[j]]] += w;
      }
  }
  xfree(lstamp);

  /* A rematerialised global pays a load at every use; a spilled one pays one
     cached reload per use segment.  A global whose uses cluster
     many-per-segment is cheaper in a slot, so it loses its remat eligibility
     here -- weight counts uses, wlat counts segments, and both are final at
     this point.  A multi-instruction constant does not: rd() builds it into
     the reload cache once per segment, which is what a slot would cost there,
     without the slot's def -- a chain and a store wherever LICM put it. */
  for (uint32_t i = 0; i < n; i++) {
    if (web[i] != i || !remat_ok(s, i)) continue;
    Inst *ri = &f->insts[i];

    /* A rematerialised global pays one load per use where a slot pays one
       cached reload per use segment (plus the def's store).  Uses split
       into their own segments wherever calls intervene -- exactly the
       shape that spilled these -- so remat wins there and loses where
       uses cluster; the same segment knob draws the line. */
    if (ri->op == I_GLOBAL_GET) {
      if (weight[i] > s->remat2_seg * wlat[i]) {
        s->no_remat[i] = 1;
        continue;
      }
      weight[i] *= remat_discount(s, i);
      continue;
    }
    weight[i] *= remat_discount(s, i);
  }

  int32_t gpr_end[32], fpr_end[32];
  int32_t gpr_owner[32], fpr_owner[32];
  OccHeap gpr_occ[32], fpr_occ[32];
  memset(gpr_occ, 0, sizeof gpr_occ);
  memset(fpr_occ, 0, sizeof fpr_occ);
  uint8_t *gone = xcalloc(ns ? ns : 1, 1);
  OStack ostack = { NULL, 0 };
  for (int i = 0; i < 32; i++) {
    gpr_end[i] = -1; fpr_end[i] = -1;
    gpr_owner[i] = -1; fpr_owner[i] = -1;
  }

  int gpool[NGPR_ALLOC + 8];
  int ngpool = 0;
  for (int i = 0; i < NGPR_ALLOC; i++) {

    if (s->lab_reg && GPR_ALLOC[i] == s->lab_reg) continue;
    if (s->tid_reg && GPR_ALLOC[i] == s->tid_reg) continue;

    if (GPR_ALLOC[i] == s->vmctx_reg || GPR_ALLOC[i] == s->membase_reg) continue;
    gpool[ngpool++] = GPR_ALLOC[i];
  }
  if (!s->needs_memsize) gpool[ngpool++] = REG_MEMSIZE;
  if (RA_HAVE_RETAREA_REUSE && !s->needs_retarea && f->sig->nresults <= 1)
    gpool[ngpool++] = RA_RETAREA_REG;
  /* The registers the back end lends besides (isel_spare_gprs) cost a save
     and a restore on every call, on the entry path, whichever way the call
     then goes, so they are a last resort: a value takes one only when no
     other register is free and it would otherwise go to a slot, and only a
     value read in a loop, where the slot would be paid round and round, and
     not a constant, which is remade where it is used instead.  Offered
     like the others, they went to values that fitted without them, a
     string switch called millions of times paid two more pairs of saves on
     every call, and the registers they freed went to constants. */
  uint32_t spare = 0;
  {
    int nsp = isel_spare_gprs(s, gpool + ngpool);
    for (int k = 0; k < nsp; k++) spare |= 1u << gpool[ngpool + k];
    ngpool += nsp;
  }

  /* The same over the contested positions of each class -- where more of
     its values are live than it has registers, so that a register held there
     keeps some other value out of one -- for EV_CONTESTED (iv_cspan). */
  double *cumc[2];
  {
    int32_t *dl[2];
    for (int c = 0; c < 2; c++) {
      dl[c] = xcalloc((size_t)maxpos + 2, sizeof(int32_t));
      cumc[c] = xcalloc((size_t)maxpos + 2, sizeof(double));
    }
    for (uint32_t i = 0; i < ns; i++) {
      const Interval *X = &sorted[i];
      int c = X->cls ? 1 : 0;
      if (!X->rcount) { dl[c][X->start]++; dl[c][X->end + 1]--; }
      else
        for (uint32_t y = 0; y < X->rcount; y++) {
          dl[c][R[X->rfirst + y].lo]++;
          dl[c][R[X->rfirst + y].hi + 1]--;
        }
    }
    int K[2] = { ngpool, NFPR_ALLOC };
    for (int c = 0; c < 2; c++) {
      int32_t live = 0;
      for (int32_t p = 0; p < maxpos; p++) {
        live += dl[c][p];
        double fp = cumf[p + 1] - cumf[p];
        cumc[c][p + 1] = cumc[c][p] + (live > K[c] ? fp : 0);
      }
      xfree(dl[c]);
    }
  }

  uint32_t slot = 0;
  for (uint32_t i = 0; i < n; i++)
    if (s->pinned[i] && value_needs_location(f, i) && !skip_location(s, i))
      s->loc[i] = SLOT(take_slot(s, i, &slot));

  int *web_hint = xmalloc((n ? n : 1) * sizeof(int));
  for (uint32_t v = 0; v < n; v++) web_hint[v] = -1;
  for (uint32_t v = 0; v < n; v++)
    if (hint[v] >= 0 && web[v] < n && web_hint[web[v]] < 0)
      web_hint[web[v]] = hint[v];

  bool no_tie = warmc_getenv("WARMC_NO_TIE") != NULL;
  bool no_hint = warmc_getenv("WARMC_NO_HINT") != NULL;
  bool no_evict = warmc_getenv("WARMC_NO_EVICT") != NULL;

  const char *csa_only = warmc_getenv("WARMC_CSA_ONLY");
  const char *csa_upto = warmc_getenv("WARMC_CSA_UPTO");
  const char *csa_max_s = warmc_getenv("WARMC_CSA_MAX");
  int csa_max = csa_max_s ? atoi(csa_max_s) : -1, csa_done = 0;
  bool csa_dbg = warmc_getenv("WARMC_CSA_DEBUG") != NULL;
  const char *cold_div_s = warmc_getenv("WARMC_CSA_COLD_DIV");
  double csa_cold_div = cold_div_s ? atof(cold_div_s) : 1.0;
  const char *margin_s = warmc_getenv("WARMC_CSA_MARGIN");
  double csa_margin = margin_s ? atof(margin_s) : 1.0;
  bool csa_lat = !warmc_getenv("WARMC_CSA_NO_LAT");
  bool csa_ok = !warmc_getenv("WARMC_NO_CSA");
  if (csa_ok && csa_only) csa_ok = (uint32_t)atoi(csa_only) == f->func_index;
  if (csa_ok && csa_upto) csa_ok = f->func_index <= (uint32_t)atoi(csa_upto);
  uint64_t csa_work = 0;

  /* A reference must be in a callee-saved register or a slot at every
     safepoint, and the unwinder restores only callee-saved state when it
     resumes at a pad; both make the whole web ineligible. */
  uint8_t *inelig = xcalloc(n ? n : 1, 1);
  if (csa_ok) {
    for (uint32_t i = 0; i < n; i++)
      if (iv[i].end >= 0 && vt_is_ref((ValType)f->insts[i].type))
        inelig[web[i]] = 1;
    for (uint32_t bi = 0; bi < f->nblocks; bi++) {
      Block *b = &f->blocks[bi];
      if (!b->reachable || !b->is_pad) continue;
      for (uint32_t k = lin_off[bi]; k < lin_off[bi + 1]; k++)
        inelig[web[lin[k]]] = 1;
    }
  }

  /* ---- the scan, under several eviction rules ----
     Which interval to give up when every register is taken is the one
     decision a linear scan cannot take back, and no single rule takes it well
     everywhere.  Comparing spill weights keeps the values a hot loop reads
     most; but in one long block, where loaded values wait hundreds of
     positions for their uses, it hands them every register and spills the
     short temporaries between, each of which still needs a register at its
     definition and its use.  Comparing weight per position held (density)
     takes that right and gets loops wrong, where a long value's positions
     are cheap to hold but its few uses are hot.  So the scan runs once per
     rule, each assignment is priced by what its spills, rematerialisations
     and caller saves would execute, and the cheapest is kept; the rules
     differ only in how a victim is chosen.  The weight rule is the
     incumbent: another must price below RA_TRIAL_MARGIN of it to be taken,
     since the price rests on the same static frequency guess the weights
     do.  An AOT compiler can afford the extra passes. */
  static const uint8_t trial_rule[][2] = {
    { EV_WEIGHT, EV_DENSITY }, { EV_UNIFORM, EV_DENSITY },
    { EV_WDENSITY, EV_DENSITY }, { EV_CONTESTED, EV_DENSITY },
    { EV_DENSITY, EV_DENSITY },
  };
  int ntrial = warmc_getenv("WARMC_NO_RA_TRIALS")
                   ? 0 : (int)(sizeof trial_rule / sizeof trial_rule[0]);
  bool trial_dbg = warmc_getenv("WARMC_RA_TRIAL_DEBUG") != NULL;
  uint32_t slot0 = slot;
  uint32_t used_gcs0 = s->used_gpr_cs, used_fcs0 = s->used_fpr_cs;
  uint64_t st_cs0 = g_stats.caller_saves, st_sp0 = g_stats.spills,
           st_spv0 = g_stats.spills_v128_xcall;
  double best_cost = 1e300, base_cost = 1e300;
  int best_rule = 0;
  for (int trial = 0; trial <= ntrial; trial++) {
    bool final = trial == ntrial;
    int rule_i = final ? best_rule : trial;
    if (trial > 0) {
      for (int r = 0; r < 32; r++) {
        gpr_end[r] = fpr_end[r] = -1;
        gpr_owner[r] = fpr_owner[r] = -1;
        gpr_occ[r].n = gpr_occ[r].dead = 0;
        fpr_occ[r].n = fpr_occ[r].dead = 0;
      }
      memset(gone, 0, ns ? ns : 1);
      for (uint32_t i = 0; i < ns; i++) {
        s->loc[sorted[i].value] = LOC_NONE;
        sorted[i].caller_saved_ok = 0;
      }
      if (s->csa_on) {
        memset(s->csa_on, 0, n ? n : 1);
        for (uint32_t z = 0; z < n; z++) s->csa_slot[z] = -1;
      }
      slot = slot0;
      s->used_gpr_cs = used_gcs0; s->used_fpr_cs = used_fcs0;
      g_stats.caller_saves = st_cs0; g_stats.spills = st_sp0;
      g_stats.spills_v128_xcall = st_spv0;
      csa_done = 0; csa_work = 0;
    }
    for (uint32_t i = 0; i < ns; i++) {
      Interval *cur = &sorted[i];
      int32_t *ends = cur->cls ? fpr_end : gpr_end;
      int32_t *owner = cur->cls ? fpr_owner : gpr_owner;
      OccHeap *heaps = cur->cls ? fpr_occ : gpr_occ;
      const int *pool = cur->cls ? FPR_ALLOC : gpool;
      int npool = cur->cls ? NFPR_ALLOC : ngpool;
      int32_t vic;

      /* Callees preserve only the low 64 bits of v8-v15, so for a V128 no
         register is safe across a call: the preference for callee-saved
         registers stands, but never satisfies a crossing. */
      bool v128 = (ValType)f->insts[cur->value].type == VT_V128;

      if (hint[cur->value] < 0 && cur->value < n)
        hint[cur->value] = web_hint[cur->value];

      bool zero = ra_zero_const(s, cur->value, cur->cls);
      if (zero && cur->crosses_call) {
        s->loc[cur->value] = LOC_NONE;
        continue;
      }
      bool spare_ok = cur->cls == 0 && weight[cur->value] >= RA_SPARE_MIN_W &&
                      !remat_ok(s, cur->value);
      uint32_t barred = spare_ok || cur->cls ? 0 : spare;

      int chosen = -1;
      if (!no_tie) {
        Ref ta = tied_operand_of(f, cur->value);
        int tl = ta == REF_NONE ? LOC_NONE : s->loc[ta];
        /* The operand's own interval has to end here; the register's end can
           be later, when the operand sat in a hole of a value that lives on
           past it -- the occupancy check below finds whether this one fits
           in that hole too.  A popcount that a running sum lent its register
           to is that: the wrap of the count found the sum's end there. */
        if (ta != REF_NONE && tl != LOC_NONE && !IS_SLOT(tl) && tl >= 0 &&
            owner[tl] >= 0 && sorted[owner[tl]].value == ta &&
            sorted[owner[tl]].end == cur->start) {
          bool cs = !v128 &&
                    (cur->cls ? fpr_is_callee_saved(tl) : gpr_is_callee_saved(tl));
          bool in_pool = false;
          for (int k = 0; k < npool; k++) if (pool[k] == tl) { in_pool = true; break; }
          int nc = occ_conflicts(&heaps[tl], cur, R, gone, &ostack, &vic);
          bool only_tied = nc == 0 || (nc == 1 && sorted[vic].value == ta);
          if (in_pool && only_tied && (!cur->crosses_call || cs)) chosen = tl;
        }
      }
      int want = no_hint ? -1 : hint[cur->value];
      if (chosen < 0 && want >= 0 &&
          occ_conflicts(&heaps[want], cur, R, gone, &ostack, &vic) == 0) {
        bool cs = !v128 && (cur->cls ? fpr_is_callee_saved(want)
                                     : gpr_is_callee_saved(want));
        bool in_pool = false;
        for (int k = 0; k < npool; k++) if (pool[k] == want) { in_pool = true; break; }
        if (in_pool && !((barred >> want) & 1) && (!cur->crosses_call || cs) &&
            !(zero && cs))
          chosen = want;
      }
      for (int pass = 0; pass < (zero ? 1 : 2) && chosen < 0; pass++) {
        for (int k = 0; k < npool; k++) {
          int rr = pool[k];
          bool cs = cur->cls ? fpr_is_callee_saved(rr) : gpr_is_callee_saved(rr);
          if (cur->crosses_call && (v128 || !cs)) continue;
          if ((spare >> rr) & 1 && !cur->cls) continue;
          bool want_cs = cur->crosses_call ? true : (pass == 1);
          if (cs != want_cs) continue;
          if (occ_conflicts(&heaps[rr], cur, R, gone, &ostack, &vic) != 0) continue;
          chosen = rr;
          break;
        }
      }

      if (chosen < 0 && zero) {
        s->loc[cur->value] = LOC_NONE;
        continue;
      }
      if (chosen < 0) {

        int victim_reg = -1;
        int32_t victim_idx = -1;
        /* The victim is the conflicting interval cur outranks by the widest
           margin under this pass's rule (ev_ratio), if any does.  Among float
           values the density rule is always the one: a long value read now
           and then before the short temporaries of a block of arithmetic,
           each of which, spilled, still needs a register at its definition and
           its use -- compared by weight alone, every temporary of an unrolled
           n-body step spilled itself. */
        int rule = trial_rule[rule_i][cur->cls ? 1 : 0];
        double cw = weight[cur->value];
        double best_r = 1.0, victim_w = cw;
        for (int k = 0; k < npool; k++) {
          int rr = pool[k];
          bool cs = cur->cls ? fpr_is_callee_saved(rr) : gpr_is_callee_saved(rr);
          if (cur->crosses_call && (v128 || !cs)) continue;
          if ((barred >> rr) & 1) continue;
          int32_t cand;
          if (occ_conflicts(&heaps[rr], cur, R, gone, &ostack, &cand) != 1) continue;
          const Interval *V = &sorted[cand];
          double w = weight[V->value];
          double r = no_evict ? 0
                   : ev_ratio(rule, cur, cw, V, w, R, cumf, cumc[cur->cls ? 1 : 0]);
          if (r > best_r) {
            best_r = r; victim_w = w; victim_reg = rr; victim_idx = cand;
          }
        }
        /* A free spare register is better than a slot, for this value or for
           the victim, but not better than remaking a constant victim where it
           is used, which costs no frame at all. */
        int spare_reg = -1;
        if (spare_ok && !(victim_reg >= 0 && remat_ok(s, sorted[victim_idx].value)))
          for (int k = 0; k < npool && spare_reg < 0; k++) {
            int rr = pool[k];
            if ((spare >> rr) & 1 &&
                occ_conflicts(&heaps[rr], cur, R, gone, &ostack, &vic) == 0)
              spare_reg = rr;
          }
        /* A call-crossing value the callee-saved registers cannot hold may
           instead take a caller-saved register plus a private slot, paying two
           frame operations per crossed site.  Priced against what it would
           otherwise cost: the eviction victim's whole-life spill, or its own. */
        int csa_reg = -1;
        double csa_cost = 0;
        if (csa_ok && cur->crosses_call && spare_reg < 0 &&
            !inelig[cur->value] && csa_work <= CSA_BUDGET &&
            !(csa_max >= 0 && csa_done >= csa_max)) {
          double price = remat_ok(s, cur->value)
                             ? weight[cur->value] * remat_discount(s, cur->value)
                             : (csa_lat ? wlat[cur->value] : weight[cur->value]);
          if (victim_reg >= 0 && victim_w < price) price = victim_w;
          csa_cost = csa_crossing_cost(cur, R, num[cur->value], call_pos,
                                       call_depth, call_cold, NULL, ncalls,
                                       csa_cold_div, &csa_work, NULL);
          if (csa_cost * csa_margin < price || warmc_getenv("WARMC_CSA_FORCE"))
            for (int k = 0; k < npool; k++) {
              int rr = pool[k];

              /* Callee-saved was already tried (and is no protection for a
                 V128); the allocation fast path writes RA_ALLOC_RES_REG with
                 no call. */
              if (cur->cls
                      ? fpr_is_callee_saved(rr)
                      : (gpr_is_callee_saved(rr) || rr == RA_ALLOC_RES_REG))
                continue;
              if (occ_conflicts(&heaps[rr], cur, R, gone, &ostack, &vic) != 0)
                continue;
              csa_reg = rr;
              break;
            }
        }
        if (spare_reg >= 0) {
          chosen = spare_reg;
        } else if (csa_reg >= 0) {
          if (!s->csa_on) {
            s->csa_on = xcalloc(n ? n : 1, 1);
            s->csa_slot = xmalloc((n ? n : 1) * sizeof(int32_t));
            for (uint32_t z = 0; z < n; z++) s->csa_slot[z] = -1;
          }
          cur->caller_saved_ok = 1;
          s->csa_on[cur->value] = 1;
          s->csa_slot[cur->value] = (int32_t)take_slot(s, cur->value, &slot);
          g_stats.caller_saves++;
          csa_done++;
          if (csa_dbg)
            fprintf(stderr, "csa[%u] v%u r%d cost=%.1f w=%.0f wl=%.0f\n",
                    f->func_index, cur->value, csa_reg, csa_cost,
                    weight[cur->value], wlat[cur->value]);
          chosen = csa_reg;
        } else if (victim_reg < 0) {

          if (remat_ok(s, cur->value)) {
            s->loc[cur->value] = LOC_NONE;
            continue;
          }
          s->loc[cur->value] = SLOT(take_slot(s, cur->value, &slot));
          g_stats.spills++;
          if (cur->crosses_call && v128) g_stats.spills_v128_xcall++;
          if (warmc_getenv("WARMC_SPILL_DEBUG"))
            spill_debug(s, "self", cur, weight[cur->value]);
          continue;
        } else {
          Interval *victim = &sorted[victim_idx];
          gone[victim_idx] = 1;
          if (s->csa_on && s->csa_on[victim->value]) {

            /* Its saves are never emitted; the slot it holds becomes the spill
               slot below or is abandoned. */
            s->csa_on[victim->value] = 0;
            victim->caller_saved_ok = 0;
            g_stats.caller_saves--;
          }
          if (remat_ok(s, victim->value))
            s->loc[victim->value] = LOC_NONE;
          else {
            int32_t reuse = s->csa_slot ? s->csa_slot[victim->value] : -1;
            s->loc[victim->value] =
                SLOT(reuse >= 0 ? (uint32_t)reuse
                                : take_slot(s, victim->value, &slot));
            g_stats.spills++;
            if (warmc_getenv("WARMC_SPILL_DEBUG"))
              spill_debug(s, "evict", victim, weight[victim->value]);
          }
          chosen = victim_reg;
        }
      }

      if (cur->end > ends[chosen]) ends[chosen] = cur->end;
      owner[chosen] = (int32_t)i;
      {
        OccHeap *h = &heaps[chosen];
        if (cur->rcount)
          for (uint32_t y = 0; y < cur->rcount; y++)
            occ_push(h, R[cur->rfirst + y].lo, R[cur->rfirst + y].hi, (int32_t)i);
        else
          occ_push(h, cur->start, cur->end, (int32_t)i);
        if (h->dead > h->n / 2) occ_rebuild(h, gone, cur->start);
      }
      s->loc[cur->value] = chosen;
      if (cur->cls) { if (fpr_is_callee_saved(chosen)) s->used_fpr_cs |= 1u << chosen; }
      else { if (gpr_is_callee_saved(chosen)) s->used_gpr_cs |= 1u << chosen; }
    }
    /* Second chance: an interval that spilled itself because a register was
       held then may find that register free after all, once the holder was
       evicted later in the scan.  The scan's heaps forget ranges behind it, so
       this pass rebuilds each register's full assignment as a range list and
       places the self-spilled intervals, heaviest first, into any register
       free over all of their ranges -- not an evicted one, whose ranges the
       heaps still hold under the register it lost, and not a spare register,
       which only a value read in a loop may take. */
    {
      bool no_refill = warmc_getenv("WARMC_NO_REFILL") != NULL;
      RList rl[2][32];
      memset(rl, 0, sizeof rl);
      RefillCand *rc = xmalloc((ns ? ns : 1) * sizeof(RefillCand));
      uint32_t nrc = 0;
      for (uint32_t i = 0; i < ns; i++) {
        const Interval *X = &sorted[i];
        int l = s->loc[X->value];
        if (l == LOC_NONE || IS_SLOT(l)) {
          if (!gone[i]) { rc[nrc].idx = i; rc[nrc].w = weight[X->value]; nrc++; }
          continue;
        }
        RList *L = &rl[X->cls ? 1 : 0][l];
        if (X->rcount)
          for (uint32_t y = 0; y < X->rcount; y++)
            rl_add(L, R[X->rfirst + y].lo, R[X->rfirst + y].hi, false);
        else
          rl_add(L, X->start, X->end, false);
      }
      for (int c = 0; c < 2; c++)
        for (int r = 0; r < 32; r++)
          if (rl[c][r].n > 1)
            sort_ranges_lo(rl[c][r].v, rl[c][r].n);
      if (!no_refill) qsort(rc, nrc, sizeof(RefillCand), refill_cmp);
      for (uint32_t a = 0; a < nrc && !no_refill; a++) {
        Interval *X = &sorted[rc[a].idx];
        bool v128 = (ValType)f->insts[X->value].type == VT_V128;
        const int *pool = X->cls ? FPR_ALLOC : gpool;
        int npool = X->cls ? NFPR_ALLOC : ngpool;
        Range whole = { X->start, X->end };
        const Range *xr = X->rcount ? &R[X->rfirst] : &whole;
        uint32_t nx = X->rcount ? X->rcount : 1;
        int got = -1;
        bool zero = ra_zero_const(s, X->value, X->cls);
        for (int pass = 0; pass < 2 && got < 0; pass++)
          for (int k = 0; k < npool && got < 0; k++) {
            int rr = pool[k];
            bool cs = X->cls ? fpr_is_callee_saved(rr) : gpr_is_callee_saved(rr);
            if (zero && cs) continue;
            if (X->crosses_call && (v128 || !cs)) continue;
            if (!X->cls && ((spare >> rr) & 1)) continue;
            bool want_cs = X->crosses_call ? true : (pass == 1);
            if (cs != want_cs) continue;
            bool clear = true;
            for (uint32_t y = 0; y < nx && clear; y++)
              clear = rl_free(&rl[X->cls ? 1 : 0][rr], xr[y].lo, xr[y].hi);
            if (clear) got = rr;
          }
        if (got < 0) continue;
        for (uint32_t y = 0; y < nx; y++) {
          rl_add(&rl[X->cls ? 1 : 0][got], xr[y].lo, xr[y].hi, true);
          occ_push(&(X->cls ? fpr_occ : gpr_occ)[got], xr[y].lo, xr[y].hi,
                   (int32_t)rc[a].idx);
        }
        if (IS_SLOT(s->loc[X->value])) g_stats.spills--;
        s->loc[X->value] = got;
        if (X->cls) { if (fpr_is_callee_saved(got)) s->used_fpr_cs |= 1u << got; }
        else { if (gpr_is_callee_saved(got)) s->used_gpr_cs |= 1u << got; }
      }
      for (int c = 0; c < 2; c++)
        for (int r = 0; r < 32; r++) xfree(rl[c][r].v);
      xfree(rc);
    }
    if (final) break;
    /* The price of this pass's assignment: what its values without a
       register of their own will execute -- a spill's store and one reload
       per segment of reads (wlat), a rematerialisation's sequence per
       segment, a caller save's two frame operations per call it spans --
       plus a prologue save and epilogue restore per callee-saved register
       the function did not need before. */
    {
      double cost = 0;
      uint64_t cw2 = 0;
      for (uint32_t i = 0; i < ns; i++) {
        const Interval *X = &sorted[i];
        Ref v = X->value;
        int l = s->loc[v];
        if (l == LOC_NONE) {
          Inst *ri = &f->insts[v];
          double per = 1.0;
          if (ri->op == I_CONST && !vt_is_float((ValType)ri->type) &&
              (ValType)ri->type != VT_V128)
            per = ir_const_cost(ri->imm, vt_is_64((ValType)ri->type));
          else if (ri->op == I_GLOBAL_GET) per = 2.0;
          else if (vt_is_float((ValType)ri->type) || (ValType)ri->type == VT_V128)
            per = 2.0;
          /* one rebuild per segment of reads, the way wlat counts reloads:
             the pieces hold a rebuilt value across a cluster */
          double segs = wlat[v] - wdef[v];
          cost += per * (segs < wuse[v] ? segs : wuse[v]);
        } else if (IS_SLOT(l)) {
          cost += wlat[v];
        } else if (X->caller_saved_ok) {
          cost += csa_crossing_cost(X, R, num[v], call_pos, call_depth, call_cold,
                                    NULL, ncalls, csa_cold_div, &cw2, NULL);
        }
      }
      cost += 2.0 * (double)(__builtin_popcount(s->used_gpr_cs & ~used_gcs0) +
                             __builtin_popcount(s->used_fpr_cs & ~used_fcs0));
      if (trial_dbg)
        fprintf(stderr, "ratrial[%u] rule %d cost %.1f\n", f->func_index, trial, cost);
      if (trial == 0) { best_cost = cost; best_rule = 0; base_cost = cost; }
      else if (cost < best_cost * (1 - 1e-9) && cost < RA_TRIAL_MARGIN * base_cost) {
        best_cost = cost; best_rule = trial;
      }
    }
  }
  if (trial_dbg && ntrial)
    fprintf(stderr, "ratrial[%u] chose %d\n", f->func_index, best_rule);

  /* ---- callee-saved registers the prologue need not save ----
     A callee-saved register costs a save and a restore on every run of the
     function; a caller-saved one saved around a call costs them only where
     the call is.  When every value holding callee-saved registers crosses
     calls only on paths few of the function's runs take -- the growth path
     of a push inlined into a leaf that a loop calls -- the prologue pays
     more than saving them around those calls would, and the registers are
     given up: each one's values move to a caller-saved register free for
     all of them, and are saved around the calls they cross.  The paths are
     priced by ir_path_share, each crossing twice over for the reload it
     puts after its call, and only a prologue that really shrinks counts --
     a pair of registers saves as one instruction where the target pairs
     them.  A value crossing a call in a loop stays where it is: a loop
     behind a chain of early returns -- trial division by the small primes
     before the loop over the rest -- is what the even split most
     underprices, and a loop multiplies the error. */
  if (csa_ok && ncalls) {
    /* the GPR intervals holding each register, chained */
    int32_t head[32];
    int32_t *next = xmalloc((ns ? ns : 1) * sizeof(int32_t));
    for (int r = 0; r < 32; r++) head[r] = -1;
    for (uint32_t i = 0; i < ns; i++) {
      int l = s->loc[sorted[i].value];
      if (gone[i] || sorted[i].cls || l == LOC_NONE || IS_SLOT(l) || l >= 32)
        continue;
      next[i] = head[l];
      head[l] = (int32_t)i;
    }

    struct { int reg, home; double cost; } vac[32];
    int nvac = 0;
    for (int k = 0; k < ngpool && nvac < 32; k++) {
      int rr = gpool[k];
      if (!gpr_is_callee_saved(rr) || !(s->used_gpr_cs & (1u << rr))) continue;
      double c = 0;
      bool ok = true;
      for (int32_t i = head[rr]; i >= 0 && ok; i = next[i]) {
        Interval *x = &sorted[i];
        if (!x->crosses_call) continue;
        uint8_t deepest;
        c += 2.0 * csa_crossing_cost(x, R, num[x->value], call_pos,
                                     call_depth, call_cold, call_share,
                                     ncalls, csa_cold_div, &csa_work,
                                     &deepest);
        ok = !inelig[x->value] && !deepest;
      }
      if (!ok) continue;
      int j = nvac++;
      while (j && vac[j - 1].cost > c) { vac[j] = vac[j - 1]; j--; }
      vac[j].reg = rr; vac[j].home = -1; vac[j].cost = c;
    }

    /* The cheapest prefix whose prologue saving outweighs its crossings,
       cut short at the first register no caller-saved one can take in --
       checked against every interval holding the candidate, range by
       range, since the heaps have shed what the scan left behind. */
    uint32_t keep = s->used_gpr_cs;
    int before = ra_cs_frame_ops(s), take = 0;
    double best = 0, cost = 0;
    uint32_t homes = 0;
    for (int t = 0; t < nvac; t++) {
      for (int k = 0; k < ngpool && vac[t].home < 0; k++) {
        int c = gpool[k];
        if (gpr_is_callee_saved(c) || c == RA_ALLOC_RES_REG ||
            (homes & (1u << c)))
          continue;
        bool fits = true;
        for (int32_t i = head[vac[t].reg]; i >= 0 && fits; i = next[i])
          for (int32_t j = head[c]; j >= 0 && fits; j = next[j])
            fits = !iv_overlaps(&sorted[i], &sorted[j], R);
        if (fits) vac[t].home = c;
      }
      if (vac[t].home < 0) break;
      homes |= 1u << vac[t].home;
      s->used_gpr_cs &= ~(1u << vac[t].reg);
      cost += vac[t].cost;
      double gain = (double)(before - ra_cs_frame_ops(s)) - cost;
      if (gain > best) { best = gain; take = t + 1; }
    }
    s->used_gpr_cs = keep;
    for (int t = 0; t < take; t++) {
      int rr = vac[t].reg, c = vac[t].home;
      for (int32_t i = head[rr]; i >= 0; i = next[i]) {
        Interval *x = &sorted[i];
        if (x->rcount)
          for (uint32_t y = 0; y < x->rcount; y++)
            occ_push(&gpr_occ[c], R[x->rfirst + y].lo, R[x->rfirst + y].hi, i);
        else
          occ_push(&gpr_occ[c], x->start, x->end, i);
        s->loc[x->value] = c;
        if (!x->crosses_call) continue;
        if (!s->csa_on) {
          s->csa_on = xcalloc(n ? n : 1, 1);
          s->csa_slot = xmalloc((n ? n : 1) * sizeof(int32_t));
          for (uint32_t z = 0; z < n; z++) s->csa_slot[z] = -1;
        }
        x->caller_saved_ok = 1;
        s->csa_on[x->value] = 1;
        s->csa_slot[x->value] = (int32_t)take_slot(s, x->value, &slot);
        g_stats.caller_saves++;
        if (csa_dbg)
          fprintf(stderr, "csa[%u] v%u r%d vacates r%d\n", f->func_index,
                  x->value, c, rr);
      }
      gpr_occ[rr].n = 0;
      s->used_gpr_cs &= ~(1u << rr);
    }
    xfree(next);
  }

  xfree(s->recomp);
  s->recomp = NULL;
#if RA_HAVE_RECOMP
  if (!warmc_getenv("WARMC_NO_RECOMP"))
    mark_recompute(s, f, iv, R, web, num, blk_end, eu, eucap, n);
#endif

  /* ---- use-cluster pieces ----
     A value left without a register of its own reloads, or is rebuilt, at
     every read the reload cache misses.  Where it is read several times in
     one block with no call between, a register free across that stretch --
     the range lists know every assignment -- can hold it instead, filled
     once before the first read (isel's piece_fill).  Clusters are placed
     heaviest first; a piece only ever takes a register nothing else holds
     there, so it cannot cost a spill, and no callee-saved register the
     function does not already save. */
  bool pc_fn_ok = true;
  {
    const char *r = warmc_getenv("WARMC_PIECES_FUNCS");
    unsigned lo = 0, hi = ~0u;
    if (r && sscanf(r, "%u-%u", &lo, &hi) >= 1 &&
        (f->func_index < lo || f->func_index > hi)) pc_fn_ok = false;
  }
  if (pc_fn_ok && RA_HAVE_PIECES && !warmc_getenv("WARMC_NO_PIECES")) {
    /* every register's assignment, as the vacating pass left it */
    RList rl[2][32];
    memset(rl, 0, sizeof rl);
    for (uint32_t i = 0; i < ns; i++) {
      const Interval *X = &sorted[i];
      int l = s->loc[X->value];
      if (l == LOC_NONE || IS_SLOT(l) || l >= 32) continue;
      RList *L = &rl[X->cls ? 1 : 0][l];
      if (X->rcount)
        for (uint32_t y = 0; y < X->rcount; y++)
          rl_add(L, R[X->rfirst + y].lo, R[X->rfirst + y].hi, false);
      else
        rl_add(L, X->start, X->end, false);
    }
    for (int c = 0; c < 2; c++)
      for (int r = 0; r < 32; r++)
        if (rl[c][r].n > 1)
          sort_ranges_lo(rl[c][r].v, rl[c][r].n);
    struct Piece *lps = NULL;
    uint32_t nlps = 0, cap_lps = 0;
    if (!warmc_getenv("WARMC_NO_LOOP_PIECES"))
      place_loop_pieces(s, f, rl, blk_start, blk_end, lin_off, lin, web, iv, num,
                        gpool, ngpool, eu, eucap, n, &lps, &nlps, &cap_lps);
    /* (value, block) of each, sorted: a block's own pieces leave them out */
    uint64_t *lpkey = xmalloc((nlps ? nlps : 1) * sizeof(uint64_t));
    for (uint32_t k = 0; k < nlps; k++)
      lpkey[k] = (uint64_t)lps[k].v << 32 | lps[k].block;
    sort_u64(lpkey, nlps);
    PieceUse *pu = NULL; uint32_t npu = 0, cap_pu = 0;
    PieceCand *pcd = NULL; uint32_t npcd = 0, cap_pcd = 0;
    uint32_t *clob = NULL; uint32_t cap_clob = 0;
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      uint32_t bi = f->order[oi];
      Block *b = &f->blocks[bi];
      if (!b->reachable || b->ninsts < 2) continue;
      if (b->ninsts + 1 > cap_clob) {
        cap_clob = b->ninsts + 1;
        clob = xrealloc(clob, cap_clob * sizeof(uint32_t));
      }
      npu = 0;
      clob[0] = 0;
      for (uint32_t i = 0; i < b->ninsts; i++) {
        Ref r = b->insts[i];
        /* A call marshals its arguments into fixed registers, a piece's
           among them, so its reads are no piece's and a piece never spans
           one; a tail call likewise. */
        uint16_t rop = f->insts[r].op;
        bool barrier = op_clobbers_caller_saved(rop) || rop == I_RETURN_CALL ||
                       rop == I_RETURN_CALL_INDIRECT || rop == I_RETURN_CALL_REF;
        clob[i + 1] = clob[i] + (barrier ? 1 : 0);
        if (barrier) continue;
        uint32_t ne = eff_uses(s, f, r, eu, eucap);
        for (uint32_t j = 0; j < ne; j++) {
          Ref u = eu[j];
          if (u >= n || num[u] < 0 || iv[web[u]].end < 0) continue;
          if (!value_needs_location(f, u) || skip_location(s, u) || s->pinned[u])
            continue;
          int l = s->loc[web[u]];
          if (l != LOC_NONE && !IS_SLOT(l)) continue;
          if (nlps) {
            uint64_t key = (uint64_t)u << 32 | bi;
            uint32_t a = 0, z = nlps;
            while (a < z) {
              uint32_t m = (a + z) / 2;
              if (lpkey[m] < key) a = m + 1; else z = m;
            }
            if (a < nlps && lpkey[a] == key) continue;
          }
          if (npu == cap_pu) {
            cap_pu = cap_pu ? cap_pu * 2 : 64;
            pu = xrealloc(pu, cap_pu * sizeof(PieceUse));
          }
          pu[npu].v = u; pu[npu].i = i; npu++;
        }
      }
      if (npu > 1) qsort(pu, npu, sizeof(PieceUse), piece_use_cmp);
      double w = 1.0;
      for (uint32_t k = 0; k < b->loop_depth && k < 8; k++) w *= 10.0;
      if (b->cold) w = 0.01;
      /* each value's reads, split into runs with no barrier between */
      for (uint32_t c0 = 0; c0 < npu;) {
        uint32_t c1 = c0, npos = 1;
        while (c1 + 1 < npu && pu[c1 + 1].v == pu[c0].v &&
               clob[pu[c1 + 1].i] == clob[pu[c0].i]) {
          if (pu[c1 + 1].i != pu[c1].i) npos++;
          c1++;
        }
        if (npos >= 2) {
          if (npcd == cap_pcd) {
            cap_pcd = cap_pcd ? cap_pcd * 2 : 64;
            pcd = xrealloc(pcd, cap_pcd * sizeof(PieceCand));
          }
          pcd[npcd++] = (PieceCand){ pu[c0].v, bi, pu[c0].i, pu[c1].i,
                                     w * (double)(npos - 1) };
        }
        c0 = c1 + 1;
      }
    }
    if (npcd > 1) qsort(pcd, npcd, sizeof(PieceCand), piece_cand_cmp);
    struct Piece *pcs = xmalloc((npcd + nlps ? npcd + nlps : 1) * sizeof(struct Piece));
    uint32_t npcs = nlps;
    if (nlps) memcpy(pcs, lps, nlps * sizeof(struct Piece));
    xfree(lps);
    xfree(lpkey);
    for (uint32_t x = 0; x < npcd; x++) {
      PieceCand *c = &pcd[x];
      int cl = s->cls[c->v];
      int32_t plo = blk_start[c->block] + 1 + (int32_t)c->lo;
      int32_t phi = blk_start[c->block] + 1 + (int32_t)c->hi;
      const int *pool = cl ? FPR_ALLOC : gpool;
      int npool = cl ? NFPR_ALLOC : ngpool;
      int got = -1;
      /* no callee-saved register the function does not save already, and
         not the register the allocation fast path writes */
      for (int k = 0; k < npool && got < 0; k++) {
        int rr = pool[k];
        if (cl ? (fpr_is_callee_saved(rr) && !(s->used_fpr_cs & (1u << rr)))
               : ((gpr_is_callee_saved(rr) && !(s->used_gpr_cs & (1u << rr))) ||
                  rr == RA_ALLOC_RES_REG))
          continue;
        if (rl_free(&rl[cl ? 1 : 0][rr], plo, phi)) got = rr;
      }
      if (got < 0) continue;
      rl_add(&rl[cl ? 1 : 0][got], plo, phi, true);
      pcs[npcs++] = (struct Piece){ c->v, c->block, c->lo, c->hi, got, PC_CLUSTER };
    }
    if (npcs) {
      qsort(pcs, npcs, sizeof(struct Piece), piece_cmp);
      s->pc = pcs;
      s->npc = npcs;
      s->pc_bfirst = xcalloc(f->nblocks ? f->nblocks : 1, sizeof(uint32_t));
      s->pc_bn = xcalloc(f->nblocks ? f->nblocks : 1, sizeof(uint32_t));
      s->pc_head = xmalloc((n ? n : 1) * sizeof(int32_t));
      s->pc_next = xmalloc(npcs * sizeof(int32_t));
      for (uint32_t z = 0; z < n; z++) s->pc_head[z] = -1;
      for (uint32_t x = npcs; x-- > 0;) {
        struct Piece *pp = &pcs[x];
        s->pc_next[x] = s->pc_head[pp->v];
        s->pc_head[pp->v] = (int32_t)x;
        s->pc_bfirst[pp->block] = x;
        s->pc_bn[pp->block]++;
      }
      g_stats.pieces += npcs;
    } else {
      xfree(pcs);
    }
    xfree(pu); xfree(pcd); xfree(clob);
  for (int c = 0; c < 2; c++)
      for (int r = 0; r < 32; r++) xfree(rl[c][r].v);
  }

  /* ---- split planning, first allocation only ----
     A spilled web reloads once per block per iteration inside a loop; a
     register over just the loop segment costs one move at the boundary
     instead.  Find each natural loop, ask which spilled webs live in it,
     and check a register is genuinely free there -- the occupancy heaps
     hold every assignment the scan just made.  Two shapes go in the plan:
     a header param of a spilled web is cut from its entry edges (veto),
     and from the params outside the loop that what it carries flows into,
     and a slot-resident value defined outside the loop but used inside is
     threaded through a new header param.  The plan only ever asks; the
     retry's allocation decides.

     By default only the first shape runs, and only for loops that make no
     call.  A merged function -- five callees inlined into one loop -- is
     where it pays: a callee's accumulator is coalesced with the param that
     receives its result after the loop and carries it past every callee
     inlined behind it, and when one of those is a register-hungry nest
     the whole web is spilled, the hot loop's update included (rsbench
     itadapt, a running sum loaded and stored seven times an iteration).
     Out of line, the return is that cut.  In a loop that calls, the piece
     can only win a callee-saved register by shuffling the neighbours that
     held one, and that is what the opt-in plan measured losing. */
  if (s->split_collect && s->split_sc) {
    SplitCtl *sc = s->split_sc;
    const char *sp_only = warmc_getenv("WARMC_SPLIT_ONLY");
    const char *sp_upto = warmc_getenv("WARMC_SPLIT_UPTO");
    const char *sp_max_s = warmc_getenv("WARMC_SPLIT_MAX");
    const char *sp_rungs_s = warmc_getenv("WARMC_SPLIT_RUNGS");
    const char *sp_clob_s = warmc_getenv("WARMC_SPLIT_CLOB");
    int sp_max = sp_max_s ? atoi(sp_max_s) : -1, sp_done = 0;
    int sp_rungs = sp_rungs_s ? atoi(sp_rungs_s) : sc->full ? 3 : 1;
    int sp_clob = sp_clob_s ? atoi(sp_clob_s) : sc->full ? -1 : 0;
    bool sp_mand = warmc_getenv("WARMC_SPLIT_MAND") != NULL;
    const char *sp_room_s = warmc_getenv("WARMC_SPLIT_HEADROOM");
    int sp_room = sp_room_s ? atoi(sp_room_s) : 0;
    bool sp_dbg = warmc_getenv("WARMC_SPLIT_DEBUG") != NULL;
    bool sp_ok = true;
    if (sp_only) sp_ok = (uint32_t)atoi(sp_only) == f->func_index;
    if (sp_ok && sp_upto) sp_ok = f->func_index <= (uint32_t)atoi(sp_upto);
    if (sp_ok) {
      uint8_t *member = xcalloc(f->nblocks ? f->nblocks : 1, 1);
      uint32_t *mstack = xmalloc(f->nblocks * sizeof(uint32_t));
      uint32_t *mblk = xmalloc(f->nblocks * sizeof(uint32_t));
      Range *mrng = xmalloc(f->nblocks * sizeof(Range));
      uint32_t *ustamp = xcalloc(n ? n : 1, sizeof(uint32_t));
      double *uw = xcalloc(n ? n : 1, sizeof(double));
      uint8_t *umand = xcalloc(n ? n : 1, 1);
      Ref *uvals = xmalloc((n ? n : 1) * sizeof(Ref));
      uint8_t *planned = xcalloc(n ? n : 1, 1);
      SplitCand *cand = NULL;
      uint32_t cap_cand = 0;
      Ref *xm = NULL;
      uint32_t cap_xm = 0;
      uint32_t stamp_ctr = 0;
      uint64_t swork = 0;

      for (uint32_t oi = 0; oi < f->norder && swork <= SPLIT_BUDGET; oi++) {
        uint32_t hb = f->order[oi];
        Block *hd = &f->blocks[hb];
        if (!hd->reachable || hd->is_pad || hb == f->entry) continue;
        bool is_hdr = false;
        for (uint32_t p = 0; p < hd->npreds && !is_hdr; p++) {
          uint32_t pb = hd->preds[p];
          if (f->blocks[pb].reachable && f->blocks[pb].rpo >= hd->rpo)
            is_hdr = true;
        }
        if (!is_hdr) continue;

        uint32_t nm = 0, spx = 0, nlatch = 0;
        uint32_t latch[8];
        bool many_latches = false;
        member[hb] = 1; mblk[nm++] = hb;
        for (uint32_t p = 0; p < hd->npreds; p++) {
          uint32_t pb = hd->preds[p];
          if (!f->blocks[pb].reachable || f->blocks[pb].rpo < hd->rpo) continue;
          if (nlatch < 8) latch[nlatch++] = pb; else many_latches = true;
          if (!member[pb]) { member[pb] = 1; mblk[nm++] = pb; mstack[spx++] = pb; }
        }
        while (spx) {
          Block *c = &f->blocks[mstack[--spx]];
          for (uint32_t p = 0; p < c->npreds; p++) {
            uint32_t pb = c->preds[p];
            if (member[pb] || !f->blocks[pb].reachable) continue;
            member[pb] = 1; mblk[nm++] = pb; mstack[spx++] = pb;
          }
        }
        bool bad = false;
        for (uint32_t i2 = 0; i2 < nm && !bad; i2++)
          bad = f->blocks[mblk[i2]].rpo < hd->rpo || mblk[i2] == f->entry;
        swork += nm;
        if (bad) {
          for (uint32_t i2 = 0; i2 < nm; i2++) member[mblk[i2]] = 0;
          continue;
        }

        for (uint32_t i2 = 0; i2 < nm; i2++)
          mrng[i2] = (Range){ blk_start[mblk[i2]], blk_end[mblk[i2]] };
        sort_ranges_lo(mrng, nm);
        bool lclob = false;
        for (uint32_t i2 = 0; i2 < nm && !lclob; i2++) {
          uint32_t a = 0, b2 = ncalls;
          while (a < b2) {
            uint32_t m = (a + b2) / 2;
            if (call_pos[m] < mrng[i2].lo) a = m + 1; else b2 = m;
          }
          lclob = a < ncalls && call_pos[a] <= mrng[i2].hi;
        }
        if (sp_clob >= 0 && lclob != (sp_clob != 0)) {
          for (uint32_t i2 = 0; i2 < nm; i2++) member[mblk[i2]] = 0;
          continue;
        }

        /* WARMC_SPLIT_CSONLY=1 restricts call-containing loops to
           callee-saved registers the function already saves (no new
           prologue pair).  Racing said no as a default: the losers it was
           built for did not move and other rows reshuffled. */
        bool csonly = warmc_getenv("WARMC_SPLIT_CSONLY") != NULL;
        int nfree[2] = { 0, 0 };
        for (int k = 0; k < ngpool; k++) {
          int rr = gpool[k];
          if (lclob && !gpr_is_callee_saved(rr)) continue;
          if (lclob && csonly && !(s->used_gpr_cs & (1u << rr))) continue;
          if (split_reg_free(&gpr_occ[rr], gone, mrng, nm, &swork)) nfree[0]++;
        }
        for (int k = 0; k < NFPR_ALLOC; k++) {
          int rr = FPR_ALLOC[k];
          if (lclob && !fpr_is_callee_saved(rr)) continue;
          if (lclob && csonly && !(s->used_fpr_cs & (1u << rr))) continue;
          if (split_reg_free(&fpr_occ[rr], gone, mrng, nm, &swork)) nfree[1]++;
        }
        if (!nfree[0] && !nfree[1]) {
          for (uint32_t i2 = 0; i2 < nm; i2++) member[mblk[i2]] = 0;
          continue;
        }

        uint32_t ncand = 0;

        /* The exit merges, wanted once some header param's web is spilled:
           each param outside the loop that a value defined in it reaches
           along a forward edge and was coalesced with.  A backward one --
           an exit straight to an enclosing header -- is not something the
           veto cuts, and leaves its web joined. */
        uint32_t nxm = 0;
        bool want_xm = false;
        for (uint32_t pi = 0; pi < hd->nparams && (sp_rungs & 1); pi++) {
          Ref q = hd->params[pi];
          want_xm |= iv[web[q]].end >= 0 && IS_SLOT(s->loc[web[q]]);
        }
        for (uint32_t oi2 = 0; want_xm && oi2 < f->norder; oi2++) {
          Block *eb = &f->blocks[f->order[oi2]];
          for (uint32_t si = 0; si < eb->nsuccs; si++) {
            Edge *e2 = &eb->succs[si];
            Block *tb = &f->blocks[e2->target];
            if (member[e2->target] || e2->eh || eb->rpo >= tb->rpo) continue;
            swork += e2->nargs;
            for (uint32_t j2 = 0; j2 < e2->nargs && j2 < tb->nparams; j2++) {
              Ref av = e2->args[j2], P = tb->params[j2];
              if (!member[f->insts[av].block] || web[av] != web[P] ||
                  s->pinned[P])
                continue;
              if (nxm == cap_xm) {
                cap_xm = cap_xm ? cap_xm * 2 : 16;
                xm = xrealloc(xm, cap_xm * sizeof(Ref));
              }
              xm[nxm++] = P;
            }
          }
        }

        /* the loop-carried shape: a header param of a spilled web that its
           entry edges or its exit merges really joined */
        if (sp_rungs & 1)
          for (uint32_t pi = 0; pi < hd->nparams; pi++) {
            Ref q = hd->params[pi];
            if (s->pinned[q] || planned[q]) continue;
            if (num[q] < 0 || !value_needs_location(f, q)) continue;
            uint32_t rep = web[q];
            if (iv[rep].end < 0 || !IS_SLOT(s->loc[rep])) continue;
            if ((ValType)f->insts[q].type == VT_V128 && lclob) continue;
            bool joined = false;
            for (uint32_t k2 = 0; k2 < nxm && !joined; k2++)
              joined = web[xm[k2]] == rep;
            for (uint32_t p = 0; p < hd->npreds && !joined; p++) {
              uint32_t pb = hd->preds[p];
              if (!f->blocks[pb].reachable || member[pb]) continue;
              Block *pbb = &f->blocks[pb];
              for (uint32_t si = 0; si < pbb->nsuccs && !joined; si++)
                joined = pbb->succs[si].target == hb &&
                         pi < pbb->succs[si].nargs &&
                         pbb->succs[si].args[pi] != q &&
                         web[pbb->succs[si].args[pi]] == rep;
            }
            if (!joined) continue;
            if (ncand == cap_cand) {
              cap_cand = cap_cand ? cap_cand * 2 : 16;
              cand = xrealloc(cand, cap_cand * sizeof(SplitCand));
            }
            cand[ncand++] = (SplitCand){ q, weight[rep], 0, s->cls[q] };
          }

        /* the invariant shape: defined outside, slot-resident, used inside */
        uint32_t nuv = 0;
        uint32_t cur_stamp = ++stamp_ctr;
        if (sp_rungs & 2)
          for (uint32_t i2 = 0; i2 < nm; i2++) {
            Block *b2 = &f->blocks[mblk[i2]];
            double bw = 1.0;
            for (uint32_t d2 = 0; d2 < b2->loop_depth && d2 < 8; d2++)
              bw *= 10.0;
            swork += b2->ninsts;

            /* WARMC_SPLIT_MAND=1 admits only values with a use in a block
               dominating every latch (one the loop cannot skip).  Racing
               said no as a default: it halved btreemap's win -- b-tree
               descent uses live on conditional arms yet pay off -- and
               did not fix the row it was built for. */
            bool bmand = true;
            if (sp_mand) {
              bmand = !many_latches;
              for (uint32_t l2 = 0; l2 < nlatch && bmand; l2++)
                bmand = mblk[i2] == latch[l2] ||
                        ir_dominates_checked(f, mblk[i2], latch[l2]);
            }

#define SPLIT_USE(x_) do {                                                     \
            Ref x__ = (x_);                                                    \
            if (x__ >= n || num[x__] < 0) break;                               \
            if (member[f->insts[x__].block]) break;                            \
            if (ustamp[x__] != cur_stamp) {                                    \
              ustamp[x__] = cur_stamp; uw[x__] = 0; uvals[nuv++] = x__;        \
              umand[x__] = 0;                                                  \
            }                                                                  \
            uw[x__] += bw;                                                     \
            if (bmand) umand[x__] = 1;                                         \
          } while (0)

            for (uint32_t i3 = 0; i3 < b2->ninsts; i3++) {
              Inst *in2 = &f->insts[b2->insts[i3]];
              Ref *a2 = ir_args(f, b2->insts[i3]);
              for (uint32_t j2 = 0; j2 < in2->nargs; j2++) SPLIT_USE(a2[j2]);
            }
            if (b2->term == T_BRIF || b2->term == T_BRTABLE)
              SPLIT_USE(b2->term_arg);
            for (uint32_t i3 = 0; i3 < b2->nret; i3++)
              SPLIT_USE(b2->ret_args[i3]);
            for (uint32_t si = 0; si < b2->nsuccs; si++) {
              Edge *e2 = &b2->succs[si];
              if (!member[e2->target]) continue;
              for (uint32_t j2 = 0; j2 < e2->nargs; j2++) SPLIT_USE(e2->args[j2]);
            }
#undef SPLIT_USE
          }
        for (uint32_t i2 = 0; i2 < nuv; i2++) {
          Ref x = uvals[i2];
          if (!umand[x]) continue;
          if (s->pinned[x] || planned[x] || skip_location(s, x)) continue;
          if (!value_needs_location(f, x)) continue;

          /* An address producer's consumers dispatch on its OP: a load
             whose operand is a mem_ptr reads [reg], anything else reads
             [membase, zx(reg)].  Threading one through a param would
             launder the host address into the index path -- the value
             must keep its producer. */
          uint16_t xop = f->insts[x].op;
          if (xop == I_MEM_PTR || xop == I_MEM_ADDR || xop == I_MEM_BASE)
            continue;
          uint32_t rep = web[x];
          if (iv[rep].end < 0 || !IS_SLOT(s->loc[rep])) continue;
          if ((ValType)f->insts[x].type == VT_V128 && lclob) continue;
          if (!live_in_has(s, hb, x)) continue;
          if (ncand == cap_cand) {
            cap_cand = cap_cand ? cap_cand * 2 : 16;
            cand = xrealloc(cand, cap_cand * sizeof(SplitCand));
          }
          cand[ncand++] = (SplitCand){ x, uw[x], 1, s->cls[x] };
        }

        if (ncand > 1) qsort(cand, ncand, sizeof(SplitCand), split_cand_cmp);
        for (uint32_t i2 = 0; i2 < ncand; i2++) {
          SplitCand *c2 = &cand[i2];
          if (sp_max >= 0 && sp_done >= sp_max) break;

          /* WARMC_SPLIT_HEADROOM=N withholds the last N free registers
             per class.  Racing said 0: a floor of one lost more than the
             failed-grant shuffle it was meant to prevent. */
          if (nfree[c2->cls] <= sp_room || planned[c2->v]) continue;
          nfree[c2->cls]--;
          planned[c2->v] = 1;
          if (!c2->kind) {
            if (!sc->veto) { sc->veto = xcalloc(n, 1); sc->veto_n = n; }
            sc->veto[c2->v] = 1;
            if (sc->ncut == sc->cap_cut) {
              sc->cap_cut = sc->cap_cut ? sc->cap_cut * 2 : 8;
              sc->cutp = xrealloc(sc->cutp, sc->cap_cut * sizeof(Ref));
            }
            sc->cutp[sc->ncut++] = c2->v;
            g_stats.split_cuts++;
            for (uint32_t k2 = 0; k2 < nxm; k2++) {
              if (web[xm[k2]] != web[c2->v]) continue;
              sc->veto[xm[k2]] = 1;
              if (sc->nxcut == sc->cap_xcut) {
                sc->cap_xcut = sc->cap_xcut ? sc->cap_xcut * 2 : 8;
                sc->xcutp = xrealloc(sc->xcutp, sc->cap_xcut * sizeof(Ref));
                sc->xcut_hdr =
                    xrealloc(sc->xcut_hdr, sc->cap_xcut * sizeof(Ref));
              }
              sc->xcutp[sc->nxcut] = xm[k2];
              sc->xcut_hdr[sc->nxcut++] = c2->v;
              if (sp_dbg)
                fprintf(stderr, "split[%u] exit v%u for v%u\n", f->func_index,
                        xm[k2], c2->v);
            }
          } else {
            if (sc->nadd == sc->cap_add) {
              sc->cap_add = sc->cap_add ? sc->cap_add * 2 : 8;
              sc->addv = xrealloc(sc->addv, sc->cap_add * sizeof(Ref));
              sc->addhdr = xrealloc(sc->addhdr, sc->cap_add * sizeof(uint32_t));
            }
            sc->addv[sc->nadd] = c2->v;
            sc->addhdr[sc->nadd] = hb;
            sc->nadd++;
            g_stats.split_invs++;
          }
          sc->nflagged++;
          sp_done++;
          if (sp_dbg)
            fprintf(stderr, "split[%u] %s v%u hdr=b%u depth=%u clob=%d w=%.0f\n",
                    f->func_index, c2->kind ? "inv" : "cut", c2->v, hb,
                    hd->loop_depth, lclob, c2->w);
        }

        for (uint32_t i2 = 0; i2 < nm; i2++) member[mblk[i2]] = 0;
      }
      xfree(member); xfree(mstack); xfree(mblk); xfree(mrng);
      xfree(ustamp); xfree(uw); xfree(umand); xfree(uvals); xfree(planned);
      xfree(cand); xfree(xm);
    }
  }

  xfree(weight);
  xfree(wlat);
  xfree(wuse);
  xfree(wdef);
  xfree(cumf);
  xfree(cumc[0]); xfree(cumc[1]);


  if (warmc_getenv("WARMC_VERIFY_RA")) {

    for (uint32_t oi = 0; oi < f->norder; oi++) {
      uint32_t bi = f->order[oi];
      Block *b = &f->blocks[bi];
      if (!b->reachable) continue;
      for (int side = 0; side < 2; side++) {
        const uint32_t *off = side ? lout_off : lin_off;
        const Ref *set = side ? lout : lin;
        int32_t want = side ? blk_end[bi] : blk_start[bi];
        for (uint32_t k = off[bi]; k < off[bi + 1]; k++) {
          uint32_t v = set[k];
          if (num[v] < 0) continue;
          uint32_t rep = web[v];
          if (iv[rep].end < 0 || !iv[rep].rcount) continue;
          bool covered = false;
          for (uint32_t y = 0; y < iv[rep].rcount && !covered; y++)
            covered = want >= R[iv[rep].rfirst + y].lo &&
                      want <= R[iv[rep].rfirst + y].hi;
          if (!covered)
            warmc_fatal("regalloc: v%u (web v%u) is live %s block %u at %d, "
                        "which none of its %u ranges covers, in %s",
                        v, rep, side ? "out of" : "into", bi, want,
                        iv[rep].rcount, f->m->funcs[f->func_index].sym);
        }
      }
      for (uint32_t p = 0; p < b->nparams; p++) {
        uint32_t rep = web[b->params[p]];
        if (iv[rep].end < 0 || !iv[rep].rcount) continue;
        bool covered = false;
        for (uint32_t y = 0; y < iv[rep].rcount && !covered; y++)
          covered = blk_start[bi] >= R[iv[rep].rfirst + y].lo &&
                    blk_start[bi] <= R[iv[rep].rfirst + y].hi;
        if (!covered)
          warmc_fatal("regalloc: block param v%u (web v%u) is not covered at "
                      "the head of block %u (%d), in %s", b->params[p], rep, bi,
                      blk_start[bi], f->m->funcs[f->func_index].sym);
      }
      for (uint32_t sidx = 0; sidx < b->nsuccs; sidx++)
        for (uint32_t j = 0; j < b->succs[sidx].nargs; j++) {
          uint32_t a = b->succs[sidx].args[j];
          if (num[a] < 0) continue;
          uint32_t rep = web[a];
          if (iv[rep].end < 0 || !iv[rep].rcount) continue;
          bool covered = false;
          for (uint32_t y = 0; y < iv[rep].rcount && !covered; y++)
            covered = blk_end[bi] >= R[iv[rep].rfirst + y].lo &&
                      blk_end[bi] <= R[iv[rep].rfirst + y].hi;
          if (!covered)
            warmc_fatal("regalloc: edge argument v%u (web v%u) is not covered "
                        "at the end of block %u (%d), in %s", a, rep, bi,
                        blk_end[bi], f->m->funcs[f->func_index].sym);
        }
      for (uint32_t i = 0; i < b->ninsts; i++) {
        Ref r = b->insts[i];
        uint32_t ne = eff_uses(s, f, r, eu, eucap);
        for (uint32_t j = 0; j < ne; j++) {
          uint32_t rep = web[eu[j]];
          if (iv[rep].end < 0 || !iv[rep].rcount) continue;
          bool covered = false;
          for (uint32_t y = 0; y < iv[rep].rcount && !covered; y++)
            covered = num[r] >= R[iv[rep].rfirst + y].lo &&
                      num[r] <= R[iv[rep].rfirst + y].hi;
          if (!covered)
            warmc_fatal("regalloc: v%u (web v%u) is used at %d, which none of "
                        "its %u live ranges covers, in %s",
                        eu[j], rep, num[r], iv[rep].rcount,
                        f->m->funcs[f->func_index].sym);
        }
      }
    }

    for (uint32_t x = 0; x < ns; x++) {
      Interval *A = &sorted[x];
      int la = s->loc[A->value];
      if (la == LOC_NONE || IS_SLOT(la)) continue;
      const int *pool = A->cls ? FPR_ALLOC : gpool;
      int npool = A->cls ? NFPR_ALLOC : ngpool;
      bool in_pool = false;
      for (int k = 0; k < npool; k++) if (pool[k] == la) { in_pool = true; break; }
      if (!in_pool)
        warmc_fatal("regalloc: v%u got r%d (class %u), which is not in the "
                    "allocatable pool, in %s", A->value, la, A->cls,
                    f->m->funcs[f->func_index].sym);
    }

    for (uint32_t x = 0; x < ns; x++) {
      Interval *A = &sorted[x];
      int la = s->loc[A->value];
      if (!A->crosses_call || la == LOC_NONE || IS_SLOT(la)) continue;
      if (A->caller_saved_ok) {
        if (!s->csa_on || !s->csa_on[A->value] || !s->csa_slot ||
            s->csa_slot[A->value] < 0 || inelig[A->value])
          warmc_fatal("regalloc: v%u is caller-saved-around-call without the "
                      "state that makes that legal, in %s", A->value,
                      f->m->funcs[f->func_index].sym);
        continue;
      }
      bool cs = A->cls ? fpr_is_callee_saved(la) : gpr_is_callee_saved(la);
      if ((ValType)f->insts[A->value].type == VT_V128) cs = false;
      if (!cs)
        warmc_fatal("regalloc: v%u is live across a call in caller-saved r%d "
                    "(class %u) in %s", A->value, la, A->cls,
                    f->m->funcs[f->func_index].sym);
    }
    for (uint32_t x = 0; x < ns; x++) {
      for (uint32_t y = x + 1; y < ns; y++) {
        Interval *A = &sorted[x], *Bv = &sorted[y];
        if (A->cls != Bv->cls) continue;
        int la = s->loc[A->value], lb = s->loc[Bv->value];
        if (la == LOC_NONE || lb == LOC_NONE) continue;
        if (IS_SLOT(la) || IS_SLOT(lb) || la != lb) continue;

        if (tied_operand_of(f, A->value) == Bv->value && Bv->end == A->start)
          continue;
        if (tied_operand_of(f, Bv->value) == A->value && A->end == Bv->start)
          continue;
        if (iv_overlaps(A, Bv, R))
          warmc_fatal("regalloc: v%u [%d,%d] and v%u [%d,%d] both got r%d in %s",
                      A->value, A->start, A->end, Bv->value, Bv->start, Bv->end,
                      la, f->m->funcs[f->func_index].sym);
      }
    }
  }

  for (uint32_t i = 0; i < n; i++)
    if (iv[i].end >= 0 && value_needs_location(f, i) && web[i] != i &&
        !skip_location(s, i) && !s->pinned[i])
      s->loc[i] = s->loc[web[i]];

  /* A granted split param the retry could not keep in a register would
     cost a hot-edge move per loop entry for no saving at all -- and worse,
     it COMPETED during this scan, shuffling every neighbouring assignment
     (measured: +7% executed instructions on rsbench/chunks at flat static
     counts).  So a failed grant is normally only reported: the driver
     rolls the IR back, drops it from the plan and re-runs, and the code
     the module keeps never contained it.  Under accept (the bounded-round
     backstop and the far-branch rerun) failures are absorbed in place
     instead: the param IS its source value, and a SINGLETON spilled SSA
     value's slot is written exactly once at its def, so aliasing the
     param onto that slot recreates the unsplit code.  A webbed source is
     excluded -- a web slot is a phi slot that later members rewrite, and
     an in-loop read through the alias would see a successor value
     (workload -O1 nbody trapped on exactly that). */
  if (s->split_sc && !s->split_collect &&
      (s->split_sc->addp || s->split_sc->ncut)) {
    SplitCtl *sc = s->split_sc;
    if (!sc->accept) {
      sc->nfailed_new = 0;
      if (sc->nadd && !sc->inv_fail) sc->inv_fail = xcalloc(sc->cap_add, 1);
      if (sc->ncut && !sc->cut_fail) sc->cut_fail = xcalloc(sc->cap_cut, 1);
      bool sdbg = warmc_getenv("WARMC_SPLIT_DEBUG") != NULL;
      for (uint32_t k = 0; k < sc->nadd; k++) {
        Ref p = sc->addp[k];
        sc->inv_fail[k] = p < n && IS_SLOT(s->loc[p]);
        sc->nfailed_new += sc->inv_fail[k];
        if (sdbg && sc->inv_fail[k]) {
          uint32_t rep = web[p];
          fprintf(stderr, "split[%u] RETRYFAIL inv p%u rep=v%u len=%d xcall=%d\n",
                  f->func_index, p, rep, iv[rep].end - iv[rep].start,
                  iv[rep].crosses_call);
        }
      }
      for (uint32_t c = 0; c < sc->ncut; c++) {
        Ref p = sc->cutp[c];
        sc->cut_fail[c] = p < n && IS_SLOT(s->loc[p]);
        sc->nfailed_new += sc->cut_fail[c];
        if (sdbg && sc->cut_fail[c]) {
          uint32_t rep = web[p];
          fprintf(stderr, "split[%u] RETRYFAIL cut p%u rep=v%u len=%d xcall=%d\n",
                  f->func_index, p, rep, iv[rep].end - iv[rep].start,
                  iv[rep].crosses_call);
        }
      }
    } else if (sc->addp) {
      uint8_t *multi = xcalloc(n ? n : 1, 1);
      for (uint32_t i = 0; i < n; i++)
        if (iv[i].end >= 0 && web[i] != i) multi[web[i]] = 1;
      for (uint32_t k = 0; k < sc->nadd; k++) {
        Ref p = sc->addp[k], v = sc->addv[k];
        if (p >= n || v >= n) continue;
        if (IS_SLOT(s->loc[p])) {
          bool can = IS_SLOT(s->loc[v]) && web[v] == v && !multi[v];
          if (can) s->loc[p] = s->loc[v];
          if (warmc_getenv("WARMC_SPLIT_DEBUG"))
            fprintf(stderr, "split[%u] FAILED v%u -> p%u (p slot, v %s)\n",
                    f->func_index, v, p,
                    can                      ? "slot (aliased)"
                    : IS_SLOT(s->loc[v])     ? "webbed slot"
                    : s->loc[v] == LOC_NONE  ? "remat"
                                             : "reg");
        }
      }
      xfree(multi);
    }
  }

  if (s->csa_on) {
    bool any = false;
    for (uint32_t i = 0; i < n && !any; i++) any = s->csa_on[i] != 0;
    if (any) {
      s->csa_first = xcalloc(n ? n : 1, sizeof(uint32_t));
      s->csa_n = xcalloc(n ? n : 1, sizeof(uint32_t));
      CsaListCtx ctx = { web, xcalloc(n ? n : 1, sizeof(uint32_t)), 0 };
      for_each_clobber_site(s, f, csa_list_sites, &ctx);
      xfree(ctx.stamp);
    }
  }

  s->nslots = share_slots(s, iv, n, slot, blk_end);

  xfree(web); xfree(mem_head); xfree(mem_tail); xfree(mem_next);
  for (int i = 0; i < 32; i++) { xfree(gpr_occ[i].v); xfree(fpr_occ[i].v); }
  xfree(gone); xfree(ostack.v); xfree(R);
  (void)maxpos;
  xfree(eu);
  xfree(num); xfree(iv); xfree(sorted); xfree(blk_start); xfree(blk_end);
  xfree(call_pos); xfree(call_depth); xfree(call_cold); xfree(inelig);
  xfree(call_share); xfree(bshare);
  xfree(hint); xfree(web_hint);
}

/* The references live across one safepoint, its own result excluded. */
static void list_safepoint(Isel *s, IrFunc *f, Ref site, const Ref *live,
                           uint32_t nlive, void *ctx) {
  (void)ctx;
  uint32_t first = s->nsp_vals;
  for (uint32_t k = 0; k < nlive; k++) {
    Ref v = live[k];
    if (v == site || !vt_is_ref((ValType)f->insts[v].type)) continue;
    if (s->nsp_vals == s->cap_sp_vals) {
      s->cap_sp_vals = s->cap_sp_vals ? s->cap_sp_vals * 2 : 16;
      s->sp_vals = xrealloc(s->sp_vals, s->cap_sp_vals * sizeof(Ref));
    }
    s->sp_vals[s->nsp_vals++] = v;
  }
  s->sp_first[site] = first;
  s->sp_n[site] = s->nsp_vals - first;
}

static void compute_safepoints(Isel *s) {
  IrFunc *f = s->f;
  uint32_t n = f->ninsts;
  s->sp_first = xcalloc(n ? n : 1, sizeof(uint32_t));
  s->sp_n = xcalloc(n ? n : 1, sizeof(uint32_t));

  for (uint16_t op = 0; op < I_OP_COUNT; op++)
    if (op_is_safepoint(op) != ir_op_is_gc_safepoint(op))
      warmc_fatal("internal: the two safepoint lists disagree about %s",
                  ir_op_name(op));

  bool any_ref = false, any_site = false;
  for (uint32_t i = 0; i < n; i++) {
    if (op_is_safepoint(f->insts[i].op)) {
      any_site = true;

      if (!op_clobbers_caller_saved(f->insts[i].op))
        warmc_fatal("internal: %s is a safepoint but does not clobber the "
                    "caller-saved registers", ir_op_name(f->insts[i].op));
    }
    if (vt_is_ref((ValType)f->insts[i].type)) any_ref = true;
  }
  if (f->ghost_safepoint) any_site = true;
  if (f->ghost_ref) any_ref = true;
  if (!any_site || !any_ref) return;

  for_each_live_site(s, f, op_is_safepoint, list_safepoint, NULL);
}
