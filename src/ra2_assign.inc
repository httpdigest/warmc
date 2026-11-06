/* ra2_assign.inc -- the registers of a function the spiller rewrote
   (ra_spill.inc, WARMC_RA2), in place of the scan.

   After the spiller no more of a class's values are live at once than it
   has registers, at any point, and the program is in SSA form: a value's
   definition dominates its uses.  So the values can take registers in one
   walk of the blocks in reverse postorder, every value one register for
   its life, freed where it dies: a value live into a block holds the
   register its definition took (which the walk met first), and the rest
   are free.  A block's params take free registers, the one their way in
   brings where it can; an instruction's operands that die there are let go
   after its result has a register, so that a result never shares one with
   its own operands.

   A value live across a call that clobbers the caller-saved registers
   takes a callee-saved one where one is free, else a caller-saved one that
   is saved and restored around each such call it crosses (csa): the
   spiller has left no more of them at a call than the callee-saved
   registers hold, but between two calls the ones crossing the first and
   the ones crossing the second can be more.  A copy in a slot (IrFunc
   .ra2_mem) has its slot, a constant the back end makes where it is read
   no location, and a value no register is left for -- a value the spiller
   leaves as it is, the scan's to place otherwise -- a slot. */

typedef struct {
  Ref *dying;            /* per instruction of a block, the values that die there */
  uint32_t *doff;        /* per instruction, from its block's first: offsets into dying */
} Ra2Die;

/* The walk of one block backwards: the values each of its instructions
   reads for the last time, which values live across which calls, and
   which values are read at all */
static void ra2a_scan_block(Isel *s, uint32_t b, uint8_t *live, Ref *lst, uint32_t *nl,
                            Ref **dying, uint32_t *ndying, uint32_t *capdying, uint32_t *doff,
                            uint8_t *xhot, uint8_t *xcold, uint8_t *xalloc, uint8_t *used, Ref *eu,
                            uint32_t eucap) {
  IrFunc *f = s->f;
  Block *bb = &f->blocks[b];
  *nl = 0;
#define RA2A_LIVE(x) do { Ref x_ = (x); if (!live[x_]) { live[x_] = 1; lst[(*nl)++] = x_; } used[x_] = 1; } while (0)
  for (uint32_t k = s->lv_out_off[b]; k < s->lv_out_off[b + 1]; k++) RA2A_LIVE(s->lv_out[k]);
  if (bb->term == T_BRIF && s->fused && s->fused[bb->term_arg]) {
    uint32_t ne = eff_uses(s, f, bb->term_arg, eu, eucap);
    for (uint32_t j = 0; j < ne; j++) RA2A_LIVE(eff_arg(s, f, eu[j]));
  } else if (bb->term == T_BRIF || bb->term == T_BRTABLE) {
    RA2A_LIVE(bb->term_arg);
  }
  for (uint32_t i = 0; i < bb->nret; i++) RA2A_LIVE(bb->ret_args[i]);
  for (uint32_t k = 0; k < bb->nsuccs; k++)
    for (uint32_t j = 0; j < bb->succs[k].nargs; j++) RA2A_LIVE(bb->succs[k].args[j]);
  /* dying lists are made backwards, then each instruction's put in order */
  uint32_t n = bb->ninsts;
  uint32_t *cnt = xcalloc(n + 1, sizeof(uint32_t));
  Ref *tmp = NULL;
  uint32_t ntmp = 0, captmp = 0;
  uint32_t *tpos = xmalloc((n + 1) * sizeof(uint32_t));
  for (uint32_t i = n; i-- > 0;) {
    Ref u = bb->insts[i];
    if (live[u]) {
      live[u] = 0;
    }
    bool clob = inst_clobbers_caller_saved(f, u);
    if (clob) {
      uint64_t ft;
      int64_t fl;
      bool cold = bb->cold || gc_fastnew_shape(f, u, &ft, &fl);
      uint16_t op = f->insts[u].op;
      bool alloc = op == I_STRUCT_NEW || op == I_ARRAY_NEW || op == I_ARRAY_NEW_SEG;
      for (uint32_t k = 0; k < *nl; k++) {
        Ref x = lst[k];
        if (!live[x] || x == u) continue;
        if (cold) xcold[x] = 1;
        else if (xhot[x] < 2) xhot[x] = bb->loop_depth ? 2 : 1;
        if (alloc) xalloc[x] = 1;
      }
    }
    tpos[i] = ntmp;
    uint32_t ne = eff_uses(s, f, u, eu, eucap);
    for (uint32_t j = 0; j < ne; j++) {
      Ref x = eu[j];
      used[x] = 1;
      if (live[x]) continue;
      live[x] = 1;
      lst[(*nl)++] = x;
      if (ntmp == captmp) { captmp = captmp ? captmp * 2 : 64; tmp = xrealloc(tmp, captmp * sizeof(Ref)); }
      tmp[ntmp++] = x;
      cnt[i]++;
    }
  }
  /* compact: instruction i's dying values are tmp[tpos[i] .. +cnt[i]] */
  for (uint32_t i = 0; i < n; i++) {
    doff[i] = *ndying;
    for (uint32_t k = 0; k < cnt[i]; k++) {
      if (*ndying == *capdying) {
        *capdying = *capdying ? *capdying * 2 : 256;
        *dying = xrealloc(*dying, *capdying * sizeof(Ref));
      }
      (*dying)[(*ndying)++] = tmp[tpos[i] + k];
    }
  }
  doff[n] = *ndying;
  xfree(cnt);
  xfree(tmp);
  xfree(tpos);
  for (uint32_t k = 0; k < *nl; k++) live[lst[k]] = 0;
#undef RA2A_LIVE
}

/* A free register of class c for value x, the hint first where it will do:
   callee-saved where x crosses a call that clobbers the caller-saved ones
   (none for a vector, whose high half no callee keeps), never the
   allocation's result register where x crosses an allocation; -1 where x
   must go to a slot: a reference crossing such a call that finds no
   callee-saved register, which may not be saved around it
   (isel_csa_refs). */
static int ra2a_pick(Isel *s, Ref x, int c, const Ref *own, const int *pool, int np, int hint,
                     const uint8_t *xhot, const uint8_t *xalloc, const uint32_t *claimed, uint8_t ghotv,
                     uint64_t avoid) {
  bool ghot = ghotv != 0, ghot_loop = ghotv == 2;
  IrFunc *f = s->f;
  bool v128 = (ValType)f->insts[x].type == VT_V128;
  bool want_cs = (xhot[x] || ghot) && !v128;
  /* the hint; then a register of the kind wanted that no value it meets
     was planned (ra2a_plan) and no group has taken for its own, then one
     no value it meets was planned, then one of that kind, then any */
  /* A value live across a call made in a loop takes the callee-saved
     registers from the other end of the pool, as the scan does (its
     cs_top): callees take theirs from x22 up, and one that saves and
     restores a loop's counter or sum puts it through memory on every way
     round (javabench/virtual 45% slower). */
  bool top = want_cs && c == 0 && (xhot[x] == 2 || ghot_loop) && f->insts[x].op != I_CONST &&
             !warmc_getenv("WARMC_RA2_NO_CSTOP");
  for (int pass = -3; pass < 2; pass++)
    for (int kk = 0; kk < np; kk++) {
      int k = top ? np - 1 - kk : kk;
      int r = pool[k];
      if (pass == -3 && r != hint) continue;
      if (r < 0 || r >= 64 || own[r] != REF_NONE) continue;
      if (c == 0 && xalloc[x] && r == RA_ALLOC_RES_REG) continue;
      bool cs = c ? fpr_is_callee_saved(r) : gpr_is_callee_saved(r);
      if (pass <= 0 && cs != want_cs) continue;
      if (pass <= -1 && pass >= -2 && (avoid >> r & 1)) continue;
      if (pass == -2 && claimed && claimed[r]) continue;
      if (pass == 1 && xhot[x] && !v128 && !cs && vt_is_ref((ValType)f->insts[x].type) && !isel_csa_refs())
        continue;
      return r;
    }
  return -1;
}

