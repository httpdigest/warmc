/* ra_spill.inc -- the spiller ahead of the allocator (-fra2, WARMC_RA2=1).

   The scan (regalloc_linear) gives a value one location for all of its
   life: a register, or a slot that every use reloads from.  A value read
   all through a loop that calls, or live across a cold stretch, takes a
   slot everywhere for want of a register somewhere.  This pass decides,
   at every point of the function, which values are in registers, by
   Belady's rule over the distances to their next uses (Braun and Hack,
   "Register Spilling and Live-Range Splitting for SSA-Form Programs",
   2009), and writes the decisions into the function as copies, so that
   each piece of a value's life is a value of its own with one location
   again -- what the scan and everything after it expect:

   - A value is spilled once, by a copy right after its definition that
     takes a slot (Inst I_COPY, IrFunc.ra2_mem): the copy dominates every
     place the value may be needed from memory.
   - A reload is a copy of that copy, a new value in a register, made ahead
     of a use where the value is not in a register and is used again soon;
     a use with none soon after reads the slot itself, as any use can.
   - A constant, an immutable global and the barrier's mask are made again
     where they are needed (a copy of their instruction), never spilled.
   - Where ways meet, a block param takes the versions each way brings: the
     edge's moves then load, store or move them as the locations say.

   A block's registers on entry are its live values with the nearest next
   uses: on a loop's header, the values the loop uses before the values it
   only carries (a way out of a loop adds RA2_LOOPX to the distance).  At an
   instruction, its operands come in first and, if the registers are full,
   the value used furthest ahead goes; at a call that clobbers the
   caller-saved registers -- one not on a way branch hints mark rare, nor an
   allocation's slow path, which the scan saves around (csa) -- no more of
   them stay than the callee-saved registers hold.  The scan then colours
   what is left, the pressure at most its registers. */

#define RA2_INF (1 << 28)
#define RA2_LOOPX (1 << 14)

/* the operand an instruction reads after writing its result, as it stands
   (ra2_assign.inc) */
static Ref ra2a_late_arg(Isel *s, Ref v);
/* a value read from its slot is loaded into a register of its own when it
   is read again within this many positions */
#define RA2_REUSE 24

typedef struct { Ref v, ver; int32_t nu; } Ra2W;
/* a block param made for an edge whose source the search had not reached:
   its argument, filled at the end of that block */
typedef struct { uint32_t from, si, ai; Ref v; } Ra2Pend;

typedef struct {
  IrFunc *f;
  Isel *s;
  uint32_t n0;
  uint8_t *need, *rem;
  /* values the pass leaves where they are that hold a register all the
     same: a native pointer (mem_ptr) -- counted among the registers, never
     pushed out, renamed nor spilled */
  uint8_t *res;
  Ref *mem;
  /* per block, the registers it ends with (xw from xoff, xn of them) */
  uint32_t *xoff, *xn;
  Ra2W *xw;
  uint32_t nxw, capxw;
  uint8_t *done;
  int32_t *din;
  int K[2], Kcs[2];
  Ra2Pend *pend;
  uint32_t npend, cappend;
  /* the values the pass made, and of each whether it is a copy in a slot */
  uint8_t *slotv;
  uint32_t nslotv;
  /* the block being rewritten: its new list, and where each value it
     defines is in it */
  uint32_t cur_b;
  Ref *out;
  uint32_t nout, capout;
  int32_t *outpos;
  /* the params the pass added, to drop the trivial ones */
  Ref *added;
  uint32_t nadded, capadded;
  uint64_t nreload, nspill, nremat, nparam, ncallsplit, nwhole, ndeadparam, nconstparam;
  /* per block the most values of each class live at once; the loop walk's
     marks and stack */
  int (*maxp)[2];
  uint32_t *lstamp, *lstack, lgen;
  uint32_t *vuse, *vlv, vgen;
  /* per block, how often it runs by the static estimate (ra2_freqs) */
  double *freq;
  bool expdist;
  /* each value's copies in slots: where one dominates, a reload reads it
     (mhead per value, through mnext) */
  uint32_t *mhead, *mnext, *mblk, nmc, capmc;
  Ref *mcp;
  uint8_t *inblk;
  /* folded instructions not moved to their one reader (ra2_fold_order): an
     address several accesses fold, each its own -- made again at each
     reader, its operands the reader's (ra2_unshare) */
  uint8_t *fmoved, *shared;
} Ra2;

__attribute__((unused))
static bool ra2_has_cont(const IrFunc *f) {
  for (uint32_t v = 0; v < f->ninsts; v++)
    if (f->insts[v].op == I_CONT_OP || f->insts[v].op == I_CONT_GET) return true;
  return false;
}

__attribute__((unused))
static bool ra2_on(void) {
  static _Atomic int on = -1;
  if (on < 0) on = warmc_getenv("WARMC_RA2") != NULL;
  return on;
}

/* the instruction a new value is, appended nowhere: the caller places it */
static Ref ra2_new(Ra2 *R, uint32_t blk, IrOp op, ValType t, const Ref *args, uint32_t na,
                   uint64_t imm, uint64_t imm2) {
  IrFunc *f = R->f;
  Ref r = ir_emit(f, blk, op, t, args, na, imm, imm2);
  f->blocks[blk].ninsts--;
  if (r >= R->nslotv) {
    uint32_t nc = R->nslotv ? R->nslotv : 64;
    while (nc <= r) nc *= 2;
    R->slotv = xrealloc(R->slotv, nc);
    memset(R->slotv + R->nslotv, 0, nc - R->nslotv);
    R->nslotv = nc;
  }
  return r;
}

static void ra2_out(Ra2 *R, Ref v) {
  if (R->nout == R->capout) {
    R->capout = R->capout ? R->capout * 2 : 64;
    R->out = xrealloc(R->out, R->capout * sizeof(Ref));
  }
  R->out[R->nout++] = v;
}

/* insert v into block b at index at */
static void ra2_insert(IrFunc *f, uint32_t b, uint32_t at, Ref v) {
  Block *bb = &f->blocks[b];
  if (bb->ninsts == bb->cap_insts) {
    uint32_t oc = bb->cap_insts;
    bb->cap_insts = bb->cap_insts ? bb->cap_insts * 2 : 8;
    bb->insts = ir_grow(f, bb->insts, (size_t)oc * sizeof(uint32_t), (size_t)bb->cap_insts * sizeof(uint32_t));
  }
  memmove(bb->insts + at + 1, bb->insts + at, (bb->ninsts - at) * sizeof(uint32_t));
  bb->insts[at] = v;
  bb->ninsts++;
}

/* whether block a dominates block b */
static bool ra2_dom(IrFunc *f, uint32_t a, uint32_t b) {
  for (uint32_t d = b, k = 0; d != REF_NONE && d < f->nblocks && k < f->nblocks; k++) {
    if (d == a) return true;
    if (d == f->entry || f->blocks[d].idom == d) break;
    d = f->blocks[d].idom;
  }
  return false;
}

static void ra2_mem_note(Ra2 *R, Ref x, Ref c, uint32_t blk) {
  if (!R->mhead) {
    R->mhead = xmalloc((R->n0 ? R->n0 : 1) * sizeof(uint32_t));
    for (uint32_t v = 0; v < R->n0; v++) R->mhead[v] = UINT32_MAX;
  }
  if (R->nmc == R->capmc) {
    R->capmc = R->capmc ? R->capmc * 2 : 64;
    R->mnext = xrealloc(R->mnext, R->capmc * sizeof(uint32_t));
    R->mblk = xrealloc(R->mblk, R->capmc * sizeof(uint32_t));
    R->mcp = xrealloc(R->mcp, R->capmc * sizeof(Ref));
  }
  R->mcp[R->nmc] = c;
  R->mblk[R->nmc] = blk;
  R->mnext[R->nmc] = R->mhead[x];
  R->mhead[x] = R->nmc++;
}

/* a copy of x in a slot that block b's code may read: one made in b
   already, or in a block that dominates b */
static Ref ra2_mem_find(Ra2 *R, Ref x, uint32_t b) {
  if (!R->mhead) return REF_NONE;
  for (uint32_t k = R->mhead[x]; k != UINT32_MAX; k = R->mnext[k])
    if (R->mblk[k] == b || ra2_dom(R->f, R->mblk[k], b)) return R->mcp[k];
  return REF_NONE;
}

/* The value's copy in a slot for block b to read: one there or above it,
   else one made right after its definition, which dominates every read;
   REF_NONE for one made again instead. */
static Ref ra2_mem_for(Ra2 *R, Ref x, uint32_t blk) {
  if (R->rem[x]) return REF_NONE;
  if (R->mem[x] == x) return x;
  Ref have = ra2_mem_find(R, x, blk);
  if (have != REF_NONE) return have;
  if (R->mem[x] != REF_NONE) return R->mem[x];
  IrFunc *f = R->f;
  uint32_t db = f->insts[x].block;
  bool param = f->insts[x].op == I_PARAM;
  ValType t = (ValType)f->insts[x].type;
  {
    const char *de = warmc_getenv("WARMC_RA2_DEBUG_EV");
    if (de && (uint32_t)atoi(de) == f->func_index)
      fprintf(stderr, "ev v%u %s def b%u d%u at b%u d%u\n", x, ir_op_name(f->insts[x].op), db,
              f->blocks[db].loop_depth, R->cur_b, f->blocks[R->cur_b].loop_depth);
  }
  Ref c = ra2_new(R, db, I_COPY, t, &x, 1, 0, 0);
  R->slotv[c] = 1;
  R->mem[x] = c;
  ra2_mem_note(R, x, c, db);
  R->nspill++;
  /* after x -- past the results that follow a call, which nothing may
     come between -- or first for a param */
  if (db == R->cur_b) {
    uint32_t at = param ? 0 : (uint32_t)R->outpos[x] + 1;
    while (!param && at < R->nout && f->insts[R->out[at]].op == I_RESULT) at++;
    ra2_out(R, c);
    memmove(R->out + at + 1, R->out + at, (R->nout - 1 - at) * sizeof(Ref));
    R->out[at] = c;
    for (uint32_t k = at + 1; k < R->nout; k++)
      if (R->out[k] < R->n0) R->outpos[R->out[k]] = (int32_t)k;
  } else {
    Block *b = &f->blocks[db];
    uint32_t at = 0;
    if (!param) {
      while (at < b->ninsts && b->insts[at] != x) at++;
      at++;
      while (at < b->ninsts && f->insts[b->insts[at]].op == I_RESULT) at++;
    }
    ra2_insert(f, db, at, c);
  }
  return c;
}

static Ref ra2_mem(Ra2 *R, Ref x) { return ra2_mem_for(R, x, R->cur_b); }

/* x's distance at block b's entry, RA2_INF where it is not live there */
static int32_t ra2_din_live(Ra2 *R, uint32_t b, Ref x, bool *live) {
  Isel *s = R->s;
  uint32_t lo = s->lv_in_off[b], hi = s->lv_in_off[b + 1];
  while (lo < hi) {
    uint32_t mid = (lo + hi) / 2;
    if (s->lv_in[mid] < x) lo = mid + 1;
    else hi = mid;
  }
  if (lo < s->lv_in_off[b + 1] && s->lv_in[lo] == x) { *live = true; return R->din[lo]; }
  *live = false;
  return RA2_INF;
}

static int32_t ra2_din(Ra2 *R, uint32_t b, Ref x) {
  bool live;
  return ra2_din_live(R, b, x, &live);
}

/* x's distance from block b's end: through each way out, a way out of a
   loop RA2_LOOPX further */
static double ra2_edge_w(const IrFunc *f, const Block *p, const Edge *e);

static int32_t ra2_dout(Ra2 *R, uint32_t b, Ref x) {
  IrFunc *f = R->f;
  Block *bb = &f->blocks[b];
  int32_t best = RA2_INF;
  /* Expected, the ways out weighted as ra2_freqs weighs them: a way on
     which x is read no more counts as one out of the loop -- a value one
     arm reads is as far as half its distance there and half that, where
     the nearest way had it as near as the arm (WARMC_RA2_NO_EXPDIST) */
  if (R->expdist) {
    double tot = 0, sum = 0;
    bool live = false;
    for (uint32_t k = 0; k < bb->nsuccs; k++) {
      double w = ra2_edge_w(f, bb, &bb->succs[k]);
      if (w <= 0) continue;
      uint32_t t = bb->succs[k].target;
      bool lv;
      int32_t d = ra2_din_live(R, t, x, &lv);
      double dd;
      /* not read on that way any more; or not found yet (the fixpoint
         brings it down from there) */
      if (!lv) dd = RA2_LOOPX;
      else {
        live = true;
        dd = d >= RA2_INF ? 4.0 * RA2_LOOPX : d + (f->blocks[t].loop_depth < bb->loop_depth ? RA2_LOOPX : 0);
      }
      tot += w;
      sum += w * dd;
    }
    if (!live || tot <= 0) {
      /* read only on ways the weights leave out (a handler's): nearest */
      for (uint32_t k = 0; k < bb->nsuccs; k++) {
        int32_t d = ra2_din(R, bb->succs[k].target, x);
        if (d < best) best = d;
      }
      return best;
    }
    double e = sum / tot;
    return e >= RA2_INF ? RA2_INF - 1 : (int32_t)e;
  }
  for (uint32_t k = 0; k < bb->nsuccs; k++) {
    uint32_t t = bb->succs[k].target;
    int32_t d = ra2_din(R, t, x);
    if (d >= RA2_INF) continue;
    if (f->blocks[t].loop_depth < bb->loop_depth) d += RA2_LOOPX;
    if (d < best) best = d;
  }
  return best;
}

/* An operand the selection takes as an immediate: a constant that holds no
   register for it, nor counts as a read the distances see -- the reader
   gets a constant of its own, made where it reads it, which takes no
   location (analyse_immediates), where a version the pass made of the
   constant -- a block's param -- would take a register and the immediate's
   form with it (a compare with zero, a test of the sign bit). */
static bool ra2_imm_use(Ra2 *R, Ref u, uint32_t j, Ref x) {
  return x < R->n0 && R->rem[x] && R->f->insts[x].op == I_CONST && is_imm_at(R->s, u, j, x) &&
         !warmc_getenv("WARMC_RA2_NO_IMMUSE");
}

/* How often each block runs, by the estimate that needs no profile: the
   entry once; a block what its ways in bring, each way a share of its
   source's runs -- the ways out of a block split them evenly, a cold or
   diverging target taking a hundredth, a way out of a loop a tenth -- and
   a loop's header ten times what enters it.  So a loop's arms run half as
   often as its header each, where loop depths alone count them as often. */
static double ra2_edge_w(const IrFunc *f, const Block *p, const Edge *e) {
  const Block *t = &f->blocks[e->target];
  if (e->eh || t->is_pad) return 0;
  if (t->cold || t->diverges) return 0.01;
  if (t->loop_depth < p->loop_depth) return 0.1;
  return 1;
}

