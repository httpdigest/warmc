#include "ir.h"
#include "opcodes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t fmap(const Module *m, uint32_t old) {
  return m->func_map ? m->func_map[old] : old;
}

typedef enum { C_FUNC, C_BLOCK, C_LOOP, C_IF, C_ELSE, C_TRY } CKind;

typedef struct {
  CKind kind;
  ValType *params; uint32_t nparams;
  ValType *results; uint32_t nresults;
  uint32_t target;
  uint32_t after;
  uint32_t else_block;
  uint32_t stack_base;
  bool dead;
  bool after_reached;
  bool skipped;

  uint32_t pad_first, pad_n;
} Ctl;

typedef struct {
  IrFunc *f;
  Module *m;
  const OptOpts *opt;
  Rd rd;

  Ref *stack; uint32_t sp, cap_stack;
  Ctl *ctl; uint32_t nctl, cap_ctl;
  uint32_t cur;
  bool dead;

  uint32_t *nexplicit; uint32_t cap_blockinfo;
  uint32_t *param_local; uint32_t cap_param_local;

  uint32_t *wq; bool *wq_on; uint32_t nwq, cap_wq;

  uint32_t nloops_seen;
  uint8_t *ldef_out;

  uint32_t *pads; uint32_t npads, cap_pads;
  uint32_t ntry;

  ValType **btypes; uint32_t nbtypes, cap_btypes;
} Bld;

static inline uint8_t rd8(Bld *b) { return rd_u8(&b->rd); }
static inline uint32_t rdu32(Bld *b) { return rd_u32(&b->rd); }
static inline int64_t rd_sleb32(Bld *b) { return rd_sleb(&b->rd, 32); }
static inline int64_t rd_sleb64(Bld *b) { return rd_sleb(&b->rd, 64); }
static inline BlockType rd_bt(Bld *b) { return rd_blocktype(&b->rd); }
static inline uint32_t rd_f32bits(Bld *b) { return rd_u32le(&b->rd); }
static inline uint64_t rd_f64bits(Bld *b) { return rd_u64le(&b->rd); }

static void push(Bld *b, Ref r) {
  if (b->sp == b->cap_stack) {
    b->cap_stack = b->cap_stack ? b->cap_stack * 2 : 32;
    b->stack = xrealloc(b->stack, b->cap_stack * sizeof(Ref));
  }
  b->stack[b->sp++] = r;
}
static Ref pop(Bld *b) {
  if (b->sp == 0) warmc_fatal("operand stack underflow while building IR");
  return b->stack[--b->sp];
}

static void note_block(Bld *b, uint32_t bi) {
  IrFunc *f = b->f;
  if (bi < b->cap_blockinfo) return;
  uint32_t old = b->cap_blockinfo;
  uint32_t want = bi + 1, cap = old ? old : 32;
  while (cap < want) cap *= 2;
  b->cap_blockinfo = cap;
  b->nexplicit = xrealloc(b->nexplicit, b->cap_blockinfo * sizeof(uint32_t));
  memset(b->nexplicit + old, 0, (b->cap_blockinfo - old) * sizeof(uint32_t));
  f->cap_curdef = (size_t)b->cap_blockinfo * f->nlocals * sizeof(Ref);
  f->curdef = xrealloc(f->curdef, f->cap_curdef);
  for (size_t i = (size_t)old * f->nlocals; i < (size_t)b->cap_blockinfo * f->nlocals; i++)
    f->curdef[i] = REF_NONE;
}

static void set_param_local(Bld *b, Ref r, uint32_t local) {
  if (r >= b->cap_param_local) {
    uint32_t old = b->cap_param_local;
    b->cap_param_local = r + 128;
    b->param_local = xrealloc(b->param_local, b->cap_param_local * sizeof(uint32_t));
    for (uint32_t i = old; i < b->cap_param_local; i++) b->param_local[i] = REF_NONE;
  }
  b->param_local[r] = local;
}

static uint32_t new_block(Bld *b, const ValType *ptypes, uint32_t nptypes) {
  uint32_t bi = ir_new_block(b->f);
  note_block(b, bi);
  for (uint32_t i = 0; i < nptypes; i++) {
    Ref p = ir_add_param(b->f, bi, ptypes[i]);
    set_param_local(b, p, REF_NONE);
  }
  b->nexplicit[bi] = nptypes;
  return bi;
}

static void write_local(Bld *b, uint32_t bi, uint32_t idx, Ref v) {
  note_block(b, bi);
  b->f->curdef[(size_t)bi * b->f->nlocals + idx] = v;
}

static Ref read_local(Bld *b, uint32_t bi, uint32_t idx);

static void wq_push(Bld *b, uint32_t bi) {
  if (!b->wq || (bi < b->cap_wq && b->wq_on[bi])) return;
  if (bi >= b->cap_wq) {
    uint32_t old = b->cap_wq;
    while (b->cap_wq <= bi) b->cap_wq *= 2;
    b->wq = xrealloc(b->wq, b->cap_wq * sizeof(uint32_t));
    b->wq_on = xrealloc(b->wq_on, b->cap_wq * sizeof(bool));
    memset(b->wq_on + old, 0, b->cap_wq - old);
  }
  b->wq_on[bi] = true;
  b->wq[b->nwq++] = bi;
}

static Ref read_local_recursive(Bld *b, uint32_t bi, uint32_t idx) {
  IrFunc *f = b->f;
  Block *blk = &f->blocks[bi];
  ValType t = f->local_types[idx];

  /* A local with no source definition outside the entry block has exactly
     one reaching value while the function has no loop: whatever the entry
     holds -- the argument, the zero constant, or the last entry-block set.
     Program order is dominance order in loop-free structured code, so the
     walk, and the parameter it would materialize at every merge on the
     way, answers nothing; on a 4000-arm ladder those walks were the whole
     compile.  The peek stops for good at the first loop: not only can an
     open back edge carry a later definition backward (wiring that eagerly
     at seal time is the recorded failure mode), but the walk's params at
     closed loop headers are live-range splits the allocator prices --
     bypassing them measured 6-11% wall on a dozen kernels. */
  if (!b->nloops_seen && bi != f->entry && !b->ldef_out[idx]) {
    Ref v = read_local(b, f->entry, idx);
    write_local(b, bi, idx, v);
    return v;
  }

  if (!blk->sealed) {
    Ref p = ir_add_param(f, bi, t);
    set_param_local(b, p, idx);
    write_local(b, bi, idx, p);
    wq_push(b, bi);
    return p;
  }
  if (blk->npreds == 0) {
    Ref z = ir_emit(f, bi, I_CONST, t, NULL, 0, 0, 0);

    Block *bb = &f->blocks[bi];
    for (uint32_t i = bb->ninsts - 1; i > 0; i--) {
      uint32_t tmp = bb->insts[i]; bb->insts[i] = bb->insts[i - 1]; bb->insts[i - 1] = tmp;
    }
    write_local(b, bi, idx, z);
    return z;
  }
  if (blk->npreds == 1) {
    Ref v = read_local(b, blk->preds[0], idx);
    write_local(b, bi, idx, v);
    return v;
  }
  Ref p = ir_add_param(f, bi, t);
  set_param_local(b, p, idx);
  write_local(b, bi, idx, p);
  wq_push(b, bi);
  return p;
}

static Ref read_local(Bld *b, uint32_t bi, uint32_t idx) {
  IrFunc *f = b->f;
  note_block(b, bi);
  Ref *slot = &f->curdef[(size_t)bi * f->nlocals + idx];
  if (*slot == REF_NONE) return read_local_recursive(b, bi, idx);
  Ref v = ir_resolve(f, *slot);
  *slot = v;
  return v;
}

static void edge_append(IrFunc *f, Edge *e, Ref v) {
  if (e->nargs == e->cap) {
    uint32_t oc_ = e->cap;
    e->cap = e->cap ? e->cap * 2 : 4;
    e->args = ir_grow(f, e->args, (size_t)oc_ * sizeof(Ref),
                      (size_t)e->cap * sizeof(Ref));
  }
  e->args[e->nargs++] = v;
}

static void drop_param(IrFunc *f, const IrEdgeIdx *ei, uint32_t bi, uint32_t pi) {
  Block *bb = &f->blocks[bi];
  for (uint32_t j = pi; j + 1 < bb->nparams; j++) bb->params[j] = bb->params[j + 1];
  bb->nparams--;
  for (uint32_t k = ei->off[bi]; k < ei->off[bi + 1]; k++) {
    Edge *e = &f->blocks[ei->src[k]].succs[ei->si[k]];
    for (uint32_t j = pi; j + 1 < e->nargs; j++) e->args[j] = e->args[j + 1];
    if (e->nargs) e->nargs--;
  }
}

static Ref remap(const uint32_t *map, Ref r) {
  if (map[r] == REF_NONE)
    warmc_fatal("internal: v%u is used but belongs to no block", r);
  return map[r];
}

static void compact_insts(IrFunc *f) {
  uint32_t *map = xmalloc((f->ninsts ? f->ninsts : 1) * sizeof(uint32_t));
  for (uint32_t i = 0; i < f->ninsts; i++) map[i] = REF_NONE;
  for (uint32_t bi = 0; bi < f->nblocks; bi++) {
    const Block *bb = &f->blocks[bi];
    for (uint32_t i = 0; i < bb->nparams; i++) map[bb->params[i]] = 0;
    for (uint32_t i = 0; i < bb->ninsts; i++) map[bb->insts[i]] = 0;
  }
  uint32_t n = 0;
  for (uint32_t i = 0; i < f->ninsts; i++)
    if (map[i] != REF_NONE) map[i] = n++;
  if (n == f->ninsts) { xfree(map); return; }

  for (uint32_t i = 0; i < f->ninsts; i++) {
    if (map[i] == REF_NONE) continue;
    Inst *in = &f->insts[i];
    Ref *a = &f->argpool[in->args_off];
    for (uint32_t j = 0; j < in->nargs; j++) a[j] = remap(map, a[j]);
    f->insts[map[i]] = *in;
  }
  f->ninsts = n;

  for (uint32_t bi = 0; bi < f->nblocks; bi++) {
    Block *bb = &f->blocks[bi];
    for (uint32_t i = 0; i < bb->nparams; i++) bb->params[i] = remap(map, bb->params[i]);
    for (uint32_t i = 0; i < bb->ninsts; i++) bb->insts[i] = remap(map, bb->insts[i]);
    for (uint32_t i = 0; i < bb->nret; i++) bb->ret_args[i] = remap(map, bb->ret_args[i]);
    if (bb->term == T_BRIF || bb->term == T_BRTABLE)
      bb->term_arg = remap(map, bb->term_arg);
    for (uint32_t s = 0; s < bb->nsuccs; s++) {
      Edge *e = &bb->succs[s];
      for (uint32_t j = 0; j < e->nargs; j++) e->args[j] = remap(map, e->args[j]);
    }
  }
  for (uint32_t i = 0; i < n; i++) f->subst[i] = i;
  xfree(map);
}

