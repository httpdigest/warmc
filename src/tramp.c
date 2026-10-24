/* A trampoline as tail calls.

   GHC's wasm back end compiles each STG function to a wasm function of no
   parameters that returns the table index of the function to run next, and
   the runtime's StgRun calls through the table in a loop until one returns
   0.  Its virtual registers -- Sp, Hp, R1 and the rest -- are wasm globals,
   so every transfer stores them at the end of one function and loads them
   again at the start of the next, besides returning to the loop and calling
   through the table.  GHC's native code generator jumps to the next
   function with the registers in machine registers.

   This rewrite does the same in wasm: each function the loop calls
   tail-calls the next itself -- with return_call when it returns a constant
   index, through the table when the index is computed -- and the three
   registers its functions read and write most are passed along as
   arguments, written back to their globals only when a function returns 0
   and the loop ends.  Over the hsbench corpus that is 29% of the cycles.

   It changes the signature of every function the loop reaches, so it is
   made only where nothing else can tell:
   - one call_indirect in the module has the loop's type, () -> i32, through
     table 0, and nothing calls through a reference of that type;
   - table 0 is frozen (module_table0_facts) and nothing reads a reference
     out of it, and each of its entries of that type is defined in the
     module, neither exported nor called directly nor taken by ref.func nor
     named by a segment other than the table's, and makes no tail call and
     handles no exception;
   - a register is a mutable i32 global, neither imported nor exported, that
     only those functions touch: the loop sees the values a function wrote
     when it returns 0, and nothing else ever reads them.
   The bodies written are validated as the module's were. */

#include "warmc.h"
#include "buf.h"
#include "ir.h"
#include "opcodes.h"
#include "reader.h"

#include <string.h>

#define TR_NREG 3

static void tr_uleb(Buf *b, uint64_t v) {
  do {
    uint8_t c = v & 0x7f;
    v >>= 7;
    if (v) c |= 0x80;
    buf_u8(b, c);
  } while (v);
}

static void tr_sleb(Buf *b, int64_t v) {
  for (;;) {
    uint8_t c = v & 0x7f;
    v >>= 7;
    if ((v == 0 && !(c & 0x40)) || (v == -1 && (c & 0x40))) { buf_u8(b, c); return; }
    buf_u8(b, c | 0x80);
  }
}

typedef struct {
  Module *m;
  uint64_t tid;                /* the loop type's identity */
  uint32_t nt;                 /* the new type: the registers, then -> i32 */
  const Table0Facts *t0;
  uint8_t *in_s;
  uint32_t reg[TR_NREG], nreg;
} Tr;

static int tr_reg(const Tr *t, uint32_t g) {
  for (uint32_t k = 0; k < t->nreg; k++)
    if (t->reg[k] == g) return (int)k;
  return -1;
}

static bool tr_is_loop_type(const Module *m, uint32_t ti, uint64_t tid) {
  return ti < m->ntypes && m->types[ti].kind == CT_FUNC && m->types[ti].id == tid;
}

/* the registers back to their globals, then 0 to the loop */
static void tr_exit(const Tr *t, Buf *b) {
  for (uint32_t k = 0; k < t->nreg; k++) {
    buf_u8(b, 0x20); tr_uleb(b, k);
    buf_u8(b, 0x24); tr_uleb(b, t->reg[k]);
  }
  buf_u8(b, 0x41); buf_u8(b, 0x00);
  buf_u8(b, 0x0f);
}

static void tr_args(const Tr *t, Buf *b) {
  for (uint32_t k = 0; k < t->nreg; k++) { buf_u8(b, 0x20); tr_uleb(b, k); }
}

/* the transfer to the function at a constant table index */
static void tr_go_const(const Tr *t, Buf *b, int32_t k) {
  if (k == 0) { tr_exit(t, b); return; }
  uint32_t fi = (uint32_t)k < t->t0->n ? t->t0->entry[k] : TABLE0_NULL;
  tr_args(t, b);
  if (fi != TABLE0_NULL && t->in_s[fi]) {
    buf_u8(b, 0x12); tr_uleb(b, fi);
  } else {
    /* what the loop's call would have trapped on, the table's call traps on */
    buf_u8(b, 0x41); tr_sleb(b, k);
    buf_u8(b, 0x13); tr_uleb(b, t->nt); tr_uleb(b, 0);
  }
}

/* the transfer to the index on the stack, through tmp */
static void tr_go(const Tr *t, Buf *b, uint32_t tmp) {
  buf_u8(b, 0x22); tr_uleb(b, tmp);
  buf_u8(b, 0x45);
  buf_u8(b, 0x04); buf_u8(b, 0x40);
  tr_exit(t, b);
  buf_u8(b, 0x0b);
  tr_args(t, b);
  buf_u8(b, 0x20); tr_uleb(b, tmp);
  buf_u8(b, 0x13); tr_uleb(b, t->nt); tr_uleb(b, 0);
}

