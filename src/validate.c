#include "warmc.h"
#include "opcodes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOT VT_BOT

typedef struct {
  uint32_t op;
  ValType *in; uint32_t nin;
  ValType *out; uint32_t nout;
  uint32_t height;
  uint32_t init_height;
  bool unreachable;
} Frame;

typedef struct {
  Module *m;
  const Func *fn;
  uint32_t func_index;

  ValType *stack; uint32_t sp, cap;
  Frame *ctl; uint32_t nctl, cap_ctl;

  bool *init;
  uint32_t *inits; uint32_t ninits, cap_inits;

  ValType **owned; uint32_t nowned, cap_owned;

  Rd rd;
} V;

static ValType *own_type(V *v, ValType t) {
  if (v->nowned == v->cap_owned) {
    v->cap_owned = v->cap_owned ? v->cap_owned * 2 : 16;
    v->owned = xrealloc(v->owned, v->cap_owned * sizeof(ValType *));
  }
  ValType *p = xmalloc(sizeof(ValType));
  *p = t;
  v->owned[v->nowned++] = p;
  return p;
}

static bool *g_declared;

__attribute__((noreturn))
static void invalid(V *v, const char *what) {
  warmc_invalid("function %u: %s (at byte 0x%zx of the body)",
                v->func_index, what, rd_pos(&v->rd));
}

static void push(V *v, ValType t) {
  if (v->sp == v->cap) {
    v->cap = v->cap ? v->cap * 2 : 64;
    v->stack = xrealloc(v->stack, v->cap * sizeof(ValType));
  }
  v->stack[v->sp++] = t;
}

static Frame *top(V *v) { return &v->ctl[v->nctl - 1]; }

static ValType pop(V *v) {
  Frame *f = top(v);
  if (v->sp == f->height) {
    if (f->unreachable) return BOT;
    invalid(v, "type mismatch: stack is empty");
  }
  return v->stack[--v->sp];
}

static ValType pop_expect(V *v, ValType want) {
  ValType got = pop(v);

  if (got == BOT || want == BOT) return got;
  if (!vt_subtype(v->m, got, want)) {
    static char msg[128];
    snprintf(msg, sizeof msg, "type mismatch: expected %s, found %s",
             vt_name(want), vt_name(got));
    invalid(v, msg);
  }
  return got;
}

static ValType pop_ref(V *v, const char *what) {
  ValType t = pop(v);
  if (t != BOT && !vt_is_ref(t)) {
    static char msg[128];
    snprintf(msg, sizeof msg, "type mismatch: %s needs a reference, found %s",
             what, vt_name(t));
    invalid(v, msg);
  }
  return t;
}

static void mark_init(V *v, uint32_t idx) {
  if (v->init[idx]) return;
  v->init[idx] = true;
  if (v->ninits == v->cap_inits) {
    v->cap_inits = v->cap_inits ? v->cap_inits * 2 : 16;
    v->inits = xrealloc(v->inits, v->cap_inits * sizeof(uint32_t));
  }
  v->inits[v->ninits++] = idx;
}

static void reset_inits(V *v, uint32_t height) {
  while (v->ninits > height) v->init[v->inits[--v->ninits]] = false;
}

static void push_many(V *v, const ValType *ts, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) push(v, ts[i]);
}
static void pop_many(V *v, const ValType *ts, uint32_t n) {
  for (uint32_t i = n; i > 0; i--) pop_expect(v, ts[i - 1]);
}

static void peek_many(V *v, const ValType *ts, uint32_t n) {
  ValType *got = xmalloc((n ? n : 1) * sizeof(ValType));
  for (uint32_t i = n; i > 0; i--) got[i - 1] = pop_expect(v, ts[i - 1]);
  push_many(v, got, n);
  xfree(got);
}

static void push_ctrl(V *v, uint32_t op, ValType *in, uint32_t nin,
                      ValType *out, uint32_t nout) {
  if (v->nctl == v->cap_ctl) {
    v->cap_ctl = v->cap_ctl ? v->cap_ctl * 2 : 16;
    v->ctl = xrealloc(v->ctl, v->cap_ctl * sizeof(Frame));
  }
  Frame *f = &v->ctl[v->nctl++];
  f->op = op;
  f->in = in; f->nin = nin;
  f->out = out; f->nout = nout;
  f->height = v->sp;
  f->init_height = v->ninits;
  f->unreachable = false;
  push_many(v, in, nin);
}

static Frame pop_ctrl(V *v) {
  if (v->nctl == 0) invalid(v, "unexpected end of block");
  Frame f = *top(v);
  pop_many(v, f.out, f.nout);
  if (v->sp != f.height) invalid(v, "type mismatch: too many values on the stack");
  reset_inits(v, f.init_height);
  v->nctl--;
  return f;
}

static void mark_unreachable(V *v) {
  Frame *f = top(v);
  v->sp = f->height;
  f->unreachable = true;
}

static const ValType *label_types(V *v, uint32_t depth, uint32_t *n) {
  Frame *f = &v->ctl[v->nctl - 1 - depth];
  if (f->op == 0x03) { *n = f->nin; return f->in; }
  *n = f->nout; return f->out;
}

static ValType read_valtype(V *v) {
  ValType t = rd_valtype(&v->rd);
  if (vt_is_ref(t) && vt_concrete(t) && vt_type_index(t) >= v->m->ntypes)
    invalid(v, "unknown type");
  return t;
}

static HeapType read_heaptype(V *v) {
  HeapType h = rd_heaptype(&v->rd);
  if (h >= 0 && (uint32_t)h >= v->m->ntypes) invalid(v, "unknown type");
  return h;
}

static CompType *func_type_at(V *v, uint32_t idx) {
  if (idx >= v->m->ntypes) invalid(v, "unknown type");
  if (v->m->types[idx].kind != CT_FUNC)
    invalid(v, "type mismatch: type index is not a function type");
  return &v->m->types[idx];
}

static CompType *comp_type_at(V *v, uint32_t idx, CompKind want) {
  if (idx >= v->m->ntypes) invalid(v, "unknown type");
  if (v->m->types[idx].kind != want)
    invalid(v, want == CT_STRUCT ? "type mismatch: type index is not a struct type"
                                 : "type mismatch: type index is not an array type");
  return &v->m->types[idx];
}
static CompType *struct_type_at(V *v, uint32_t idx) {
  return comp_type_at(v, idx, CT_STRUCT);
}
static CompType *array_type_at(V *v, uint32_t idx) {
  return comp_type_at(v, idx, CT_ARRAY);
}

static const FieldType *field_at(V *v, const CompType *t, uint32_t j) {
  if (j >= t->nfields) invalid(v, "unknown field");
  return &t->fields[j];
}