/* Coalescing.  The affinities -- a param and the argument an edge passes
   it, a copy and its operand, a result and the operand it is tied to --
   by how often their move would run, heaviest first; two groups become one
   where no value of the one is live where a value of the other is made,
   unless the two are the same value (a copy of it, or a copy of a copy):
   a group's values may then all take one register, and its moves are
   none.  A group whose values were live at once would take the register
   of its first wherever it is free and leave the others to whatever was
   left, crossing groups over and moving their values round on every way
   between them. */
typedef struct { Ref a, b; double w; } Ra2Aff;

static int ra2a_aff_cmp(const void *x, const void *y) {
  const Ra2Aff *p = x, *q = y;
  if (p->w != q->w) return p->w > q->w ? -1 : 1;
  if (p->a != q->a) return p->a < q->a ? -1 : 1;
  return p->b < q->b ? -1 : p->b > q->b;
}

typedef struct {
  Isel *s;
  int32_t *num;          /* a value's index in its block, -1 for a param */
  uint32_t *blk;
  Ref *val;              /* the value a copy is of, through copies */
  Ref *eu;
  uint32_t eucap;
  uint64_t budget, cap;
} Ra2Int;

/* x live just after position p of block bi (regalloc.inc live_after) */
static bool ra2a_live_after(Ra2Int *I, Ref x, uint32_t bi, int32_t p) {
  Isel *s = I->s;
  IrFunc *f = s->f;
  if (I->blk[x] == bi) {
    if (I->num[x] >= p) return I->num[x] == p;
  } else if (!live_in_has(s, bi, x)) {
    return false;
  }
  if (live_out_has(s, bi, x)) return true;
  Block *b = &f->blocks[bi];
  for (uint32_t i = (uint32_t)(p + 1); i < b->ninsts; i++) {
    Ref r = b->insts[i];
    I->budget++;
    uint32_t ne = eff_uses(s, f, r, I->eu, I->eucap);
    for (uint32_t j = 0; j < ne; j++)
      if (I->eu[j] == x) return true;
  }
  if (b->term == T_BRIF && s->fused && s->fused[b->term_arg]) {
    uint32_t ne = eff_uses(s, f, b->term_arg, I->eu, I->eucap);
    for (uint32_t j = 0; j < ne; j++)
      if (eff_arg(s, f, I->eu[j]) == x) return true;
  } else if ((b->term == T_BRIF || b->term == T_BRTABLE) && b->term_arg == x) {
    return true;
  }
  for (uint32_t i = 0; i < b->nret; i++) if (b->ret_args[i] == x) return true;
  for (uint32_t k = 0; k < b->nsuccs; k++)
    for (uint32_t j = 0; j < b->succs[k].nargs; j++)
      if (b->succs[k].args[j] == x) return true;
  return false;
}

static bool ra2a_interfere(Ra2Int *I, Ref a, Ref b) {
  if (I->val[a] == I->val[b]) return false;
  return ra2a_live_after(I, a, I->blk[b], I->num[b]) || ra2a_live_after(I, b, I->blk[a], I->num[a]);
}

static void ra2a_coalesce(Isel *s, const bool *wants, uint32_t *grp, Ref *eu, uint32_t eucap, double *gw,
                          Ref **valout) {
  IrFunc *f = s->f;
  uint32_t n = f->ninsts;
  Ra2Int I = { s, xmalloc((n ? n : 1) * sizeof(int32_t)), xmalloc((n ? n : 1) * sizeof(uint32_t)),
               xmalloc((n ? n : 1) * sizeof(Ref)), eu, eucap, 0, 16 * (uint64_t)n + 65536 };
  for (uint32_t v = 0; v < n; v++) { I.num[v] = INT32_MAX; I.blk[v] = UINT32_MAX; I.val[v] = v; }
  Ra2Aff *aff = NULL;
  uint32_t naff = 0, capaff = 0;
#define RA2A_AFF(x, y, w_) do { if (naff == capaff) { capaff = capaff ? capaff * 2 : 64; \
    aff = xrealloc(aff, capaff * sizeof(Ra2Aff)); } aff[naff++] = (Ra2Aff){ (x), (y), (w_) }; } while (0)
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *bb = &f->blocks[bi];
    for (uint32_t p = 0; p < bb->nparams; p++) { I.num[bb->params[p]] = -1; I.blk[bb->params[p]] = bi; }
    for (uint32_t i = 0; i < bb->ninsts; i++) { I.num[bb->insts[i]] = (int32_t)i; I.blk[bb->insts[i]] = bi; }
  }
  /* copies' values, in order: a copy's operand is made before it */
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *bb = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < bb->ninsts; i++) {
      Ref u = bb->insts[i];
      if (f->insts[u].op == I_COPY && f->insts[u].nargs == 1) I.val[u] = I.val[ir_args(f, u)[0]];
    }
  }
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *bb = &f->blocks[bi];
    double w = blk_freq(bb);
    for (uint32_t si = 0; si < bb->nsuccs; si++) {
      Edge *e = &bb->succs[si];
      if (e->eh) continue;
      Block *tb = &f->blocks[e->target];
      /* an edge's moves run as often as the less often run of its ends */
      double we = blk_freq(tb) < w ? blk_freq(tb) : w;
      for (uint32_t j = 0; j < e->nargs && j < tb->nparams; j++) {
        Ref x = e->args[j], y = tb->params[j];
        if (wants[x] && wants[y] && (bool)s->cls[x] == (bool)s->cls[y]) RA2A_AFF(x, y, we);
      }
    }
    for (uint32_t i = 0; i < bb->ninsts; i++) {
      Ref u = bb->insts[i];
      if (!wants[u]) continue;
      Ref t = f->insts[u].op == I_COPY ? ir_args(f, u)[0] : tied_operand_of(f, u);
      if (t != REF_NONE && wants[t] && (bool)s->cls[u] == (bool)s->cls[t]) RA2A_AFF(u, t, w);
    }
  }
