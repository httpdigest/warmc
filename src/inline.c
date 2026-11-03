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
/* the most instructions a partial inline takes in: the start of a callee
   up to where it would call or loop (pinl_build) */
#define PINLINE_MAX 20

/* Site scoring: the value of removing one call, before dividing by what the
   splice costs. A constant argument is worth more than the instructions it
   feeds because pass_fold has not run yet -- the site's I_CONSTs are all the
   evidence there is. A called-once callee dominates everything: its
   out-of-line body dies with the last call to it. */
#define INL_C_BASE 12
#define INL_C_CONST 4
#define INL_C_FRAMEONLY 6
#define INL_C_ONCE 1000

/* Escape evidence: a site that is the only way out for an allocation, into
   a callee that keeps the object to itself, is the one inline that lets
   pass_escape remove the allocation.  Such a site may take a callee making
   up to INL_ESC_RETCALLS calls of its own, is taken from a cold block when
   the object is allocated on a warm one, is scored by the allocation's loop
   depth, and may go INL_ESC_SLACK surviving calls past the caller's cap. */
#define INL_C_ESCAPE 24
#define INL_ESC_RETCALLS 4
#define INL_ESC_SLACK 8
#define INL_ESC_MAX_ELEMS 32
#define INL_ESC_MAX_ROOTS 256

/* A parameter is followed this far down the direct calls a callee hands it
   to; a dispatch on one is recorded for the first INL_DISP_PARAMS. */
#define INL_KEEP_DEPTH 3
#define INL_DISP_PARAMS 8
#define INL_ICALLS 8
#define INL_NO_DISP 0xffffffffu

/* The 2026-08-07 cap sweeps needed a rebuild per point; every future sweep
   is an environment variable. Defaults reproduce the constants above. */
static pthread_once_t g_inl_once = PTHREAD_ONCE_INIT;
static struct {
  uint32_t max_callee, growth, tiny;
  uint32_t retcalls, retcall_cost, retcall_total, depth, round2_growth;
  uint32_t partial_max;
  long only, upto, max;
  bool dbg, noscore, leafonly, notrans, notail, round2, nopartial;
} g_inl;

static void inl_init(void) {
  const char *e;
  g_inl.max_callee =
      (e = warmc_getenv("WARMC_INLINE_MAX_CALLEE")) ? (uint32_t)atol(e)
                                              : INLINE_MAX_CALLEE;
  g_inl.growth =
      (e = warmc_getenv("WARMC_INLINE_GROWTH")) ? (uint32_t)atol(e)
                                          : INLINE_MAX_GROWTH;
  g_inl.tiny =
      (e = warmc_getenv("WARMC_INLINE_TINY")) ? (uint32_t)atol(e) : INLINE_TINY;
  g_inl.retcalls = (e = warmc_getenv("WARMC_INLINE_RETCALLS")) ? (uint32_t)atol(e)
                                                         : INLINE_RETCALLS;
  g_inl.retcall_cost = (e = warmc_getenv("WARMC_INLINE_RETCALL_COST"))
                           ? (uint32_t)atol(e)
                           : INLINE_RETCALL_COST;
  g_inl.retcall_total = (e = warmc_getenv("WARMC_INLINE_RETCALL_TOTAL"))
                            ? (uint32_t)atol(e)
                            : INLINE_RETCALL_TOTAL;
  g_inl.depth =
      (e = warmc_getenv("WARMC_INLINE_DEPTH")) ? (uint32_t)atol(e) : INLINE_DEPTH;
  if (g_inl.depth > INLINE_MAX_DEPTH) g_inl.depth = INLINE_MAX_DEPTH;
  g_inl.only = (e = warmc_getenv("WARMC_INLINE_ONLY")) ? atol(e) : -1;
  g_inl.upto = (e = warmc_getenv("WARMC_INLINE_UPTO")) ? atol(e) : -1;
  g_inl.max = (e = warmc_getenv("WARMC_INLINE_MAX")) ? atol(e) : -1;
  g_inl.round2_growth = (e = warmc_getenv("WARMC_INLINE_ROUND2_GROWTH"))
                            ? (uint32_t)atol(e)
                            : INLINE_ROUND2_GROWTH;
  g_inl.dbg = warmc_getenv("WARMC_INLINE_DEBUG") != NULL;
  g_inl.noscore = warmc_getenv("WARMC_INLINE_NOSCORE") != NULL;
  g_inl.leafonly = warmc_getenv("WARMC_INLINE_LEAFONLY") != NULL;
  g_inl.notrans = warmc_getenv("WARMC_NO_INLINE_TRANS") != NULL;
  /* the old gate: no callee that makes a tail call */
  g_inl.notail = warmc_getenv("WARMC_INLINE_NO_TAIL") != NULL;
  g_inl.round2 = warmc_getenv("WARMC_INLINE_ROUND2") != NULL;
  g_inl.nopartial = warmc_getenv("WARMC_NO_PARTIAL_INLINE") != NULL;
  g_inl.partial_max = (e = warmc_getenv("WARMC_PARTIAL_INLINE_MAX"))
                          ? (uint32_t)atol(e)
                          : PINLINE_MAX;
}

static const char *inl_name(const Func *fn) {
  return fn->name ? fn->name : fn->sym ? fn->sym : "?";
}

static void inl_dbg(IrFunc *f, uint32_t fidx, uint32_t bi, const char *what) {
  if (!g_inl.dbg) return;
  fprintf(stderr, "inline %s: callee %s at block %u depth %u: %s\n",
          inl_name(&f->m->funcs[f->func_index]), inl_name(&f->m->funcs[fidx]), bi,
          f->blocks[bi].loop_depth, what);
}

static uint32_t body_bytes(Module *m, uint32_t fi) {
  Func *fn = &m->funcs[fi];
  if (fn->imported || !fn->body) return 0xffffffffu;
  return (uint32_t)(fn->body_end - fn->body);
}

static bool raises(uint16_t op) {
  return op == I_CALL || op == I_CALL_INDIRECT || op == I_CALL_REF ||
         op == I_THROW || op == I_THROW_REF || op == I_CONT_OP;
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
          f->blocks[nb].succs[f->blocks[nb].nsuccs - 1].cold = e->cold;
        }
        break;
      }
      default:
        f->blocks[nb].term = T_TRAP;
        f->blocks[nb].term_imm = TRAP_UNREACHABLE;
        break;
    }

    /* a rare way of the callee's is one of the caller's.  (Not every way
       of a callee called on a rare way: that moved the registers of
       javabench/generic's hot loop, which ran 18% slower on the same
       instructions -- held back in the core's execution, with no more cache
       misses, mispredictions or memory-order restarts than before.) */
    f->blocks[nb].cold = gb->cold;
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

/* A callee's tail calls as calls, each followed by the return of what it
   returns, for a splice: inlined, the callee's frame is gone either way, so
   what a tail call saves -- that frame, while what it calls runs -- the
   splice saves too, and the call it leaves is the caller's own call, made
   from the caller's frame as the call of the callee was.  A call that
   gives several results ends its block, as one the builder makes does, so
   that a splice into a try gives it the site's handler edges: the
   results and the return go in a block of their own.  The calls are then
   returning calls like any other, which the caps count. */
