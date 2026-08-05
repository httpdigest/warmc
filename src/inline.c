#include "ir.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INLINE_MAX_CALLEE 90
#define INLINE_MAX_GROWTH 2500
#define INLINE_TINY 12
#define INLINE_RETCALLS 1
#define INLINE_RETCALL_COST 16
#define INLINE_RETCALL_TOTAL 8
#define INLINE_DEPTH 4
#define INLINE_MAX_DEPTH 8
#define INLINE_ROUND2_GROWTH 600

/* Site scoring: the value of removing one call, before dividing by what the
   splice costs. A constant argument is worth more than the instructions it
   feeds because pass_fold has not run yet -- the site's I_CONSTs are all the
   evidence there is. A called-once callee dominates everything: its
   out-of-line body dies with the last call to it. */
#define INL_C_BASE 12
#define INL_C_CONST 4
#define INL_C_FRAMEONLY 6
#define INL_C_ONCE 1000

/* The 2026-08-07 cap sweeps needed a rebuild per point; every future sweep
   is an environment variable. Defaults reproduce the constants above. */
static pthread_once_t g_inl_once = PTHREAD_ONCE_INIT;
static struct {
  uint32_t max_callee, growth, tiny;
  uint32_t retcalls, retcall_cost, retcall_total, depth, round2_growth;
  long only, upto, max;
  bool dbg, noscore, leafonly, notrans, round2;
} g_inl;

static void inl_init(void) {
  const char *e;
  g_inl.max_callee =
      (e = getenv("WARMC_INLINE_MAX_CALLEE")) ? (uint32_t)atol(e)
                                              : INLINE_MAX_CALLEE;
  g_inl.growth =
      (e = getenv("WARMC_INLINE_GROWTH")) ? (uint32_t)atol(e)
                                          : INLINE_MAX_GROWTH;
  g_inl.tiny =
      (e = getenv("WARMC_INLINE_TINY")) ? (uint32_t)atol(e) : INLINE_TINY;
  g_inl.retcalls = (e = getenv("WARMC_INLINE_RETCALLS")) ? (uint32_t)atol(e)
                                                         : INLINE_RETCALLS;
  g_inl.retcall_cost = (e = getenv("WARMC_INLINE_RETCALL_COST"))
                           ? (uint32_t)atol(e)
                           : INLINE_RETCALL_COST;
  g_inl.retcall_total = (e = getenv("WARMC_INLINE_RETCALL_TOTAL"))
                            ? (uint32_t)atol(e)
                            : INLINE_RETCALL_TOTAL;
  g_inl.depth =
      (e = getenv("WARMC_INLINE_DEPTH")) ? (uint32_t)atol(e) : INLINE_DEPTH;
  if (g_inl.depth > INLINE_MAX_DEPTH) g_inl.depth = INLINE_MAX_DEPTH;
  g_inl.only = (e = getenv("WARMC_INLINE_ONLY")) ? atol(e) : -1;
  g_inl.upto = (e = getenv("WARMC_INLINE_UPTO")) ? atol(e) : -1;
  g_inl.max = (e = getenv("WARMC_INLINE_MAX")) ? atol(e) : -1;
  g_inl.round2_growth = (e = getenv("WARMC_INLINE_ROUND2_GROWTH"))
                            ? (uint32_t)atol(e)
                            : INLINE_ROUND2_GROWTH;
  g_inl.dbg = getenv("WARMC_INLINE_DEBUG") != NULL;
  g_inl.noscore = getenv("WARMC_INLINE_NOSCORE") != NULL;
  g_inl.leafonly = getenv("WARMC_INLINE_LEAFONLY") != NULL;
  g_inl.notrans = getenv("WARMC_NO_INLINE_TRANS") != NULL;
  g_inl.round2 = getenv("WARMC_INLINE_ROUND2") != NULL;
}