static void ra2_freqs(Ra2 *R) {
  IrFunc *f = R->f;
  uint32_t nb = f->nblocks;
  R->freq = xcalloc(nb ? nb : 1, sizeof(double));
  uint32_t *by = xmalloc((f->norder ? f->norder : 1) * sizeof(uint32_t));
  uint32_t n = 0;
  for (uint32_t oi = 0; oi < f->norder; oi++) by[n++] = f->order[oi];
  /* by rpo: an insertion sort is too slow for big functions -- counting */
  uint32_t maxr = 0;
  for (uint32_t i = 0; i < n; i++) if (f->blocks[by[i]].rpo > maxr) maxr = f->blocks[by[i]].rpo;
  uint32_t *slot = xmalloc(((size_t)maxr + 1) * sizeof(uint32_t));
  for (uint32_t r = 0; r <= maxr; r++) slot[r] = UINT32_MAX;
  for (uint32_t i = 0; i < n; i++) slot[f->blocks[by[i]].rpo] = by[i];
  n = 0;
  for (uint32_t r = 0; r <= maxr; r++) if (slot[r] != UINT32_MAX) by[n++] = slot[r];
  xfree(slot);
  for (uint32_t i = 0; i < n; i++) {
    uint32_t b = by[i];
    Block *bb = &f->blocks[b];
    if (b == f->entry) { R->freq[b] = 1; continue; }
    double in = 0;
    bool header = false;
    for (uint32_t q = 0; q < bb->npreds; q++) {
      uint32_t pb = bb->preds[q];
      const Block *p = &f->blocks[pb];
      if (p->rpo >= bb->rpo) { header = true; continue; }
      bool dup = false;
      for (uint32_t z = 0; z < q && !dup; z++) dup = bb->preds[z] == pb;
      if (dup) continue;
      double tot = 0, to = 0;
      for (uint32_t k = 0; k < p->nsuccs; k++) {
        double w = ra2_edge_w(f, p, &p->succs[k]);
        tot += w;
        if (p->succs[k].target == b) to += w;
      }
      if (tot > 0) in += R->freq[pb] * to / tot;
    }
    if (header) in *= 10;
    if (in > 1e12) in = 1e12;
    R->freq[b] = in;
  }
  xfree(by);
}

/* The reads of instruction u as the distances count them, into rb: through
   the folded instructions several read (each reader reads their operands),
   none of a constant taken as an immediate; none at all for such a folded
   instruction itself */
static uint32_t ra2_reads_in(Ra2 *R, Ref u, Ref **rb, uint32_t *cap, uint32_t n) {
  IrFunc *f = R->f;
  for (uint32_t j = 0; j < f->insts[u].nargs; j++) {
    Ref y = ir_args(f, u)[j];
    if (y < R->n0 && R->shared && R->shared[y]) { n = ra2_reads_in(R, y, rb, cap, n); continue; }
    if (ra2_imm_use(R, u, j, y)) continue;
    if (n == *cap) { *cap = *cap ? *cap * 2 : 16; *rb = xrealloc(*rb, *cap * sizeof(Ref)); }
    (*rb)[n++] = y;
  }
  return n;
}

static uint32_t ra2_reads(Ra2 *R, Ref u, Ref **rb, uint32_t *cap) {
  if (u < R->n0 && R->shared && R->shared[u]) return 0;
  return ra2_reads_in(R, u, rb, cap, 0);
}

/* The first use of each live-in in its block, then the distances to a
   fixpoint (from the end of the order backwards, a loop's back edges
   carrying them round). */
static void ra2_distances(Ra2 *R) {
  IrFunc *f = R->f;
  Isel *s = R->s;
  uint32_t nb = f->nblocks;
  uint32_t tot = s->lv_in_off[nb];
  R->din = xmalloc((tot ? tot : 1) * sizeof(int32_t));
  int32_t *fu = xmalloc((tot ? tot : 1) * sizeof(int32_t));
  int32_t *first = xmalloc((R->n0 ? R->n0 : 1) * sizeof(int32_t));
  for (uint32_t i = 0; i < R->n0; i++) first[i] = -1;
  Ref *rb = NULL;
  uint32_t caprb = 0;
  for (uint32_t k = 0; k < tot; k++) R->din[k] = RA2_INF;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t b = f->order[oi];
    Block *bb = &f->blocks[b];
    Ref *touched = NULL;
    uint32_t nt = 0, ct = 0;
#define RA2_SEEN(x, p) do { Ref x_ = (x); if (x_ < R->n0 && first[x_] < 0) { first[x_] = (p); \
      if (nt == ct) { ct = ct ? ct * 2 : 32; touched = xrealloc(touched, ct * sizeof(Ref)); } \
      touched[nt++] = x_; } } while (0)
    for (uint32_t i = 0; i < bb->ninsts; i++) {
      uint32_t na = ra2_reads(R, bb->insts[i], &rb, &caprb);
      for (uint32_t j = 0; j < na; j++) RA2_SEEN(rb[j], (int32_t)i + 1);
    }
    int32_t te = (int32_t)bb->ninsts + 1;
    if (bb->term == T_BRIF || bb->term == T_BRTABLE) RA2_SEEN(bb->term_arg, te);
    for (uint32_t i = 0; i < bb->nret; i++) RA2_SEEN(bb->ret_args[i], te);
    for (uint32_t k = 0; k < bb->nsuccs; k++)
      for (uint32_t j = 0; j < bb->succs[k].nargs; j++) RA2_SEEN(bb->succs[k].args[j], te);
#undef RA2_SEEN
    for (uint32_t k = s->lv_in_off[b]; k < s->lv_in_off[b + 1]; k++) {
      Ref x = s->lv_in[k];
      fu[k] = x < R->n0 ? first[x] : -1;
    }
    for (uint32_t t = 0; t < nt; t++) first[touched[t]] = -1;
    xfree(touched);
  }
  for (int iter = 0; iter < 64; iter++) {
    bool changed = false;
    for (uint32_t oi = f->norder; oi-- > 0;) {
      uint32_t b = f->order[oi];
      Block *bb = &f->blocks[b];
      for (uint32_t k = s->lv_in_off[b]; k < s->lv_in_off[b + 1]; k++) {
        int32_t d;
        if (fu[k] >= 0) d = fu[k];
        else {
          int32_t o = ra2_dout(R, b, s->lv_in[k]);
          d = o >= RA2_INF ? RA2_INF : o + (int32_t)bb->ninsts + 2;
          if (d > RA2_INF) d = RA2_INF;
        }
        if (d < R->din[k]) { R->din[k] = d; changed = true; }
      }
    }
    if (!changed) break;
  }
  xfree(fu);
  xfree(first);
  xfree(rb);
}

static int ra2_cls(Ra2 *R, Ref x) { return R->s->cls[x] ? 1 : 0; }

/* The most values of each class live at once in each block: from its end
   backwards, a use bringing its value in and a definition taking it out */
static void ra2_block_pressure(Ra2 *R) {
  IrFunc *f = R->f;
  Isel *s = R->s;
  R->maxp = xcalloc(f->nblocks ? f->nblocks : 1, sizeof(*R->maxp));
  uint8_t *live = xcalloc(R->n0 ? R->n0 : 1, 1);
  Ref *lst = xmalloc((R->n0 ? R->n0 : 1) * sizeof(Ref));
  Ref *rb = NULL;
  uint32_t caprb = 0;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t b = f->order[oi];
    Block *bb = &f->blocks[b];
    int cnt[2] = { 0, 0 }, mx[2] = { 0, 0 };
    uint32_t nl = 0;
#define RA2_LIVE(x) do { Ref x_ = (x); if (x_ < R->n0 && (R->need[x_] || R->res[x_]) && !live[x_]) { \
      live[x_] = 1; lst[nl++] = x_; cnt[ra2_cls(R, x_)]++; } } while (0)
    for (uint32_t k = s->lv_out_off[b]; k < s->lv_out_off[b + 1]; k++) RA2_LIVE(s->lv_out[k]);
    if (bb->term == T_BRIF || bb->term == T_BRTABLE) RA2_LIVE(bb->term_arg);
    for (uint32_t i = 0; i < bb->nret; i++) RA2_LIVE(bb->ret_args[i]);
    for (uint32_t k = 0; k < bb->nsuccs; k++)
      for (uint32_t j = 0; j < bb->succs[k].nargs; j++) RA2_LIVE(bb->succs[k].args[j]);
    for (int c = 0; c < 2; c++) if (cnt[c] > mx[c]) mx[c] = cnt[c];
    for (uint32_t i = bb->ninsts; i-- > 0;) {
      Ref u = bb->insts[i];
      if (u < R->n0 && (R->need[u] || R->res[u]) && live[u]) { live[u] = 0; cnt[ra2_cls(R, u)]--; }
      uint32_t na = ra2_reads(R, u, &rb, &caprb);
      for (uint32_t j = 0; j < na; j++) RA2_LIVE(rb[j]);
      for (int c = 0; c < 2; c++) if (cnt[c] > mx[c]) mx[c] = cnt[c];
    }
#undef RA2_LIVE
    R->maxp[b][0] = mx[0];
    R->maxp[b][1] = mx[1];
    for (uint32_t k = 0; k < nl; k++) live[lst[k]] = 0;
  }
  xfree(live);
  xfree(lst);
  xfree(rb);
}

/* How often, by ra2_freqs, the loop headed by h reads each of the values
   v[0..n): its blocks those that reach a way back to h without passing it. */
static double *ra2_loop_uses(Ra2 *R, uint32_t h, const Ref *v, uint32_t n) {
  IrFunc *f = R->f;
  uint32_t nb = f->nblocks;
  double *uw = xcalloc(n ? n : 1, sizeof(double));
  if (!R->lstamp) R->lstamp = xcalloc(nb ? nb : 1, sizeof(uint32_t));
  if (!R->lstack) R->lstack = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  if (!R->vuse) { R->vuse = xcalloc(R->n0 ? R->n0 : 1, sizeof(uint32_t)); R->vlv = xcalloc(R->n0 ? R->n0 : 1, sizeof(uint32_t)); }
  /* the candidates' indices, through vlv as a stamp-tagged map */
  uint32_t us = ++R->vgen;
  for (uint32_t i = 0; i < n; i++) if (v[i] < R->n0) { R->vuse[v[i]] = us; R->vlv[v[i]] = i; }
  uint32_t st = ++R->lgen;
  R->lstamp[h] = st;
  uint32_t sp = 0;
  uint32_t *lb = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  uint32_t nl = 0;
  lb[nl++] = h;
  Block *hb = &f->blocks[h];
  for (uint32_t p = 0; p < hb->npreds; p++) {
    uint32_t q = hb->preds[p];
    if (R->done[q] || R->lstamp[q] == st) continue;
    R->lstamp[q] = st;
    R->lstack[sp++] = q;
  }
  while (sp) {
    uint32_t x = R->lstack[--sp];
    lb[nl++] = x;
    Block *xb = &f->blocks[x];
    for (uint32_t p = 0; p < xb->npreds; p++) {
      uint32_t q = xb->preds[p];
      if (R->lstamp[q] == st || !f->blocks[q].reachable) continue;
      R->lstamp[q] = st;
      R->lstack[sp++] = q;
    }
  }
  Ref *rb = NULL;
  uint32_t caprb = 0;
#define RA2_UW(x, w) do { Ref x_ = (x); if (x_ < R->n0 && R->vuse[x_] == us) uw[R->vlv[x_]] += (w); } while (0)
  for (uint32_t k = 0; k < nl; k++) {
    Block *bb = &f->blocks[lb[k]];
    double w = R->freq[lb[k]];
    for (uint32_t i = 0; i < bb->ninsts; i++) {
      uint32_t na = ra2_reads(R, bb->insts[i], &rb, &caprb);
      for (uint32_t j = 0; j < na; j++) RA2_UW(rb[j], w);
    }
    if (bb->term == T_BRIF || bb->term == T_BRTABLE) RA2_UW(bb->term_arg, w);
    for (uint32_t e = 0; e < bb->nsuccs; e++) {
      /* an argument on a way round is read there, one on a way out
         after the loop */
      if (R->lstamp[bb->succs[e].target] != st) continue;
      for (uint32_t j = 0; j < bb->succs[e].nargs; j++) RA2_UW(bb->succs[e].args[j], w);
    }
  }
#undef RA2_UW
  xfree(rb);
  xfree(lb);
  return uw;
}

/* The most values live at once in the loop headed by h: its blocks, those
   that reach a way back to h without passing h.  With pm, each of its
   blocks' own most, into *pm (2 a block, by class) for the blocks in *plb,
   *pnl of them, h the first -- both the caller's to free. */