static void block_type(V *v, BlockType bt, ValType **in, uint32_t *nin,
                       ValType **out, uint32_t *nout) {
  if (bt.empty) { *in = NULL; *nin = 0; *out = NULL; *nout = 0; return; }
  if (bt.single) {
    if (vt_is_ref(bt.vt) && vt_concrete(bt.vt) && vt_type_index(bt.vt) >= v->m->ntypes)
      invalid(v, "unknown type");
    *in = NULL; *nin = 0;
    *out = own_type(v, bt.vt); *nout = 1;
    return;
  }
  if (bt.typeidx >= v->m->ntypes) invalid(v, "unknown type: block type index out of range");
  CompType *t = func_type_at(v, bt.typeidx);
  *in = t->params; *nin = t->nparams;
  *out = t->results; *nout = t->nresults;
}

static ValType sig_type(char c) {
  switch (c) {
    case 'i': return VT_I32;
    case 'l': return VT_I64;
    case 'f': return VT_F32;
    case 'd': return VT_F64;
    case 'v': return VT_V128;
  }
  return VT_BOT;
}

static void apply_sig(V *v, const char *sig) {
  const char *colon = strchr(sig, ':');

  for (const char *p = colon; p-- != sig;) pop_expect(v, sig_type(*p));
  for (const char *p = colon + 1; *p; p++) push(v, sig_type(*p));
}

static uint32_t read_memarg(V *v, const OpInfo *oi) {
  uint32_t align = rd_u32(&v->rd);

  if (align >= 0x80) warmc_malformed("malformed memop flags");
  uint32_t memidx = 0;
  if (align & 0x40) { align &= ~0x40u; memidx = rd_u32(&v->rd); }
  uint64_t offset = rd_u64(&v->rd);
  if (memidx >= v->m->nmemories) invalid(v, "unknown memory");
  if (align > oi->align)
    invalid(v, "alignment must not be larger than natural");

  if (!v->m->memories[memidx].index64 && offset > 0xffffffffull)
    invalid(v, "offset out of range for a 32-bit memory");
  return memidx;
}

static ValType mem_index_type(V *v, uint32_t i) {
  return v->m->memories[i].index64 ? VT_I64 : VT_I32;
}
static ValType table_index_type(const Table *t) {
  return t->index64 ? VT_I64 : VT_I32;
}

static void apply_mem_sig(V *v, const char *sig, ValType at) {
  const char *colon = strchr(sig, ':');
  for (const char *p = colon; p-- != sig;)
    pop_expect(v, p == sig ? at : sig_type(*p));
  for (const char *p = colon + 1; *p; p++) push(v, sig_type(*p));
}

static void check_lane(V *v, const OpInfo *oi, uint32_t lane) {
  if (lane >= (16u >> oi->align)) invalid(v, "invalid lane index");
}

static void require_datacount(const Module *m) {
  if (!m->has_datacount && m->ndatas > 0)
    warmc_malformed("data count section required");
}

static Table *table_at(V *v, uint32_t idx) {
  if (idx >= v->m->ntables) invalid(v, "unknown table");
  return &v->m->tables[idx];
}

static bool results_match(const Module *m, const CompType *callee,
                          const CompType *caller) {
  if (callee->nresults != caller->nresults) return false;
  for (uint32_t i = 0; i < callee->nresults; i++)
    if (!vt_subtype(m, callee->results[i], caller->results[i])) return false;
  return true;
}

static void check_catch(V *v) {
  Module *m = v->m;
  uint8_t kind = rd_u8(&v->rd);
  if (kind > 3) warmc_malformed("unknown catch clause 0x%02x", kind);
  uint32_t x = kind < 2 ? rd_u32(&v->rd) : 0;
  uint32_t d = rd_u32(&v->rd);
  if (d >= v->nctl) invalid(v, "unknown label");
  uint32_t n; const ValType *ts = label_types(v, d, &n);

  uint32_t np = 0;
  const ValType *ps = NULL;
  if (kind < 2) {
    if (x >= m->ntags) invalid(v, "unknown tag");
    CompType *tt = &m->types[m->tags[x].type_index];
    np = tt->nparams;
    ps = tt->params;
  }
  bool carries_ref = (kind == 1 || kind == 3);
  if (n != np + (carries_ref ? 1u : 0u)) {
    static char msg[128];
    snprintf(msg, sizeof msg,
             "type mismatch: catch label takes %u values, the clause transfers %u",
             n, np + (carries_ref ? 1u : 0u));
    invalid(v, msg);
  }
  for (uint32_t j = 0; j < np; j++)
    if (!vt_subtype(m, ps[j], ts[j]))
      invalid(v, "type mismatch: catch label does not accept the tag's payload");

  if (carries_ref && !vt_subtype(m, vt_ref(false, HT_EXN), ts[n - 1]))
    invalid(v, "type mismatch: catch label does not accept an exnref");
}