static void inl_dbg(IrFunc *f, uint32_t fidx, uint32_t bi, const char *what) {
  if (!g_inl.dbg) return;
  fprintf(stderr, "inline %s: callee %s at block %u depth %u: %s\n",
          f->m->funcs[f->func_index].sym ? f->m->funcs[f->func_index].sym : "?",
          f->m->funcs[fidx].sym ? f->m->funcs[fidx].sym : "?", bi,
          f->blocks[bi].loop_depth, what);
}

static uint32_t body_bytes(Module *m, uint32_t fi) {
  Func *fn = &m->funcs[fi];
  if (fn->imported || !fn->body) return 0xffffffffu;
  return (uint32_t)(fn->body_end - fn->body);
}

static bool raises(uint16_t op) {
  return op == I_CALL || op == I_CALL_INDIRECT || op == I_CALL_REF ||
         op == I_THROW || op == I_THROW_REF;
}

static void split_at_raising_sites(IrFunc *g) {

  for (uint32_t bi = 0; bi < g->nblocks; bi++) {
    Block *b = &g->blocks[bi];
    uint32_t at = 0;
    bool found = false;
    for (uint32_t i = 0; i + 1 < b->ninsts; i++)
      if (raises(g->insts[b->insts[i]].op)) { at = i; found = true; break; }
    if (!found) continue;

    uint32_t nb = ir_new_block(g);
    b = &g->blocks[bi];
    Block *n = &g->blocks[nb];
    uint32_t rest = b->ninsts - (at + 1);
    if (rest) {
      n->insts = ir_alloc(g, rest * sizeof(uint32_t));
      memcpy(n->insts, b->insts + at + 1, rest * sizeof(uint32_t));
      n->ninsts = n->cap_insts = rest;
      for (uint32_t i = 0; i < rest; i++) g->insts[n->insts[i]].block = nb;
    }
    b->ninsts = at + 1;

    n->term = b->term; n->term_arg = b->term_arg; n->term_imm = b->term_imm;
    n->succs = b->succs; n->nsuccs = b->nsuccs; n->cap_succs = b->cap_succs;
    n->ret_args = b->ret_args; n->nret = b->nret; n->cap_ret = b->cap_ret;
    b->succs = NULL; b->nsuccs = b->cap_succs = 0;
    b->ret_args = NULL; b->nret = b->cap_ret = 0;
    n->sealed = n->filled = n->reachable = true;
    b->term = T_JUMP;
    ir_add_edge(g, bi, nb, NULL, 0);
  }
  ir_order(g);
}