static void ra2_loop_pressure(Ra2 *R, uint32_t h, int out[2], int **pm, uint32_t **plb, uint32_t *pnl) {
  IrFunc *f = R->f;
  Isel *s = R->s;
  uint32_t nb = f->nblocks;
  if (!R->lstamp) R->lstamp = xcalloc(nb ? nb : 1, sizeof(uint32_t));
  if (!R->lstack) R->lstack = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  uint32_t st = ++R->lgen;
  R->lstamp[h] = st;
  uint32_t sp = 0, nl = 0;
  /* the loop's blocks, in lstack[0..nl) once gathered */
  uint32_t *lb = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  lb[nl++] = h;
  Block *hb = &f->blocks[h];
  for (uint32_t p = 0; p < hb->npreds; p++) {
    uint32_t q = hb->preds[p];
    if (R->done[q] || R->lstamp[q] == st) continue;
    R->lstamp[q] = st;
    R->lstack[sp++] = q;
  }
  while (sp) {
    uint32_t x = R->lstack[--sp];
    lb[nl++] = x;
    Block *xb = &f->blocks[x];
    for (uint32_t p = 0; p < xb->npreds; p++) {
      uint32_t q = xb->preds[p];
      if (R->lstamp[q] == st || !f->blocks[q].reachable) continue;
      R->lstamp[q] = st;
      R->lstack[sp++] = q;
    }
  }
  if (warmc_getenv("WARMC_RA2_OLD_LP")) {
    out[0] = out[1] = 0;
    for (uint32_t k = 0; k < nl; k++)
      for (int c = 0; c < 2; c++) if (R->maxp[lb[k]][c] > out[c]) out[c] = R->maxp[lb[k]][c];
    if (pm) {
      *pm = xmalloc((size_t)nl * 2 * sizeof(int));
      for (uint32_t k = 0; k < nl; k++)
        for (int c = 0; c < 2; c++) (*pm)[2 * k + c] = R->maxp[lb[k]][c];
      *plb = lb;
      *pnl = nl;
    } else {
      xfree(lb);
    }
    return;
  }
  /* The most values the loop reads live at once in it: a value live
     through it and not read there is the carried value the header decides
     on, in its register or in its slot -- counted, the values a big
     function keeps in slots across a small loop left it no room for any
     (gobench/strswitch's strings across its byte compare, stored on every
     way round). */
  if (!R->vuse) { R->vuse = xcalloc(R->n0 ? R->n0 : 1, sizeof(uint32_t)); R->vlv = xcalloc(R->n0 ? R->n0 : 1, sizeof(uint32_t)); }
  uint32_t us = ++R->vgen;
  Ref *rb = NULL;
  uint32_t caprb = 0;
#define RA2_LU(x) do { Ref x_ = (x); if (x_ < R->n0 && (R->need[x_] || R->res[x_])) R->vuse[x_] = us; } while (0)
  for (uint32_t k = 0; k < nl; k++) {
    Block *bb = &f->blocks[lb[k]];
    for (uint32_t i = 0; i < bb->ninsts; i++) {
      uint32_t na = ra2_reads(R, bb->insts[i], &rb, &caprb);
      for (uint32_t j = 0; j < na; j++) RA2_LU(rb[j]);
    }
    if (bb->term == T_BRIF || bb->term == T_BRTABLE) RA2_LU(bb->term_arg);
    for (uint32_t i = 0; i < bb->nret; i++) RA2_LU(bb->ret_args[i]);
    for (uint32_t e = 0; e < bb->nsuccs; e++)
      for (uint32_t j = 0; j < bb->succs[e].nargs; j++) RA2_LU(bb->succs[e].args[j]);
  }
#undef RA2_LU
  out[0] = out[1] = 0;
  bool diefirst = !warmc_getenv("WARMC_RA2_NO_DIEFIRST");
  int *bm = pm ? xmalloc((size_t)nl * 2 * sizeof(int)) : NULL;
  for (uint32_t k = 0; k < nl; k++) {
    uint32_t b = lb[k];
    Block *bb = &f->blocks[b];
    uint32_t ls = ++R->vgen;
    int cnt[2] = { 0, 0 };
    /* the running most, as of the loop's blocks before b */
    int o0[2] = { out[0], out[1] };
    out[0] = out[1] = 0;
#define RA2_LV(x) do { Ref x_ = (x); if (x_ < R->n0 && R->vuse[x_] == us && R->vlv[x_] != ls) { \
      R->vlv[x_] = ls; cnt[ra2_cls(R, x_)]++; } } while (0)
    for (uint32_t q = s->lv_out_off[b]; q < s->lv_out_off[b + 1]; q++) RA2_LV(s->lv_out[q]);
    if (bb->term == T_BRIF || bb->term == T_BRTABLE) RA2_LV(bb->term_arg);
    for (uint32_t i = 0; i < bb->nret; i++) RA2_LV(bb->ret_args[i]);
    for (uint32_t e = 0; e < bb->nsuccs; e++)
      for (uint32_t j = 0; j < bb->succs[e].nargs; j++) RA2_LV(bb->succs[e].args[j]);
    for (int c = 0; c < 2; c++) if (cnt[c] > out[c]) out[c] = cnt[c];
    for (uint32_t i = bb->ninsts; i-- > 0;) {
      Ref u = bb->insts[i];
      if (u < R->n0 && R->vlv[u] == ls) { R->vlv[u] = 0; cnt[ra2_cls(R, u)]--; }
      uint32_t na = ra2_reads(R, u, &rb, &caprb);
      /* a result made while its operands are held -- in the register of
         one read for the last time here, but the one it reads after
         writing (ra2_block) */
      bool reuse = false;
      bool want = u < R->n0 && (R->need[u] || R->res[u]);
      Ref lr = want && diefirst ? ra2a_late_arg(s, u) : REF_NONE;
      for (uint32_t j = 0; j < na; j++) {
        Ref y = rb[j];
        if (want && diefirst && y < R->n0 && R->vuse[y] == us && R->vlv[y] != ls && y != lr &&
            ra2_cls(R, y) == ra2_cls(R, u))
          reuse = true;
        RA2_LV(y);
      }
      if (want) {
        int c = ra2_cls(R, u);
        if (cnt[c] + !reuse > out[c]) out[c] = cnt[c] + !reuse;
      }
      for (int c = 0; c < 2; c++) if (cnt[c] > out[c]) out[c] = cnt[c];
    }
#undef RA2_LV
    for (int c = 0; c < 2; c++) {
      if (bm) bm[2 * k + c] = out[c];
      if (o0[c] > out[c]) out[c] = o0[c];
    }
  }
  xfree(rb);
  if (pm) {
    *pm = bm;
    *plb = lb;
    *pnl = nl;
  } else {
    xfree(lb);
  }
}

/* How many carried values the loop headed by h keeps registers for, of
   class c, fixed[c] registers held throughout -- by what one more costs
   against what it saves, as ra2_freqs counts them: kept, it is not loaded
   after the loop, as often as the loop is entered; pushed out inside, it is
   loaded back on each way round past where it was pushed out -- at a block
   of the loop's own, as often as that block runs, and in a loop within,
   whose header pushes out what it carries itself (the carried values read
   farthest on first), as often as that loop is entered.  By the fullest
   point alone, cbench/varargs' vpack loop, fullest in its strings' loop,
   which the loop enters for one argument in four, loaded two values back
   after it on every call (WARMC_RA2_NO_ROOMCOST).  pm, lb and nl are
   ra2_loop_pressure's. */
static int ra2_carried_room(Ra2 *R, uint32_t h, const int *pm, const uint32_t *lb, uint32_t nl, int c, int fixed) {
  IrFunc *f = R->f;
  uint32_t nb = f->nblocks;
  uint32_t d = f->blocks[h].loop_depth;
  double save = R->freq[h] / 10;
  int K = R->K[c];
  if (!(save > 0)) {
    int lp = 0;
    for (uint32_t k = 0; k < nl; k++) if (pm[2 * k + c] > lp) lp = pm[2 * k + c];
    return K - lp - fixed > 0 ? K - lp - fixed : 0;
  }
  /* each block's place in lb, plus one; the loops within: their headers,
     and of each block the one it is in, plus one (none: the loop's own) */
  uint32_t *pos = xcalloc(nb ? nb : 1, sizeof(uint32_t));
  for (uint32_t k = 0; k < nl; k++) pos[lb[k]] = k + 1;
  uint32_t *in = xcalloc(nl ? nl : 1, sizeof(uint32_t));
  uint32_t *hd = xmalloc((nl ? nl : 1) * sizeof(uint32_t));
  uint32_t *stk = xmalloc((nl ? nl : 1) * sizeof(uint32_t));
  uint32_t nh = 0;
  for (uint32_t k = 1; k < nl; k++) {
    uint32_t x = lb[k];
    Block *xb = &f->blocks[x];
    if (xb->loop_depth != d + 1 || in[k]) continue;
    uint32_t sp = 0;
    bool back = false;
    for (uint32_t p = 0; p < xb->npreds; p++) {
      uint32_t q = xb->preds[p];
      if (!pos[q] || f->blocks[q].rpo < xb->rpo) continue;
      back = true;
      if (q == x || in[pos[q] - 1]) continue;
      in[pos[q] - 1] = nh + 1;
      stk[sp++] = q;
    }
    if (!back) continue;
    in[k] = nh + 1;
    hd[nh++] = x;
    while (sp) {
      Block *yb = &f->blocks[stk[--sp]];
      for (uint32_t p = 0; p < yb->npreds; p++) {
        uint32_t q = yb->preds[p];
        if (!pos[q] || in[pos[q] - 1]) continue;
        in[pos[q] - 1] = nh;
        stk[sp++] = q;
      }
    }
  }
  int *hp = xcalloc(nh ? nh : 1, sizeof(int));
  for (uint32_t k = 0; k < nl; k++)
    if (in[k] && pm[2 * k + c] > hp[in[k] - 1]) hp[in[k] - 1] = pm[2 * k + c];
  int room = 0;
  while (room < 64) {
    int j = room + 1;
    double cost = 0;
    for (uint32_t k = 0; k < nl; k++)
      if (!in[k] && pm[2 * k + c] + fixed + j > K) cost += R->freq[lb[k]];
    for (uint32_t i = 0; i < nh; i++)
      if (hp[i] + fixed + j > K) cost += R->freq[hd[i]] / 10;
    if (cost >= save) break;
    room = j;
  }
  xfree(pos); xfree(in); xfree(hd); xfree(stk); xfree(hp);
  return room;
}

static Ra2W *ra2_exit_find(Ra2 *R, uint32_t b, Ref x) {
  if (!R->done[b]) return NULL;
  for (uint32_t k = 0; k < R->xn[b]; k++)
    if (R->xw[R->xoff[b] + k].v == x) return &R->xw[R->xoff[b] + k];
  return NULL;
}

/* A value the loop headed by h only carries, with no room: stored on the
   one way into the loop, from the register it holds there, where that
   runs less often than the value is made -- at its definition, the store
   would run on every way round of the loop it is made in: gobench/
   unicode's swapCase stored two on every rune for its search loop, which
   only the runes past Latin-1 enter.  The reads below find the copy
   (ra2_mem_find); false where it is not made (WARMC_RA2_NO_ENTRYSPILL). */
static bool ra2_spill_entry(Ra2 *R, uint32_t h, Ref x) {
  IrFunc *f = R->f;
  if (!R->freq || R->rem[x] || R->res[x] || R->mem[x] != REF_NONE || warmc_getenv("WARMC_RA2_NO_ENTRYSPILL"))
    return false;
  Block *hb = &f->blocks[h];
  uint32_t q = UINT32_MAX;
  for (uint32_t p = 0; p < hb->npreds; p++) {
    uint32_t pb = hb->preds[p];
    if (!R->done[pb]) continue;
    if (q != UINT32_MAX && q != pb) return false;
    q = pb;
  }
  if (q == UINT32_MAX) return false;
  uint32_t db = f->insts[x].block;
  if (!(R->freq[q] < R->freq[db])) return false;
  if (ra2_mem_find(R, x, q) != REF_NONE) return true;
  Ra2W *w = ra2_exit_find(R, q, x);
  if (!w) return false;
  Ref c = ra2_new(R, q, I_COPY, (ValType)f->insts[x].type, &w->ver, 1, 0, 0);
  R->slotv[c] = 1;
  ra2_mem_note(R, x, c, q);
  ra2_insert(f, q, f->blocks[q].ninsts, c);
  R->nspill++;
  return true;
}

/* the argument an edge of block p passes for x, by p's registers at its end */
static Ref ra2_edge_arg(Ra2 *R, uint32_t p, Ref x) {
  Ra2W *w = ra2_exit_find(R, p, x);
  if (w) return w->ver;
  if (R->rem[x]) {
    /* made again at the end of p: the value itself would hold a register
       from its definition on */
    IrFunc *f = R->f;
    Inst xc = f->insts[x];
    Ref r = ra2_new(R, p, (IrOp)xc.op, (ValType)xc.type, NULL, 0, xc.imm, xc.imm2);
    if (p == R->cur_b) ra2_out(R, r);
    else ra2_insert(f, p, f->blocks[p].ninsts, r);
    R->nremat++;
    /* read by the edge alone: its move makes it in its target's place, in
       no register of its own (ra2_assign) */
    R->slotv[r] = 2;
    return r;
  }
  return ra2_mem_for(R, x, p);
}

static void ra2_push_arg(IrFunc *f, Edge *e, Ref a) {
  if (e->nargs == e->cap) {
    uint32_t oc = e->cap;
    e->cap = e->cap ? e->cap * 2 : 4;
    e->args = ir_grow(f, e->args, (size_t)oc * sizeof(Ref), (size_t)e->cap * sizeof(Ref));
  }
  e->args[e->nargs++] = a;
}

typedef struct { Ra2W w[2][64]; int n[2]; } Ra2Set;

static int ra2_find(const Ra2Set *W, int c, Ref x) {
  for (int k = 0; k < W->n[c]; k++)
    if (W->w[c][k].v == x) return k;
  return -1;
}

static void ra2_drop(Ra2Set *W, int c, int k) { W->w[c][k] = W->w[c][--W->n[c]]; }

/* x out of the registers: its copy in a slot made where it is to be read
   again -- or, for a resident, which no copy may stand in for, a slot for
   all of its life, every read reading it there */
/* Whether the block being rewritten runs less often than x's own, by the
   loops: outside the loop x is made in, or within it on a way not every
   trip takes -- one that does not dominate each way back to its header.
   Elsewhere one store where x is made serves every way that wants it,
   where a store wherever it goes would be one per way, or one per trip. */
static bool ra2_rarer(Ra2 *R, Ref x) {
  IrFunc *f = R->f;
  uint32_t db = f->insts[x].block, b = R->cur_b;
  uint32_t dd = f->blocks[db].loop_depth, bd = f->blocks[b].loop_depth;
  if (bd < dd) return true;
  if (bd > dd || b == db || dd == 0) return false;
  /* the header of x's loop: the nearest block above x's of its depth that a
     way back enters */
  uint32_t h = REF_NONE;
  for (uint32_t d = db, k = 0; d != REF_NONE && d < f->nblocks && k < f->nblocks; k++) {
    const Block *hb = &f->blocks[d];
    if (hb->loop_depth < dd) break;
    for (uint32_t p = 0; p < hb->npreds && h == REF_NONE; p++)
      if (f->blocks[hb->preds[p]].rpo >= hb->rpo) h = d;
    if (h != REF_NONE || d == f->entry || hb->idom == d) break;
    d = hb->idom;
  }
  if (h == REF_NONE || !ra2_dom(f, h, b)) return false;
  const Block *hb = &f->blocks[h];
  for (uint32_t p = 0; p < hb->npreds; p++) {
    uint32_t q = hb->preds[p];
    if (f->blocks[q].rpo >= hb->rpo && !ra2_dom(f, b, q)) return true;
  }
  return false;
}

/* Its copy for the slot is made where it goes, of the version the
   registers hold -- before_last: ahead of the call just put out -- so that
   a value only a rare way pushes out is stored on that way, not wherever
   it is made (asbench/instof's sum, stored on every way round for a call
   on its growth path); a copy above here does already. */
static void ra2_evict_at(Ra2 *R, Ref x, Ref ver, int32_t nu, bool before_last) {
  if (R->res[x]) {
    R->res[x] = 0;
    R->slotv[x] = 1;
    R->nwhole++;
    return;
  }
  if (nu >= RA2_INF || R->rem[x]) return;
  if (ra2_mem_find(R, x, R->cur_b) != REF_NONE || R->mem[x] == x) return;
  if (ver == REF_NONE || warmc_getenv("WARMC_RA2_NO_LOCALSPILL") || !ra2_rarer(R, x)) { ra2_mem(R, x); return; }
  IrFunc *f = R->f;
  Ref c = ra2_new(R, R->cur_b, I_COPY, (ValType)f->insts[x].type, &ver, 1, 0, 0);
  R->slotv[c] = 1;
  ra2_mem_note(R, x, c, R->cur_b);
  R->nspill++;
  ra2_out(R, c);
  if (before_last && R->nout >= 2) {
    Ref last = R->out[R->nout - 2];
    R->out[R->nout - 2] = c;
    R->out[R->nout - 1] = last;
    if (last < R->n0) R->outpos[last] = (int32_t)R->nout - 1;
  }
}