static void validate_body(V *v) {
  Module *m = v->m;
  CompType *sig = &m->types[v->fn->type_index];

  push_ctrl(v, 0, NULL, 0, sig->results, sig->nresults);

  bool ended = false;
  while (!rd_eof(&v->rd)) {
    if (ended) invalid(v, "unexpected instruction after the function body ended");
    uint32_t op = wasm_read_op(&v->rd);
    const OpInfo *oi = wasm_op_info(op);

    if (!oi) {
      if (op > 0xff)
        warmc_malformed("function %u: illegal opcode 0x%02x 0x%02x",
                        v->func_index, op >> 16, op & 0xffff);
      warmc_malformed("function %u: illegal opcode 0x%02x", v->func_index, op);
    }

    if (oi->sig && oi->imm == IMM_NONE) { apply_sig(v, oi->sig); continue; }

    switch (op) {
      case 0x00: mark_unreachable(v); break;
      case 0x01: break;

      case 0x02: case 0x03: case 0x04: {
        BlockType bt = rd_blocktype(&v->rd);
        ValType *in, *out; uint32_t nin, nout;
        block_type(v, bt, &in, &nin, &out, &nout);
        if (op == 0x04) pop_expect(v, VT_I32);
        pop_many(v, in, nin);
        push_ctrl(v, op, in, nin, out, nout);
        break;
      }

      case 0x05: {
        Frame f = pop_ctrl(v);
        if (f.op != 0x04) invalid(v, "else without a matching if");
        push_ctrl(v, 0x05, f.in, f.nin, f.out, f.nout);
        break;
      }

      case 0x0b: {
        Frame f = pop_ctrl(v);

        if (f.op == 0x04 && !(f.nin == f.nout && (!f.nin ||
                              !memcmp(f.in, f.out, f.nin * sizeof(ValType)))))
          invalid(v, "type mismatch: if without else must not change the stack");
        push_many(v, f.out, f.nout);
        if (v->nctl == 0) ended = true;
        break;
      }

      case 0x0c: {
        uint32_t d = rd_u32(&v->rd);
        if (d >= v->nctl) invalid(v, "unknown label");
        uint32_t n; const ValType *ts = label_types(v, d, &n);
        pop_many(v, ts, n);
        mark_unreachable(v);
        break;
      }
      case 0x0d: {
        uint32_t d = rd_u32(&v->rd);
        if (d >= v->nctl) invalid(v, "unknown label");
        pop_expect(v, VT_I32);
        uint32_t n; const ValType *ts = label_types(v, d, &n);

        pop_many(v, ts, n);
        push_many(v, ts, n);
        break;
      }
      case 0x0e: {
        uint32_t n = rd_u32(&v->rd);
        if ((uint64_t)n + 1 > (uint64_t)(v->rd.end - v->rd.p))
          warmc_malformed("br_table vector of %u labels does not fit in the "
                          "function body", n);
        uint32_t *targets = xmalloc(((uint64_t)n + 1) * sizeof(uint32_t));
        for (uint32_t i = 0; i < n; i++) targets[i] = rd_u32(&v->rd);
        targets[n] = rd_u32(&v->rd);
        pop_expect(v, VT_I32);
        uint32_t arity = 0;
        for (uint32_t i = 0; i <= n; i++) {
          if (targets[i] >= v->nctl) { xfree(targets); invalid(v, "unknown label"); }
          uint32_t k; label_types(v, targets[i], &k);
          if (i == 0) arity = k;
          else if (k != arity) { xfree(targets); invalid(v, "type mismatch: br_table targets differ in arity"); }
        }

        for (uint32_t i = 0; i <= n; i++) {
          uint32_t k; const ValType *ts = label_types(v, targets[i], &k);
          peek_many(v, ts, k);
        }
        { uint32_t k; const ValType *ts = label_types(v, targets[n], &k);
          pop_many(v, ts, k); }
        xfree(targets);
        mark_unreachable(v);
        break;
      }

      case 0x08: {
        uint32_t x = rd_u32(&v->rd);
        if (x >= m->ntags) invalid(v, "unknown tag");
        CompType *tt = &m->types[m->tags[x].type_index];
        pop_many(v, tt->params, tt->nparams);
        mark_unreachable(v);
        break;
      }
      case 0x0a: {
        pop_expect(v, vt_ref(true, HT_EXN));
        mark_unreachable(v);
        break;
      }
      case 0x1f: {
        BlockType bt = rd_blocktype(&v->rd);
        ValType *in, *out; uint32_t nin, nout;
        block_type(v, bt, &in, &nin, &out, &nout);

        uint32_t nc = rd_u32(&v->rd);
        for (uint32_t i = 0; i < nc; i++) check_catch(v);
        pop_many(v, in, nin);
        push_ctrl(v, 0x1f, in, nin, out, nout);
        break;
      }

      case 0x0f: {
        CompType *ft = &m->types[v->fn->type_index];
        pop_many(v, ft->results, ft->nresults);
        mark_unreachable(v);
        break;
      }

      case 0x10: {
        uint32_t idx = rd_u32(&v->rd);
        if (idx >= m->nfuncs) invalid(v, "unknown function");
        CompType *ft = &m->types[m->funcs[idx].type_index];
        pop_many(v, ft->params, ft->nparams);
        push_many(v, ft->results, ft->nresults);
        break;
      }
      case 0x11: {
        uint32_t ti = rd_u32(&v->rd);
        uint32_t tbl = rd_u32(&v->rd);
        Table *t = table_at(v, tbl);
        if (!vt_subtype(m, t->elem_type, VT_FUNCREF))
          invalid(v, "type mismatch: call_indirect needs a funcref table");
        CompType *ft = func_type_at(v, ti);
        pop_expect(v, table_index_type(t));
        pop_many(v, ft->params, ft->nparams);
        push_many(v, ft->results, ft->nresults);
        break;
      }

      case 0x14: case 0x15: {
        uint32_t ti = rd_u32(&v->rd);
        CompType *ft = func_type_at(v, ti);
        pop_expect(v, vt_ref(true, (HeapType)ti));
        if (op == 0x15) {
          CompType *self = &m->types[v->fn->type_index];
          if (!results_match(m, ft, self))
            invalid(v, "type mismatch: tail call result type does not match the caller's");
        }
        pop_many(v, ft->params, ft->nparams);
        if (op == 0x15) mark_unreachable(v);
        else push_many(v, ft->results, ft->nresults);
        break;
      }

      case 0x12: case 0x13: {
        CompType *ft;
        if (op == 0x12) {
          uint32_t idx = rd_u32(&v->rd);
          if (idx >= m->nfuncs) invalid(v, "unknown function");
          ft = &m->types[m->funcs[idx].type_index];
        } else {
          uint32_t ti = rd_u32(&v->rd);
          Table *t = table_at(v, rd_u32(&v->rd));
          if (!vt_subtype(m, t->elem_type, VT_FUNCREF))
            invalid(v, "type mismatch: return_call_indirect needs a funcref table");
          ft = func_type_at(v, ti);
          pop_expect(v, table_index_type(t));
        }
        CompType *self = &m->types[v->fn->type_index];
        if (!results_match(m, ft, self))
          invalid(v, "type mismatch: tail call result type does not match the caller's");
        pop_many(v, ft->params, ft->nparams);
        mark_unreachable(v);
        break;
      }

      case 0x1a: pop(v); break;
      case 0x1b: {
        pop_expect(v, VT_I32);
        ValType b = pop(v);
        ValType a = pop(v);
        if ((a != BOT && vt_is_ref(a)) || (b != BOT && vt_is_ref(b)))
          invalid(v, "type mismatch: select without a type needs a numeric operand");
        if (a != BOT && b != BOT && a != b)
          invalid(v, "type mismatch: select operands differ");
        push(v, a == BOT ? b : a);
        break;
      }
      case 0x1c: {
        uint32_t n = rd_u32(&v->rd);
        if (n != 1) invalid(v, "invalid result arity: select takes exactly one type");
        ValType t = read_valtype(v);
        pop_expect(v, VT_I32);
        pop_expect(v, t);
        pop_expect(v, t);
        push(v, t);
        break;
      }

      case 0x20: case 0x21: case 0x22: {
        uint32_t idx = rd_u32(&v->rd);
        if (idx >= v->fn->ntotal_locals) invalid(v, "unknown local");
        ValType t = v->fn->local_types[idx];
        if (op == 0x20) {
          if (!v->init[idx]) invalid(v, "uninitialized local");
          push(v, t);
        } else if (op == 0x21) {
          pop_expect(v, t);
          mark_init(v, idx);
        } else {
          pop_expect(v, t);
          mark_init(v, idx);
          push(v, t);
        }
        break;
      }
      case 0x23: case 0x24: {
        uint32_t idx = rd_u32(&v->rd);
        if (idx >= m->nglobals) invalid(v, "unknown global");
        Global *g = &m->globals[idx];
        if (op == 0x23) push(v, g->type);
        else {
          if (!g->mutable_) invalid(v, "global is immutable");
          pop_expect(v, g->type);
        }
        break;
      }

      case 0x25: {
        Table *t = table_at(v, rd_u32(&v->rd));
        pop_expect(v, table_index_type(t));
        push(v, t->elem_type);
        break;
      }
      case 0x26: {
        Table *t = table_at(v, rd_u32(&v->rd));
        pop_expect(v, t->elem_type);
        pop_expect(v, table_index_type(t));
        break;
      }

      case 0x3f: case 0x40: {
        uint32_t mi = rd_u32(&v->rd);
        if (mi >= m->nmemories) invalid(v, "unknown memory");
        ValType at = mem_index_type(v, mi);
        if (op == 0x40) pop_expect(v, at);
        push(v, at);
        break;
      }

      case 0x41: push(v, VT_I32); rd_i32(&v->rd); break;
      case 0x42: push(v, VT_I64); rd_i64(&v->rd); break;
      case 0x43: push(v, VT_F32); rd_u32le(&v->rd); break;
      case 0x44: push(v, VT_F64); rd_u64le(&v->rd); break;

      case 0xd0: {
        push(v, vt_ref(true, read_heaptype(v)));
        break;
      }
      case 0xd1: {
        pop_ref(v, "ref.is_null");
        push(v, VT_I32);
        break;
      }
      case 0xd2: {
        uint32_t idx = rd_u32(&v->rd);
        if (idx >= m->nfuncs) invalid(v, "unknown function");
        if (!g_declared[idx]) invalid(v, "undeclared function reference");

        push(v, vt_ref(false, (HeapType)m->funcs[idx].type_index));
        break;
      }
      case 0xd3: {
        pop_expect(v, vt_ref(true, HT_EQ));
        pop_expect(v, vt_ref(true, HT_EQ));
        push(v, VT_I32);
        break;
      }
      case 0xd4: {
        ValType t = pop_ref(v, "ref.as_non_null");
        push(v, t == BOT ? BOT : vt_as_non_null(t));
        break;
      }

      case 0xd5: case 0xd6: {
        uint32_t d = rd_u32(&v->rd);
        if (d >= v->nctl) invalid(v, "unknown label");
        uint32_t n; const ValType *ts = label_types(v, d, &n);
        if (op == 0xd5) {
          ValType t = pop_ref(v, "br_on_null");
          pop_many(v, ts, n);
          push_many(v, ts, n);
          push(v, t == BOT ? BOT : vt_as_non_null(t));
        } else {

          if (n == 0) invalid(v, "type mismatch: br_on_non_null needs a label taking a reference");
          ValType t = pop_ref(v, "br_on_non_null");
          if (t != BOT && !vt_subtype(m, vt_as_non_null(t), ts[n - 1]))
            invalid(v, "type mismatch: br_on_non_null label does not accept the reference");
          pop_many(v, ts, n - 1);
          push_many(v, ts, n - 1);
        }
        break;
      }

      default:
        if (oi->imm == IMM_MEMARG) {
          uint32_t mi = read_memarg(v, oi);
          apply_mem_sig(v, oi->sig, mem_index_type(v, mi));
          break;
        }
        if (oi->imm == IMM_MEMARG_LANE) {
          uint32_t mi = read_memarg(v, oi);
          check_lane(v, oi, rd_u8(&v->rd));
          apply_mem_sig(v, oi->sig, mem_index_type(v, mi));
          break;
        }
        if (oi->imm == IMM_LANE) {
          check_lane(v, oi, rd_u8(&v->rd));
          apply_sig(v, oi->sig);
          break;
        }
        if (oi->imm == IMM_LANES16) {

          for (int i = 0; i < 16; i++)
            if (rd_u8(&v->rd) >= 32) invalid(v, "invalid lane index");
          apply_sig(v, oi->sig);
          break;
        }
        switch (op) {
          case OP_PREFIXED(0xfc, 8): {
            uint32_t seg = rd_u32(&v->rd);
            uint32_t mi = rd_u32(&v->rd);
            require_datacount(m);
            if (seg >= m->ndatas) invalid(v, "unknown data segment");
            if (mi >= m->nmemories) invalid(v, "unknown memory");

            pop_expect(v, VT_I32);
            pop_expect(v, VT_I32);
            pop_expect(v, mem_index_type(v, mi));
            break;
          }
          case OP_PREFIXED(0xfc, 9): {
            uint32_t seg = rd_u32(&v->rd);
            require_datacount(m);
            if (seg >= m->ndatas) invalid(v, "unknown data segment");
            break;
          }
          case OP_PREFIXED(0xfc, 10): {
            uint32_t a = rd_u32(&v->rd), b = rd_u32(&v->rd);
            if (a >= m->nmemories || b >= m->nmemories) invalid(v, "unknown memory");

            ValType da = mem_index_type(v, a), sa = mem_index_type(v, b);
            pop_expect(v, da == VT_I32 || sa == VT_I32 ? VT_I32 : VT_I64);
            pop_expect(v, sa);
            pop_expect(v, da);
            break;
          }
          case OP_PREFIXED(0xfc, 11): {
            uint32_t mi = rd_u32(&v->rd);
            if (mi >= m->nmemories) invalid(v, "unknown memory");
            ValType at = mem_index_type(v, mi);
            pop_expect(v, at);
            pop_expect(v, VT_I32);
            pop_expect(v, at);
            break;
          }
          case OP_PREFIXED(0xfc, 12): {
            uint32_t seg = rd_u32(&v->rd);
            Table *t = table_at(v, rd_u32(&v->rd));
            if (seg >= m->nelems) invalid(v, "unknown elem segment");
            if (!vt_subtype(m, m->elems[seg].elem_type, t->elem_type))
              invalid(v, "type mismatch: element segment and table differ");
            pop_expect(v, VT_I32);
            pop_expect(v, VT_I32);
            pop_expect(v, table_index_type(t));
            break;
          }
          case OP_PREFIXED(0xfc, 13): {
            uint32_t seg = rd_u32(&v->rd);
            if (seg >= m->nelems) invalid(v, "unknown elem segment");
            break;
          }
          case OP_PREFIXED(0xfc, 14): {
            Table *a = table_at(v, rd_u32(&v->rd));
            Table *b = table_at(v, rd_u32(&v->rd));

            if (!vt_subtype(m, b->elem_type, a->elem_type))
              invalid(v, "type mismatch: tables differ in element type");
            ValType da = table_index_type(a), sa = table_index_type(b);
            pop_expect(v, da == VT_I32 || sa == VT_I32 ? VT_I32 : VT_I64);
            pop_expect(v, sa);
            pop_expect(v, da);
            break;
          }
          case OP_PREFIXED(0xfc, 15): {
            Table *t = table_at(v, rd_u32(&v->rd));
            pop_expect(v, table_index_type(t));
            pop_expect(v, t->elem_type);
            push(v, table_index_type(t));
            break;
          }
          case OP_PREFIXED(0xfc, 16): {
            Table *t = table_at(v, rd_u32(&v->rd));
            push(v, table_index_type(t));
            break;
          }
          case OP_PREFIXED(0xfc, 17): {
            Table *t = table_at(v, rd_u32(&v->rd));
            pop_expect(v, table_index_type(t));
            pop_expect(v, t->elem_type);
            pop_expect(v, table_index_type(t));
            break;
          }
          case OP_PREFIXED(0xfd, 12):
            rd_u64le(&v->rd); rd_u64le(&v->rd);
            push(v, VT_V128);
            break;

          case OP_PREFIXED(0xfb, 0): {
            CompType *t = struct_type_at(v, rd_u32(&v->rd));
            for (uint32_t j = t->nfields; j > 0; j--)
              pop_expect(v, vt_unpacked(t->fields[j - 1].type));
            push(v, vt_ref(false, (HeapType)(t - m->types)));
            break;
          }
          case OP_PREFIXED(0xfb, 1): {
            uint32_t ti = rd_u32(&v->rd);
            CompType *t = struct_type_at(v, ti);

            for (uint32_t j = 0; j < t->nfields; j++)
              if (!vt_is_packed(t->fields[j].type) &&
                  !vt_defaultable(t->fields[j].type))
                invalid(v, "type mismatch: field has no default value");
            push(v, vt_ref(false, (HeapType)ti));
            break;
          }
          case OP_PREFIXED(0xfb, 2):
          case OP_PREFIXED(0xfb, 3):
          case OP_PREFIXED(0xfb, 4): {
            uint32_t ti = rd_u32(&v->rd);
            CompType *t = struct_type_at(v, ti);
            const FieldType *fd = field_at(v, t, rd_u32(&v->rd));

            bool signed_form = op != OP_PREFIXED(0xfb, 2);
            if (vt_is_packed(fd->type) != signed_form)
              invalid(v, vt_is_packed(fd->type)
                         ? "type mismatch: packed field needs struct.get_s or _u"
                         : "type mismatch: struct.get_s/_u needs a packed field");
            pop_expect(v, vt_ref(true, (HeapType)ti));
            push(v, vt_unpacked(fd->type));
            break;
          }
          case OP_PREFIXED(0xfb, 5): {
            uint32_t ti = rd_u32(&v->rd);
            CompType *t = struct_type_at(v, ti);
            const FieldType *fd = field_at(v, t, rd_u32(&v->rd));
            if (!fd->mutable_) invalid(v, "type mismatch: field is immutable");
            pop_expect(v, vt_unpacked(fd->type));
            pop_expect(v, vt_ref(true, (HeapType)ti));
            break;
          }

          case OP_PREFIXED(0xfb, 6):
          case OP_PREFIXED(0xfb, 7): {
            uint32_t ti = rd_u32(&v->rd);
            CompType *t = array_type_at(v, ti);
            pop_expect(v, VT_I32);
            if (op == OP_PREFIXED(0xfb, 6))
              pop_expect(v, vt_unpacked(t->fields[0].type));
            else if (!vt_is_packed(t->fields[0].type) &&
                     !vt_defaultable(t->fields[0].type))
              invalid(v, "type mismatch: element has no default value");
            push(v, vt_ref(false, (HeapType)ti));
            break;
          }
          case OP_PREFIXED(0xfb, 8): {
            uint32_t ti = rd_u32(&v->rd);
            CompType *t = array_type_at(v, ti);
            uint32_t n = rd_u32(&v->rd);
            if (n > MAX_ARRAY_NEW_FIXED)
              invalid(v, "array.new_fixed element count is above the limit");
            for (uint32_t j = 0; j < n; j++) {
              Frame *fr = top(v);
              if (v->sp == fr->height && fr->unreachable) break;
              pop_expect(v, vt_unpacked(t->fields[0].type));
            }
            push(v, vt_ref(false, (HeapType)ti));
            break;
          }
          case OP_PREFIXED(0xfb, 9):
          case OP_PREFIXED(0xfb, 10): {
            uint32_t ti = rd_u32(&v->rd);
            CompType *t = array_type_at(v, ti);
            uint32_t seg = rd_u32(&v->rd);
            if (op == OP_PREFIXED(0xfb, 9)) {

              if (vt_is_ref(t->fields[0].type))
                invalid(v, "type mismatch: array.new_data needs a numeric element");
              require_datacount(m);
              if (seg >= m->ndatas) invalid(v, "unknown data segment");
            } else {
              if (seg >= m->nelems) invalid(v, "unknown elem segment");
              if (!vt_subtype(m, m->elems[seg].elem_type, t->fields[0].type))
                invalid(v, "type mismatch: element segment type does not match");
            }
            pop_expect(v, VT_I32);
            pop_expect(v, VT_I32);
            push(v, vt_ref(false, (HeapType)ti));
            break;
          }
          case OP_PREFIXED(0xfb, 11):
          case OP_PREFIXED(0xfb, 12):
          case OP_PREFIXED(0xfb, 13): {
            uint32_t ti = rd_u32(&v->rd);
            CompType *t = array_type_at(v, ti);
            bool signed_form = op != OP_PREFIXED(0xfb, 11);
            if (vt_is_packed(t->fields[0].type) != signed_form)
              invalid(v, vt_is_packed(t->fields[0].type)
                         ? "type mismatch: packed element needs array.get_s or _u"
                         : "type mismatch: array.get_s/_u needs a packed element");
            pop_expect(v, VT_I32);
            pop_expect(v, vt_ref(true, (HeapType)ti));
            push(v, vt_unpacked(t->fields[0].type));
            break;
          }
          case OP_PREFIXED(0xfb, 14): {
            uint32_t ti = rd_u32(&v->rd);
            CompType *t = array_type_at(v, ti);
            if (!t->fields[0].mutable_)
              invalid(v, "type mismatch: array is immutable");
            pop_expect(v, vt_unpacked(t->fields[0].type));
            pop_expect(v, VT_I32);
            pop_expect(v, vt_ref(true, (HeapType)ti));
            break;
          }
          case OP_PREFIXED(0xfb, 15): {

            pop_expect(v, vt_ref(true, HT_ARRAY));
            push(v, VT_I32);
            break;
          }
          case OP_PREFIXED(0xfb, 16): {
            uint32_t ti = rd_u32(&v->rd);
            CompType *t = array_type_at(v, ti);
            if (!t->fields[0].mutable_)
              invalid(v, "type mismatch: array is immutable");
            pop_expect(v, VT_I32);
            pop_expect(v, vt_unpacked(t->fields[0].type));
            pop_expect(v, VT_I32);
            pop_expect(v, vt_ref(true, (HeapType)ti));
            break;
          }
          case OP_PREFIXED(0xfb, 17): {
            uint32_t dti = rd_u32(&v->rd);
            CompType *dt = array_type_at(v, dti);
            uint32_t sti = rd_u32(&v->rd);
            CompType *st = array_type_at(v, sti);
            if (!dt->fields[0].mutable_)
              invalid(v, "type mismatch: destination array is immutable");

            if (!vt_subtype(m, st->fields[0].type, dt->fields[0].type))
              invalid(v, "type mismatch: array.copy element types do not match");
            pop_expect(v, VT_I32);
            pop_expect(v, VT_I32);
            pop_expect(v, vt_ref(true, (HeapType)sti));
            pop_expect(v, VT_I32);
            pop_expect(v, vt_ref(true, (HeapType)dti));
            break;
          }
          case OP_PREFIXED(0xfb, 18):
          case OP_PREFIXED(0xfb, 19): {
            uint32_t ti = rd_u32(&v->rd);
            CompType *t = array_type_at(v, ti);
            uint32_t seg = rd_u32(&v->rd);
            if (!t->fields[0].mutable_)
              invalid(v, "type mismatch: array is immutable");
            if (op == OP_PREFIXED(0xfb, 18)) {
              if (vt_is_ref(t->fields[0].type))
                invalid(v, "type mismatch: array.init_data needs a numeric element");
              require_datacount(m);
              if (seg >= m->ndatas) invalid(v, "unknown data segment");
            } else {
              if (seg >= m->nelems) invalid(v, "unknown elem segment");
              if (!vt_subtype(m, m->elems[seg].elem_type, t->fields[0].type))
                invalid(v, "type mismatch: element segment type does not match");
            }
            pop_expect(v, VT_I32);
            pop_expect(v, VT_I32);
            pop_expect(v, VT_I32);
            pop_expect(v, vt_ref(true, (HeapType)ti));
            break;
          }

          case OP_PREFIXED(0xfb, 20):
          case OP_PREFIXED(0xfb, 21):
          case OP_PREFIXED(0xfb, 22):
          case OP_PREFIXED(0xfb, 23): {
            bool nul = (op == OP_PREFIXED(0xfb, 21) || op == OP_PREFIXED(0xfb, 23));
            bool cast = op >= OP_PREFIXED(0xfb, 22);
            ValType want = vt_ref(nul, read_heaptype(v));

            ValType got = pop_ref(v, cast ? "ref.cast" : "ref.test");
            if (got != BOT && vt_join(m, got, want) == VT_BOT)
              invalid(v, "type mismatch: cast target is in a different hierarchy");
            if (cast) {

              push(v, got == BOT ? want
                                 : (vt_nullable(got) ? want : vt_as_non_null(want)));
            } else {
              push(v, VT_I32);
            }
            break;
          }
          case OP_PREFIXED(0xfb, 24):
          case OP_PREFIXED(0xfb, 25): {
            uint8_t flags = rd_u8(&v->rd);
            if (flags > 3) warmc_malformed("malformed br_on_cast flags 0x%02x", flags);
            uint32_t d = rd_u32(&v->rd);
            ValType t1 = vt_ref((flags & 1) != 0, read_heaptype(v));
            ValType t2 = vt_ref((flags & 2) != 0, read_heaptype(v));
            if (d >= v->nctl) invalid(v, "unknown label");

            if (!vt_subtype(m, t2, t1))
              invalid(v, "type mismatch: br_on_cast target is not a subtype of its source");
            uint32_t n; const ValType *ts = label_types(v, d, &n);
            if (n == 0)
              invalid(v, "type mismatch: br_on_cast label takes no values");

            bool taken_is_cast = (op == OP_PREFIXED(0xfb, 24));
            ValType diff = vt_nullable(t2) ? vt_as_non_null(t1) : t1;
            ValType to_label = taken_is_cast ? t2 : diff;
            ValType result = taken_is_cast ? diff : t2;
            if (!vt_subtype(m, to_label, ts[n - 1]))
              invalid(v, "type mismatch: br_on_cast label does not accept the result");
            pop_expect(v, t1);

            pop_many(v, ts, n - 1);
            push_many(v, ts, n - 1);
            push(v, result);
            break;
          }

          case OP_PREFIXED(0xfb, 26): {
            ValType t = pop_expect(v, vt_ref(true, HT_EXTERN));
            push(v, vt_ref(t != BOT && !vt_nullable(t) ? false : true, HT_ANY));
            break;
          }
          case OP_PREFIXED(0xfb, 27): {
            ValType t = pop_expect(v, vt_ref(true, HT_ANY));
            push(v, vt_ref(t != BOT && !vt_nullable(t) ? false : true, HT_EXTERN));
            break;
          }

          case OP_PREFIXED(0xfb, 28):
            pop_expect(v, VT_I32);
            push(v, vt_ref(false, HT_I31));
            break;
          case OP_PREFIXED(0xfb, 29):
          case OP_PREFIXED(0xfb, 30):
            pop_expect(v, vt_ref(true, HT_I31));
            push(v, VT_I32);
            break;
          default:

            invalid(v, "unsupported instruction");
        }
    }
  }

  if (!ended)
    warmc_malformed("function %u: unexpected end of function body: missing end "
                    "opcode (at byte 0x%zx of the body)",
                    v->func_index, rd_pos(&v->rd));
}