static void splice(IrFunc *f, IrFunc *g, uint32_t bi, uint32_t cont,
                   const Ref *callargs, uint32_t nargs,
                   const Edge *ehe, uint32_t nehe) {
  uint32_t *bmap = xmalloc(g->nblocks * sizeof(uint32_t));
  Ref *vmap = xmalloc((g->ninsts ? g->ninsts : 1) * sizeof(Ref));
  for (uint32_t i = 0; i < g->ninsts; i++) vmap[i] = REF_NONE;

  for (uint32_t i = 0; i < g->nblocks; i++) bmap[i] = ir_new_block(f);

  for (uint32_t i = 0; i < g->nblocks; i++)
    for (uint32_t p = 0; p < g->blocks[i].nparams; p++) {
      Ref src = g->blocks[i].params[p];
      vmap[src] = ir_add_param(f, bmap[i], (ValType)g->insts[src].type);
    }

  Ref *tmp = xmalloc(64 * sizeof(Ref));
  uint32_t tmpcap = 64;

  uint32_t *corder = xmalloc(g->nblocks * sizeof(uint32_t));
  uint8_t *ordered = xcalloc(g->nblocks ? g->nblocks : 1, 1);
  uint32_t ncorder = 0;
  for (uint32_t oi = 0; oi < g->norder; oi++) {
    corder[ncorder++] = g->order[oi];
    ordered[g->order[oi]] = 1;
  }
  for (uint32_t i = 0; i < g->nblocks; i++)
    if (!ordered[i]) corder[ncorder++] = i;
  xfree(ordered);
  for (uint32_t ci = 0; ci < ncorder; ci++) {
    uint32_t i = corder[ci];
    Block *gb = &g->blocks[i];
    for (uint32_t k = 0; k < gb->ninsts; k++) {
      Ref src = gb->insts[k];
      Inst *si = &g->insts[src];
      if (si->op == I_PARAM) continue;
      if (si->nargs > tmpcap) {
        tmpcap = si->nargs * 2;
        tmp = xrealloc(tmp, tmpcap * sizeof(Ref));
      }
      Ref *sa = ir_args(g, src);
      for (uint32_t j = 0; j < si->nargs; j++) {
        Ref mapped = vmap[sa[j]];
        if (mapped == REF_NONE)
          warmc_fatal("internal: inlining saw a use before its definition");
        tmp[j] = mapped;
      }
      vmap[src] = ir_emit(f, bmap[i], (IrOp)si->op, (ValType)si->type,
                          tmp, si->nargs, si->imm, si->imm2);
    }
  }

  xfree(corder);
  for (uint32_t i = 0; i < g->nblocks; i++) {
    Block *gb = &g->blocks[i];
    uint32_t nb = bmap[i];
    switch (gb->term) {
      case T_RETURN: {
        if (gb->nret > tmpcap) {
          tmpcap = gb->nret * 2;
          tmp = xrealloc(tmp, tmpcap * sizeof(Ref));
        }
        for (uint32_t j = 0; j < gb->nret; j++) tmp[j] = vmap[gb->ret_args[j]];
        f->blocks[nb].term = T_JUMP;
        ir_add_edge(f, nb, cont, tmp, gb->nret);
        break;
      }
      case T_TRAP:
        f->blocks[nb].term = T_TRAP;
        f->blocks[nb].term_imm = gb->term_imm;
        break;
      case T_JUMP: case T_BRIF: case T_BRTABLE: {
        f->blocks[nb].term = gb->term;
        if (gb->term != T_JUMP) f->blocks[nb].term_arg = vmap[gb->term_arg];
        for (uint32_t s = 0; s < gb->nsuccs; s++) {
          Edge *e = &gb->succs[s];
          if (e->nargs > tmpcap) {
            tmpcap = e->nargs * 2;
            tmp = xrealloc(tmp, tmpcap * sizeof(Ref));
          }
          for (uint32_t j = 0; j < e->nargs; j++) tmp[j] = vmap[e->args[j]];
          ir_add_edge(f, nb, bmap[e->target], tmp, e->nargs);
        }
        break;
      }
      default:
        f->blocks[nb].term = T_TRAP;
        f->blocks[nb].term_imm = TRAP_UNREACHABLE;
        break;
    }

    if (nehe && gb->ninsts && raises(g->insts[gb->insts[gb->ninsts - 1]].op))
      for (uint32_t e = 0; e < nehe; e++) {
        ir_add_edge(f, nb, ehe[e].target, ehe[e].args, ehe[e].nargs);
        f->blocks[nb].succs[f->blocks[nb].nsuccs - 1].eh = true;
      }
  }

  f->blocks[bi].term = T_JUMP;
  ir_add_edge(f, bi, bmap[g->entry], callargs, nargs);

  xfree(bmap); xfree(vmap); xfree(tmp);
}

static bool has_tail_call(IrFunc *g) {
  for (uint32_t bi = 0; bi < g->nblocks; bi++)
    for (uint32_t i = 0; i < g->blocks[bi].ninsts; i++) {
      uint16_t op = g->insts[g->blocks[bi].insts[i]].op;

      if (op == I_RETURN_CALL || op == I_RETURN_CALL_INDIRECT ||
          op == I_RETURN_CALL_REF) return true;
    }
  return false;
}