static void finalize_params(Bld *b) {
  IrFunc *f = b->f;
  uint32_t *wired = xmalloc(f->nblocks * sizeof(uint32_t));
  for (uint32_t i = 0; i < f->nblocks; i++) wired[i] = b->nexplicit[i];

  IrEdgeIdx ei = ir_edge_index(f);
  uint32_t indexed = f->nblocks;
  bool dropped = false;

  b->cap_wq = f->nblocks ? f->nblocks : 1;
  b->wq = xmalloc(b->cap_wq * sizeof(uint32_t));
  b->wq_on = xcalloc(b->cap_wq, sizeof(bool));
  b->nwq = 0;
  for (uint32_t i = f->nblocks; i-- > 0;)
    if (wired[i] < f->blocks[i].nparams) wq_push(b, i);

  while (b->nwq) {
    uint32_t bi = b->wq[--b->nwq];
    b->wq_on[bi] = false;
    while (wired[bi] < f->blocks[bi].nparams) {
      uint32_t pi = wired[bi]++;
      Ref p = f->blocks[bi].params[pi];
      uint32_t local = (p < b->cap_param_local) ? b->param_local[p] : REF_NONE;
      if (local == REF_NONE)
        warmc_fatal("internal: block parameter v%u has no associated local", p);
      if (f->nblocks != indexed) {
        ir_edge_index_free(&ei);
        ei = ir_edge_index(f);
        indexed = f->nblocks;
      }

      Ref same = REF_NONE;
      bool trivial = true;
      for (uint32_t k = ei.off[bi]; k < ei.off[bi + 1]; k++) {
        Ref v = read_local(b, ei.src[k], local);
        edge_append(f, &f->blocks[ei.src[k]].succs[ei.si[k]], v);
        if (v == p) continue;
        if (same == REF_NONE) same = v;
        else if (same != v) trivial = false;
      }
      if (trivial && same != REF_NONE) {
        ir_replace(f, p, same);
        drop_param(f, &ei, bi, pi);
        wired[bi] = pi;
        dropped = true;
      }
    }
  }
  ir_edge_index_free(&ei);
  xfree(wired);
  xfree(b->wq); xfree(b->wq_on);
  b->wq = NULL; b->wq_on = NULL; b->cap_wq = b->nwq = 0;
  if (dropped) { ir_apply_subst(f); compact_insts(f); }
}

static Ref emit1(Bld *b, IrOp op, ValType t, Ref a) {
  Ref args[1] = { a };
  return ir_emit(b->f, b->cur, op, t, args, 1, 0, 0);
}
static Ref emit2(Bld *b, IrOp op, ValType t, Ref a, Ref c) {
  Ref args[2] = { a, c };
  return ir_emit(b->f, b->cur, op, t, args, 2, 0, 0);
}
static void bin(Bld *b, IrOp op, ValType t) {
  Ref y = pop(b), x = pop(b);
  push(b, emit2(b, op, t, x, y));
}
static void cmp(Bld *b, IrOp op) {
  Ref y = pop(b), x = pop(b);
  push(b, emit2(b, op, VT_I32, x, y));
}
static void un(Bld *b, IrOp op, ValType t) {
  Ref x = pop(b);
  push(b, emit1(b, op, t, x));
}

static void set_term(Bld *b, uint32_t bi, TermKind k) { b->f->blocks[bi].term = (uint8_t)k; }

static void jump_to(Bld *b, uint32_t from, uint32_t to, const Ref *args, uint32_t nargs) {
  set_term(b, from, T_JUMP);
  ir_add_edge(b->f, from, to, args, nargs);
}

static void branch_if(Bld *b, uint32_t d, Ref cond, Ref extra) {
  IrFunc *f = b->f;
  Ctl *t = &b->ctl[b->nctl - 1 - d];
  uint32_t arity = t->kind == C_LOOP ? t->nparams : t->nresults;
  uint32_t nstack = arity - (extra != REF_NONE ? 1u : 0u);
  Ref *args = xmalloc((arity ? arity : 1) * sizeof(Ref));
  memcpy(args, &b->stack[b->sp - nstack], nstack * sizeof(Ref));
  if (extra != REF_NONE) args[nstack] = extra;

  uint32_t next = new_block(b, NULL, 0);
  set_term(b, b->cur, T_BRIF);
  f->blocks[b->cur].term_arg = cond;
  if (t->kind == C_FUNC) {

    uint32_t retb = new_block(b, NULL, 0);
    ir_add_edge(f, b->cur, retb, NULL, 0);
    ir_add_edge(f, b->cur, next, NULL, 0);
    Block *rb = &f->blocks[retb];
    rb->term = T_RETURN;
    rb->nret = arity;
    rb->ret_args = xmalloc((arity ? arity : 1) * sizeof(Ref));
    memcpy(rb->ret_args, args, arity * sizeof(Ref));
    rb->sealed = true;
  } else {
    ir_add_edge(f, b->cur, t->target, args, arity);
    ir_add_edge(f, b->cur, next, NULL, 0);
    if (t->kind != C_LOOP) t->after_reached = true;
  }
  xfree(args);
  f->blocks[next].sealed = true;
  b->cur = next;
}

static void blocktype_sig(Bld *b, BlockType bt, ValType **ps, uint32_t *np,
                          ValType **rs, uint32_t *nr) {
  if (bt.empty) { *ps = NULL; *np = 0; *rs = NULL; *nr = 0; return; }
  if (bt.single) {
    if (b->nbtypes == b->cap_btypes) {
      b->cap_btypes = b->cap_btypes ? b->cap_btypes * 2 : 16;
      b->btypes = xrealloc(b->btypes, b->cap_btypes * sizeof(ValType *));
    }
    ValType *one = xmalloc(sizeof(ValType));
    *one = bt.vt;
    b->btypes[b->nbtypes++] = one;
    *ps = NULL; *np = 0; *rs = one; *nr = 1;
    return;
  }
  if (bt.typeidx >= b->m->ntypes)
    warmc_fatal("block type %llu out of range", (unsigned long long)bt.typeidx);
  CompType *t = &b->m->types[bt.typeidx];
  *ps = t->params; *np = t->nparams;
  *rs = t->results; *nr = t->nresults;
}

static void ctl_push(Bld *b, CKind k, ValType *ps, uint32_t np, ValType *rs, uint32_t nr) {
  if (b->nctl == b->cap_ctl) {
    b->cap_ctl = b->cap_ctl ? b->cap_ctl * 2 : 16;
    b->ctl = xrealloc(b->ctl, b->cap_ctl * sizeof(Ctl));
  }
  Ctl *c = &b->ctl[b->nctl++];
  memset(c, 0, sizeof *c);
  c->kind = k;
  c->params = ps; c->nparams = np;
  c->results = rs; c->nresults = nr;
  c->target = c->after = c->else_block = REF_NONE;
}

static void build_pad(Bld *b, uint8_t kind, uint32_t tag, uint32_t d) {
  IrFunc *f = b->f;
  if (d >= b->nctl) warmc_fatal("catch label depth %u out of range", d);
  Ctl *t = &b->ctl[b->nctl - 1 - d];

  uint32_t pad = new_block(b, NULL, 0);
  f->blocks[pad].is_pad = true;
  f->blocks[pad].pad_kind = kind;
  f->blocks[pad].pad_tag = tag;
  f->npads++;

  Ref exn = ir_emit(f, pad, I_EXN_OBJ, vt_ref(true, HT_EXN), NULL, 0, 0, 0);
  uint32_t np = 0;
  const ValType *ps = NULL;
  if (kind < 2) {
    CompType *tt = &b->m->types[b->m->tags[tag].type_index];
    np = tt->nparams;
    ps = tt->params;
  }
  bool with_ref = (kind == 1 || kind == 3);
  uint32_t n = np + (with_ref ? 1u : 0u);
  Ref *args = xmalloc((n ? n : 1) * sizeof(Ref));
  for (uint32_t i = 0; i < np; i++)
    args[i] = ir_emit(f, pad, I_EXN_PAYLOAD, ps[i], &exn, 1, i, 0);
  if (with_ref) args[np] = exn;

  if (t->kind == C_FUNC) {

    Block *pb = &f->blocks[pad];
    pb->term = T_RETURN;
    pb->nret = n;
    pb->ret_args = xmalloc((n ? n : 1) * sizeof(Ref));
    memcpy(pb->ret_args, args, n * sizeof(Ref));
  } else {
    jump_to(b, pad, t->target, args, n);
    if (t->kind != C_LOOP) t->after_reached = true;
  }
  xfree(args);

  if (b->npads == b->cap_pads) {
    b->cap_pads = b->cap_pads ? b->cap_pads * 2 : 8;
    b->pads = xrealloc(b->pads, b->cap_pads * sizeof(uint32_t));
  }
  b->pads[b->npads++] = pad;
}

static void eh_add_edges(Bld *b, uint32_t from) {
  for (uint32_t ci = b->nctl; ci-- > 0;) {
    Ctl *c = &b->ctl[ci];
    if (c->kind != C_TRY) continue;
    for (uint32_t k = 0; k < c->pad_n; k++)
      ir_add_eh_edge(b->f, from, b->pads[c->pad_first + k]);
  }
}

static void eh_split(Bld *b) {
  if (!b->ntry) return;
  IrFunc *f = b->f;
  uint32_t next = new_block(b, NULL, 0);
  set_term(b, b->cur, T_JUMP);
  ir_add_edge(f, b->cur, next, NULL, 0);
  eh_add_edges(b, b->cur);
  f->blocks[next].sealed = true;
  b->cur = next;
}

static void end_with_throw(Bld *b) {
  eh_add_edges(b, b->cur);
  set_term(b, b->cur, T_TRAP);
  b->f->blocks[b->cur].term_imm = TRAP_UNREACHABLE;
  b->dead = true;
}

typedef struct { uint64_t offset; uint32_t mem; } MemArg;

static ValType mem_index_type(Module *m, uint32_t i) {
  return i < m->nmemories && m->memories[i].index64 ? VT_I64 : VT_I32;
}
static ValType table_index_type(Module *m, uint32_t i) {
  return i < m->ntables && m->tables[i].index64 ? VT_I64 : VT_I32;
}

static MemArg rd_memarg(Bld *b) {
  uint32_t align = rdu32(b);
  MemArg ma;
  ma.mem = (align & 0x40) ? rdu32(b) : 0;
  ma.offset = rd_u64(&b->rd);
  return ma;
}

static Ref mem_ea(Bld *b, Ref addr, uint64_t offset, uint32_t width, uint32_t mem) {
  IrFunc *f = b->f;
  if (mem < f->m->nmemories && f->m->memories[mem].index64) {

    uint64_t need = offset + width;
    if (need < offset) {

      ir_emit(f, b->cur, I_TRAP_IF, 0,
              (Ref[]){ ir_const(f, b->cur, VT_I32, 0) }, 1, TRAP_MEM_OOB, 0);
    } else if (b->opt->level > 0 && need <= mem_guaranteed_bytes(f->m, mem)) {

      Ref lim = ir_emit(f, b->cur, I_MEM_LIMIT, VT_I64, NULL, 0, mem, need);
      Ref a2[2] = { addr, lim };
      ir_emit(f, b->cur, I_BOUNDS_CHECK, 0, a2, 2, mem, need);
    } else {
      ir_emit(f, b->cur, I_BOUNDS_CHECK, 0, &addr, 1, mem, need);
    }
    return ir_emit(f, b->cur, I_MEM_ADDR, VT_I64, &addr, 1, offset, 0);
  }
  Ref ea = ir_emit(f, b->cur, I_MEM_ADDR, VT_I64, &addr, 1, offset, 0);

  if (!b->opt->guard_pages || offset + width > WASM_GUARD_OFFSET_MAX)
    ir_emit(f, b->cur, I_BOUNDS_CHECK, 0, &ea, 1, mem, width);
  return ea;
}

typedef struct { ValType type; int size; int sign; int store; } MemOp;