/* How much further ahead than it is a value made again where it is out of
   the registers counts, for which goes: the constant costs its making at
   the next read, where another value costs a store once and a load there.
   Taken in at a block's start by the same measure.  Tracked as values,
   one-instruction constants had pushed asbench/arrpush's push loop's
   length into a slot, stored on every push. */
static int64_t ra2_remw(Ra2 *R, Ref x) {
  if (!R->rem[x]) return 1;
  static int w = -1;
  if (w < 0) { const char *e = getenv("WARMC_RA2_REMW"); w = e ? atoi(e) : 1; if (w < 1) w = 1; }
  return w;
}

/* Room for need more values of class c: the ones used furthest ahead go,
   a value made again sooner than one that would need a reload, and none
   that this instruction reads (keep). */
/* A resident pushed out goes to its slot whole, every read of it a load
   (ra2_evict_at): it goes last, where nothing else can -- taken for the
   farthest read at a call on memory.grow's way, rsbench/bintree's malloc
   loaded its state's address fifty times over (WARMC_RA2_RES_EVICT) */
static bool ra2_res_last(Ra2 *R, Ref x) {
  return R->res[x] && !warmc_getenv("WARMC_RA2_RES_EVICT");
}

/* Whether x pushed out here needs no store here: it is in a slot already,
   or its copy for one is made where x is made, which runs less often than
   here -- one load to have it back, where a value made on the way round
   costs a store and a load on every trip (gobench/unicode's swapCase:
   the invariants its call takes held, the values each rune moves stored
   and loaded round the call) */
static bool ra2_clean(Ra2 *R, Ref x) {
  IrFunc *f = R->f;
  if (R->mem[x] != REF_NONE || ra2_mem_find(R, x, R->cur_b) != REF_NONE) return true;
  return R->freq && R->freq[f->insts[x].block] < R->freq[R->cur_b];
}

static void ra2_room(Ra2 *R, Ra2Set *W, int c, int need, const Ref *keep, uint32_t nkeep) {
  while (W->n[c] + need > R->K[c]) {
    int vic = -1;
    int64_t best = -1;
    bool bres = true;
    for (int k = 0; k < W->n[c]; k++) {
      Ref x = W->w[c][k].v;
      bool kept = false;
      for (uint32_t j = 0; j < nkeep && !kept; j++) kept = keep[j] == x;
      if (kept) continue;
      int64_t key = (int64_t)W->w[c][k].nu * 2 * ra2_remw(R, x) + (R->rem[x] ? 1 : 0);
      /* one that needs no store to go (ra2_clean) as if read twice as far
         on: a load to have it back, where another costs a store and a load
         (gobench/strbuild, 20% faster; WARMC_RA2_NO_CLEANROOM) */
      if (!R->rem[x] && ra2_clean(R, x) && !warmc_getenv("WARMC_RA2_NO_CLEANROOM")) key *= 2;
      bool res = ra2_res_last(R, x);
      if (vic < 0 || (bres && !res) || (res == bres && key > best)) { best = key; vic = k; bres = res; }
    }
    if (vic < 0) return;
    Ref x = W->w[c][vic].v;
    ra2_evict_at(R, x, W->w[c][vic].ver, W->w[c][vic].nu, false);
    ra2_drop(W, c, vic);
  }
}

static bool ra2_hot_call(Ra2 *R, uint32_t b, Ref u) {
  IrFunc *f = R->f;
  if (!inst_clobbers_caller_saved(f, u)) return false;
  if (f->blocks[b].cold) return false;
  uint64_t ft;
  int64_t fl;
  if (gc_fastnew_shape(f, u, &ft, &fl)) return false;
  return true;
}

static bool ra2_is_call(uint16_t op) {
  return op == I_CALL || op == I_CALL_INDIRECT || op == I_CALL_REF || op == I_RETURN_CALL ||
         op == I_RETURN_CALL_INDIRECT || op == I_RETURN_CALL_REF;
}

/* x's next use after position p of the block being rewritten: its uses'
   positions are in pos (np of them, increasing) */
typedef struct { int32_t *pos; uint32_t n, cap; } Ra2Uses;

static int32_t ra2_next(Ra2 *R, uint32_t b, const Ra2Uses *u, Ref x, int32_t p, int32_t blen) {
  if (u) {
    uint32_t lo = 0, hi = u->n;
    while (lo < hi) {
      uint32_t mid = (lo + hi) / 2;
      if (u->pos[mid] <= p) lo = mid + 1;
      else hi = mid;
    }
    if (lo < u->n) return u->pos[lo];
  }
  int32_t o = ra2_dout(R, b, x);
  return o >= RA2_INF ? RA2_INF : blen + 1 + o;
}


/* One instruction's operands as the walk takes them in: its block's
   registers, where it is, what its reads keep, and the versions it reads */
typedef struct {
  Ra2Set *W;
  Ra2Uses *uses;
  uint32_t b;
  int32_t p, blen, ncall;
  bool call;
  const Ref *keep;
  uint32_t nkeep;
  Ref *rd;
  uint32_t nrd, caprd;
} Ra2Op;

static void ra2_note_read(Ra2Op *o, Ref v) {
  if (o->nrd == o->caprd) {
    o->caprd = o->caprd ? o->caprd * 2 : 8;
    o->rd = xrealloc(o->rd, o->caprd * sizeof(Ref));
  }
  o->rd[o->nrd++] = v;
}

/* what an instruction reads, through the folded instructions several read
   (each reader reads their operands) */
static void ra2_keep_add(Ra2 *R, Ref u, Ref **kp, uint32_t *nk, uint32_t *ck, Ref *inl) {
  IrFunc *f = R->f;
  for (uint32_t j = 0; j < f->insts[u].nargs; j++) {
    Ref y = ir_args(f, u)[j];
    if (y < R->n0 && R->shared[y]) { ra2_keep_add(R, y, kp, nk, ck, inl); continue; }
    if (*nk == *ck) {
      *ck *= 2;
      if (*kp == inl) { Ref *n = xmalloc(*ck * sizeof(Ref)); memcpy(n, inl, *nk * sizeof(Ref)); *kp = n; }
      else *kp = xrealloc(*kp, *ck * sizeof(Ref));
    }
    (*kp)[(*nk)++] = y;
  }
}

static Ref ra2_operand(Ra2 *R, Ra2Op *o, Ref user, uint32_t j, Ref x);

/* A folded instruction several read, as this reader reads it: itself where
   its operands are the versions the reader has, else a copy of it made
   right here of those -- which folds into this reader alone, as the
   selection folds it into each (analyse_immediates) */
static Ref ra2_unshare(Ra2 *R, Ra2Op *o, Ref x) {
  IrFunc *f = R->f;
  uint32_t na = f->insts[x].nargs;
  Ref buf[8];
  Ref *nv = na <= 8 ? buf : xmalloc(na * sizeof(Ref));
  bool same = true;
  for (uint32_t j = 0; j < na; j++) {
    Ref y = ir_args(f, x)[j];
    nv[j] = ra2_operand(R, o, x, j, y);
    if (nv[j] != y) same = false;
  }
  Ref r = x;
  if (!same) {
    Inst xc = f->insts[x];
    r = ra2_new(R, o->b, (IrOp)xc.op, (ValType)xc.type, nv, na, xc.imm, xc.imm2);
    ra2_out(R, r);
  }
  if (nv != buf) xfree(nv);
  return r;
}

/* Operand j of user, x, as the user reads it: the version in a register, a
   constant made again, a reload, or the copy in a slot */
static Ref ra2_operand(Ra2 *R, Ra2Op *o, Ref user, uint32_t j, Ref x) {
  IrFunc *f = R->f;
  if (x < R->n0 && R->shared[x]) return ra2_unshare(R, o, x);
  if (x >= R->n0 || !R->need[x]) return x;
  if (ra2_imm_use(R, user, j, x)) {
    Inst xc = f->insts[x];
    Ref r = ra2_new(R, o->b, (IrOp)xc.op, (ValType)xc.type, NULL, 0, xc.imm, xc.imm2);
    ra2_out(R, r);
    return r;
  }
  Ra2Set *W = o->W;
  int c = ra2_cls(R, x);
  int k = ra2_find(W, c, x);
  if (k >= 0) {
    /* A constant held as a block's param is no constant to the selection,
       whose forms test the operand's instruction (a call to the allocator
       for an array of constant length): one that is one instruction to make
       is made again here for a reader that takes its operands by moves --
       a call, an allocation -- which costs that instruction anyway; the
       value the register holds from here on. */
    Ref ver = W->w[c][k].ver;
    Inst *xi = &f->insts[x];
    uint16_t uop = f->insts[user].op;
    bool moved = uop == I_CALL || uop == I_CALL_INDIRECT || uop == I_CALL_REF || uop == I_RETURN_CALL ||
                 uop == I_RETURN_CALL_INDIRECT || uop == I_RETURN_CALL_REF || uop == I_MEM_GROW ||
                 uop == I_MEM_COPY || uop == I_MEM_FILL || uop == I_MEM_INIT || uop == I_TABLE_GET ||
                 uop == I_TABLE_SET || uop == I_TABLE_GROW || uop == I_TABLE_FILL || uop == I_TABLE_COPY ||
                 uop == I_TABLE_INIT || uop == I_STRUCT_NEW || uop == I_ARRAY_NEW || uop == I_ARRAY_NEW_SEG ||
                 uop == I_ARRAY_OP || uop == I_REF_TEST || uop == I_ANY_OF_EXTERN || uop == I_EXTERN_OF_ANY;
    if (moved && R->rem[x] && xi->op == I_CONST && f->insts[ver].op != I_CONST && !vt_is_float((ValType)xi->type) &&
        (ValType)xi->type != VT_V128 && ir_const_cost(xi->imm, vt_is_64((ValType)xi->type)) <= 1 &&
        !warmc_getenv("WARMC_RA2_NO_CONSTUSE")) {
      Inst xc = *xi;
      Ref r = ra2_new(R, o->b, (IrOp)xc.op, (ValType)xc.type, NULL, 0, xc.imm, xc.imm2);
      ra2_out(R, r);
      R->slotv[r] = 3;
      W->w[c][k].ver = r;
      ver = r;
    }
    ra2_note_read(o, ver);
    return ver;
  }
  int32_t nu = ra2_next(R, o->b, &o->uses[x], x, o->p, o->blen);
  if (R->rem[x]) {
    ra2_room(R, W, c, 1, o->keep, o->nkeep);
    Inst xc = f->insts[x];
    Ref r = ra2_new(R, o->b, (IrOp)xc.op, (ValType)xc.type, NULL, 0, xc.imm, xc.imm2);
    ra2_out(R, r);
    R->nremat++;
    R->slotv[r] = 3;
    if (nu < RA2_INF && W->n[c] < R->K[c] && W->n[c] < 64) W->w[c][W->n[c]++] = (Ra2W){ x, r, nu };
    ra2_note_read(o, r);
    return r;
  }
  Ref m = ra2_mem(R, x);
  /* loaded into a register where it is read again before the next call
     that would take it (or the block's end) -- the load then serves both
     -- or soon */
  bool again = nu - o->p < RA2_REUSE || (nu <= o->blen && nu < o->ncall);
  if (!o->call && again) {
    ra2_room(R, W, c, 1, o->keep, o->nkeep);
    Ref r = ra2_new(R, o->b, I_COPY, (ValType)f->insts[x].type, &m, 1, 0, 0);
    ra2_out(R, r);
    R->nreload++;
    if (W->n[c] < R->K[c] && W->n[c] < 64) W->w[c][W->n[c]++] = (Ra2W){ x, r, nu };
    ra2_note_read(o, r);
    return r;
  }
  return m;
}

static void ra2_block(Ra2 *R, uint32_t b, Ra2Uses *uses, Ref *touched, uint32_t *ntouched) {
  IrFunc *f = R->f;
  Isel *s = R->s;
  bool diefirst = !warmc_getenv("WARMC_RA2_NO_DIEFIRST");
  Block *bb = &f->blocks[b];
  uint32_t nb = bb->ninsts;
  int32_t blen = (int32_t)nb + 1;
  Ref *orig = xmalloc((nb ? nb : 1) * sizeof(Ref));
  memcpy(orig, bb->insts, nb * sizeof(Ref));

  /* the uses' positions in this block, per value */
  *ntouched = 0;
#define RA2_USE(x, p) do { Ref x_ = (x); if (x_ < R->n0 && (R->need[x_] || R->res[x_])) { Ra2Uses *U = &uses[x_]; \
      if (!U->n) touched[(*ntouched)++] = x_; \
      if (U->n == U->cap) { U->cap = U->cap ? U->cap * 2 : 4; U->pos = xrealloc(U->pos, U->cap * sizeof(int32_t)); } \
      U->pos[U->n++] = (p); } } while (0)
  Ref *rb = NULL;
  uint32_t caprb = 0;
  for (uint32_t i = 0; i < nb; i++) {
    uint32_t na = ra2_reads(R, orig[i], &rb, &caprb);
    for (uint32_t j = 0; j < na; j++) RA2_USE(rb[j], (int32_t)i + 1);
  }
  xfree(rb);
  if (bb->term == T_BRIF || bb->term == T_BRTABLE) RA2_USE(bb->term_arg, blen);
  for (uint32_t i = 0; i < bb->nret; i++) RA2_USE(bb->ret_args[i], blen);
  for (uint32_t k = 0; k < bb->nsuccs; k++)
    for (uint32_t j = 0; j < bb->succs[k].nargs; j++) RA2_USE(bb->succs[k].args[j], blen);