#undef RA2A_AFF
  qsort(aff, naff, sizeof(Ra2Aff), ra2a_aff_cmp);
  /* each group's members, as lists through mnext from its root */
  uint32_t *mnext = xmalloc((n ? n : 1) * sizeof(uint32_t));
  uint32_t *mtail = xmalloc((n ? n : 1) * sizeof(uint32_t));
  uint32_t *msize = xmalloc((n ? n : 1) * sizeof(uint32_t));
  for (uint32_t v = 0; v < n; v++) { mnext[v] = UINT32_MAX; mtail[v] = v; msize[v] = 1; }
  for (uint32_t k = 0; k < naff; k++) {
    uint32_t ga = aff[k].a, gb = aff[k].b;
    while (grp[ga] != ga) ga = grp[ga];
    while (grp[gb] != gb) gb = grp[gb];
    if (ga == gb) continue;
    if (I.budget > I.cap || (uint64_t)msize[ga] * msize[gb] > 4096) continue;
    bool clash = false;
    Ref cx = REF_NONE, cy = REF_NONE;
    for (uint32_t x = ga; x != UINT32_MAX && !clash; x = mnext[x])
      for (uint32_t y = gb; y != UINT32_MAX && !clash; y = mnext[y])
        if ((clash = ra2a_interfere(&I, x, y))) { cx = x; cy = y; }
    if (clash) {
      const char *dc = warmc_getenv("WARMC_RA2_DEBUG_CO");
      if (dc && (uint32_t)atoi(dc) == f->func_index)
        fprintf(stderr, "co reject v%u-v%u w=%g: v%u(b%u) vs v%u(b%u)\n", aff[k].a, aff[k].b, aff[k].w,
                cx, I.blk[cx], cy, I.blk[cy]);
      continue;
    }
    /* gb's members join ga's */
    grp[gb] = ga;
    mnext[mtail[ga]] = gb;
    mtail[ga] = mtail[gb];
    msize[ga] += msize[gb];
    gw[ga] += gw[gb] + aff[k].w;
  }
  xfree(mnext); xfree(mtail); xfree(msize);
  xfree(aff);
  xfree(I.num); xfree(I.blk);
  *valout = I.val;
}

/* The plan: a register for each group of more than one value, the groups
   that move the most first, none that a group live where it is has taken
   -- by the values each group's are live where they are made, the walk
   backwards through every block -- so that the walk's first member of a
   group does not take, where it is made, the register a group it meets
   later in a loop has to have. */
typedef struct { uint32_t a, b; } Ra2GE;

static int ra2a_ge_cmp(const void *x, const void *y) {
  const Ra2GE *p = x, *q = y;
  if (p->a != q->a) return p->a < q->a ? -1 : 1;
  return p->b < q->b ? -1 : p->b > q->b;
}

static const double *ra2a_gw_sort;
static int ra2a_gw_cmp(const void *x, const void *y) {
  uint32_t a = *(const uint32_t *)x, b = *(const uint32_t *)y;
  if (ra2a_gw_sort[a] != ra2a_gw_sort[b]) return ra2a_gw_sort[a] > ra2a_gw_sort[b] ? -1 : 1;
  return a < b ? -1 : a > b;
}

static void ra2a_plan(Isel *s, const bool *wants, uint32_t *grp, const uint32_t *gsize, const double *gw,
                      const uint8_t *ghot, const uint8_t *xalloc, const Ref *val, const int *gpool, int ngall,
                      int *gpref, uint32_t *gclaim, uint32_t *fclaim, Ref *eu, uint32_t eucap,
                      uint32_t **aoff_out, uint32_t **adj_out) {
  IrFunc *f = s->f;
  uint32_t n = f->ninsts;
#define RA2P_G(x) ({ uint32_t r_ = (x); while (grp[r_] != r_) r_ = grp[r_]; r_; })
  uint8_t *multi = xcalloc(n ? n : 1, 1);
  uint8_t *galloc = xcalloc(n ? n : 1, 1);
  bool any = false;
  bool all = !warmc_getenv("WARMC_RA2_PLAN_GROUPS");
  for (uint32_t v = 0; v < n; v++)
    if (wants[v]) {
      uint32_t g = RA2P_G(v);
      if (gsize[g] > 1) any = true;
      if (gsize[g] > 1 || all) multi[v] = 1;
      if (xalloc[v]) galloc[g] = 1;
    }
  if (!any) { xfree(multi); xfree(galloc); return; }
  Ra2GE *ge = NULL;
  size_t nge = 0, capge = 0;
  size_t limit = 8 * (size_t)n + 65536;
  uint8_t *live = xcalloc(n ? n : 1, 1);
  Ref *lst = xmalloc((n ? n : 1) * sizeof(Ref));
  bool over = false;
#define RA2P_EDGE(x, y) do { if (multi[x] && multi[y] && val[x] != val[y] && (bool)s->cls[x] == (bool)s->cls[y]) { \
      uint32_t ga_ = RA2P_G(x), gb_ = RA2P_G(y); if (ga_ != gb_) { \
      if (nge == capge) { capge = capge ? capge * 2 : 1024; ge = xrealloc(ge, capge * sizeof(Ra2GE)); } \
      ge[nge++] = (Ra2GE){ ga_ < gb_ ? ga_ : gb_, ga_ < gb_ ? gb_ : ga_ }; if (nge > limit) over = true; } } } while (0)
  for (uint32_t oi = 0; oi < f->norder && !over; oi++) {
    uint32_t b = f->order[oi];
    Block *bb = &f->blocks[b];
    uint32_t nl = 0;
#define RA2P_LIVE(x) do { Ref x_ = (x); if (wants[x_] && !live[x_]) { live[x_] = 1; lst[nl++] = x_; } } while (0)
    for (uint32_t k = s->lv_out_off[b]; k < s->lv_out_off[b + 1]; k++) RA2P_LIVE(s->lv_out[k]);
    if (bb->term == T_BRIF && s->fused && s->fused[bb->term_arg]) {
      uint32_t ne = eff_uses(s, f, bb->term_arg, eu, eucap);
      for (uint32_t j = 0; j < ne; j++) RA2P_LIVE(eff_arg(s, f, eu[j]));
    } else if (bb->term == T_BRIF || bb->term == T_BRTABLE) {
      RA2P_LIVE(bb->term_arg);
    }
    for (uint32_t i = 0; i < bb->nret; i++) RA2P_LIVE(bb->ret_args[i]);
    for (uint32_t k = 0; k < bb->nsuccs; k++)
      for (uint32_t j = 0; j < bb->succs[k].nargs; j++) RA2P_LIVE(bb->succs[k].args[j]);
    for (uint32_t i = bb->ninsts; i-- > 0 && !over;) {
      Ref u = bb->insts[i];
      if (wants[u] && multi[u])
        for (uint32_t k = 0; k < nl; k++) {
          Ref x = lst[k];
          if (live[x] && x != u) RA2P_EDGE(u, x);
        }
      if (live[u]) live[u] = 0;
      uint32_t ne = eff_uses(s, f, u, eu, eucap);
      for (uint32_t j = 0; j < ne; j++) RA2P_LIVE(eu[j]);
      /* the list keeps what died for good: compact it now and then */
      if (nl > 2 * 64) {
        uint32_t m = 0;
        for (uint32_t k = 0; k < nl; k++) if (live[lst[k]]) lst[m++] = lst[k];
        nl = m;
      }
    }
    /* the params, made together on entry, and what is live in */
    for (uint32_t p = 0; p < bb->nparams; p++) RA2P_LIVE(bb->params[p]);
    for (uint32_t p = 0; p < bb->nparams && !over; p++) {
      Ref u = bb->params[p];
      if (!wants[u] || !multi[u]) continue;
      for (uint32_t k = 0; k < nl; k++) {
        Ref x = lst[k];
        if (live[x] && x != u) RA2P_EDGE(u, x);
      }
    }
    for (uint32_t k = 0; k < nl; k++) live[lst[k]] = 0;
#undef RA2P_LIVE
  }