/* The old leaf gate, kept verbatim behind WARMC_INLINE_LEAFONLY. */
static bool callee_calls_are_cold(IrFunc *g) {
  for (uint32_t bi = 0; bi < g->nblocks; bi++) {
    Block *b = &g->blocks[bi];
    if (!b->reachable || b->term == T_TRAP) continue;
    for (uint32_t i = 0; i < b->ninsts; i++) {
      switch (g->insts[b->insts[i]].op) {
        case I_CALL: case I_CALL_INDIRECT: case I_CALL_REF:
        case I_RETURN_CALL: case I_RETURN_CALL_INDIRECT: case I_RETURN_CALL_REF:
          return false;
        default: break;
      }
    }
  }
  return true;
}

/* Returning calls the caller will actually cross: reachable, warm, not on a
   panic path. The old gate's trap-block exemption widens to diverging
   blocks, the same cold-path discount CSA prices with. */
static uint32_t warm_returning_calls(IrFunc *g) {
  uint32_t n = 0;
  for (uint32_t bi = 0; bi < g->nblocks; bi++) {
    Block *b = &g->blocks[bi];
    if (!b->reachable || b->term == T_TRAP || b->diverges) continue;
    for (uint32_t i = 0; i < b->ninsts; i++) {
      uint16_t op = g->insts[b->insts[i]].op;
      if (op == I_CALL || op == I_CALL_INDIRECT || op == I_CALL_REF) n++;
    }
  }
  return n;
}

static bool inline_flat(IrFunc *f, IrFunc *g, uint32_t bi, uint32_t at,
                        Ref call, const Ref *callargs, uint32_t nargs,
                        CompType *sig) {
  Block *gb = &g->blocks[g->entry];
  if (gb->nparams != nargs) return false;

  Ref *vmap = xmalloc((g->ninsts ? g->ninsts : 1) * sizeof(Ref));
  for (uint32_t i = 0; i < g->ninsts; i++) vmap[i] = REF_NONE;

  for (uint32_t p = 0; p < gb->nparams; p++) vmap[gb->params[p]] = callargs[p];

  uint32_t before = f->blocks[bi].ninsts;
  Ref *tmp = NULL;
  uint32_t tmpcap = 0;
  for (uint32_t k = 0; k < gb->ninsts; k++) {
    Ref src = gb->insts[k];
    Inst *si = &g->insts[src];
    if (si->op == I_PARAM) continue;
    if (si->nargs > tmpcap) {
      tmpcap = si->nargs * 2 + 4;
      tmp = xrealloc(tmp, tmpcap * sizeof(Ref));
    }
    Ref *sa = ir_args(g, src);
    for (uint32_t j = 0; j < si->nargs; j++) {
      Ref mapped = vmap[sa[j]];
      if (mapped == REF_NONE)
        warmc_fatal("internal: flat inlining saw a use before its definition");
      tmp[j] = mapped;
    }
    vmap[src] = ir_emit(f, bi, (IrOp)si->op, (ValType)si->type,
                        tmp, si->nargs, si->imm, si->imm2);
  }
  xfree(tmp);

  Block *b = &f->blocks[bi];
  uint32_t added = b->ninsts - before;
  uint32_t ntail = before - (at + 1);
  uint32_t total = at + added + ntail;
  uint32_t *ni = ir_alloc(f, (total ? total : 1) * sizeof(uint32_t));
  memcpy(ni, b->insts, at * sizeof(uint32_t));
  memcpy(ni + at, b->insts + before, added * sizeof(uint32_t));
  memcpy(ni + at + added, b->insts + at + 1, ntail * sizeof(uint32_t));
  b->insts = ni;
  b->ninsts = b->cap_insts = total;

  if (sig->nresults == 1) {
    ir_replace(f, call, vmap[gb->ret_args[0]]);
  } else if (sig->nresults > 1) {
    for (Ref r = 0; r < f->ninsts; r++) {
      if (f->insts[r].op != I_RESULT || f->insts[r].nargs < 1) continue;
      if (ir_args(f, r)[0] != call) continue;
      uint32_t slot = (uint32_t)f->insts[r].imm;
      ir_replace(f, r, vmap[gb->ret_args[slot]]);
      f->insts[r].op = I_NOP;
    }
  }
  f->insts[call].op = I_NOP;
  xfree(vmap);
  return true;
}

