#include "warmc.h"
#include "reader.h"
#include "opcodes.h"
#include "sort.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

size_t wasm_utf8_prefix(const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n;) {
    uint8_t b = p[i];
    uint32_t need, cp, lo;
    if (b < 0x80) { i++; continue; }
    else if ((b & 0xe0) == 0xc0) { need = 1; cp = b & 0x1f; lo = 0x80; }
    else if ((b & 0xf0) == 0xe0) { need = 2; cp = b & 0x0f; lo = 0x800; }
    else if ((b & 0xf8) == 0xf0) { need = 3; cp = b & 0x07; lo = 0x10000; }
    else return i;
    if (i + need >= n) return i;
    for (uint32_t k = 1; k <= need; k++) {
      uint8_t c = p[i + k];
      if ((c & 0xc0) != 0x80) return i;
      cp = (cp << 6) | (c & 0x3f);
    }
    if (cp < lo || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return i;
    i += need + 1;
  }
  return n;
}

static void check_utf8(const uint8_t *p, uint32_t n) {
  if (wasm_utf8_prefix(p, n) != n) warmc_malformed("malformed UTF-8 encoding");
}

static char *rd_name(Rd *r, uint32_t *len) {
  uint32_t n = rd_u32(r);
  rd_check(r, n);
  check_utf8(r->p, n);
  char *s = xstrndup((const char *)r->p, n);
  r->p += n;
  if (len) *len = n;
  return s;
}

static uint32_t rd_count(Rd *r) {
  uint32_t n = rd_u32(r);
  if ((size_t)n > (size_t)(r->end - r->p))
    warmc_malformed("vector of %u elements does not fit in the section", n);
  return n;
}

static uint32_t g_type_limit;

static void check_heap_indices(ValType t) {
  if (vt_is_ref(t) && vt_concrete(t) && vt_type_index(t) >= g_type_limit)
    warmc_invalid("unknown type %u", vt_type_index(t));
}

static ValType rd_valtype_checked(Rd *r) {
  ValType t = rd_valtype(r);
  check_heap_indices(t);
  return t;
}

static ValType rd_reftype_checked(Rd *r) {
  ValType t = rd_reftype(r);
  check_heap_indices(t);
  return t;
}

static bool rd_mutability(Rd *r) {
  uint8_t b = rd_u8(r);
  if (b > 1) warmc_malformed("malformed mutability 0x%02x", b);
  return b != 0;
}

static void rd_limits(Rd *r, uint64_t *min, uint64_t *max, bool *has_max,
                      bool *index64) {
  uint8_t flags = rd_u8(r);
  if (flags != 0 && flags != 1 && flags != 3 && flags != 4 && flags != 5)
    warmc_malformed("unsupported limits flags 0x%02x", flags);
  if (flags & 2)
    warmc_invalid("a shared memory or table needs atomic operations, which "
                  "this compiler does not implement");
  *index64 = (flags & 4) != 0;
  *min = rd_u64(r);
  *has_max = (flags & 1) != 0;
  *max = *has_max ? rd_u64(r) : 0;
}

static void init_push(InitExpr *e, uint8_t op, uint64_t lo, uint64_t hi) {
  e->ins = xrealloc(e->ins, (e->n + 1) * sizeof(InitInsn));
  e->ins[e->n].op = op;
  e->ins[e->n].lo = lo;
  e->ins[e->n].hi = hi;
  e->n++;
}