static bool mem_op_info(uint8_t op, MemOp *mo) {
  switch (op) {
    case 0x28: *mo = (MemOp){ VT_I32, 2, 0, 0 }; return true;
    case 0x29: *mo = (MemOp){ VT_I64, 3, 0, 0 }; return true;
    case 0x2a: *mo = (MemOp){ VT_F32, 2, 0, 0 }; return true;
    case 0x2b: *mo = (MemOp){ VT_F64, 3, 0, 0 }; return true;
    case 0x2c: *mo = (MemOp){ VT_I32, 0, 1, 0 }; return true;
    case 0x2d: *mo = (MemOp){ VT_I32, 0, 0, 0 }; return true;
    case 0x2e: *mo = (MemOp){ VT_I32, 1, 1, 0 }; return true;
    case 0x2f: *mo = (MemOp){ VT_I32, 1, 0, 0 }; return true;
    case 0x30: *mo = (MemOp){ VT_I64, 0, 1, 0 }; return true;
    case 0x31: *mo = (MemOp){ VT_I64, 0, 0, 0 }; return true;
    case 0x32: *mo = (MemOp){ VT_I64, 1, 1, 0 }; return true;
    case 0x33: *mo = (MemOp){ VT_I64, 1, 0, 0 }; return true;
    case 0x34: *mo = (MemOp){ VT_I64, 2, 1, 0 }; return true;
    case 0x35: *mo = (MemOp){ VT_I64, 2, 0, 0 }; return true;
    case 0x36: *mo = (MemOp){ VT_I32, 2, 0, 1 }; return true;
    case 0x37: *mo = (MemOp){ VT_I64, 3, 0, 1 }; return true;
    case 0x38: *mo = (MemOp){ VT_F32, 2, 0, 1 }; return true;
    case 0x39: *mo = (MemOp){ VT_F64, 3, 0, 1 }; return true;
    case 0x3a: *mo = (MemOp){ VT_I32, 0, 0, 1 }; return true;
    case 0x3b: *mo = (MemOp){ VT_I32, 1, 0, 1 }; return true;
    case 0x3c: *mo = (MemOp){ VT_I64, 0, 0, 1 }; return true;
    case 0x3d: *mo = (MemOp){ VT_I64, 1, 0, 1 }; return true;
    case 0x3e: *mo = (MemOp){ VT_I64, 2, 0, 1 }; return true;
  }
  return false;
}

static Ref vec1(Bld *b, IrOp op, ValType t, Ref x, uint64_t imm, uint64_t lane) {
  Ref args[1] = { x };
  return ir_emit(b->f, b->cur, op, t, args, 1, imm, lane);
}
static Ref vec2(Bld *b, IrOp op, ValType t, Ref x, Ref y, uint64_t imm, uint64_t lane) {
  Ref args[2] = { x, y };
  return ir_emit(b->f, b->cur, op, t, args, 2, imm, lane);
}
static Ref vec3(Bld *b, IrOp op, ValType t, Ref x, Ref y, Ref z,
                uint64_t imm, uint64_t lane) {
  Ref args[3] = { x, y, z };
  return ir_emit(b->f, b->cur, op, t, args, 3, imm, lane);
}

static const uint64_t VBITMASK_WEIGHTS[4][2] = {
  { 0x8040201008040201ull, 0x8040201008040201ull },
  { 0x0008000400020001ull, 0x0080004000200010ull },
  { 0x0000000200000001ull, 0x0000000800000004ull },
  { 1ull, 2ull },
};
static void vbitmask(Bld *b, uint64_t lane) {
  Ref x = pop(b);
  int sz = VLANE_SIZE(lane);
  Ref w = ir_emit(b->f, b->cur, I_CONST, VT_V128, NULL, 0,
                  VBITMASK_WEIGHTS[sz][0], VBITMASK_WEIGHTS[sz][1]);
  Ref args[2] = { x, w };
  push(b, ir_emit(b->f, b->cur, I_VBITMASK, VT_I32, args, 2, 0, lane));
}

static void vbin(Bld *b, IrOp op, uint64_t lane) {
  Ref y = pop(b), x = pop(b);
  push(b, vec2(b, op, VT_V128, x, y, 0, lane));
}
static void vbin_imm(Bld *b, IrOp op, uint64_t imm, uint64_t lane) {
  Ref y = pop(b), x = pop(b);
  push(b, vec2(b, op, VT_V128, x, y, imm, lane));
}
static void vun(Bld *b, IrOp op, uint64_t lane) {
  push(b, vec1(b, op, VT_V128, pop(b), 0, lane));
}
static void vun_imm(Bld *b, IrOp op, uint64_t imm, uint64_t lane) {
  push(b, vec1(b, op, VT_V128, pop(b), imm, lane));
}

static ValType lane_scalar(uint64_t lane) {
  switch (lane) {
    case VLANE_I64: return VT_I64;
    case VLANE_F32: return VT_F32;
    case VLANE_F64: return VT_F64;
    default: return VT_I32;
  }
}

static Ref vec_narrow_load(Bld *b, int size) {
  MemArg ma = rd_memarg(b);
  Ref ea = mem_ea(b, pop(b), ma.offset, 1u << size, ma.mem);
  return ir_emit(b->f, b->cur, I_LOAD, VT_V128, &ea, 1, 0,
                 MEMF_MK_AT(size, 0, ma.mem));
}