/* One verdict and at most two IR builds per callee per caller, however many
   sites look at it. The two builds are the two shapes a site can need:
   sites without exceptional edges splice the callee as built, sites with
   them splice it split at raising instructions -- both read-only to the
   splice, so one copy serves every site of its kind. Size is not part of
   the verdict: the cap depends on who is asking (a called-once callee may
   be growth-sized), so it lives with the site. */
typedef struct {
  uint8_t verdict;
  IrFunc *plain, *split;
  uint32_t ninsts, eparams, retcalls;
} InlMemo;

static IrFunc *inl_callee(IrFunc *f, const OptOpts *o, InlMemo *memo,
                          uint32_t fidx, uint32_t bi, bool want_split) {
  Module *m = f->m;
  InlMemo *e = &memo[fidx];
  if (e->verdict == 0) {
    e->verdict = 2;
    IrFunc *g = ir_build(m, fidx, o);
    uint32_t r = warm_returning_calls(g);
    if (has_tail_call(g)) {
      g_stats.inl_rej_tailcall++;
      inl_dbg(f, fidx, bi, "tail call");
      ir_free(g);
    } else if (g_inl.leafonly ? !callee_calls_are_cold(g)
                              : r > g_inl.retcalls) {
      g_stats.inl_rej_retcalls++;
      inl_dbg(f, fidx, bi, "returning calls");
      ir_free(g);
    } else if (g->npads) {
      g_stats.inl_rej_pads++;
      inl_dbg(f, fidx, bi, "landing pads");
      ir_free(g);
    } else {
      e->verdict = 1;
      ir_order(g);
      e->plain = g;
      e->ninsts = g->ninsts;
      e->eparams = g->blocks[g->entry].nparams;
      e->retcalls = r;
    }
  }
  if (e->verdict != 1) return NULL;
  if (!want_split) return e->plain;
  if (!e->split) {
    e->split = ir_build(m, fidx, o);
    split_at_raising_sites(e->split);
  }
  return e->split;
}

typedef struct {
  Ref call;
  uint32_t fidx;
  uint32_t charged, score;
  uint32_t rpo, idx;
  bool once;
  uint16_t idepth;
  uint32_t anc[INLINE_MAX_DEPTH];
} InlSite;

typedef struct {
  InlSite *v;
  uint32_t n, cap;
} InlVec;

static void inl_push(InlVec *s, const InlSite *site) {
  if (s->n == s->cap) {
    s->cap = s->cap ? s->cap * 2 : 16;
    s->v = xrealloc(s->v, s->cap * sizeof(InlSite));
  }
  s->v[s->n++] = *site;
}

static int inl_site_cmp(const void *pa, const void *pb) {
  const InlSite *x = pa, *y = pb;
  if (x->score != y->score) return x->score > y->score ? -1 : 1;
  if (x->rpo != y->rpo) return x->rpo < y->rpo ? -1 : 1;
  return x->idx < y->idx ? -1 : x->idx > y->idx ? 1 : 0;
}

/* Judge one site and fill in its score. Charged is the splice cost the
   budget pays: the callee's raw instruction count overcharges, since entry
   params substitute away and instructions fed by constant arguments fold in
   the rounds right after, so both are discounted, floored so nothing is
   ever free. A transitive site carries the chain of callees it was copied
   out of; a candidate already on its own chain is a cycle. */