static InitExpr rd_init(Rd *r) {
  InitExpr e;
  memset(&e, 0, sizeof e);
  for (;;) {
    uint8_t op = rd_u8(r);
    if (op == 0x0b) break;
    switch (op) {
      case 0x41: init_push(&e, IOP_I32_CONST, (uint64_t)(uint32_t)rd_i32(r), 0); break;
      case 0x42: init_push(&e, IOP_I64_CONST, (uint64_t)rd_i64(r), 0); break;
      case 0x43: init_push(&e, IOP_F32_CONST, rd_u32le(r), 0); break;
      case 0x44: init_push(&e, IOP_F64_CONST, rd_u64le(r), 0); break;
      case 0x23: init_push(&e, IOP_GLOBAL_GET, rd_u32(r), 0); break;

      case 0xd0: init_push(&e, IOP_REF_NULL, vt_ref(true, rd_heaptype(r)), 0); break;
      case 0xd2: init_push(&e, IOP_REF_FUNC, rd_u32(r), 0); break;
      case 0x6a: init_push(&e, IOP_I32_ADD, 0, 0); break;
      case 0x6b: init_push(&e, IOP_I32_SUB, 0, 0); break;
      case 0x6c: init_push(&e, IOP_I32_MUL, 0, 0); break;
      case 0x7c: init_push(&e, IOP_I64_ADD, 0, 0); break;
      case 0x7d: init_push(&e, IOP_I64_SUB, 0, 0); break;
      case 0x7e: init_push(&e, IOP_I64_MUL, 0, 0); break;

      case 0xfb: {
        uint32_t sub = rd_u32(r);
        switch (sub) {
          case 0:  init_push(&e, IOP_STRUCT_NEW, rd_u32(r), 0); break;
          case 1:  init_push(&e, IOP_STRUCT_NEW_DEFAULT, rd_u32(r), 0); break;
          case 6:  init_push(&e, IOP_ARRAY_NEW, rd_u32(r), 0); break;
          case 7:  init_push(&e, IOP_ARRAY_NEW_DEFAULT, rd_u32(r), 0); break;
          case 8: {
            uint32_t ti = rd_u32(r);
            init_push(&e, IOP_ARRAY_NEW_FIXED, ti, rd_u32(r));
            break;
          }
          case 26: init_push(&e, IOP_REF_CONVERT, 0, 0); break;
          case 27: init_push(&e, IOP_REF_CONVERT, 1, 0); break;
          case 28: init_push(&e, IOP_REF_I31, 0, 0); break;
          default: {
            uint32_t full = OP_PREFIXED(0xfb, sub > 0xffffu ? OP_SUB_UNKNOWN : sub);
            if (wasm_op_info(full)) {
              xfree(e.ins);
              warmc_invalid("constant expression required: %s", wasm_op_name(full));
            }
            xfree(e.ins);
            warmc_malformed("illegal opcode 0xfb %u in a constant expression", sub);
          }
        }
        break;
      }

      case 0xfd: {
        uint32_t sub = rd_u32(r);
        if (sub != 12) {

          uint32_t full = OP_PREFIXED(0xfd, sub > 0xffffu ? OP_SUB_UNKNOWN : sub);
          if (wasm_op_info(full)) {
            xfree(e.ins);
            warmc_invalid("constant expression required: %s", wasm_op_name(full));
          }
          xfree(e.ins);
          warmc_malformed("illegal opcode 0xfd %u in a constant expression", sub);
        }
        uint64_t lo = rd_u64le(r), hi = rd_u64le(r);
        init_push(&e, IOP_V128_CONST, lo, hi);
        break;
      }

      default:
        xfree(e.ins);
        if (wasm_op_info(op)) warmc_invalid("constant expression required: %s", wasm_op_name(op));
        warmc_malformed("illegal opcode 0x%02x in a constant expression", op);
    }
  }
  return e;
}

#define FNV_OFFSET 1469598103934665603ull
#define FNV_PRIME  1099511628211ull

static void mix(uint64_t *h, uint64_t v) {
  for (int i = 0; i < 8; i++) {
    *h ^= (v >> (8 * i)) & 0xff;
    *h *= FNV_PRIME;
  }
}

static void mix_type_ref(uint64_t *h, const Module *m, uint32_t idx,
                         uint32_t first, uint32_t n) {
  if (idx >= first && idx < first + n) { mix(h, 1); mix(h, idx - first); }
  else { mix(h, 2); mix(h, m->types[idx].id); }
}

static void mix_valtype(uint64_t *h, const Module *m, ValType v,
                        uint32_t first, uint32_t n) {
  if (!vt_is_ref(v) || !vt_concrete(v)) { mix(h, 0); mix(h, v); return; }
  mix(h, 3);
  mix(h, vt_nullable(v) ? 1 : 0);
  mix_type_ref(h, m, vt_type_index(v), first, n);
}

static void identify_group(Module *m, uint32_t first, uint32_t n) {
  uint64_t g = FNV_OFFSET;
  mix(&g, n);
  for (uint32_t k = 0; k < n; k++) {
    const CompType *t = &m->types[first + k];
    mix(&g, t->final_ ? 1 : 0);
    mix(&g, t->has_super ? 1 : 0);
    if (t->has_super) mix_type_ref(&g, m, t->super, first, n);
    mix(&g, t->kind);
    if (t->kind == CT_FUNC) {
      mix(&g, t->nparams);
      for (uint32_t j = 0; j < t->nparams; j++)
        mix_valtype(&g, m, t->params[j], first, n);
      mix(&g, t->nresults);
      for (uint32_t j = 0; j < t->nresults; j++)
        mix_valtype(&g, m, t->results[j], first, n);
    } else {

      mix(&g, t->nfields);
      for (uint32_t j = 0; j < t->nfields; j++) {
        mix_valtype(&g, m, t->fields[j].type, first, n);
        mix(&g, t->fields[j].mutable_ ? 1 : 0);
      }
    }
  }
  for (uint32_t k = 0; k < n; k++) {
    uint64_t id = g;
    mix(&id, k + 1);
    m->types[first + k].id = id;
  }
}

static bool id_same_ref(const Module *m, ValType x, ValType y) {
  if (x == y) return true;
  if (!vt_is_ref(x) || !vt_is_ref(y)) return false;
  if (vt_nullable(x) != vt_nullable(y)) return false;
  if (!vt_concrete(x) || !vt_concrete(y)) return false;
  return m->types[vt_type_index(x)].id == m->types[vt_type_index(y)].id;
}

static bool id_types_alike(const Module *m, const CompType *a,
                           const CompType *b) {
  if (a->kind != b->kind || a->final_ != b->final_ ||
      a->has_super != b->has_super || a->depth != b->depth)
    return false;
  if (a->has_super && m->types[a->super].id != m->types[b->super].id)
    return false;
  if (a->kind == CT_FUNC) {
    if (a->nparams != b->nparams || a->nresults != b->nresults) return false;
    for (uint32_t i = 0; i < a->nparams; i++)
      if (!id_same_ref(m, a->params[i], b->params[i])) return false;
    for (uint32_t i = 0; i < a->nresults; i++)
      if (!id_same_ref(m, a->results[i], b->results[i])) return false;
    return true;
  }
  if (a->nfields != b->nfields) return false;
  for (uint32_t i = 0; i < a->nfields; i++) {
    if (a->fields[i].mutable_ != b->fields[i].mutable_) return false;
    if (!id_same_ref(m, a->fields[i].type, b->fields[i].type)) return false;
  }
  return true;
}