static void untail(IrFunc *g) {
  Module *m = g->m;
  bool any = false;
  uint32_t nb = g->nblocks;
  for (uint32_t bi = 0; bi < nb; bi++) {
    Block *b = &g->blocks[bi];
    if (!b->reachable || !b->ninsts) continue;
    Ref call = b->insts[b->ninsts - 1];
    Inst *in = &g->insts[call];
    CompType *sig;
    switch (in->op) {
      case I_RETURN_CALL:
        sig = &m->types[m->funcs[in->imm].type_index];
        in->op = I_CALL;
        break;
      case I_RETURN_CALL_INDIRECT:
        sig = &m->types[in->imm];
        in->op = I_CALL_INDIRECT;
        break;
      case I_RETURN_CALL_REF:
        sig = &m->types[in->imm];
        in->op = I_CALL_REF;
        break;
      default:
        continue;
    }
    any = true;
    uint32_t nres = sig->nresults;
    in->type = nres == 1 ? sig->results[0] : 0;
    uint32_t rb = bi;
    if (nres > 1) {
      rb = ir_new_block(g);
      b = &g->blocks[bi];
      b->term = T_JUMP;
      ir_add_edge(g, bi, rb, NULL, 0);
      Block *r = &g->blocks[rb];
      r->sealed = r->filled = r->reachable = true;
    }
    Ref *ret = ir_alloc(g, (nres ? nres : 1) * sizeof(Ref));
    if (nres == 1) ret[0] = call;
    else
      for (uint32_t i = 0; i < nres; i++) {
        Ref a[1] = { call };
        ret[i] = ir_emit(g, rb, I_RESULT, sig->results[i], a, 1, i, 0);
      }
    Block *r = &g->blocks[rb];
    r->term = T_RETURN;
    r->nret = r->cap_ret = nres;
    r->ret_args = ret;
  }
  if (any) ir_order(g);
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
        case I_CONT_OP:
          return false;
        default: break;
      }
    }
  }
  return true;
}

/* Returning calls the caller will actually cross: reachable, warm, not on a
   panic path. The old gate's trap-block exemption widens to diverging
   blocks, the same cold-path discount CSA prices with.  A call through a
   class initializer's global is none: in the caller pass_once takes it out
   where another went before it or of a loop's copy, and where it cannot,
   the caller makes that call in place of the one to the callee. */
static uint32_t warm_returning_calls(IrFunc *g, uint32_t *in_loop) {
  uint32_t n = 0;
  *in_loop = 0;
  for (uint32_t bi = 0; bi < g->nblocks; bi++) {
    Block *b = &g->blocks[bi];
    if (!b->reachable || b->term == T_TRAP || b->diverges) continue;
    for (uint32_t i = 0; i < b->ninsts; i++) {
      uint16_t op = g->insts[b->insts[i]].op;
      if (op == I_CALL_REF && opt_once_call(g, b->insts[i])) continue;
      if (op == I_CALL || op == I_CALL_INDIRECT || op == I_CALL_REF || op == I_CONT_OP) n++;
      if ((op == I_CALL_INDIRECT || op == I_CALL_REF) && b->loop_depth)
        (*in_loop)++;
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
      ir_make_nop(&f->insts[r]);
    }
  }
  ir_make_nop(&f->insts[call]);
  xfree(vmap);
  return true;
}

/* Follow each root through its aliases -- conversions, and block
   parameters every argument of which is the root, the parameter itself, a
   null, a fresh allocation or what a pad caught -- and sort its uses:
   local (a field or element access, a null check, a test or comparison),
   an argument to a direct call, or an escape.  The roots are allocations
   in a caller and reference parameters in a callee.  escaped[r] is set for
   a root that leaves by anything but a direct call; the direct-call uses
   go in *calls.  Given returned[], a return sets that instead of
   escaped[]; given disp[], so does a call_ref whose target is loaded off
   the root, which is a dispatch on it: disp[r] is the offset of the read
   nearest the root, its vtable.  With cold, a use on a path that never
   returns is no escape, though a direct call there is still a call use:
   partial escape analysis makes the object there, and the warm path goes
   without it. */
typedef struct { Ref call; uint32_t arg, root; } InlCallUse;

/* A value a root may merge with and still leave by the merge's uses only:
   a null, another fresh allocation -- a constructor's `this ?: new` -- or
   the payload of a pad, an exception thrown by a call that inlining the
   root's constructors takes away. */
static bool inl_mergeable(IrFunc *g, Ref a) {
  Inst *in = &g->insts[a];
  if (in->op == I_EXN_PAYLOAD) return g->blocks[in->block].is_pad;
  if (in->op == I_STRUCT_NEW || in->op == I_ARRAY_NEW) return true;
  return vt_is_ref((ValType)in->type) &&
         (in->op == I_REF_NULL || (in->op == I_CONST && in->imm == 0));
}

static bool inl_dispatch_off(IrFunc *g, Ref r, const int32_t *own, uint32_t n,
                             int32_t c, uint32_t *off) {
  for (int d = 0; d < 8; d++) {
    r = ir_resolve(g, r);
    Inst *in = &g->insts[r];
    if ((in->op != I_FIELD_GET && in->op != I_ARRAY_GET &&
         in->op != I_REF_CONVERT) || !in->nargs)
      return false;
    Ref x = ir_resolve(g, ir_args(g, r)[0]);
    if (x < n && own[x] == c) {
      if (in->op != I_FIELD_GET) return false;
      *off = (uint32_t)in->imm;
      return true;
    }
    r = x;
  }
  return false;
}

static void inl_escape_scan(IrFunc *g, const Ref *roots, uint32_t nroots,
                            uint8_t *escaped, uint8_t *returned,
                            uint32_t *disp, bool cold, InlCallUse **calls,
                            uint32_t *ncalls) {
  uint32_t n = g->ninsts;
  int32_t *own = xmalloc((n ? n : 1) * sizeof(int32_t));
  for (uint32_t v = 0; v < n; v++) own[v] = -1;
  for (uint32_t r = 0; r < nroots; r++) own[roots[r]] = (int32_t)r;
#define IOWN(x_) ((uint32_t)(x_) < n ? own[(x_)] : -1)
  IrEdgeIdx ei = ir_edge_index(g);
  for (bool again = true; again;) {
    again = false;
    for (uint32_t oi = 0; oi < g->norder; oi++) {
      uint32_t bi = g->order[oi];
      Block *b = &g->blocks[bi];
      if (bi != g->entry)
        for (uint32_t p = 0; p < b->nparams; p++) {
          Ref P = b->params[p];
          if (P >= n || own[P] >= 0 || !vt_is_ref(g->insts[P].type)) continue;
          int32_t one = -1;
          bool other = false;
          for (uint32_t k = ei.off[bi]; k < ei.off[bi + 1] && !other; k++) {
            Block *sb = &g->blocks[ei.src[k]];
            if (!sb->reachable || sb->succs[ei.si[k]].eh) continue;
            Ref a = ir_resolve(g, sb->succs[ei.si[k]].args[p]);
            if (a == P) continue;
            int32_t c = IOWN(a);
            if (c < 0 && inl_mergeable(g, a)) continue;
            if (c < 0 || (one >= 0 && c != one)) other = true;
            else one = c;
          }
          if (!other && one >= 0) { own[P] = one; again = true; }
        }
      for (uint32_t i = 0; i < b->ninsts; i++) {
        Ref v = b->insts[i];
        if (v < n && own[v] < 0 && g->insts[v].op == I_REF_CONVERT &&
            g->insts[v].nargs == 1) {
          int32_t c = IOWN(ir_resolve(g, ir_args(g, v)[0]));
          if (c >= 0) { own[v] = c; again = true; }
        }
      }
    }
  }

  uint32_t cap = 0;
  for (uint32_t oi = 0; oi < g->norder; oi++) {
    uint32_t bi = g->order[oi];
    Block *b = &g->blocks[bi];
    bool lenient = cold && b->diverges;
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      Inst *in = &g->insts[v];
      Ref *a = ir_args(g, v);
      for (uint32_t j = 0; j < in->nargs; j++) {
        int32_t c = IOWN(ir_resolve(g, a[j]));
        if (c < 0) continue;
        bool local = false;
        switch (in->op) {
          case I_FIELD_GET: case I_FIELD_SET:
          case I_ARRAY_GET: case I_ARRAY_SET: local = j == 0; break;
          case I_ARRAY_LEN: case I_REF_IS_NULL: case I_REF_TEST:
          case I_REF_CONVERT: case I_EQ: case I_NE: local = true; break;
          case I_TRAP_IF: local = in->imm == TRAP_NULL_REF; break;
          case I_CALL: {
            if (*ncalls == cap) {
              cap = cap ? 2 * cap : 16;
              *calls = xrealloc(*calls, cap * sizeof **calls);
            }
            (*calls)[(*ncalls)++] = (InlCallUse){ v, j, (uint32_t)c };
            local = true;
            break;
          }
          case I_CALL_REF: {
            uint32_t off;
            if (disp && j + 1 < in->nargs &&
                inl_dispatch_off(g, a[in->nargs - 1], own, n, c, &off)) {
              if (disp[c] == INL_NO_DISP) disp[c] = off;
              local = disp[c] == off;
            }
            break;
          }
          default: break;
        }
        if (!local && !lenient) escaped[c] = 1;
      }
    }
    if (lenient) continue;
    if (b->term == T_BRIF || b->term == T_BRTABLE) {
      int32_t c = IOWN(ir_resolve(g, b->term_arg));
      if (c >= 0) escaped[c] = 1;
    }
    for (uint32_t i = 0; i < b->nret; i++) {
      int32_t c = IOWN(ir_resolve(g, b->ret_args[i]));
      if (c >= 0) (returned ? returned : escaped)[c] = 1;
    }
    for (uint32_t s = 0; s < b->nsuccs; s++) {
      Edge *e = &b->succs[s];
      Block *t = &g->blocks[e->target];
      for (uint32_t j = 0; j < e->nargs && j < t->nparams; j++) {
        int32_t c = IOWN(ir_resolve(g, e->args[j]));
        if (c >= 0 && IOWN(t->params[j]) != c) escaped[c] = 1;
      }
    }
  }