static bool inl_score_site(IrFunc *f, const OptOpts *o, InlMemo *memo,
                           Ref call, uint32_t ord1, uint32_t ord2,
                           const InlSite *parent, InlSite *out) {
  Module *m = f->m;
  static const uint32_t w[4] = {1, 10, 100, 1000};
  Inst *cin = &f->insts[call];
  if (cin->op != I_CALL) return false;
  uint32_t fidx = (uint32_t)cin->imm;
  if (fidx == f->func_index) return false;
  if (m->funcs[fidx].imported) return false;
  uint32_t bi = cin->block;
  Block *b = &f->blocks[bi];

  uint16_t idepth = 0;
  if (parent) {
    if ((uint32_t)parent->idepth + 1 > g_inl.depth) {
      g_stats.inl_rej_depth++;
      return false;
    }
    if (fidx == parent->fidx) {
      g_stats.inl_rej_recursion++;
      inl_dbg(f, fidx, bi, "on its own inline chain");
      return false;
    }
    for (uint16_t j = 0; j < parent->idepth; j++)
      if (parent->anc[j] == fidx) {
        g_stats.inl_rej_recursion++;
        inl_dbg(f, fidx, bi, "on its own inline chain");
        return false;
      }
    idepth = parent->idepth + 1;
  }

  bool once = opt_direct_callsite_count(m, fidx) == 1;
  uint32_t lim = once ? g_inl.growth : g_inl.max_callee;
  if (body_bytes(m, fidx) > 8 * lim) {
    g_stats.inl_rej_size++;
    return false;
  }
  if (!inl_callee(f, o, memo, fidx, bi, false)) return false;
  InlMemo *e = &memo[fidx];
  if (e->ninsts > lim) {
    g_stats.inl_rej_size++;
    inl_dbg(f, fidx, bi, "too big");
    return false;
  }

  uint32_t nconst = 0;
  Ref *a = ir_args(f, call);
  for (uint32_t j = 0; j < cin->nargs; j++)
    if (f->insts[a[j]].op == I_CONST) nconst++;
  bool fonly = opt_call_frame_only(m, fidx);
  uint32_t sub = e->eparams + 2 * nconst;
  uint32_t charged = e->ninsts > sub + 4 ? e->ninsts - sub : 4;

  /* A surviving call crosses the site: everything live around it pays
     CSA's save/restore there, doubled in loops where those frame ops are
     loop-priced. In a loop, only take the crossing when something says
     this site is special -- a dying body, a folding argument, a callee
     that touches nothing. */
  if (e->retcalls) {
    if (b->loop_depth >= 1 && !once && nconst == 0 && !fonly) {
      g_stats.inl_rej_retcalls++;
      inl_dbg(f, fidx, bi, "retcall in loop without evidence");
      return false;
    }
    charged += e->retcalls * g_inl.retcall_cost * (b->loop_depth ? 2 : 1);
  }

  if (b->diverges && charged > g_inl.tiny) {
    g_stats.inl_rej_score++;
    inl_dbg(f, fidx, bi, "cold and not tiny");
    return false;
  }

  uint32_t benefit = INL_C_BASE + INL_C_CONST * nconst +
                     (fonly ? INL_C_FRAMEONLY : 0) + (once ? INL_C_ONCE : 0);
  uint32_t d = b->loop_depth < 3 ? b->loop_depth : 3;
  uint32_t score = b->diverges ? 0 : w[d] * benefit * 16 / charged;
  if (g_inl.noscore) score = 1;

  memset(out, 0, sizeof(*out));
  out->call = call;
  out->fidx = fidx;
  out->charged = charged;
  out->score = score;
  out->rpo = ord1;
  out->idx = ord2;
  out->once = once;
  out->idepth = idepth;
  if (parent) {
    memcpy(out->anc, parent->anc, parent->idepth * sizeof(uint32_t));
    out->anc[parent->idepth] = parent->fidx;
  }
  return true;
}

static void inl_collect(IrFunc *f, const OptOpts *o, InlMemo *memo,
                        InlVec *sites) {
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t k = 0; k < b->ninsts; k++) {
      InlSite s;
      if (f->insts[b->insts[k]].op != I_CALL) continue;
      if (inl_score_site(f, o, memo, b->insts[k], oi, k, NULL, &s))
        inl_push(sites, &s);
    }
  }
}

