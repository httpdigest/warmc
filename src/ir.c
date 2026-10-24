#include "ir.h"
#include "target.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Thread_local OptStats g_stats;

void opt_stats_add(OptStats *dst, const OptStats *src) {

  uint64_t mf = dst->max_frame_bytes > src->max_frame_bytes
                    ? dst->max_frame_bytes
                    : src->max_frame_bytes;
  uint64_t *d = (uint64_t *)dst;
  const uint64_t *s = (const uint64_t *)src;
  for (size_t i = 0; i < sizeof *dst / sizeof(uint64_t); i++) d[i] += s[i];
  dst->max_frame_bytes = mf;
}

/* Every array an IrFunc owns lives in one arena and dies with it.  Building
   the IR for a function was seven million allocator calls for a 2.7M module,
   about a third of the compile: a block owns six separately malloc'd arrays,
   ir_free walked all of them, and sixteen worker threads then contended for
   the system allocator -- which is why one thread cost 0.86s of CPU and
   sixteen cost 1.45s for the same work.  Growing copies rather than extending
   the last allocation in place: the space an abandoned array leaves is
   bounded by what the doubling would have wasted anyway, and extending in
   place meant handing back a pointer to be used past the size it was
   allocated with, which is a thing the optimiser is entitled to reason
   about and did. */
struct IrChunk { struct IrChunk *next; size_t cap; };

/* Chunks start small and double, each filling its allocator size class: a
   fixed 64 KB was a 128 KB allocation once the allocator's header was added,
   for every function built -- and the inliner keeps the IR of every callee
   it considers while it compiles a caller, most of them a few instructions
   long. */
#define IR_CHUNK_FIRST (16u * 1024u)
#define IR_CHUNK_MAX ((size_t)1 << 20)
#define IR_ALIGN(n) (((n) + 15u) & ~(size_t)15u)

void *ir_alloc(IrFunc *f, size_t n) {
  n = IR_ALIGN(n);
  if (n > f->bump_left) {
    size_t want = IR_CHUNK_FIRST;
    if (f->chunks) want = f->chunks->cap * 2 > IR_CHUNK_MAX ? IR_CHUNK_MAX : f->chunks->cap * 2;
    size_t cap = n + sizeof(struct IrChunk);
    if (cap < want) cap = want;
    cap = xmalloc_fit(cap);
    struct IrChunk *c = xmalloc(cap);
    c->next = f->chunks;
    c->cap = cap;
    f->chunks = c;
    f->bump = (char *)c + sizeof(struct IrChunk);
    f->bump_left = cap - sizeof(struct IrChunk);
  }
  void *p = f->bump;
  f->bump += n;
  f->bump_left -= n;
  return p;
}

void *ir_grow(IrFunc *f, void *p, size_t oldn, size_t newn) {
  void *q = ir_alloc(f, newn);
  if (oldn && p) memcpy(q, p, oldn);
  return q;
}

#define GROW(f, arr, count, cap, type)                                         \
  do {                                                                         \
    if ((count) == (cap)) {                                                    \
      uint32_t oldcap_ = (cap);                                                \
      (cap) = (cap) ? (cap) * 2 : 8;                                           \
      (arr) = ir_grow((f), (arr), (size_t)oldcap_ * sizeof(type),              \
                      (size_t)(cap) * sizeof(type));                           \
    }                                                                          \
  } while (0)

/* The wasm body is the one thing known before any of it is read, and it
   predicts the shape well enough to start the two arrays that dominate near
   where they will end: roughly an instruction per byte and a block per forty.
   Starting at eight instead means a 4000-instruction function copies its
   instruction array nine times on the way up. */
IrFunc *ir_new(Module *m, uint32_t func_index) {
  IrFunc *f = xcalloc(1, sizeof(IrFunc));
  f->m = m;
  f->func_index = func_index;
  f->sig = &m->types[m->funcs[func_index].type_index];
  f->nlocals = m->funcs[func_index].ntotal_locals;
  f->local_types = m->funcs[func_index].local_types;
  f->entry = REF_NONE;

  const Func *fn = &m->funcs[func_index];
  size_t body = fn->body && fn->body_end > fn->body
                  ? (size_t)(fn->body_end - fn->body) : 0;
  if (body) {
    uint32_t ni = body > (1u << 22) ? (1u << 22) : (uint32_t)body;
    if (ni < 16) ni = 16;
    f->cap_insts = ni;
    f->insts = ir_alloc(f, (size_t)ni * sizeof(Inst));
    f->subst = ir_alloc(f, (size_t)ni * sizeof(Ref));

    uint32_t nb = ni / 40;
    if (nb < 8) nb = 8;
    f->cap_blocks = nb;
    f->blocks = ir_alloc(f, (size_t)nb * sizeof(Block));

    f->cap_argpool = ni;
    f->argpool = ir_alloc(f, (size_t)ni * sizeof(Ref));
  }
  return f;
}

void ir_free(IrFunc *f) {
  if (!f) return;
  xfree(f->kb_epoch);
  xfree(f->kb_bits);
  xfree(f->ord_tmp);
  struct IrChunk *c = f->chunks;
  while (c) {
    struct IrChunk *n = c->next;
    xfree(c);
    c = n;
  }
  xfree(f);
}

uint32_t ir_new_block(IrFunc *f) {
  GROW(f, f->blocks, f->nblocks, f->cap_blocks, Block);
  Block *b = &f->blocks[f->nblocks];
  memset(b, 0, sizeof *b);
  b->term = T_NONE;
  b->idom = REF_NONE;
  return f->nblocks++;
}

/* subst is indexed by Ref exactly as insts is, and used to be reallocated on
   every single instruction rather than doubled -- one allocator call per IR
   value built.  It grows with insts now, in the same branch. */
static Ref alloc_inst(IrFunc *f) {
  if (f->ninsts == f->cap_insts) {
    uint32_t oldcap = f->cap_insts;
    f->cap_insts = f->cap_insts ? f->cap_insts * 2 : 8;
    f->insts = ir_grow(f, f->insts, (size_t)oldcap * sizeof(Inst),
                       (size_t)f->cap_insts * sizeof(Inst));
    f->subst = ir_grow(f, f->subst, (size_t)oldcap * sizeof(Ref),
                       (size_t)f->cap_insts * sizeof(Ref));
  }
  memset(&f->insts[f->ninsts], 0, sizeof(Inst));
  f->subst[f->ninsts] = f->ninsts;
  return f->ninsts++;
}

Ref ir_add_param(IrFunc *f, uint32_t block, ValType t) {
  Ref r = alloc_inst(f);
  Inst *in = &f->insts[r];
  in->op = I_PARAM;
  in->type = t;
  in->block = block;
  Block *b = &f->blocks[block];
  GROW(f, b->params, b->nparams, b->cap_params, Ref);
  b->params[b->nparams++] = r;
  return r;
}

Ref ir_emit(IrFunc *f, uint32_t block, IrOp op, ValType type,
            const Ref *args, uint32_t nargs, uint64_t imm, uint64_t imm2) {
  Ref r = alloc_inst(f);
  Inst *in = &f->insts[r];
  in->op = (uint16_t)op;

  in->type = type;
  in->block = block;
  in->imm = imm;
  in->imm2 = imm2;
  in->nargs = nargs;
  if (nargs) {
    uint32_t oldcap = f->cap_argpool;
    while (f->nargpool + nargs > f->cap_argpool)
      f->cap_argpool = f->cap_argpool ? f->cap_argpool * 2 : 64;
    if (f->cap_argpool != oldcap)
      f->argpool = ir_grow(f, f->argpool, (size_t)oldcap * sizeof(Ref),
                           (size_t)f->cap_argpool * sizeof(Ref));
    in->args_off = f->nargpool;
    memcpy(&f->argpool[f->nargpool], args, nargs * sizeof(Ref));
    f->nargpool += nargs;
  }
  Block *b = &f->blocks[block];
  GROW(f, b->insts, b->ninsts, b->cap_insts, uint32_t);
  b->insts[b->ninsts++] = r;
  return r;
}

Ref ir_const(IrFunc *f, uint32_t block, ValType t, uint64_t bits) {
  if (t == VT_I32 || t == VT_F32) bits &= 0xffffffffu;
  return ir_emit(f, block, I_CONST, t, NULL, 0, bits, 0);
}

Ref ir_resolve(IrFunc *f, Ref r) {
  if (r == REF_NONE) return r;
  while (f->subst[r] != r) r = f->subst[r];
  return r;
}

void ir_replace(IrFunc *f, Ref from, Ref to) {
  if (from == to) return;
  f->subst[from] = to;
}

void ir_apply_subst(IrFunc *f) {
  for (uint32_t i = 0; i < f->ninsts; i++) {
    Inst *in = &f->insts[i];
    Ref *a = &f->argpool[in->args_off];
    for (uint32_t j = 0; j < in->nargs; j++) a[j] = ir_resolve(f, a[j]);
  }
  for (uint32_t bi = 0; bi < f->nblocks; bi++) {
    Block *b = &f->blocks[bi];
    if (b->term == T_BRIF || b->term == T_BRTABLE) b->term_arg = ir_resolve(f, b->term_arg);
    for (uint32_t j = 0; j < b->nret; j++) b->ret_args[j] = ir_resolve(f, b->ret_args[j]);
    for (uint32_t s = 0; s < b->nsuccs; s++) {
      Edge *e = &b->succs[s];
      for (uint32_t j = 0; j < e->nargs; j++) e->args[j] = ir_resolve(f, e->args[j]);
    }

    uint32_t k = 0;
    for (uint32_t j = 0; j < b->ninsts; j++) {
      Ref r = b->insts[j];
      if (ir_resolve(f, r) != r) continue;
      b->insts[k++] = r;
    }
    b->ninsts = k;
  }
}