#undef IOWN
  ir_edge_index_free(&ei);
  xfree(own);
}

/* Whether anything reads v: an instruction, an edge, a branch or a return. */
static bool inl_value_used(IrFunc *f, Ref v) {
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Inst *in = &f->insts[b->insts[i]];
      Ref *a = ir_args(f, b->insts[i]);
      for (uint32_t j = 0; j < in->nargs; j++)
        if (ir_resolve(f, a[j]) == v) return true;
    }
    if ((b->term == T_BRIF || b->term == T_BRTABLE) &&
        ir_resolve(f, b->term_arg) == v)
      return true;
    for (uint32_t i = 0; i < b->nret; i++)
      if (ir_resolve(f, b->ret_args[i]) == v) return true;
    for (uint32_t s = 0; s < b->nsuccs; s++)
      for (uint32_t j = 0; j < b->succs[s].nargs; j++)
        if (ir_resolve(f, b->succs[s].args[j]) == v) return true;
  }
  return false;
}

/* A direct call a callee hands reference parameter `param` to, as argument
   `arg`, and whether the call's result is read. */
typedef struct { uint32_t param, callee, arg; bool used; } InlPass;

/* The caller's side: the call sites that are the only way out for some
   allocation made on a warm path.  Per call, which arguments those
   allocations are and the deepest loop one is made in. */
typedef struct { uint64_t args; uint8_t depth; } InlEsc;

static InlEsc *inl_caller_evidence(IrFunc *f) {
  Module *m = f->m;
  Ref roots[INL_ESC_MAX_ROOTS];
  uint32_t nr = 0;
  for (uint32_t oi = 0; oi < f->norder && nr < INL_ESC_MAX_ROOTS; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    if (b->diverges) continue;
    for (uint32_t i = 0; i < b->ninsts && nr < INL_ESC_MAX_ROOTS; i++) {
      Ref v = b->insts[i];
      Inst *in = &f->insts[v];
      if (in->op == I_STRUCT_NEW && in->nargs == 0) roots[nr++] = v;
      else if (in->op == I_ARRAY_NEW && (in->nargs == 1 || in->nargs == 3) &&
               in->imm < m->ntypes) {
        Inst *lc = &f->insts[ir_resolve(f, ir_args(f, v)[0])];
        if (lc->op == I_CONST && (lc->imm & 0xffffffffu) <= INL_ESC_MAX_ELEMS)
          roots[nr++] = v;
      }
    }
  }
  if (!nr) return NULL;
  uint8_t *esc = xcalloc(nr, 1);
  InlCallUse *calls = NULL;
  uint32_t ncalls = 0;
  inl_escape_scan(f, roots, nr, esc, NULL, NULL, true, &calls, &ncalls);
  InlEsc *ev = NULL;
  for (uint32_t k = 0; k < ncalls; k++) {
    InlCallUse *u = &calls[k];
    if (esc[u->root] || u->arg >= 64) continue;
    if (!ev) ev = xcalloc(f->ninsts, sizeof *ev);
    ev[u->call].args |= 1ull << u->arg;
    uint8_t d = (uint8_t)f->blocks[f->insts[roots[u->root]].block].loop_depth;
    if (d > ev[u->call].depth) ev[u->call].depth = d;
  }
  xfree(esc); xfree(calls);
  return ev;
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
  /* returning calls: the ones a splice leaves, and all in the body as
     built, which a site that does not run on every call goes by (see
     inl_runs_every) */
  uint32_t ninsts, eparams, retcalls, retcalls_built;
  /* the indirect ones among them that sit in a loop of the callee */
  uint32_t loopcalls;
  /* The indirect calls through table 0 whose index is a parameter: a site
     that passes it a constant makes them direct calls, so they are not
     calls the caller has to keep crossing. */
  uint32_t nicall;
  struct { uint8_t param, in_loop; uint32_t type; } icall[INL_ICALLS];
  /* What the callee does with each reference parameter, bit p for p:
     keeps it to itself (no call, store, return or global sees it), lets it
     out by anything but a direct call or a return, returns it, or
     dispatches on it -- through the field at disp_off[p]. */
  bool summed;
  uint64_t local_params, esc_params, ret_params, disp_params;
  uint32_t disp_off[INL_DISP_PARAMS];
  InlPass *pass;
  uint32_t npass;
  /* the callee's start as a partial inline takes it in (pinl_build), as
     built and split at raising instructions, and its size: pverdict 1 when
     there is one, 2 when not */
  uint8_t pverdict;
  IrFunc *pplain, *psplit;
  uint32_t pninsts;
  /* the callee as built, untailed and ordered, when the whole callee was
     refused: the partial inline's to cut, so that it is not built twice */
  IrFunc *refused;
} InlMemo;

/* The memo every inline run on a thread uses: an entry per callee the run
   looks at, found through a table of their indices.  The entries sit in
   pages that stay where they are while more are made, since a caller holds
   one across the making of another, and the run's end frees what they hold
   and empties the table.  An entry for every function of the module, kept
   by each thread from one run to the next, came to most of the module's
   functions times two hundred bytes per thread once a worker had compiled
   enough callers: a quarter of the memory Go's net/http server's compile
   needed at its peak.  A fresh array per run, zeroed and then walked to
   free, was the module's size again for every function compiled: 3.4 GB
   zeroed compiling a Go program of 3,700 functions. */
#define INL_MEMO_PAGE 64u
typedef struct {
  InlMemo **page;
  uint32_t npage;
  /* open addressing: a callee's index, UINT32_MAX where none, and its
     entry's number in the pages */
  uint32_t *key, *at;
  uint32_t cap, n;
} InlMemos;

static _Thread_local InlMemos g_memos;

static InlMemos *inl_memos_begin(const Module *m) {
  (void)m;
  InlMemos *ms = &g_memos;
  if (!ms->cap) {
    ms->cap = 64;
    ms->key = xmalloc(ms->cap * sizeof(uint32_t));
    ms->at = xmalloc(ms->cap * sizeof(uint32_t));
    memset(ms->key, 0xff, ms->cap * sizeof(uint32_t));
  }
  ms->n = 0;
  return ms;
}

static InlMemo *memo_entry(InlMemos *ms, uint32_t k) {
  return &ms->page[k / INL_MEMO_PAGE][k % INL_MEMO_PAGE];
}

static uint32_t memo_slot(const InlMemos *ms, uint32_t fidx) {
  uint32_t mask = ms->cap - 1, s = (fidx * 0x9E3779B1u) & mask;
  while (ms->key[s] != UINT32_MAX && ms->key[s] != fidx) s = (s + 1) & mask;
  return s;
}