static void simd_build(Bld *b, uint32_t sub, const char *sym) {
  IrFunc *f = b->f;

  static const IrOp ICMP[10] = { I_EQ, I_NE, I_LTS, I_LTU, I_GTS, I_GTU,
                                 I_LES, I_LEU, I_GES, I_GEU };
  static const IrOp FCMP[6] = { I_FEQ, I_FNE, I_FLT, I_FGT, I_FLE, I_FGE };
  if (sub >= 35 && sub <= 64) {
    uint64_t lane = (sub - 35) / 10;
    vbin_imm(b, I_VCMP, ICMP[(sub - 35) % 10], lane);
    return;
  }
  if (sub >= 65 && sub <= 76) {
    uint64_t lane = sub < 71 ? VLANE_F32 : VLANE_F64;
    vbin_imm(b, I_VCMP, FCMP[(sub - 65) % 6], lane);
    return;
  }
  if (sub >= 214 && sub <= 219) {
    static const IrOp I64CMP[6] = { I_EQ, I_NE, I_LTS, I_GTS, I_LES, I_GES };
    vbin_imm(b, I_VCMP, I64CMP[sub - 214], VLANE_I64);
    return;
  }

  switch (sub) {

    case 0: {
      MemArg ma = rd_memarg(b);
      Ref ea = mem_ea(b, pop(b), ma.offset, 16, ma.mem);
      push(b, ir_emit(f, b->cur, I_LOAD, VT_V128, &ea, 1, 0,
                      MEMF_MK_AT(4, 0, ma.mem)));
      break;
    }
    case 11: {
      MemArg ma = rd_memarg(b);
      Ref val = pop(b), addr = pop(b);
      Ref ea = mem_ea(b, addr, ma.offset, 16, ma.mem);
      Ref a[2] = { ea, val };
      ir_emit(f, b->cur, I_STORE, 0, a, 2, 0, MEMF_MK_AT(4, 0, ma.mem));
      break;
    }
    case 1: case 2: case 3: case 4: case 5: case 6: {

      uint64_t lane = VLANE_I16 + (sub - 1) / 2;
      bool sgn = (sub & 1) != 0;
      Ref v = vec_narrow_load(b, 3);
      push(b, vec1(b, I_VEXTEND, VT_V128, v, sgn ? 1 : 0, lane));
      break;
    }
    case 7: case 8: case 9: case 10: {
      uint64_t lane = VLANE_I8 + (sub - 7);
      Ref v = vec_narrow_load(b, (int)(sub - 7));
      push(b, vec1(b, I_VSPLAT, VT_V128, v, 0, lane));
      break;
    }
    case 92: push(b, vec_narrow_load(b, 2)); break;
    case 93: push(b, vec_narrow_load(b, 3)); break;

    case 84: case 85: case 86: case 87: {
      int size = (int)(sub - 84);
      uint64_t lane = VLANE_I8 + (uint64_t)size;
      MemArg ma = rd_memarg(b);
      uint8_t idx = rd8(b);
      Ref vec = pop(b);
      Ref ea = mem_ea(b, pop(b), ma.offset, 1u << size, ma.mem);
      Ref sc = ir_emit(f, b->cur, I_LOAD, lane_scalar(lane), &ea, 1, 0,
                       MEMF_MK_AT(size, 0, ma.mem));
      push(b, vec2(b, I_VREPLACE, VT_V128, vec, sc, idx, lane));
      break;
    }
    case 88: case 89: case 90: case 91: {
      int size = (int)(sub - 88);
      uint64_t lane = VLANE_I8 + (uint64_t)size;
      MemArg ma = rd_memarg(b);
      uint8_t idx = rd8(b);
      Ref vec = pop(b), addr = pop(b);
      Ref sc = vec1(b, I_VEXTRACTU, lane_scalar(lane), vec, idx, lane);
      Ref ea = mem_ea(b, addr, ma.offset, 1u << size, ma.mem);
      Ref a[2] = { ea, sc };
      ir_emit(f, b->cur, I_STORE, 0, a, 2, 0, MEMF_MK_AT(size, 0, ma.mem));
      break;
    }

    case 12: {

      uint64_t lo = rd_u64le(&b->rd), hi = rd_u64le(&b->rd);
      push(b, ir_emit(f, b->cur, I_CONST, VT_V128, NULL, 0, lo, hi));
      break;
    }
    case 13: {
      uint64_t lo = 0, hi = 0;
      for (int i = 0; i < 16; i++) {
        uint64_t idx = rd8(b);
        if (i < 8) lo |= idx << (8 * i);
        else hi |= idx << (8 * (i - 8));
      }
      Ref y = pop(b), x = pop(b);

      uint64_t lo2 = 0, hi2 = 0;
      bool from_x = false, from_y = false;
      for (int i = 0; i < 16; i++) {
        uint32_t idx = (uint32_t)((i < 8 ? (lo >> (8 * i)) : (hi >> (8 * (i - 8)))) & 0xff);
        if (idx < 16) from_x = true; else from_y = true;
        uint64_t b2 = (idx - 16u) & 0xffu;
        if (i < 8) lo2 |= b2 << (8 * i); else hi2 |= b2 << (8 * (i - 8));
      }
      Ref args[4] = { x, y, REF_NONE, REF_NONE };
      uint32_t n = 2;
      if (from_x || !from_y)
        args[n++] = ir_emit(f, b->cur, I_CONST, VT_V128, NULL, 0, lo, hi);
      if (from_y)
        args[n++] = ir_emit(f, b->cur, I_CONST, VT_V128, NULL, 0, lo2, hi2);
      push(b, ir_emit(f, b->cur, I_VSHUFFLE, VT_V128, args, n, lo, hi));
      break;
    }
    case 14: vbin(b, I_VSWIZZLE, VLANE_I8); break;

    case 15: case 16: case 17: case 18: case 19: case 20: {
      static const uint64_t SPLAT[6] = { VLANE_I8, VLANE_I16, VLANE_I32,
                                         VLANE_I64, VLANE_F32, VLANE_F64 };
      push(b, vec1(b, I_VSPLAT, VT_V128, pop(b), 0, SPLAT[sub - 15]));
      break;
    }

    case 21: case 22: case 23: case 24: case 25: case 26:
    case 27: case 28: case 29: case 30: case 31: case 32:
    case 33: case 34: {

      static const uint64_t LANE[14] = {
        VLANE_I8, VLANE_I8, VLANE_I8, VLANE_I16, VLANE_I16, VLANE_I16,
        VLANE_I32, VLANE_I32, VLANE_I64, VLANE_I64,
        VLANE_F32, VLANE_F32, VLANE_F64, VLANE_F64 };
      static const uint8_t KIND[14] = { 0, 1, 2, 0, 1, 2, 1, 2, 1, 2,
                                        1, 2, 1, 2 };
      uint64_t lane = LANE[sub - 21];
      uint8_t idx = rd8(b);
      if (KIND[sub - 21] == 2) {
        Ref sc = pop(b), vec = pop(b);
        push(b, vec2(b, I_VREPLACE, VT_V128, vec, sc, idx, lane));
      } else {
        IrOp op = KIND[sub - 21] == 0 ? I_VEXTRACTS : I_VEXTRACTU;
        push(b, vec1(b, op, lane_scalar(lane), pop(b), idx, lane));
      }
      break;
    }

    case 77: vun(b, I_VNOT, VLANE_I8); break;
    case 78: vbin(b, I_VAND, VLANE_I8); break;
    case 79: vbin(b, I_VANDNOT, VLANE_I8); break;
    case 80: vbin(b, I_VOR, VLANE_I8); break;
    case 81: vbin(b, I_VXOR, VLANE_I8); break;

    case 82: case 265: case 266: case 267: case 268: {

      Ref c = pop(b), y = pop(b), x = pop(b);
      push(b, vec3(b, I_VSEL, VT_V128, c, x, y, 0, VLANE_I8));
      break;
    }
    case 83: push(b, vec1(b, I_VANYTRUE, VT_I32, pop(b), 0, VLANE_I8)); break;
    case 99:  push(b, vec1(b, I_VALLTRUE, VT_I32, pop(b), 0, VLANE_I8)); break;
    case 131: push(b, vec1(b, I_VALLTRUE, VT_I32, pop(b), 0, VLANE_I16)); break;
    case 163: push(b, vec1(b, I_VALLTRUE, VT_I32, pop(b), 0, VLANE_I32)); break;
    case 195: push(b, vec1(b, I_VALLTRUE, VT_I32, pop(b), 0, VLANE_I64)); break;
    case 100: vbitmask(b, VLANE_I8); break;
    case 132: vbitmask(b, VLANE_I16); break;
    case 164: vbitmask(b, VLANE_I32); break;
    case 196: vbitmask(b, VLANE_I64); break;

    case 96:  vun(b, I_VABS, VLANE_I8); break;
    case 128: vun(b, I_VABS, VLANE_I16); break;
    case 160: vun(b, I_VABS, VLANE_I32); break;
    case 192: vun(b, I_VABS, VLANE_I64); break;
    case 97:  vun(b, I_VNEG, VLANE_I8); break;
    case 129: vun(b, I_VNEG, VLANE_I16); break;
    case 161: vun(b, I_VNEG, VLANE_I32); break;
    case 193: vun(b, I_VNEG, VLANE_I64); break;
    case 98:  vun(b, I_VPOPCNT, VLANE_I8); break;

    case 110: vbin(b, I_VADD, VLANE_I8); break;
    case 142: vbin(b, I_VADD, VLANE_I16); break;
    case 174: vbin(b, I_VADD, VLANE_I32); break;
    case 206: vbin(b, I_VADD, VLANE_I64); break;
    case 113: vbin(b, I_VSUB, VLANE_I8); break;
    case 145: vbin(b, I_VSUB, VLANE_I16); break;
    case 177: vbin(b, I_VSUB, VLANE_I32); break;
    case 209: vbin(b, I_VSUB, VLANE_I64); break;
    case 149: vbin(b, I_VMUL, VLANE_I16); break;
    case 181: vbin(b, I_VMUL, VLANE_I32); break;
    case 213: vbin(b, I_VMUL, VLANE_I64); break;

    case 111: vbin(b, I_VADDSATS, VLANE_I8); break;
    case 112: vbin(b, I_VADDSATU, VLANE_I8); break;
    case 114: vbin(b, I_VSUBSATS, VLANE_I8); break;
    case 115: vbin(b, I_VSUBSATU, VLANE_I8); break;
    case 143: vbin(b, I_VADDSATS, VLANE_I16); break;
    case 144: vbin(b, I_VADDSATU, VLANE_I16); break;
    case 146: vbin(b, I_VSUBSATS, VLANE_I16); break;
    case 147: vbin(b, I_VSUBSATU, VLANE_I16); break;

    case 118: vbin(b, I_VSMIN, VLANE_I8); break;
    case 119: vbin(b, I_VUMIN, VLANE_I8); break;
    case 120: vbin(b, I_VSMAX, VLANE_I8); break;
    case 121: vbin(b, I_VUMAX, VLANE_I8); break;
    case 150: vbin(b, I_VSMIN, VLANE_I16); break;
    case 151: vbin(b, I_VUMIN, VLANE_I16); break;
    case 152: vbin(b, I_VSMAX, VLANE_I16); break;
    case 153: vbin(b, I_VUMAX, VLANE_I16); break;
    case 182: vbin(b, I_VSMIN, VLANE_I32); break;
    case 183: vbin(b, I_VUMIN, VLANE_I32); break;
    case 184: vbin(b, I_VSMAX, VLANE_I32); break;
    case 185: vbin(b, I_VUMAX, VLANE_I32); break;

    case 123: vbin(b, I_VAVGRU, VLANE_I8); break;
    case 155: vbin(b, I_VAVGRU, VLANE_I16); break;
    case 130: vbin(b, I_VQ15MULR, VLANE_I16); break;
    case 186: vbin(b, I_VDOT, VLANE_I32); break;

    case 124: vun_imm(b, I_VEXTADD, 1, VLANE_I16); break;
    case 125: vun_imm(b, I_VEXTADD, 0, VLANE_I16); break;
    case 126: vun_imm(b, I_VEXTADD, 1, VLANE_I32); break;
    case 127: vun_imm(b, I_VEXTADD, 0, VLANE_I32); break;

    case 101: vbin_imm(b, I_VNARROW, 1, VLANE_I8); break;
    case 102: vbin_imm(b, I_VNARROW, 0, VLANE_I8); break;
    case 133: vbin_imm(b, I_VNARROW, 1, VLANE_I16); break;
    case 134: vbin_imm(b, I_VNARROW, 0, VLANE_I16); break;

    case 135: case 136: case 137: case 138:
    case 167: case 168: case 169: case 170:
    case 199: case 200: case 201: case 202: {
      uint64_t lane = VLANE_I16 + (sub - 135) / 32;
      uint32_t k = (sub - 135) % 32;
      uint64_t mod = ((k & 2) ? 0u : 1u) | ((k & 1) ? 2u : 0u);
      vun_imm(b, I_VEXTEND, mod, lane);
      break;
    }
    case 156: case 157: case 158: case 159:
    case 188: case 189: case 190: case 191:
    case 220: case 221: case 222: case 223: {
      uint64_t lane = VLANE_I16 + (sub - 156) / 32;
      uint32_t k = (sub - 156) % 32;
      uint64_t mod = ((k & 2) ? 0u : 1u) | ((k & 1) ? 2u : 0u);
      vbin_imm(b, I_VEXTMUL, mod, lane);
      break;
    }

    case 107: case 108: case 109:
    case 139: case 140: case 141:
    case 171: case 172: case 173:
    case 203: case 204: case 205: {
      static const IrOp SH[3] = { I_VSHLV, I_VSHRSV, I_VSHRUV };
      uint64_t lane = VLANE_I8 + (sub - 107) / 32;
      Ref n = pop(b), x = pop(b);
      push(b, vec2(b, SH[(sub - 107) % 32], VT_V128, x, n, 0, lane));
      break;
    }

    case 224: vun(b, I_VFABS, VLANE_F32); break;
    case 225: vun(b, I_VFNEG, VLANE_F32); break;
    case 227: vun(b, I_VFSQRT, VLANE_F32); break;
    case 228: vbin(b, I_VFADD, VLANE_F32); break;
    case 229: vbin(b, I_VFSUB, VLANE_F32); break;
    case 230: vbin(b, I_VFMUL, VLANE_F32); break;
    case 231: vbin(b, I_VFDIV, VLANE_F32); break;
    case 232: vbin(b, I_VFMIN, VLANE_F32); break;
    case 233: vbin(b, I_VFMAX, VLANE_F32); break;
    case 234: vbin(b, I_VFPMIN, VLANE_F32); break;
    case 235: vbin(b, I_VFPMAX, VLANE_F32); break;
    case 236: vun(b, I_VFABS, VLANE_F64); break;
    case 237: vun(b, I_VFNEG, VLANE_F64); break;
    case 239: vun(b, I_VFSQRT, VLANE_F64); break;
    case 240: vbin(b, I_VFADD, VLANE_F64); break;
    case 241: vbin(b, I_VFSUB, VLANE_F64); break;
    case 242: vbin(b, I_VFMUL, VLANE_F64); break;
    case 243: vbin(b, I_VFDIV, VLANE_F64); break;
    case 244: vbin(b, I_VFMIN, VLANE_F64); break;
    case 245: vbin(b, I_VFMAX, VLANE_F64); break;
    case 246: vbin(b, I_VFPMIN, VLANE_F64); break;
    case 247: vbin(b, I_VFPMAX, VLANE_F64); break;

    case 103: vun(b, I_VFCEIL, VLANE_F32); break;
    case 104: vun(b, I_VFFLOOR, VLANE_F32); break;
    case 105: vun(b, I_VFTRUNC, VLANE_F32); break;
    case 106: vun(b, I_VFNEAREST, VLANE_F32); break;
    case 116: vun(b, I_VFCEIL, VLANE_F64); break;
    case 117: vun(b, I_VFFLOOR, VLANE_F64); break;
    case 122: vun(b, I_VFTRUNC, VLANE_F64); break;
    case 148: vun(b, I_VFNEAREST, VLANE_F64); break;

    case 94: vun(b, I_VDEMOTE, VLANE_F32); break;
    case 95: vun(b, I_VPROMOTE, VLANE_F64); break;
    case 248: vun_imm(b, I_VTRUNCSAT, 1, VLANE_F32); break;
    case 249: vun_imm(b, I_VTRUNCSAT, 0, VLANE_F32); break;
    case 250: vun_imm(b, I_VCONVERT, 1, VLANE_F32); break;
    case 251: vun_imm(b, I_VCONVERT, 0, VLANE_F32); break;
    case 252: vun_imm(b, I_VTRUNCSAT, 1, VLANE_F64); break;
    case 253: vun_imm(b, I_VTRUNCSAT, 0, VLANE_F64); break;
    case 254: vun_imm(b, I_VCONVERT, 1, VLANE_F64); break;
    case 255: vun_imm(b, I_VCONVERT, 0, VLANE_F64); break;

    case 256: vbin(b, I_VSWIZZLE, VLANE_I8); break;
    case 257: vun_imm(b, I_VTRUNCSAT, 1, VLANE_F32); break;
    case 258: vun_imm(b, I_VTRUNCSAT, 0, VLANE_F32); break;
    case 259: vun_imm(b, I_VTRUNCSAT, 1, VLANE_F64); break;
    case 260: vun_imm(b, I_VTRUNCSAT, 0, VLANE_F64); break;
    case 269: vbin(b, I_VFMIN, VLANE_F32); break;
    case 270: vbin(b, I_VFMAX, VLANE_F32); break;
    case 271: vbin(b, I_VFMIN, VLANE_F64); break;
    case 272: vbin(b, I_VFMAX, VLANE_F64); break;
    case 273: vbin(b, I_VQ15MULR, VLANE_I16); break;

    case 261: case 262: case 263: case 264: {
      uint64_t lane = sub < 263 ? VLANE_F32 : VLANE_F64;
      bool neg = (sub & 1) == 0;
      Ref c = pop(b), y = pop(b), x = pop(b);
      if (b->opt->det_profile) {
        if (neg) x = vec1(b, I_VFNEG, VT_V128, x, 0, lane);
        Ref t = vec2(b, I_VFMUL, VT_V128, x, y, 0, lane);
        push(b, vec2(b, I_VFADD, VT_V128, t, c, 0, lane));
      } else {
        push(b, vec3(b, I_VFMADD, VT_V128, x, y, c, neg ? 1u : 0u, lane));
      }
      break;
    }

    case 274: vbin(b, I_VDOTI7, VLANE_I16); break;
    case 275: {
      Ref c = pop(b), y = pop(b), x = pop(b);
      push(b, vec3(b, I_VDOTI7ADD, VT_V128, x, y, c, 0, VLANE_I32));
      break;
    }

    default:

      warmc_fatal("internal: no lowering for 0xfd %u in %s", sub, sym);
  }
}

static uint64_t width_log2(uint32_t w) {
  uint64_t s = 0;
  while ((1u << s) < w) s++;
  return s;
}

static uint64_t struct_total_bytes(const CompType *t) {
  uint64_t tot = (uint64_t)t->hdr_size + t->body_size;
  tot = (tot + 15) & ~15ull;
  return tot < 16 ? 16 : tot;
}

/* An array allocation's shape: header bytes over the element stride.  The
   stride is the element's width, as the type descriptor has it -- not
   body_size, which is a struct's payload and 0 for every array, and which
   left the inline allocation, labcache and escape all reading "no stride"
   and refusing every array. */