static bool inl_apply(IrFunc *f, const OptOpts *o, InlMemo *memo,
                      const InlSite *site, uint32_t *budget,
                      uint32_t *retsum) {
  Module *m = f->m;
  Ref call = site->call;
  Inst *cin = &f->insts[call];
  if (cin->op != I_CALL || (uint32_t)cin->imm != site->fidx) return false;
  uint32_t fidx = site->fidx;
  uint32_t bi = cin->block;
  Block *b = &f->blocks[bi];
  if (!b->reachable) return false;
  uint32_t k = 0;
  while (k < b->ninsts && b->insts[k] != call) k++;
  if (k == b->ninsts) return false;

  uint32_t nehe = 0;
  bool any_eh = false;
  for (uint32_t s = 0; s < b->nsuccs; s++)
    if (b->succs[s].eh) {
      any_eh = true;
      if (k + 1 == b->ninsts) nehe++;
    }

  IrFunc *g = inl_callee(f, o, memo, fidx, bi, nehe > 0);
  if (!g) return false;
  if (site->charged > *budget) {
    g_stats.inl_rej_budget++;
    return false;
  }

  /* the frame-cap pressure valve: surviving calls a caller absorbs */
  if (memo[fidx].retcalls &&
      *retsum + memo[fidx].retcalls > g_inl.retcall_total) {
    g_stats.inl_rej_retcalls++;
    inl_dbg(f, fidx, bi, "caller absorbed enough calls");
    return false;
  }

  CompType *sig = &m->types[m->funcs[fidx].type_index];
  uint32_t nargs = cin->nargs;
  Ref *callargs = xmalloc((nargs ? nargs : 1) * sizeof(Ref));
  memcpy(callargs, ir_args(f, call), nargs * sizeof(Ref));

  if (site->once && memo[fidx].ninsts > g_inl.max_callee)
    g_stats.inl_once_forced++;
  if (memo[fidx].retcalls) g_stats.inl_retcall_sites++;
  *retsum += memo[fidx].retcalls;

  if (!nehe && !any_eh && g->norder == 1 && g->order[0] == g->entry &&
      g->blocks[g->entry].term == T_RETURN &&
      inline_flat(f, g, bi, k, call, callargs, nargs, sig)) {
    *budget -= site->charged;
    g_stats.inlined++;
    g_stats.inl_flat++;
    inl_dbg(f, fidx, bi, "inlined flat");
    xfree(callargs);
    ir_apply_subst(f);
    ir_order(f);
    return true;
  }

  uint32_t cont = ir_new_block(f);
  Ref *contp = xmalloc((sig->nresults ? sig->nresults : 1) * sizeof(Ref));
  for (uint32_t i = 0; i < sig->nresults; i++)
    contp[i] = ir_add_param(f, cont, sig->results[i]);

  b = &f->blocks[bi];
  Block *cb = &f->blocks[cont];
  for (uint32_t i = k + 1; i < b->ninsts; i++) {
    Ref mv = b->insts[i];
    f->insts[mv].block = cont;
    if (cb->ninsts == cb->cap_insts) {
      uint32_t oc_ = cb->cap_insts;
      cb->cap_insts = cb->cap_insts ? cb->cap_insts * 2 : 8;
      cb->insts = ir_grow(f, cb->insts, (size_t)oc_ * sizeof(uint32_t),
                          (size_t)cb->cap_insts * sizeof(uint32_t));
    }
    cb->insts[cb->ninsts++] = mv;
  }
  b->ninsts = k;

  cb->term = b->term;
  cb->term_arg = b->term_arg;
  cb->term_imm = b->term_imm;
  cb->succs = b->succs; cb->nsuccs = b->nsuccs; cb->cap_succs = b->cap_succs;
  cb->ret_args = b->ret_args; cb->nret = b->nret; cb->cap_ret = b->cap_ret;
  b->succs = NULL; b->nsuccs = b->cap_succs = 0;
  b->ret_args = NULL; b->nret = b->cap_ret = 0;
  b->term = T_NONE;

  Edge *ehe = nehe ? xmalloc(nehe * sizeof(Edge)) : NULL;
  if (nehe) {
    uint32_t keep = 0, got = 0;
    for (uint32_t s = 0; s < cb->nsuccs; s++) {
      if (cb->succs[s].eh) ehe[got++] = cb->succs[s];
      else cb->succs[keep++] = cb->succs[s];
    }
    cb->nsuccs = keep;
  }

  if (sig->nresults == 1) {
    ir_replace(f, call, contp[0]);
  } else if (sig->nresults > 1) {
    for (Ref r = 0; r < f->ninsts; r++) {
      if (f->insts[r].op != I_RESULT || f->insts[r].nargs < 1) continue;
      if (ir_args(f, r)[0] != call) continue;
      uint32_t slot = (uint32_t)f->insts[r].imm;
      ir_replace(f, r, contp[slot]);
      f->insts[r].op = I_NOP;
    }
  }

  splice(f, g, bi, cont, callargs, nargs, ehe, nehe);

  xfree(ehe);
  *budget -= site->charged;
  g_stats.inlined++;
  g_stats.inl_spliced++;
  inl_dbg(f, fidx, bi, "inlined spliced");
  xfree(callargs); xfree(contp);
  ir_apply_subst(f);
  ir_order(f);
  return true;
}