static uint8_t *tr_own(Module *m, Buf *b) {
  m->owned_bodies = xrealloc(m->owned_bodies, (m->nowned_bodies + 1) * sizeof(uint8_t *));
  m->owned_bodies[m->nowned_bodies++] = b->data;
  return b->data;
}

/* A function the loop calls: the registers become its first parameters,
   its body a block whose value is the index it would have returned, and
   the transfer follows.  A `return` of a constant transfers in place, by
   its direct tail call; any other branches to the block's end with its
   index, so a body of many returns -- GHC's case analyses return from each
   arm -- has the one transfer through the table. */
static bool tr_rewrite_s(Tr *t, uint32_t fi) {
  Module *m = t->m;
  Func *f = &m->funcs[fi];
  uint32_t nr = t->nreg, old = f->ntotal_locals, tmp = nr + old;
  Buf b;
  memset(&b, 0, sizeof b);
  buf_u8(&b, 0x02); buf_u8(&b, 0x7f);
  Rd r = rd_make(f->body, f->body_end, f->body);
  size_t const_at = (size_t)-1;
  int32_t const_k = 0;
  uint32_t depth = 0;
  while (!rd_eof(&r)) {
    const uint8_t *at = r.p;
    uint32_t op = wasm_read_op(&r);
    size_t here = b.len;
    if (op == 0x02 || op == 0x03 || op == 0x04 || op == 0x1f) depth++;
    else if (op == 0x0b && depth) depth--;
    switch (op) {
      case 0x20: case 0x21: case 0x22: {
        uint32_t i = rd_u32(&r);
        buf_u8(&b, (uint8_t)op); tr_uleb(&b, i + nr);
        break;
      }
      case 0x23: case 0x24: {
        uint32_t g = rd_u32(&r);
        int k = tr_reg(t, g);
        if (k >= 0) { buf_u8(&b, op == 0x23 ? 0x20 : 0x21); tr_uleb(&b, (uint32_t)k); }
        else buf_bytes(&b, at, (size_t)(r.p - at));
        break;
      }
      case 0x41: {
        int32_t k = rd_i32(&r);
        buf_bytes(&b, at, (size_t)(r.p - at));
        const_at = here;
        const_k = k;
        continue;
      }
      case 0x0f:
        if (const_at != (size_t)-1) {
          b.len = const_at;
          tr_go_const(t, &b, const_k);
        } else {
          buf_u8(&b, 0x0c); tr_uleb(&b, depth);
        }
        break;
      default:
        if (!wasm_skip_imm(&r, op)) { buf_free(&b); return false; }
        buf_bytes(&b, at, (size_t)(r.p - at));
        break;
    }
    const_at = (size_t)-1;
  }
  tr_go(t, &b, tmp);
  buf_u8(&b, 0x0b);

  ValType *lt = xmalloc((tmp + 1) * sizeof(ValType));
  for (uint32_t k = 0; k < nr; k++) lt[k] = VT_I32;
  memcpy(lt + nr, f->local_types, old * sizeof(ValType));
  lt[tmp] = VT_I32;
  xfree(f->local_types);
  f->local_types = lt;
  f->ntotal_locals = tmp + 1;
  f->nlocals = old + 1;
  m->total_locals += nr + 1;
  f->type_index = t->nt;
  f->body = tr_own(m, &b);
  f->body_end = b.data + b.len;
  return true;
}

/* The loop: its call passes the registers' globals. */
static bool tr_rewrite_loop(Tr *t, uint32_t fi) {
  Module *m = t->m;
  Func *f = &m->funcs[fi];
  uint32_t tmp = f->ntotal_locals;
  Buf b;
  memset(&b, 0, sizeof b);
  Rd r = rd_make(f->body, f->body_end, f->body);
  bool done = false;
  while (!rd_eof(&r)) {
    const uint8_t *at = r.p;
    uint32_t op = wasm_read_op(&r);
    if (op == 0x11) {
      uint32_t ti = rd_u32(&r), tab = rd_u32(&r);
      if (tab == 0 && tr_is_loop_type(m, ti, t->tid)) {
        buf_u8(&b, 0x21); tr_uleb(&b, tmp);
        for (uint32_t k = 0; k < t->nreg; k++) { buf_u8(&b, 0x23); tr_uleb(&b, t->reg[k]); }
        buf_u8(&b, 0x20); tr_uleb(&b, tmp);
        buf_u8(&b, 0x11); tr_uleb(&b, t->nt); tr_uleb(&b, 0);
        done = true;
        continue;
      }
      buf_bytes(&b, at, (size_t)(r.p - at));
      continue;
    }
    if (!wasm_skip_imm(&r, op)) { buf_free(&b); return false; }
    buf_bytes(&b, at, (size_t)(r.p - at));
  }
  if (!done) { buf_free(&b); return false; }
  f->local_types = xrealloc(f->local_types, (tmp + 1) * sizeof(ValType));
  f->local_types[tmp] = VT_I32;
  f->ntotal_locals = tmp + 1;
  f->nlocals++;
  m->total_locals++;
  f->body = tr_own(m, &b);
  f->body_end = b.data + b.len;
  return true;
}

