/* Go's stack pointer as an argument.

   Go's wasm port keeps its goroutine stacks in linear memory and the stack
   pointer in global 0, and gives every function it compiles the one type
   (i32) -> i32: the parameter says where to resume, the result whether to
   unwind.  Each call pushes its return address below the pointer, each
   function moves it down by its frame and up again, each return pops the
   address, so every call and return passes the pointer through the global
   -- a store and then a load of it on the path of every call, which is
   what bounds a program of many small calls.

   This rewrite passes it along instead: each function of that type takes
   the pointer as a second parameter and returns it beside its result,
   packed into an i64 (the result above, the pointer below), and reads and
   writes it as a local.  Only the pointer's value moves.  Arguments,
   results and frames stay in memory where Go wrote them, for its collector
   to scan and its stack growth to copy, and a goroutine's switch to
   another stack happens as it did: the function that switches sets the
   pointer and returns 1, every function returns 1 and its pointer up to
   wasm_pc_f_loop, and the loop resumes each through the table with what it
   finds in the global.  That takes 11% off the stdgo corpus's cycles, a
   third off errwrap's, recover's and sprintf's.

   The global stays true wherever something else can read it.  The
   functions of other types that touch it -- the write barrier, the resume
   loop, _start -- and those that reach one of them, or a function of Go's
   type, through their calls -- gcWriteBarrier1 to 8 -- keep it: a call
   into one of them from a rewritten function stores the pointer into the
   global first and loads it again after, and their calls into rewritten
   functions pass the global and store what comes back to it.  An import
   counts as one when the host could call back into one through an export.

   It changes the signature of every function of Go's type, so it is made
   only where nothing else can tell:
   - the module is Go's (a go:buildid section).  A trap leaves the global
     where the last call out of a rewritten function put it, which no Go
     program reads again;
   - global 0 is a mutable i32, neither imported nor exported;
   - Go's type is final, and each function of it is defined in the module,
     neither exported nor the start function nor taken by ref.func outside
     an active or passive segment; no import of it is in a segment;
   - no table is imported or exported, and nothing reads a reference out of
     one, calls through a reference, makes a tail call or handles an
     exception.
   The bodies written are validated as the module's were. */

#include "warmc.h"
#include "buf.h"
#include "ir.h"
#include "opcodes.h"
#include "reader.h"

#include <string.h>
#include <stdatomic.h>

#define GS_SP 0

static void gs_uleb(Buf *b, uint64_t v) {
  do {
    uint8_t c = v & 0x7f;
    v >>= 7;
    if (v) c |= 0x80;
    buf_u8(b, c);
  } while (v);
}

typedef struct {
  Module *m;
  uint64_t tid;                /* Go's type's identity */
  uint32_t nty;                /* the module's types before the new one */
  uint32_t nt;                 /* the new type: (i32, i32) -> i64 */
  uint32_t *canon;             /* per type index, the first of its identity */
  uint8_t *in_s;               /* functions of Go's type */
  uint8_t *in_r;               /* others that reach the global */
  uint8_t *rtype;              /* per canonical type index: one of in_r has it */
} Gs;

static bool gs_is_go_type(const Gs *g, uint32_t ti) {
  const Module *m = g->m;
  return ti < m->ntypes && m->types[ti].kind == CT_FUNC && m->types[ti].id == g->tid;
}

/* the i64 a rewritten function returned, on the stack: its pointer into
   the local or the global, its result left */
static void gs_unpack(Buf *b, uint8_t set_op, uint32_t set_idx, uint32_t t64) {
  buf_u8(b, 0x22); gs_uleb(b, t64);
  buf_u8(b, 0xa7);
  buf_u8(b, set_op); gs_uleb(b, set_idx);
  buf_u8(b, 0x20); gs_uleb(b, t64);
  buf_u8(b, 0x42); buf_u8(b, 0x20);
  buf_u8(b, 0x88);
  buf_u8(b, 0xa7);
}

/* the result on the stack with the pointer in local 1, as the i64 a
   rewritten function returns: i64.extend_i32_u, << 32, | the pointer */