static uint64_t array_shape_imm(const CompType *t) {
  return ((uint64_t)t->hdr_size << 8) | vt_width(t->fields[0].type);
}

static uint64_t gc_access_flags(Bld *b, Ref r, ValType field, bool signed_) {
  ValType t = (ValType)b->f->insts[r].type;
  uint64_t fl = width_log2(vt_width(field)) | (signed_ ? GCF_SIGNED : 0u);
  if (!vt_is_ref(t) || vt_nullable(t)) fl |= GCF_NULLCHK;
  return fl;
}

static Ref value_lo(Bld *b, Ref x) {
  ValType t = (ValType)b->f->insts[x].type;
  if (t == VT_F32) return ir_emit(b->f, b->cur, I_REINTERPRET, VT_I32, &x, 1, VT_F32, 0);
  if (t == VT_F64) return ir_emit(b->f, b->cur, I_REINTERPRET, VT_I64, &x, 1, VT_F64, 0);
  if (t == VT_V128) return vec1(b, I_VEXTRACTU, VT_I64, x, 0, VLANE_I64);
  return x;
}
static Ref value_hi(Bld *b, Ref x) {
  ValType t = (ValType)b->f->insts[x].type;
  if (t == VT_V128) return vec1(b, I_VEXTRACTU, VT_I64, x, 1, VLANE_I64);
  return ir_const(b->f, b->cur, VT_I64, 0);
}

static Ref gc_ref_test(Bld *b, Ref x, HeapType ht, bool nul) {
  IrFunc *f = b->f;
  Module *m = b->m;
  ValType t = (ValType)f->insts[x].type;
  bool known_non_null = vt_is_ref(t) && !vt_nullable(t);
  bool is_top = (ht == HT_ANY || ht == HT_FUNC || ht == HT_EXTERN || ht == HT_EXN);
  bool is_bottom = (ht == HT_NONE || ht == HT_NOFUNC ||
                    ht == HT_NOEXTERN || ht == HT_NOEXN);

  if (is_top) {
    if (nul || known_non_null) return ir_const(f, b->cur, VT_I32, 1);
    Ref isn = ir_emit(f, b->cur, I_REF_IS_NULL, VT_I32, &x, 1, 0, 0);
    return ir_emit(f, b->cur, I_EQZ, VT_I32, &isn, 1, 0, 0);
  }
  if (is_bottom) {
    if (!nul || known_non_null) return ir_const(f, b->cur, VT_I32, 0);
    return ir_emit(f, b->cur, I_REF_IS_NULL, VT_I32, &x, 1, 0, 0);
  }

  uint32_t kind, depth = 0;
  uint64_t id = 0;
  if (ht >= 0) {
    kind = m->types[ht].kind == CT_FUNC ? RTK_CONCRETE_FUNC : RTK_CONCRETE_ANY;
    depth = m->types[ht].depth;
    id = m->types[ht].id;
  } else {
    switch (ht) {
      case HT_EQ: kind = RTK_EQ; break;
      case HT_I31: kind = RTK_I31; break;
      case HT_STRUCT: kind = RTK_STRUCT; break;
      case HT_ARRAY: kind = RTK_ARRAY; break;
      default: kind = RTK_ANY; break;
    }
  }
  if (nul) kind |= RTK_NULLABLE;
  return ir_emit(f, b->cur, I_REF_TEST, VT_I32, &x, 1,
                 RTEST_MK(kind, depth, ht), id);
}

static void gc_build(Bld *b, uint32_t sub, const char *sym) {
  IrFunc *f = b->f;
  Module *m = b->m;
  switch (sub) {
    case 0: {
      uint32_t ti = rdu32(b);
      CompType *t = &m->types[ti];

      Ref *vals = xmalloc((t->nfields ? t->nfields : 1) * sizeof(Ref));
      for (uint32_t j = t->nfields; j > 0; j--) vals[j - 1] = pop(b);
      Ref o = ir_emit(f, b->cur, I_STRUCT_NEW, vt_ref(false, (HeapType)ti),
                      NULL, 0, ti, struct_total_bytes(t));
      for (uint32_t j = 0; j < t->nfields; j++) {
        Ref a[2] = { o, vals[j] };

        ir_emit(f, b->cur, I_FIELD_SET, 0, a, 2,
                t->hdr_size + t->fields[j].offset,
                width_log2(vt_width(t->fields[j].type)) | GCF_INIT);
      }
      xfree(vals);
      push(b, o);
      break;
    }
    case 1: {

      uint32_t ti = rdu32(b);
      push(b, ir_emit(f, b->cur, I_STRUCT_NEW, vt_ref(false, (HeapType)ti),
                      NULL, 0, ti, struct_total_bytes(&m->types[ti])));
      break;
    }
    case 2: case 3: case 4: {
      uint32_t ti = rdu32(b), fi = rdu32(b);
      const FieldType *fd = &m->types[ti].fields[fi];
      Ref o = pop(b);
      push(b, ir_emit(f, b->cur, I_FIELD_GET, vt_unpacked(fd->type), &o, 1,
                      m->types[ti].hdr_size + fd->offset,
                      gc_access_flags(b, o, fd->type, sub == 3)));
      break;
    }
    case 5: {
      uint32_t ti = rdu32(b), fi = rdu32(b);
      const FieldType *fd = &m->types[ti].fields[fi];
      Ref val = pop(b), o = pop(b);
      Ref a[2] = { o, val };
      ir_emit(f, b->cur, I_FIELD_SET, 0, a, 2,
              m->types[ti].hdr_size + fd->offset,
              gc_access_flags(b, o, fd->type, false));
      break;
    }

    case 6: case 7: {
      uint32_t ti = rdu32(b);
      Ref len = pop(b);
      Ref a[3];
      uint32_t n = 1;
      if (sub == 6) {
        Ref val = pop(b);
        n = 3;
        a[1] = value_lo(b, val);
        a[2] = value_hi(b, val);
      }
      a[0] = len;
      push(b, ir_emit(f, b->cur, I_ARRAY_NEW, vt_ref(false, (HeapType)ti),
                      a, n, ti, array_shape_imm(&m->types[ti])));
      break;
    }
    case 8: {
      uint32_t ti = rdu32(b);
      uint32_t n = rdu32(b);
      const FieldType *fd = &m->types[ti].fields[0];
      Ref *vals = xmalloc((n ? n : 1) * sizeof(Ref));
      for (uint32_t j = n; j > 0; j--) vals[j - 1] = pop(b);
      Ref len = ir_const(f, b->cur, VT_I32, n);
      Ref o = ir_emit(f, b->cur, I_ARRAY_NEW, vt_ref(false, (HeapType)ti),
                      &len, 1, ti, array_shape_imm(&m->types[ti]));

      uint64_t fl = width_log2(vt_width(fd->type)) | GCF_INIT;
      for (uint32_t j = 0; j < n; j++) {
        Ref idx = ir_const(f, b->cur, VT_I32, j);
        Ref sa[3] = { o, idx, vals[j] };
        ir_emit(f, b->cur, I_ARRAY_SET, 0, sa, 3, ti, fl);
      }
      xfree(vals);
      push(b, o);
      break;
    }
    case 9: case 10: {
      uint32_t ti = rdu32(b), seg = rdu32(b);
      Ref n = pop(b), off = pop(b);
      Ref a[2] = { off, n };
      push(b, ir_emit(f, b->cur, I_ARRAY_NEW_SEG, vt_ref(false, (HeapType)ti),
                      a, 2, ti,
                      ARRO_MK(sub == 9 ? ARRO_INIT_DATA : ARRO_INIT_ELEM, seg)));
      break;
    }
    case 11: case 12: case 13: {
      uint32_t ti = rdu32(b);
      const FieldType *fd = &m->types[ti].fields[0];
      Ref idx = pop(b), o = pop(b);
      Ref a[2] = { o, idx };
      push(b, ir_emit(f, b->cur, I_ARRAY_GET, vt_unpacked(fd->type), a, 2, ti,
                      gc_access_flags(b, o, fd->type, sub == 12)));
      break;
    }
    case 14: {
      uint32_t ti = rdu32(b);
      const FieldType *fd = &m->types[ti].fields[0];
      Ref val = pop(b), idx = pop(b), o = pop(b);
      Ref a[3] = { o, idx, val };
      ir_emit(f, b->cur, I_ARRAY_SET, 0, a, 3, ti,
              gc_access_flags(b, o, fd->type, false));
      break;
    }
    case 15: {
      Ref o = pop(b);
      ValType t = (ValType)f->insts[o].type;
      uint64_t fl = (!vt_is_ref(t) || vt_nullable(t)) ? GCF_NULLCHK : 0u;
      push(b, ir_emit(f, b->cur, I_ARRAY_LEN, VT_I32, &o, 1, 0, fl));
      break;
    }
    case 16: {
      uint32_t ti = rdu32(b);
      Ref n = pop(b), val = pop(b), idx = pop(b), o = pop(b);
      Ref a[5] = { o, idx, n, value_lo(b, val), value_hi(b, val) };
      ir_emit(f, b->cur, I_ARRAY_OP, 0, a, 5, ti, ARRO_MK(ARRO_FILL, 0));
      break;
    }
    case 17: {
      uint32_t dti = rdu32(b);
      (void)rdu32(b);
      Ref n = pop(b), si = pop(b), s = pop(b), di = pop(b), d = pop(b);
      Ref a[5] = { d, di, s, si, n };
      ir_emit(f, b->cur, I_ARRAY_OP, 0, a, 5, dti, ARRO_MK(ARRO_COPY, 0));
      break;
    }
    case 18: case 19: {
      uint32_t ti = rdu32(b), seg = rdu32(b);
      Ref n = pop(b), so = pop(b), di = pop(b), o = pop(b);
      Ref a[4] = { o, di, so, n };
      ir_emit(f, b->cur, I_ARRAY_OP, 0, a, 4, ti,
              ARRO_MK(sub == 18 ? ARRO_INIT_DATA : ARRO_INIT_ELEM, seg));
      break;
    }

    case 26: case 27: {
      Ref x = pop(b);
      ValType t = (ValType)f->insts[x].type;
      bool nul = !vt_is_ref(t) || vt_nullable(t);
      push(b, ir_emit(f, b->cur, sub == 26 ? I_ANY_OF_EXTERN : I_EXTERN_OF_ANY,
                      vt_ref(nul, sub == 26 ? HT_ANY : HT_EXTERN), &x, 1, 0, 0));
      break;
    }
    case 20: case 21: case 22: case 23: {
      bool nul = (sub == 21 || sub == 23);
      HeapType ht = rd_heaptype(&b->rd);
      Ref x = pop(b);
      Ref t = gc_ref_test(b, x, ht, nul);
      if (sub < 22) { push(b, t); break; }

      ir_emit(f, b->cur, I_TRAP_IF, 0, &t, 1, TRAP_CAST, 0);
      ValType src = (ValType)f->insts[x].type;
      bool out_nul = nul && (!vt_is_ref(src) || vt_nullable(src));
      push(b, ir_emit(f, b->cur, I_REF_CONVERT, vt_ref(out_nul, ht), &x, 1, 0, 0));
      break;
    }
    case 24: case 25: {
      uint8_t flags = (uint8_t)rd_u8(&b->rd);
      uint32_t d = rdu32(b);
      HeapType h1 = rd_heaptype(&b->rd);
      HeapType h2 = rd_heaptype(&b->rd);
      ValType t1 = vt_ref((flags & 1) != 0, h1);
      ValType t2 = vt_ref((flags & 2) != 0, h2);
      if (d >= b->nctl) warmc_fatal("branch depth %u out of range", d);

      Ref x = b->stack[b->sp - 1];
      Ref t = gc_ref_test(b, x, h2, vt_nullable(t2));
      bool taken_is_cast = (sub == 24);
      Ref cond = t;
      if (!taken_is_cast)
        cond = ir_emit(f, b->cur, I_EQZ, VT_I32, &t, 1, 0, 0);
      branch_if(b, d, cond, REF_NONE);

      ValType diff = vt_nullable(t2) ? vt_as_non_null(t1) : t1;
      ValType left = taken_is_cast ? diff : t2;
      Ref old = pop(b);
      push(b, ir_emit(f, b->cur, I_REF_CONVERT, left, &old, 1, 0, 0));
      break;
    }

    case 28: {
      Ref x = pop(b);
      push(b, ir_emit(f, b->cur, I_REF_I31, vt_ref(false, HT_I31), &x, 1, 0, 0));
      break;
    }
    case 29: case 30: {
      Ref r = pop(b);
      ValType t = (ValType)f->insts[r].type;
      uint64_t fl = (!vt_is_ref(t) || vt_nullable(t)) ? GCF_NULLCHK : 0u;
      push(b, ir_emit(f, b->cur, I_I31_GET, VT_I32, &r, 1, sub == 29, fl));
      break;
    }

    default:
      warmc_fatal("internal: no lowering for 0xfb %u in %s", sub, sym);
  }
}