static void expect_init(Module *m, const InitExpr *e, ValType want,
                        uint32_t upto_global, const char *what) {
  ValType *st = xmalloc((e->n + 1) * sizeof(ValType));
  uint32_t sp = 0, peak = 0;

  for (uint32_t i = 0; i < e->n; i++) {
    const InitInsn *in = &e->ins[i];
    ValType binop = VT_BOT;
    switch (in->op) {
      case IOP_I32_CONST:  st[sp++] = VT_I32; break;
      case IOP_I64_CONST:  st[sp++] = VT_I64; break;
      case IOP_F32_CONST:  st[sp++] = VT_F32; break;
      case IOP_F64_CONST:  st[sp++] = VT_F64; break;
      case IOP_V128_CONST: st[sp++] = VT_V128; break;
      case IOP_REF_NULL: {

        ValType t = (ValType)in->lo;
        if (vt_concrete(t) && vt_type_index(t) >= m->ntypes)
          warmc_invalid("unknown type %u in %s", vt_type_index(t), what);
        st[sp++] = t;
        break;
      }
      case IOP_REF_FUNC:
        if (in->lo >= m->nfuncs)
          warmc_invalid("unknown function %u in %s", (uint32_t)in->lo, what);
        st[sp++] = vt_ref(false, (HeapType)m->funcs[in->lo].type_index);
        break;
      case IOP_GLOBAL_GET: {

        uint32_t gi = (uint32_t)in->lo;
        if (in->lo >= upto_global)
          warmc_invalid("unknown global %u in %s", gi, what);
        if (m->globals[gi].mutable_)
          warmc_invalid("constant expression required: global %u is mutable", gi);
        st[sp++] = m->globals[gi].type;
        break;
      }
      case IOP_I32_ADD: case IOP_I32_SUB: case IOP_I32_MUL: binop = VT_I32; break;
      case IOP_I64_ADD: case IOP_I64_SUB: case IOP_I64_MUL: binop = VT_I64; break;
      case IOP_REF_CONVERT: {

        if (sp < 1) warmc_invalid("type mismatch in %s: not enough operands", what);
        ValType from = in->lo ? vt_ref(true, HT_ANY) : vt_ref(true, HT_EXTERN);
        if (!vt_subtype(m, st[sp - 1], from))
          warmc_invalid("type mismatch in %s: expected %s, found %s", what,
                        vt_name(from), vt_name(st[sp - 1]));
        bool nul = vt_nullable(st[sp - 1]);
        st[sp - 1] = vt_ref(nul, in->lo ? HT_EXTERN : HT_ANY);
        break;
      }
      case IOP_REF_I31:
        if (sp < 1 || st[sp - 1] != VT_I32)
          warmc_invalid("type mismatch in %s: ref.i31 needs an i32", what);
        st[sp - 1] = vt_ref(false, HT_I31);
        break;
      case IOP_ARRAY_NEW: case IOP_ARRAY_NEW_DEFAULT:
      case IOP_ARRAY_NEW_FIXED: {
        uint32_t ti = (uint32_t)in->lo;
        if (ti >= m->ntypes || m->types[ti].kind != CT_ARRAY)
          warmc_invalid("type mismatch in %s: type %u is not an array type",
                        what, ti);
        ValType et = vt_unpacked(m->types[ti].fields[0].type);
        if (in->op == IOP_ARRAY_NEW_FIXED) {
          uint32_t n = (uint32_t)in->hi;
          if (sp < n)
            warmc_invalid("type mismatch in %s: not enough operands", what);
          sp -= n;
          for (uint32_t j = 0; j < n; j++)
            if (!vt_subtype(m, st[sp + j], et))
              warmc_invalid("type mismatch in %s: element %u expects %s", what,
                            j, vt_name(et));
        } else {

          if (sp < 1 || st[sp - 1] != VT_I32)
            warmc_invalid("type mismatch in %s: array length must be an i32", what);
          sp--;
          if (in->op == IOP_ARRAY_NEW) {
            if (sp < 1) warmc_invalid("type mismatch in %s: not enough operands", what);
            sp--;
            if (!vt_subtype(m, st[sp], et))
              warmc_invalid("type mismatch in %s: element expects %s", what,
                            vt_name(et));
          } else if (!vt_is_packed(m->types[ti].fields[0].type) &&
                     !vt_defaultable(m->types[ti].fields[0].type)) {
            warmc_invalid("type mismatch in %s: element has no default value", what);
          }
        }
        st[sp++] = vt_ref(false, (HeapType)ti);
        break;
      }
      case IOP_STRUCT_NEW: case IOP_STRUCT_NEW_DEFAULT: {
        uint32_t ti = (uint32_t)in->lo;
        if (ti >= m->ntypes || m->types[ti].kind != CT_STRUCT)
          warmc_invalid("type mismatch in %s: type %u is not a struct type",
                        what, ti);
        CompType *t = &m->types[ti];
        if (in->op == IOP_STRUCT_NEW) {
          if (sp < t->nfields)
            warmc_invalid("type mismatch in %s: not enough operands", what);
          sp -= t->nfields;
          for (uint32_t j = 0; j < t->nfields; j++)
            if (!vt_subtype(m, st[sp + j], vt_unpacked(t->fields[j].type)))
              warmc_invalid("type mismatch in %s: field %u expects %s", what, j,
                            vt_name(vt_unpacked(t->fields[j].type)));
        } else {
          for (uint32_t j = 0; j < t->nfields; j++)
            if (!vt_is_packed(t->fields[j].type) &&
                !vt_defaultable(t->fields[j].type))
              warmc_invalid("type mismatch in %s: field %u has no default value",
                            what, j);
        }
        st[sp++] = vt_ref(false, (HeapType)ti);
        break;
      }
      default: warmc_invalid("constant expression required in %s", what);
    }
    if (sp > peak) peak = sp;
    if (binop != VT_BOT) {
      if (sp < 2) warmc_invalid("type mismatch in %s: not enough operands", what);
      if (st[sp - 1] != binop || st[sp - 2] != binop)
        warmc_invalid("type mismatch in %s: expected %s", what, vt_name(binop));
      st[--sp - 1] = binop;
    }
  }
  if (sp != 1)
    warmc_invalid("type mismatch in %s: a constant expression must leave one value, not %u",
                  what, sp);
  if (!vt_subtype(m, st[0], want))
    warmc_invalid("type mismatch in %s: expected %s, found %s", what,
                  vt_name(want), vt_name(st[0]));
  if (peak > MAX_CONST_DEPTH)
    warmc_invalid("%s needs a stack of %u values, more than the %u the runtime "
                  "evaluates one on", what, peak, MAX_CONST_DEPTH);
  if (peak > m->max_const_depth) m->max_const_depth = peak;
  xfree(st);
}