static void gs_pack(Buf *b) {
  buf_u8(b, 0xad);
  buf_u8(b, 0x42); buf_u8(b, 0x20);
  buf_u8(b, 0x86);
  buf_u8(b, 0x20); gs_uleb(b, 1);
  buf_u8(b, 0xad);
  buf_u8(b, 0x84);
}

/* A function's calls into rewritten ones: Go's pointer comes from get_op
   (the local of a rewritten function, the global of another) and goes back
   to set_op's.  A rewritten function also stores it into the global around
   a call that can read it there.  Returns whether the body decoded; the
   body written is the module's to free, and it is *body. */
static bool gs_rewrite(const Gs *g, uint32_t fi, bool s, uint8_t **body) {
  Module *m = g->m;
  Func *f = &m->funcs[fi];
  uint32_t old = f->ntotal_locals;
  uint32_t tmp = s ? old + 1 : old, t64 = tmp + 1;
  uint8_t get_op = s ? 0x20 : 0x23, set_op = s ? 0x21 : 0x24;
  uint32_t sp = s ? 1 : GS_SP;
  Buf b;
  memset(&b, 0, sizeof b);
  /* a Go function's calls grow it by a quarter or so */
  buf_reserve(&b, (size_t)(f->body_end - f->body) * 5 / 4 + 32);
  /* a return packs its result with the pointer in place, so that each
     return's constant stays its own; a branch to the function's label or
     its end goes to a block around the body, packed at that block's end */
  if (s) { buf_u8(&b, 0x02); buf_u8(&b, 0x7f); }
  Rd r = rd_make(f->body, f->body_end, f->body);
  uint32_t depth = 0;
  while (!rd_eof(&r)) {
    const uint8_t *at = r.p;
    uint32_t op = wasm_read_op(&r);
    if (op == 0x02 || op == 0x03 || op == 0x04) depth++;
    else if (op == 0x0b && depth) depth--;
    switch (op) {
      case 0x20: case 0x21: case 0x22: {
        uint32_t i = rd_u32(&r);
        buf_u8(&b, (uint8_t)op); gs_uleb(&b, s && i ? i + 1 : i);
        continue;
      }
      case 0x23: case 0x24: {
        uint32_t gi = rd_u32(&r);
        if (s && gi == GS_SP) { buf_u8(&b, op == 0x23 ? 0x20 : 0x21); gs_uleb(&b, 1); continue; }
        break;
      }
      case 0x0f:
        if (s) gs_pack(&b);
        break;
      case 0x10: {
        uint32_t c = rd_u32(&r);
        if (c < m->nfuncs && g->in_s[c]) {
          buf_u8(&b, get_op); gs_uleb(&b, sp);
          buf_bytes(&b, at, (size_t)(r.p - at));
          gs_unpack(&b, set_op, sp, t64);
          continue;
        }
        if (s && c < m->nfuncs && g->in_r[c]) {
          buf_u8(&b, 0x20); gs_uleb(&b, 1);
          buf_u8(&b, 0x24); gs_uleb(&b, GS_SP);
          buf_bytes(&b, at, (size_t)(r.p - at));
          buf_u8(&b, 0x23); gs_uleb(&b, GS_SP);
          buf_u8(&b, 0x21); gs_uleb(&b, 1);
          continue;
        }
        buf_bytes(&b, at, (size_t)(r.p - at));
        continue;
      }
      case 0x11: {
        uint32_t ti = rd_u32(&r), tab = rd_u32(&r);
        if (gs_is_go_type(g, ti)) {
          buf_u8(&b, 0x21); gs_uleb(&b, tmp);
          buf_u8(&b, get_op); gs_uleb(&b, sp);
          buf_u8(&b, 0x20); gs_uleb(&b, tmp);
          buf_u8(&b, 0x11); gs_uleb(&b, g->nt); gs_uleb(&b, tab);
          gs_unpack(&b, set_op, sp, t64);
          continue;
        }
        if (s && ti < g->nty && g->rtype[g->canon[ti]]) {
          buf_u8(&b, 0x20); gs_uleb(&b, 1);
          buf_u8(&b, 0x24); gs_uleb(&b, GS_SP);
          buf_bytes(&b, at, (size_t)(r.p - at));
          buf_u8(&b, 0x23); gs_uleb(&b, GS_SP);
          buf_u8(&b, 0x21); gs_uleb(&b, 1);
          continue;
        }
        buf_bytes(&b, at, (size_t)(r.p - at));
        continue;
      }
      default:
        if (!wasm_skip_imm(&r, op)) { buf_free(&b); return false; }
        break;
    }
    buf_bytes(&b, at, (size_t)(r.p - at));
  }
  if (s) {
    gs_pack(&b);
    buf_u8(&b, 0x0b);
  }
  /* the module keeps every body to the end */
  if (b.cap > b.len + b.len / 8) b.data = xrealloc(b.data, b.len);

  uint32_t add = s ? 3 : 2;
  ValType *lt = xmalloc((old + add) * sizeof(ValType));
  if (s) {
    lt[0] = f->local_types[0];
    lt[1] = VT_I32;
    memcpy(lt + 2, f->local_types + 1, (old - 1) * sizeof(ValType));
  } else {
    memcpy(lt, f->local_types, old * sizeof(ValType));
  }
  lt[tmp] = VT_I32;
  lt[t64] = VT_I64;
  xfree(f->local_types);
  f->local_types = lt;
  f->ntotal_locals = old + add;
  f->nlocals += 2;
  if (s) f->type_index = g->nt;
  f->body = *body = b.data;
  f->body_end = b.data + b.len;
  return true;
}