#undef RA2_USE

  /* each position's next call that clobbers the caller-saved registers */
  int32_t *ncall = xmalloc(((size_t)nb + 1) * sizeof(int32_t));
  {
    int32_t nx = RA2_INF;
    ncall[nb] = nx;
    for (uint32_t i = nb; i-- > 0;) {
      if (ra2_hot_call(R, b, orig[i])) nx = (int32_t)i + 1;
      ncall[i] = nx;
    }
  }

  Ra2Set W;
  W.n[0] = W.n[1] = 0;
  R->cur_b = b;
  R->nout = 0;

  /* the registers on entry: the live-ins and the params by their next
     uses, a value the ways in already hold in a register ahead of one
     they do not on a tie */
  {
    uint32_t nl = s->lv_in_off[b + 1] - s->lv_in_off[b];
    uint32_t nc = nl + bb->nparams;
    Ref *cv = xmalloc((nc ? nc : 1) * sizeof(Ref));
    int64_t *ck = xmalloc((nc ? nc : 1) * sizeof(int64_t));
    uint32_t m = 0;
    bool entry = b == f->entry;
    uint32_t nproc = 0;
    for (uint32_t p = 0; p < bb->npreds; p++) nproc += R->done[bb->preds[p]];
    /* On a loop's header, which of the values live in the loop reads: one
       read after a loop within it is as far by the distances as one read
       after the loop itself (a way out of a loop adds RA2_LOOPX), and would
       be taken for one the loop only carries -- stdgo/variadic's two
       constants and an address, read after its inner loop, made again and
       loaded back on every trip with five registers free
       (WARMC_RA2_NO_INLOOP) */
    /* the loop's pressure (ra2_loop_pressure), for the room below too --
       -- and where it is more than the registers hold, a value read only
       past a loop within is carried still: taken in, it would be pushed
       out inside and loaded back on every way round (cbench/varargs) */
    int hlp[2] = { 0, 0 };
    int *pm = NULL;
    uint32_t *plb = NULL, pnl = 0;
    if (nproc < bb->npreds)
      ra2_loop_pressure(R, b, hlp, R->freq && !warmc_getenv("WARMC_RA2_NO_ROOMCOST") ? &pm : NULL, &plb, &pnl);
    /* -- nor in a loop that makes a call, across which no more than the
       callee-saved registers keep theirs (hsbench/lazy, typeclass) */
    bool hfits[2] = { false, false };
    if (plb) {
      bool lcall = false;
      for (uint32_t k = 0; k < pnl && !lcall; k++) {
        Block *lbk = &f->blocks[plb[k]];
        for (uint32_t i = 0; i < lbk->ninsts && !lcall; i++) lcall = ra2_hot_call(R, plb[k], lbk->insts[i]);
      }
      for (int c = 0; c < 2; c++) hfits[c] = hlp[c] <= R->K[c] && !lcall;
    }
    double *lrd = NULL;
    if (nproc < bb->npreds && R->freq && !warmc_getenv("WARMC_RA2_NO_INLOOP")) {
      Ref *all = xmalloc((nc ? nc : 1) * sizeof(Ref));
      for (uint32_t k = 0; k < nc; k++) all[k] = k < nl ? s->lv_in[s->lv_in_off[b] + k] : bb->params[k - nl];
      lrd = ra2_loop_uses(R, b, all, nc);
      xfree(all);
    }
    bool *cr = xmalloc((nc ? nc : 1) * sizeof(bool));
    /* the residents live in, ahead of everything */
    for (uint32_t k = 0; k < nl + bb->nparams; k++) {
      Ref x = k < nl ? s->lv_in[s->lv_in_off[b] + k] : bb->params[k - nl];
      if (x >= R->n0 || !R->res[x]) continue;
      int32_t nu = ra2_next(R, b, uses[x].n ? &uses[x] : NULL, x, 0, blen);
      int c = ra2_cls(R, x);
      /* one the loop does not read is carried: past RA2_LOOPX */
      if (lrd && nu < RA2_INF) {
        if (lrd[k] == 0) { if (nu < RA2_LOOPX) nu = RA2_LOOPX; }
        else if (nu >= RA2_LOOPX && hfits[c]) nu = RA2_LOOPX - 1;
      }
      if (nu < RA2_INF && W.n[c] < 64) W.w[c][W.n[c]++] = (Ra2W){ x, x, nu };
    }
    for (uint32_t k = 0; k < nl + bb->nparams; k++) {
      Ref x = k < nl ? s->lv_in[s->lv_in_off[b] + k] : bb->params[k - nl];
      if (x >= R->n0 || !R->need[x]) continue;
      if (entry && k < nl) continue;
      int32_t nu = ra2_next(R, b, uses[x].n ? &uses[x] : NULL, x, 0, blen);
      if (nu >= RA2_INF) {
        /* a param never read: no register, nor slot */
        continue;
      }
      uint32_t held = 0;
      for (uint32_t p = 0; p < bb->npreds; p++) held += ra2_exit_find(R, bb->preds[p], x) != NULL;
      /* a value no way in holds is loaded where it is read, not on the way
         in -- but for one a loop reads, loaded on the way into the loop
         rather than on every way round */
      bool own = f->insts[x].op == I_PARAM && f->insts[x].block == b;
      bool header = nproc < bb->npreds;
      bool carried = lrd ? lrd[k] == 0 || (nu >= RA2_LOOPX && !hfits[ra2_cls(R, x)]) : nu >= RA2_LOOPX;
      if (!own && nproc && !held && !(header && !carried) &&
          !warmc_getenv("WARMC_RA2_ENTRY_ALL"))
        continue;
      int64_t key = (int64_t)(nu < RA2_LOOPX ? nu * ra2_remw(R, x) : nu) * 4 +
                    (held == nproc && nproc ? 0 : held ? 1 : 2);
      cr[m] = carried;
      cv[m] = x;
      ck[m++] = key;
    }
    xfree(lrd);
    /* by key, a few dozen at most: an insertion sort -- on a loop's header,
       the values the loop reads by how often it reads them (ra2_freqs)
       first, the nearest read only among equals: a value one arm of the
       loop's switch reads is no more wanted than one each trip reads,
       which held, had to go to a slot (cbench/switch's counter) */
    double *sk = xmalloc((m ? m : 1) * sizeof(double));
    bool byfreq = nproc < bb->npreds && R->freq && !warmc_getenv("WARMC_RA2_NO_FREQ");
    if (byfreq) {
      double *uw = ra2_loop_uses(R, b, cv, m);
      for (uint32_t i = 0; i < m; i++)
        sk[i] = cr[i] ? 1e30 + (double)ck[i] : -uw[i] * 1e6 + (double)ck[i];
      xfree(uw);
    } else {
      for (uint32_t i = 0; i < m; i++) sk[i] = (double)ck[i];
    }
    for (uint32_t i = 1; i < m; i++) {
      Ref x = cv[i];
      int64_t kx = ck[i];
      double sx = sk[i];
      bool cx = cr[i];
      uint32_t j = i;
      while (j > 0 && sk[j - 1] > sx) { cv[j] = cv[j - 1]; ck[j] = ck[j - 1]; sk[j] = sk[j - 1]; cr[j] = cr[j - 1]; j--; }
      cv[j] = x;
      ck[j] = kx;
      sk[j] = sx;
      cr[j] = cx;
    }
    xfree(sk);
    /* On a loop's header, a value the loop only carries keeps a register
       only where the loop's own values leave one free at its fullest
       point: kept and then pushed out inside, it would be loaded back on
       every way round. */
    int lt_room[2] = { 64, 64 };
    bool lfull[2] = { false, false };
    if (nproc < bb->npreds) {
      int lp[2] = { hlp[0], hlp[1] }, nlt[2] = { 0, 0 };
      /* a loop that reads more than the registers hold: its constants are
         made where they are read, not carried round in params -- pushed out
         inside, they would be made again on every way back for the header
         (cbench/switch: three per trip for cases that seldom read them) */
      for (int c = 0; c < 2; c++) lfull[c] = lp[c] > R->K[c] && warmc_getenv("WARMC_RA2_HDR_NOCONSTS");
      for (uint32_t i = 0; i < m; i++)
        if (cr[i]) nlt[ra2_cls(R, cv[i])]++;
      for (int c = 0; c < 2; c++) {
        /* the loop's pressure counts what it reads only, the carried
           values none (ra2_loop_pressure) */
        lt_room[c] = warmc_getenv("WARMC_RA2_OLD_LP") ? R->K[c] - (lp[c] - nlt[c]) : R->K[c] - lp[c];
        /* -- nor the residents it carries, which hold theirs throughout
           (gobench/typeswitch: two native pointers across its switch,
           which let in two carried values too many, pushed out in every
           arm and loaded back on the way round) */
        int fixed = 0;
        if (!warmc_getenv("WARMC_RA2_NO_RESROOM"))
          for (int k = 0; k < W.n[c]; k++)
            if (W.w[c][k].nu >= RA2_LOOPX) fixed++;
        lt_room[c] -= fixed;
        if (lt_room[c] < 0) lt_room[c] = 0;
        if (pm) lt_room[c] = ra2_carried_room(R, b, pm, plb, pnl, c, fixed);
        {
          const char *de = warmc_getenv("WARMC_RA2_DEBUG_EV");
          if (de && (uint32_t)atoi(de) == f->func_index)
            fprintf(stderr, "hdr b%u c%d K=%d lp=%d nlt=%d room=%d m=%u\n", b, c, R->K[c], lp[c], nlt[c], lt_room[c], m);
        }
      }
      xfree(pm);
      xfree(plb);
    }
    for (uint32_t i = 0; i < m; i++) {
      Ref x = cv[i];
      int c = ra2_cls(R, x);
      bool param = f->insts[x].op == I_PARAM && f->insts[x].block == b;
      int32_t nu = (int32_t)(ck[i] / 4);
      bool carried = cr[i];
      if (carried && lt_room[c] <= 0 && !param) {
        if (!ra2_spill_entry(R, b, x)) ra2_mem(R, x);
        continue;
      }
      if (lfull[c] && R->rem[x] && !param) continue;
      if (carried) lt_room[c]--;
      if (W.n[c] >= R->K[c] || W.n[c] >= 64) {
        if (param) {
          /* a param with no register on entry is its own copy in a slot */
          R->mem[x] = x;
          R->slotv[x] = 1;
        } else if (!R->rem[x]) {
          ra2_mem(R, x);
        }
        continue;
      }
      Ref ver = x;
      if (!param) {
        Ra2W *one = bb->npreds == 1 ? ra2_exit_find(R, bb->preds[0], x) : NULL;
        if (one) ver = one->ver;
        else {
          ver = ir_add_param(f, b, (ValType)f->insts[x].type);
          if (ver >= R->nslotv) {
            uint32_t nc2 = R->nslotv ? R->nslotv : 64;
            while (nc2 <= ver) nc2 *= 2;
            R->slotv = xrealloc(R->slotv, nc2);
            memset(R->slotv + R->nslotv, 0, nc2 - R->nslotv);
            R->nslotv = nc2;
          }
          R->nparam++;
          if (R->nadded == R->capadded) {
            R->capadded = R->capadded ? R->capadded * 2 : 32;
            R->added = xrealloc(R->added, R->capadded * sizeof(Ref));
          }
          R->added[R->nadded++] = ver;
          /* every edge into b passes an argument for it: from a block done,
             what it ends with; from one not reached yet, filled there */
          for (uint32_t pi = 0; pi < bb->npreds; pi++) {
            uint32_t ob = bb->preds[pi];
            bool dup = false;
            for (uint32_t q = 0; q < pi && !dup; q++) dup = bb->preds[q] == ob;
            if (dup) continue;
            Block *pb = &f->blocks[ob];
            for (uint32_t si = 0; si < pb->nsuccs; si++) {
              Edge *e = &pb->succs[si];
              if (e->target != b) continue;
              if (R->done[ob]) {
                ra2_push_arg(f, e, ra2_edge_arg(R, ob, x));
              } else {
                if (R->npend == R->cappend) {
                  R->cappend = R->cappend ? R->cappend * 2 : 32;
                  R->pend = xrealloc(R->pend, R->cappend * sizeof(Ra2Pend));
                }
                R->pend[R->npend++] = (Ra2Pend){ ob, si, e->nargs, x };
                ra2_push_arg(f, e, x);
              }
            }
          }
        }
      }
      W.w[c][W.n[c]++] = (Ra2W){ x, ver, nu };
    }
    /* what is live in and in no register is in a slot */
    for (uint32_t k = 0; k < nl; k++) {
      Ref x = s->lv_in[s->lv_in_off[b] + k];
      if (x >= R->n0 || !R->need[x] || entry) continue;
      if (ra2_find(&W, ra2_cls(R, x), x) < 0 && !R->rem[x]) ra2_mem(R, x);
    }
    xfree(cv);
    xfree(ck);
    xfree(cr);
  }

  /* the instructions */
  const char *dbgw = warmc_getenv("WARMC_RA2_DEBUG_W");
  bool dw = dbgw && (uint32_t)atoi(dbgw) == f->func_index;
  if (dw) {
    fprintf(stderr, "ra2w f%u b%u entry:", f->func_index, b);
    for (int c = 0; c < 2; c++) for (int k = 0; k < W.n[c]; k++) fprintf(stderr, " v%u/v%u@%d", W.w[c][k].v, W.w[c][k].ver, W.w[c][k].nu);
    fprintf(stderr, "\n");
  }
  for (uint32_t i = 0; i < nb; i++) {
    Ref u = orig[i];
    if (dw) {
      fprintf(stderr, "ra2w   v%u %s [%d]:", u, ir_op_name(f->insts[u].op), W.n[0]);
      for (int c = 0; c < 1; c++) for (int k = 0; k < W.n[c]; k++) fprintf(stderr, " v%u/v%u@%d", W.w[c][k].v, W.w[c][k].ver, W.w[c][k].nu);
      fprintf(stderr, "\n");
    }
    Inst *in = &f->insts[u];
    int32_t p = (int32_t)i + 1;
    /* a folded instruction several read is made at each of them */
    if (u < R->n0 && R->shared[u]) {
      R->outpos[u] = (int32_t)R->nout;
      ra2_out(R, u);
      continue;
    }
    uint32_t na = in->nargs;
    bool call = ra2_is_call(in->op);
    /* the instruction's operands as they were, through what folds into it:
       what the evictions keep */
    Ref keep[16];
    Ref *kp = keep;
    uint32_t nk = 0, ck = 16;
    ra2_keep_add(R, u, &kp, &nk, &ck, keep);
    /* its operands into registers, or read from their slots */
    Ra2Op o = { &W, uses, b, p, blen, ncall[i], call, kp, nk, NULL, 0, 0 };
    for (uint32_t j = 0; j < na; j++) {
      Ref x = ir_args(f, u)[j];
      Ref r = ra2_operand(R, &o, u, j, x);
      ir_args(f, u)[j] = r;
    }
    /* the operands' next uses; the dead ones leave only once the result
       has its register, which the allocation gives no operand's */
    for (int c = 0; c < 2; c++)
      for (int k = 0; k < W.n[c]; k++) {
        Ra2W *w = &W.w[c][k];
        bool read = false;
        for (uint32_t j = 0; j < o.nrd && !read; j++) read = o.rd[j] == w->ver;
        if (read) w->nu = ra2_next(R, b, &uses[w->v], w->v, p, blen);
      }
    xfree(o.rd);
    R->outpos[u] = (int32_t)R->nout;
    ra2_out(R, u);
    if (ra2_hot_call(R, b, u)) {
      /* what lives past the call, as many as the callee-saved registers
         hold, by their next uses; no vector, whose high half no callee
         keeps */
      for (int c = 0; c < 2; c++) {
        int keep = R->Kcs[c];
        /* what dies at the call crosses nothing */
        for (int k = 0; k < W.n[c];) {
          if (W.w[c][k].nu >= RA2_INF) ra2_drop(&W, c, k);
          else k++;
        }
        for (int k = 0; k < W.n[c];) {
          Ref x = W.w[c][k].v;
          if (c == 1 && (ValType)f->insts[x].type == VT_V128) {
            if (!R->rem[x]) ra2_mem(R, x);
            ra2_drop(&W, c, k);
          } else k++;
        }
        /* what a call keeps across it in the callee-saved registers: no
           constant ahead of a value, the constant being made again after
           it for what one load and store would cost the value (asbench/
           mathf's libm, its float constants round each call) */
        bool cfirst = !warmc_getenv("WARMC_RA2_CALL_KEEPCONST");
        while (W.n[c] > keep) {
          int vic = -1;
          bool clfirst = !warmc_getenv("WARMC_RA2_NO_CLEANFIRST");
          for (int k = 0; k < W.n[c]; k++) {
            if (vic < 0) { vic = k; continue; }
            bool sk = ra2_res_last(R, W.w[c][k].v), sv = ra2_res_last(R, W.w[c][vic].v);
            if (sk != sv) { if (sv) vic = k; continue; }
            bool rk = cfirst && R->rem[W.w[c][k].v], rv = cfirst && R->rem[W.w[c][vic].v];
            if (rk != rv) { if (rk) vic = k; continue; }
            bool ck = clfirst && ra2_clean(R, W.w[c][k].v), cv2 = clfirst && ra2_clean(R, W.w[c][vic].v);
            if (ck != cv2 ? ck : W.w[c][k].nu > W.w[c][vic].nu) vic = k;
          }
          if (vic < 0) break;
          ra2_evict_at(R, W.w[c][vic].v, W.w[c][vic].ver, W.w[c][vic].nu, true);
          ra2_drop(&W, c, vic);
        }
      }
      /* What stays goes on past the call as a copy made just ahead of it:
         only the copies need callee-saved registers, from the call on --
         no more of them than there are -- where a value made between two
         calls would otherwise find them held by the first call's values
         and be saved round the second (a copy of a value already in a
         callee-saved register takes that register: no move). */
      if (!warmc_getenv("WARMC_RA2_NO_CALLSPLIT"))
        for (int c = 0; c < 2; c++)
          for (int k = 0; k < W.n[c]; k++) {
            Ra2W *w = &W.w[c][k];
            if (R->res[w->v]) continue;
            /* a constant is made again, not copied: a constant still to the
               selection's forms and to ra2_const_params */
            Inst wc = f->insts[w->v];
            bool cst = R->rem[w->v] && wc.op == I_CONST && warmc_getenv("WARMC_RA2_CLONE_CONSTS");
            Ref cp = cst ? ra2_new(R, b, I_CONST, (ValType)wc.type, NULL, 0, wc.imm, wc.imm2)
                         : ra2_new(R, b, I_COPY, (ValType)f->insts[w->v].type, &w->ver, 1, 0, 0);
            if (cst) R->slotv[cp] = 3;
            /* ahead of the call, which is the list's last */
            ra2_out(R, cp);
            R->out[R->nout - 1] = u;
            R->out[R->nout - 2] = cp;
            R->outpos[u] = (int32_t)R->nout - 1;
            /* the call reads the copy: the value dies at it, whose register
               the copy can take */
            if (!warmc_getenv("WARMC_RA2_NO_CALLREAD")) {
              Ref *ua = ir_args(f, u);
              for (uint32_t j = 0; j < f->insts[u].nargs; j++)
                if (ua[j] == w->ver) ua[j] = cp;
            }
            w->ver = cp;
            R->ncallsplit++;
          }
    }
    if (u < R->n0 && (R->need[u] || R->res[u])) {
      int32_t nu = ra2_next(R, b, &uses[u], u, p, blen);
      if (nu < RA2_INF) {
        int c = ra2_cls(R, u);
        /* an operand read for the last time leaves first, its register
           the result's to take -- but the one the instruction reads after
           writing its result (ra2a_late_read), which the result may not
           share: held for the result too, sum's loop in cbench/varargs had
           one value fewer room than it needed (WARMC_RA2_NO_DIEFIRST) */
        if (diefirst) {
          Ref lr = ra2a_late_arg(s, u);
          for (int k = 0; k < W.n[c];) {
            Ra2W *w = &W.w[c][k];
            if (w->nu >= RA2_INF && w->v != lr && w->ver != lr) ra2_drop(&W, c, k);
            else k++;
          }
        }
        ra2_room(R, &W, c, 1, kp, nk);
        if (W.n[c] < 64) W.w[c][W.n[c]++] = (Ra2W){ u, u, nu };
      }
    }
    for (int c = 0; c < 2; c++)
      for (int k = 0; k < W.n[c];) {
        if (W.w[c][k].nu >= RA2_INF) ra2_drop(&W, c, k);
        else k++;
      }
    if (kp != keep) xfree(kp);
  }

  /* the branch's operand, the returned values, the edges' arguments */
  if (bb->term == T_BRIF || bb->term == T_BRTABLE) {
    Ref x = bb->term_arg;
    if (x < R->n0 && R->need[x]) {
      int c = ra2_cls(R, x);
      int k = ra2_find(&W, c, x);
      if (k >= 0) bb->term_arg = W.w[c][k].ver;
      else if (R->rem[x]) {
        Inst xc = f->insts[x];
        Ref r = ra2_new(R, b, (IrOp)xc.op, (ValType)xc.type, NULL, 0, xc.imm, xc.imm2);
        ra2_out(R, r);
        R->nremat++;
        bb->term_arg = r;
      } else bb->term_arg = ra2_mem(R, x);
    }
  }
  for (uint32_t i = 0; i < bb->nret; i++) {
    Ref x = bb->ret_args[i];
    if (x >= R->n0 || !R->need[x]) continue;
    int c = ra2_cls(R, x);
    int k = ra2_find(&W, c, x);
    if (k >= 0) bb->ret_args[i] = W.w[c][k].ver;
    else bb->ret_args[i] = ra2_edge_arg(R, b, x);
  }

  /* what the block ends with, then its edges' arguments by it */
  R->done[b] = 1;
  R->xoff[b] = R->nxw;
  R->xn[b] = 0;
  for (int c = 0; c < 2; c++)
    for (int k = 0; k < W.n[c]; k++) {
      if (R->nxw == R->capxw) {
        R->capxw = R->capxw ? R->capxw * 2 : 256;
        R->xw = xrealloc(R->xw, R->capxw * sizeof(Ra2W));
      }
      R->xw[R->nxw++] = W.w[c][k];
      R->xn[b]++;
    }
  for (uint32_t si = 0; si < bb->nsuccs; si++) {
    Edge *e = &bb->succs[si];
    Block *tb = &f->blocks[e->target];
    /* the target's own params' arguments; the ones the pass added it
       fills from the pending list below */
    for (uint32_t j = 0; j < e->nargs && j < tb->nparams; j++) {
      Ref x = e->args[j];
      if (x >= R->n0 || !R->need[x]) continue;
      if (j >= tb->nparams) break;
      if (tb->params[j] >= R->n0) continue;
      e->args[j] = ra2_edge_arg(R, b, x);
    }
  }
  for (uint32_t k = 0; k < R->npend; k++) {
    Ra2Pend *pd = &R->pend[k];
    if (pd->from != b) continue;
    Edge *e = &bb->succs[pd->si];
    e->args[pd->ai] = ra2_edge_arg(R, b, pd->v);
    pd->from = UINT32_MAX;
  }

  /* the block's new list */
  bb = &f->blocks[b];
  if (R->nout > bb->cap_insts) {
    uint32_t oc = bb->cap_insts;
    bb->cap_insts = R->nout;
    bb->insts = ir_grow(f, bb->insts, (size_t)oc * sizeof(uint32_t), (size_t)bb->cap_insts * sizeof(uint32_t));
  }
  memcpy(bb->insts, R->out, R->nout * sizeof(Ref));
  bb->ninsts = R->nout;
  for (uint32_t t = 0; t < *ntouched; t++) uses[touched[t]].n = 0;
  xfree(orig);
  xfree(ncall);
}