static void validate_limits(uint64_t min, uint64_t max, bool has_max,
                            uint64_t ceiling, const char *what) {
  if (min > ceiling)
    warmc_invalid("%s minimum %llu is above the limit of %llu", what,
                  (unsigned long long)min, (unsigned long long)ceiling);
  if (has_max) {
    if (max > ceiling)
      warmc_invalid("%s maximum %llu is above the limit of %llu", what,
                    (unsigned long long)max, (unsigned long long)ceiling);
    if (max < min)
      warmc_invalid("size minimum must not be greater than maximum");
  }
}

static void declare_from_init(Module *m, const InitExpr *e) {
  for (uint32_t i = 0; i < e->n; i++)
    if (e->ins[i].op == IOP_REF_FUNC && e->ins[i].lo < m->nfuncs)
      g_declared[e->ins[i].lo] = true;
}

static void collect_declared(Module *m) {
  g_declared = xcalloc(m->nfuncs ? m->nfuncs : 1, sizeof(bool));
  for (uint32_t i = 0; i < m->nexports; i++)
    if (m->exports[i].kind == EK_FUNC && m->exports[i].index < m->nfuncs)
      g_declared[m->exports[i].index] = true;
  for (uint32_t i = 0; i < m->nelems; i++)
    for (uint32_t j = 0; j < m->elems[i].n; j++)
      declare_from_init(m, &m->elems[i].items[j]);
  for (uint32_t i = 0; i < m->nglobals; i++)
    if (!m->globals[i].imported) declare_from_init(m, &m->globals[i].init);
  for (uint32_t i = 0; i < m->ntables; i++)
    if (m->tables[i].has_init) declare_from_init(m, &m->tables[i].init);

}