#undef RA2P_EDGE
  xfree(live);
  xfree(lst);
  if (over) { xfree(ge); xfree(multi); xfree(galloc); return; }
  qsort(ge, nge, sizeof(Ra2GE), ra2a_ge_cmp);
  size_t m = 0;
  for (size_t k = 0; k < nge; k++)
    if (!m || ge[k].a != ge[m - 1].a || ge[k].b != ge[m - 1].b) ge[m++] = ge[k];
  nge = m;
  /* adjacency, both ways */
  uint32_t *aoff = xcalloc((size_t)n + 1, sizeof(uint32_t));
  for (size_t k = 0; k < nge; k++) { aoff[ge[k].a + 1]++; aoff[ge[k].b + 1]++; }
  for (uint32_t v = 0; v < n; v++) aoff[v + 1] += aoff[v];
  uint32_t *adj = xmalloc((aoff[n] ? aoff[n] : 1) * sizeof(uint32_t));
  uint32_t *acur = xmalloc(((size_t)n + 1) * sizeof(uint32_t));
  memcpy(acur, aoff, ((size_t)n + 1) * sizeof(uint32_t));
  for (size_t k = 0; k < nge; k++) { adj[acur[ge[k].a]++] = ge[k].b; adj[acur[ge[k].b]++] = ge[k].a; }
  xfree(acur);
  xfree(ge);
  /* by weight: the moves a group's register saves and its values' reads,
     each by how often it runs */
  uint32_t *order = xmalloc((n ? n : 1) * sizeof(uint32_t));
  double *pw = xcalloc(n ? n : 1, sizeof(double));
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *bb = &f->blocks[f->order[oi]];
    double w = blk_freq(bb);
    for (uint32_t i = 0; i < bb->ninsts; i++) {
      uint32_t ne = eff_uses(s, f, bb->insts[i], eu, eucap);
      for (uint32_t j = 0; j < ne; j++) if (wants[eu[j]]) pw[RA2P_G(eu[j])] += w;
    }
    for (uint32_t k = 0; k < bb->nsuccs; k++)
      for (uint32_t j = 0; j < bb->succs[k].nargs; j++)
        if (wants[bb->succs[k].args[j]]) pw[RA2P_G(bb->succs[k].args[j])] += w;
  }
  uint32_t ng = 0;
  for (uint32_t v = 0; v < n; v++)
    if (wants[v] && multi[v] && grp[v] == v) {
      order[ng++] = v;
      pw[v] += gw[v];
    }
  ra2a_gw_sort = pw;
  qsort(order, ng, sizeof(uint32_t), ra2a_gw_cmp);
  for (uint32_t k = 0; k < ng; k++) {
    uint32_t g = order[k];
    int c = s->cls[g] ? 1 : 0;
    uint64_t forb = 0;
    for (uint32_t t = aoff[g]; t < aoff[g + 1]; t++)
      if (gpref[adj[t]] >= 0) forb |= 1ull << gpref[adj[t]];
    const int *pool = c ? FPR_ALLOC : gpool;
    int np = c ? NFPR_ALLOC : ngall;
    bool v128 = (ValType)f->insts[g].type == VT_V128;
    bool want_cs = ghot[g] && !v128;
    bool ref = vt_is_ref((ValType)f->insts[g].type);
    int pick = -1;
    bool top = want_cs && ghot[g] == 2 && c == 0 && f->insts[g].op != I_CONST && !warmc_getenv("WARMC_RA2_NO_CSTOP");
    for (int pass = 0; pass < 2 && pick < 0; pass++)
      for (int qq = 0; qq < np && pick < 0; qq++) {
        int r = pool[top ? np - 1 - qq : qq];
        if (r < 0 || r >= 64 || (forb >> r & 1)) continue;
        if (!c && galloc[g] && r == RA_ALLOC_RES_REG) continue;
        bool cs = c ? fpr_is_callee_saved(r) : gpr_is_callee_saved(r);
        if (pass == 0 && cs != want_cs) continue;
        /* the walk would not put it there (ra2a_pick) */
        if (pass == 1 && want_cs) continue;
        (void)ref;
        pick = r;
      }
    if (pick < 0) continue;
    gpref[g] = pick;
    if (gsize[g] > 1) (c ? fclaim : gclaim)[pick]++;
  }
  xfree(order);
  xfree(pw);
  *aoff_out = aoff;
  *adj_out = adj;
  xfree(multi);
  xfree(galloc);
#undef RA2P_G
}

/* Recoloring.  The walk gives each value a register where it is made, the
   group's planned one where that is free; where it was not, a group's
   values sit in several registers, and the moves between them run.  Then,
   the heaviest group first, the group takes one register for all of its
   values where none of the values any of them meets holds it -- by the
   values live where each is made, the walk backwards through each block --
   one of the registers its values have, the most used first, or any other;
   a value that crosses a call keeps to a callee-saved register if it has
   one, a value live across an allocation off its result's register. */
typedef struct { Ref a, b; } Ra2VE;

static int ra2a_ve_cmp(const void *x, const void *y) {
  const Ra2VE *p = x, *q = y;
  if (p->a != q->a) return p->a < q->a ? -1 : 1;
  return p->b < q->b ? -1 : p->b > q->b;
}