/* A param the pass added whose ways in all bring one value besides itself
   is that value: its reads take it, its slot in the edges goes. */
/* A param the pass added for a constant -- every way in brings that
   constant, made again or held -- where a way back makes it again at its
   end (ra2_edge_arg: the loop did not keep it) makes it on every trip for
   whichever of the loop's reads comes next: each of its reads instead
   takes the constant made where it reads, made only when that read runs
   (cbench/switch: three per trip on the way back for its cases' compares),
   and the param, read by nothing then, goes (ra2_trivial_params). */
static void ra2_const_params(Ra2 *R) {
  IrFunc *f = R->f;
  /* which added params are constants: every way in brings the one constant
     -- made, or a param already found to be it -- or the param itself,
     found to a fixpoint (a loop's param fed by an outer loop's) */
  uint32_t na = R->nadded;
  uint8_t *isc = xcalloc(na ? na : 1, 1);
  uint64_t *cimm = xcalloc(na ? na : 1, sizeof(uint64_t)), *cimm2 = xcalloc(na ? na : 1, sizeof(uint64_t));
  uint8_t *rem = xcalloc(na ? na : 1, 1);
  int32_t *idx = xmalloc((f->ninsts ? f->ninsts : 1) * sizeof(int32_t));
  for (uint32_t v = 0; v < f->ninsts; v++) idx[v] = -1;
  for (uint32_t k = 0; k < na; k++) { idx[R->added[k]] = (int32_t)k; isc[k] = 1; }
  for (bool changed = true; changed;) {
    changed = false;
    for (uint32_t k = 0; k < na; k++) {
      if (!isc[k]) continue;
      Ref p = R->added[k];
      uint32_t b = f->insts[p].block;
      Block *bb = &f->blocks[b];
      uint32_t pi = 0;
      while (pi < bb->nparams && bb->params[pi] != p) pi++;
      bool ok = pi < bb->nparams, any = false, remade = false;
      uint64_t imm = 0, imm2 = 0;
      ValType ty = (ValType)f->insts[p].type;
      for (uint32_t q = 0; q < bb->npreds && ok; q++) {
        Block *pb = &f->blocks[bb->preds[q]];
        for (uint32_t si = 0; si < pb->nsuccs && ok; si++) {
          Edge *e = &pb->succs[si];
          if (e->target != b || pi >= e->nargs) continue;
          Ref a = e->args[pi];
          if (a == p) continue;
          /* a copy of a constant -- one made ahead of a call -- is it */
          for (int hop = 0; hop < 8 && f->insts[a].op == I_COPY && f->insts[a].nargs == 1 &&
                            !(a < R->nslotv && R->slotv[a] == 1); hop++)
            a = ir_args(f, a)[0];
          uint64_t ai, ai2;
          if (f->insts[a].op == I_CONST && (ValType)f->insts[a].type == ty) {
            ai = f->insts[a].imm;
            ai2 = f->insts[a].imm2;
            if (pb->rpo >= bb->rpo && a < R->nslotv && R->slotv[a] == 2) remade = true;
          } else if (idx[a] >= 0 && isc[idx[a]] && (ValType)f->insts[a].type == ty) {
            ai = cimm[idx[a]];
            ai2 = cimm2[idx[a]];
            /* still being found: taken as it stands, undone below if not */
            if (!any && idx[a] > (int32_t)k) { ai = cimm[idx[a]]; }
          } else {
            ok = false;
            break;
          }
          if (any && (ai != imm || ai2 != imm2)) { ok = false; break; }
          any = true;
          imm = ai;
          imm2 = ai2;
        }
      }
      if (!ok || !any) { isc[k] = 0; changed = true; continue; }
      if (cimm[k] != imm || cimm2[k] != imm2 || rem[k] != (remade || rem[k])) changed = true;
      cimm[k] = imm;
      cimm2[k] = imm2;
      rem[k] |= remade;
    }
  }
  /* grounded: some way in brings the constant made, or a param grounded */
  uint8_t *gnd = xcalloc(na ? na : 1, 1);
  for (bool changed = true; changed;) {
    changed = false;
    for (uint32_t k = 0; k < na; k++) {
      if (!isc[k] || gnd[k]) continue;
      Ref p = R->added[k];
      uint32_t b = f->insts[p].block;
      Block *bb = &f->blocks[b];
      uint32_t pi = 0;
      while (pi < bb->nparams && bb->params[pi] != p) pi++;
      for (uint32_t q = 0; q < bb->npreds && !gnd[k]; q++) {
        Block *pb = &f->blocks[bb->preds[q]];
        for (uint32_t si = 0; si < pb->nsuccs && !gnd[k]; si++) {
          Edge *e = &pb->succs[si];
          if (e->target != b || pi >= e->nargs || e->args[pi] == p) continue;
          Ref a = e->args[pi];
          for (int hop = 0; hop < 8 && f->insts[a].op == I_COPY && f->insts[a].nargs == 1 &&
                            !(a < R->nslotv && R->slotv[a] == 1); hop++)
            a = ir_args(f, a)[0];
          if (f->insts[a].op == I_CONST || (idx[a] >= 0 && gnd[idx[a]])) { gnd[k] = 1; changed = true; }
        }
      }
    }
  }
  for (uint32_t k = 0; k < na; k++) {
    Ref p = R->added[k];
    if (!isc[k] || !gnd[k] || !rem[k] || warmc_getenv("WARMC_RA2_NO_CONSTPARAM")) continue;
    ValType ty = (ValType)f->insts[p].type;
    uint64_t imm = cimm[k], imm2 = cimm2[k];
    /* each read of p: the constant made just ahead of it */
    for (uint32_t bi = 0; bi < f->nblocks; bi++) {
      Block *xb = &f->blocks[bi];
      for (uint32_t i = 0; i < xb->ninsts; i++) {
        Ref u = xb->insts[i];
        Ref *ua = ir_args(f, u);
        bool reads = false;
        for (uint32_t j = 0; j < f->insts[u].nargs && !reads; j++) reads = ua[j] == p;
        if (!reads) continue;
        Ref c = ra2_new(R, bi, I_CONST, ty, NULL, 0, imm, imm2);
        ra2_insert(f, bi, i, c);
        i++;
        ua = ir_args(f, u);
        for (uint32_t j = 0; j < f->insts[u].nargs; j++) if (ua[j] == p) ua[j] = c;
      }
      bool at_end = (xb->term == T_BRIF || xb->term == T_BRTABLE) && xb->term_arg == p;
      for (uint32_t i = 0; i < xb->nret && !at_end; i++) at_end = xb->ret_args[i] == p;
      for (uint32_t si = 0; si < xb->nsuccs && !at_end; si++)
        for (uint32_t j = 0; j < xb->succs[si].nargs && !at_end; j++) at_end = xb->succs[si].args[j] == p;
      if (!at_end) continue;
      Ref c = ra2_new(R, bi, I_CONST, ty, NULL, 0, imm, imm2);
      ra2_insert(f, bi, xb->ninsts, c);
      if ((xb->term == T_BRIF || xb->term == T_BRTABLE) && xb->term_arg == p) xb->term_arg = c;
      bool edge_only = true;
      if (xb->term_arg == c) edge_only = false;
      for (uint32_t i = 0; i < xb->nret; i++) if (xb->ret_args[i] == p) xb->ret_args[i] = c;
      for (uint32_t si = 0; si < xb->nsuccs; si++)
        for (uint32_t j = 0; j < xb->succs[si].nargs; j++)
          if (xb->succs[si].args[j] == p) xb->succs[si].args[j] = c;
      if (edge_only) R->slotv[c] = 2;
    }
    R->nconstparam++;
  }
  xfree(isc); xfree(cimm); xfree(cimm2); xfree(rem); xfree(idx); xfree(gnd);
}