static InlMemo *memo_at(InlMemos *ms, uint32_t fidx) {
  uint32_t s = memo_slot(ms, fidx);
  if (ms->key[s] == fidx) return memo_entry(ms, ms->at[s]);
  if ((ms->n + 1) * 2 > ms->cap) {
    uint32_t oc = ms->cap, *ok = ms->key, *oa = ms->at;
    ms->cap = oc * 2;
    ms->key = xmalloc(ms->cap * sizeof(uint32_t));
    ms->at = xmalloc(ms->cap * sizeof(uint32_t));
    memset(ms->key, 0xff, ms->cap * sizeof(uint32_t));
    for (uint32_t i = 0; i < oc; i++)
      if (ok[i] != UINT32_MAX) {
        uint32_t t = memo_slot(ms, ok[i]);
        ms->key[t] = ok[i];
        ms->at[t] = oa[i];
      }
    xfree(ok); xfree(oa);
    s = memo_slot(ms, fidx);
  }
  uint32_t k = ms->n++;
  if (k / INL_MEMO_PAGE >= ms->npage) {
    ms->page = xrealloc(ms->page, (ms->npage + 1) * sizeof(InlMemo *));
    ms->page[ms->npage++] = xmalloc(INL_MEMO_PAGE * sizeof(InlMemo));
  }
  ms->key[s] = fidx;
  ms->at[s] = k;
  InlMemo *e = memo_entry(ms, k);
  memset(e, 0, sizeof *e);
  return e;
}

/* the run's end: what its entries hold freed, the table emptied */
static void memo_end_run(InlMemos *ms) {
  for (uint32_t k = 0; k < ms->n; k++) {
    InlMemo *e = memo_entry(ms, k);
    if (e->plain) ir_free(e->plain);
    if (e->split) ir_free(e->split);
    if (e->pplain) ir_free(e->pplain);
    if (e->psplit) ir_free(e->psplit);
    if (e->refused) ir_free(e->refused);
    xfree(e->pass);
  }
  if (ms->n) memset(ms->key, 0xff, ms->cap * sizeof(uint32_t));
  ms->n = 0;
}

void inl_memos_release(void) {
  InlMemos *ms = &g_memos;
  for (uint32_t i = 0; i < ms->npage; i++) xfree(ms->page[i]);
  xfree(ms->page); xfree(ms->key); xfree(ms->at);
  memset(ms, 0, sizeof *ms);
}

static bool is_const_arg(IrFunc *f, Ref r, uint64_t *v) {
  Inst *in = &f->insts[ir_resolve(f, r)];
  if (in->op != I_CONST) return false;
  *v = in->imm;
  return true;
}

/* Of those, the call_indirects through table 0 whose index is one of the
   callee's own parameters: what TinyGo passes a closure's function as, and
   what LLVM unrolls into several calls when it unrolls the loop around one. */
static void param_indirect_calls(IrFunc *g, InlMemo *e) {
  Block *en = &g->blocks[g->entry];
  e->nicall = 0;
  for (uint32_t bi = 0; bi < g->nblocks; bi++) {
    Block *b = &g->blocks[bi];
    if (!b->reachable || b->term == T_TRAP || b->diverges) continue;
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Inst *in = &g->insts[b->insts[i]];
      if (in->op != I_CALL_INDIRECT || (uint32_t)in->imm2 != 0 || !in->nargs)
        continue;
      Ref ix = ir_resolve(g, ir_args(g, b->insts[i])[in->nargs - 1]);
      for (uint32_t p = 0; p < en->nparams && p < 256; p++)
        if (en->params[p] == ix && e->nicall < INL_ICALLS) {
          e->icall[e->nicall].param = (uint8_t)p;
          e->icall[e->nicall].in_loop = b->loop_depth != 0;
          e->icall[e->nicall].type = (uint32_t)in->imm;
          e->nicall++;
          break;
        }
    }
  }
}

static void inl_summarize(InlMemo *e, IrFunc *g) {
  e->summed = true;
  for (uint32_t p = 0; p < INL_DISP_PARAMS; p++) e->disp_off[p] = INL_NO_DISP;
  Block *en = &g->blocks[g->entry];
  uint32_t np = en->nparams < 64 ? en->nparams : 64;
  Ref roots[64];
  uint32_t idx[64], nr = 0;
  for (uint32_t p = 0; p < np; p++)
    if (vt_is_ref(g->insts[en->params[p]].type)) { idx[nr] = p; roots[nr++] = en->params[p]; }
  if (!nr) return;
  uint8_t esc[64] = {0}, ret[64] = {0};
  uint32_t disp[64];
  for (uint32_t r = 0; r < nr; r++) disp[r] = INL_NO_DISP;
  InlCallUse *calls = NULL;
  uint32_t ncalls = 0;
  inl_escape_scan(g, roots, nr, esc, ret, disp, false, &calls, &ncalls);
  uint64_t called = 0;
  if (ncalls) e->pass = xmalloc(ncalls * sizeof(InlPass));
  for (uint32_t k = 0; k < ncalls; k++) {
    called |= 1ull << idx[calls[k].root];
    e->pass[e->npass++] = (InlPass){ idx[calls[k].root],
                                     (uint32_t)g->insts[calls[k].call].imm,
                                     calls[k].arg,
                                     inl_value_used(g, calls[k].call) };
  }
  xfree(calls);
  for (uint32_t r = 0; r < nr; r++) {
    uint64_t bit = 1ull << idx[r];
    if (esc[r]) e->esc_params |= bit;
    if (ret[r]) e->ret_params |= bit;
    if (disp[r] != INL_NO_DISP) {
      e->disp_params |= bit;
      if (idx[r] < INL_DISP_PARAMS) e->disp_off[idx[r]] = disp[r];
    }
    if (!esc[r] && !ret[r] && disp[r] == INL_NO_DISP && !(called & bit))
      e->local_params |= bit;
  }
}

/* Of a callee's returning calls, the ones that stay calls once it is
   inlined: a call the callee makes every time it runs, to a function small
   enough to take in that makes no call of its own, goes with the splice.
   C's struct helpers are such calls in a body as built and nothing once
   inlined -- cbench/structval's step made nine, all but one to three-line
   vector functions, and for them stayed a call in its caller's loop, its
   arguments copied through two frames.  A call on some of the callee's
   paths only goes if what it calls is not much more than tiny -- step's
   bounce, a v3scale of 18 instructions: inlined, it brings that path's
   whole body into the caller, as AssemblyScript's collector marking a white
   object, 73, did into the loops that visit them, 5% slower.  And a callee too
   big for the usual cap that has loops of its own keeps all its calls: its
   call is paid for over its loops, and taken in, the caller's live values
   crowd them -- cbench/lexer's scanner, a single site so growth-sized,
   spilled in its token loop inside main's.  Whether a function makes a
   call is the effect summaries', read off its body once. */
static uint32_t surviving_calls(IrFunc *f, IrFunc *g, uint32_t fidx, uint32_t *in_loop) {
  Module *m = f->m;
  uint32_t n = warm_returning_calls(g, in_loop);
  bool loops = false;
  for (uint32_t oi = 0; oi < g->norder && !loops; oi++)
    if (g->blocks[g->order[oi]].loop_depth) loops = true;
  if (loops && g->ninsts > g_inl.max_callee) return n;
  for (uint32_t oi = 0; oi < g->norder && n; oi++) {
    uint32_t gb = g->order[oi];
    Block *b = &g->blocks[gb];
    if (!b->reachable || b->term == T_TRAP || b->diverges) continue;
    bool every = true;
    for (uint32_t oj = 0; oj < g->norder && every; oj++) {
      uint32_t r = g->order[oj];
      if (g->blocks[r].term == T_RETURN && !ir_dominates_checked(g, gb, r)) every = false;
    }
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Inst *in = &g->insts[b->insts[i]];
      uint32_t ni;
      if (in->op == I_CALL && in->imm != fidx && opt_func_leaf(m, (uint32_t)in->imm, &ni) &&
          ni <= (every ? g_inl.max_callee : 2 * g_inl.tiny))
        n--;
    }
  }
  return n;
}

