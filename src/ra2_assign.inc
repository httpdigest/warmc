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
        else xhot[x] = 1;
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
                     const uint8_t *xhot, const uint8_t *xalloc, const uint32_t *claimed, bool ghot) {
  IrFunc *f = s->f;
  bool v128 = (ValType)f->insts[x].type == VT_V128;
  bool want_cs = (xhot[x] || ghot) && !v128;
  /* the hint; then a register of the kind wanted that no group has taken
     for its own, then one of that kind, then any */
  for (int pass = -2; pass < 2; pass++)
    for (int k = 0; k < np; k++) {
      int r = pool[k];
      if (pass == -2 && r != hint) continue;
      if (r < 0 || r >= 64 || own[r] != REF_NONE) continue;
      if (c == 0 && xalloc[x] && r == RA_ALLOC_RES_REG) continue;
      bool cs = c ? fpr_is_callee_saved(r) : gpr_is_callee_saved(r);
      if (pass <= 0 && cs != want_cs) continue;
      if (pass == -1 && claimed && claimed[r]) continue;
      if (pass == 1 && xhot[x] && !v128 && !cs && vt_is_ref((ValType)f->insts[x].type) && !isel_csa_refs())
        continue;
      return r;
    }
  return -1;
}

__attribute__((unused))
static void ra2_assign(Isel *s) {
  IrFunc *f = s->f;
  uint32_t n = f->ninsts;
  uint32_t nb = f->nblocks;
  compute_liveness(s);
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
  for (uint32_t v = 0; v < n; v++) {
    if (!value_needs_location(f, v) || skip_location(s, v)) continue;
    if (s->pinned[v]) { reg[v] = SLOT(take_slot(s, v, &slot)); continue; }
    if (ra_zero_const(s, v, s->cls[v])) continue;
    if (remat_ok(s, v)) continue;
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
  if (affin)
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

  /* the groups of more than one value, and the registers they took */
  uint32_t *gsize = xcalloc(n ? n : 1, sizeof(uint32_t));
  for (uint32_t v = 0; v < n; v++) if (wants[v]) gsize[RA2A_FIND(v)]++;
  uint32_t gclaim[64] = { 0 }, fclaim[64] = { 0 };
  /* a group one of whose values crosses a call takes a callee-saved
     register for all of them: a loop's param whose copy ahead of the
     call is what crosses it, in the same register, moves nothing */
  uint8_t *ghot = xcalloc(n ? n : 1, 1);
  if (!warmc_getenv("WARMC_RA2_NO_GHOT"))
    for (uint32_t v = 0; v < n; v++) if (wants[v] && xhot[v]) ghot[RA2A_FIND(v)] = 1;
  if (warmc_getenv("WARMC_RA2_NO_CLAIM")) gsize[0] = gsize[0];

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
                           c ? fclaim : gclaim, ghot[RA2A_FIND(x)]);
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
                             ghot[RA2A_FIND(u)]);
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
  xfree(boff); xfree(doff); xfree(dying); xfree(wants); xfree(grp); xfree(gpref); xfree(gsize); xfree(ghot);
#undef RA2A_FIND
#undef RA2A_UNION
}