void ir_add_edge(IrFunc *f, uint32_t from, uint32_t to, const Ref *args, uint32_t nargs) {
  Block *b = &f->blocks[from];
  GROW(f, b->succs, b->nsuccs, b->cap_succs, Edge);
  Edge *e = &b->succs[b->nsuccs++];
  memset(e, 0, sizeof *e);
  e->target = to;
  if (nargs) {
    e->args = ir_alloc(f, nargs * sizeof(Ref));
    memcpy(e->args, args, nargs * sizeof(Ref));
    e->nargs = e->cap = nargs;
  }
  Block *t = &f->blocks[to];
  GROW(f, t->preds, t->npreds, t->cap_preds, uint32_t);
  t->preds[t->npreds++] = from;
}

void ir_add_eh_edge(IrFunc *f, uint32_t from, uint32_t to) {
  ir_add_edge(f, from, to, NULL, 0);
  f->blocks[from].succs[f->blocks[from].nsuccs - 1].eh = true;
}

bool ir_block_has_eh(const IrFunc *f, uint32_t bi) {
  const Block *b = &f->blocks[bi];
  for (uint32_t s = 0; s < b->nsuccs; s++) if (b->succs[s].eh) return true;
  return false;
}

IrEdgeIdx ir_edge_index(IrFunc *f) {
  IrEdgeIdx x;
  uint32_t n = f->nblocks, tot = 0;
  x.off = xcalloc(n + 1, sizeof(uint32_t));
  for (uint32_t i = 0; i < n; i++) {
    tot += f->blocks[i].nsuccs;
    for (uint32_t s = 0; s < f->blocks[i].nsuccs; s++)
      x.off[f->blocks[i].succs[s].target + 1]++;
  }
  for (uint32_t i = 0; i < n; i++) x.off[i + 1] += x.off[i];
  x.src = xmalloc((tot ? tot : 1) * sizeof(uint32_t));
  x.si = xmalloc((tot ? tot : 1) * sizeof(uint32_t));
  uint32_t *fill = xmalloc((n ? n : 1) * sizeof(uint32_t));
  memcpy(fill, x.off, n * sizeof(uint32_t));
  for (uint32_t i = 0; i < n; i++)
    for (uint32_t s = 0; s < f->blocks[i].nsuccs; s++) {
      uint32_t k = fill[f->blocks[i].succs[s].target]++;
      x.src[k] = i; x.si[k] = s;
    }
  xfree(fill);
  return x;
}

void ir_edge_index_free(IrEdgeIdx *x) { xfree(x->off); xfree(x->src); xfree(x->si); }

void ir_recompute_preds(IrFunc *f) {
  for (uint32_t i = 0; i < f->nblocks; i++) f->blocks[i].npreds = 0;
  for (uint32_t i = 0; i < f->nblocks; i++) {
    Block *b = &f->blocks[i];
    if (!b->reachable) continue;
    for (uint32_t s = 0; s < b->nsuccs; s++) {
      Block *t = &f->blocks[b->succs[s].target];
      GROW(f, t->preds, t->npreds, t->cap_preds, uint32_t);
      t->preds[t->npreds++] = i;
    }
  }
}

bool ir_dom_range(const IrFunc *f, uint32_t a, uint32_t b) {
  if (a == b) return true;
  const Block *A = &f->blocks[a], *B = &f->blocks[b];
  return A->dom_in <= B->dom_in && B->dom_out <= A->dom_out &&
         A->dom_in != UINT32_MAX;
}

bool ir_dom_walk(const IrFunc *f, uint32_t a, uint32_t b) {
  for (;;) {
    if (a == b) return true;
    if (b == f->entry) return false;
    uint32_t d = f->blocks[b].idom;
    if (d == REF_NONE || d == b) return false;
    b = d;
  }
}

/* Read before the first ordering computes a range, which every compile
   thread does before it asks a dominance question. */
static bool g_dom_check;
static pthread_once_t g_dom_check_once = PTHREAD_ONCE_INIT;
static void dom_check_init(void) { g_dom_check = warmc_getenv("WARMC_CHECK_DOM") != NULL; }

bool ir_dominates_checked(const IrFunc *f, uint32_t a, uint32_t b) {
  bool r = ir_dom_range(f, a, b);
  if (g_dom_check && r != ir_dom_walk(f, a, b))
    warmc_fatal("internal: dominator range test says %s for %u dom %u and the "
                "idom walk says %s", r ? "yes" : "no", a, b, r ? "no" : "yes");
  return r;
}

static bool block_dominates(IrFunc *f, uint32_t a, uint32_t b) {
  return ir_dominates_checked(f, a, b);
}

static int branch_bias(const IrFunc *f, const Block *b) {
  if (b->term != T_BRIF || b->nsuccs != 2) return 0;
  Ref c = b->term_arg;
  if (c >= f->ninsts) return 0;
  const Inst *in = &f->insts[c];
  if (in->op == I_EQZ || in->op == I_REF_IS_NULL) return -1;
  if (in->nargs != 2) return 0;
  const Ref *a = &f->argpool[in->args_off];
  const Inst *rhs = a[1] < f->ninsts ? &f->insts[a[1]] : NULL;
  bool rhs_const = rhs && rhs->op == I_CONST;
  bool rhs_zero = rhs_const && rhs->imm == 0;
  switch (in->op) {

    case I_LTS: case I_LES: return rhs_zero ? -1 : 0;
    case I_GES: case I_GTS: return rhs_zero ? 1 : 0;

    case I_EQ: return rhs_const ? -1 : 0;
    case I_NE: return rhs_const ? 1 : 0;
    default: return 0;
  }
}

static int layout_score_of(const IrFunc *f, const Block *b, uint32_t si);

int ir_succ_score(const IrFunc *f, const Block *b, uint32_t si) {
  return layout_score_of(f, b, si);
}

static int layout_score_of(const IrFunc *f, const Block *b, uint32_t si) {
  const Edge *e = &b->succs[si];
  const Block *t = &f->blocks[e->target];
  if (e->eh || t->is_pad) return -1000;

  if (t->diverges || t->cold) return -100;
  int score = 4 * (int)(t->loop_depth < 8 ? t->loop_depth : 8);
  int bias = branch_bias(f, b);
  if (bias) score += (si == 0) ? bias : -bias;
  return score;
}

/* stk0 holds cap entries; a search that needs more moves to the heap */
static void dfs_post(IrFunc *f, uint32_t entry, uint8_t *seen, uint32_t *post,
                     uint32_t *n, bool by_score, uint32_t *stk0, uint32_t cap) {
  uint32_t sp = 0;
  uint32_t *stk = stk0;

#define DFS_PUSH(blk, ph) do {                                                 \
    if (sp == cap) {                                                           \
      cap *= 2;                                                                \
      if (stk == stk0) {                                                       \
        stk = xmalloc(cap * 2 * sizeof(uint32_t));                             \
        memcpy(stk, stk0, sp * 2 * sizeof(uint32_t));                          \
      } else stk = xrealloc(stk, cap * 2 * sizeof(uint32_t));                  \
    }                                                                          \
    stk[sp * 2] = (blk); stk[sp * 2 + 1] = (ph); sp++;                         \
  } while (0)

  DFS_PUSH(entry, 0);
  while (sp) {
    sp--;
    uint32_t bi = stk[sp * 2], leaving = stk[sp * 2 + 1];
    if (leaving) { post[(*n)++] = bi; continue; }
    if (seen[bi]) continue;
    seen[bi] = 1;
    Block *b = &f->blocks[bi];

    DFS_PUSH(bi, 1);

    if (!by_score || b->nsuccs < 2) {
      for (uint32_t s = b->nsuccs; s-- > 0;) DFS_PUSH(b->succs[s].target, 0);
      continue;
    }

    uint32_t idx[64];
    int sc[64];
    uint32_t ns = b->nsuccs < 64 ? b->nsuccs : 64;
    for (uint32_t i = 0; i < ns; i++) { idx[i] = i; sc[i] = layout_score_of(f, b, i); }
    for (uint32_t i = 0; i < ns; i++)
      for (uint32_t j = i + 1; j < ns; j++)
        if (sc[idx[j]] < sc[idx[i]]) {
          uint32_t tmp = idx[i]; idx[i] = idx[j]; idx[j] = tmp;
        }

    for (uint32_t i = b->nsuccs; i-- > ns;) DFS_PUSH(b->succs[i].target, 0);
    for (uint32_t i = ns; i-- > 0;) DFS_PUSH(b->succs[idx[i]].target, 0);
  }
#undef DFS_PUSH
  if (stk != stk0) xfree(stk);
}

/* Returns whether every retreating edge was natural, which makes the graph
   reducible: then the dominators, the predecessor lists and the loop depths
   are the graph's own and not the search order's, and a second ordering
   given keep = true takes them as they are and renumbers only what follows
   the order -- rpo, and the dominator tree's interval numbering. */