static IrFunc *inl_callee(IrFunc *f, const OptOpts *o, InlMemos *memo,
                          uint32_t fidx, uint32_t bi, bool want_split) {
  Module *m = f->m;
  InlMemo *e = memo_at(memo, fidx);
  if (e->verdict == 0) {
    e->verdict = 2;
    IrFunc *g = ir_build(m, fidx, o);
    if (!g_inl.notail) untail(g);
    inl_summarize(e, g);
    uint32_t lc;
    uint32_t built = warm_returning_calls(g, &lc);
    uint32_t r = surviving_calls(f, g, fidx, &lc);
    param_indirect_calls(g, e);
    if (g_inl.notail && has_tail_call(g)) {
      g_stats.inl_rej_tailcall++;
      inl_dbg(f, fidx, bi, "tail call");
      e->refused = g;
    } else if (g_inl.leafonly ? !callee_calls_are_cold(g)
                              : r - e->nicall > (g_inl.retcalls > INL_ESC_RETCALLS
                                                 ? g_inl.retcalls : INL_ESC_RETCALLS)) {
      /* up to INL_ESC_RETCALLS calls stays a candidate: whether it is
         taken is the site's question, and only escape evidence says yes */
      g_stats.inl_rej_retcalls++;
      inl_dbg(f, fidx, bi, "returning calls");
      e->refused = g;
    } else if (g->npads) {
      g_stats.inl_rej_pads++;
      inl_dbg(f, fidx, bi, "landing pads");
      e->refused = g;
    } else {
      e->verdict = 1;
      ir_order(g);
      e->plain = g;
      e->ninsts = g->ninsts;
      e->eparams = g->blocks[g->entry].nparams;
      e->retcalls = r;
      e->retcalls_built = built;
      e->loopcalls = lc;
    }
  }
  if (e->verdict != 1) return NULL;
  if (!want_split) return e->plain;
  if (!e->split) {
    e->split = ir_build(m, fidx, o);
    if (!g_inl.notail) untail(e->split);
    split_at_raising_sites(e->split);
  }
  return e->split;
}

/* Partial inlining.  A callee too big to take in, or one that makes calls,
   often starts with a test that, most of the time, sends it straight back:
   TeaVM's ArrayList.ensureCapacity returns when the array is big enough and
   grows it otherwise, Integer.ensureIntegerCache when the cache is made.
   Called, such a function saves its callee-saved registers, checks the
   stack and makes a frame for the few loads and the compare of that test --
   javabench/generic spent a fifth of its cycles in three of them.  So a
   site may take in just that start: the blocks from the entry that reach a
   return without a call or a loop (the warm way), where every way from them
   into the rest of the callee becomes a call of the callee itself, with the
   site's arguments, whose results are the site's.  That call runs the
   callee from its start again, so the start may do nothing on the way to
   such a cut that running it twice would see: it may read and trap -- a
   trap there is the one the callee would make first -- and make a class
   initializer's call (opt_once_call), which once made calls nothing; the
   blocks that only lead to a return may store as well. */

/* whether running v again, with nothing written in between, does nothing
   the first run did not: no write, no allocation, no call but a class
   initializer's */
static bool pinl_rerun_ok(IrFunc *g, Ref v) {
  const Inst *in = &g->insts[v];
  if (ir_has_no_effect(in)) return true;
  switch (in->op) {
    case I_FIELD_GET: case I_ARRAY_GET: case I_ARRAY_LEN: case I_I31_GET:
    case I_DIVS: case I_DIVU: case I_REMS: case I_REMU:
    case I_TRUNCS: case I_TRUNCU:
    case I_LOAD: case I_TABLE_GET: case I_MEM_SIZE: case I_TABLE_SIZE:
    case I_REF_SLOT:
      return true;
    case I_CALL_REF:
      return opt_once_call(g, v);
    default:
      return false;
  }
}

/* The callee fidx's warm start, as a body a splice takes in: built, its tail
   calls made calls, split at raising instructions when the site has handler
   edges, and cut where it would call or loop -- the cut edges going to one
   block that calls fidx with the entry's parameters and returns what that
   returns.  NULL when the callee has no such start (its entry calls or
   loops, no return is reached without, nothing is cut, or the start is over
   g_inl.partial_max instructions); *size is the start's instruction count. */
static IrFunc *pinl_build(IrFunc *f, const OptOpts *o, uint32_t fidx,
                          bool split, uint32_t *size, IrFunc *built) {
  Module *m = f->m;
  IrFunc *g = built;
  if (!g) {
    g = ir_build(m, fidx, o);
    if (!g_inl.notail) untail(g);
  }
  if (split) split_at_raising_sites(g);
  ir_order(g);
  uint32_t nb = g->nblocks;
  uint8_t *bad = xcalloc(nb ? nb : 1, 1), *in_g = xcalloc(nb ? nb : 1, 1);
  uint8_t *ret = xcalloc(nb ? nb : 1, 1), *cut = xcalloc(nb ? nb : 1, 1);
  for (uint32_t oi = 0; oi < g->norder; oi++) {
    uint32_t bi = g->order[oi];
    Block *b = &g->blocks[bi];
    if (b->loop_depth || b->is_pad) { bad[bi] = 1; continue; }
    for (uint32_t s = 0; s < b->nsuccs; s++)
      if (b->succs[s].eh) bad[bi] = 1;
    for (uint32_t i = 0; i < b->ninsts && !bad[bi]; i++) {
      Ref v = b->insts[i];
      uint16_t op = g->insts[v].op;
      if (op == I_CALL_REF && opt_once_call(g, v)) continue;
      if (raises(op) || op == I_RETURN_CALL || op == I_RETURN_CALL_INDIRECT ||
          op == I_RETURN_CALL_REF)
        bad[bi] = 1;
    }
  }
  IrFunc *out = NULL;
  uint32_t n = 0, ncut = 0;
  bool any_ret = false;
  /* the warm start, made smaller while a block on the way to a cut does
     what may not run twice (each round marks one more block bad) */
  for (uint32_t round = 0; round <= g->norder; round++) {
    if (bad[g->entry]) goto done;
    memset(in_g, 0, nb);
    in_g[g->entry] = 1;
    for (uint32_t oi = 0; oi < g->norder; oi++) {    /* reverse postorder */
      uint32_t bi = g->order[oi];
      if (!in_g[bi]) continue;
      const Block *b = &g->blocks[bi];
      for (uint32_t s = 0; s < b->nsuccs; s++)
        if (!bad[b->succs[s].target]) in_g[b->succs[s].target] = 1;
    }
    /* reaching a return or a trap within: the blocks taken in.  The region
       has no loop, so successors come first in reverse of the order */
    memset(ret, 0, nb);
    for (uint32_t oi = g->norder; oi-- > 0;) {
      uint32_t bi = g->order[oi];
      if (!in_g[bi]) continue;
      const Block *b = &g->blocks[bi];
      if (b->term == T_RETURN || b->term == T_TRAP) ret[bi] = 1;
      for (uint32_t s = 0; s < b->nsuccs && !ret[bi]; s++)
        if (ret[b->succs[s].target]) ret[bi] = 1;
    }
    if (!ret[g->entry]) goto done;
    /* reaching a cut, and the blocks on the way that may not run twice */
    memset(cut, 0, nb);
    bool again = false;
    n = ncut = 0;
    any_ret = false;
    for (uint32_t oi = g->norder; oi-- > 0;) {
      uint32_t bi = g->order[oi];
      if (!ret[bi]) continue;
      const Block *b = &g->blocks[bi];
      if (b->term == T_RETURN) any_ret = true;
      for (uint32_t s = 0; s < b->nsuccs; s++) {
        uint32_t t = b->succs[s].target;
        if (!ret[t]) { cut[bi] = 1; ncut++; }
        else if (cut[t]) cut[bi] = 1;
      }
      for (uint32_t i = 0; i < b->ninsts; i++) {
        Ref v = b->insts[i];
        uint16_t op = g->insts[v].op;
        if (op != I_CONST && op != I_PARAM && op != I_NOP) n++;
        if (cut[bi] && !pinl_rerun_ok(g, v)) { bad[bi] = 1; again = true; }
      }
    }
    if (!again) break;
  }
  if (!ncut || !any_ret || n > g_inl.partial_max) goto done;

  /* the call of the whole callee where the start is cut */
  {
    CompType *sig = &m->types[m->funcs[fidx].type_index];
    uint32_t nres = sig->nresults;
    uint32_t sb = ir_new_block(g);
    Block *S = &g->blocks[sb];
    S->sealed = S->filled = S->reachable = true;
    const Block *eb = &g->blocks[g->entry];
    Ref *args = xmalloc((eb->nparams ? eb->nparams : 1) * sizeof(Ref));
    memcpy(args, eb->params, eb->nparams * sizeof(Ref));
    Ref call = ir_emit(g, sb, I_CALL, nres == 1 ? sig->results[0] : 0, args,
                       eb->nparams, fidx, 0);
    xfree(args);
    uint32_t rb = sb;
    if (nres > 1) {
      rb = ir_new_block(g);
      Block *R = &g->blocks[rb];
      R->sealed = R->filled = R->reachable = true;
      g->blocks[sb].term = T_JUMP;
      ir_add_edge(g, sb, rb, NULL, 0);
    }
    Ref *rv = ir_alloc(g, (nres ? nres : 1) * sizeof(Ref));
    if (nres == 1) rv[0] = call;
    else
      for (uint32_t i = 0; i < nres; i++) {
        Ref a[1] = { call };
        rv[i] = ir_emit(g, rb, I_RESULT, sig->results[i], a, 1, i, 0);
      }
    Block *R = &g->blocks[rb];
    R->term = T_RETURN;
    R->nret = R->cap_ret = nres;
    R->ret_args = rv;
    /* the cut edges to it; the rest of the callee emptied, so that the
       splice copies nothing of it */
    for (uint32_t bi = 0; bi < nb; bi++) {
      Block *b = &g->blocks[bi];
      if (ret[bi]) {
        for (uint32_t s = 0; s < b->nsuccs; s++)
          if (!ret[b->succs[s].target]) {
            b->succs[s].target = sb;
            b->succs[s].nargs = 0;
          }
        continue;
      }
      b->ninsts = 0;
      b->nsuccs = 0;
      b->nret = 0;
      b->term = T_TRAP;
      b->term_imm = TRAP_UNREACHABLE;
      b->reachable = false;
    }
    ir_order(g);
    out = g;
    *size = n + 1;
  }
done:
  xfree(bad); xfree(in_g); xfree(ret); xfree(cut);
  if (!out) ir_free(g);
  return out;
}

