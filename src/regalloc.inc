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
      return true;
    default:
      return false;
  }
}

static bool op_is_safepoint(uint16_t op) {
  switch (op) {
    case I_CALL: case I_CALL_INDIRECT: case I_CALL_REF:
    case I_STRUCT_NEW: case I_ARRAY_NEW: case I_ARRAY_NEW_SEG:
      return true;
    default:
      return false;
  }
}

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

static int op_tied_arg(uint16_t op) {
  switch (op) {
    case I_VSEL: return 0;
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

static bool occ_hits(const Interval *cur, const Range *R, int32_t lo, int32_t hi) {
  if (!cur->rcount) return cur->start <= hi && lo <= cur->end;
  for (uint32_t i = 0; i < cur->rcount; i++) {
    const Range *x = &R[cur->rfirst + i];
    if (x->lo > hi) break;
    if (x->lo <= hi && lo <= x->hi) return true;
  }
  return false;
}

/* 0 = free, 1 = exactly one interval conflicts (in *victim), 2 = more.
   Ranges whose hi is behind cur, and ranges of an interval that has since been
   spilled, are dead: they are skipped here and dropped by the next rebuild. */
static int occ_conflicts(OccHeap *h, const Interval *cur, const Range *R,
                         const uint8_t *gone, uint32_t *stack, int32_t *victim) {
  *victim = -1;
  if (!h->n) return 0;
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
                                const uint8_t *call_cold, uint32_t ncalls,
                                double cold_div, uint64_t *work) {
  double c = 0;
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
      double w = 1.0;
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
static void csa_list_sites(Isel *s, IrFunc *f, Ref site, const uint64_t *live,
                           size_t W, void *vctx) {
  CsaListCtx *ctx = vctx;
  uint16_t op = f->insts[site].op;
  if (op == I_THROW || op == I_THROW_REF) return;
  if (op == I_TABLE_SIZE && f->insts[site].imm == 0) return;
  uint32_t stamp = ++ctx->stamp_ctr;
  uint32_t first = s->ncsa_vals;
  for (size_t w = 0; w < W; w++) {
    uint64_t bits = live[w];
    while (bits) {
      uint32_t v = (uint32_t)(w * 64 + (unsigned)__builtin_ctzll(bits));
      bits &= bits - 1;
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
  }
  s->csa_first[site] = first;
  s->csa_n[site] = s->ncsa_vals - first;
}

static int interval_cmp(const void *a, const void *b) {
  const Interval *x = a, *y = b;
  if (x->start != y->start) return x->start < y->start ? -1 : 1;
  return x->value < y->value ? -1 : 1;
}

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
  return maxargs * 8 + 32;
}

#define LV_WORDS(n) (((size_t)(n) + 63) / 64)
static inline void lv_set(uint64_t *s, uint32_t v) { s[v >> 6] |= 1ull << (v & 63); }
static inline void lv_clr(uint64_t *s, uint32_t v) { s[v >> 6] &= ~(1ull << (v & 63)); }

static void compute_liveness_now(Isel *s, uint64_t **livein_out,
                                 uint64_t **liveout_out) {
  IrFunc *f = s->f;
  uint32_t n = f->ninsts;
  size_t W = LV_WORDS(n);
  uint32_t eucap = eff_use_cap(f);
  Ref *eu = xmalloc(eucap * sizeof(Ref));
  uint64_t *livein = xcalloc((size_t)f->nblocks * W + 1, sizeof(uint64_t));
  uint64_t *liveout = xcalloc((size_t)f->nblocks * W + 1, sizeof(uint64_t));

  uint64_t *out = xmalloc((W ? W : 1) * sizeof(uint64_t));
  uint64_t *edge = xmalloc((W ? W : 1) * sizeof(uint64_t));
  bool changed = true;
  while (changed) {
    changed = false;
    for (int32_t oi = (int32_t)f->norder - 1; oi >= 0; oi--) {
      uint32_t bi = f->order[oi];
      Block *b = &f->blocks[bi];
      memset(out, 0, W * sizeof(uint64_t));
      /* Each edge on its own: a target's params are defined on its edges,
         not live across them, but the same values may be live into another
         successor -- a loop header that is its own br_table default keeps
         its params live into every case it dispatches to. */
      for (uint32_t sidx = 0; sidx < b->nsuccs; sidx++) {
        uint32_t t = b->succs[sidx].target;
        Block *tb = &f->blocks[t];
        memcpy(edge, &livein[(size_t)t * W], W * sizeof(uint64_t));
        for (uint32_t p = 0; p < tb->nparams; p++) lv_clr(edge, tb->params[p]);
        for (uint32_t j = 0; j < b->succs[sidx].nargs; j++)
          lv_set(edge, b->succs[sidx].args[j]);
        for (size_t w = 0; w < W; w++) out[w] |= edge[w];
      }
      if (b->term == T_BRIF && s->fused && s->fused[b->term_arg]) {
        uint32_t ne = eff_uses(s, f, b->term_arg, eu, eucap);
        for (uint32_t j = 0; j < ne; j++) lv_set(out, eff_arg(s, f, eu[j]));
      } else if (b->term == T_BRIF || b->term == T_BRTABLE) {
        lv_set(out, b->term_arg);
      }
      for (uint32_t i = 0; i < b->nret; i++) lv_set(out, b->ret_args[i]);

      uint64_t *lo = &liveout[(size_t)bi * W];
      for (size_t w = 0; w < W; w++) {
        uint64_t add = out[w] & ~lo[w];
        if (add) { lo[w] |= add; changed = true; }
      }

      for (int32_t i = (int32_t)b->ninsts - 1; i >= 0; i--) {
        Ref r2 = b->insts[i];
        lv_clr(out, r2);
        uint32_t ne = eff_uses(s, f, r2, eu, eucap);
        for (uint32_t j = 0; j < ne; j++) lv_set(out, eu[j]);
      }
      for (uint32_t p = 0; p < b->nparams; p++) lv_clr(out, b->params[p]);

      uint64_t *li = &livein[(size_t)bi * W];
      for (size_t w = 0; w < W; w++) {
        uint64_t add = out[w] & ~li[w];
        if (add) { li[w] |= add; changed = true; }
      }
    }
  }
  xfree(out);
  xfree(edge);
  xfree(eu);
  *livein_out = livein;
  *liveout_out = liveout;
}

static void compute_liveness(Isel *s, uint64_t **livein_out,
                             uint64_t **liveout_out) {
  if (!s->lv_in) compute_liveness_now(s, &s->lv_in, &s->lv_out);
  *livein_out = s->lv_in;
  *liveout_out = s->lv_out;
}

/* Backward walk over the emission order invoking fn at every instruction that
   clobbers the caller-saved registers, with the values live across it (the
   site's own def is still in the set; callers exclude it). */
static void for_each_clobber_site(Isel *s, IrFunc *f, const uint64_t *liveout,
                                  size_t W,
                                  void (*fn)(Isel *, IrFunc *, Ref,
                                             const uint64_t *, size_t, void *),
                                  void *ctx) {
  uint32_t eucap = eff_use_cap(f);
  Ref *eu = xmalloc(eucap * sizeof(Ref));
  uint64_t *live = xmalloc((W ? W : 1) * sizeof(uint64_t));
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    memcpy(live, &liveout[(size_t)bi * W], W * sizeof(uint64_t));
    for (int32_t i = (int32_t)b->ninsts - 1; i >= 0; i--) {
      Ref r2 = b->insts[i];
      if (op_clobbers_caller_saved(f->insts[r2].op))
        fn(s, f, r2, live, W, ctx);
      lv_clr(live, r2);
      uint32_t ne = eff_uses(s, f, r2, eu, eucap);
      for (uint32_t j = 0; j < ne; j++) lv_set(live, eu[j]);
    }
  }
  xfree(live);
  xfree(eu);
}

static void mark_crosses_call(Isel *s, IrFunc *f, Ref site,
                              const uint64_t *live, size_t W, void *ctx) {
  Interval *iv = ctx;
  (void)s; (void)f;
  for (size_t w = 0; w < W; w++) {
    uint64_t bits = live[w];
    while (bits) {
      uint32_t v = (uint32_t)(w * 64 + (unsigned)__builtin_ctzll(bits));
      bits &= bits - 1;
      if (v != site) iv[v].crosses_call = 1;
    }
  }
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

static int split_range_cmp(const void *a, const void *b) {
  const Range *x = a, *y = b;
  return x->lo < y->lo ? -1 : x->lo > y->lo ? 1 : 0;
}

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

  uint64_t *livein, *liveout;
  compute_liveness(s, &livein, &liveout);
  size_t W = LV_WORDS(n);
  for (uint32_t bi = 0; bi < f->nblocks; bi++) {
    if (!f->blocks[bi].reachable) continue;

    const uint64_t *li = &livein[(size_t)bi * W], *lo = &liveout[(size_t)bi * W];
    for (size_t w = 0; w < W; w++) {
      uint64_t bits = li[w];
      while (bits) {
        extend(&iv[w * 64 + (unsigned)__builtin_ctzll(bits)], blk_start[bi]);
        bits &= bits - 1;
      }
      bits = lo[w];
      while (bits) {
        extend(&iv[w * 64 + (unsigned)__builtin_ctzll(bits)], blk_end[bi]);
        bits &= bits - 1;
      }
    }
  }

  for_each_clobber_site(s, f, liveout, W, mark_crosses_call, iv);

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

  const char *co_only = getenv("WARMC_COALESCE_ONLY");
  const char *co_upto = getenv("WARMC_COALESCE_UPTO");
  const char *co_max_s = getenv("WARMC_COALESCE_MAX");
  int co_max = co_max_s ? atoi(co_max_s) : -1, co_done = 0;
  bool co_dbg = getenv("WARMC_COALESCE_DEBUG") != NULL;
  bool co_ok = !getenv("WARMC_NO_COALESCE");
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

          bool conflict = false;
          for (uint32_t u = mem_head[ra]; u != UINT32_MAX && !conflict;
               u = mem_next[u])
            for (uint32_t w = mem_head[rp]; w != UINT32_MAX; w = mem_next[w]) {

              if (iv[u].start == iv[w].start) { conflict = true; break; }

              if (iv[u].start < iv[w].end && iv[w].start < iv[u].end) { conflict = true; break; }
            }
          if (conflict) continue;
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
              mem_next[mem_tail[rp]] = mem_head[ra];
              mem_tail[rp] = mem_tail[ra];
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
  if (!getenv("WARMC_NO_HOLES")) {
    int32_t *fpos = xmalloc(n * sizeof(int32_t));
    int32_t *lpos = xmalloc(n * sizeof(int32_t));
    uint32_t *stamp = xcalloc(n ? n : 1, sizeof(uint32_t));
    uint32_t *touched = xmalloc(n * sizeof(uint32_t));
    uint32_t *rcnt = xcalloc(n ? n : 1, sizeof(uint32_t));
    uint32_t *roff = xmalloc((n + 1) * sizeof(uint32_t));
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

        const uint64_t *li = &livein[(size_t)bi * W], *lo = &liveout[(size_t)bi * W];
        for (size_t w = 0; w < W; w++) {
          uint64_t bits = li[w];
          while (bits) {
            uint32_t v = (uint32_t)(w * 64 + (unsigned)__builtin_ctzll(bits));
            bits &= bits - 1;
            if (num[v] >= 0) TOUCH(v, blk_start[bi]);
          }
          bits = lo[w];
          while (bits) {
            uint32_t v = (uint32_t)(w * 64 + (unsigned)__builtin_ctzll(bits));
            bits &= bits - 1;
            if (num[v] >= 0) TOUCH(v, blk_end[bi]);
          }
        }
#undef TOUCH

        for (uint32_t t = 0; t < nt; t++) {
          uint32_t v = touched[t], rep = web[v] < n ? web[v] : v;
          if (iv[rep].end < 0) continue;
          if (pass == 0) {
            rcnt[rep]++;
            if (++budget > RANGE_BUDGET) {
              over_budget = true;
              g_stats.range_budget_trips++;
              break;
            }
          }
          else {
            Range *slot_r = &R[roff[rep] + rcnt[rep]++];
            slot_r->lo = fpos[v]; slot_r->hi = lpos[v];
          }
        }
      }
    }

    if (R) {
      for (uint32_t i = 0; i < n; i++) {
        uint32_t c = rcnt[i];
        if (!c) continue;
        Range *rr = &R[roff[i]];
        for (uint32_t x = 1; x < c; x++) {
          Range key = rr[x];
          uint32_t y = x;
          while (y && (rr[y - 1].lo > key.lo)) { rr[y] = rr[y - 1]; y--; }
          rr[y] = key;
        }
        uint32_t m = 0;
        for (uint32_t x = 1; x < c; x++) {
          if (rr[x].lo <= rr[m].hi + 1) {
            if (rr[x].hi > rr[m].hi) rr[m].hi = rr[x].hi;
          } else rr[++m] = rr[x];
        }
        iv[i].rfirst = roff[i];
        iv[i].rcount = m + 1;
      }
    }
    xfree(fpos); xfree(lpos); xfree(stamp); xfree(touched); xfree(rcnt); xfree(roff);

  }

  Interval *sorted = xmalloc((n ? n : 1) * sizeof(Interval));
  uint32_t ns = 0;
  for (uint32_t i = 0; i < n; i++)
    if (iv[i].end >= 0 && value_needs_location(f, i) && web[i] == i &&
        !skip_location(s, i) && !s->pinned[i]) sorted[ns++] = iv[i];
  qsort(sorted, ns, sizeof(Interval), interval_cmp);

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
  uint32_t *lstamp = xcalloc(n ? n : 1, sizeof(uint32_t));
  uint32_t lseg = 0;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    uint32_t d = f->blocks[bi].loop_depth;
    double w = 1.0;
    for (uint32_t k = 0; k < d && k < 8; k++) w *= 10.0;
    lseg++;
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref r2 = b->insts[i];
      Inst *in = &f->insts[r2];
      Ref *a = ir_args(f, r2);
      if (op_clobbers_caller_saved(in->op)) lseg++;
      weight[web[r2]] += w;
      wlat[web[r2]] += w;
      uint32_t ne = eff_uses(s, f, r2, eu, eucap);
      (void)a;
      for (uint32_t j = 0; j < ne; j++) {
        uint32_t rep = web[eu[j]];
        weight[rep] += w;
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
    }
    for (uint32_t i = 0; i < b->nret; i++) {
      weight[web[b->ret_args[i]]] += w;
      wlat[web[b->ret_args[i]]] += w;
    }
    for (uint32_t sidx = 0; sidx < b->nsuccs; sidx++)
      for (uint32_t j = 0; j < b->succs[sidx].nargs; j++) {
        weight[web[b->succs[sidx].args[j]]] += w;
        wlat[web[b->succs[sidx].args[j]]] += w;
      }
  }
  xfree(lstamp);

  /* A rematerialised constant pays its move sequence at every use; a spilled
     one pays one cached reload per use segment.  A multi-instruction constant
     whose uses cluster many-per-segment is cheaper in a slot, so it loses its
     remat eligibility here -- weight counts uses, wlat counts segments, and
     both are final at this point. */
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
    if (ri->op == I_CONST && !vt_is_float((ValType)ri->type) &&
        (ValType)ri->type != VT_V128) {
      int c = ir_const_cost(ri->imm, vt_is_64((ValType)ri->type));
      if (c > 1 && ((double)c * weight[i] > s->remat2_seg * wlat[i] ||
                    weight[i] > s->remat2_wcap)) {
        s->no_remat[i] = 1;
        continue;
      }
    }
    weight[i] *= remat_discount(s, i);
  }

  int32_t gpr_end[32], fpr_end[32];
  int32_t gpr_owner[32], fpr_owner[32];
  OccHeap gpr_occ[32], fpr_occ[32];
  memset(gpr_occ, 0, sizeof gpr_occ);
  memset(fpr_occ, 0, sizeof fpr_occ);
  uint8_t *gone = xcalloc(ns ? ns : 1, 1);
  uint32_t ostack_cap = (nR > ns ? nR : ns) + 2;
  uint32_t *ostack = xmalloc(ostack_cap * sizeof(uint32_t));
  for (int i = 0; i < 32; i++) {
    gpr_end[i] = -1; fpr_end[i] = -1;
    gpr_owner[i] = -1; fpr_owner[i] = -1;
  }

  int gpool[NGPR_ALLOC + 2];
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

  uint32_t slot = 0;
  for (uint32_t i = 0; i < n; i++)
    if (s->pinned[i] && value_needs_location(f, i) && !skip_location(s, i))
      s->loc[i] = SLOT(take_slot(s, i, &slot));

  int *web_hint = xmalloc((n ? n : 1) * sizeof(int));
  for (uint32_t v = 0; v < n; v++) web_hint[v] = -1;
  for (uint32_t v = 0; v < n; v++)
    if (hint[v] >= 0 && web[v] < n && web_hint[web[v]] < 0)
      web_hint[web[v]] = hint[v];

  bool no_tie = getenv("WARMC_NO_TIE") != NULL;
  bool no_hint = getenv("WARMC_NO_HINT") != NULL;
  bool no_evict = getenv("WARMC_NO_EVICT") != NULL;

  const char *csa_only = getenv("WARMC_CSA_ONLY");
  const char *csa_upto = getenv("WARMC_CSA_UPTO");
  const char *csa_max_s = getenv("WARMC_CSA_MAX");
  int csa_max = csa_max_s ? atoi(csa_max_s) : -1, csa_done = 0;
  bool csa_dbg = getenv("WARMC_CSA_DEBUG") != NULL;
  const char *cold_div_s = getenv("WARMC_CSA_COLD_DIV");
  double csa_cold_div = cold_div_s ? atof(cold_div_s) : 1.0;
  const char *margin_s = getenv("WARMC_CSA_MARGIN");
  double csa_margin = margin_s ? atof(margin_s) : 1.0;
  bool csa_lat = !getenv("WARMC_CSA_NO_LAT");
  bool csa_ok = !getenv("WARMC_NO_CSA");
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
      const uint64_t *li = &livein[(size_t)bi * W];
      for (size_t w = 0; w < W; w++) {
        uint64_t bits = li[w];
        while (bits) {
          uint32_t v = (uint32_t)(w * 64 + (unsigned)__builtin_ctzll(bits));
          bits &= bits - 1;
          inelig[web[v]] = 1;
        }
      }
    }
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

    int chosen = -1;
    if (!no_tie) {
      Ref ta = tied_operand_of(f, cur->value);
      int tl = ta == REF_NONE ? LOC_NONE : s->loc[ta];
      if (ta != REF_NONE && tl != LOC_NONE && !IS_SLOT(tl) && tl >= 0 &&
          owner[tl] >= 0 && sorted[owner[tl]].value == ta &&
          ends[tl] == cur->start) {
        bool cs = !v128 &&
                  (cur->cls ? fpr_is_callee_saved(tl) : gpr_is_callee_saved(tl));
        bool in_pool = false;
        for (int k = 0; k < npool; k++) if (pool[k] == tl) { in_pool = true; break; }
        int nc = occ_conflicts(&heaps[tl], cur, R, gone, ostack, &vic);
        bool only_tied = nc == 0 || (nc == 1 && sorted[vic].value == ta);
        if (in_pool && only_tied && (!cur->crosses_call || cs)) chosen = tl;
      }
    }
    int want = no_hint ? -1 : hint[cur->value];
    if (chosen < 0 && want >= 0 &&
        occ_conflicts(&heaps[want], cur, R, gone, ostack, &vic) == 0) {
      bool cs = !v128 && (cur->cls ? fpr_is_callee_saved(want)
                                   : gpr_is_callee_saved(want));
      bool in_pool = false;
      for (int k = 0; k < npool; k++) if (pool[k] == want) { in_pool = true; break; }
      if (in_pool && (!cur->crosses_call || cs)) chosen = want;
    }
    for (int pass = 0; pass < 2 && chosen < 0; pass++) {
      for (int k = 0; k < npool; k++) {
        int rr = pool[k];
        bool cs = cur->cls ? fpr_is_callee_saved(rr) : gpr_is_callee_saved(rr);
        if (cur->crosses_call && (v128 || !cs)) continue;
        bool want_cs = cur->crosses_call ? true : (pass == 1);
        if (cs != want_cs) continue;
        if (occ_conflicts(&heaps[rr], cur, R, gone, ostack, &vic) != 0) continue;
        chosen = rr;
        break;
      }
    }

    if (chosen < 0) {

      int victim_reg = -1;
      int32_t victim_idx = -1;
      double victim_w = no_evict ? -1e300 : weight[cur->value];
      for (int k = 0; k < npool; k++) {
        int rr = pool[k];
        bool cs = cur->cls ? fpr_is_callee_saved(rr) : gpr_is_callee_saved(rr);
        if (cur->crosses_call && (v128 || !cs)) continue;
        int32_t cand;
        if (occ_conflicts(&heaps[rr], cur, R, gone, ostack, &cand) != 1) continue;
        double w = weight[sorted[cand].value];
        if (w < victim_w) { victim_w = w; victim_reg = rr; victim_idx = cand; }
      }
      /* A call-crossing value the callee-saved registers cannot hold may
         instead take a caller-saved register plus a private slot, paying two
         frame operations per crossed site.  Priced against what it would
         otherwise cost: the eviction victim's whole-life spill, or its own. */
      int csa_reg = -1;
      double csa_cost = 0;
      if (csa_ok && cur->crosses_call &&
          !inelig[cur->value] && csa_work <= CSA_BUDGET &&
          !(csa_max >= 0 && csa_done >= csa_max)) {
        double price = remat_ok(s, cur->value)
                           ? weight[cur->value] * remat_discount(s, cur->value)
                           : (csa_lat ? wlat[cur->value] : weight[cur->value]);
        if (victim_reg >= 0 && victim_w < price) price = victim_w;
        csa_cost = csa_crossing_cost(cur, R, num[cur->value], call_pos,
                                     call_depth, call_cold, ncalls,
                                     csa_cold_div, &csa_work);
        if (csa_cost * csa_margin < price || getenv("WARMC_CSA_FORCE"))
          for (int k = 0; k < npool; k++) {
            int rr = pool[k];

            /* Callee-saved was already tried (and is no protection for a
               V128); the allocation fast path writes RA_ALLOC_RES_REG with
               no call. */
            if (cur->cls
                    ? fpr_is_callee_saved(rr)
                    : (gpr_is_callee_saved(rr) || rr == RA_ALLOC_RES_REG))
              continue;
            if (occ_conflicts(&heaps[rr], cur, R, gone, ostack, &vic) != 0)
              continue;
            csa_reg = rr;
            break;
          }
      }
      if (csa_reg >= 0) {
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
        if (getenv("WARMC_SPILL_DEBUG"))
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
          if (getenv("WARMC_SPILL_DEBUG"))
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

  /* ---- split planning, first allocation only ----
     A spilled web reloads once per block per iteration inside a loop; a
     register over just the loop segment costs one move at the boundary
     instead.  Find each natural loop, ask which spilled webs live in it,
     and check a register is genuinely free there -- the occupancy heaps
     hold every assignment the scan just made.  Two shapes go in the plan:
     a header param of a spilled web is cut from its entry edges (veto),
     and a slot-resident value defined outside the loop but used inside is
     threaded through a new header param.  The plan only ever asks; the
     retry's allocation decides. */
  if (s->split_collect && s->split_sc) {
    SplitCtl *sc = s->split_sc;
    const char *sp_only = getenv("WARMC_SPLIT_ONLY");
    const char *sp_upto = getenv("WARMC_SPLIT_UPTO");
    const char *sp_max_s = getenv("WARMC_SPLIT_MAX");
    const char *sp_rungs_s = getenv("WARMC_SPLIT_RUNGS");
    const char *sp_clob_s = getenv("WARMC_SPLIT_CLOB");
    int sp_max = sp_max_s ? atoi(sp_max_s) : -1, sp_done = 0;
    int sp_rungs = sp_rungs_s ? atoi(sp_rungs_s) : 3;
    int sp_clob = sp_clob_s ? atoi(sp_clob_s) : -1;
    bool sp_mand = getenv("WARMC_SPLIT_MAND") != NULL;
    const char *sp_room_s = getenv("WARMC_SPLIT_HEADROOM");
    int sp_room = sp_room_s ? atoi(sp_room_s) : 0;
    bool sp_dbg = getenv("WARMC_SPLIT_DEBUG") != NULL;
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
        qsort(mrng, nm, sizeof(Range), split_range_cmp);
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
        bool csonly = getenv("WARMC_SPLIT_CSONLY") != NULL;
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

        /* the loop-carried shape: a header param of a spilled web whose
           entry edges really joined it */
        if (sp_rungs & 1)
          for (uint32_t pi = 0; pi < hd->nparams; pi++) {
            Ref q = hd->params[pi];
            if (s->pinned[q] || planned[q]) continue;
            if (num[q] < 0 || !value_needs_location(f, q)) continue;
            uint32_t rep = web[q];
            if (iv[rep].end < 0 || !IS_SLOT(s->loc[rep])) continue;
            if ((ValType)f->insts[q].type == VT_V128 && lclob) continue;
            bool joined = false;
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
          if (!(livein[(size_t)hb * W + (x >> 6)] & (1ull << (x & 63))))
            continue;
          if (ncand == cap_cand) {
            cap_cand = cap_cand ? cap_cand * 2 : 16;
            cand = xrealloc(cand, cap_cand * sizeof(SplitCand));
          }
          cand[ncand++] = (SplitCand){ x, uw[x], 1, s->cls[x] };
        }

        qsort(cand, ncand, sizeof(SplitCand), split_cand_cmp);
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
      xfree(cand);
    }
  }

  xfree(weight);
  xfree(wlat);


  if (getenv("WARMC_VERIFY_RA")) {

    for (uint32_t oi = 0; oi < f->norder; oi++) {
      uint32_t bi = f->order[oi];
      Block *b = &f->blocks[bi];
      if (!b->reachable) continue;
      const uint64_t *vli = &livein[(size_t)bi * W], *vlo = &liveout[(size_t)bi * W];
      for (int side = 0; side < 2; side++) {
        const uint64_t *bits_at = side ? vlo : vli;
        int32_t want = side ? blk_end[bi] : blk_start[bi];
        for (size_t w = 0; w < W; w++) {
          uint64_t bits = bits_at[w];
          while (bits) {
            uint32_t v = (uint32_t)(w * 64 + (unsigned)__builtin_ctzll(bits));
            bits &= bits - 1;
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
      bool sdbg = getenv("WARMC_SPLIT_DEBUG") != NULL;
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
          if (getenv("WARMC_SPLIT_DEBUG"))
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
      for_each_clobber_site(s, f, liveout, W, csa_list_sites, &ctx);
      xfree(ctx.stamp);
    }
  }

  s->nslots = slot;

  xfree(web); xfree(mem_head); xfree(mem_tail); xfree(mem_next);
  for (int i = 0; i < 32; i++) { xfree(gpr_occ[i].v); xfree(fpr_occ[i].v); }
  xfree(gone); xfree(ostack); xfree(R);
  (void)maxpos;
  xfree(eu);
  xfree(num); xfree(iv); xfree(sorted); xfree(blk_start); xfree(blk_end);
  xfree(call_pos); xfree(call_depth); xfree(call_cold); xfree(inelig);
  xfree(hint); xfree(web_hint);
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
  if (!any_site || !any_ref) return;

  uint64_t *livein, *liveout;
  compute_liveness(s, &livein, &liveout);
  size_t W = LV_WORDS(n);
  (void)livein;

  uint32_t eucap = eff_use_cap(f);
  Ref *eu = xmalloc(eucap * sizeof(Ref));

  uint64_t *live = xmalloc((W ? W : 1) * sizeof(uint64_t));
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    memcpy(live, &liveout[(size_t)bi * W], W * sizeof(uint64_t));
    for (int32_t i = (int32_t)b->ninsts - 1; i >= 0; i--) {
      Ref r = b->insts[i];
      if (op_is_safepoint(f->insts[r].op)) {
        uint32_t first = s->nsp_vals;
        for (size_t w = 0; w < W; w++) {
          uint64_t bits = live[w];
          while (bits) {
            uint32_t v = (uint32_t)(w * 64 + (unsigned)__builtin_ctzll(bits));
            bits &= bits - 1;
            if (v == r) continue;
            if (!vt_is_ref((ValType)f->insts[v].type)) continue;
            if (s->nsp_vals == s->cap_sp_vals) {
              s->cap_sp_vals = s->cap_sp_vals ? s->cap_sp_vals * 2 : 16;
              s->sp_vals = xrealloc(s->sp_vals, s->cap_sp_vals * sizeof(Ref));
            }
            s->sp_vals[s->nsp_vals++] = v;
          }
        }
        s->sp_first[r] = first;
        s->sp_n[r] = s->nsp_vals - first;
      }
      lv_clr(live, r);
      uint32_t ne = eff_uses(s, f, r, eu, eucap);
      for (uint32_t j = 0; j < ne; j++) lv_set(live, eu[j]);
    }
  }
  xfree(live);
  xfree(eu);
}