static bool order_once(IrFunc *f, bool by_score, bool keep) {
  bool reducible = true;
  /* Every array below is the function's blocks long, or one more, and the
     search's stack twice that: carved from one buffer the function keeps.
     An ordering is redone after most passes that touch the graph, and this
     runs twice for each -- 118,000 times in compiling stdgotests/webtext --
     where allocating its dozen arrays each time was half a percent. */
  uint32_t nb = f->nblocks;
  size_t words = 14 * (size_t)nb + 64;
  if (words > f->ord_tmp_cap) {
    xfree(f->ord_tmp);
    f->ord_tmp_cap = words + words / 2;
    f->ord_tmp = xmalloc(f->ord_tmp_cap * sizeof(uint32_t));
  }
  uint32_t *w = f->ord_tmp;
  uint32_t *post = w;      w += nb;
  uint32_t *dstk = w;      w += 2 * ((size_t)nb + 8);
  uint32_t *stamp = w;     w += nb + 1;
  uint32_t *kids_at = w;   w += nb + 1;
  uint32_t *kids = w;      w += nb + 1;
  uint32_t *fill = w;      w += nb + 1;
  uint32_t *dstack = w;    w += nb + 1;
  uint32_t *taken = w;     w += nb + 1;
  uint32_t *lstack = w;    w += nb + 1;
  uint32_t *body = w;      w += nb + 1;
  uint8_t *seen = (uint8_t *)w;   w += nb / 4 + 1;
  uint8_t *inloop = (uint8_t *)w; w += nb / 4 + 1;
  uint8_t *fwd = (uint8_t *)w;
  memset(seen, 0, nb);
  uint32_t n = 0;
  dfs_post(f, f->entry, seen, post, &n, by_score, dstk, nb + 8);

  if (n > f->cap_order) {
    f->order = ir_grow(f, f->order, (size_t)f->cap_order * sizeof(uint32_t),
                       (size_t)n * sizeof(uint32_t));
    f->cap_order = n;
  }
  f->norder = n;
  for (uint32_t i = 0; i < n; i++) f->order[i] = post[n - 1 - i];
  for (uint32_t i = 0; i < f->nblocks; i++) {
    f->blocks[i].reachable = seen[i] != 0;
    f->blocks[i].rpo = 0xffffffffu;
    if (!keep) f->blocks[i].loop_depth = 0;
  }
  for (uint32_t i = 0; i < n; i++) f->blocks[f->order[i]].rpo = i;

  if (!keep) {
  ir_recompute_preds(f);

  for (uint32_t i = 0; i < f->nblocks; i++) f->blocks[i].idom = REF_NONE;
  f->blocks[f->entry].idom = f->entry;
  /* Every block a walk passes for one block's predecessors lies below the
     dominator found so far, which only climbs, so a later walk that meets
     one already has its answer.  Without the stamps a landing pad that
     thousands of calls unwind to walked the dominator chain once for each
     of them: Kotlin's main, nine tenths of its compile. */
  memset(stamp, 0, nb * sizeof(uint32_t));
  uint32_t cur = 0;
  bool changed = true;
  while (changed) {
    changed = false;
    for (uint32_t i = 1; i < n; i++) {
      uint32_t bi = f->order[i];
      Block *b = &f->blocks[bi];
      uint32_t newidom = REF_NONE;
      cur++;
      for (uint32_t p = 0; p < b->npreds; p++) {
        uint32_t pi = b->preds[p];
        if (f->blocks[pi].idom == REF_NONE) continue;
        if (newidom == REF_NONE) { newidom = pi; stamp[pi] = cur; continue; }
        uint32_t x = pi, y = newidom;
        while (x != y) {
          while (f->blocks[x].rpo > f->blocks[y].rpo) {
            if (stamp[x] == cur) { x = y; break; }
            stamp[x] = cur;
            x = f->blocks[x].idom;
          }
          while (f->blocks[y].rpo > f->blocks[x].rpo) {
            stamp[y] = cur;
            y = f->blocks[y].idom;
          }
        }
        newidom = x;
        stamp[x] = cur;
      }
      if (newidom != REF_NONE && b->idom != newidom) { b->idom = newidom; changed = true; }
    }
  }
  }

  for (uint32_t i = 0; i < f->nblocks; i++) {
    f->blocks[i].dom_in = UINT32_MAX;
    f->blocks[i].dom_out = 0;
  }
  if (n) {
    memset(kids_at, 0, ((size_t)nb + 1) * sizeof(uint32_t));
    for (uint32_t i = 1; i < n; i++) {
      uint32_t bi = f->order[i], d = f->blocks[bi].idom;
      if (d != REF_NONE && d != bi) kids_at[d + 1]++;
    }
    for (uint32_t i = 0; i < f->nblocks; i++) kids_at[i + 1] += kids_at[i];
    for (uint32_t i = 0; i < f->nblocks; i++) fill[i] = kids_at[i];
    for (uint32_t i = 1; i < n; i++) {
      uint32_t bi = f->order[i], d = f->blocks[bi].idom;
      if (d != REF_NONE && d != bi) kids[fill[d]++] = bi;
    }

    uint32_t *stack = dstack;
    uint32_t sp = 0, clock = 0;
    stack[sp] = f->entry; taken[sp] = 0;
    f->blocks[f->entry].dom_in = clock++;
    while (1) {
      uint32_t bi = stack[sp];
      if (taken[sp] < kids_at[bi + 1] - kids_at[bi]) {
        uint32_t c = kids[kids_at[bi] + taken[sp]++];
        f->blocks[c].dom_in = clock++;
        sp++;
        stack[sp] = c; taken[sp] = 0;
        continue;
      }
      f->blocks[bi].dom_out = clock++;
      if (!sp) break;
      sp--;
    }
  }

  /* Each back edge's loop body, walked up from the latch.  The walk lists
     the blocks it marks, so a loop costs its own size: clearing and scanning
     every block per back edge made this blocks times loops, which in a
     function of 78,000 blocks and 300 loops was most of the compile. */
  if (keep) return true;
  memset(inloop, 0, nb);
  memset(fwd, 0, nb);
  uint32_t *stack = lstack;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t bi = f->order[i];
    Block *b = &f->blocks[bi];
    for (uint32_t s = 0; s < b->nsuccs; s++) {
      uint32_t t = b->succs[s].target;
      if (f->blocks[t].rpo > b->rpo) continue;
      bool natural = block_dominates(f, t, bi);
      if (!natural) reducible = false;
      uint32_t sp = 0, nbody = 0;
      inloop[t] = 1;
      body[nbody++] = t;

      if (bi != t) { inloop[bi] = 1; body[nbody++] = bi; stack[sp++] = bi; }
      while (sp) {
        uint32_t cur = stack[--sp];
        Block *c = &f->blocks[cur];
        for (uint32_t p = 0; p < c->npreds; p++)
          if (!inloop[c->preds[p]]) {
            inloop[c->preds[p]] = 1;
            body[nbody++] = c->preds[p];
            stack[sp++] = c->preds[p];
          }
      }
      if (!natural) {
        /* Only the blocks t reaches stay.  The walk from t need not leave
           the body: a block on a path from t to one in the body reaches bi
           through that one, without passing t, so it is in the body too.
           Walking the whole function from t, once for each such edge, was
           a tenth of compiling Go, whose threaded dispatch leaves hundreds
           of them. */
        fwd[t] = 1; sp = 0; stack[sp++] = t;
        while (sp) {
          uint32_t cur = stack[--sp];
          Block *c = &f->blocks[cur];
          for (uint32_t e = 0; e < c->nsuccs; e++) {
            uint32_t g = c->succs[e].target;
            if (inloop[g] && !fwd[g]) { fwd[g] = 1; stack[sp++] = g; }
          }
        }
        for (uint32_t j = 0; j < nbody; j++)
          if (!fwd[body[j]]) inloop[body[j]] = 0;
        for (uint32_t j = 0; j < nbody; j++) fwd[body[j]] = 0;
      }
      for (uint32_t j = 0; j < nbody; j++) {
        uint32_t k = body[j];
        if (!inloop[k]) continue;
        inloop[k] = 0;
        if (natural && !ir_dom_range(f, t, k))
          warmc_fatal("internal: block %u is in the loop headed by %u but "
                      "is not dominated by it", k, t);
        f->blocks[k].loop_depth++;
      }
    }
  }
  return reducible;
}

static bool diverge_step(IrFunc *f, uint32_t i) {
  Block *b = &f->blocks[i];
  if (!b->diverges) return false;
  for (uint32_t s = 0; s < b->nsuccs; s++)
    if (!f->blocks[b->succs[s].target].diverges) {
      b->diverges = false;
      return true;
    }
  return false;
}

static void mark_diverging(IrFunc *f) {
  for (uint32_t i = 0; i < f->nblocks; i++)
    f->blocks[i].diverges = f->blocks[i].term != T_RETURN;

  bool changed = true;
  while (changed) {
    changed = false;
    for (uint32_t k = f->norder; k-- > 0;)
      if (diverge_step(f, f->order[k])) changed = true;
  }
}

static size_t order_sig_len_of(const IrFunc *f) {
  size_t k = 2;
  for (uint32_t i = 0; i < f->nblocks; i++)
    k += 5 + 2 * (size_t)f->blocks[i].nsuccs;
  return k;
}

static void order_sig_fill(const IrFunc *f, uint32_t *o) {
  size_t k = 0;
  o[k++] = f->nblocks;
  o[k++] = f->entry;
  for (uint32_t i = 0; i < f->nblocks; i++) {
    const Block *b = &f->blocks[i];
    o[k++] = b->reachable;
    o[k++] = b->is_pad;
    o[k++] = b->term;
    o[k++] = b->nsuccs;
    o[k++] = (uint32_t)(branch_bias(f, b) + 2);
    for (uint32_t s = 0; s < b->nsuccs; s++) {
      o[k++] = b->succs[s].target;
      o[k++] = b->succs[s].eh;
    }
  }
}

/* How many of the function's runs reach each block, as a share of its
   entry, by the one estimate that needs no profile: every branch splits
   evenly between the successors that can return, and a path that cannot
   gets nothing.  Only the top level is split.  A loop is taken whole -- it
   runs when its header is reached, its blocks share the header's share,
   and its exits divide that share between them -- so inside a loop this is
   what reaches the loop, and how often the loop goes round stays the loop
   depth's business, as everywhere else.  It is a guess, and its users
   take it only where being wrong costs little: it tells the growth path
   of a push from the push.  Needs ir_order's dominators, depths and RPO. */