static IrFunc *inl_partial_callee(IrFunc *f, const OptOpts *o, InlMemos *memo,
                                  uint32_t fidx, bool want_split) {
  InlMemo *e = memo_at(memo, fidx);
  if (e->pverdict == 0) {
    /* most callees have no such start, which the module says once */
    uint8_t *known = f->m->pinl && fidx < f->m->nfuncs ? &f->m->pinl[fidx] : NULL;
    e->pverdict = 2;
    if (known && __atomic_load_n(known, __ATOMIC_RELAXED) == 2) return NULL;
    uint32_t size = 0;
    IrFunc *built = e->refused;
    e->refused = NULL;
    e->pplain = pinl_build(f, o, fidx, false, &size, built);
    if (e->pplain) { e->pverdict = 1; e->pninsts = size; }
    if (known) __atomic_store_n(known, e->pplain ? 1 : 2, __ATOMIC_RELAXED);
  }
  if (e->pverdict != 1) return NULL;
  if (!want_split) return e->pplain;
  if (!e->psplit) {
    uint32_t size = 0;
    e->psplit = pinl_build(f, o, fidx, true, &size, NULL);
  }
  return e->psplit;
}

/* Whether callee fidx keeps reference parameter p to itself, the direct
   calls it hands p to included: each must keep it too, and must not hand
   it back by returning it into a result that is read.  At the top, a
   dispatch on p is allowed when `disp` says the dispatch will resolve. */
static bool inl_keeps(IrFunc *f, const OptOpts *o, InlMemos *memo,
                      uint32_t fidx, uint32_t p, uint32_t bi, bool disp,
                      int depth) {
  Module *m = f->m;
  if (p >= 64 || depth > INL_KEEP_DEPTH || fidx >= m->nfuncs ||
      fidx == f->func_index || m->funcs[fidx].imported || !m->funcs[fidx].body)
    return false;
  InlMemo *e = memo_at(memo, fidx);
  if (!e->summed) inl_callee(f, o, memo, fidx, bi, false);
  if (!e->summed) return false;
  uint64_t bit = 1ull << p;
  if ((e->esc_params & bit) || ((e->disp_params & bit) && !disp)) return false;
  for (uint32_t k = 0; k < e->npass; k++) {
    InlPass q = e->pass[k];
    if (q.param != p) continue;
    if (!inl_keeps(f, o, memo, q.callee, q.arg, bi, false, depth + 1))
      return false;
    if (q.used && (memo_at(memo, q.callee)->ret_params >> q.arg & 1)) return false;
  }
  return true;
}

/* Whether a dispatch through the field at `off` resolves on argument a: a
   fresh allocation whose initialising store there is a constant object,
   a vtable the module built. */
static bool inl_const_dispatch(IrFunc *f, Ref a, uint32_t off) {
  for (int d = 0; d < 8; d++) {
    a = ir_resolve(f, a);
    Inst *in = &f->insts[a];
    if (in->op == I_REF_CONVERT && in->nargs == 1) {
      a = ir_args(f, a)[0];
      continue;
    }
    if (in->op != I_STRUCT_NEW) return false;
    Block *b = &f->blocks[in->block];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref v = b->insts[i];
      Inst *st = &f->insts[v];
      if (st->op != I_FIELD_SET || st->nargs < 2 || !(st->imm2 & GCF_INIT) ||
          st->imm != off || ir_resolve(f, ir_args(f, v)[0]) != a)
        continue;
      return opt_const_object(f, ir_args(f, v)[1]);
    }
    return false;
  }
  return false;
}

typedef struct {
  Ref call;
  uint32_t fidx;
  uint32_t charged, score;
  uint32_t rpo, idx;
  bool once;
  bool escape;   /* the inline frees an allocation for pass_escape */
  bool every;    /* runs on every call, or in a loop: see inl_runs_every */
  bool partial;  /* takes in the callee's warm start only (pinl_build) */
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
/* Whether the block a call sits in runs whenever its function returns --
   it dominates each return -- or in a loop.  There a callee's calls to leaf
   functions small enough to inline are taken as gone with the splice;
   elsewhere a branch may lead to a slow path whose callee its calls kept out
   of line, as libm's argument reduction for large angles was.  A site in a
   body spliced this round has no dominators yet and goes by its loop. */
static bool inl_runs_every(IrFunc *f, uint32_t bi, bool spliced) {
  if (f->blocks[bi].loop_depth) return true;
  if (spliced) return false;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t r = f->order[oi];
    if (f->blocks[r].term == T_RETURN && !ir_dominates_checked(f, bi, r)) return false;
  }
  return true;
}