/* What the module's bodies do that the rewrite must know: who is called
   directly or taken by ref.func, where the loop's type is called through,
   which globals each function touches. */
typedef struct {
  uint8_t *direct, *refd;
  uint32_t loop_fn, nloop;
  bool bad;
} TrScan;

static void tr_scan(const Module *m, uint64_t tid, TrScan *sc) {
  for (uint32_t fi = m->nimported_funcs; fi < m->nfuncs && !sc->bad; fi++) {
    const Func *f = &m->funcs[fi];
    if (!f->body) continue;
    Rd r = rd_make(f->body, f->body_end, f->body);
    while (!rd_eof(&r) && !sc->bad) {
      uint32_t op = wasm_read_op(&r);
      switch (op) {
        case 0x10: case 0x12: {
          uint32_t c = rd_u32(&r);
          if (c < m->nfuncs) sc->direct[c] = 1;
          continue;
        }
        case 0xd2: {
          uint32_t c = rd_u32(&r);
          if (c < m->nfuncs) sc->refd[c] = 1;
          continue;
        }
        case 0x25:
          /* table.get hands out a reference to what the table holds */
          sc->bad = true;
          continue;
        case 0x11: case 0x13: {
          uint32_t ti = rd_u32(&r), tab = rd_u32(&r);
          if (tr_is_loop_type(m, ti, tid)) {
            if (op != 0x11 || tab != 0) sc->bad = true;
            sc->loop_fn = fi;
            sc->nloop++;
          }
          continue;
        }
        case 0x14: case 0x15: {
          uint32_t ti = rd_u32(&r);
          if (tr_is_loop_type(m, ti, tid)) sc->bad = true;
          continue;
        }
        default: break;
      }
      if (!wasm_skip_imm(&r, op)) sc->bad = true;
    }
  }
}

/* Of a body: whether it tail-calls or handles exceptions, and the counts of
   its reads and writes of each global below 64 (into cnt, or marks in out). */
static bool tr_body_globals(const Func *f, uint32_t *cnt, uint8_t *out, uint32_t nglobals) {
  Rd r = rd_make(f->body, f->body_end, f->body);
  while (!rd_eof(&r)) {
    uint32_t op = wasm_read_op(&r);
    switch (op) {
      case 0x23: case 0x24: {
        uint32_t g = rd_u32(&r);
        if (g < nglobals) { if (cnt) cnt[g]++; if (out) out[g] = 1; }
        continue;
      }
      case 0x12: case 0x13: case 0x15:
      case 0x06: case 0x07: case 0x08: case 0x09: case 0x0a: case 0x18: case 0x19: case 0x1f:
        if (cnt) return false;
        break;
      default: break;
    }
    if (!wasm_skip_imm(&r, op)) return false;
  }
  return true;
}