typedef struct {
  const Gs *g;
  const uint32_t *todo;
  uint8_t **bodies;
  _Atomic uint32_t failed;
} GsRewrite;

static void gs_rewrite_one(void *ctx, uint32_t k, uint32_t w) {
  (void)w;
  GsRewrite *rw = ctx;
  uint32_t fi = rw->todo[k];
  if (!gs_rewrite(rw->g, fi, rw->g->in_s[fi], &rw->bodies[k])) atomic_store(&rw->failed, fi);
}

/* What each body does that the rewrite must know, found on the compiler's
   threads, each body writing only its own: whether it touches the global,
   calls a function of Go's type or takes one by ref.func, or does what the
   rewrite cannot follow; and what it calls -- a function's index, or nf
   and the canonical type of a call_indirect. */
typedef struct {
  const Gs *g;
  uint8_t *touch, *calls_s, *refs_s, *bad;
  uint32_t **out, *nout;
} GsScan;

static void gs_out(GsScan *sc, uint32_t fi, uint32_t *cap, uint32_t x) {
  if (sc->nout[fi] == *cap) {
    *cap = *cap ? *cap * 2 : 16;
    sc->out[fi] = xrealloc(sc->out[fi], *cap * sizeof(uint32_t));
  }
  sc->out[fi][sc->nout[fi]++] = x;
}

static void gs_scan_one(void *ctx, uint32_t i, uint32_t w) {
  (void)w;
  GsScan *sc = ctx;
  const Gs *g = sc->g;
  const Module *m = g->m;
  uint32_t fi = m->nimported_funcs + i, cap = 0;
  const Func *f = &m->funcs[fi];
  if (!f->body) return;
  Rd r = rd_make(f->body, f->body_end, f->body);
  while (!rd_eof(&r)) {
    uint32_t op = wasm_read_op(&r);
    switch (op) {
      case 0x23: case 0x24:
        if (rd_u32(&r) == GS_SP) sc->touch[fi] = 1;
        continue;
      case 0x10: {
        uint32_t c = rd_u32(&r);
        if (c < m->nfuncs) {
          gs_out(sc, fi, &cap, c);
          if (g->in_s[c]) sc->calls_s[fi] = 1;
        }
        continue;
      }
      case 0x11: {
        uint32_t ti = rd_u32(&r);
        rd_u32(&r);
        if (ti < g->nty) {
          gs_out(sc, fi, &cap, m->nfuncs + g->canon[ti]);
          if (gs_is_go_type(g, ti)) sc->calls_s[fi] = 1;
        }
        continue;
      }
      case 0xd2: {
        uint32_t c = rd_u32(&r);
        if (c < m->nfuncs && g->in_s[c]) sc->refs_s[fi] = 1;
        continue;
      }
      /* tail calls, calls through a reference, table.get, exceptions */
      case 0x12: case 0x13: case 0x14: case 0x15: case 0x25:
      case 0x06: case 0x07: case 0x08: case 0x09: case 0x0a: case 0x18: case 0x19: case 0x1f:
        sc->bad[fi] = 1;
        return;
      default: break;
    }
    if (!wasm_skip_imm(&r, op)) { sc->bad[fi] = 1; return; }
  }
}