void ir_path_share(const IrFunc *f, float *share) {
  uint32_t nb = f->nblocks;
  uint32_t *top = xmalloc((nb ? nb : 1) * sizeof(uint32_t));
  uint32_t *nexit = xcalloc(nb ? nb : 1, sizeof(uint32_t));
  for (uint32_t i = 0; i < nb; i++) share[i] = 0;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi], t = bi;
    while (f->blocks[t].loop_depth && t != f->entry &&
           f->blocks[t].idom != REF_NONE && f->blocks[t].idom != t &&
           f->blocks[f->blocks[t].idom].loop_depth)
      t = f->blocks[t].idom;
    top[bi] = f->blocks[bi].loop_depth ? t : bi;
  }
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    const Block *b = &f->blocks[f->order[oi]];
    if (!b->loop_depth) continue;
    for (uint32_t s = 0; s < b->nsuccs; s++) {
      const Block *t = &f->blocks[b->succs[s].target];
      if (!t->loop_depth && t->rpo > b->rpo && !t->diverges &&
          !b->succs[s].eh)
        nexit[top[f->order[oi]]]++;
    }
  }
  share[f->entry] = 1;
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    const Block *b = &f->blocks[bi];
    if (b->loop_depth && top[bi] != bi) share[bi] = share[top[bi]];
    uint32_t n = 0;
    for (uint32_t s = 0; s < b->nsuccs; s++) {
      const Block *t = &f->blocks[b->succs[s].target];
      if (t->rpo <= b->rpo || t->diverges || b->succs[s].eh) continue;
      if (b->loop_depth && t->loop_depth) continue;
      n++;
    }
    if (!n) continue;
    float out = b->loop_depth ? share[top[bi]] / (float)nexit[top[bi]]
                              : share[bi] / (float)n;
    for (uint32_t s = 0; s < b->nsuccs; s++) {
      uint32_t ti = b->succs[s].target;
      const Block *t = &f->blocks[ti];
      if (t->rpo <= b->rpo || t->diverges || b->succs[s].eh) continue;
      if (b->loop_depth && t->loop_depth) continue;
      share[ti] += out;
    }
  }
  for (uint32_t i = 0; i < nb; i++) if (share[i] > 1) share[i] = 1;
  xfree(top); xfree(nexit);
}

void ir_order(IrFunc *f) { (void)ir_order_changed(f); }

bool ir_order_changed(IrFunc *f) {
  pthread_once(&g_dom_check_once, dom_check_init);
  size_t len = order_sig_len_of(f);
  if (len > f->order_sig_cap) {
    size_t osc_ = f->order_sig_cap;
    f->order_sig_cap = len * 2;
    f->order_sig = ir_grow(f, f->order_sig, osc_ * sizeof(uint32_t),
                           f->order_sig_cap * sizeof(uint32_t));
    f->order_sig_new = ir_grow(f, f->order_sig_new, osc_ * sizeof(uint32_t),
                               f->order_sig_cap * sizeof(uint32_t));
  }
  order_sig_fill(f, f->order_sig_new);
  if (f->order_sig_valid && f->order_sig_len == len &&
      memcmp(f->order_sig, f->order_sig_new, len * sizeof(uint32_t)) == 0)
    return false;

  bool reducible = order_once(f, false, false);

  mark_diverging(f);
  order_once(f, true, reducible);

  f->order_sig_len = order_sig_len_of(f);
  order_sig_fill(f, f->order_sig);
  f->order_sig_valid = true;
  return true;
}

/* Before code generation: number the instructions densely, in their old
   order, dropping those nothing holds -- by then twice as many have been
   made as are left, and every array instruction selection and the
   allocator keep per value (some fifty) is sized by the count.  Kept: every
   instruction a block lists, live or not (collect_refs reads unreachable
   blocks too), every param, and whatever those, a branch, a return or an
   edge reads.  The order is kept, so every comparison of values decides as
   it did; the few decisions that read the whole array rather than the code
   get what it held from made_insts, made_maxargs and the ghost flags.
   Substitutions are applied first, and the renumbering starts the map
   afresh: refusing to compact while any was recorded skipped all but six of
   stdgotests/webtext's 5243 functions, since one applied stays recorded. */
void ir_compact(IrFunc *f) {
  ir_apply_subst(f);
  uint32_t n = f->ninsts;
  uint8_t *keep = xcalloc(n ? n : 1, 1);
  Ref *stack = xmalloc((n ? n : 1) * sizeof(Ref));
  uint32_t sp = 0;
#define KEEP(r) do { Ref r_ = (r); if (r_ < n && !keep[r_]) { keep[r_] = 1; stack[sp++] = r_; } } while (0)
  for (uint32_t bi = 0; bi < f->nblocks; bi++) {
    Block *b = &f->blocks[bi];
    for (uint32_t p = 0; p < b->nparams; p++) KEEP(b->params[p]);
    for (uint32_t i = 0; i < b->ninsts; i++) KEEP(b->insts[i]);
    KEEP(b->term_arg);
    for (uint32_t i = 0; i < b->nret; i++) KEEP(b->ret_args[i]);
    for (uint32_t s = 0; s < b->nsuccs; s++)
      for (uint32_t j = 0; j < b->succs[s].nargs; j++) KEEP(b->succs[s].args[j]);
  }
  while (sp) {
    Ref v = stack[--sp];
    const Ref *a = &f->argpool[f->insts[v].args_off];
    for (uint32_t j = 0; j < f->insts[v].nargs; j++) KEEP(a[j]);
  }
#undef KEEP
  xfree(stack);

  uint32_t newn = 0, maxargs = 0, nargs = 0;
  bool gsp = false, gref = false;
  for (uint32_t v = 0; v < n; v++) {
    const Inst *in = &f->insts[v];
    if (in->nargs > maxargs) maxargs = in->nargs;
    if (keep[v]) { newn++; nargs += in->nargs; continue; }
    if (ir_op_is_gc_safepoint(in->op)) gsp = true;
    if (vt_is_ref((ValType)in->type)) gref = true;
  }
  if (newn == n) { xfree(keep); return; }

  Ref *map = xmalloc((n ? n : 1) * sizeof(Ref));
  Inst *ni = ir_alloc(f, (size_t)(newn ? newn : 1) * sizeof(Inst));
  Ref *na = ir_alloc(f, (size_t)(nargs ? nargs : 1) * sizeof(Ref));
  Ref *ns = ir_alloc(f, (size_t)(newn ? newn : 1) * sizeof(Ref));
  uint32_t k = 0;
  for (uint32_t v = 0; v < n; v++) map[v] = keep[v] ? k++ : REF_NONE;
  uint32_t off = 0;
  for (uint32_t v = 0; v < n; v++) {
    if (!keep[v]) continue;
    Inst in = f->insts[v];
    const Ref *a = &f->argpool[in.args_off];
    for (uint32_t j = 0; j < in.nargs; j++) na[off + j] = map[a[j]];
    in.args_off = off;
    off += in.nargs;
    ni[map[v]] = in;
    ns[map[v]] = map[v];
  }
#define MAP(r) ((r) < n ? map[(r)] : (r))
  for (uint32_t bi = 0; bi < f->nblocks; bi++) {
    Block *b = &f->blocks[bi];
    for (uint32_t p = 0; p < b->nparams; p++) b->params[p] = MAP(b->params[p]);
    for (uint32_t i = 0; i < b->ninsts; i++) b->insts[i] = MAP(b->insts[i]);
    b->term_arg = MAP(b->term_arg);
    for (uint32_t i = 0; i < b->nret; i++) b->ret_args[i] = MAP(b->ret_args[i]);
    for (uint32_t s = 0; s < b->nsuccs; s++)
      for (uint32_t j = 0; j < b->succs[s].nargs; j++)
        b->succs[s].args[j] = MAP(b->succs[s].args[j]);
  }
#undef MAP
  f->insts = ni;
  f->ninsts = f->cap_insts = newn;
  f->argpool = na;
  f->nargpool = f->cap_argpool = nargs;
  f->subst = ns;
  xfree(f->kb_epoch); xfree(f->kb_bits);
  f->kb_epoch = NULL; f->kb_bits = NULL; f->kb_cap = 0; f->kb_gen = 0;
  f->made_insts = n;
  f->made_maxargs = maxargs;
  f->ghost_safepoint = gsp;
  f->ghost_ref = gref;
  xfree(map);
  xfree(keep);
}

int ir_const_cost(uint64_t bits, bool wide) {
  return g_target->const_cost(bits, wide);
}

int ir_vconst_cost(uint64_t lo, uint64_t hi) {
  return g_target->vconst_cost(lo, hi);
}

uint32_t ir_vec_copies(void) {
  return g_target->vec_copies;
}

bool ir_vec_mul_one(uint32_t lane) {
  return !VLANE_IS_FLOAT(lane) && ((g_target->vec_mul_lanes >> VLANE_SIZE(lane)) & 1);
}

bool ir_has_result(const Inst *in) {
  return in->type != 0;
}

static uint64_t branch_bound(IrFunc *f, Block *pb, uint32_t si, Ref v,
                             int depth) {
  if (pb->term != T_BRIF || si > 1 || pb->nsuccs < 2) return ~0ull;
  if (pb->succs[si].eh) return ~0ull;
  Ref c = pb->term_arg;
  Inst *ci = &f->insts[c];
  Ref *ca = ir_args(f, c);
  bool taken = (si == 0);
  if (ci->op == I_EQZ && ci->nargs == 1)
    return (ca[0] == v && taken) ? 0 : ~0ull;
  if (ci->nargs != 2) return ~0ull;
  if (f->insts[ca[0]].type != f->insts[ca[1]].type) return ~0ull;

  IrOp rel = (IrOp)ci->op;
  Ref other;
  if (ca[0] == v) other = ca[1];
  else if (ca[1] == v) {
    other = ca[0];
    switch (rel) {
      case I_LTU: rel = I_GTU; break;
      case I_GTU: rel = I_LTU; break;
      case I_LEU: rel = I_GEU; break;
      case I_GEU: rel = I_LEU; break;
      case I_EQ: case I_NE: break;
      default: return ~0ull;
    }
  } else return ~0ull;

  bool strict;
  switch (rel) {
    case I_LTU: if (!taken) return ~0ull; strict = true;  break;
    case I_LEU: if (!taken) return ~0ull; strict = false; break;
    case I_GTU: if (taken) return ~0ull;  strict = false; break;
    case I_GEU: if (taken) return ~0ull;  strict = true;  break;
    case I_EQ:  if (!taken) return ~0ull; strict = false; break;
    default: return ~0ull;
  }
  uint64_t om = ir_value_max(f, other, depth - 1);
  if (!strict) return om;
  return om == 0 ? 0 : om - 1;
}