uint32_t module_untrampoline(Module *m) {
  /* the loop's type */
  uint64_t tid = 0;
  bool have = false;
  for (uint32_t ti = 0; ti < m->ntypes && !have; ti++) {
    const CompType *c = &m->types[ti];
    if (c->kind == CT_FUNC && c->nparams == 0 && c->nresults == 1 &&
        c->results[0] == VT_I32 && !c->has_super && c->final_ && c->rec_n == 1) {
      tid = c->id;
      have = true;
    }
  }
  if (!have || m->ntables != 1 || !m->nglobals) return 0;
  const Table0Facts *t0 = module_table0_facts(m);
  if (!t0->frozen || !t0->entry) {
    xfree(m->t0->entry); xfree(m->t0->sites); xfree(m->t0);
    m->t0 = NULL;
    return 0;
  }

  uint32_t nf = m->nfuncs, ng = m->nglobals;
  TrScan sc = { xcalloc(nf, 1), xcalloc(nf, 1), UINT32_MAX, 0, false };
  tr_scan(m, tid, &sc);
  uint8_t *in_s = xcalloc(nf, 1);
  uint32_t *cnt = xcalloc(ng, sizeof(uint32_t));
  uint8_t *outside = xcalloc(ng, 1);
  uint32_t ns = 0, applied = 0;
  bool ok = !sc.bad && sc.nloop == 1 && sc.loop_fn != UINT32_MAX;

  for (uint32_t k = 0; k < t0->n && ok; k++) {
    uint32_t fi = t0->entry[k];
    if (fi == TABLE0_NULL || fi >= nf || in_s[fi]) continue;
    if (!tr_is_loop_type(m, m->funcs[fi].type_index, tid)) continue;
    if (m->funcs[fi].imported || !m->funcs[fi].body || sc.direct[fi] || sc.refd[fi] ||
        fi == sc.loop_fn || (m->has_start && m->start_func == fi))
      ok = false;
    in_s[fi] = 1;
    ns++;
  }
  for (uint32_t e = 0; e < m->nexports && ok; e++)
    if (m->exports[e].kind == EK_FUNC && m->exports[e].index < nf && in_s[m->exports[e].index])
      ok = false;
  for (uint32_t s = 0; s < m->nelems && ok; s++) {
    const ElemSeg *e = &m->elems[s];
    if (e->mode == SEG_ACTIVE) continue;
    for (uint32_t j = 0; j < e->n && ok; j++)
      for (uint32_t k = 0; k < e->items[j].n; k++)
        if (e->items[j].ins[k].op == IOP_REF_FUNC && e->items[j].ins[k].lo < nf &&
            in_s[e->items[j].ins[k].lo])
          ok = false;
  }
  for (uint32_t g = 0; g < ng && ok; g++)
    for (uint32_t k = 0; k < m->globals[g].init.n; k++)
      if (m->globals[g].init.ins[k].op == IOP_REF_FUNC &&
          m->globals[g].init.ins[k].lo < nf && in_s[m->globals[g].init.ins[k].lo])
        ok = false;
  if (!ok || !ns) goto out;

  for (uint32_t fi = m->nimported_funcs; fi < nf && ok; fi++) {
    const Func *f = &m->funcs[fi];
    if (!f->body) continue;
    if (in_s[fi]) ok = tr_body_globals(f, cnt, NULL, ng);
    else ok = tr_body_globals(f, NULL, outside, ng);
  }
  if (!ok) goto out;
  for (uint32_t e = 0; e < m->nexports; e++)
    if (m->exports[e].kind == EK_GLOBAL && m->exports[e].index < ng) outside[m->exports[e].index] = 1;

  Tr t;
  memset(&t, 0, sizeof t);
  t.m = m;
  t.tid = tid;
  t.t0 = t0;
  t.in_s = in_s;
  for (uint32_t k = 0; k < TR_NREG; k++) {
    uint32_t best = UINT32_MAX;
    for (uint32_t g = 0; g < ng; g++) {
      const Global *gl = &m->globals[g];
      if (gl->imported || !gl->mutable_ || gl->type != VT_I32 || outside[g] || !cnt[g]) continue;
      if (tr_reg(&t, g) >= 0) continue;
      if (best == UINT32_MAX || cnt[g] > cnt[best]) best = g;
    }
    if (best == UINT32_MAX) break;
    t.reg[t.nreg++] = best;
  }
  if (!t.nreg) goto out;

  ValType params[TR_NREG], res = VT_I32;
  for (uint32_t k = 0; k < t.nreg; k++) params[k] = VT_I32;
  /* not final: a call through a type the module wrote by the same structure
     -- C's (i32, i32, i32) -> i32 function pointers -- still traps on these
     functions, as it did on their type before */
  t.nt = module_add_func_type(m, params, t.nreg, &res, 1, false);

  uint32_t *done = xmalloc((ns + 1) * sizeof(uint32_t));
  uint32_t nd = 0;
  for (uint32_t fi = 0; fi < nf; fi++)
    if (in_s[fi]) {
      if (!tr_rewrite_s(&t, fi)) warmc_fatal("internal: the trampoline rewrite could not decode function %u", fi);
      done[nd++] = fi;
    }
  if (!tr_rewrite_loop(&t, sc.loop_fn))
    warmc_fatal("internal: the trampoline rewrite found no loop call in function %u", sc.loop_fn);
  done[nd++] = sc.loop_fn;

  wasm_validate_funcs(m, done, nd);
  xfree(done);
  applied = ns;

out:
  /* The table's facts were read of the module before compile_module
     compacts its functions, and of the bodies as they were: they are made
     again when asked for. */
  xfree(m->t0->entry); xfree(m->t0->sites); xfree(m->t0);
  m->t0 = NULL;
  xfree(sc.direct); xfree(sc.refd);
  xfree(in_s); xfree(cnt); xfree(outside);
  return applied;
}