/* whether a constant expression names a function of Go's type */
static bool gs_init_refs(const Gs *g, const InitExpr *e, bool imports_only) {
  const Module *m = g->m;
  for (uint32_t k = 0; k < e->n; k++) {
    if (e->ins[k].op != IOP_REF_FUNC || e->ins[k].lo >= m->nfuncs) continue;
    uint32_t c = (uint32_t)e->ins[k].lo;
    if (imports_only ? m->funcs[c].imported && gs_is_go_type(g, m->funcs[c].type_index) : g->in_s[c])
      return true;
  }
  return false;
}

uint32_t module_go_sp_args(Module *m) {
  if (!m->go_stack || !m->nglobals) return 0;
  const Global *sp = &m->globals[GS_SP];
  if (sp->imported || !sp->mutable_ || sp->type != VT_I32) return 0;
  for (uint32_t e = 0; e < m->nexports; e++)
    if ((m->exports[e].kind == EK_GLOBAL && m->exports[e].index == GS_SP) ||
        m->exports[e].kind == EK_TABLE)
      return 0;
  for (uint32_t t = 0; t < m->ntables; t++)
    if (m->tables[t].imported) return 0;

  /* Go's type, (i32) -> i32; and no function type below another, so that
     a call through a type reaches the functions of its identity alone */
  Gs g;
  memset(&g, 0, sizeof g);
  g.m = m;
  bool have = false;
  for (uint32_t ti = 0; ti < m->ntypes; ti++) {
    const CompType *c = &m->types[ti];
    if (c->kind != CT_FUNC) continue;
    if (c->has_super) return 0;
    if (!have && c->nparams == 1 && c->params[0] == VT_I32 && c->nresults == 1 &&
        c->results[0] == VT_I32 && c->final_ && c->rec_n == 1) {
      g.tid = c->id;
      have = true;
    }
  }
  if (!have) return 0;

  uint32_t nf = m->nfuncs, nty = m->ntypes;
  g.nty = nty;
  g.canon = xmalloc((nty ? nty : 1) * sizeof(uint32_t));
  for (uint32_t ti = 0; ti < nty; ti++) {
    g.canon[ti] = ti;
    for (uint32_t u = 0; u < ti; u++)
      if (m->types[u].id == m->types[ti].id) { g.canon[ti] = u; break; }
  }
  g.in_s = xcalloc(nf, 1);
  g.in_r = xcalloc(nf, 1);
  g.rtype = xcalloc(nty ? nty : 1, 1);
  GsScan sc = { &g, xcalloc(nf, 1), xcalloc(nf, 1), xcalloc(nf, 1), xcalloc(nf, 1),
                xcalloc(nf, sizeof(uint32_t *)), xcalloc(nf, sizeof(uint32_t)) };
  uint32_t ns = 0, applied = 0, nb = nf - m->nimported_funcs;
  bool ok = true;

  for (uint32_t fi = 0; fi < nf; fi++) {
    const Func *f = &m->funcs[fi];
    if (f->imported || !gs_is_go_type(&g, f->type_index)) continue;
    if (!f->body || (m->has_start && m->start_func == fi)) ok = false;
    g.in_s[fi] = 1;
    ns++;
  }
  for (uint32_t e = 0; e < m->nexports && ok; e++)
    if (m->exports[e].kind == EK_FUNC && m->exports[e].index < nf && g.in_s[m->exports[e].index])
      ok = false;
  for (uint32_t s = 0; s < m->nelems && ok; s++) {
    const ElemSeg *e = &m->elems[s];
    for (uint32_t j = 0; j < e->n && ok; j++)
      if (gs_init_refs(&g, &e->items[j], true) ||
          (e->mode == SEG_DECLARATIVE && gs_init_refs(&g, &e->items[j], false)))
        ok = false;
  }
  for (uint32_t gi = 0; gi < m->nglobals && ok; gi++)
    if (gs_init_refs(&g, &m->globals[gi].init, false)) ok = false;
  if (!ok || !ns) goto out;

  warmc_par_for(nb, compile_want_threads(m, nb), gs_scan_one, &sc);
  for (uint32_t fi = 0; fi < nf; fi++)
    if (sc.bad[fi] || sc.refs_s[fi]) goto out;

  /* reach: what can read or write the global, itself or through a call */
  uint8_t *reach = xcalloc(nf, 1);
  bool imports = false;
  for (uint32_t fi = 0; fi < nf; fi++)
    if (g.in_s[fi] || sc.touch[fi]) {
      reach[fi] = 1;
      g.rtype[g.canon[m->funcs[fi].type_index]] = 1;
    }
  for (bool ch = true; ch;) {
    ch = false;
    for (uint32_t fi = m->nimported_funcs; fi < nf; fi++) {
      if (reach[fi]) continue;
      for (uint32_t k = 0; k < sc.nout[fi]; k++) {
        uint32_t x = sc.out[fi][k];
        if (x < nf ? !reach[x] : !g.rtype[x - nf]) continue;
        reach[fi] = 1;
        g.rtype[g.canon[m->funcs[fi].type_index]] = 1;
        ch = true;
        break;
      }
    }
    if (!imports)
      for (uint32_t e = 0; e < m->nexports; e++)
        if (m->exports[e].kind == EK_FUNC && m->exports[e].index < nf && reach[m->exports[e].index]) {
          imports = true;
          for (uint32_t fi = 0; fi < m->nimported_funcs; fi++) {
            reach[fi] = 1;
            g.rtype[g.canon[m->funcs[fi].type_index]] = 1;
          }
          ch = true;
          break;
        }
  }
  for (uint32_t fi = 0; fi < nf; fi++) g.in_r[fi] = reach[fi] && !g.in_s[fi];
  xfree(reach);

  /* Final, so that a call through it checks the type by its identity
     alone -- unless the module wrote a final type of that structure, which
     would then be the same type and a call through it reach these
     functions: not final, it stays apart, at the cost of a subtype check
     on each call through the table. */
  ValType params[2] = { VT_I32, VT_I32 }, res = VT_I64;
  bool clash = false;
  for (uint32_t ti = 0; ti < nty && !clash; ti++) {
    const CompType *c = &m->types[ti];
    clash = c->kind == CT_FUNC && c->final_ && !c->has_super && c->rec_n == 1 &&
            c->nparams == 2 && c->params[0] == VT_I32 && c->params[1] == VT_I32 &&
            c->nresults == 1 && c->results[0] == VT_I64;
  }
  g.nt = module_add_func_type(m, params, 2, &res, 1, !clash);

  /* the Go functions and those of the others that call one, rewritten on
     the compiler's threads, each its own body */
  uint32_t *todo = xmalloc(nf * sizeof(uint32_t));
  uint32_t nd = 0;
  for (uint32_t fi = m->nimported_funcs; fi < nf; fi++)
    if (g.in_s[fi] || (g.in_r[fi] && sc.calls_s[fi])) todo[nd++] = fi;
  GsRewrite rw = { &g, todo, xmalloc((nd ? nd : 1) * sizeof(uint8_t *)), UINT32_MAX };
  warmc_par_for(nd, compile_want_threads(m, nd), gs_rewrite_one, &rw);
  if (rw.failed != UINT32_MAX)
    warmc_fatal("internal: the Go stack pointer rewrite could not decode function %u", rw.failed);
  m->owned_bodies = xrealloc(m->owned_bodies, (m->nowned_bodies + nd) * sizeof(uint8_t *));
  for (uint32_t k = 0; k < nd; k++) {
    m->owned_bodies[m->nowned_bodies++] = rw.bodies[k];
    m->total_locals += g.in_s[todo[k]] ? 3 : 2;
  }
  xfree(rw.bodies);
  wasm_validate_funcs(m, todo, nd);
  xfree(todo);
  applied = ns;

out:
  for (uint32_t fi = 0; fi < nf; fi++) xfree(sc.out[fi]);
  xfree(sc.out); xfree(sc.nout);
  xfree(g.canon); xfree(g.in_s); xfree(g.in_r); xfree(g.rtype);
  xfree(sc.touch); xfree(sc.calls_s); xfree(sc.refs_s); xfree(sc.bad);
  return applied;
}