/* The bound the branches above block pbi put on v, for blocks entered from
   one predecessor only: that predecessor's branch holds wherever the block
   runs, and so do the branches above it.  Jump threading leaves an edge from
   a block below the test that bounded its value -- the clamp
   `n > 1024 ? 1024 : n` and then `n == 0` -- and a parameter's bound must not
   end at the first block up. */
static uint64_t chain_bound(IrFunc *f, uint32_t pbi, Ref v, int depth) {
  uint64_t best = ~0ull;
  for (int up = 0; up < 3; up++) {
    Block *pb = &f->blocks[pbi];
    if (pb->npreds != 1) break;
    uint32_t qi = pb->preds[0];
    Block *qb = &f->blocks[qi];
    uint32_t sidx = 0, n = 0;
    for (uint32_t s = 0; s < qb->nsuccs; s++)
      if (qb->succs[s].target == pbi) { sidx = s; n++; }
    if (n != 1) break;
    uint64_t bb = branch_bound(f, qb, sidx, v, depth);
    if (bb < best) best = bb;
    pbi = qi;
  }
  return best;
}

uint64_t ir_value_max(IrFunc *f, Ref v, int depth) {
  Inst *in = &f->insts[v];
  int width = vt_is_64((ValType)in->type) ? 64 : 32;
  uint64_t all = (width == 64) ? ~0ull : 0xffffffffu;

  if (in->op == I_CONST) return in->imm & all;
  if (depth <= 0) return all;
  Ref *a = ir_args(f, v);
  switch (in->op) {
    case I_PARAM: {

      uint32_t bi = in->block;
      if (bi >= f->nblocks) return all;
      Block *b = &f->blocks[bi];
      uint32_t k = b->nparams;
      for (uint32_t i = 0; i < b->nparams; i++) if (b->params[i] == v) k = i;
      if (k == b->nparams || b->npreds == 0) return all;
      if (b->npreds > 4) return all;
      uint64_t r = 0;
      for (uint32_t pi = 0; pi < b->npreds; pi++) {
        Block *pb = &f->blocks[b->preds[pi]];
        bool seen = false;
        for (uint32_t si = 0; si < pb->nsuccs; si++) {
          if (pb->succs[si].target != bi) continue;
          if (k >= pb->succs[si].nargs) return all;
          Ref inv = pb->succs[si].args[k];
          uint64_t m = ir_value_max(f, inv, depth - 1);
          uint64_t bb = branch_bound(f, pb, si, inv, depth - 1);
          if (bb < m) m = bb;
          bb = chain_bound(f, b->preds[pi], inv, depth - 1);
          if (bb < m) m = bb;
          if (m > r) r = m;
          seen = true;
        }
        if (!seen) return all;
        if (r >= all) return all;
      }
      return r & all;
    }
    case I_AND: {

      uint64_t lo = ir_value_max(f, a[0], depth - 1);
      uint64_t hi = ir_value_max(f, a[1], depth - 1);
      return (lo < hi ? lo : hi) & all;
    }
    case I_ADD: {

      uint64_t x = ir_value_max(f, a[0], depth - 1);
      uint64_t y = ir_value_max(f, a[1], depth - 1);
      if (x > all - y) return all;
      return x + y;
    }
    case I_MUL: {
      uint64_t x = ir_value_max(f, a[0], depth - 1);
      uint64_t y = ir_value_max(f, a[1], depth - 1);
      if (x && y > all / x) return all;
      return x * y;
    }
    case I_OR: case I_XOR: {

      uint64_t m = ir_value_max(f, a[0], depth - 1) | ir_value_max(f, a[1], depth - 1);
      m |= m >> 1; m |= m >> 2; m |= m >> 4;
      m |= m >> 8; m |= m >> 16; m |= m >> 32;
      return m & all;
    }
    case I_SHRU:
      if (f->insts[a[1]].op == I_CONST)
        return ir_value_max(f, a[0], depth - 1) >> (f->insts[a[1]].imm % (uint64_t)width);
      return all;
    case I_SHL:

      if (f->insts[a[1]].op == I_CONST) {
        uint64_t mx = ir_value_max(f, a[0], depth - 1);
        uint64_t k = f->insts[a[1]].imm % (uint64_t)width;
        if (k && (mx >> (uint64_t)(width - (int)k))) return all;
        return (mx << k) & all;
      }
      return all;
    case I_SELECT: {
      uint64_t x = ir_value_max(f, a[0], depth - 1);
      uint64_t y = ir_value_max(f, a[1], depth - 1);
      return (x > y ? x : y) & all;
    }
    case I_LOAD: {

      uint32_t size = (uint32_t)MEMF_SIZE(in->imm2);
      if (in->imm2 & MEMF_SIGNED) return all;
      if (size >= 3) return all;
      return ((1ull << (8u << size)) - 1) & all;
    }

    case I_EXTENDU: return ir_value_max(f, a[0], depth - 1) & 0xffffffffu;

    case I_WRAP: {
      uint64_t x = ir_value_max(f, a[0], depth - 1);
      return x <= 0xffffffffu ? x : 0xffffffffu;
    }
    case I_CLZ: case I_CTZ: case I_POPCNT: return (uint64_t)width;
    case I_REMU:
      if (f->insts[a[1]].op == I_CONST && (f->insts[a[1]].imm & all))
        return (f->insts[a[1]].imm & all) - 1;
      return all;
    case I_DIVU:
      if (f->insts[a[1]].op == I_CONST && (f->insts[a[1]].imm & all))
        return ir_value_max(f, a[0], depth - 1) / (f->insts[a[1]].imm & all);
      return all;
    case I_EQ: case I_NE: case I_LTS: case I_LTU: case I_GTS: case I_GTU:
    case I_LES: case I_LEU: case I_GES: case I_GEU: case I_EQZ:
    case I_FEQ: case I_FNE: case I_FLT: case I_FGT: case I_FLE: case I_FGE:
    case I_REF_IS_NULL:
      return 1;
    default: return all;
  }
}

static uint64_t bit_smear(uint64_t m) {
  m |= m >> 1; m |= m >> 2; m |= m >> 4;
  m |= m >> 8; m |= m >> 16; m |= m >> 32;
  return m;
}

static uint64_t kb_rec(IrFunc *f, Ref v, int depth);

static uint64_t kb_compute(IrFunc *f, Ref v, int depth) {
  Inst *in = &f->insts[v];
  int width = vt_is_64((ValType)in->type) ? 64 : 32;
  uint64_t all = (width == 64) ? ~0ull : 0xffffffffu;
  if (depth <= 0) return all;
  Ref *a = ir_args(f, v);
  switch (in->op) {
    case I_CONST: return in->imm & all;
    case I_PARAM: {

      uint32_t bi = in->block;
      if (bi >= f->nblocks) return all;
      Block *b = &f->blocks[bi];
      uint32_t k = b->nparams;
      for (uint32_t i = 0; i < b->nparams; i++) if (b->params[i] == v) k = i;
      if (k == b->nparams || b->npreds == 0) return all;
      uint64_t r = 0;
      for (uint32_t pi = 0; pi < b->npreds; pi++) {
        Block *pb = &f->blocks[b->preds[pi]];
        bool seen = false;
        for (uint32_t si = 0; si < pb->nsuccs; si++) {
          if (pb->succs[si].target != bi) continue;
          if (k >= pb->succs[si].nargs) return all;
          r |= kb_rec(f, pb->succs[si].args[k], depth - 1);
          seen = true;
        }
        if (!seen) return all;
      }
      return r & all;
    }
    case I_AND:
      return kb_rec(f, a[0], depth - 1) &
             kb_rec(f, a[1], depth - 1) & all;
    case I_OR: case I_XOR: case I_SELECT:
      return (kb_rec(f, a[0], depth - 1) |
              kb_rec(f, a[1], depth - 1)) & all;
    case I_SHL:
      if (f->insts[a[1]].op != I_CONST) return all;
      return (kb_rec(f, a[0], depth - 1)
                << (f->insts[a[1]].imm % (uint64_t)width)) & all;
    case I_SHRU:
      if (f->insts[a[1]].op != I_CONST) return all;
      return (kb_rec(f, a[0], depth - 1) & all)
                >> (f->insts[a[1]].imm % (uint64_t)width);
    case I_LOAD: {
      uint32_t size = (uint32_t)MEMF_SIZE(in->imm2);
      if ((in->imm2 & MEMF_SIGNED) || size >= 3) return all;
      return ((1ull << (8u << size)) - 1) & all;
    }
    case I_EXTENDU: case I_WRAP:
      return kb_rec(f, a[0], depth - 1) & all;
    case I_EQ: case I_NE: case I_LTS: case I_LTU: case I_GTS: case I_GTU:
    case I_LES: case I_LEU: case I_GES: case I_GEU: case I_EQZ:
    case I_FEQ: case I_FNE: case I_FLT: case I_FGT: case I_FLE: case I_FGE:
    case I_REF_IS_NULL:
      return 1;

    case I_CLZ: case I_CTZ: case I_POPCNT:
      return bit_smear((uint64_t)width) & all;
    case I_REMU:
      if (f->insts[a[1]].op == I_CONST && (f->insts[a[1]].imm & all))
        return bit_smear((f->insts[a[1]].imm & all) - 1) & all;
      return all;
    case I_DIVU:
      if (f->insts[a[1]].op == I_CONST && (f->insts[a[1]].imm & all))
        return bit_smear(ir_value_max(f, a[0], depth - 1) /
                         (f->insts[a[1]].imm & all)) & all;
      return all;
    default: return all;
  }
}

#define KB_MAXD 4