static const Module *g_id_sort_m;

static int id_cmp(const void *pa, const void *pb) {
  uint64_t a = g_id_sort_m->types[*(const uint32_t *)pa].id;
  uint64_t b = g_id_sort_m->types[*(const uint32_t *)pb].id;
  return a < b ? -1 : a > b ? 1 : 0;
}

static void check_type_ids(Module *m) {
  if (m->ntypes < 2) return;
  uint32_t *ord = xmalloc(m->ntypes * sizeof(uint32_t));
  for (uint32_t i = 0; i < m->ntypes; i++) ord[i] = i;
  g_id_sort_m = m;
  qsort(ord, m->ntypes, sizeof(uint32_t), id_cmp);
  for (uint32_t i = 1; i < m->ntypes; i++) {
    uint32_t a = ord[i - 1], b = ord[i];
    if (m->types[a].id != m->types[b].id) continue;
    if (id_types_alike(m, &m->types[a], &m->types[b])) continue;
    xfree(ord);
    warmc_invalid("types %u and %u are different and have the same identity, "
                  "which this compiler derives by hashing their structure", a, b);
  }
  xfree(ord);
}

#define MAX_SUBTYPE_DEPTH 63
#define MAX_FUNC_LOCALS 50000u
#define MAX_FUNC_PARAMS 1000u
#define MAX_FUNC_RESULTS 1000u
#define MAX_MODULE_LOCALS 50000000ull

static void build_display(Module *m, uint32_t i) {
  CompType *t = &m->types[i];
  const CompType *s = t->has_super ? &m->types[t->super] : NULL;
  t->depth = s ? s->depth + 1 : 0;
  if (t->depth > MAX_SUBTYPE_DEPTH)
    warmc_invalid("type %u: subtyping depth %u exceeds the %u limit", i,
                  t->depth, MAX_SUBTYPE_DEPTH);
  t->display = xcalloc(t->depth + 1, sizeof(uint64_t));
  if (s) memcpy(t->display, s->display, (t->depth) * sizeof(uint64_t));
  t->display[t->depth] = t->id;
}

typedef enum { HH_FUNC, HH_EXTERN, HH_ANY, HH_EXN } HeapHier;

static HeapHier heap_hier(const Module *m, HeapType h) {
  if (h >= 0)
    return m->types[h].kind == CT_FUNC ? HH_FUNC : HH_ANY;
  switch (h) {
    case HT_EXTERN: case HT_NOEXTERN: return HH_EXTERN;
    case HT_EXN: case HT_NOEXN: return HH_EXN;
    case HT_FUNC: case HT_NOFUNC: return HH_FUNC;
    default: return HH_ANY;
  }
}

static bool heap_is_bottom(HeapType h) {
  return h == HT_NOFUNC || h == HT_NOEXTERN || h == HT_NONE || h == HT_NOEXN;
}

static int any_rank(HeapType h) {
  switch (h) {
    case HT_ANY: return 0;
    case HT_EQ: return 1;
    case HT_I31: case HT_STRUCT: case HT_ARRAY: return 2;
    default: return 3;
  }
}

static HeapType concrete_top(const Module *m, HeapType h) {
  switch (m->types[h].kind) {
    case CT_STRUCT: return HT_STRUCT;
    case CT_ARRAY:  return HT_ARRAY;
    default:        return HT_FUNC;
  }
}

bool heap_subtype(const Module *m, HeapType a, HeapType b) {
  if (a == b) return true;
  if (heap_hier(m, a) != heap_hier(m, b)) return false;
  if (heap_is_bottom(a)) return true;
  if (heap_is_bottom(b)) return false;
  if (a >= 0 && b >= 0) {

    const CompType *ta = &m->types[a], *tb = &m->types[b];
    return tb->depth <= ta->depth && ta->display[tb->depth] == tb->id;
  }
  if (a >= 0) {

    HeapType top = concrete_top(m, a);
    return b == top || (heap_hier(m, b) == HH_ANY && any_rank(b) < any_rank(top));
  }
  if (b >= 0) return false;

  return heap_hier(m, a) == HH_ANY && any_rank(a) > any_rank(b);
}

bool vt_subtype(const Module *m, ValType a, ValType b) {
  if (a == b) return true;
  if (a == VT_BOT || b == VT_BOT) return true;
  if (!vt_is_ref(a) || !vt_is_ref(b)) return false;

  if (vt_nullable(a) && !vt_nullable(b)) return false;
  return heap_subtype(m, vt_heap(a), vt_heap(b));
}