static bool inl_score_site(IrFunc *f, const OptOpts *o, InlMemos *memo,
                           const InlEsc *ev, Ref call, uint32_t ord1,
                           uint32_t ord2, const InlSite *parent,
                           InlSite *out) {
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
  InlMemo *e = memo_at(memo, fidx);
  if (e->ninsts > lim) {
    g_stats.inl_rej_size++;
    inl_dbg(f, fidx, bi, "too big");
    return false;
  }

  /* An allocation that leaves only through this call, into a parameter
     the callee keeps to itself: inlined, nothing is left holding it.  Kept
     counts the calls the callee hands it on to, when they keep it too, and
     a return of it nobody reads; and a dispatch on it, when the vtable the
     allocation was made with resolves the dispatch. */
  uint64_t want = ev && call < f->ninsts ? ev[call].args : 0;
  uint64_t freed = want & e->local_params;
  int used = -1;
  for (uint64_t x = want & ~freed; x; x &= x - 1) {
    uint32_t p = (uint32_t)__builtin_ctzll(x);
    bool disp = p < INL_DISP_PARAMS && (e->disp_params >> p & 1) &&
                inl_const_dispatch(f, ir_args(f, call)[p], e->disp_off[p]);
    if (!inl_keeps(f, o, memo, fidx, p, bi, disp, 0)) continue;
    if (e->ret_params >> p & 1) {
      if (used < 0) used = inl_value_used(f, call);
      if (used) continue;
    }
    freed |= 1ull << p;
  }
  bool escape = freed != 0;
  uint32_t nfreed = 0;
  for (uint64_t x = freed; x; x &= x - 1) nfreed++;

  /* off the path every call takes, what the callee's own gate was before
     the splice's discount */
  bool every = inl_runs_every(f, bi, parent != NULL);
  if (!every &&
      e->retcalls_built - e->nicall > (g_inl.retcalls > INL_ESC_RETCALLS
                                       ? g_inl.retcalls : INL_ESC_RETCALLS)) {
    g_stats.inl_rej_retcalls++;
    inl_dbg(f, fidx, bi, "returning calls");
    return false;
  }

  /* The callee's indirect calls this site's constants turn direct. */
  uint32_t retcalls = every ? e->retcalls : e->retcalls_built;
  uint32_t loopcalls = e->loopcalls;
  if (o->devirt)
    for (uint32_t k = 0; k < e->nicall; k++) {
      uint32_t p = e->icall[k].param;
      uint64_t ix;
      if (p < cin->nargs && is_const_arg(f, ir_args(f, call)[p], &ix) &&
          opt_table0_target(m, e->icall[k].type, ix) != UINT32_MAX) {
        retcalls--;
        if (e->icall[k].in_loop) loopcalls--;
      }
    }

  if (retcalls > g_inl.retcalls && !escape && !g_inl.leafonly) {
    g_stats.inl_rej_retcalls++;
    inl_dbg(f, fidx, bi, "returning calls");
    return false;
  }

  /* each argument as substituted, which the caller's may not have been
     yet since a splice */
  uint32_t nconst = 0;
  Ref *a = ir_args(f, call);
  for (uint32_t j = 0; j < cin->nargs; j++)
    if (f->insts[ir_resolve(f, a[j])].op == I_CONST) nconst++;
  bool fonly = opt_call_frame_only(m, fidx);
  uint32_t sub = e->eparams + 2 * nconst;
  uint32_t charged = e->ninsts > sub + 4 ? e->ninsts - sub : 4;

  /* A surviving call crosses the site: everything live around it pays
     CSA's save/restore there, doubled in loops where those frame ops are
     loop-priced. In a loop, only take the crossing when something says
     this site is special -- a dying body, a folding argument, a callee
     that touches nothing.

     A dying body says nothing about a callee whose own loop makes an
     indirect call.  There every value the caller holds across the site
     has to sit in a callee-saved register or in memory for the whole
     loop, next to the loop's own call-crossing values, and the excess is
     reloaded on every trip; out of line, the callee's prologue saves
     them once a call.  What inlining saves is one call per run of the
     loop, which the trips amortise to nothing -- a dispatch loop over a
     table of method thunks ran 20% more instructions merged into its
     caller's loop. */
  if (retcalls) {
    bool dies = once && !loopcalls;
    if (b->loop_depth >= 1 && !dies && nconst == 0 && !fonly && !escape) {
      g_stats.inl_rej_retcalls++;
      inl_dbg(f, fidx, bi, "retcall in loop without evidence");
      return false;
    }
    charged += retcalls * g_inl.retcall_cost * (b->loop_depth ? 2 : 1);
  }

  /* A cold site is worth taking when what it frees is made on a warm path:
     the allocation goes from there, not just from here. */
  if (b->diverges && charged > g_inl.tiny && !escape) {
    g_stats.inl_rej_score++;
    inl_dbg(f, fidx, bi, "cold and not tiny");
    return false;
  }

  uint32_t benefit = INL_C_BASE + INL_C_CONST * nconst +
                     (fonly ? INL_C_FRAMEONLY : 0) + (once ? INL_C_ONCE : 0) +
                     INL_C_ESCAPE * nfreed;
  uint32_t d = escape ? (ev[call].depth > b->loop_depth ? ev[call].depth
                                                        : b->loop_depth)
                      : b->loop_depth;
  if (d > 3) d = 3;
  uint32_t score = b->diverges && !escape ? 0 : w[d] * benefit * 16 / charged;
  if (g_inl.noscore) score = 1;

  memset(out, 0, sizeof(*out));
  out->call = call;
  out->fidx = fidx;
  out->charged = charged;
  out->score = score;
  out->rpo = ord1;
  out->idx = ord2;
  out->once = once;
  out->every = every;
  out->escape = escape;
  out->idepth = idepth;
  if (parent) {
    memcpy(out->anc, parent->anc, parent->idepth * sizeof(uint32_t));
    out->anc[parent->idepth] = parent->fidx;
  }
  return true;
}

/* A site the whole callee was refused for, taking in its warm start: one
   on a way that returns, not the call a partial inline left (which would
   take the same start in again), scored as a splice of the start's size. */
static bool inl_score_partial(IrFunc *f, const OptOpts *o, InlMemos *memo,
                              Ref call, uint32_t ord1, uint32_t ord2,
                              const InlSite *parent, InlSite *out) {
  Module *m = f->m;
  static const uint32_t w[4] = {1, 10, 100, 1000};
  if (g_inl.nopartial) return false;
  Inst *cin = &f->insts[call];
  if (cin->op != I_CALL || (cin->flags & IRF_PARTIAL_SLOW)) return false;
  uint32_t fidx = (uint32_t)cin->imm;
  if (fidx == f->func_index || m->funcs[fidx].imported || !m->funcs[fidx].body)
    return false;
  uint32_t bi = cin->block;
  Block *b = &f->blocks[bi];
  if (b->diverges || b->cold) return false;
  uint16_t idepth = 0;
  if (parent) {
    if ((uint32_t)parent->idepth + 1 > g_inl.depth || fidx == parent->fidx)
      return false;
    for (uint16_t j = 0; j < parent->idepth; j++)
      if (parent->anc[j] == fidx) return false;
    idepth = parent->idepth + 1;
  }
  if (!inl_partial_callee(f, o, memo, fidx, false)) return false;
  uint32_t charged = memo_at(memo, fidx)->pninsts;
  if (charged < 4) charged = 4;
  uint32_t d = b->loop_depth > 3 ? 3 : b->loop_depth;
  memset(out, 0, sizeof(*out));
  out->call = call;
  out->fidx = fidx;
  out->charged = charged;
  out->score = g_inl.noscore ? 1 : w[d] * INL_C_BASE * 16 / charged;
  out->rpo = ord1;
  out->idx = ord2;
  out->every = inl_runs_every(f, bi, parent != NULL);
  out->partial = true;
  out->idepth = idepth;
  if (parent) {
    memcpy(out->anc, parent->anc, parent->idepth * sizeof(uint32_t));
    out->anc[parent->idepth] = parent->fidx;
  }
  return true;
}

/* A later round takes only the calls pass_devirt has made since, and the
   calls an allocation leaves by. */
static void inl_collect(IrFunc *f, const OptOpts *o, InlMemos *memo,
                        const InlEsc *ev, InlVec *sites, bool more) {
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    Block *b = &f->blocks[f->order[oi]];
    for (uint32_t k = 0; k < b->ninsts; k++) {
      InlSite s;
      Ref v = b->insts[k];
      Inst *in = &f->insts[v];
      if (in->op != I_CALL) continue;
      bool fresh = (in->flags & IRF_DEVIRT) != 0;
      in->flags &= (uint8_t)~IRF_DEVIRT;
      if (more && !fresh && !(ev && ev[v].args)) continue;
      if (inl_score_site(f, o, memo, ev, v, oi, k, NULL, &s) ||
          inl_score_partial(f, o, memo, v, oi, k, NULL, &s))
        inl_push(sites, &s);
    }
  }
}

/* Sets *stale when it leaves the caller's substitutions to apply and its
   block order to recompute.  Both are functions of the graph alone, so the
   inliner does them once at the end.  What it reads of them in between is
   local: the call's arguments, resolved here; the call block's instruction
   list, brought up to date here as applying would; that block's
   reachability, which a splice changes only when the callee never returns
   or takes over the call's exception edges, and which is found again then;
   and, to score the calls a splice copied in, their blocks' loop depths
   and divergence (ir_order_depths).  Doing all of it after every inline
   was most of a compile that inlined two thousand calls into 78,000
   blocks, and redoing the order after each such splice, and before each
   scoring, most of a Rust main that inlines three hundred panics. */