static uint64_t kb_rec(IrFunc *f, Ref v, int depth) {
  if (depth < 0) depth = 0;
  if (depth > KB_MAXD || v >= f->ninsts || !f->kb_epoch)
    return kb_compute(f, v, depth);
  size_t idx = (size_t)v * (KB_MAXD + 1) + (size_t)depth;
  if (f->kb_epoch[idx] == f->kb_gen) return f->kb_bits[idx];
  uint64_t r = kb_compute(f, v, depth);
  f->kb_epoch[idx] = f->kb_gen;
  f->kb_bits[idx] = r;
  return r;
}

uint64_t ir_known_bits(IrFunc *f, Ref v, int depth) {
  size_t need = (size_t)f->ninsts * (KB_MAXD + 1);
  if (need > f->kb_cap) {
    size_t want = f->kb_cap ? f->kb_cap : (size_t)1024;
    while (want < need) want *= 2;
    xfree(f->kb_epoch);
    xfree(f->kb_bits);
    f->kb_epoch = xcalloc(want, sizeof(uint32_t));
    f->kb_bits = xmalloc(want * sizeof(uint64_t));
    f->kb_cap = want;
    f->kb_gen = 0;
  }

  if (++f->kb_gen == 0) {
    memset(f->kb_epoch, 0, f->kb_cap * sizeof(uint32_t));
    f->kb_gen = 1;
  }
  return kb_rec(f, v, depth);
}

bool ir_imm_ok(IrFunc *f, Ref user, uint32_t idx, uint64_t cval, ValType ct) {
  return g_target->imm_ok(f, user, idx, cval, ct);
}

bool ir_mem_disp_ok(uint64_t off, uint32_t size_log2) {
  return g_target->mem_disp_ok(off, size_log2);
}

const Target *g_target = &warmc_target_a64;

bool ir_has_no_effect(const Inst *in) {
  switch (in->op) {
    case I_PARAM: case I_CONST:
    case I_ADD: case I_SUB: case I_MUL: case I_MULHU: case I_MULHS:
    case I_AND: case I_OR: case I_XOR:
    case I_SHL: case I_SHRS: case I_SHRU: case I_ROTL: case I_ROTR:
    case I_CLZ: case I_CTZ: case I_POPCNT: case I_BSWAP:
    case I_EQ: case I_NE: case I_LTS: case I_LTU: case I_GTS: case I_GTU:
    case I_LES: case I_LEU: case I_GES: case I_GEU: case I_EQZ:
    case I_FADD: case I_FSUB: case I_FMUL: case I_FDIV:
    case I_FMIN: case I_FMAX: case I_FCOPYSIGN:
    case I_FABS: case I_FNEG: case I_FSQRT:
    case I_FCEIL: case I_FFLOOR: case I_FTRUNC: case I_FNEAREST: case I_FMA:
    case I_FEQ: case I_FNE: case I_FLT: case I_FGT: case I_FLE: case I_FGE:
    case I_WRAP: case I_EXTENDS: case I_EXTENDU:
    case I_TRUNC_SATS: case I_TRUNC_SATU:
    case I_CONVERTS: case I_CONVERTU: case I_DEMOTE: case I_PROMOTE:
    case I_REINTERPRET:
    case I_EXT8S: case I_EXT16S: case I_EXT32S:
    case I_GLOBAL_GET:
    case I_SELECT:
    case I_REF_NULL: case I_REF_IS_NULL: case I_REF_FUNC:
    case I_MEM_ADDR: case I_MEM_PTR: case I_MEM_BASE:

    case I_VLOAD: case I_VLDN:
    case I_VADD: case I_VSUB: case I_VMUL:
    case I_VAND: case I_VOR: case I_VXOR:
    case I_VSMAX: case I_VSMIN: case I_VUMAX: case I_VUMIN:
    case I_VUZP1: case I_VUZP2: case I_VZIP1: case I_VZIP2:
    case I_VCMP: case I_VSEL:
    case I_VSHL: case I_VSHRU: case I_VSHRS:
    case I_VFADD: case I_VFSUB: case I_VFMUL: case I_VFDIV:
    case I_VFMIN: case I_VFMAX:
    case I_VFNEG: case I_VFABS: case I_VFSQRT:
    case I_VSPLAT: case I_VREDUCE:
    case I_VNOT: case I_VANDNOT:
    case I_VNEG: case I_VABS: case I_VPOPCNT: case I_VREV:
    case I_VADDSATS: case I_VADDSATU: case I_VSUBSATS: case I_VSUBSATU:
    case I_VAVGRU: case I_VQ15MULR: case I_VDOT:
    case I_VEXTADD: case I_VEXTMUL: case I_VEXTEND: case I_VNARROW:
    case I_VEXTMLA: case I_VADDHN:
    case I_VSHLV: case I_VSHRUV: case I_VSHRSV:
    case I_VANYTRUE: case I_VALLTRUE: case I_VBITMASK:
    case I_VEXTRACTS: case I_VEXTRACTU: case I_VREPLACE:
    case I_VSHUFFLE: case I_VSWIZZLE:
    case I_VFPMIN: case I_VFPMAX:
    case I_VFCEIL: case I_VFFLOOR: case I_VFTRUNC: case I_VFNEAREST:
    case I_VTRUNCSAT: case I_VCONVERT: case I_VDEMOTE: case I_VPROMOTE:
    case I_VFMADD: case I_VDOTI7: case I_VDOTI7ADD:

    case I_EXN_OBJ: case I_EXN_PAYLOAD:

    case I_REF_I31:

    case I_REF_TEST: case I_REF_CONVERT: case I_ANY_OF_EXTERN: case I_EXTERN_OF_ANY:
    case I_NOP:
      return true;

    case I_STRUCT_NEW: case I_ARRAY_NEW: case I_ARRAY_NEW_SEG:
      return false;

    case I_ARRAY_OP:
      return false;

    case I_FIELD_GET: case I_FIELD_SET:
    case I_ARRAY_GET: case I_ARRAY_SET:
      return false;

    case I_ARRAY_LEN:
      return false;

    case I_I31_GET:
      return false;

    case I_DIVS: case I_DIVU: case I_REMS: case I_REMU:
    case I_TRUNCS: case I_TRUNCU:
      return false;

    case I_LOAD:
      return (in->imm2 & MEMF_NOCHECK) != 0;

    case I_TABLE_GET:
      return false;

    case I_MEM_SIZE: case I_TABLE_SIZE:
      return false;

    case I_MEM_LIMIT:
      return false;

    default:
      return false;
  }
}

/* What reads state its operands do not carry: memory, a mutable global,
   the exception being caught.  It has no effect, but two of it are not one
   value, nor is one of it the same before a write as after. */
static bool ir_reads_state(const Inst *in) {
  switch (in->op) {
    case I_GLOBAL_GET: case I_VLOAD: case I_VLDN: case I_EXN_OBJ:
      return true;
    case I_LOAD:
      return (in->imm2 & MEMF_NOCHECK) != 0;
    default:
      return false;
  }
}

bool ir_is_pure(const Inst *in) {
  return ir_has_no_effect(in) && !ir_reads_state(in);
}