ValType vt_join(const Module *m, ValType a, ValType b) {
  if (vt_subtype(m, a, b)) return b;
  if (vt_subtype(m, b, a)) return a;
  if (!vt_is_ref(a) || !vt_is_ref(b)) return VT_BOT;
  if (heap_hier(m, vt_heap(a)) != heap_hier(m, vt_heap(b))) return VT_BOT;

  bool nul = vt_nullable(a) || vt_nullable(b);
  switch (heap_hier(m, vt_heap(a))) {
    case HH_FUNC: return vt_ref(nul, HT_FUNC);
    case HH_EXTERN: return vt_ref(nul, HT_EXTERN);
    case HH_EXN: return vt_ref(nul, HT_EXN);
    default: return vt_ref(nul, HT_ANY);
  }
}

static void parse_fieldtype(Rd *r, FieldType *f) {
  const uint8_t *save = r->p;
  uint8_t b = rd_u8(r);
  if (b == VT_I8 || b == VT_I16) f->type = b;
  else { r->p = save; f->type = rd_valtype_checked(r); }
  f->mutable_ = rd_mutability(r);
}

static void layout_struct(Module *m, CompType *t) {
  uint64_t off = GC_HDR_BYTES;
  uint32_t align = 1;
  for (uint32_t i = 0; i < t->nfields; i++) {
    uint32_t w = vt_width(t->fields[i].type);
    off = (off + w - 1) & ~(uint64_t)(w - 1);
    if (off - GC_HDR_BYTES > STRUCT_MAX_BYTES)
      warmc_fatal("type %u: struct payload exceeds %u bytes",
                  (uint32_t)(t - m->types), STRUCT_MAX_BYTES);
    t->fields[i].offset = (uint32_t)(off - GC_HDR_BYTES);
    off += w;
    if (w > align) align = w;
  }
  off = (off + align - 1) & ~(uint64_t)(align - 1);
  if (off - GC_HDR_BYTES > STRUCT_MAX_BYTES)
    warmc_fatal("type %u: struct payload exceeds %u bytes",
                (uint32_t)(t - m->types), STRUCT_MAX_BYTES);
  t->body_size = (uint32_t)(off - GC_HDR_BYTES);

  t->hdr_size = GC_HDR_BYTES;
}

static void parse_comptype(Module *m, Rd *r, CompType *t) {
  uint8_t form = rd_u8(r);
  if (form == 0x5f) {
    t->kind = CT_STRUCT;
    t->nfields = rd_count(r);
    t->fields = xcalloc(t->nfields ? t->nfields : 1, sizeof(FieldType));
    for (uint32_t i = 0; i < t->nfields; i++) parse_fieldtype(r, &t->fields[i]);
    layout_struct(m, t);
    return;
  }
  if (form == 0x5e) {

    t->kind = CT_ARRAY;
    t->nfields = 1;
    t->fields = xcalloc(1, sizeof(FieldType));
    parse_fieldtype(r, &t->fields[0]);

    t->hdr_size = GC_HDR_FOR(vt_width(t->fields[0].type));
    return;
  }
  if (form != 0x60)
    warmc_malformed("type %u: expected a composite type, got 0x%02x",
                    (uint32_t)(t - m->types), form);
  t->kind = CT_FUNC;
  t->nparams = rd_count(r);
  if (t->nparams > MAX_FUNC_PARAMS)
    warmc_malformed("type %u: %u parameters is above the limit of %u",
                    (uint32_t)(t - m->types), t->nparams, MAX_FUNC_PARAMS);
  t->params = xcalloc(t->nparams ? t->nparams : 1, sizeof(ValType));
  for (uint32_t j = 0; j < t->nparams; j++) t->params[j] = rd_valtype_checked(r);
  t->nresults = rd_count(r);
  if (t->nresults > MAX_FUNC_RESULTS)
    warmc_malformed("type %u: %u results is above the limit of %u",
                    (uint32_t)(t - m->types), t->nresults, MAX_FUNC_RESULTS);
  t->results = xcalloc(t->nresults ? t->nresults : 1, sizeof(ValType));
  for (uint32_t j = 0; j < t->nresults; j++) t->results[j] = rd_valtype_checked(r);
}

static void parse_subtype(Module *m, Rd *r, uint32_t i) {
  CompType *t = &m->types[i];
  const uint8_t *save = r->p;
  uint8_t b = rd_u8(r);
  t->final_ = true;
  if (b == 0x50 || b == 0x4f) {
    t->final_ = (b == 0x4f);
    uint32_t nsup = rd_count(r);
    if (nsup > 1) warmc_invalid("type %u: at most one supertype", i);
    if (nsup == 1) {
      uint32_t s = rd_u32(r);

      if (s >= i) warmc_invalid("type %u: unknown supertype %u", i, s);
      t->has_super = true;
      t->super = s;
    }
  } else {
    r->p = save;
  }
  parse_comptype(m, r, t);
}

static void parse_types(Module *m, Rd *r) {

  uint32_t ngroups = rd_count(r);
  uint32_t cap = 0;
  for (uint32_t g = 0; g < ngroups; g++) {
    const uint8_t *save = r->p;
    uint8_t b = rd_u8(r);
    uint32_t n = 1;
    if (b == 0x4e) n = rd_count(r);
    else r->p = save;

    uint32_t first = m->ntypes;
    if ((uint64_t)first + n > (uint64_t)VT_HEAP_MAX + 1u)
      warmc_malformed("too many types");
    if (first + n > cap) {
      cap = first + n > cap * 2 ? first + n : cap * 2;
      m->types = xrealloc(m->types, cap * sizeof(CompType));
    }
    memset(&m->types[first], 0, n * sizeof(CompType));

    g_type_limit = first + n;
    for (uint32_t k = 0; k < n; k++) {
      m->types[first + k].rec_first = first;
      m->types[first + k].rec_n = n;
      parse_subtype(m, r, first + k);
    }
    m->ntypes = first + n;
    identify_group(m, first, n);
    for (uint32_t k = 0; k < n; k++) build_display(m, first + k);
  }
  if (!m->types) m->types = xcalloc(1, sizeof(CompType));
  g_type_limit = m->ntypes;
  check_type_ids(m);
}