/* A value made again where it is read that its every reader now takes a
   copy of instead: nothing reads it, and it reads nothing -- kept, it was
   made where it was first defined into a register no one read, six
   memory bases a block in stdgo/wordfreq's bufio.Scanner.Scan
   (WARMC_RA2_KEEP_DEADREM) */
static bool ra2_dead_rem(Ra2 *R, Ref u) {
  const Inst *in = &R->f->insts[u];
  return R->rem[u] && in->nargs == 0 && !warmc_getenv("WARMC_RA2_KEEP_DEADREM") &&
         (in->op == I_CONST || in->op == I_MEM_BASE || in->op == I_GC_BADMASK || in->op == I_GLOBAL_GET);
}

static void ra2_trivial_params(Ra2 *R) {
  IrFunc *f = R->f;
  bool changed = true;
  Ref *repl = xcalloc(f->ninsts ? f->ninsts : 1, sizeof(Ref));
  for (uint32_t i = 0; i < f->ninsts; i++) repl[i] = i;
  uint8_t *gone = xcalloc(f->ninsts ? f->ninsts : 1, 1);
  while (changed) {
    changed = false;
    for (uint32_t k = 0; k < R->nadded; k++) {
      Ref p = R->added[k];
      if (gone[p]) continue;
      uint32_t b = f->insts[p].block;
      Block *bb = &f->blocks[b];
      uint32_t pi = 0;
      while (pi < bb->nparams && bb->params[pi] != p) pi++;
      if (pi == bb->nparams) continue;
      Ref one = REF_NONE;
      bool ok = true;
      for (uint32_t pq = 0; pq < bb->npreds && ok; pq++) {
        uint32_t ob = bb->preds[pq];
        bool dup = false;
        for (uint32_t q = 0; q < pq && !dup; q++) dup = bb->preds[q] == ob;
        if (dup) continue;
        Block *pb = &f->blocks[ob];
        for (uint32_t si = 0; si < pb->nsuccs && ok; si++) {
          if (pb->succs[si].target != b || pi >= pb->succs[si].nargs) continue;
          Ref a = pb->succs[si].args[pi];
          while (repl[a] != a) a = repl[a];
          if (a == p) continue;
          if (one == REF_NONE) one = a;
          else if (one != a) ok = false;
        }
      }
      if (!ok || one == REF_NONE) continue;
      /* not a param in a register for the copy in a slot it is loaded from
         on the way into its loop: every read would load it again
         (gobench/typeswitch's array base, on every trip) */
      bool pslot = p < R->nslotv && R->slotv[p] == 1, oslot = one < R->nslotv && R->slotv[one] == 1;
      if (oslot && !pslot && !warmc_getenv("WARMC_RA2_TRIV_SLOT")) continue;
      /* a param in a register for a copy made for the edges alone: the
         copy takes the param's reads, in its register -- made at each, the
         memory base made ahead of gobench/matrix's loops was made twice a
         trip; not for a param in its slot, whose reads take no register
         (WARMC_RA2_NO_EDGEREAD) */
      if (!pslot && one < R->nslotv && R->slotv[one] == 2 && !warmc_getenv("WARMC_RA2_NO_EDGEREAD"))
        R->slotv[one] = 3;
      repl[p] = one;
      gone[p] = 1;
      changed = true;
    }
  }
  /* A param the pass added that nothing reads but the edges into the
     params of its kind that nothing reads either -- a value it held a
     register for on entry and later let go to its slot -- goes too: kept,
     it would hold a register to the end of its block for nothing. */
  if (!warmc_getenv("WARMC_RA2_NO_DEADPARAM")) {
    uint8_t *added = xcalloc(f->ninsts ? f->ninsts : 1, 1);
    uint8_t *live = xcalloc(f->ninsts ? f->ninsts : 1, 1);
    Ref *wl = xmalloc((R->nadded ? R->nadded : 1) * sizeof(Ref));
    uint32_t nwl = 0;
    for (uint32_t k = 0; k < R->nadded; k++) if (!gone[R->added[k]]) added[R->added[k]] = 1;
#define RA2_RES(x) ({ Ref r_ = (x); while (repl[r_] != r_) r_ = repl[r_]; r_; })
#define RA2_MARK(x) do { Ref m_ = RA2_RES(x); if (added[m_] && !live[m_]) { live[m_] = 1; wl[nwl++] = m_; } } while (0)
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      Block *bb = &f->blocks[f->order[oi]];
      for (uint32_t i = 0; i < bb->ninsts; i++)
        for (uint32_t j = 0; j < f->insts[bb->insts[i]].nargs; j++) RA2_MARK(ir_args(f, bb->insts[i])[j]);
      if (bb->term == T_BRIF || bb->term == T_BRTABLE) RA2_MARK(bb->term_arg);
      for (uint32_t i = 0; i < bb->nret; i++) RA2_MARK(bb->ret_args[i]);
      for (uint32_t si = 0; si < bb->nsuccs; si++) {
        Edge *e = &bb->succs[si];
        Block *tb = &f->blocks[e->target];
        for (uint32_t j = 0; j < e->nargs; j++)
          if (j >= tb->nparams || !added[tb->params[j]]) RA2_MARK(e->args[j]);
      }
    }
    while (nwl) {
      Ref p = wl[--nwl];
      uint32_t b = f->insts[p].block;
      Block *bb = &f->blocks[b];
      uint32_t pi = 0;
      while (pi < bb->nparams && bb->params[pi] != p) pi++;
      for (uint32_t pq = 0; pq < bb->npreds; pq++) {
        Block *pb = &f->blocks[bb->preds[pq]];
        for (uint32_t si = 0; si < pb->nsuccs; si++)
          if (pb->succs[si].target == b && pi < pb->succs[si].nargs) RA2_MARK(pb->succs[si].args[pi]);
      }
    }
#undef RA2_MARK
#undef RA2_RES
    for (uint32_t k = 0; k < R->nadded; k++) {
      Ref p = R->added[k];
      if (added[p] && !live[p]) { gone[p] = 1; R->ndeadparam++; }
    }
    xfree(added);
    xfree(live);
    xfree(wl);
  }
  /* the reads renamed, the gone params and their edges' slots dropped */
  bool any = false;
  for (uint32_t k = 0; k < R->nadded; k++) any |= gone[R->added[k]];
  if (any) {
    for (uint32_t bi = 0; bi < f->nblocks; bi++) {
      Block *bb = &f->blocks[bi];
      for (uint32_t i = 0; i < bb->ninsts; i++) {
        Ref *a = ir_args(f, bb->insts[i]);
        for (uint32_t j = 0; j < f->insts[bb->insts[i]].nargs; j++)
          while (repl[a[j]] != a[j]) a[j] = repl[a[j]];
      }
      if (bb->term == T_BRIF || bb->term == T_BRTABLE)
        while (repl[bb->term_arg] != bb->term_arg) bb->term_arg = repl[bb->term_arg];
      for (uint32_t i = 0; i < bb->nret; i++)
        while (repl[bb->ret_args[i]] != bb->ret_args[i]) bb->ret_args[i] = repl[bb->ret_args[i]];
      for (uint32_t si = 0; si < bb->nsuccs; si++) {
        Edge *e = &bb->succs[si];
        for (uint32_t j = 0; j < e->nargs; j++)
          while (repl[e->args[j]] != e->args[j]) e->args[j] = repl[e->args[j]];
      }
    }
    for (uint32_t bi = 0; bi < f->nblocks; bi++) {
      Block *bb = &f->blocks[bi];
      uint32_t m = 0;
      uint8_t *keep = xmalloc(bb->nparams ? bb->nparams : 1);
      for (uint32_t pi = 0; pi < bb->nparams; pi++) {
        keep[pi] = !gone[bb->params[pi]];
        if (keep[pi]) bb->params[m++] = bb->params[pi];
      }
      if (m != bb->nparams) {
        for (uint32_t pq = 0; pq < bb->npreds; pq++) {
          uint32_t ob = bb->preds[pq];
          bool dup = false;
          for (uint32_t q = 0; q < pq && !dup; q++) dup = bb->preds[q] == ob;
          if (dup) continue;
          Block *pb = &f->blocks[ob];
          for (uint32_t si = 0; si < pb->nsuccs; si++) {
            Edge *e = &pb->succs[si];
            if (e->target != bi) continue;
            uint32_t q = 0;
            for (uint32_t j = 0; j < e->nargs; j++)
              if (j >= bb->nparams || keep[j]) e->args[q++] = e->args[j];
            e->nargs = q;
          }
        }
        bb->nparams = m;
      }
      xfree(keep);
    }
  }
  xfree(repl);
  xfree(gone);
}

/* Each instruction folded into the one that reads it -- an address into
   its access, a compare into its select, a fused compare into the branch
   -- moved to just ahead of its reader, where the selection makes it: its
   operands are read there, so the spiller sees them live up to there, and
   a reload it makes for one comes ahead of both. */
static void ra2_fold_emit(Ra2 *R, uint32_t b, Ref u, Ref *outl, uint32_t *no, uint8_t *placed) {
  IrFunc *f = R->f;
  Isel *s = R->s;
  Ref *a = ir_args(f, u);
  for (uint32_t j = 0; j < f->insts[u].nargs; j++) {
    Ref x = a[j];
    if (x >= R->n0 || placed[x] || !R->inblk[x]) continue;
    if (!((s->folded && s->folded[x]) || (s->fused && s->fused[x]))) continue;
    if (s->uses && s->uses[x] != 1) continue;
    placed[x] = 1;
    R->fmoved[x] = 1;
    ra2_fold_emit(R, b, x, outl, no, placed);
    outl[(*no)++] = x;
  }
}

static void ra2_fold_order(Ra2 *R) {
  IrFunc *f = R->f;
  Isel *s = R->s;
  uint8_t *placed = xcalloc(R->n0 ? R->n0 : 1, 1);
  uint8_t *here = xcalloc(R->n0 ? R->n0 : 1, 1);
  R->inblk = xcalloc(R->n0 ? R->n0 : 1, 1);
  R->fmoved = xcalloc(R->n0 ? R->n0 : 1, 1);
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t b = f->order[oi];
    Block *bb = &f->blocks[b];
    uint32_t n = bb->ninsts;
    if (!n) continue;
    /* the folded instructions an instruction of the block, or its branch,
       reads: those move to their reader; any other stays where it is */
    for (uint32_t i = 0; i < n; i++) R->inblk[bb->insts[i]] = 1;
    for (uint32_t i = 0; i < n; i++) {
      Ref *a = ir_args(f, bb->insts[i]);
      for (uint32_t j = 0; j < f->insts[bb->insts[i]].nargs; j++)
        if (a[j] < R->n0) here[a[j]] = 1;
    }
    if (bb->term == T_BRIF && bb->term_arg < R->n0) here[bb->term_arg] = 1;
    Ref *outl = xmalloc(n * sizeof(Ref));
    uint32_t no = 0;
    for (uint32_t i = 0; i < n; i++) {
      Ref u = bb->insts[i];
      bool folded = ((s->folded && s->folded[u]) || (s->fused && s->fused[u])) &&
                    (!s->uses || s->uses[u] == 1) && here[u];
      if (folded) continue;
      ra2_fold_emit(R, b, u, outl, &no, placed);
      if (placed[u]) {
        fprintf(stderr, "twice v%u %s folded=%d fused=%d uses=%u here=%d\n", u, ir_op_name(f->insts[u].op),
                s->folded ? s->folded[u] : -1, s->fused ? s->fused[u] : -1, s->uses ? s->uses[u] : 0, here[u]);
      }
      placed[u] = 1;
      outl[no++] = u;
    }
    if (bb->term == T_BRIF && bb->term_arg < R->n0 && R->inblk[bb->term_arg] &&
        !placed[bb->term_arg]) {
      Ref c = bb->term_arg;
      ra2_fold_emit(R, b, c, outl, &no, placed);
      placed[c] = 1;
      outl[no++] = c;
    }
    if (no != n) {
      for (uint32_t i = 0; i < n; i++)
        if (!placed[bb->insts[i]])
          fprintf(stderr, "lost v%u %s folded=%d fused=%d uses=%u\n", bb->insts[i], ir_op_name(f->insts[bb->insts[i]].op),
                  s->folded ? s->folded[bb->insts[i]] : -1, s->fused ? s->fused[bb->insts[i]] : -1, s->uses ? s->uses[bb->insts[i]] : 0);
      warmc_fatal("internal: the spiller's fold order lost an instruction of b%u", b);
    }
    memcpy(bb->insts, outl, n * sizeof(Ref));
    for (uint32_t i = 0; i < n; i++) {
      placed[bb->insts[i]] = 0;
      R->inblk[bb->insts[i]] = 0;
      Ref *a = ir_args(f, bb->insts[i]);
      for (uint32_t j = 0; j < f->insts[bb->insts[i]].nargs; j++)
        if (a[j] < R->n0) here[a[j]] = 0;
    }
    if (bb->term_arg < R->n0) here[bb->term_arg] = 0;
    xfree(outl);
  }
  xfree(placed);
  xfree(here);
  xfree(R->inblk);
  R->inblk = NULL;
  /* what is folded and stays where it is: read where its readers are */
  R->shared = xcalloc(R->n0 ? R->n0 : 1, 1);
  if (s->folded && !warmc_getenv("WARMC_RA2_NO_UNSHARE"))
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      Block *bb = &f->blocks[f->order[oi]];
      for (uint32_t i = 0; i < bb->ninsts; i++) {
        Ref x = bb->insts[i];
        if (x < R->n0 && s->folded[x] && (!R->fmoved[x] || !warmc_getenv("WARMC_RA2_MOVED_OWN")))
          R->shared[x] = 1;
      }
    }
  xfree(R->fmoved);
  R->fmoved = NULL;
}

/* Of the copies and params, those that are versions of a mem_base or of
   the barrier's mask, which the selection takes by their op (IrFunc's
   ra2_of): a copy that of its operand, a param that of its arguments
   where all of them are versions of one -- copies of it made apart, or
   made again where it ran out of registers, make the same -- the way round
   a loop, a version of the param itself, left out of the count until it
   turns out otherwise. */
__attribute__((unused))
static bool ra2_same_source(const IrFunc *f, Ref a, Ref b) {
  const Inst *x = &f->insts[a], *y = &f->insts[b];
  return x->op == y->op && x->imm == y->imm && x->imm2 == y->imm2 && x->type == y->type;
}