static void ra2a_recolor(Isel *s, const bool *wants, uint32_t *grp, const double *gw, const Ref *val,
                         int *reg, const uint8_t *xhot, const uint8_t *xalloc, const int *gpool, int ngall,
                         Ref *eu, uint32_t eucap) {
  IrFunc *f = s->f;
  uint32_t n = f->ninsts;
#define RA2R_G(x) ({ uint32_t r_ = (x); while (grp[r_] != r_) r_ = grp[r_]; r_; })
#define RA2R_REG(x) (reg[x] != LOC_NONE && !IS_SLOT(reg[x]) && reg[x] >= 0 && reg[x] < 64)
  /* the groups of more than one value with values in more than one register */
  uint32_t *gfirst = xmalloc((n ? n : 1) * sizeof(uint32_t));
  uint32_t *gnext = xmalloc((n ? n : 1) * sizeof(uint32_t));
  int *greg = xmalloc((n ? n : 1) * sizeof(int));
  uint8_t *split = xcalloc(n ? n : 1, 1);
  for (uint32_t v = 0; v < n; v++) { gfirst[v] = UINT32_MAX; gnext[v] = UINT32_MAX; greg[v] = -2; }
  bool any = false;
  for (uint32_t v = n; v-- > 0;) {
    if (!wants[v] || !RA2R_REG(v)) continue;
    uint32_t g = RA2R_G(v);
    gnext[v] = gfirst[g];
    gfirst[g] = v;
    if (greg[g] == -2) greg[g] = reg[v];
    else if (greg[g] != reg[v]) { split[g] = 1; any = true; }
  }
  if (!any) { xfree(gfirst); xfree(gnext); xfree(greg); xfree(split); return; }
  /* who meets whom */
  Ra2VE *ve = NULL;
  size_t nve = 0, capve = 0;
  size_t limit = 32 * (size_t)n + (1u << 20);
  uint8_t *live = xcalloc(n ? n : 1, 1);
  Ref *lst = xmalloc((n ? n : 1) * sizeof(Ref));
  bool over = false;
#define RA2R_EDGE(x, y) do { if (val[x] != val[y] && (bool)s->cls[x] == (bool)s->cls[y] && \
      (split[RA2R_G(x)] || split[RA2R_G(y)])) { \
      if (nve == capve) { capve = capve ? capve * 2 : 1024; ve = xrealloc(ve, capve * sizeof(Ra2VE)); } \
      ve[nve++] = (Ra2VE){ (x) < (y) ? (x) : (y), (x) < (y) ? (y) : (x) }; if (nve > limit) over = true; } } while (0)
  for (uint32_t oi = 0; oi < f->norder && !over; oi++) {
    uint32_t b = f->order[oi];
    Block *bb = &f->blocks[b];
    uint32_t nl = 0;
#define RA2R_LIVE(x) do { Ref x_ = (x); if (wants[x_] && RA2R_REG(x_) && !live[x_]) { live[x_] = 1; lst[nl++] = x_; } } while (0)
    for (uint32_t k = s->lv_out_off[b]; k < s->lv_out_off[b + 1]; k++) RA2R_LIVE(s->lv_out[k]);
    if (bb->term == T_BRIF && s->fused && s->fused[bb->term_arg]) {
      uint32_t ne = eff_uses(s, f, bb->term_arg, eu, eucap);
      for (uint32_t j = 0; j < ne; j++) RA2R_LIVE(eff_arg(s, f, eu[j]));
    } else if (bb->term == T_BRIF || bb->term == T_BRTABLE) {
      RA2R_LIVE(bb->term_arg);
    }
    for (uint32_t i = 0; i < bb->nret; i++) RA2R_LIVE(bb->ret_args[i]);
    for (uint32_t k = 0; k < bb->nsuccs; k++)
      for (uint32_t j = 0; j < bb->succs[k].nargs; j++) RA2R_LIVE(bb->succs[k].args[j]);
    for (uint32_t i = bb->ninsts; i-- > 0 && !over;) {
      Ref u = bb->insts[i];
      if (wants[u] && RA2R_REG(u))
        for (uint32_t k = 0; k < nl; k++) {
          Ref x = lst[k];
          if (live[x] && x != u) RA2R_EDGE(u, x);
        }
      live[u] = 0;
      uint32_t ne = eff_uses(s, f, u, eu, eucap);
      for (uint32_t j = 0; j < ne; j++) RA2R_LIVE(eu[j]);
      if (nl > 128) {
        uint32_t m = 0;
        for (uint32_t k = 0; k < nl; k++) if (live[lst[k]]) lst[m++] = lst[k];
        nl = m;
      }
    }
    for (uint32_t p = 0; p < bb->nparams; p++) RA2R_LIVE(bb->params[p]);
    for (uint32_t p = 0; p < bb->nparams && !over; p++) {
      Ref u = bb->params[p];
      if (!wants[u] || !RA2R_REG(u)) continue;
      for (uint32_t k = 0; k < nl; k++) {
        Ref x = lst[k];
        if (live[x] && x != u) RA2R_EDGE(u, x);
      }
    }
    for (uint32_t k = 0; k < nl; k++) live[lst[k]] = 0;
#undef RA2R_LIVE
  }
#undef RA2R_EDGE
  xfree(live);
  xfree(lst);
  if (over) goto done;
  {
    qsort(ve, nve, sizeof(Ra2VE), ra2a_ve_cmp);
    size_t m = 0;
    for (size_t k = 0; k < nve; k++)
      if (!m || ve[k].a != ve[m - 1].a || ve[k].b != ve[m - 1].b) ve[m++] = ve[k];
    nve = m;
    uint32_t *aoff = xcalloc((size_t)n + 1, sizeof(uint32_t));
    for (size_t k = 0; k < nve; k++) { aoff[ve[k].a + 1]++; aoff[ve[k].b + 1]++; }
    for (uint32_t v = 0; v < n; v++) aoff[v + 1] += aoff[v];
    uint32_t *adj = xmalloc((aoff[n] ? aoff[n] : 1) * sizeof(uint32_t));
    uint32_t *acur = xmalloc(((size_t)n + 1) * sizeof(uint32_t));
    memcpy(acur, aoff, ((size_t)n + 1) * sizeof(uint32_t));
    for (size_t k = 0; k < nve; k++) { adj[acur[ve[k].a]++] = ve[k].b; adj[acur[ve[k].b]++] = ve[k].a; }
    xfree(acur);
    uint32_t *order = xmalloc((n ? n : 1) * sizeof(uint32_t));
    uint32_t ng = 0;
    for (uint32_t v = 0; v < n; v++) if (split[v]) order[ng++] = v;
    ra2a_gw_sort = gw;
    qsort(order, ng, sizeof(uint32_t), ra2a_gw_cmp);
    double wr[64];
    for (uint32_t k = 0; k < ng; k++) {
      uint32_t g = order[k];
      int c = s->cls[gfirst[g]] ? 1 : 0;
      /* the candidates: the group's registers by how often their values
         are made, then the rest of the pool */
      for (int r = 0; r < 64; r++) wr[r] = 0;
      bool alloc = false, hotcs = false;
      for (uint32_t v = gfirst[g]; v != UINT32_MAX; v = gnext[v]) {
        wr[reg[v]] += 1 + blk_freq(&f->blocks[f->insts[v].block]);
        alloc |= xalloc[v] != 0;
        bool cs = c ? fpr_is_callee_saved(reg[v]) : gpr_is_callee_saved(reg[v]);
        hotcs |= xhot[v] && cs;
      }
      const int *pool = c ? FPR_ALLOC : gpool;
      int np = c ? NFPR_ALLOC : ngall;
      int cand[64], nc = 0;
      for (int q = 0; q < np; q++) if (pool[q] >= 0 && pool[q] < 64) cand[nc++] = pool[q];
      /* the group's own first, heaviest first (stable for the rest) */
      for (int i = 1; i < nc; i++) {
        int x = cand[i], j = i;
        while (j > 0 && wr[cand[j - 1]] < wr[x]) { cand[j] = cand[j - 1]; j--; }
        cand[j] = x;
      }
      for (int ci = 0; ci < nc; ci++) {
        int r = cand[ci];
        if (r == greg[g] && !split[g]) break;
        if (!c && alloc && r == RA_ALLOC_RES_REG) continue;
        bool rcs = c ? fpr_is_callee_saved(r) : gpr_is_callee_saved(r);
        if (hotcs && !rcs) continue;
        bool ok = true;
        for (uint32_t v = gfirst[g]; v != UINT32_MAX && ok; v = gnext[v]) {
          if (xhot[v] && !rcs && (c ? fpr_is_callee_saved(reg[v]) : gpr_is_callee_saved(reg[v]))) ok = false;
          for (uint32_t t = aoff[v]; t < aoff[v + 1] && ok; t++) {
            Ref x = adj[t];
            if (reg[x] == r && RA2R_G(x) != g) ok = false;
          }
        }
        if (!ok) continue;
        for (uint32_t v = gfirst[g]; v != UINT32_MAX; v = gnext[v]) reg[v] = r;
        g_stats.ra2_recolored++;
        break;
      }
    }
    xfree(order);
    xfree(aoff);
    xfree(adj);
  }