#define PUSH(arr, count, type)                                                 \
  ((m->arr = xrealloc(m->arr, (m->count + 1) * sizeof(type))),                 \
   memset(&m->arr[m->count], 0, sizeof(type)), &m->arr[m->count++])

static uint32_t rd_tag_attribute_and_type(Rd *r) {
  uint8_t attr = rd_u8(r);
  if (attr != 0) warmc_malformed("unsupported tag attribute 0x%02x", attr);
  return rd_u32(r);
}

static void parse_tags(Module *m, Rd *r) {
  uint32_t n = rd_count(r);
  for (uint32_t i = 0; i < n; i++) {
    uint32_t ti = rd_tag_attribute_and_type(r);
    Tag *t = PUSH(tags, ntags, Tag);
    t->type_index = ti;
  }
}

static void parse_import_desc(Module *m, Rd *r, Import *im, uint32_t index) {
  switch (im->kind) {
    case EK_FUNC: {
      im->type_index = rd_u32(r);
      Func *f = PUSH(funcs, nfuncs, Func);
      f->type_index = im->type_index;
      f->imported = true;
      f->import_index = index;
      m->nimported_funcs++;
      break;
    }
    case EK_TABLE: {
      ValType et = rd_reftype_checked(r);
      Table *t = PUSH(tables, ntables, Table);
      t->elem_type = et;
      t->imported = true;
      rd_limits(r, &t->min, &t->max, &t->has_max, &t->index64);
      break;
    }
    case EK_MEM: {
      Memory *mem = PUSH(memories, nmemories, Memory);
      mem->imported = true;
      rd_limits(r, &mem->min, &mem->max, &mem->has_max, &mem->index64);
      break;
    }
    case EK_GLOBAL: {
      ValType vt = rd_valtype_checked(r);
      bool mut = rd_mutability(r);
      Global *g = PUSH(globals, nglobals, Global);
      g->type = vt;
      g->mutable_ = mut;
      g->imported = true;
      m->nimported_globals++;
      break;
    }
    case EK_TAG: {
      uint32_t ti = rd_tag_attribute_and_type(r);
      im->type_index = ti;
      Tag *t = PUSH(tags, ntags, Tag);
      t->type_index = ti;
      t->imported = true;
      m->nimported_tags++;
      break;
    }
    default: warmc_malformed("unknown import kind %d", im->kind);
  }
}

static void parse_imports(Module *m, Rd *r) {
  uint32_t nentries = rd_count(r);
  m->nimports = 0;
  for (uint32_t e = 0; e < nentries; e++) {
    uint32_t module_len;
    char *module = rd_name(r, &module_len);
    uint32_t name_len;
    char *name = rd_name(r, &name_len);
    uint8_t kind = rd_u8(r);

    /* Compact import section (binaryen --enable-compact-imports): an entry
       with an empty field name and kind byte 0x7f is a group -- one module
       name shared by a vector of imports written without theirs. */
    if (kind == 0x7f) {
      if (name_len != 0)
        warmc_malformed("compact import group with a non-empty field name");
      uint32_t gn = rd_count(r);
      for (uint32_t k = 0; k < gn; k++) {
        Import *im = PUSH(imports, nimports, Import);
        im->module = module;
        im->module_len = module_len;
        im->name = rd_name(r, &im->name_len);
        im->kind = (ExternKind)rd_u8(r);
        parse_import_desc(m, r, im, m->nimports - 1);
      }
      continue;
    }
    Import *im = PUSH(imports, nimports, Import);
    im->module = module;
    im->module_len = module_len;
    im->name = name;
    im->name_len = name_len;
    im->kind = (ExternKind)kind;
    parse_import_desc(m, r, im, m->nimports - 1);
  }
}

static void parse_functions(Module *m, Rd *r) {
  uint32_t n = rd_count(r);
  for (uint32_t i = 0; i < n; i++) {
    uint32_t ti = rd_u32(r);
    Func *f = PUSH(funcs, nfuncs, Func);
    f->type_index = ti;
  }
}

static void parse_tables(Module *m, Rd *r) {
  uint32_t n = rd_count(r);
  for (uint32_t i = 0; i < n; i++) {

    const uint8_t *save = r->p;
    bool has_init = rd_u8(r) == 0x40;
    if (has_init) {
      uint8_t reserved = rd_u8(r);
      if (reserved != 0)
        warmc_malformed("table %u: expected 0x00 after 0x40, got 0x%02x", i, reserved);
    } else {
      r->p = save;
    }
    ValType et = rd_reftype_checked(r);
    Table *t = PUSH(tables, ntables, Table);
    t->elem_type = et;
    rd_limits(r, &t->min, &t->max, &t->has_max, &t->index64);
    t->has_init = has_init;
    if (has_init) t->init = rd_init(r);
  }
}