__attribute__((unused))
static void ra2_versions(IrFunc *f) {
  uint32_t n = f->ninsts;
  xfree(f->ra2_of);
  f->ra2_of = NULL;
  f->ra2_nof = 0;
  bool any = false;
  for (uint32_t v = 0; v < n && !any; v++)
    any = f->insts[v].op == I_MEM_BASE || f->insts[v].op == I_GC_BADMASK;
  if (!any || warmc_getenv("WARMC_RA2_NO_VERSIONS")) return;
  /* 0 not known yet, 1 a version of of[v], 2 none */
  uint8_t *st = xcalloc(n, 1);
  Ref *of = xmalloc(n * sizeof(Ref));
  for (uint32_t v = 0; v < n; v++) {
    uint16_t op = f->insts[v].op;
    of[v] = REF_NONE;
    if (op == I_MEM_BASE || op == I_GC_BADMASK) { st[v] = 1; of[v] = v; }
    else if (!(op == I_COPY && f->insts[v].nargs == 1) && op != I_PARAM) st[v] = 2;
  }
  /* a meet of what is known: none where two disagree or one is none */
#define RA2_MEET(a) do { Ref a_ = (a); \
    if (st[a_] == 2) { ns = 2; } \
    else if (st[a_] == 1 && ns != 2) { \
      if (ns == 0) { ns = 1; no = of[a_]; } \
      else if (!ra2_same_source(f, no, of[a_])) ns = 2; } } while (0)
  bool changed = true;
  while (changed) {
    changed = false;
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      uint32_t b = f->order[oi];
      Block *bb = &f->blocks[b];
      for (uint32_t pi = 0; pi < bb->nparams; pi++) {
        Ref p = bb->params[pi];
        if (st[p] == 2) continue;
        uint8_t ns = 0;
        Ref no = REF_NONE;
        if (bb->is_pad || b == f->entry) ns = 2;
        for (uint32_t q = 0; q < bb->npreds && ns != 2; q++) {
          Block *pb = &f->blocks[bb->preds[q]];
          for (uint32_t si = 0; si < pb->nsuccs && ns != 2; si++) {
            Edge *e = &pb->succs[si];
            if (e->target != b) continue;
            if (e->eh || pi >= e->nargs) { ns = 2; break; }
            if (e->args[pi] != p) RA2_MEET(e->args[pi]);
          }
        }
        if (ns != 0 && (ns != st[p] || (ns == 1 && of[p] == REF_NONE))) {
          if (ns == 1 && st[p] == 1) continue;
          st[p] = ns;
          if (ns == 1) of[p] = no;
          changed = true;
        }
      }
      for (uint32_t i = 0; i < bb->ninsts; i++) {
        Ref u = bb->insts[i];
        if (st[u] == 2 || f->insts[u].op != I_COPY) continue;
        uint8_t ns = 0;
        Ref no = REF_NONE;
        RA2_MEET(ir_args(f, u)[0]);
        if (ns != 0 && ns != st[u]) {
          st[u] = ns;
          if (ns == 1) of[u] = no;
          changed = true;
        }
      }
    }
  }
#undef RA2_MEET
  bool some = false;
  for (uint32_t v = 0; v < n; v++) {
    if (st[v] != 1 || of[v] == v) of[v] = REF_NONE;
    else some = true;
  }
  xfree(st);
  if (!some) { xfree(of); return; }
  f->ra2_of = of;
  f->ra2_nof = n;
}

/* The function rewritten: its pieces of values in registers and copies in
   slots (IrFunc.ra2_mem), for instruction selection to start over on. */
__attribute__((unused))
static void ra2_spill(Isel *s, const int *gpool, int ngpool) {
  IrFunc *f = s->f;
  Ra2 R;
  memset(&R, 0, sizeof R);
  R.f = f;
  R.s = s;
  R.n0 = f->ninsts;
  uint32_t n0 = R.n0;
  R.need = xcalloc(n0 ? n0 : 1, 1);
  R.rem = xcalloc(n0 ? n0 : 1, 1);
  R.res = xcalloc(n0 ? n0 : 1, 1);
  R.mem = xmalloc((n0 ? n0 : 1) * sizeof(Ref));
  for (uint32_t v = 0; v < n0; v++) {
    R.mem[v] = REF_NONE;
    Inst *in = &f->insts[v];
    if (!value_needs_location(f, v) || in->op == I_NOP) continue;
    /* what takes no register of its own: an immediate, a fused or folded
       instruction, a zero, a value made again where it is read */
    if (skip_location(s, v) || ra_zero_const(s, v, s->cls[v])) continue;
    /* (the barrier's mask, held as a resident -- one load where the scan
       holds it in a register -- cost javabench/treemap 14% in spills
       elsewhere) */
    if (in->op == I_GC_BADMASK && warmc_getenv("WARMC_RA2_RESKEEP")) {
      R.res[v] = 1;
      continue;
    }
    /* The barrier's mask competes for a register like a constant, made
       again where it is out of them: made at every read, a loop of
       barriers loads it at each (javabench/list: four a trip, where the
       scan holds it in a register); a load reads it through any version
       (load_barrier_mask) */
    bool mask = in->op == I_GC_BADMASK && !warmc_getenv("WARMC_RA2_MASK_PERREAD");
    /* -- and so does the memory base plus an offset of one add, which an
       access reads through any version too (ra2_versions): made at every
       read, asbench's loops made theirs twice a trip, where the scan holds
       it in a register (asbench/virtual, staticarr, stdgo/iface) */
    bool mbase = in->op == I_MEM_BASE && remat_ok(s, v) && !warmc_getenv("WARMC_RA2_MBASE_PERREAD");
    /* A constant competes for a register like any value, made again where
       it is out of them: a loop's constant made on every way round costs
       that on every way round (rsbench/vecgrow's hash multiplier,
       rsbench/enummatch's element size, zigbench/simdsum's vector mask). */
    /* (a scalar float constant one fmov or a load from the pool makes is
       left to be made where it is read: held, those of asbench/mathf's
       libm took the callee-saved vector registers round its calls) */
    if (remat_ok(s, v) && !mask && !mbase &&
        !(in->op == I_CONST && !warmc_getenv("WARMC_RA2_NO_CONST1") &&
          (!vt_is_float((ValType)in->type) || !warmc_getenv("WARMC_RA2_NO_FPCONST"))))
      continue;
    /* left as they are: the barrier's mask, made at each read (or held,
       above); a memory base of more than one add and an address made of
       it, which only an access may read (analyse_immediates); and a native
       pointer, which an access takes for one only as the mem_ptr itself --
       a copy of it would be taken for an address in the memory */
    if (in->op == I_MEM_PTR || (in->op == I_GC_BADMASK && !mask) ||
        (in->op == I_MEM_BASE && !mbase) || in->op == I_MEM_ADDR) {
      R.res[v] = 1;
      continue;
    }
    /* a constant or an immutable global's value that does take a
       register -- one an edge or a call's moves read from a location, one
       too long to make at every read -- is made again where it is out of
       the registers, the copy a value of its own */
    if (in->nargs == 0 &&
        (in->op == I_CONST || mask || mbase ||
         (in->op == I_GLOBAL_GET && !f->m->globals[in->imm].mutable_ &&
          !f->m->globals[in->imm].imported)))
      R.rem[v] = !warmc_getenv("WARMC_RA2_NO_CREM");
    /* -- but an integer one of three instructions or more goes to a slot
       like any value and comes back with one load, as the scan's do
       (hsbench/bits' hash multipliers, made again on every way round) */
    if (R.rem[v] && in->op == I_CONST && !vt_is_float((ValType)in->type) && (ValType)in->type != VT_V128 &&
        ir_const_cost(in->imm, vt_is_64((ValType)in->type)) >= 3 && !warmc_getenv("WARMC_RA2_REM_ALL"))
      R.rem[v] = 0;
    if (warmc_getenv("WARMC_RA2_NO_CREM") && in->nargs == 0 &&
        (in->op == I_CONST ||
         (in->op == I_GLOBAL_GET && !f->m->globals[in->imm].mutable_ &&
          !f->m->globals[in->imm].imported)))
      continue;
    R.need[v] = 1;
  }
  int ncs = 0;
  for (int k = 0; k < ngpool; k++) ncs += gpr_is_callee_saved(gpool[k]);
  int nfcs = 0;
  for (int k = 0; k < NFPR_ALLOC; k++) nfcs += fpr_is_callee_saved(FPR_ALLOC[k]);
  const char *km = warmc_getenv("WARMC_RA2_KM");
  int margin = km ? atoi(km) : 0;
  R.K[0] = ngpool - margin;
  R.K[1] = NFPR_ALLOC - margin;
  R.Kcs[0] = ncs;
  R.Kcs[1] = nfcs;
  if (R.K[0] > 64) R.K[0] = 64;
  if (R.K[1] > 64) R.K[1] = 64;
  R.nslotv = n0 + 64;
  R.slotv = xcalloc(R.nslotv, 1);
  R.xoff = xcalloc(f->nblocks ? f->nblocks : 1, sizeof(uint32_t));
  R.xn = xcalloc(f->nblocks ? f->nblocks : 1, sizeof(uint32_t));
  R.done = xcalloc(f->nblocks ? f->nblocks : 1, 1);
  R.outpos = xmalloc((n0 ? n0 : 1) * sizeof(int32_t));

  ra2_fold_order(&R);
  compute_liveness(s);
  R.expdist = !warmc_getenv("WARMC_RA2_NO_EXPDIST");
  ra2_distances(&R);
  ra2_freqs(&R);
  ra2_block_pressure(&R);

  Ra2Uses *uses = xcalloc(n0 ? n0 : 1, sizeof(Ra2Uses));
  Ref *touched = xmalloc((n0 ? n0 : 1) * sizeof(Ref));
  uint32_t nt = 0;
  for (uint32_t oi = 0; oi < f->norder; oi++) ra2_block(&R, f->order[oi], uses, touched, &nt);
  for (uint32_t v = 0; v < n0; v++) xfree(uses[v].pos);
  xfree(uses);
  xfree(touched);

  if (!warmc_getenv("WARMC_RA2_NO_CONSTPARAM")) ra2_const_params(&R);
  ra2_trivial_params(&R);
  /* a folded instruction several read that each now reads a copy of: gone */
  {
    uint32_t *cnt = xcalloc(f->ninsts ? f->ninsts : 1, sizeof(uint32_t));
    for (uint32_t oi = 0; oi < f->norder; oi++) {
      Block *bb = &f->blocks[f->order[oi]];
      for (uint32_t i = 0; i < bb->ninsts; i++) {
        Ref u = bb->insts[i];
        for (uint32_t j = 0; j < f->insts[u].nargs; j++) cnt[ir_args(f, u)[j]]++;
      }
      if (bb->term == T_BRIF || bb->term == T_BRTABLE) cnt[bb->term_arg]++;
      for (uint32_t i = 0; i < bb->nret; i++) cnt[bb->ret_args[i]]++;
      for (uint32_t k = 0; k < bb->nsuccs; k++)
        for (uint32_t j = 0; j < bb->succs[k].nargs; j++) cnt[bb->succs[k].args[j]]++;
    }
    /* the copies' own operands read nothing once they go: from the last
       made back, a copy no one reads drops its reads of its operands */
    for (uint32_t oi = f->norder; oi-- > 0;) {
      Block *bb = &f->blocks[f->order[oi]];
      uint32_t m = 0;
      for (uint32_t i = bb->ninsts; i-- > 0;) {
        Ref u = bb->insts[i];
        bool gone = u < n0 && (R.shared[u] || ra2_dead_rem(&R, u)) && !cnt[u];
        if (gone)
          for (uint32_t j = 0; j < f->insts[u].nargs; j++) cnt[ir_args(f, u)[j]]--;
        (void)m;
      }
      for (uint32_t i = 0; i < bb->ninsts; i++) {
        Ref u = bb->insts[i];
        if (u < n0 && (R.shared[u] || ra2_dead_rem(&R, u)) && !cnt[u]) continue;
        bb->insts[m++] = u;
      }
      bb->ninsts = m;
    }
    xfree(cnt);
  }
  ir_verify(f, "ra2");
  if (warmc_getenv("WARMC_RA2_DEBUG")) ir_print(f, "ra2");

  f->ra2_done = true;
  ra2_versions(f);
  xfree(f->ra2_mem);
  f->ra2_nmem = f->ninsts;
  f->ra2_mem = xcalloc(f->ninsts ? f->ninsts : 1, 1);
  for (uint32_t v = 0; v < f->ninsts && v < R.nslotv; v++) f->ra2_mem[v] = R.slotv[v];
  /* the constants the pass kept in registers: kept there (ra2_assign);
     and the barrier's mask, where it is held as a resident
     (WARMC_RA2_RESKEEP), which the assignment would otherwise load again
     at every read (javabench/iface: twice a trip) */
  for (uint32_t v = 0; v < n0; v++) if (R.rem[v] && R.need[v] && !f->ra2_mem[v]) f->ra2_mem[v] = 3;
  for (uint32_t v = 0; v < n0; v++)
    if (R.res[v] && !f->ra2_mem[v] && f->insts[v].op == I_GC_BADMASK) f->ra2_mem[v] = 3;

  if (warmc_getenv("WARMC_RA2_DEBUG_FB")) {
    xfree(f->ra2_dbg);
    f->ra2_dbg = xcalloc(f->ninsts ? f->ninsts : 1, 1);
    for (uint32_t v = 0; v < f->ninsts; v++)
      f->ra2_dbg[v] = v >= n0 ? 3 : R.need[v] ? 1 : R.res[v] ? 2 : 0;
  }
  g_stats.ra2_reloads += R.nreload;
  g_stats.ra2_spills += R.nspill;
  g_stats.ra2_remats += R.nremat;
  g_stats.ra2_params += R.nparam;
  g_stats.ra2_callsplits += R.ncallsplit;
  g_stats.ra2_whole += R.nwhole;

  xfree(R.need); xfree(R.rem); xfree(R.res); xfree(R.mem); xfree(R.xoff); xfree(R.xn); xfree(R.xw);
  xfree(R.done); xfree(R.din); xfree(R.pend); xfree(R.slotv); xfree(R.out); xfree(R.outpos);
  xfree(R.added);
  xfree(R.shared);
  xfree(R.maxp); xfree(R.lstamp); xfree(R.lstack); xfree(R.vuse); xfree(R.vlv);
  xfree(R.mhead); xfree(R.mnext); xfree(R.mblk); xfree(R.mcp);
  xfree(f->ra2_freq);
  f->ra2_freq = R.freq;
  f->ra2_nfreq = f->nblocks;
  /* the liveness made for the pass is of the function before it */
  xfree(s->lv_in); xfree(s->lv_out); xfree(s->lv_in_off); xfree(s->lv_out_off);
  s->lv_in = s->lv_out = NULL;
  s->lv_in_off = s->lv_out_off = NULL;
}