done:
  xfree(ve);
  xfree(gfirst); xfree(gnext); xfree(greg); xfree(split);
#undef RA2R_G
#undef RA2R_REG
}

__attribute__((unused))
static void ra2_assign(Isel *s) {
  IrFunc *f = s->f;
  uint32_t n = f->ninsts;
  uint32_t nb = f->nblocks;
  compute_liveness(s);
  if (warmc_getenv("WARMC_RA2_DEBUG_AS")) {
    ir_print(f, "ra2a");
    for (uint32_t b = 0; b < f->nblocks; b++) {
      fprintf(stderr, "lvin b%u:", b);
      for (uint32_t k = s->lv_in_off[b]; k < s->lv_in_off[b + 1]; k++) fprintf(stderr, " v%u", s->lv_in[k]);
      fprintf(stderr, "\n");
    }
  }
  uint32_t eucap = eff_use_cap(f);
  Ref *eu = xmalloc(eucap * sizeof(Ref));

  /* the registers: the scan's pool, the spare ones last */
  int gpool[NGPR_ALLOC + 16];
  int ngpool = ra_gpr_pool(s, gpool);
  int nspare = isel_spare_gprs(s, gpool + ngpool);
  int ngall = ngpool + nspare;

  uint8_t *live = xcalloc(n ? n : 1, 1);
  Ref *lst = xmalloc((n ? n : 1) * sizeof(Ref));
  uint8_t *xhot = xcalloc(n ? n : 1, 1), *xcold = xcalloc(n ? n : 1, 1);
  /* across an allocation, whose fast path writes RA_ALLOC_RES_REG with no
     call to save it around */
  uint8_t *xalloc = xcalloc(n ? n : 1, 1);
  uint8_t *used = xcalloc(n ? n : 1, 1);
  uint32_t *boff = xmalloc((nb + 1) * sizeof(uint32_t));
  uint32_t totins = 0;
  for (uint32_t bi = 0; bi < nb; bi++) { boff[bi] = totins; totins += f->blocks[bi].ninsts + 1; }
  boff[nb] = totins;
  uint32_t *doff = xmalloc((totins ? totins : 1) * sizeof(uint32_t));
  Ref *dying = NULL;
  uint32_t ndying = 0, capdying = 0;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t b = f->order[oi];
    uint32_t nl;
    ra2a_scan_block(s, b, live, lst, &nl, &dying, &ndying, &capdying, doff + boff[b],
                    xhot, xcold, xalloc, used, eu, eucap);
  }

  /* each value's location: LOC_NONE where none is to be had */
  int *reg = s->loc;
  uint32_t slot = 0;
  for (uint32_t v = 0; v < n; v++) reg[v] = LOC_NONE;
  bool *wants = xcalloc(n ? n : 1, sizeof(bool));
  /* what only edges and returns read */
  uint8_t *edge_only = xcalloc(n ? n : 1, 1);
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *bb = &f->blocks[f->order[oi]];
    for (uint32_t k = 0; k < bb->nsuccs; k++)
      for (uint32_t j = 0; j < bb->succs[k].nargs; j++) edge_only[bb->succs[k].args[j]] = 1;
    for (uint32_t i = 0; i < bb->nret; i++) edge_only[bb->ret_args[i]] = 1;
  }
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *bb = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < bb->ninsts; i++)
      for (uint32_t j = 0; j < f->insts[bb->insts[i]].nargs; j++) edge_only[ir_args(f, bb->insts[i])[j]] = 0;
    if (bb->term == T_BRIF || bb->term == T_BRTABLE) edge_only[bb->term_arg] = 0;
  }
  for (uint32_t v = 0; v < n; v++) {
    if (!value_needs_location(f, v) || skip_location(s, v)) continue;
    if (s->pinned[v]) { reg[v] = SLOT(take_slot(s, v, &slot)); continue; }
    if (ra_zero_const(s, v, s->cls[v])) continue;
    /* a constant made where it is read, unless the spiller kept it in a
       register */
    if (remat_ok(s, v) && !(f->ra2_mem && v < f->ra2_nmem && f->ra2_mem[v] == 3)) continue;
    /* a constant the spiller made at a block's end for its edges alone: the
       edges' moves make it where it goes */
    if (f->ra2_mem && v < f->ra2_nmem && f->ra2_mem[v] == 2 && !s->no_remat[v] && edge_only[v]) continue;
    wants[v] = true;
  }

  /* Affinities: a param and the arguments the edges pass it, a copy and
     its operand, a result and the operand it is tied to -- one group, which
     takes the register its first member took wherever that is free, so
     that the edge's move or the copy is none */
  uint32_t *grp = xmalloc((n ? n : 1) * sizeof(uint32_t));
  int *gpref = xmalloc((n ? n : 1) * sizeof(int));
  for (uint32_t v = 0; v < n; v++) { grp[v] = v; gpref[v] = -1; }