static bool field_matches(const Module *m, const FieldType *sub,
                          const FieldType *sup) {
  if (sub->mutable_ != sup->mutable_) return false;
  if (sup->mutable_) return sub->type == sup->type;
  return vt_subtype(m, sub->type, sup->type);
}

static void check_declared_subtyping(Module *m) {
  for (uint32_t i = 0; i < m->ntypes; i++) {
    CompType *t = &m->types[i];
    if (!t->has_super) continue;
    CompType *s = &m->types[t->super];
    if (s->final_)
      warmc_invalid("type %u: supertype %u is final", i, t->super);
    if (t->kind != s->kind)
      warmc_invalid("type %u: does not match supertype %u", i, t->super);
    if (t->kind == CT_FUNC) {
      if (t->nparams != s->nparams || t->nresults != s->nresults)
        warmc_invalid("type %u: does not match supertype %u", i, t->super);
      for (uint32_t j = 0; j < t->nparams; j++)
        if (!vt_subtype(m, s->params[j], t->params[j]))
          warmc_invalid("type %u: parameter %u is not a supertype of %u's", i, j, t->super);
      for (uint32_t j = 0; j < t->nresults; j++)
        if (!vt_subtype(m, t->results[j], s->results[j]))
          warmc_invalid("type %u: result %u is not a subtype of %u's", i, j, t->super);
      continue;
    }

    if (t->nfields < s->nfields)
      warmc_invalid("type %u: does not match supertype %u", i, t->super);
    for (uint32_t j = 0; j < s->nfields; j++)
      if (!field_matches(m, &t->fields[j], &s->fields[j]))
        warmc_invalid("type %u: field %u does not match %u's", i, j, t->super);
  }
}