const char *ir_op_name(uint16_t op) {

#define N(op, str) [op] = str
  static const char *names[I_OP_COUNT] = {
    N(I_PARAM, "param"), N(I_CONST, "const"),
    N(I_ADD, "add"), N(I_SUB, "sub"), N(I_MUL, "mul"), N(I_MULHU, "mulhu"), N(I_MULHS, "mulhs"),
    N(I_DIVS, "divs"), N(I_DIVU, "divu"), N(I_REMS, "rems"), N(I_REMU, "remu"),
    N(I_AND, "and"), N(I_OR, "or"), N(I_XOR, "xor"),
    N(I_SHL, "shl"), N(I_SHRS, "shrs"), N(I_SHRU, "shru"),
    N(I_ROTL, "rotl"), N(I_ROTR, "rotr"),
    N(I_CLZ, "clz"), N(I_CTZ, "ctz"), N(I_POPCNT, "popcnt"), N(I_BSWAP, "bswap"),
    N(I_EQ, "eq"), N(I_NE, "ne"), N(I_LTS, "lts"), N(I_LTU, "ltu"),
    N(I_GTS, "gts"), N(I_GTU, "gtu"), N(I_LES, "les"), N(I_LEU, "leu"),
    N(I_GES, "ges"), N(I_GEU, "geu"), N(I_EQZ, "eqz"),
    N(I_FADD, "fadd"), N(I_FSUB, "fsub"), N(I_FMUL, "fmul"), N(I_FDIV, "fdiv"),
    N(I_FMIN, "fmin"), N(I_FMAX, "fmax"), N(I_FCOPYSIGN, "fcopysign"),
    N(I_FABS, "fabs"), N(I_FNEG, "fneg"), N(I_FSQRT, "fsqrt"),
    N(I_FCEIL, "fceil"), N(I_FFLOOR, "ffloor"), N(I_FTRUNC, "ftrunc"),
    N(I_FNEAREST, "fnearest"), N(I_FMA, "fma"),
    N(I_FEQ, "feq"), N(I_FNE, "fne"), N(I_FLT, "flt"), N(I_FGT, "fgt"),
    N(I_FLE, "fle"), N(I_FGE, "fge"),
    N(I_WRAP, "wrap"), N(I_EXTENDS, "extends"), N(I_EXTENDU, "extendu"),
    N(I_TRUNCS, "truncs"), N(I_TRUNCU, "truncu"),
    N(I_TRUNC_SATS, "trunc_sats"), N(I_TRUNC_SATU, "trunc_satu"),
    N(I_CONVERTS, "converts"), N(I_CONVERTU, "convertu"),
    N(I_DEMOTE, "demote"), N(I_PROMOTE, "promote"),
    N(I_REINTERPRET, "reinterpret"),
    N(I_EXT8S, "ext8s"), N(I_EXT16S, "ext16s"), N(I_EXT32S, "ext32s"),
    N(I_LOAD, "load"), N(I_STORE, "store"),
    N(I_MEM_SIZE, "mem.size"), N(I_MEM_GROW, "mem.grow"),
    N(I_MEM_COPY, "mem.copy"), N(I_MEM_FILL, "mem.fill"),
    N(I_MEM_INIT, "mem.init"), N(I_DATA_DROP, "data.drop"),
    N(I_GLOBAL_GET, "global.get"), N(I_GLOBAL_SET, "global.set"),
    N(I_SELECT, "select"), N(I_CALL, "call"),
    N(I_CALL_INDIRECT, "call_indirect"), N(I_CALL_REF, "call_ref"),
    N(I_RETURN_CALL, "return_call"),
    N(I_RETURN_CALL_INDIRECT, "return_call_indirect"),
    N(I_RETURN_CALL_REF, "return_call_ref"), N(I_RESULT, "result"),
    N(I_TRAP, "trap"), N(I_TRAP_IF, "trap_if"),
    N(I_TABLE_GET, "table.get"), N(I_TABLE_SET, "table.set"),
    N(I_TABLE_SIZE, "table.size"), N(I_TABLE_GROW, "table.grow"),
    N(I_TABLE_FILL, "table.fill"), N(I_TABLE_COPY, "table.copy"),
    N(I_TABLE_INIT, "table.init"), N(I_ELEM_DROP, "elem.drop"),
    N(I_REF_NULL, "ref.null"), N(I_REF_IS_NULL, "ref.is_null"),
    N(I_REF_FUNC, "ref.func"),
    N(I_MEM_ADDR, "mem_addr"), N(I_BOUNDS_CHECK, "bounds_check"),
    N(I_MEM_PTR, "mem_ptr"), N(I_MEM_BASE, "mem_base"),
    N(I_VLOAD, "vload"), N(I_VSTORE, "vstore"), N(I_VLDN, "vldn"),
    N(I_VADD, "vadd"), N(I_VSUB, "vsub"), N(I_VMUL, "vmul"),
    N(I_VAND, "vand"), N(I_VOR, "vor"), N(I_VXOR, "vxor"),
    N(I_VSMAX, "vsmax"), N(I_VSMIN, "vsmin"),
    N(I_VUMAX, "vumax"), N(I_VUMIN, "vumin"),
    N(I_VUZP1, "vuzp1"), N(I_VUZP2, "vuzp2"),
    N(I_VZIP1, "vzip1"), N(I_VZIP2, "vzip2"),
    N(I_VCMP, "vcmp"), N(I_VSEL, "vsel"),
    N(I_VSHL, "vshl"), N(I_VSHRU, "vshru"), N(I_VSHRS, "vshrs"),
    N(I_VFADD, "vfadd"), N(I_VFSUB, "vfsub"), N(I_VFMUL, "vfmul"),
    N(I_VFDIV, "vfdiv"), N(I_VFMIN, "vfmin"), N(I_VFMAX, "vfmax"),
    N(I_VFNEG, "vfneg"), N(I_VFABS, "vfabs"), N(I_VFSQRT, "vfsqrt"),
    N(I_VSPLAT, "vsplat"), N(I_VREDUCE, "vreduce"),
    N(I_VNOT, "vnot"), N(I_VANDNOT, "vandnot"),
    N(I_VNEG, "vneg"), N(I_VABS, "vabs"), N(I_VPOPCNT, "vpopcnt"),
    N(I_VREV, "vrev"),
    N(I_VADDSATS, "vaddsats"), N(I_VADDSATU, "vaddsatu"),
    N(I_VSUBSATS, "vsubsats"), N(I_VSUBSATU, "vsubsatu"),
    N(I_VAVGRU, "vavgru"), N(I_VQ15MULR, "vq15mulr"), N(I_VDOT, "vdot"),
    N(I_VEXTADD, "vextadd"), N(I_VEXTMUL, "vextmul"),
    N(I_VEXTEND, "vextend"), N(I_VNARROW, "vnarrow"),
    N(I_VEXTMLA, "vextmla"), N(I_VADDHN, "vaddhn"),
    N(I_VSHLV, "vshlv"), N(I_VSHRUV, "vshruv"), N(I_VSHRSV, "vshrsv"),
    N(I_VANYTRUE, "vanytrue"), N(I_VALLTRUE, "valltrue"),
    N(I_VBITMASK, "vbitmask"),
    N(I_VEXTRACTS, "vextracts"), N(I_VEXTRACTU, "vextractu"),
    N(I_VREPLACE, "vreplace"),
    N(I_VSHUFFLE, "vshuffle"), N(I_VSWIZZLE, "vswizzle"),
    N(I_VFPMIN, "vfpmin"), N(I_VFPMAX, "vfpmax"),
    N(I_VFCEIL, "vfceil"), N(I_VFFLOOR, "vffloor"),
    N(I_VFTRUNC, "vftrunc"), N(I_VFNEAREST, "vfnearest"),
    N(I_VFMADD, "vfmadd"),
    N(I_VDOTI7, "vdoti7"), N(I_VDOTI7ADD, "vdoti7add"),
    N(I_VTRUNCSAT, "vtruncsat"), N(I_VCONVERT, "vconvert"),
    N(I_VDEMOTE, "vdemote"), N(I_VPROMOTE, "vpromote"),
    N(I_THROW, "throw"), N(I_THROW_REF, "throw_ref"),
    N(I_EXN_OBJ, "exn.obj"), N(I_EXN_PAYLOAD, "exn.payload"),
    N(I_MEM_LIMIT, "mem.limit"),
    N(I_STRUCT_NEW, "struct.new"), N(I_FIELD_GET, "field.get"),
    N(I_FIELD_SET, "field.set"),
    N(I_ARRAY_NEW, "array.new"), N(I_ARRAY_NEW_SEG, "array.new_seg"),
    N(I_ARRAY_OP, "array.op"),
    N(I_ARRAY_GET, "array.get"), N(I_ARRAY_SET, "array.set"),
    N(I_ARRAY_LEN, "array.len"),
    N(I_GC_LAB_OPEN, "gc.lab_open"), N(I_GC_LAB_FLUSH, "gc.lab_flush"),
    N(I_GC_BADMASK, "gc.badmask"),
    N(I_REF_TEST, "ref.test"), N(I_REF_CONVERT, "ref.convert"), N(I_REF_I31, "ref.i31"), N(I_I31_GET, "i31.get"),
    N(I_ANY_OF_EXTERN, "any.convert_extern"), N(I_EXTERN_OF_ANY, "extern.convert_any"),
    N(I_NOP, "nop"),
  };
#undef N
  return op < I_OP_COUNT && names[op] ? names[op] : "?";
}

void ir_print(IrFunc *f, const char *stage) {
  fprintf(stderr, "; ---- %s: wasm_func_%u (%s) ----\n", stage, f->func_index,
          f->m->funcs[f->func_index].sym);
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    fprintf(stderr, "b%u", bi);
    if (b->nparams) {
      fprintf(stderr, "(");
      for (uint32_t i = 0; i < b->nparams; i++)
        fprintf(stderr, "%sv%u:%s", i ? ", " : "", b->params[i],
                vt_name((ValType)f->insts[b->params[i]].type));
      fprintf(stderr, ")");
    }
    fprintf(stderr, ":  ; depth=%u preds=", b->loop_depth);
    for (uint32_t i = 0; i < b->npreds; i++) fprintf(stderr, "%sb%u", i ? "," : "", b->preds[i]);
    if (b->is_pad) fprintf(stderr, " pad(kind=%u tag=%u)", b->pad_kind, b->pad_tag);
    for (uint32_t i = 0; i < b->nsuccs; i++)
      if (b->succs[i].eh) fprintf(stderr, " unwind->b%u", b->succs[i].target);
    fprintf(stderr, "\n");
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Ref r = b->insts[i];
      Inst *in = &f->insts[r];
      if (in->op == I_NOP) continue;
      fprintf(stderr, "    ");
      if (ir_has_result(in)) fprintf(stderr, "v%u:%s = ", r, vt_name((ValType)in->type));
      fprintf(stderr, "%s", ir_op_name(in->op));
      Ref *a = ir_args(f, r);
      for (uint32_t j = 0; j < in->nargs; j++) fprintf(stderr, " v%u", a[j]);
      if (in->op == I_CONST) fprintf(stderr, " #%llu (0x%llx)",
                                     (unsigned long long)in->imm, (unsigned long long)in->imm);
      else if (in->imm || in->imm2) fprintf(stderr, " [imm=%llu imm2=%llu]",
                                            (unsigned long long)in->imm,
                                            (unsigned long long)in->imm2);
      fprintf(stderr, "\n");
    }
    switch (b->term) {
      case T_JUMP: fprintf(stderr, "    jump"); break;
      case T_BRIF: fprintf(stderr, "    brif v%u ->", b->term_arg); break;
      case T_BRTABLE: fprintf(stderr, "    br_table v%u ->", b->term_arg); break;
      default: break;
    }
    if (b->term == T_JUMP || b->term == T_BRIF || b->term == T_BRTABLE) {
      for (uint32_t s = 0; s < b->nsuccs; s++) {
        const Edge *e = &b->succs[s];
        if (e->eh) continue;
        fprintf(stderr, "%s b%u", s ? "," : "", e->target);
        for (uint32_t j = 0; j < e->nargs; j++)
          fprintf(stderr, "%sv%u%s", j ? " " : "(", e->args[j], j + 1 == e->nargs ? ")" : "");
      }
      fprintf(stderr, "\n");
    }
    switch (b->term) {
      case T_JUMP: case T_BRIF: case T_BRTABLE: break;
      case T_RETURN: fprintf(stderr, "    return");
                     for (uint32_t i = 0; i < b->nret; i++) fprintf(stderr, " v%u", b->ret_args[i]);
                     fprintf(stderr, "\n"); break;
      case T_TRAP: fprintf(stderr, "    trap #%llu\n", (unsigned long long)b->term_imm); break;
      default: fprintf(stderr, "    <no terminator>\n"); break;
    }
  }
}