#define RA2A_FIND(x) ({ uint32_t r_ = (x); while (grp[r_] != r_) r_ = grp[r_] = grp[grp[r_]]; r_; })
#define RA2A_UNION(x, y) do { uint32_t a_ = RA2A_FIND(x), b_ = RA2A_FIND(y); \
    if (a_ != b_ && (bool)s->cls[x] == (bool)s->cls[y]) grp[a_] = b_; } while (0)
  bool affin = !warmc_getenv("WARMC_RA2_NO_AFFIN");
  if (affin && warmc_getenv("WARMC_RA2_BLIND_AFFIN"))
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      Block *bb = &f->blocks[f->order[oi]];
      for (uint32_t si = 0; si < bb->nsuccs; si++) {
        Edge *e = &bb->succs[si];
        if (e->eh) continue;
        Block *tb = &f->blocks[e->target];
        for (uint32_t j = 0; j < e->nargs && j < tb->nparams; j++)
          if (wants[e->args[j]] && wants[tb->params[j]]) RA2A_UNION(e->args[j], tb->params[j]);
      }
      for (uint32_t i = 0; i < bb->ninsts; i++) {
        Ref u = bb->insts[i];
        if (!wants[u]) continue;
        Ref t = f->insts[u].op == I_COPY ? ir_args(f, u)[0] : tied_operand_of(f, u);
        if (t != REF_NONE && wants[t]) RA2A_UNION(u, t);
      }
    }
  double *gw = xcalloc(n ? n : 1, sizeof(double));
  Ref *val = NULL;
  if (affin && !warmc_getenv("WARMC_RA2_BLIND_AFFIN")) ra2a_coalesce(s, wants, grp, eu, eucap, gw, &val);
  /* the groups of more than one value, and the registers they took */
  uint32_t *gsize = xcalloc(n ? n : 1, sizeof(uint32_t));
  for (uint32_t v = 0; v < n; v++) if (wants[v]) gsize[RA2A_FIND(v)]++;
  uint32_t gclaim[64] = { 0 }, fclaim[64] = { 0 };
  /* a group one of whose values crosses a call takes a callee-saved
     register for all of them: a loop's param whose copy ahead of the
     call is what crosses it, in the same register, moves nothing */
  uint8_t *ghot = xcalloc(n ? n : 1, 1);
  if (!warmc_getenv("WARMC_RA2_NO_GHOT"))
    for (uint32_t v = 0; v < n; v++)
      if (wants[v] && xhot[v] && ghot[RA2A_FIND(v)] < xhot[v]) ghot[RA2A_FIND(v)] = xhot[v];
  if (warmc_getenv("WARMC_RA2_NO_CLAIM")) gsize[0] = gsize[0];
  uint32_t *paoff = NULL, *padj = NULL;
  if (val && !warmc_getenv("WARMC_RA2_NO_PLAN"))
    ra2a_plan(s, wants, grp, gsize, gw, ghot, xalloc, val, gpool, ngall, gpref, gclaim, fclaim, eu, eucap,
              &paoff, &padj);
  bool use_avoid = paoff && !warmc_getenv("WARMC_RA2_NO_AVOID");
#define RA2A_AVOID(x) ({ uint64_t m_ = 0; if (use_avoid) { uint32_t g_ = RA2A_FIND(x); \
    for (uint32_t t_ = paoff[g_]; t_ < paoff[g_ + 1]; t_++) if (gpref[padj[t_]] >= 0) m_ |= 1ull << gpref[padj[t_]]; } m_; })

  /* occupancy: the value in each register, the one walk's */
  Ref gown[64], fown[64];
  int nfpool = NFPR_ALLOC;
  uint8_t *csa = NULL;
  int32_t *csa_slot = NULL;
  int hint_reg[2] = { -1, -1 };
  (void)hint_reg;

  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t b = f->order[oi];
    Block *bb = &f->blocks[b];
    for (int k = 0; k < 64; k++) gown[k] = fown[k] = REF_NONE;
#define RA2A_IDX(c, r) ((c) ? (r) : (r))
    /* the registers the values live in hold */
    for (uint32_t k = s->lv_in_off[b]; k < s->lv_in_off[b + 1]; k++) {
      Ref x = s->lv_in[k];
      int l = reg[x];
      if (l == LOC_NONE || IS_SLOT(l) || l < 0 || l >= 64) continue;
      if (s->cls[x]) fown[l] = x;
      else gown[l] = x;
    }
    /* the params */
    for (uint32_t p = 0; p < bb->nparams; p++) {
      Ref x = bb->params[p];
      if (!wants[x] || reg[x] != LOC_NONE) continue;
      int c = s->cls[x] ? 1 : 0;
      int hint = -1;
      if (b == f->entry) {
        /* the argument registers: x1.. after the instance, v0.. */
        int ni = 1, nf = 0;
        for (uint32_t i = 0; i < f->sig->nparams && i < bb->nparams; i++) {
          bool fp = vt_is_vec_reg(f->sig->params[i]);
          int h = fp ? (nf < 8 ? nf : -1) : (ni <= 7 ? ni : -1);
          if (fp) nf++; else ni++;
          if (bb->params[i] == x) hint = h;
        }
      } else {
        /* the register the first way in that is done passes it in */
        for (uint32_t q = 0; q < bb->npreds && hint < 0; q++) {
          Block *pb = &f->blocks[bb->preds[q]];
          for (uint32_t si = 0; si < pb->nsuccs && hint < 0; si++) {
            Edge *e = &pb->succs[si];
            if (e->target != b || p >= e->nargs) continue;
            int l = reg[e->args[p]];
            if (l != LOC_NONE && !IS_SLOT(l) && (bool)s->cls[e->args[p]] == (bool)c) hint = l;
          }
        }
      }
      Ref *own = c ? fown : gown;
      int gp = gpref[RA2A_FIND(x)];
      if (gp >= 0 && own[gp] == REF_NONE) hint = gp;
      int pick = ra2a_pick(s, x, c, own, c ? FPR_ALLOC : gpool, c ? nfpool : ngall, hint, xhot, xalloc,
                           c ? fclaim : gclaim, ghot[RA2A_FIND(x)], RA2A_AVOID(x));
      if (pick < 0) { reg[x] = SLOT(take_slot(s, x, &slot)); g_stats.ra2_fallback++; continue; }
      reg[x] = pick;
      own[pick] = x;
      if (gpref[RA2A_FIND(x)] < 0) {
        gpref[RA2A_FIND(x)] = pick;
        if (gsize[RA2A_FIND(x)] > 1) (c ? fclaim : gclaim)[pick]++;
      }
    }
    /* the instructions */
    uint32_t *dp = doff + boff[b];
    for (uint32_t i = 0; i < bb->ninsts; i++) {
      Ref u = bb->insts[i];
      /* a copy reads its operand before it writes: the operand's register,
         where it dies there, is the copy's to take (no move) */
      if (f->insts[u].op == I_COPY)
        for (uint32_t k = dp[i]; k < dp[i + 1]; k++) {
          Ref x = dying[k];
          int l = reg[x];
          if (l == LOC_NONE || IS_SLOT(l) || l < 0 || l >= 64) continue;
          Ref *own = s->cls[x] ? fown : gown;
          if (own[l] == x) own[l] = REF_NONE;
        }
      if (wants[u] && reg[u] == LOC_NONE) {
        int c = s->cls[u] ? 1 : 0;
        Ref *own = c ? fown : gown;
        const int *pool = c ? FPR_ALLOC : gpool;
        int np = c ? nfpool : ngall;
        bool v128 = (ValType)f->insts[u].type == VT_V128;
        bool want_cs = xhot[u] && !v128;
        /* a call's result where it comes back, a copy where its operand is
           when that dies here */
        int hint = -1;
        uint16_t op = f->insts[u].op;
        if ((op == I_CALL || op == I_CALL_INDIRECT || op == I_CALL_REF) && !want_cs) hint = c ? 0 : 0;
        if (op == I_COPY) {
          Ref a0 = ir_args(f, u)[0];
          int l = reg[a0];
          bool dies = false;
          for (uint32_t k = dp[i]; k < dp[i + 1] && !dies; k++) dies = dying[k] == a0;
          if (dies && l != LOC_NONE && !IS_SLOT(l)) hint = l;
        }
        /* an operand of its group that dies here leaves the result its
           register: it is read before the result is written */
        if (affin && op != I_COPY)
          for (uint32_t k = dp[i]; k < dp[i + 1]; k++) {
            Ref x = dying[k];
            int l = reg[x];
            if (l == LOC_NONE || IS_SLOT(l) || l < 0 || l >= 64 || (bool)s->cls[x] != (bool)c) continue;
            if (RA2A_FIND(x) != RA2A_FIND(u) || own[l] != x) continue;
            own[l] = REF_NONE;
            if (hint < 0) hint = l;
          }
        if (hint < 0) {
          int gp = gpref[RA2A_FIND(u)];
          if (gp >= 0 && own[gp] == REF_NONE) hint = gp;
        }
        int pick = ra2a_pick(s, u, c, own, pool, np, hint, xhot, xalloc, c ? fclaim : gclaim,
                             ghot[RA2A_FIND(u)], RA2A_AVOID(u));
        if (warmc_getenv("WARMC_RA2_DEBUG_CS") && want_cs && pick >= 0 &&
            !(c ? fpr_is_callee_saved(pick) : gpr_is_callee_saved(pick))) {
          fprintf(stderr, "ra2 cs-miss f%u b%u v%u %s:", f->func_index, b, u, ir_op_name(f->insts[u].op));
          for (int k = 0; k < np; k++) {
            int r = pool[k];
            if (!(c ? fpr_is_callee_saved(r) : gpr_is_callee_saved(r))) continue;
            if (own[r] != REF_NONE)
              fprintf(stderr, " r%d=v%u(%s%s)", r, own[r], ir_op_name(f->insts[own[r]].op), xhot[own[r]] ? ",x" : "");
            else fprintf(stderr, " r%d=free", r);
          }
          fprintf(stderr, "\n");
        }
        if (pick < 0) {
          reg[u] = SLOT(take_slot(s, u, &slot));
          g_stats.ra2_fallback++;
          if (warmc_getenv("WARMC_RA2_DEBUG_FB")) {
            fprintf(stderr, "ra2 fallback f%u b%u v%u %s xhot=%d:", f->func_index, b, u,
                    ir_op_name(f->insts[u].op), xhot[u]);
            for (int k = 0; k < np; k++)
              if (own[pool[k]] != REF_NONE)
                fprintf(stderr, " r%d=v%u(%s%s%s)", pool[k], own[pool[k]], ir_op_name(f->insts[own[pool[k]]].op),
                        s->pinned[own[pool[k]]] ? ",pin" : "",
                        f->ra2_dbg && own[pool[k]] < f->ninsts ? (const char *[]){",UNM", "", ",res", ""}[f->ra2_dbg[own[pool[k]]]] : "");
            fprintf(stderr, "\n");
          }
        } else {
          reg[u] = pick;
          own[pick] = u;
          if (gpref[RA2A_FIND(u)] < 0) {
            gpref[RA2A_FIND(u)] = pick;
            if (gsize[RA2A_FIND(u)] > 1) (c ? fclaim : gclaim)[pick]++;
          }
        }
      }
      /* what dies here lets its register go; a result read by nothing
         goes at once */
      for (uint32_t k = dp[i]; k < dp[i + 1]; k++) {
        Ref x = dying[k];
        int l = reg[x];
        if (l == LOC_NONE || IS_SLOT(l) || l < 0 || l >= 64) continue;
        Ref *own = s->cls[x] ? fown : gown;
        if (own[l] == x) own[l] = REF_NONE;
      }
      if (wants[u] && !used[u]) {
        int l = reg[u];
        if (l != LOC_NONE && !IS_SLOT(l) && l >= 0 && l < 64) {
          Ref *own = s->cls[u] ? fown : gown;
          if (own[l] == u) own[l] = REF_NONE;
        }
      }
    }