static void parse_memories(Module *m, Rd *r) {
  uint32_t n = rd_count(r);
  for (uint32_t i = 0; i < n; i++) {
    Memory *mem = PUSH(memories, nmemories, Memory);
    rd_limits(r, &mem->min, &mem->max, &mem->has_max, &mem->index64);
  }
}

static void parse_globals(Module *m, Rd *r) {
  uint32_t n = rd_count(r);
  for (uint32_t i = 0; i < n; i++) {
    ValType vt = rd_valtype_checked(r);
    bool mut = rd_mutability(r);
    InitExpr init = rd_init(r);
    Global *g = PUSH(globals, nglobals, Global);
    g->type = vt;
    g->mutable_ = mut;
    g->init = init;
  }
}

static void parse_exports(Module *m, Rd *r) {
  m->nexports = rd_count(r);
  m->exports = xcalloc(m->nexports ? m->nexports : 1, sizeof(Export));
  for (uint32_t i = 0; i < m->nexports; i++) {
    m->exports[i].name = rd_name(r, &m->exports[i].name_len);
    m->exports[i].kind = (ExternKind)rd_u8(r);
    m->exports[i].index = rd_u32(r);
  }
}

static void parse_elems(Module *m, Rd *r) {
  m->nelems = rd_count(r);
  m->elems = xcalloc(m->nelems ? m->nelems : 1, sizeof(ElemSeg));
  for (uint32_t i = 0; i < m->nelems; i++) {
    ElemSeg *e = &m->elems[i];
    uint32_t flags = rd_u32(r);

    if (flags > 7) warmc_malformed("unsupported element segment flags %u", flags);
    e->table_index = 0;

    bool passive = (flags & 1) != 0;
    bool explicit_table_or_decl = (flags & 2) != 0;
    bool exprs = (flags & 4) != 0;

    e->elem_type = exprs ? VT_FUNCREF : vt_ref(false, HT_FUNC);

    if (!passive) {
      e->mode = SEG_ACTIVE;
      if (explicit_table_or_decl) e->table_index = rd_u32(r);
      e->offset = rd_init(r);
    } else {
      e->mode = explicit_table_or_decl ? SEG_DECLARATIVE : SEG_PASSIVE;
    }

    if (flags != 0 && flags != 4) {
      if (exprs) e->elem_type = rd_reftype_checked(r);
      else { uint8_t k = rd_u8(r); if (k != 0) warmc_malformed("unsupported elemkind %u", k); }
    }
    e->n = rd_count(r);
    e->items = xcalloc(e->n ? e->n : 1, sizeof(InitExpr));
    for (uint32_t j = 0; j < e->n; j++) {
      if (exprs) e->items[j] = rd_init(r);
      else init_push(&e->items[j], IOP_REF_FUNC, rd_u32(r), 0);
    }
  }
}

static void parse_datas(Module *m, Rd *r) {
  m->ndatas = rd_count(r);
  m->datas = xcalloc(m->ndatas ? m->ndatas : 1, sizeof(DataSeg));
  for (uint32_t i = 0; i < m->ndatas; i++) {
    DataSeg *d = &m->datas[i];
    uint32_t flags = rd_u32(r);
    switch (flags) {
      case 0: d->mode = SEG_ACTIVE; d->mem_index = 0; d->offset = rd_init(r); break;
      case 1: d->mode = SEG_PASSIVE; break;
      case 2: d->mode = SEG_ACTIVE; d->mem_index = rd_u32(r); d->offset = rd_init(r); break;
      default: warmc_malformed("unsupported data segment flags %u", flags);
    }
    d->len = rd_u32(r);
    rd_check(r, d->len);
    d->data = r->p;
    r->p += d->len;
  }
}

static void parse_code(Module *m, Rd *r) {
  uint32_t n = rd_count(r);
  uint32_t fi = m->nimported_funcs;
  if (n != m->nfuncs - m->nimported_funcs)
    warmc_malformed("function and code section have inconsistent lengths");
  for (uint32_t i = 0; i < n; i++, fi++) {
    uint32_t size = rd_u32(r);
    rd_check(r, size);
    const uint8_t *body_start = r->p;
    const uint8_t *body_end = r->p + size;
    Rd br = { body_start, body_end, r->start };

    Func *f = &m->funcs[fi];

    if (f->type_index >= m->ntypes)
      warmc_invalid("unknown type %u in function %u", f->type_index, fi);
    CompType *ft = &m->types[f->type_index];

    if (ft->kind != CT_FUNC)
      warmc_invalid("function %u: type %u is not a function type", fi,
                    f->type_index);

    uint32_t ngroups = rd_count(&br);

    uint64_t nlocals = 0;

    const uint8_t *after_count = br.p;
    for (uint32_t g = 0; g < ngroups; g++) {
      uint32_t cnt = rd_u32(&br);
      rd_valtype_checked(&br);
      nlocals += cnt;
      if (nlocals > MAX_FUNC_LOCALS) warmc_malformed("too many locals");
    }
    m->total_locals += nlocals;
    if (m->total_locals > MAX_MODULE_LOCALS)
      warmc_malformed("too many locals in the module");
    f->nlocals = (uint32_t)nlocals;
    f->ntotal_locals = ft->nparams + (uint32_t)nlocals;
    f->local_types = xcalloc(f->ntotal_locals ? f->ntotal_locals : 1, sizeof(ValType));
    for (uint32_t j = 0; j < ft->nparams; j++) f->local_types[j] = ft->params[j];

    br.p = after_count;
    uint32_t at = ft->nparams;
    for (uint32_t g = 0; g < ngroups; g++) {
      uint32_t cnt = rd_u32(&br);
      ValType vt = rd_valtype_checked(&br);
      for (uint32_t k = 0; k < cnt; k++) f->local_types[at++] = vt;
    }
    f->body = br.p;
    f->body_end = body_end;
    r->p = body_end;
  }
}