IrFunc *ir_build_unordered(Module *m, uint32_t fi, const OptOpts *opt) {
  Func *fn = &m->funcs[fi];
  IrFunc *f = ir_new(m, fi);
  Bld bld;
  memset(&bld, 0, sizeof bld);
  Bld *b = &bld;
  b->f = f; b->m = m; b->opt = opt;
  b->rd = rd_make(fn->body, fn->body_end, m->bin);
  b->ldef_out = xcalloc(f->nlocals ? f->nlocals : 1, 1);

  uint32_t entry = new_block(b, f->sig->params, f->sig->nparams);
  f->entry = entry;
  f->blocks[entry].sealed = true;
  b->cur = entry;
  for (uint32_t i = 0; i < f->sig->nparams; i++)
    write_local(b, entry, i, f->blocks[entry].params[i]);
  for (uint32_t i = f->sig->nparams; i < f->nlocals; i++) {
    Ref z = ir_emit(f, entry, I_CONST, f->local_types[i], NULL, 0, 0, 0);
    write_local(b, entry, i, z);
  }

  ctl_push(b, C_FUNC, NULL, 0, f->sig->results, f->sig->nresults);
  b->ctl[0].stack_base = 0;

  while (!rd_eof(&b->rd)) {
    uint8_t op = rd8(b);

    if (b->dead && op != 0x02 && op != 0x03 && op != 0x04 && op != 0x05 &&
        op != 0x0b && op != 0x1f) {

      Rd r = rd_make(b->rd.p - 1, b->rd.end, b->rd.start);
      uint32_t full = wasm_read_op(&r);
      if (!wasm_skip_imm(&r, full))
        warmc_malformed("unknown opcode 0x%02x in unreachable code", op);
      b->rd.p = r.p;
      continue;
    }

    switch (op) {
      case 0x00:
        set_term(b, b->cur, T_TRAP);
        f->blocks[b->cur].term_imm = TRAP_UNREACHABLE;
        b->dead = true;
        break;
      case 0x01: break;

      case 0x02: case 0x03: case 0x04: {
        BlockType bt = rd_bt(b);
        ValType *ps, *rs; uint32_t np, nr;
        blocktype_sig(b, bt, &ps, &np, &rs, &nr);
        CKind kind = op == 0x02 ? C_BLOCK : (op == 0x03 ? C_LOOP : C_IF);
        ctl_push(b, kind, ps, np, rs, nr);
        Ctl *c = &b->ctl[b->nctl - 1];
        if (b->dead) { c->dead = true; c->skipped = true; break; }

        if (kind == C_IF) {
          Ref cond = pop(b);
          uint32_t base = b->sp - np;
          uint32_t then_b = new_block(b, ps, np);
          uint32_t else_b = new_block(b, ps, np);
          uint32_t after = new_block(b, rs, nr);
          set_term(b, b->cur, T_BRIF);
          f->blocks[b->cur].term_arg = cond;
          ir_add_edge(f, b->cur, then_b, &b->stack[base], np);
          ir_add_edge(f, b->cur, else_b, &b->stack[base], np);
          f->blocks[then_b].sealed = true;
          f->blocks[else_b].sealed = true;
          b->sp = base;
          c->stack_base = base;
          c->target = c->after = after;
          c->else_block = else_b;
          b->cur = then_b;
          for (uint32_t i = 0; i < np; i++) push(b, f->blocks[then_b].params[i]);
        } else if (kind == C_LOOP) {
          b->nloops_seen++;
          uint32_t base = b->sp - np;
          uint32_t header = new_block(b, ps, np);
          uint32_t after = new_block(b, rs, nr);
          jump_to(b, b->cur, header, &b->stack[base], np);
          b->sp = base;
          c->stack_base = base;
          c->target = header;
          c->after = after;
          b->cur = header;
          for (uint32_t i = 0; i < np; i++) push(b, f->blocks[header].params[i]);
        } else {
          uint32_t base = b->sp - np;
          uint32_t after = new_block(b, rs, nr);
          c->stack_base = base;
          c->target = c->after = after;
        }
        break;
      }

      case 0x1f: {
        BlockType bt = rd_bt(b);
        ValType *ps, *rs; uint32_t np, nr;
        blocktype_sig(b, bt, &ps, &np, &rs, &nr);
        uint32_t nc = rdu32(b);
        uint32_t first = b->npads, built = 0;
        for (uint32_t i = 0; i < nc; i++) {
          uint8_t kind = rd8(b);
          uint32_t tag = kind < 2 ? rdu32(b) : 0;
          uint32_t d = rdu32(b);

          if (b->dead) continue;
          build_pad(b, kind, tag, d);
          built++;
        }
        ctl_push(b, C_TRY, ps, np, rs, nr);
        Ctl *c = &b->ctl[b->nctl - 1];
        c->pad_first = first;
        c->pad_n = built;
        if (b->dead) { c->dead = true; c->skipped = true; break; }
        uint32_t base = b->sp - np;
        uint32_t after = new_block(b, rs, nr);
        c->stack_base = base;
        c->target = c->after = after;
        b->ntry++;
        break;
      }

      case 0x08: {
        uint32_t x = rdu32(b);
        CompType *tt = &m->types[m->tags[x].type_index];
        uint32_t base = b->sp - tt->nparams;
        ir_emit(f, b->cur, I_THROW, VT_BOT, &b->stack[base], tt->nparams, x, 0);
        b->sp = base;
        end_with_throw(b);
        break;
      }
      case 0x0a: {
        Ref x = pop(b);
        ir_emit(f, b->cur, I_THROW_REF, VT_BOT, &x, 1, 0, 0);
        end_with_throw(b);
        break;
      }

      case 0x05: {
        Ctl *c = &b->ctl[b->nctl - 1];
        if (c->kind != C_IF) warmc_fatal("`else` without a matching `if`");
        if (c->skipped) { c->kind = C_ELSE; break; }
        if (!b->dead) {
          uint32_t base = b->sp - c->nresults;
          jump_to(b, b->cur, c->after, &b->stack[base], c->nresults);
          c->after_reached = true;
        }
        b->sp = c->stack_base;
        b->cur = c->else_block;
        for (uint32_t i = 0; i < c->nparams; i++) push(b, f->blocks[c->else_block].params[i]);
        c->kind = C_ELSE;
        b->dead = false;
        break;
      }

      case 0x0b: {
        Ctl *c = &b->ctl[b->nctl - 1];
        if (b->nctl == 1) {
          if (!b->dead) {
            uint32_t base = b->sp - c->nresults;
            Block *blk = &f->blocks[b->cur];
            set_term(b, b->cur, T_RETURN);
            blk->nret = c->nresults;
            blk->ret_args = xmalloc((c->nresults ? c->nresults : 1) * sizeof(Ref));
            if (c->nresults)
              memcpy(blk->ret_args, &b->stack[base], c->nresults * sizeof(Ref));
          }
          b->nctl--;
          goto done;
        }

        if (c->kind == C_TRY && !c->skipped) {
          for (uint32_t k = 0; k < c->pad_n; k++)
            f->blocks[b->pads[c->pad_first + k]].sealed = true;
          b->ntry--;
        }
        if (c->skipped) { b->nctl--; break; }

        if (c->kind == C_IF) {

          jump_to(b, c->else_block, c->after, f->blocks[c->else_block].params,
                  f->blocks[c->else_block].nparams);
          c->after_reached = true;
        }
        if (!b->dead) {
          uint32_t base = b->sp - c->nresults;
          jump_to(b, b->cur, c->after, &b->stack[base], c->nresults);
          c->after_reached = true;
        }
        if (c->kind == C_LOOP) f->blocks[c->target].sealed = true;
        f->blocks[c->after].sealed = true;

        b->sp = c->stack_base;
        b->cur = c->after;
        for (uint32_t i = 0; i < c->nresults; i++) push(b, f->blocks[c->after].params[i]);
        b->dead = !c->after_reached;
        b->nctl--;
        break;
      }

      case 0x0c: {
        uint32_t d = rdu32(b);
        if (d >= b->nctl) warmc_fatal("branch depth %u out of range", d);
        Ctl *t = &b->ctl[b->nctl - 1 - d];
        if (t->kind == C_FUNC) {
          uint32_t base = b->sp - t->nresults;
          Block *blk = &f->blocks[b->cur];
          set_term(b, b->cur, T_RETURN);
          blk->nret = t->nresults;
          blk->ret_args = xmalloc((t->nresults ? t->nresults : 1) * sizeof(Ref));
          memcpy(blk->ret_args, &b->stack[base], t->nresults * sizeof(Ref));
        } else {
          uint32_t arity = t->kind == C_LOOP ? t->nparams : t->nresults;
          uint32_t base = b->sp - arity;
          jump_to(b, b->cur, t->target, &b->stack[base], arity);
          if (t->kind != C_LOOP) t->after_reached = true;
        }
        b->dead = true;
        break;
      }

      case 0x0d: {
        uint32_t d = rdu32(b);
        if (d >= b->nctl) warmc_fatal("branch depth %u out of range", d);
        Ref cond = pop(b);
        branch_if(b, d, cond, REF_NONE);
        break;
      }

      case 0x0e: {
        uint32_t n = rdu32(b);
        uint32_t *targets = xmalloc(((uint64_t)n + 1) * sizeof(uint32_t));
        for (uint32_t i = 0; i <= n; i++) targets[i] = rdu32(b);
        Ref idx = pop(b);
        Ctl *t0 = &b->ctl[b->nctl - 1 - targets[n]];
        uint32_t arity = t0->kind == C_LOOP ? t0->nparams
                        : (t0->kind == C_FUNC ? t0->nresults : t0->nresults);
        uint32_t base = b->sp - arity;
        set_term(b, b->cur, T_BRTABLE);
        f->blocks[b->cur].term_arg = idx;
        for (uint32_t i = 0; i <= n; i++) {
          if (targets[i] >= b->nctl) warmc_fatal("br_table depth out of range");
          Ctl *t = &b->ctl[b->nctl - 1 - targets[i]];
          if (t->kind == C_FUNC) {
            uint32_t retb = new_block(b, NULL, 0);
            Block *rb = &f->blocks[retb];
            rb->term = T_RETURN;
            rb->nret = t->nresults;
            rb->ret_args = xmalloc((t->nresults ? t->nresults : 1) * sizeof(Ref));
            memcpy(rb->ret_args, &b->stack[base], t->nresults * sizeof(Ref));
            rb->sealed = true;
            ir_add_edge(f, b->cur, retb, NULL, 0);
          } else {
            ir_add_edge(f, b->cur, t->target, &b->stack[base], arity);
            if (t->kind != C_LOOP) t->after_reached = true;
          }
        }
        xfree(targets);
        b->dead = true;
        break;
      }

      case 0x0f: {
        Ctl *t = &b->ctl[0];
        uint32_t base = b->sp - t->nresults;
        Block *blk = &f->blocks[b->cur];
        set_term(b, b->cur, T_RETURN);
        blk->nret = t->nresults;
        blk->ret_args = xmalloc((t->nresults ? t->nresults : 1) * sizeof(Ref));
        memcpy(blk->ret_args, &b->stack[base], t->nresults * sizeof(Ref));
        b->dead = true;
        break;
      }

      case 0x10: {
        uint32_t idx = fmap(m, rdu32(b));
        if (idx >= m->nfuncs) warmc_fatal("call to undefined function %u", idx);
        CompType *sig = &m->types[m->funcs[idx].type_index];
        uint32_t base = b->sp - sig->nparams;
        Ref call = ir_emit(f, b->cur, I_CALL,
                           sig->nresults == 1 ? sig->results[0] : 0,
                           &b->stack[base], sig->nparams, idx, 0);
        b->sp = base;
        eh_split(b);
        if (sig->nresults == 1) push(b, call);
        else for (uint32_t i = 0; i < sig->nresults; i++) {
          Ref args[1] = { call };
          push(b, ir_emit(f, b->cur, I_RESULT, sig->results[i], args, 1, i, 0));
        }
        break;
      }

      case 0x11: {
        uint32_t ti = rdu32(b);
        uint32_t tb = rdu32(b);
        if (ti >= m->ntypes) warmc_fatal("call_indirect type %u out of range", ti);
        CompType *sig = &m->types[ti];
        Ref callee = pop(b);
        uint32_t base = b->sp - sig->nparams;
        Ref *args = xmalloc((sig->nparams + 1) * sizeof(Ref));
        memcpy(args, &b->stack[base], sig->nparams * sizeof(Ref));
        args[sig->nparams] = callee;
        Ref call = ir_emit(f, b->cur, I_CALL_INDIRECT,
                           sig->nresults == 1 ? sig->results[0] : 0,
                           args, sig->nparams + 1, ti, tb);
        xfree(args);
        b->sp = base;
        eh_split(b);
        if (sig->nresults == 1) push(b, call);
        else for (uint32_t i = 0; i < sig->nresults; i++) {
          Ref a[1] = { call };
          push(b, ir_emit(f, b->cur, I_RESULT, sig->results[i], a, 1, i, 0));
        }
        break;
      }

      case 0x12: case 0x13: case 0x15: {

        CompType *sig;
        Ref callee = REF_NONE;
        uint64_t imm, imm2 = 0;
        IrOp irop = I_RETURN_CALL;
        if (op == 0x12) {
          imm = fmap(m, rdu32(b));
          if (imm >= m->nfuncs) warmc_fatal("return_call to undefined function %u", (uint32_t)imm);
          sig = &m->types[m->funcs[imm].type_index];
        } else if (op == 0x15) {
          imm = rdu32(b);
          if (imm >= m->ntypes) warmc_fatal("return_call_ref type %u out of range", (uint32_t)imm);
          sig = &m->types[imm];
          callee = pop(b);
          irop = I_RETURN_CALL_REF;
        } else {
          imm = rdu32(b);
          imm2 = rdu32(b);
          if (imm >= m->ntypes) warmc_fatal("return_call_indirect type %u out of range", (uint32_t)imm);
          sig = &m->types[imm];
          callee = pop(b);
          irop = I_RETURN_CALL_INDIRECT;
        }
        uint32_t base = b->sp - sig->nparams;
        uint32_t n = sig->nparams + (callee != REF_NONE ? 1u : 0u);
        Ref *args = xmalloc((n ? n : 1) * sizeof(Ref));
        if (sig->nparams) memcpy(args, &b->stack[base], sig->nparams * sizeof(Ref));
        if (callee != REF_NONE) args[sig->nparams] = callee;
        ir_emit(f, b->cur, irop, VT_BOT, args, n, imm, imm2);
        xfree(args);
        b->sp = base;
        b->dead = true;
        break;
      }

      case 0x14: {
        uint32_t ti = rdu32(b);
        if (ti >= m->ntypes) warmc_fatal("call_ref type %u out of range", ti);
        CompType *sig = &m->types[ti];
        Ref callee = pop(b);
        uint32_t base = b->sp - sig->nparams;
        Ref *args = xmalloc((sig->nparams + 1) * sizeof(Ref));
        memcpy(args, &b->stack[base], sig->nparams * sizeof(Ref));
        args[sig->nparams] = callee;
        Ref call = ir_emit(f, b->cur, I_CALL_REF,
                           sig->nresults == 1 ? sig->results[0] : VT_BOT,
                           args, sig->nparams + 1, ti, 0);
        xfree(args);
        b->sp = base;
        eh_split(b);
        if (sig->nresults == 1) push(b, call);
        else for (uint32_t i = 0; i < sig->nresults; i++) {
          Ref a[1] = { call };
          push(b, ir_emit(f, b->cur, I_RESULT, sig->results[i], a, 1, i, 0));
        }
        break;
      }

      case 0x1a: pop(b); break;
      case 0x1b: case 0x1c: {
        if (op == 0x1c) {
          uint32_t n = rdu32(b);
          for (uint32_t i = 0; i < n; i++) (void)rd_valtype(&b->rd);
        }
        Ref cond = pop(b), y = pop(b), x = pop(b);
        Ref args[3] = { x, y, cond };
        push(b, ir_emit(f, b->cur, I_SELECT, (ValType)f->insts[x].type, args, 3, 0, 0));
        break;
      }

      case 0x20: push(b, read_local(b, b->cur, rdu32(b))); break;
      case 0x21: { uint32_t i = rdu32(b); write_local(b, b->cur, i, pop(b));
                   if (b->cur != f->entry) b->ldef_out[i] = 1; break; }
      case 0x22: { uint32_t i = rdu32(b); Ref v = pop(b); write_local(b, b->cur, i, v); push(b, v);
                   if (b->cur != f->entry) b->ldef_out[i] = 1; break; }
      case 0x23: { uint32_t i = rdu32(b);
                   push(b, ir_emit(f, b->cur, I_GLOBAL_GET, m->globals[i].type, NULL, 0, i, 0));
                   break; }
      case 0x24: { uint32_t i = rdu32(b); Ref v = pop(b);
                   ir_emit(f, b->cur, I_GLOBAL_SET, 0, &v, 1, i, 0); break; }

      case 0x25: { uint32_t t = rdu32(b); Ref i = pop(b);
                   push(b, ir_emit(f, b->cur, I_TABLE_GET, m->tables[t].elem_type,
                                   &i, 1, t, 0)); break; }
      case 0x26: { uint32_t t = rdu32(b); Ref v = pop(b), i = pop(b);
                   Ref a[2] = { i, v };
                   ir_emit(f, b->cur, I_TABLE_SET, 0, a, 2, t, 0); break; }

      case 0x3f: { uint32_t mi = rdu32(b);
                   push(b, ir_emit(f, b->cur, I_MEM_SIZE, mem_index_type(m, mi),
                                   NULL, 0, mi, 0)); break; }
      case 0x40: { uint32_t mi = rdu32(b); Ref d = pop(b);
                   push(b, ir_emit(f, b->cur, I_MEM_GROW, mem_index_type(m, mi),
                                   &d, 1, mi, 0)); break; }

      case 0x41: push(b, ir_const(f, b->cur, VT_I32, (uint64_t)(uint32_t)(int32_t)rd_sleb32(b))); break;
      case 0x42: push(b, ir_const(f, b->cur, VT_I64, (uint64_t)rd_sleb64(b))); break;
      case 0x43: push(b, ir_const(f, b->cur, VT_F32, rd_f32bits(b))); break;
      case 0x44: push(b, ir_const(f, b->cur, VT_F64, rd_f64bits(b))); break;

      case 0x45: un(b, I_EQZ, VT_I32); break;
      case 0x46: cmp(b, I_EQ); break;
      case 0x47: cmp(b, I_NE); break;
      case 0x48: cmp(b, I_LTS); break;
      case 0x49: cmp(b, I_LTU); break;
      case 0x4a: cmp(b, I_GTS); break;
      case 0x4b: cmp(b, I_GTU); break;
      case 0x4c: cmp(b, I_LES); break;
      case 0x4d: cmp(b, I_LEU); break;
      case 0x4e: cmp(b, I_GES); break;
      case 0x4f: cmp(b, I_GEU); break;

      case 0x50: un(b, I_EQZ, VT_I32); break;
      case 0x51: cmp(b, I_EQ); break;
      case 0x52: cmp(b, I_NE); break;
      case 0x53: cmp(b, I_LTS); break;
      case 0x54: cmp(b, I_LTU); break;
      case 0x55: cmp(b, I_GTS); break;
      case 0x56: cmp(b, I_GTU); break;
      case 0x57: cmp(b, I_LES); break;
      case 0x58: cmp(b, I_LEU); break;
      case 0x59: cmp(b, I_GES); break;
      case 0x5a: cmp(b, I_GEU); break;

      case 0x5b: cmp(b, I_FEQ); break;
      case 0x5c: cmp(b, I_FNE); break;
      case 0x5d: cmp(b, I_FLT); break;
      case 0x5e: cmp(b, I_FGT); break;
      case 0x5f: cmp(b, I_FLE); break;
      case 0x60: cmp(b, I_FGE); break;
      case 0x61: cmp(b, I_FEQ); break;
      case 0x62: cmp(b, I_FNE); break;
      case 0x63: cmp(b, I_FLT); break;
      case 0x64: cmp(b, I_FGT); break;
      case 0x65: cmp(b, I_FLE); break;
      case 0x66: cmp(b, I_FGE); break;

      case 0x67: un(b, I_CLZ, VT_I32); break;
      case 0x68: un(b, I_CTZ, VT_I32); break;
      case 0x69: un(b, I_POPCNT, VT_I32); break;
      case 0x6a: bin(b, I_ADD, VT_I32); break;
      case 0x6b: bin(b, I_SUB, VT_I32); break;
      case 0x6c: bin(b, I_MUL, VT_I32); break;
      case 0x6d: bin(b, I_DIVS, VT_I32); break;
      case 0x6e: bin(b, I_DIVU, VT_I32); break;
      case 0x6f: bin(b, I_REMS, VT_I32); break;
      case 0x70: bin(b, I_REMU, VT_I32); break;
      case 0x71: bin(b, I_AND, VT_I32); break;
      case 0x72: bin(b, I_OR, VT_I32); break;
      case 0x73: bin(b, I_XOR, VT_I32); break;
      case 0x74: bin(b, I_SHL, VT_I32); break;
      case 0x75: bin(b, I_SHRS, VT_I32); break;
      case 0x76: bin(b, I_SHRU, VT_I32); break;
      case 0x77: bin(b, I_ROTL, VT_I32); break;
      case 0x78: bin(b, I_ROTR, VT_I32); break;

      case 0x79: un(b, I_CLZ, VT_I64); break;
      case 0x7a: un(b, I_CTZ, VT_I64); break;
      case 0x7b: un(b, I_POPCNT, VT_I64); break;
      case 0x7c: bin(b, I_ADD, VT_I64); break;
      case 0x7d: bin(b, I_SUB, VT_I64); break;
      case 0x7e: bin(b, I_MUL, VT_I64); break;
      case 0x7f: bin(b, I_DIVS, VT_I64); break;
      case 0x80: bin(b, I_DIVU, VT_I64); break;
      case 0x81: bin(b, I_REMS, VT_I64); break;
      case 0x82: bin(b, I_REMU, VT_I64); break;
      case 0x83: bin(b, I_AND, VT_I64); break;
      case 0x84: bin(b, I_OR, VT_I64); break;
      case 0x85: bin(b, I_XOR, VT_I64); break;
      case 0x86: bin(b, I_SHL, VT_I64); break;
      case 0x87: bin(b, I_SHRS, VT_I64); break;
      case 0x88: bin(b, I_SHRU, VT_I64); break;
      case 0x89: bin(b, I_ROTL, VT_I64); break;
      case 0x8a: bin(b, I_ROTR, VT_I64); break;

      case 0x8b: un(b, I_FABS, VT_F32); break;
      case 0x8c: un(b, I_FNEG, VT_F32); break;
      case 0x8d: un(b, I_FCEIL, VT_F32); break;
      case 0x8e: un(b, I_FFLOOR, VT_F32); break;
      case 0x8f: un(b, I_FTRUNC, VT_F32); break;
      case 0x90: un(b, I_FNEAREST, VT_F32); break;
      case 0x91: un(b, I_FSQRT, VT_F32); break;
      case 0x92: bin(b, I_FADD, VT_F32); break;
      case 0x93: bin(b, I_FSUB, VT_F32); break;
      case 0x94: bin(b, I_FMUL, VT_F32); break;
      case 0x95: bin(b, I_FDIV, VT_F32); break;
      case 0x96: bin(b, I_FMIN, VT_F32); break;
      case 0x97: bin(b, I_FMAX, VT_F32); break;
      case 0x98: bin(b, I_FCOPYSIGN, VT_F32); break;

      case 0x99: un(b, I_FABS, VT_F64); break;
      case 0x9a: un(b, I_FNEG, VT_F64); break;
      case 0x9b: un(b, I_FCEIL, VT_F64); break;
      case 0x9c: un(b, I_FFLOOR, VT_F64); break;
      case 0x9d: un(b, I_FTRUNC, VT_F64); break;
      case 0x9e: un(b, I_FNEAREST, VT_F64); break;
      case 0x9f: un(b, I_FSQRT, VT_F64); break;
      case 0xa0: bin(b, I_FADD, VT_F64); break;
      case 0xa1: bin(b, I_FSUB, VT_F64); break;
      case 0xa2: bin(b, I_FMUL, VT_F64); break;
      case 0xa3: bin(b, I_FDIV, VT_F64); break;
      case 0xa4: bin(b, I_FMIN, VT_F64); break;
      case 0xa5: bin(b, I_FMAX, VT_F64); break;
      case 0xa6: bin(b, I_FCOPYSIGN, VT_F64); break;

      case 0xa7: un(b, I_WRAP, VT_I32); break;
      case 0xa8: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_TRUNCS, VT_I32, &x, 1, VT_F32, 0)); break; }
      case 0xa9: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_TRUNCU, VT_I32, &x, 1, VT_F32, 0)); break; }
      case 0xaa: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_TRUNCS, VT_I32, &x, 1, VT_F64, 0)); break; }
      case 0xab: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_TRUNCU, VT_I32, &x, 1, VT_F64, 0)); break; }
      case 0xac: un(b, I_EXTENDS, VT_I64); break;
      case 0xad: un(b, I_EXTENDU, VT_I64); break;
      case 0xae: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_TRUNCS, VT_I64, &x, 1, VT_F32, 0)); break; }
      case 0xaf: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_TRUNCU, VT_I64, &x, 1, VT_F32, 0)); break; }
      case 0xb0: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_TRUNCS, VT_I64, &x, 1, VT_F64, 0)); break; }
      case 0xb1: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_TRUNCU, VT_I64, &x, 1, VT_F64, 0)); break; }
      case 0xb2: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_CONVERTS, VT_F32, &x, 1, VT_I32, 0)); break; }
      case 0xb3: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_CONVERTU, VT_F32, &x, 1, VT_I32, 0)); break; }
      case 0xb4: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_CONVERTS, VT_F32, &x, 1, VT_I64, 0)); break; }
      case 0xb5: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_CONVERTU, VT_F32, &x, 1, VT_I64, 0)); break; }
      case 0xb6: un(b, I_DEMOTE, VT_F32); break;
      case 0xb7: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_CONVERTS, VT_F64, &x, 1, VT_I32, 0)); break; }
      case 0xb8: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_CONVERTU, VT_F64, &x, 1, VT_I32, 0)); break; }
      case 0xb9: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_CONVERTS, VT_F64, &x, 1, VT_I64, 0)); break; }
      case 0xba: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_CONVERTU, VT_F64, &x, 1, VT_I64, 0)); break; }
      case 0xbb: un(b, I_PROMOTE, VT_F64); break;
      case 0xbc: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_REINTERPRET, VT_I32, &x, 1, VT_F32, 0)); break; }
      case 0xbd: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_REINTERPRET, VT_I64, &x, 1, VT_F64, 0)); break; }
      case 0xbe: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_REINTERPRET, VT_F32, &x, 1, VT_I32, 0)); break; }
      case 0xbf: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_REINTERPRET, VT_F64, &x, 1, VT_I64, 0)); break; }

      case 0xc0: un(b, I_EXT8S, VT_I32); break;
      case 0xc1: un(b, I_EXT16S, VT_I32); break;
      case 0xc2: un(b, I_EXT8S, VT_I64); break;
      case 0xc3: un(b, I_EXT16S, VT_I64); break;
      case 0xc4: un(b, I_EXT32S, VT_I64); break;

      case 0xd0: { ValType t = vt_ref(true, rd_heaptype(&b->rd));
                   push(b, ir_const(f, b->cur, t, 0)); break; }
      case 0xd1: un(b, I_REF_IS_NULL, VT_I32); break;
      case 0xd2: { uint32_t idx = fmap(m, rdu32(b));
                   ValType t = vt_ref(false, (HeapType)m->funcs[idx].type_index);
                   push(b, ir_emit(f, b->cur, I_REF_FUNC, t, NULL, 0, idx, 0)); break; }

      case 0xd3: { Ref y = pop(b), x = pop(b); Ref a[2] = { x, y };
                   push(b, ir_emit(f, b->cur, I_EQ, VT_I32, a, 2, 0, 0)); break; }

      case 0xd4: { Ref x = pop(b);
                   ir_emit(f, b->cur, I_TRAP_IF, VT_BOT, &x, 1, TRAP_NULL_REF, 0);
                   push(b, x); break; }
      case 0xd5: case 0xd6: {
        uint32_t d = rdu32(b);
        if (d >= b->nctl) warmc_fatal("branch depth %u out of range", d);
        Ref x = pop(b);
        Ref cond = ir_emit(f, b->cur, I_REF_IS_NULL, VT_I32, &x, 1, 0, 0);
        if (op == 0xd6)
          cond = ir_emit(f, b->cur, I_EQZ, VT_I32, &cond, 1, 0, 0);
        branch_if(b, d, cond, op == 0xd6 ? x : REF_NONE);

        if (op == 0xd5) push(b, x);
        break;
      }

      case 0xfc: {
        uint32_t sub = rdu32(b);
        switch (sub) {
          case 0: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_TRUNC_SATS, VT_I32, &x, 1, VT_F32, 0)); break; }
          case 1: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_TRUNC_SATU, VT_I32, &x, 1, VT_F32, 0)); break; }
          case 2: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_TRUNC_SATS, VT_I32, &x, 1, VT_F64, 0)); break; }
          case 3: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_TRUNC_SATU, VT_I32, &x, 1, VT_F64, 0)); break; }
          case 4: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_TRUNC_SATS, VT_I64, &x, 1, VT_F32, 0)); break; }
          case 5: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_TRUNC_SATU, VT_I64, &x, 1, VT_F32, 0)); break; }
          case 6: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_TRUNC_SATS, VT_I64, &x, 1, VT_F64, 0)); break; }
          case 7: { Ref x = pop(b); push(b, ir_emit(f, b->cur, I_TRUNC_SATU, VT_I64, &x, 1, VT_F64, 0)); break; }
          case 8: { uint32_t seg = rdu32(b); uint32_t mi = rdu32(b);
                    Ref n = pop(b), s = pop(b), d = pop(b);
                    Ref a[3] = { d, s, n };
                    ir_emit(f, b->cur, I_MEM_INIT, 0, a, 3, seg, mi); break; }
          case 9: { uint32_t seg = rdu32(b);
                    ir_emit(f, b->cur, I_DATA_DROP, 0, NULL, 0, seg, 0); break; }
          case 10: { uint32_t dm = rdu32(b), sm = rdu32(b);
                     Ref n = pop(b), s = pop(b), d = pop(b);
                     Ref a[3] = { d, s, n };
                     ir_emit(f, b->cur, I_MEM_COPY, 0, a, 3, dm, sm); break; }
          case 11: { uint32_t mi = rdu32(b); Ref n = pop(b), v = pop(b), d = pop(b);
                     Ref a[3] = { d, v, n };
                     ir_emit(f, b->cur, I_MEM_FILL, 0, a, 3, mi, 0); break; }
          case 12: { uint32_t seg = rdu32(b); uint32_t tab = rdu32(b);
                     Ref n = pop(b), s = pop(b), d = pop(b);
                     Ref a[3] = { d, s, n };
                     ir_emit(f, b->cur, I_TABLE_INIT, 0, a, 3, seg, tab); break; }
          case 13: { uint32_t seg = rdu32(b);
                     ir_emit(f, b->cur, I_ELEM_DROP, 0, NULL, 0, seg, 0); break; }
          case 14: { uint32_t dt = rdu32(b), st = rdu32(b);
                     Ref n = pop(b), s = pop(b), d = pop(b);
                     Ref a[3] = { d, s, n };
                     ir_emit(f, b->cur, I_TABLE_COPY, 0, a, 3, dt, st); break; }
          case 15: { uint32_t tab = rdu32(b); Ref n = pop(b), v = pop(b);
                     Ref a[2] = { v, n };
                     push(b, ir_emit(f, b->cur, I_TABLE_GROW, table_index_type(m, tab),
                                     a, 2, tab, 0)); break; }
          case 16: { uint32_t tab = rdu32(b);
                     push(b, ir_emit(f, b->cur, I_TABLE_SIZE, table_index_type(m, tab),
                                     NULL, 0, tab, 0)); break; }
          case 17: { uint32_t tab = rdu32(b); Ref n = pop(b), v = pop(b), i = pop(b);
                     Ref a[3] = { i, v, n };
                     ir_emit(f, b->cur, I_TABLE_FILL, 0, a, 3, tab, 0); break; }
          default: warmc_fatal("unsupported 0xfc opcode %u", sub);
        }
        break;
      }

      case 0xfd: simd_build(b, rdu32(b), fn->sym); break;

      case 0xfb: gc_build(b, rdu32(b), fn->sym); break;

      default: {
        MemOp mo;
        if (mem_op_info(op, &mo)) {
          MemArg ma = rd_memarg(b);
          uint32_t width = 1u << mo.size;
          if (mo.store) {
            Ref val = pop(b), addr = pop(b);
            Ref ea = mem_ea(b, addr, ma.offset, width, ma.mem);
            Ref a[2] = { ea, val };
            ir_emit(f, b->cur, I_STORE, 0, a, 2, 0,
                    MEMF_MK_AT(mo.size, 0, ma.mem));
          } else {
            Ref ea = mem_ea(b, pop(b), ma.offset, width, ma.mem);
            push(b, ir_emit(f, b->cur, I_LOAD, mo.type, &ea, 1, 0,
                            MEMF_MK_AT(mo.size, mo.sign, ma.mem)));
          }
          break;
        }
        warmc_fatal("unsupported opcode 0x%02x in %s", op, fn->sym);
      }
    }
  }

done:
  if (b->nctl != 0) warmc_fatal("unbalanced control structures in %s", fn->sym);

  for (uint32_t i = 0; i < f->nblocks; i++) {
    if (f->blocks[i].term != T_NONE) continue;
    f->blocks[i].term = T_TRAP;
    f->blocks[i].term_imm = TRAP_UNREACHABLE;
  }
  finalize_params(b);

  xfree(f->curdef); f->curdef = NULL; f->cap_curdef = 0;
  xfree(b->stack); xfree(b->ctl); xfree(b->nexplicit); xfree(b->param_local);
  xfree(b->pads); xfree(b->ldef_out);
  for (uint32_t i = 0; i < b->nbtypes; i++) xfree(b->btypes[i]);
  xfree(b->btypes);
  return f;
}

IrFunc *ir_build(Module *m, uint32_t fi, const OptOpts *opt) {
  IrFunc *f = ir_build_unordered(m, fi, opt);
  ir_order(f);
  return f;
}