static bool inl_run(IrFunc *f, const OptOpts *o, uint32_t budget0) {
  Module *m = f->m;
  InlMemo *memo = xcalloc(m->nfuncs ? m->nfuncs : 1, sizeof(InlMemo));
  InlVec sites = {0};
  inl_collect(f, o, memo, &sites);
  qsort(sites.v, sites.n, sizeof(InlSite), inl_site_cmp);

  uint32_t budget = budget0;
  uint32_t retsum = 0;
  long events = 0;
  bool any = false;
  for (uint32_t i = 0; i < sites.n && budget > 0; i++) {
    if (g_inl.max >= 0 && events >= g_inl.max) break;
    InlSite site = sites.v[i];
    uint32_t before = f->ninsts;
    if (!inl_apply(f, o, memo, &site, &budget, &retsum)) continue;
    any = true;
    events++;
    if (site.idepth) g_stats.inl_transitive++;

    /* the splice may have copied calls in; they are sites too */
    if (g_inl.notrans) continue;
    uint32_t had = sites.n;
    for (Ref v = (Ref)before; v < (Ref)f->ninsts; v++) {
      if (f->insts[v].op != I_CALL) continue;
      InlSite ns;
      if (inl_score_site(f, o, memo, v, f->insts[v].block, v, &site, &ns))
        inl_push(&sites, &ns);
    }
    if (sites.n != had)
      qsort(sites.v + i + 1, sites.n - (i + 1), sizeof(InlSite),
            inl_site_cmp);
  }

  xfree(sites.v);
  for (uint32_t fi = 0; fi < m->nfuncs; fi++) {
    if (memo[fi].plain) ir_free(memo[fi].plain);
    if (memo[fi].split) ir_free(memo[fi].split);
  }
  xfree(memo);
  return any;
}

bool pass_inline(IrFunc *f, const OptOpts *o) {
  pthread_once(&g_inl_once, inl_init);
  if (g_inl.only >= 0 && f->func_index != (uint32_t)g_inl.only) return false;
  if (g_inl.upto >= 0 && f->func_index > (uint32_t)g_inl.upto) return false;
  return inl_run(f, o, g_inl.growth);
}

/* A second, smaller helping after the first fold rounds: constant
   arguments only become I_CONST at sites once pass_fold has run, and the
   post-inline devirt pass may have minted new direct calls. Off unless
   WARMC_INLINE_ROUND2 says otherwise. */
bool pass_inline_round2(IrFunc *f, const OptOpts *o) {
  pthread_once(&g_inl_once, inl_init);
  if (!g_inl.round2) return false;
  if (g_inl.only >= 0 && f->func_index != (uint32_t)g_inl.only) return false;
  if (g_inl.upto >= 0 && f->func_index > (uint32_t)g_inl.upto) return false;
  return inl_run(f, o, g_inl.round2_growth);
}