Module *wasm_parse(const uint8_t *bin, size_t len) {
  Module *m = xcalloc(1, sizeof(Module));
  m->bin = bin;
  m->bin_len = len;

  g_type_limit = 0;

  Rd r = { bin, bin + len, bin };
  if (len < 8) warmc_malformed("file too short to be a wasm module");
  uint32_t magic = rd_u32le(&r);
  uint32_t version = rd_u32le(&r);
  if (magic != 0x6d736100u) warmc_malformed("magic header not detected: 0x%08x", magic);
  if (version != 1) warmc_malformed("unknown binary version %u", version);

  int last_id = 0;
  while (r.p < r.end) {
    uint8_t id = rd_u8(&r);
    uint32_t size = rd_u32(&r);
    rd_check(&r, size);
    Rd sec = { r.p, r.p + size, bin };
    r.p += size;

    if (id != 0) {

      int rank = (id == 12) ? 95 : (id == 13) ? 55 : id * 10;
      if (rank <= last_id) warmc_malformed("unexpected content after last section: id %u out of order", id);
      last_id = rank;
    }
    switch (id) {
      case 0: {
        uint32_t nl;
        char *nm = rd_name(&sec, &nl);
        const uint8_t *at = sec.p - nl;
        if (nl == 10 && !memcmp(nm, "go:buildid", 10)) m->go_stack = true;
        xfree(nm);
        m->customs = xrealloc(m->customs, (m->ncustoms + 1) * sizeof(CustomSec));
        m->customs[m->ncustoms++] = (CustomSec){ at, sec.p, nl, (uint32_t)(sec.end - sec.p) };
        break;
      }
      case 1: parse_types(m, &sec); break;
      case 2: parse_imports(m, &sec); break;
      case 3: parse_functions(m, &sec); break;
      case 4: parse_tables(m, &sec); break;
      case 5: parse_memories(m, &sec); break;
      case 6: parse_globals(m, &sec); break;
      case 7: parse_exports(m, &sec); break;
      case 8: m->has_start = true; m->start_func = rd_u32(&sec); break;
      case 9: parse_elems(m, &sec); break;
      case 10: parse_code(m, &sec); break;
      case 11: parse_datas(m, &sec); break;
      case 12: m->has_datacount = true; m->datacount = rd_u32(&sec); break;
      case 13: parse_tags(m, &sec); break;
      default: warmc_malformed("malformed section id %u", id);
    }
    if (id != 0 && sec.p != sec.end)
      warmc_malformed("section size mismatch: section %u has %zd trailing bytes", id, sec.end - sec.p);
  }

  for (uint32_t i = 0; i < m->nfuncs; i++) {
    if (m->funcs[i].type_index >= m->ntypes)
      warmc_invalid("unknown type %u in function %u", m->funcs[i].type_index, i);
    char name[64];
    snprintf(name, sizeof name, "wasm_func_%u", i);
    m->funcs[i].sym = xstrndup(name, strlen(name));
  }
  for (uint32_t i = 0; i < m->nfuncs; i++) {
    if (!m->funcs[i].imported && !m->funcs[i].body)
      warmc_malformed("function and code section have inconsistent lengths");
  }
  return m;
}

void wasm_check_utf8(const uint8_t *p, uint32_t n) { check_utf8(p, n); }

void wasm_skip_core_externtype(Rd *r) {
  uint8_t k = rd_u8(r);
  uint64_t min, max;
  bool has_max, index64;
  switch (k) {
    case 0x00: rd_u32(r); break;
    case 0x01:
      rd_valtype_checked(r);
      rd_limits(r, &min, &max, &has_max, &index64);
      break;
    case 0x02: rd_limits(r, &min, &max, &has_max, &index64); break;
    case 0x03: rd_valtype_checked(r); rd_u8(r); break;
    case 0x04: rd_tag_attribute_and_type(r); break;
    default: warmc_malformed("malformed core extern type 0x%02x", k);
  }
}

void wasm_skip_core_rectype(Rd *r) {
  Module *scratch = xcalloc(1, sizeof(Module));
  const uint8_t *save = r->p;
  uint8_t b = rd_u8(r);
  uint32_t n = 1;
  if (b == 0x4e) n = rd_count(r);
  else r->p = save;
  scratch->types = xcalloc(n ? n : 1, sizeof(CompType));
  uint32_t saved_limit = g_type_limit;
  g_type_limit = n;
  for (uint32_t k = 0; k < n; k++) {
    scratch->types[k].rec_first = 0;
    scratch->types[k].rec_n = n;
    parse_subtype(scratch, r, k);
  }
  scratch->ntypes = n;
  g_type_limit = saved_limit;
  module_free(scratch);
}