void wasm_validate(Module *m) {
  check_declared_subtyping(m);

  for (uint32_t i = 0; i < m->nfuncs; i++) {
    if (m->funcs[i].type_index >= m->ntypes)
      warmc_invalid("function %u: unknown type %u", i, m->funcs[i].type_index);

    if (m->types[m->funcs[i].type_index].kind != CT_FUNC)
      warmc_invalid("function %u: type %u is not a function type", i,
                    m->funcs[i].type_index);
  }

  for (uint32_t i = 0; i < m->ntags; i++) {
    if (m->tags[i].type_index >= m->ntypes)
      warmc_invalid("tag %u: unknown type %u", i, m->tags[i].type_index);
    if (m->types[m->tags[i].type_index].kind != CT_FUNC)
      warmc_invalid("tag %u: type %u is not a function type", i,
                    m->tags[i].type_index);
    if (m->types[m->tags[i].type_index].nresults != 0)
      warmc_invalid("tag %u: non-empty tag result type", i);
  }

  for (uint32_t i = 0; i < m->nmemories; i++)
    validate_limits(m->memories[i].min, m->memories[i].max,
                    m->memories[i].has_max,
                    m->memories[i].index64 ? (1ull << 48) : 65536ull, "memory");
  for (uint32_t i = 0; i < m->ntables; i++) {
    Table *t = &m->tables[i];
    if (!vt_is_ref(t->elem_type))
      warmc_invalid("table %u: element type is not a reference type", i);
    validate_limits(t->min, t->max, t->has_max,
                    t->index64 ? UINT64_MAX : 0xffffffffull, "table");

    if (t->has_init)
      expect_init(m, &t->init, t->elem_type, m->nimported_globals,
                  "table initialiser");
    else if (!t->imported && !vt_nullable(t->elem_type))
      warmc_invalid("table %u: type %s has no default value, so the table "
                    "needs an initialiser", i, vt_name(t->elem_type));
  }

  for (uint32_t i = 0; i < m->nglobals; i++) {
    if (m->globals[i].imported) continue;
    expect_init(m, &m->globals[i].init, m->globals[i].type, i, "global initialiser");
  }

  uint32_t ecap = 16;
  while (ecap < m->nexports * 2) ecap *= 2;
  uint32_t *ebucket = xmalloc(ecap * sizeof(uint32_t));
  for (uint32_t i = 0; i < ecap; i++) ebucket[i] = UINT32_MAX;
  for (uint32_t i = 0; i < m->nexports; i++) {
    Export *e = &m->exports[i];

    uint64_t h = 1469598103934665603ull;
    for (uint32_t p = 0; p < e->name_len; p++) {
      h ^= (unsigned char)e->name[p];
      h *= 1099511628211ull;
    }
    for (uint32_t k = (uint32_t)(h & (ecap - 1));; k = (k + 1) & (ecap - 1)) {
      if (ebucket[k] == UINT32_MAX) { ebucket[k] = i; break; }
      const Export *o = &m->exports[ebucket[k]];
      if (o->name_len == e->name_len &&
          !memcmp(o->name, e->name, e->name_len))
        warmc_invalid("duplicate export name \"%s\"", e->name);
    }
    switch (e->kind) {
      case EK_FUNC:   if (e->index >= m->nfuncs)    warmc_invalid("unknown function %u in export \"%s\"", e->index, e->name); break;
      case EK_TABLE:  if (e->index >= m->ntables)   warmc_invalid("unknown table %u in export \"%s\"", e->index, e->name); break;
      case EK_MEM:    if (e->index >= m->nmemories) warmc_invalid("unknown memory %u in export \"%s\"", e->index, e->name); break;
      case EK_GLOBAL: if (e->index >= m->nglobals)  warmc_invalid("unknown global %u in export \"%s\"", e->index, e->name); break;
      case EK_TAG:    if (e->index >= m->ntags)     warmc_invalid("unknown tag %u in export \"%s\"", e->index, e->name); break;
      default: warmc_invalid("unknown export kind %d", e->kind);
    }
  }
  xfree(ebucket);

  if (m->has_start) {
    if (m->start_func >= m->nfuncs) warmc_invalid("unknown function %u in start section", m->start_func);
    CompType *t = &m->types[m->funcs[m->start_func].type_index];
    if (t->nparams || t->nresults) warmc_invalid("start function must have type [] -> []");
  }

  for (uint32_t i = 0; i < m->nelems; i++) {
    ElemSeg *e = &m->elems[i];
    if (!vt_is_ref(e->elem_type)) warmc_invalid("element segment %u: not a reference type", i);
    if (e->mode == SEG_ACTIVE) {
      if (e->table_index >= m->ntables) warmc_invalid("unknown table %u in element segment %u", e->table_index, i);
      if (!vt_subtype(m, e->elem_type, m->tables[e->table_index].elem_type))
        warmc_invalid("type mismatch: element segment %u does not match table %u", i, e->table_index);
      expect_init(m, &e->offset,
                  m->tables[e->table_index].index64 ? VT_I64 : VT_I32,
                  m->nglobals, "element segment offset");
    }
    for (uint32_t j = 0; j < e->n; j++)
      expect_init(m, &e->items[j], e->elem_type, m->nglobals, "element segment item");
  }

  for (uint32_t i = 0; i < m->ndatas; i++) {
    DataSeg *d = &m->datas[i];
    if (d->mode == SEG_ACTIVE) {
      if (d->mem_index >= m->nmemories) warmc_invalid("unknown memory %u in data segment %u", d->mem_index, i);
      expect_init(m, &d->offset,
                  m->memories[d->mem_index].index64 ? VT_I64 : VT_I32,
                  m->nglobals, "data segment offset");
    }
  }

  if (m->has_datacount && m->datacount != m->ndatas)
    warmc_malformed("data count and data section have inconsistent lengths");

  collect_declared(m);

  for (uint32_t i = m->nimported_funcs; i < m->nfuncs; i++) {
    V v;
    memset(&v, 0, sizeof v);
    v.m = m;
    v.fn = &m->funcs[i];
    v.func_index = i;
    v.rd = rd_make(m->funcs[i].body, m->funcs[i].body_end, m->bin);

    v.init = xcalloc(v.fn->ntotal_locals ? v.fn->ntotal_locals : 1, sizeof(bool));
    for (uint32_t j = 0; j < v.fn->ntotal_locals; j++)
      v.init[j] = j < m->types[v.fn->type_index].nparams ||
                  vt_defaultable(v.fn->local_types[j]);
    validate_body(&v);
    xfree(v.stack);
    xfree(v.ctl);
    xfree(v.init);
    xfree(v.inits);
    for (uint32_t j = 0; j < v.nowned; j++) xfree(v.owned[j]);
    xfree(v.owned);
  }

  xfree(g_declared);
  g_declared = NULL;
}