static bool inl_apply(IrFunc *f, const OptOpts *o, InlMemos *memo,
                      const InlSite *site, uint32_t *budget,
                      uint32_t *retsum, bool *stale) {
  Module *m = f->m;
  Ref call = site->call;
  Inst *cin = &f->insts[call];
  if (cin->op != I_CALL || (uint32_t)cin->imm != site->fidx) return false;
  uint32_t fidx = site->fidx;
  uint32_t bi = cin->block;
  Block *b = &f->blocks[bi];
  if (!b->reachable) return false;
  if (*stale) {
    uint32_t w = 0;
    for (uint32_t i = 0; i < b->ninsts; i++)
      if (ir_resolve(f, b->insts[i]) == b->insts[i]) b->insts[w++] = b->insts[i];
    b->ninsts = w;
  }
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

  bool partial = site->partial;
  IrFunc *g = partial ? inl_partial_callee(f, o, memo, fidx, nehe > 0)
                      : inl_callee(f, o, memo, fidx, bi, nehe > 0);
  if (!g) return false;
  uint32_t charged = site->charged;

  /* the frame-cap pressure valve: surviving calls a caller absorbs */
  uint32_t rc = partial ? 0
                : site->every ? memo_at(memo, fidx)->retcalls
                              : memo_at(memo, fidx)->retcalls_built;
  uint32_t rcap = g_inl.retcall_total + (site->escape ? INL_ESC_SLACK : 0);
  /* a whole callee refused here may still have its warm start taken in,
     which leaves the call it makes as it was, and is smaller */
  if (!partial && (charged > *budget || (rc && *retsum + rc > rcap)) &&
      !g_inl.nopartial && !(cin->flags & IRF_PARTIAL_SLOW) && !b->diverges &&
      !b->cold) {
    IrFunc *pg = inl_partial_callee(f, o, memo, fidx, nehe > 0);
    if (pg) {
      partial = true;
      g = pg;
      charged = memo_at(memo, fidx)->pninsts > 4 ? memo_at(memo, fidx)->pninsts : 4;
      rc = 0;
    }
  }
  if (charged > *budget) {
    g_stats.inl_rej_budget++;
    return false;
  }
  if (rc && *retsum + rc > rcap) {
    g_stats.inl_rej_retcalls++;
    inl_dbg(f, fidx, bi, "caller absorbed enough calls");
    return false;
  }

  CompType *sig = &m->types[m->funcs[fidx].type_index];
  uint32_t nargs = cin->nargs;
  Ref *callargs = xmalloc((nargs ? nargs : 1) * sizeof(Ref));
  memcpy(callargs, ir_args(f, call), nargs * sizeof(Ref));
  for (uint32_t i = 0; i < nargs; i++) callargs[i] = ir_resolve(f, callargs[i]);

  if (site->once && memo_at(memo, fidx)->ninsts > g_inl.max_callee)
    g_stats.inl_once_forced++;
  if (rc) g_stats.inl_retcall_sites++;
  if (site->escape && !partial) g_stats.inl_escape++;
  *retsum += rc;

  if (!partial && !nehe && !any_eh && g->norder == 1 && g->order[0] == g->entry &&
      g->blocks[g->entry].term == T_RETURN &&
      inline_flat(f, g, bi, k, call, callargs, nargs, sig)) {
    *budget -= charged;
    g_stats.inlined++;
    g_stats.inl_flat++;
    inl_dbg(f, fidx, bi, "inlined flat");
    xfree(callargs);
    *stale = true;
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
      ir_make_nop(&f->insts[r]);
    }
  }

  bool returns = false;
  for (uint32_t oi = 0; oi < g->norder && !returns; oi++)
    returns = g->blocks[g->order[oi]].term == T_RETURN;

  Ref first = (Ref)f->ninsts;
  splice(f, g, bi, cont, callargs, nargs, ehe, nehe);

  xfree(ehe);
  *budget -= charged;
  if (partial) {
    /* the cut's call of the whole callee: not taken in partially again */
    for (Ref v = first; v < (Ref)f->ninsts; v++)
      if (f->insts[v].op == I_CALL && (uint32_t)f->insts[v].imm == fidx)
        f->insts[v].flags |= IRF_PARTIAL_SLOW;
    g_stats.inl_partial++;
    inl_dbg(f, fidx, bi, "inlined partially");
  } else {
    g_stats.inlined++;
    g_stats.inl_spliced++;
    inl_dbg(f, fidx, bi, "inlined spliced");
  }
  xfree(callargs); xfree(contp);
  if (nehe || !returns) ir_mark_reachable(f);
  else f->blocks[cont].reachable = true;
  *stale = true;
  return true;
}

static bool inl_run(IrFunc *f, const OptOpts *o, uint32_t budget0, bool more) {
  Module *m = f->m;
  InlMemos *memo = inl_memos_begin(m);
  InlVec sites = {0};
  InlEsc *ev = o->escape ? inl_caller_evidence(f) : NULL;
  inl_collect(f, o, memo, ev, &sites, more);
  if (sites.n > 1) qsort(sites.v, sites.n, sizeof(InlSite), inl_site_cmp);

  uint32_t budget = budget0;
  uint32_t retsum = 0;
  long events = 0;
  /* stale: the substitutions and the order are left to the end; depths:
     the blocks' loop depths and divergence are the graph's as it is now */
  bool any = false, stale = false, depths = true;
  for (uint32_t i = 0; i < sites.n && budget > 0; i++) {
    if (g_inl.max >= 0 && events >= g_inl.max) break;
    InlSite site = sites.v[i];
    uint32_t before = f->ninsts, nb_before = f->nblocks;
    if (!inl_apply(f, o, memo, &site, &budget, &retsum, &stale)) continue;
    any = true;
    /* a flat inline leaves the graph as it was */
    if (f->nblocks != nb_before) depths = false;
    events++;
    if (more) g_stats.inl_more++;
    if (site.idepth) g_stats.inl_transitive++;

    /* the splice may have copied calls in; they are sites too */
    if (g_inl.notrans) continue;
    uint32_t had = sites.n;
    for (Ref v = (Ref)before; v < (Ref)f->ninsts; v++) {
      if (f->insts[v].op != I_CALL) continue;
      if (!depths) { ir_order_depths(f); depths = true; }
      InlSite ns;
      if (inl_score_site(f, o, memo, NULL, v, f->insts[v].block, v, &site, &ns) ||
          inl_score_partial(f, o, memo, v, f->insts[v].block, v, &site, &ns))
        inl_push(&sites, &ns);
    }
    if (sites.n != had)
      qsort(sites.v + i + 1, sites.n - (i + 1), sizeof(InlSite),
            inl_site_cmp);
  }

  if (stale) { ir_apply_subst(f); ir_order(f); }
  xfree(sites.v);
  xfree(ev);
  memo_end_run(memo);
  return any;
}

bool pass_inline(IrFunc *f, const OptOpts *o) {
  pthread_once(&g_inl_once, inl_init);
  if (g_inl.only >= 0 && f->func_index != (uint32_t)g_inl.only) return false;
  if (g_inl.upto >= 0 && f->func_index > (uint32_t)g_inl.upto) return false;
  return inl_run(f, o, g_inl.growth, false);
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
  return inl_run(f, o, g_inl.round2_growth, false);
}

/* The rounds after the cleanup: what devirtualization made direct, and
   what escape analysis says an allocation is held by. */
bool pass_inline_more(IrFunc *f, const OptOpts *o) {
  pthread_once(&g_inl_once, inl_init);
  if (g_inl.only >= 0 && f->func_index != (uint32_t)g_inl.only) return false;
  if (g_inl.upto >= 0 && f->func_index > (uint32_t)g_inl.upto) return false;
  return inl_run(f, o, g_inl.round2_growth, true);
}