void module_free(Module *m) {
  if (!m) return;
  xfree(m->customs);
  for (uint32_t i = 0; i < m->ntypes; i++) {
    xfree(m->types[i].params); xfree(m->types[i].results); xfree(m->types[i].display);
    xfree(m->types[i].fields);
  }
  xfree(m->types);
  for (uint32_t i = 0; i < m->nimports; i++) { xfree(m->imports[i].module); xfree(m->imports[i].name); }
  xfree(m->imports);
  for (uint32_t i = 0; i < m->nfuncs; i++) { xfree(m->funcs[i].local_types); xfree(m->funcs[i].sym); }
  xfree(m->funcs);
  for (uint32_t i = 0; i < m->ntables; i++) xfree(m->tables[i].init.ins);
  xfree(m->tables);
  xfree(m->memories);
  for (uint32_t i = 0; i < m->nglobals; i++) xfree(m->globals[i].init.ins);
  xfree(m->globals);
  xfree(m->tags);
  for (uint32_t i = 0; i < m->nexports; i++) xfree(m->exports[i].name);
  xfree(m->exports);
  for (uint32_t i = 0; i < m->nelems; i++) {
    for (uint32_t j = 0; j < m->elems[i].n; j++) xfree(m->elems[i].items[j].ins);
    xfree(m->elems[i].offset.ins);
    xfree(m->elems[i].items);
  }
  xfree(m->elems);
  for (uint32_t i = 0; i < m->ndatas; i++) xfree(m->datas[i].offset.ins);
  xfree(m->datas);
  if (m->t0) { xfree(m->t0->entry); xfree(m->t0->sites); xfree(m->t0); }
  xfree(m->ict); xfree(m->ict_of);
  for (uint32_t i = 0; i < m->nowned_bodies; i++) xfree(m->owned_bodies[i]);
  xfree(m->owned_bodies);
  xfree(m);
}

typedef struct { uint64_t id; uint32_t i; } TypeById;
#define TYPE_BY_ID_LESS(x, y) ((x)->id < (y)->id || ((x)->id == (y)->id && (x)->i < (y)->i))
WARMC_SORT(sort_types_by_id, TypeById, TYPE_BY_ID_LESS)

/* Per type index, the first type index of the same identity: a call
   through either reaches the same functions. */
uint32_t *module_type_canon(const Module *m) {
  uint32_t n = m->ntypes;
  uint32_t *canon = xmalloc((n ? n : 1) * sizeof(uint32_t));
  TypeById *by = xmalloc((n ? n : 1) * sizeof(TypeById));
  for (uint32_t i = 0; i < n; i++) by[i] = (TypeById){ m->types[i].id, i };
  sort_types_by_id(by, n);
  for (uint32_t k = 0; k < n; k++)
    canon[by[k].i] = k && by[k - 1].id == by[k].id ? canon[by[k - 1].i] : by[k].i;
  xfree(by);
  return canon;
}

/* A final function type with an identity of its own, appended as the
   parser appends one: for a transform that gives functions a signature the
   module did not have (tramp.c, gosp.c).  It shares a rec group with empty
   function types, so no type the module wrote by the same structure is the
   same type -- a call through one of those still traps on these functions
   -- while a call through it checks the type by its identity alone, as a
   final type's is checked.  One empty type does unless the module wrote
   that very group, and then the group takes another until it is new. */
uint32_t module_add_distinct_func_type(Module *m, const ValType *params, uint32_t nparams,
                                       const ValType *results, uint32_t nresults) {
  uint32_t i = m->ntypes;
  for (uint32_t n = 2;; n++) {
    m->types = xrealloc(m->types, (i + n) * sizeof(CompType));
    memset(&m->types[i], 0, n * sizeof(CompType));
    for (uint32_t k = 0; k < n; k++) {
      CompType *t = &m->types[i + k];
      t->kind = CT_FUNC;
      t->final_ = true;
      t->rec_first = i;
      t->rec_n = n;
      t->params = xmalloc((k ? 1 : nparams ? nparams : 1) * sizeof(ValType));
      t->results = xmalloc((k ? 1 : nresults ? nresults : 1) * sizeof(ValType));
    }
    CompType *t = &m->types[i];
    memcpy(t->params, params, nparams * sizeof(ValType));
    t->nparams = nparams;
    memcpy(t->results, results, nresults * sizeof(ValType));
    t->nresults = nresults;
    m->ntypes = i + n;
    identify_group(m, i, n);
    for (uint32_t k = 0; k < n; k++) build_display(m, i + k);
    /* the identity is a hash of the structure (check_type_ids): a type of
       the module with one of the group's is a group the module wrote */
    bool taken = false;
    for (uint32_t u = 0; u < i && !taken; u++)
      for (uint32_t k = 0; k < n && !taken; k++)
        if (m->types[u].id == m->types[i + k].id) taken = true;
    if (!taken) return i;
    for (uint32_t k = 0; k < n; k++) {
      xfree(m->types[i + k].params);
      xfree(m->types[i + k].results);
      xfree(m->types[i + k].display);
    }
    m->ntypes = i;
  }
}