static uint32_t ir_vec_arity(uint16_t op) {
  switch (op) {
    case I_VLOAD: case I_VLDN: case I_VSPLAT: case I_VREDUCE:
    case I_VSHL: case I_VSHRU: case I_VSHRS:
    case I_VFNEG: case I_VFABS: case I_VFSQRT:
    case I_VNOT: case I_VNEG: case I_VABS: case I_VPOPCNT: case I_VREV:
    case I_VEXTADD: case I_VEXTEND:
    case I_VANYTRUE: case I_VALLTRUE:
    case I_VEXTRACTS: case I_VEXTRACTU:
    case I_VFCEIL: case I_VFFLOOR: case I_VFTRUNC: case I_VFNEAREST:
    case I_VTRUNCSAT: case I_VCONVERT: case I_VDEMOTE: case I_VPROMOTE:
      return 1;
    case I_VSTORE:
    case I_VADD: case I_VSUB: case I_VMUL:
    case I_VAND: case I_VOR: case I_VXOR:
    case I_VSMAX: case I_VSMIN: case I_VUMAX: case I_VUMIN:
    case I_VUZP1: case I_VUZP2: case I_VZIP1: case I_VZIP2:
    case I_VCMP:
    case I_VFADD: case I_VFSUB: case I_VFMUL: case I_VFDIV:
    case I_VFMIN: case I_VFMAX:
    case I_VANDNOT:
    case I_VADDSATS: case I_VADDSATU: case I_VSUBSATS: case I_VSUBSATU:
    case I_VAVGRU: case I_VQ15MULR: case I_VDOT:
    case I_VEXTMUL: case I_VNARROW:
    case I_VSHLV: case I_VSHRUV: case I_VSHRSV:
    case I_VREPLACE: case I_VSWIZZLE:
    case I_VFPMIN: case I_VFPMAX:

    case I_VBITMASK:
    case I_VDOTI7:
      return 2;
    case I_VSEL:
    case I_VFMADD: case I_VDOTI7ADD: case I_VEXTMLA:
      return 3;
    case I_VADDHN:
      return 4;
    default:
      return 0xffffffffu;
  }
}

/* Every use is dominated by its definition: an argument by an earlier
   instruction or a param of its own block, or by a block dominating it; an
   edge's arguments and a terminator's operands likewise at the end of the
   block they leave.  Off unless WARMC_VERIFY_SSA is set -- it is what finds
   the pass that moved a definition away from a use it left behind. */
static void ir_verify_ssa(IrFunc *f, const char *stage) {
  int32_t *pos = xmalloc((f->ninsts ? f->ninsts : 1) * sizeof(int32_t));
  for (uint32_t v = 0; v < f->ninsts; v++) pos[v] = -1;
  for (uint32_t bi = 0; bi < f->nblocks; bi++) {
    Block *b = &f->blocks[bi];
    if (!b->reachable) continue;
    for (uint32_t p = 0; p < b->nparams; p++) pos[b->params[p]] = 0;
    for (uint32_t i = 0; i < b->ninsts; i++) pos[b->insts[i]] = (int32_t)i + 1;
  }
#define SSA_SEE(use_, bi_, at_, what_) do {                                     \
    Ref d_ = ir_resolve(f, (use_));                                           \
    uint32_t db_ = f->insts[d_].block;                                        \
    bool ok_ = pos[d_] >= 0 &&                                                \
               (db_ == (bi_) ? pos[d_] < (at_)                                \
                             : ir_dominates_checked(f, db_, (bi_)));          \
    if (!ok_)                                                                 \
      warmc_fatal("ir_verify(%s): %s in b%u of %s uses v%u, which b%u does "  \
                  "not have by then", stage, (what_), (bi_),                  \
                  f->m->funcs[f->func_index].sym, d_, (bi_));                 \
  } while (0)
  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Inst *in = &f->insts[b->insts[i]];
      Ref *a = ir_args(f, b->insts[i]);
      for (uint32_t j = 0; j < in->nargs; j++)
        SSA_SEE(a[j], bi, (int32_t)i + 1, ir_op_name(in->op));
    }
    int32_t end = (int32_t)b->ninsts + 1;
    if (b->term == T_BRIF || b->term == T_BRTABLE) SSA_SEE(b->term_arg, bi, end, "a branch");
    for (uint32_t i = 0; i < b->nret; i++) SSA_SEE(b->ret_args[i], bi, end, "a return");
    for (uint32_t s = 0; s < b->nsuccs; s++)
      for (uint32_t j = 0; j < b->succs[s].nargs; j++)
        SSA_SEE(b->succs[s].args[j], bi, end, "an edge");
  }
#undef SSA_SEE
  xfree(pos);
}

static bool g_verify_ssa;
static pthread_once_t g_verify_ssa_once = PTHREAD_ONCE_INIT;
static void verify_ssa_init(void) { g_verify_ssa = warmc_getenv("WARMC_VERIFY_SSA") != NULL; }

void ir_verify(IrFunc *f, const char *stage) {
  pthread_once(&g_verify_ssa_once, verify_ssa_init);
  if (g_verify_ssa) ir_verify_ssa(f, stage);

  /* Landing pads are only created, never destroyed, so the count must
     reconcile; and a result must still name a call that lives in a block --
     the inliner's whole-function result rewiring corrupts silently when it
     misses one, which is exactly what this turns into a fatal. */
  uint32_t pads = 0;
  uint8_t *inblock = xcalloc(f->ninsts ? f->ninsts : 1, 1);
  for (uint32_t bi = 0; bi < f->nblocks; bi++) {
    if (f->blocks[bi].is_pad) pads++;
    for (uint32_t i = 0; i < f->blocks[bi].ninsts; i++)
      inblock[f->blocks[bi].insts[i]] = 1;
  }
  if (pads != f->npads)
    warmc_fatal("ir_verify(%s): %s has %u pad blocks but npads says %u",
                stage, f->m->funcs[f->func_index].sym, pads, f->npads);

  for (uint32_t oi = 0; oi < f->norder; oi++) {
    uint32_t bi = f->order[oi];
    Block *b = &f->blocks[bi];
    if (b->term == T_NONE)
      warmc_fatal("ir_verify(%s): block b%u in %s has no terminator",
                  stage, bi, f->m->funcs[f->func_index].sym);

    if (ir_block_has_eh(f, bi)) {
      if (b->ninsts == 0)
        warmc_fatal("ir_verify(%s): b%u in %s reaches a landing pad from an "
                    "empty block", stage, bi, f->m->funcs[f->func_index].sym);
      uint16_t last = f->insts[b->insts[b->ninsts - 1]].op;
      if (last != I_CALL && last != I_CALL_INDIRECT && last != I_CALL_REF &&
          last != I_THROW && last != I_THROW_REF)
        warmc_fatal("ir_verify(%s): b%u in %s reaches a landing pad but ends "
                    "in %s", stage, bi, f->m->funcs[f->func_index].sym,
                    ir_op_name(last));
      bool seen_eh = false;
      for (uint32_t s = 0; s < b->nsuccs; s++) {
        if (b->succs[s].eh) { seen_eh = true; continue; }
        if (seen_eh)
          warmc_fatal("ir_verify(%s): b%u in %s has an ordinary successor "
                      "behind an exceptional one", stage, bi,
                      f->m->funcs[f->func_index].sym);
      }
    }
    for (uint32_t s = 0; s < b->nsuccs; s++) {
      Edge *e = &b->succs[s];
      Block *t = &f->blocks[e->target];
      if (e->eh && !t->is_pad)
        warmc_fatal("ir_verify(%s): edge b%u->b%u is exceptional but b%u is "
                    "not a landing pad", stage, bi, e->target, e->target);
      if (e->nargs != t->nparams)
        warmc_fatal("ir_verify(%s): edge b%u->b%u passes %u args but b%u takes %u params",
                    stage, bi, e->target, e->nargs, e->target, t->nparams);
      for (uint32_t j = 0; j < e->nargs; j++)
        if (e->args[j] >= f->ninsts)
          warmc_fatal("ir_verify(%s): edge b%u->b%u arg %u is out of range", stage, bi, e->target, j);
    }
    for (uint32_t i = 0; i < b->ninsts; i++) {
      Inst *in = &f->insts[b->insts[i]];
      Ref *a = ir_args(f, b->insts[i]);
      for (uint32_t j = 0; j < in->nargs; j++)
        if (a[j] >= f->ninsts)
          warmc_fatal("ir_verify(%s): v%u arg %u out of range", stage, b->insts[i], j);

      if (in->op == I_VSHUFFLE && in->nargs != 3 && in->nargs != 4)
        warmc_fatal("ir_verify(%s): v%u is vshuffle with %u operands, not 3 or 4",
                    stage, b->insts[i], in->nargs);
      uint32_t want = ir_vec_arity(in->op);
      if (want != 0xffffffffu && in->nargs != want)
        warmc_fatal("ir_verify(%s): v%u is %s with %u operands, not %u",
                    stage, b->insts[i], ir_op_name(in->op), in->nargs, want);

      if ((in->op == I_RETURN_CALL || in->op == I_RETURN_CALL_INDIRECT ||
           in->op == I_RETURN_CALL_REF ||
           in->op == I_THROW || in->op == I_THROW_REF) &&
          i != b->ninsts - 1)
        warmc_fatal("ir_verify(%s): b%u in %s has %s before the end of the block",
                    stage, bi, f->m->funcs[f->func_index].sym, ir_op_name(in->op));

      if (in->op == I_RESULT) {
        uint16_t p = in->nargs ? f->insts[a[0]].op : I_NOP;
        if ((p != I_CALL && p != I_CALL_INDIRECT && p != I_CALL_REF) ||
            !inblock[a[0]])
          warmc_fatal("ir_verify(%s): v%u in %s is a result of v%u, which is "
                      "not a call in a block",
                      stage, b->insts[i], f->m->funcs[f->func_index].sym,
                      in->nargs ? a[0] : 0);
      }
    }
  }
  xfree(inblock);
}