#undef RA2A_IDX
  }

  if (val && !warmc_getenv("WARMC_RA2_NO_RECOLOR"))
    ra2a_recolor(s, wants, grp, gw, val, reg, xhot, xalloc, gpool, ngall, eu, eucap);
  xfree(val);
  xfree(gw);
  xfree(paoff);
  xfree(padj);
#undef RA2A_AVOID
  {
    const char *dr = warmc_getenv("WARMC_RA2_DEBUG_REG");
    if (dr && (uint32_t)atoi(dr) == f->func_index)
      for (uint32_t v = 0; v < n; v++)
        if (wants[v]) fprintf(stderr, "reg v%u %s -> %d grp %u gpref %d xhot %d\n", v, ir_op_name(f->insts[v].op),
                              reg[v], RA2A_FIND(v), gpref[RA2A_FIND(v)], xhot[v]);
  }
  /* the caller-saved registers live across a call: saved around it */
  for (uint32_t v = 0; v < n; v++) {
    int l = reg[v];
    if (l == LOC_NONE || IS_SLOT(l)) continue;
    bool cs = s->cls[v] ? fpr_is_callee_saved(l) : gpr_is_callee_saved(l);
    bool v128 = (ValType)f->insts[v].type == VT_V128;
    if (cs && !v128) {
      if (s->cls[v]) s->used_fpr_cs |= 1u << l;
      else s->used_gpr_cs |= 1u << l;
      continue;
    }
    if (!xhot[v] && !xcold[v]) continue;
    if (!csa) {
      csa = xcalloc(n ? n : 1, 1);
      csa_slot = xmalloc((n ? n : 1) * sizeof(int32_t));
      for (uint32_t z = 0; z < n; z++) csa_slot[z] = -1;
    }
    csa[v] = 1;
    csa_slot[v] = (int32_t)take_slot(s, v, &slot);
    if (xhot[v]) g_stats.ra2_csa_hot++;
  }
  s->csa_on = csa;
  s->csa_slot = csa_slot;
  if (csa) {
    s->csa_first = xcalloc(n ? n : 1, sizeof(uint32_t));
    s->csa_n = xcalloc(n ? n : 1, sizeof(uint32_t));
    uint32_t *web = xmalloc((n ? n : 1) * sizeof(uint32_t));
    for (uint32_t v = 0; v < n; v++) web[v] = v;
    CsaListCtx ctx = { web, xcalloc(n ? n : 1, sizeof(uint32_t)), 0 };
    for_each_clobber_site(s, f, csa_list_sites, &ctx);
    xfree(ctx.stamp);
    xfree(web);
  }
  s->nslots = slot;

  xfree(eu); xfree(live); xfree(lst); xfree(xhot); xfree(xcold); xfree(xalloc); xfree(used);
  xfree(boff); xfree(doff); xfree(dying); xfree(wants); xfree(edge_only); xfree(grp); xfree(gpref); xfree(gsize); xfree(ghot);
#undef RA2A_FIND
#undef RA2A_UNION
}
