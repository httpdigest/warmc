#include "warmc.h"
#include "clink.h"
#include "target.h"
#include "opcodes.h"
#include "reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t module_max_outgoing(Module *m) {
  uint32_t worst = 0;
  int int_cap = g_target->narg_int - 1;   /* one int slot carries vmctx */
  int fp_cap = g_target->narg_fp;
  /* Mirrors the backends' stack-argument walk: one arg_slot per scalar, an
     aligned 16 for a v128.  With a64's 16-byte slot that is just
     nstack * 16, the shape this had before x64 needed a narrower one. */
  for (uint32_t i = 0; i < m->ntypes; i++) {
    CompType *t = &m->types[i];
    int ints = 0, flts = 0;
    uint32_t bytes = 0;
    for (uint32_t j = 0; j < t->nparams; j++) {
      bool vec = t->params[j] == VT_V128;
      if (vt_is_vec_reg(t->params[j])) { if (flts < fp_cap) { flts++; continue; } }
      else { if (ints < int_cap) { ints++; continue; } }
      if (vec) bytes = ((bytes + 15) & ~15u) + 16;
      else bytes += g_target->arg_slot;
    }
    if (bytes > worst) worst = bytes;
  }
  return (worst + 15) & ~15u;
}

static bool module_tail_calls(Module *m) {
  for (uint32_t i = m->nimported_funcs; i < m->nfuncs; i++) {
    const Func *fn = &m->funcs[i];
    if (!fn->body) continue;
    Rd r = rd_make(fn->body, fn->body_end, m->bin);
    while (!rd_eof(&r)) {
      uint32_t op = wasm_read_op(&r);
      if (op == 0x12 || op == 0x13 || op == 0x15) return true;
      if (!wasm_skip_imm(&r, op)) break;
    }
  }
  return false;
}

static FuncUnwind *g_unwind;
static uint32_t g_nunwind, g_cap_unwind;

void codegen_reset_unwind(void) {
  for (uint32_t i = 0; i < g_nunwind; i++) {
    xfree(g_unwind[i].sites);
    xfree(g_unwind[i].clauses);
    xfree(g_unwind[i].gcsites);
    xfree(g_unwind[i].gcroots);
  }
  xfree(g_unwind);
  g_unwind = NULL;
  g_nunwind = g_cap_unwind = 0;
}

void codegen_note_unwind(const FuncUnwind *u) {
  if (g_nunwind == g_cap_unwind) {
    g_cap_unwind = g_cap_unwind ? g_cap_unwind * 2 : 32;
    g_unwind = xrealloc(g_unwind, g_cap_unwind * sizeof(FuncUnwind));
  }
  FuncUnwind *d = &g_unwind[g_nunwind++];
  *d = *u;

  if (u->nsites) {
    d->sites = xmalloc(u->nsites * sizeof(UwSite));
    memcpy(d->sites, u->sites, u->nsites * sizeof(UwSite));
  } else d->sites = NULL;
  if (u->nclauses) {
    d->clauses = xmalloc(u->nclauses * sizeof(UwClause));
    memcpy(d->clauses, u->clauses, u->nclauses * sizeof(UwClause));
  } else d->clauses = NULL;
  if (u->ngcsites) {
    d->gcsites = xmalloc(u->ngcsites * sizeof(GcSite));
    memcpy(d->gcsites, u->gcsites, u->ngcsites * sizeof(GcSite));
  } else d->gcsites = NULL;
  if (u->ngcroots) {
    d->gcroots = xmalloc(u->ngcroots * sizeof(uint32_t));
    memcpy(d->gcroots, u->gcroots, u->ngcroots * sizeof(uint32_t));
  } else d->gcroots = NULL;
}

void codegen_prepare_module(Module *m, ObjBuilder *o) {
  (void)o;
  codegen_reset_unwind();
  codegen_reset_merge();
  if (module_max_outgoing(m) > INVOKE_OUTGOING_BYTES && module_tail_calls(m))
    warmc_fatal("a signature needs %u bytes of stack arguments, more than the "
                "%u a tail call out of an exported function has room for",
                module_max_outgoing(m), INVOKE_OUTGOING_BYTES);

  for (uint32_t i = 0; i < m->nexports; i++) {
    const Export *e = &m->exports[i];
    if (e->kind != EK_FUNC || e->index >= m->nfuncs) continue;
    const CompType *sig = &m->types[m->funcs[e->index].type_index];
    if (sig->nresults > INVOKE_MAX_RESULTS)
      warmc_fatal("export \"%.*s\" returns %u values, more than the %u the "
                  "invoke ABI carries back",
                  (int)e->name_len, e->name, sig->nresults,
                  INVOKE_MAX_RESULTS);
  }

}

void codegen_prewarm_symbols(Module *m, ObjBuilder *o) {
  static const char *const helpers[] = {
    "warmc_array_copy",
    "warmc_array_fill",
    "warmc_array_init_data",
    "warmc_array_init_elem",
    "warmc_array_new",
    "warmc_array_new_data",
    "warmc_array_new_elem",
    "warmc_array_new_fill",
    "warmc_data_drop",
    "warmc_elem_drop",
    "warmc_gc_load_stub",
    "warmc_memory_copy",
    "warmc_memory_fill",
    "warmc_memory_grow",
    "warmc_memory_init",
    "warmc_ref_test",
    "warmc_struct_new",
    "warmc_table_copy",
    "warmc_table_fill",
    "warmc_table_get",
    "warmc_table_grow",
    "warmc_table_init",
    "warmc_table_set",
    "warmc_table_size",
    "warmc_throw",
    "warmc_throw_ref",
    "warmc_trap",
    "warmc_unreached",
  };
  for (size_t i = 0; i < sizeof helpers / sizeof helpers[0]; i++)
    obj_intern_sym(o, helpers[i], SYM_UNDEF_FUNC, -1, 0);

  for (uint32_t i = m->nimported_funcs; i < m->nfuncs; i++)
    if (!m->reached || m->reached[i])
      obj_intern_sym(o, m->funcs[i].sym, SYM_LOCAL_FUNC, -1, 0);
}

uint32_t codegen_out_bytes(Module *m) { return module_max_outgoing(m); }

static char g_module_sym[128] = "warmc_module";

static char g_anchor[3][160] = { "__warmc_text", "__warmc_rodata", "__warmc_data" };

bool warmc_load_barrier = true;
void codegen_set_load_barrier(bool on) { warmc_load_barrier = on; }

void codegen_set_symbol_prefix(const char *prefix) {
  snprintf(g_module_sym, sizeof g_module_sym, "%s_module", prefix);
  snprintf(g_anchor[0], sizeof g_anchor[0], "__warmc_%s_text", prefix);
  snprintf(g_anchor[1], sizeof g_anchor[1], "__warmc_%s_rodata", prefix);
  snprintf(g_anchor[2], sizeof g_anchor[2], "__warmc_%s_data", prefix);
}

static const char *module_sym(void) { return g_module_sym; }

static uint64_t emit_string(ObjBuilder *o, const char *s) {
  uint64_t off = o->rodata.len;
  buf_bytes(&o->rodata, s, strlen(s) + 1);
  return off;
}

static uint64_t emit_name(ObjBuilder *o, const char *s, uint32_t n) {
  uint64_t off = o->rodata.len;
  buf_bytes(&o->rodata, s, n);
  buf_u8(&o->rodata, 0);
  return off;
}

typedef struct { uint64_t hash, off; uint32_t len; } RoBlob;
static RoBlob *g_ro;
static uint32_t g_nro, g_cap_ro;
static uint32_t *g_ro_ix, g_ro_ix_cap;

static uint64_t g_ro_shared;

static void rodata_pool_reset(void) {
  xfree(g_ro); xfree(g_ro_ix);
  g_ro = NULL; g_ro_ix = NULL;
  g_nro = g_cap_ro = g_ro_ix_cap = 0;
  g_ro_shared = 0;
}

static uint64_t ro_hash(const void *p, size_t n) {
  const uint8_t *b = p;
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
  return h;
}

static void ro_ix_put(uint32_t i) {
  uint32_t mask = g_ro_ix_cap - 1;
  uint32_t h = (uint32_t)g_ro[i].hash & mask;
  while (g_ro_ix[h]) h = (h + 1) & mask;
  g_ro_ix[h] = i + 1;
}

static void ro_ix_grow(void) {
  uint32_t want = g_ro_ix_cap ? g_ro_ix_cap * 2 : 256;
  xfree(g_ro_ix);
  g_ro_ix = xcalloc(want, sizeof(uint32_t));
  g_ro_ix_cap = want;
  for (uint32_t i = 0; i < g_nro; i++) ro_ix_put(i);
}

static uint64_t rodata_intern(ObjBuilder *o, const void *p, size_t n,
                              size_t align) {
  if (!n) return o->rodata.len;
  uint64_t h = ro_hash(p, n);
  if (g_ro_ix_cap) {
    uint32_t mask = g_ro_ix_cap - 1;
    uint32_t k = (uint32_t)h & mask;
    while (g_ro_ix[k]) {
      const RoBlob *e = &g_ro[g_ro_ix[k] - 1];
      if (e->hash == h && e->len == n && (e->off & (align - 1)) == 0 &&
          !memcmp(o->rodata.data + e->off, p, n)) {
        g_ro_shared += n;
        return e->off;
      }
      k = (k + 1) & mask;
    }
  }
  buf_align(&o->rodata, align);
  uint64_t off = o->rodata.len;
  buf_bytes(&o->rodata, p, n);
  if (g_nro == g_cap_ro) {
    g_cap_ro = g_cap_ro ? g_cap_ro * 2 : 64;
    g_ro = xrealloc(g_ro, g_cap_ro * sizeof(RoBlob));
  }
  g_ro[g_nro].hash = h;
  g_ro[g_nro].off = off;
  g_ro[g_nro].len = (uint32_t)n;
  uint32_t idx = g_nro++;

  if ((uint64_t)g_nro * 3 >= (uint64_t)g_ro_ix_cap * 2) ro_ix_grow();
  else ro_ix_put(idx);
  return off;
}

typedef struct { uint8_t *p; size_t len, cap; } RoTmp;

static void rotmp_u32(RoTmp *t, uint32_t v) {
  if (t->len + 4 > t->cap) {
    t->cap = t->cap ? t->cap * 2 : 64;
    t->p = xrealloc(t->p, t->cap);
  }
  for (int i = 0; i < 4; i++) t->p[t->len++] = (uint8_t)(v >> (8 * i));
}

static void rotmp_uleb(RoTmp *t, uint32_t v) {
  do {
    if (t->len + 1 > t->cap) {
      t->cap = t->cap ? t->cap * 2 : 64;
      t->p = xrealloc(t->p, t->cap);
    }
    uint8_t b = (uint8_t)(v & 0x7Fu);
    v >>= 7;
    t->p[t->len++] = v ? (uint8_t)(b | 0x80u) : b;
  } while (v);
}

static void site_delta(RoTmp *t, const Module *m, uint32_t func_index,
                       uint32_t pc, uint32_t *prev) {
  if (t->len && pc <= *prev)
    warmc_fatal("internal: %s has frame sites at %u after %u; they must "
                "increase", m->funcs[func_index].sym, pc, *prev);
  rotmp_uleb(t, pc - *prev);
  *prev = pc;
}

static void rotmp_u8(RoTmp *t, uint8_t v) {
  if (t->len + 1 > t->cap) {
    t->cap = t->cap ? t->cap * 2 : 64;
    t->p = xrealloc(t->p, t->cap);
  }
  t->p[t->len++] = v;
}

static void rotmp_u64(RoTmp *t, uint64_t v) {
  if (t->len + 8 > t->cap) {
    t->cap = t->cap ? t->cap * 2 : 64;
    t->p = xrealloc(t->p, t->cap);
  }
  for (int i = 0; i < 8; i++) t->p[t->len++] = (uint8_t)(v >> (8 * i));
}

typedef struct { uint64_t hash; uint32_t first, n, nbytes; } RootRun;
static RootRun *g_rr;
static uint32_t g_nrr, g_cap_rr;
static uint32_t *g_rr_ix, g_rr_ix_cap;
static uint32_t g_rr_shared;

static void root_pool_reset(void) {
  xfree(g_rr); xfree(g_rr_ix);
  g_rr = NULL; g_rr_ix = NULL;
  g_nrr = g_cap_rr = g_rr_ix_cap = 0;
  g_rr_shared = 0;
}

static void rr_ix_put(uint32_t i) {
  uint32_t mask = g_rr_ix_cap - 1;
  uint32_t h = (uint32_t)g_rr[i].hash & mask;
  while (g_rr_ix[h]) h = (h + 1) & mask;
  g_rr_ix[h] = i + 1;
}

static void rr_ix_grow(void) {
  uint32_t want = g_rr_ix_cap ? g_rr_ix_cap * 2 : 256;
  xfree(g_rr_ix);
  g_rr_ix = xcalloc(want, sizeof(uint32_t));
  g_rr_ix_cap = want;
  for (uint32_t i = 0; i < g_nrr; i++) rr_ix_put(i);
}

static void rr_record(const RoTmp *pool, uint32_t first, uint32_t n,
                      uint32_t nbytes, uint64_t hash) {
  if (g_rr_ix_cap) {
    uint32_t mask = g_rr_ix_cap - 1;
    uint32_t k = (uint32_t)hash & mask;
    while (g_rr_ix[k]) {
      const RootRun *r = &g_rr[g_rr_ix[k] - 1];
      if (r->hash == hash && r->n == n && r->nbytes == nbytes &&
          !memcmp(pool->p + r->first, pool->p + first, nbytes))
        return;
      k = (k + 1) & mask;
    }
  }
  if (g_nrr == g_cap_rr) {
    g_cap_rr = g_cap_rr ? g_cap_rr * 2 : 64;
    g_rr = xrealloc(g_rr, g_cap_rr * sizeof(RootRun));
  }
  g_rr[g_nrr].hash = hash;
  g_rr[g_nrr].first = first;
  g_rr[g_nrr].n = n;
  g_rr[g_nrr].nbytes = nbytes;
  uint32_t idx = g_nrr++;
  if ((uint64_t)g_nrr * 3 >= (uint64_t)g_rr_ix_cap * 2) rr_ix_grow();
  else rr_ix_put(idx);
}

static void sort_u32(uint32_t *a, uint32_t n) {
  for (uint32_t i = 1; i < n; i++) {
    uint32_t v = a[i], j = i;
    while (j && a[j - 1] > v) { a[j] = a[j - 1]; j--; }
    a[j] = v;
  }
}

static uint32_t uleb_put(uint8_t *p, uint32_t v) {
  uint32_t i = 0;
  do {
    uint8_t b = (uint8_t)(v & 0x7Fu);
    v >>= 7;
    p[i++] = v ? (uint8_t)(b | 0x80u) : b;
  } while (v);
  return i;
}

static bool g_check_roots;

static void root_run_check(const RoTmp *pool, uint32_t first,
                           const uint32_t *v, uint32_t n) {
  const uint8_t *p = pool->p + first;
  const uint8_t *end = pool->p + pool->len;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t got = 0, s = 0, b;
    do {
      if (p >= end)
        warmc_fatal("internal: root run at %u ran off the end of the pool",
                    first);
      b = *p++;
      got |= (b & 0x7Fu) << s;
      s += 7;
    } while (b & 0x80u);
    if (got != v[i])
      warmc_fatal("internal: root run at %u entry %u decodes as %u, not %u",
                  first, i, got, v[i]);
  }
}

static uint32_t root_run_intern(RoTmp *pool, const uint32_t *v, uint32_t n) {

  uint8_t bytes[5 * 64];
  uint32_t offs[64];
  uint8_t *heap = NULL;
  uint32_t *offheap = NULL;
  uint8_t *p = bytes;
  uint32_t *off = offs;
  if (n > 64) {
    heap = xmalloc(n * 5u); p = heap;
    offheap = xmalloc(n * sizeof(uint32_t)); off = offheap;
  }
  uint32_t len = 0;
  for (uint32_t i = 0; i < n; i++) { off[i] = len; len += uleb_put(p + len, v[i]); }
  uint64_t h = ro_hash(p, len);
  if (g_rr_ix_cap) {
    uint32_t mask = g_rr_ix_cap - 1;
    uint32_t k = (uint32_t)h & mask;
    while (g_rr_ix[k]) {
      const RootRun *r = &g_rr[g_rr_ix[k] - 1];
      if (r->hash == h && r->n == n && r->nbytes == len &&
          !memcmp(pool->p + r->first, p, len)) {
        g_rr_shared += n;
        xfree(heap); xfree(offheap);
        if (g_check_roots) root_run_check(pool, r->first, v, n);
        return r->first;
      }
      k = (k + 1) & mask;
    }
  }
  uint32_t first = (uint32_t)pool->len;
  for (uint32_t i = 0; i < len; i++) rotmp_u8(pool, p[i]);

  for (uint32_t s = 0; s < n; s++)
    rr_record(pool, first + off[s], n - s, len - off[s],
              s ? ro_hash(p + off[s], len - off[s]) : h);
  xfree(heap); xfree(offheap);
  if (g_check_roots) root_run_check(pool, first, v, n);
  return first;
}

#define CONST_UNIT 8u

typedef struct { uint64_t hash; uint32_t first, nbytes; } ConstProg;
static ConstProg *g_cp;
static uint32_t g_ncp, g_cap_cp;
static uint32_t *g_cp_ix, g_cp_ix_cap;
static RoTmp g_cp_tmp;

static uint32_t g_cp_shared;

static void const_pool_reset(void) {
  xfree(g_cp); xfree(g_cp_ix); xfree(g_cp_tmp.p);
  g_cp = NULL; g_cp_ix = NULL; g_cp_tmp.p = NULL;
  g_ncp = g_cap_cp = g_cp_ix_cap = 0;
  g_cp_tmp.len = g_cp_tmp.cap = 0;
  g_cp_shared = 0;
}

static void cp_ix_put(uint32_t i) {
  uint32_t mask = g_cp_ix_cap - 1;
  uint32_t h = (uint32_t)g_cp[i].hash & mask;
  while (g_cp_ix[h]) h = (h + 1) & mask;
  g_cp_ix[h] = i + 1;
}

static void cp_ix_grow(void) {
  uint32_t want = g_cp_ix_cap ? g_cp_ix_cap * 2 : 256;
  xfree(g_cp_ix);
  g_cp_ix = xcalloc(want, sizeof(uint32_t));
  g_cp_ix_cap = want;
  for (uint32_t i = 0; i < g_ncp; i++) cp_ix_put(i);
}

static void cp_record(ObjBuilder *o, uint64_t base, uint32_t first,
                      uint32_t nbytes, uint64_t hash) {
  if (g_cp_ix_cap) {
    uint32_t mask = g_cp_ix_cap - 1;
    uint32_t k = (uint32_t)hash & mask;
    const uint8_t *mine = o->data.data + base + (uint64_t)first * CONST_UNIT;
    while (g_cp_ix[k]) {
      const ConstProg *c = &g_cp[g_cp_ix[k] - 1];
      if (c->hash == hash && c->nbytes == nbytes &&
          !memcmp(o->data.data + base + (uint64_t)c->first * CONST_UNIT,
                  mine, nbytes))
        return;
      k = (k + 1) & mask;
    }
  }
  if (g_ncp == g_cap_cp) {
    g_cap_cp = g_cap_cp ? g_cap_cp * 2 : 64;
    g_cp = xrealloc(g_cp, g_cap_cp * sizeof(ConstProg));
  }
  g_cp[g_ncp].hash = hash;
  g_cp[g_ncp].first = first;
  g_cp[g_ncp].nbytes = nbytes;
  uint32_t idx = g_ncp++;
  if ((uint64_t)g_ncp * 3 >= (uint64_t)g_cp_ix_cap * 2) cp_ix_grow();
  else cp_ix_put(idx);
}

static void cp_insn(RoTmp *t, uint32_t op, uint64_t lo, uint64_t hi) {
  uint32_t narrow;
  switch (op) {

    case IOP_I32_CONST: case IOP_F32_CONST:
    case IOP_GLOBAL_GET: case IOP_REF_FUNC:
    case IOP_STRUCT_NEW: case IOP_STRUCT_NEW_DEFAULT:
    case IOP_ARRAY_NEW: case IOP_ARRAY_NEW_DEFAULT: case IOP_ARRAY_NEW_FIXED:
      narrow = (uint32_t)lo;
      break;

    default:
      narrow = 0;
      break;
  }
  rotmp_u32(t, op);
  rotmp_u32(t, narrow);
  switch (op) {
    case IOP_I64_CONST: case IOP_F64_CONST: rotmp_u64(t, lo); break;
    case IOP_V128_CONST: rotmp_u64(t, lo); rotmp_u64(t, hi); break;
    case IOP_ARRAY_NEW_FIXED: rotmp_u64(t, hi); break;
    default: break;
  }
}

static uint32_t emit_const_expr(ObjBuilder *o, const Module *m, uint64_t base,
                                const InitExpr *e, uint32_t *count) {

  uint32_t *at = xmalloc(((size_t)e->n + 1) * sizeof(uint32_t));
  g_cp_tmp.len = 0;
  for (uint32_t i = 0; i < e->n; i++) {
    at[i] = (uint32_t)g_cp_tmp.len;
    uint64_t lo = e->ins[i].lo;

    switch (e->ins[i].op) {
      case IOP_STRUCT_NEW: case IOP_STRUCT_NEW_DEFAULT:
      case IOP_ARRAY_NEW: case IOP_ARRAY_NEW_DEFAULT: case IOP_ARRAY_NEW_FIXED:
        lo = gctype_slot(m, (uint32_t)lo);
        break;
      default: break;
    }
    cp_insn(&g_cp_tmp, e->ins[i].op, lo, e->ins[i].hi);
  }
  at[e->n] = (uint32_t)g_cp_tmp.len;
  cp_insn(&g_cp_tmp, IOP_END, 0, 0);

  const uint8_t *p = g_cp_tmp.p;
  size_t n = g_cp_tmp.len;
  uint64_t h = ro_hash(p, n);
  if (g_cp_ix_cap) {
    uint32_t mask = g_cp_ix_cap - 1;
    uint32_t k = (uint32_t)h & mask;
    while (g_cp_ix[k]) {
      const ConstProg *c = &g_cp[g_cp_ix[k] - 1];
      if (c->hash == h && c->nbytes == n &&
          !memcmp(o->data.data + base + (uint64_t)c->first * CONST_UNIT,
                  p, n)) {
        g_cp_shared += (uint32_t)(n / CONST_UNIT);
        xfree(at);
        return c->first;
      }
      k = (k + 1) & mask;
    }
  }
  uint32_t first = *count;
  buf_bytes(&o->data, p, n);
  *count += (uint32_t)(n / CONST_UNIT);

  for (uint32_t i = 0; i <= e->n; i++)
    cp_record(o, base, first + at[i] / CONST_UNIT, (uint32_t)(n - at[i]),
              at[i] ? ro_hash(p + at[i], n - at[i]) : h);
  xfree(at);
  return first;
}

static void data_ptr(ObjBuilder *o, int section, uint64_t off) {
  uint32_t sym = obj_intern_sym(o, g_anchor[section],
                                section == 0 ? SYM_LOCAL_FUNC : SYM_LOCAL_DATA, section, 0);
  obj_add_reloc(o, REL_ABS64, 2, o->data.len, sym, (int64_t)off);
  buf_u64(&o->data, 0);
}

static void func_ptr(ObjBuilder *o, Module *m, uint32_t fi) {
  uint32_t sym = obj_intern_sym(o, m->funcs[fi].sym, SYM_UNDEF_FUNC, -1, 0);
  obj_add_reloc(o, REL_ABS64, 2, o->data.len, sym, 0);
  buf_u64(&o->data, 0);
}

static uint64_t type_hash(const CompType *t) { return t->id; }

uint64_t wasm_type_hash(const CompType *t) { return t->id; }

static uint64_t valtype_id(const Module *m, ValType t) {
  if (!vt_is_ref(t) || !vt_concrete(t)) return t;
  uint64_t h = 1469598103934665603ull;
  #define MIX(b) do { h ^= (uint64_t)(uint8_t)(b); h *= 1099511628211ull; } while (0)
  MIX(vt_nullable(t) ? 1 : 0);
  uint64_t id = m->types[vt_type_index(t)].id;
  for (int i = 0; i < 8; i++) MIX(id >> (8 * i));
  #undef MIX
  return h;
}

static uint64_t heap_id_of(const Module *m, HeapType h) {
  if (h >= 0) return m->types[h].id;
  return 0x8000000000000000ull | (uint64_t)(uint32_t)(-h);
}

static const HeapType ABSTRACT_HEAPS[] = {
  HT_FUNC, HT_NOFUNC, HT_EXTERN, HT_NOEXTERN, HT_ANY, HT_EQ, HT_I31,
  HT_STRUCT, HT_ARRAY, HT_NONE, HT_EXN, HT_NOEXN
};

static uint32_t vt_flags_of(const Module *m, ValType t) {
  if (!vt_is_ref(t)) return 0;
  HeapType h = vt_heap(t);
  uint32_t flags = VTF_REF | (vt_nullable(t) ? VTF_NULL : 0u);

  HeapType top = HT_ANY;
  if (heap_subtype(m, h, HT_FUNC)) top = HT_FUNC;
  else if (heap_subtype(m, h, HT_EXTERN)) top = HT_EXTERN;
  else if (heap_subtype(m, h, HT_EXN)) top = HT_EXN;
  uint32_t hier = top == HT_FUNC ? 0u : top == HT_EXTERN ? 1u
                : top == HT_EXN ? 2u : 3u;
  flags |= hier << VTF_HIER_SHIFT;
  if (h == top) flags |= VTF_TOP;
  bool bottom = true;
  for (size_t i = 0; i < sizeof ABSTRACT_HEAPS / sizeof ABSTRACT_HEAPS[0]; i++)
    if (heap_subtype(m, ABSTRACT_HEAPS[i], h) && ABSTRACT_HEAPS[i] != h)
      { bottom = false; break; }

  if (h >= 0) bottom = false;
  if (bottom) flags |= VTF_BOTTOM;
  return flags;
}

static uint64_t emit_supers(ObjBuilder *o, const Module *m, HeapType h) {
  uint64_t n = 1;
  for (size_t i = 0; i < sizeof ABSTRACT_HEAPS / sizeof ABSTRACT_HEAPS[0]; i++)
    if (ABSTRACT_HEAPS[i] != h && heap_subtype(m, h, ABSTRACT_HEAPS[i])) n++;
  if (h >= 0) n += m->types[h].depth;
  RoTmp t = { NULL, 0, 0 };
  rotmp_u64(&t, n);
  rotmp_u64(&t, heap_id_of(m, h));
  for (size_t i = 0; i < sizeof ABSTRACT_HEAPS / sizeof ABSTRACT_HEAPS[0]; i++)
    if (ABSTRACT_HEAPS[i] != h && heap_subtype(m, h, ABSTRACT_HEAPS[i]))
      rotmp_u64(&t, heap_id_of(m, ABSTRACT_HEAPS[i]));
  if (h >= 0)
    for (uint32_t d = 0; d < m->types[h].depth; d++)
      rotmp_u64(&t, m->types[h].display[d]);

  uint64_t off = rodata_intern(o, t.p, t.len, 8);
  xfree(t.p);
  return off;
}

bool g_guard_pages = false;
bool g_det_profile = false;
bool g_no_export_names = false;

bool g_profile = false;

static bool export_keeps_name(const Export *ex) {
  if (!g_no_export_names) return true;
  return (ex->name_len == 6 && !memcmp(ex->name, "_start", 6)) ||
         (ex->name_len == 4 && !memcmp(ex->name, "main", 4));
}

void codegen_emit_module_data(Module *m, ObjBuilder *o) {

  obj_intern_sym(o, g_anchor[1], SYM_LOCAL_DATA, 1, 0);
  obj_intern_sym(o, g_anchor[2], SYM_LOCAL_DATA, 2, 0);

  rodata_pool_reset();
  const_pool_reset();

  uint64_t blob_off = o->rodata.len;
  uint64_t *seg_blob = xcalloc(m->ndatas ? m->ndatas : 1, sizeof(uint64_t));
  uint64_t *seg_len = xcalloc(m->ndatas ? m->ndatas : 1, sizeof(uint64_t));
  for (uint32_t i = 0; i < m->ndatas; i++) {
    seg_blob[i] = o->rodata.len - blob_off;
    if (m->data_read && !m->data_read[i]) continue;
    seg_len[i] = m->datas[i].len;
    buf_bytes(&o->rodata, m->datas[i].data, m->datas[i].len);
  }
  buf_align(&o->rodata, 8);

  uint64_t ro_after_blob = o->rodata.len;
  uint64_t *exp_name = xcalloc(m->nexports ? m->nexports : 1, sizeof(uint64_t));
  uint64_t *exp_ptypes = xcalloc(m->nexports ? m->nexports : 1, sizeof(uint64_t));
  uint64_t *exp_rtypes = xcalloc(m->nexports ? m->nexports : 1, sizeof(uint64_t));
  for (uint32_t i = 0; i < m->nexports; i++) {
    if (!export_keeps_name(&m->exports[i])) continue;
    exp_name[i] = emit_name(o, m->exports[i].name, m->exports[i].name_len);
    if (m->exports[i].kind == EK_FUNC) {
      CompType *t = &m->types[m->funcs[m->exports[i].index].type_index];

      RoTmp sig = { NULL, 0, 0 };
      for (uint32_t j = 0; j < t->nparams; j++) {
        if (sig.len == sig.cap) { sig.cap = sig.cap ? sig.cap * 2 : 16;
                                  sig.p = xrealloc(sig.p, sig.cap); }
        sig.p[sig.len++] = vt_abi_code(t->params[j]);
      }
      exp_ptypes[i] = rodata_intern(o, sig.p, sig.len, 1);
      sig.len = 0;
      for (uint32_t j = 0; j < t->nresults; j++) {
        if (sig.len == sig.cap) { sig.cap = sig.cap ? sig.cap * 2 : 16;
                                  sig.p = xrealloc(sig.p, sig.cap); }
        sig.p[sig.len++] = vt_abi_code(t->results[j]);
      }
      exp_rtypes[i] = rodata_intern(o, sig.p, sig.len, 1);
      xfree(sig.p);
    }
  }

  buf_align(&o->rodata, 8);
  uint64_t ro_after_expstr = o->rodata.len;
  uint64_t *rtt_off = xcalloc(m->ntypes ? m->ntypes : 1, sizeof(uint64_t));

  uint64_t rtt_none = o->rodata.len;
  buf_u64(&o->rodata, 0);

  RoTmp rtt = { NULL, 0, 0 };
  for (uint32_t i = 0; i < m->ntypes; i++) {
    if (m->type_live && !m->type_live[i]) { rtt_off[i] = rtt_none; continue; }
    rtt.len = 0;
    rotmp_u64(&rtt, m->types[i].depth + 1u);
    for (uint32_t d = 0; d <= m->types[i].depth; d++)
      rotmp_u64(&rtt, m->types[i].display[d]);
    rtt_off[i] = rodata_intern(o, rtt.p, rtt.len, 8);
  }
  xfree(rtt.p);
  buf_align(&o->rodata, 8);

  uint64_t ro_after_rtt = o->rodata.len;

  buf_align(&o->data, 8);

  uint64_t consts_off = o->data.len;
  uint32_t nconsts = 0;
  uint32_t *g_init = xcalloc(m->nglobals ? m->nglobals : 1, sizeof(uint32_t));
  uint32_t *d_init = xcalloc(m->ndatas ? m->ndatas : 1, sizeof(uint32_t));
  uint32_t *e_init = xcalloc(m->nelems ? m->nelems : 1, sizeof(uint32_t));
  uint32_t *t_init = xcalloc(m->ntables ? m->ntables : 1, sizeof(uint32_t));
  uint64_t total_entries = 0;
  for (uint32_t i = 0; i < m->nelems; i++) total_entries += m->elems[i].n;
  uint32_t *item_init = xcalloc(total_entries ? total_entries : 1, sizeof(uint32_t));

  for (uint32_t i = 0; i < m->nglobals; i++)
    if (!m->globals[i].imported && (!m->glob_read || m->glob_read[i]))
      g_init[i] = emit_const_expr(o, m, consts_off, &m->globals[i].init, &nconsts);
  for (uint32_t i = 0; i < m->ntables; i++)
    if (m->tables[i].has_init)
      t_init[i] = emit_const_expr(o, m, consts_off, &m->tables[i].init, &nconsts);
  uint32_t *d_off = xcalloc(m->ndatas ? m->ndatas : 1, sizeof(uint32_t));
  uint8_t *d_off_const = xcalloc(m->ndatas ? m->ndatas : 1, 1);
  for (uint32_t i = 0; i < m->ndatas; i++)
    if (m->datas[i].mode == SEG_ACTIVE) {
      const InitExpr *e = &m->datas[i].offset;
      if (e->n == 1 && e->ins[0].op == IOP_I32_CONST) {
        d_off[i] = (uint32_t)e->ins[0].lo;
        d_off_const[i] = 1;
      } else {
        d_init[i] = emit_const_expr(o, m, consts_off, e, &nconsts);
      }
    }
  {
    uint32_t k = 0;
    for (uint32_t i = 0; i < m->nelems; i++) {
      if (m->elems[i].mode == SEG_ACTIVE)
        e_init[i] = emit_const_expr(o, m, consts_off, &m->elems[i].offset, &nconsts);
      for (uint32_t j = 0; j < m->elems[i].n; j++, k++)
        item_init[k] = emit_const_expr(o, m, consts_off, &m->elems[i].items[j],
                                       &nconsts);
    }
  }

  uint64_t globals_off = o->data.len;
  for (uint32_t i = 0; i < m->nglobals; i++) {
    buf_u8(&o->data, vt_abi_code(m->globals[i].type));

    buf_u8(&o->data, (uint8_t)((m->globals[i].imported ? 1u : 0u) |
                               (!m->globals[i].imported && m->glob_read &&
                                !m->glob_read[i] ? 2u : 0u)));
    buf_u16(&o->data, 0);
    buf_u32(&o->data, g_init[i]);
  }
  buf_align(&o->data, 8);

  uint64_t datasegs_off = o->data.len;
  for (uint32_t i = 0; i < m->ndatas; i++) {
    if (seg_blob[i] > 0xffffffffull || seg_len[i] > 0xffffffffull ||
        m->datas[i].mem_index > 0xffffffu)
      warmc_fatal("data segment %u is past what its descriptor can name", i);
    uint32_t info = m->datas[i].mem_index << 8;
    if (m->datas[i].mode != SEG_ACTIVE) info |= 1u;
    if (d_off_const[i]) info |= 2u;
    buf_u32(&o->data, (uint32_t)seg_blob[i]);
    buf_u32(&o->data, (uint32_t)seg_len[i]);
    buf_u32(&o->data, d_off_const[i] ? d_off[i] : d_init[i]);
    buf_u32(&o->data, info);
  }

  uint64_t *seg_first = xcalloc(m->nelems ? m->nelems : 1, sizeof(uint64_t));
  uint64_t entries_off = o->data.len;
  {
    uint32_t k = 0;
    for (uint32_t i = 0; i < m->nelems; i++) {
      seg_first[i] = k;
      for (uint32_t j = 0; j < m->elems[i].n; j++, k++)
        buf_u32(&o->data, item_init[k]);
    }
  }
  buf_align(&o->data, 8);

  uint64_t elemsegs_off = o->data.len;
  for (uint32_t i = 0; i < m->nelems; i++) {
    buf_u32(&o->data, (uint32_t)m->elems[i].mode);
    buf_u32(&o->data, m->elems[i].table_index);
    buf_u32(&o->data, e_init[i]);
    buf_u32(&o->data, 0);
    buf_u64(&o->data, seg_first[i]);
    buf_u64(&o->data, m->elems[i].n);
  }

  uint64_t *exp_supers = xcalloc(m->nexports ? m->nexports : 1, sizeof(uint64_t));
  uint32_t *exp_flags = xcalloc(m->nexports ? m->nexports : 1, sizeof(uint32_t));
  uint64_t *exp_heap = xcalloc(m->nexports ? m->nexports : 1, sizeof(uint64_t));
  bool *exp_has_supers = xcalloc(m->nexports ? m->nexports : 1, sizeof(bool));
  for (uint32_t i = 0; i < m->nexports; i++) {
    Export *ex = &m->exports[i];
    ValType t;
    if (!export_keeps_name(ex)) continue;

    if (ex->kind == EK_FUNC)
      t = vt_ref(false, (HeapType)m->funcs[ex->index].type_index);
    else if (ex->kind == EK_GLOBAL) t = m->globals[ex->index].type;
    else if (ex->kind == EK_TABLE) t = m->tables[ex->index].elem_type;
    else continue;
    if (!vt_is_ref(t)) { exp_flags[i] = 0; continue; }
    exp_flags[i] = vt_flags_of(m, t);
    exp_heap[i] = heap_id_of(m, vt_heap(t));
    exp_supers[i] = emit_supers(o, m, vt_heap(t));
    exp_has_supers[i] = true;
  }
  buf_align(&o->rodata, 8);

  uint64_t exports_off = o->data.len;
  uint32_t nexports = 0;
  for (uint32_t i = 0; i < m->nexports; i++) {
    Export *ex = &m->exports[i];
    uint64_t type_id = 0;
    uint32_t mut = 0;

    if (!export_keeps_name(ex)) continue;
    nexports++;
    data_ptr(o, 1, exp_name[i]);

    if (ex->kind == EK_FUNC) {
      CompType *t = &m->types[m->funcs[ex->index].type_index];
      type_id = type_hash(t);
    } else {
      if (ex->kind == EK_GLOBAL) {
        type_id = valtype_id(m, m->globals[ex->index].type);
        mut = m->globals[ex->index].mutable_ ? 1u : 0u;
      } else if (ex->kind == EK_TABLE) {
        type_id = valtype_id(m, m->tables[ex->index].elem_type);
      } else if (ex->kind == EK_TAG) {
        type_id = type_hash(&m->types[m->tags[ex->index].type_index]);
      }
    }
    buf_u32(&o->data, ex->name_len);
    buf_u32(&o->data, (uint32_t)ex->kind);
    buf_u32(&o->data, ex->index);
    if (ex->kind == EK_FUNC) {
      CompType *t = &m->types[m->funcs[ex->index].type_index];
      buf_u32(&o->data, t->nparams);
      buf_u32(&o->data, t->nresults);
    } else {
      buf_u32(&o->data, 0);
      buf_u32(&o->data, 0);
    }
    buf_u32(&o->data, 0);
    data_ptr(o, 1, exp_ptypes[i]);
    data_ptr(o, 1, exp_rtypes[i]);
    buf_u64(&o->data, type_id);
    buf_u32(&o->data, mut);
    buf_u32(&o->data, exp_flags[i]);
    buf_u64(&o->data, exp_heap[i]);
    if (exp_has_supers[i]) data_ptr(o, 1, exp_supers[i]);
    else buf_u64(&o->data, 0);
  }

  uint64_t funcrefs_off = o->data.len;
  for (uint32_t i = 0; i < m->nfuncs; i++) {

    if (m->funcs[i].imported) buf_u64(&o->data, 0);
    else if (m->reached && !m->reached[i]) {
      uint32_t sym = obj_intern_sym(o, "warmc_unreached", SYM_UNDEF_FUNC, -1, 0);
      obj_add_reloc(o, REL_ABS64, 2, o->data.len, sym, 0);
      buf_u64(&o->data, 0);
    }
    else func_ptr(o, m, i);
    buf_u64(&o->data, type_hash(&m->types[m->funcs[i].type_index]));
    data_ptr(o, 1, rtt_off[m->funcs[i].type_index]);
  }

  uint64_t *imp_module = xcalloc(m->nimports ? m->nimports : 1, sizeof(uint64_t));
  uint64_t *imp_name = xcalloc(m->nimports ? m->nimports : 1, sizeof(uint64_t));
  for (uint32_t i = 0; i < m->nimports; i++) {
    imp_module[i] = emit_name(o, m->imports[i].module, m->imports[i].module_len);
    imp_name[i] = emit_name(o, m->imports[i].name, m->imports[i].name_len);
  }
  buf_align(&o->rodata, 8);
  buf_align(&o->data, 8);

  uint64_t imports_off = o->data.len;
  uint32_t nimported_funcs = 0, ntable_imports = 0, nmem_imports = 0;
  for (uint32_t i = 0; i < m->nimports; i++) {
    Import *im = &m->imports[i];
    uint64_t type_id = 0, min = 0, max = 0, heap = 0;
    uint32_t mut = 0, has_max = 0, index64 = 0, flags = 0;
    switch (im->kind) {
      case EK_FUNC: {
        type_id = type_hash(&m->types[im->type_index]);
        ValType t = vt_ref(false, (HeapType)im->type_index);
        flags = vt_flags_of(m, t);
        heap = heap_id_of(m, vt_heap(t));
        nimported_funcs++;
        break;
      }
      case EK_TABLE: {

        Table *t = &m->tables[ntable_imports++];
        type_id = valtype_id(m, t->elem_type);
        flags = vt_flags_of(m, t->elem_type);
        heap = vt_is_ref(t->elem_type) ? heap_id_of(m, vt_heap(t->elem_type)) : 0;
        min = t->min; max = t->max; has_max = t->has_max;
        index64 = t->index64;
        break;
      }
      case EK_MEM: {

        Memory *mm = &m->memories[nmem_imports++];
        min = mm->min; max = mm->max; has_max = mm->has_max;
        index64 = mm->index64;
        break;
      }
      case EK_GLOBAL: {
        uint32_t gi = 0;
        for (uint32_t j = 0; j < i; j++) if (m->imports[j].kind == EK_GLOBAL) gi++;
        type_id = valtype_id(m, m->globals[gi].type);
        flags = vt_flags_of(m, m->globals[gi].type);
        heap = vt_is_ref(m->globals[gi].type)
                 ? heap_id_of(m, vt_heap(m->globals[gi].type)) : 0;
        mut = m->globals[gi].mutable_ ? 1u : 0u;
        break;
      }
      case EK_TAG:
        type_id = type_hash(&m->types[im->type_index]);
        break;
    }
    data_ptr(o, 1, imp_module[i]);
    data_ptr(o, 1, imp_name[i]);
    buf_u64(&o->data, type_id);
    buf_u64(&o->data, min);
    buf_u64(&o->data, max);
    buf_u32(&o->data, (uint32_t)im->kind);
    buf_u32(&o->data, mut);
    buf_u32(&o->data, has_max);
    buf_u32(&o->data, index64);
    buf_u32(&o->data, flags);
    buf_u32(&o->data, im->name_len);
    buf_u64(&o->data, heap);
    buf_u32(&o->data, im->module_len);
    buf_u32(&o->data, 0);
  }

  uint64_t tables_off = o->data.len;
  for (uint32_t i = 0; i < m->ntables; i++) {
    Table *t = &m->tables[i];
    buf_u64(&o->data, t->min);
    buf_u64(&o->data, t->has_max ? t->max : 0xffffffffull);
    buf_u32(&o->data, t->has_max ? 1u : 0u);
    buf_u32(&o->data, vt_abi_code(t->elem_type));
    buf_u32(&o->data, t->imported ? 1u : 0u);
    buf_u32(&o->data, t->index64 ? 1u : 0u);
    buf_u32(&o->data, t->has_init ? 1u : 0u);
    buf_u32(&o->data, t_init[i]);
  }

  uint64_t memories_off = o->data.len;
  for (uint32_t i = 0; i < m->nmemories; i++) {
    Memory *mm = &m->memories[i];
    buf_u64(&o->data, mm->min);
    buf_u64(&o->data, mm->has_max ? mm->max : 65536ull);
    buf_u32(&o->data, mm->has_max ? 1u : 0u);
    buf_u32(&o->data, mm->imported ? 1u : 0u);
    buf_u32(&o->data, mm->index64 ? 1u : 0u);
    buf_u32(&o->data, 0);
  }

  uint64_t tags_off = o->data.len;
  for (uint32_t i = 0; i < m->ntags; i++) {
    buf_u32(&o->data, m->tags[i].imported ? 1u : 0u);
    buf_u32(&o->data, 0);
    buf_u64(&o->data, type_hash(&m->types[m->tags[i].type_index]));
  }

  uint64_t ro_after_supers = o->rodata.len;
  uint64_t *uw_sites = xcalloc(g_nunwind ? g_nunwind : 1, sizeof(uint64_t));
  uint64_t *uw_clauses = xcalloc(g_nunwind ? g_nunwind : 1, sizeof(uint64_t));
  uint64_t *uw_gcsites = xcalloc(g_nunwind ? g_nunwind : 1, sizeof(uint64_t));
  uint64_t *uw_gcroots = xcalloc(g_nunwind ? g_nunwind : 1, sizeof(uint64_t));

  uint32_t **site_first = xcalloc(g_nunwind ? g_nunwind : 1, sizeof(uint32_t *));
  RoTmp pool = { NULL, 0, 0 };
  uint32_t *run = NULL, run_cap = 0;
  root_pool_reset();
  g_check_roots = getenv("WARMC_CHECK_ROOTS") != NULL;
  for (uint32_t i = 0; i < g_nunwind; i++) {
    const FuncUnwind *u = &g_unwind[i];
    if (!(u->saved & UW_HAS_LR) || !u->ngcsites) continue;
    site_first[i] = xcalloc(u->ngcsites, sizeof(uint32_t));
    for (uint32_t k = 0; k < u->ngcsites; k++) {
      uint32_t n = u->gcsites[k].n;
      if (!n) continue;
      if (n > run_cap) { run_cap = n; run = xrealloc(run, run_cap * 4); }
      memcpy(run, u->gcroots + u->gcsites[k].first, n * 4);
      sort_u32(run, n);
      site_first[i][k] = root_run_intern(&pool, run, n);
    }
  }
  xfree(run);

  bool pool_have = pool.len != 0;
  uint64_t pool_off = rodata_intern(o, pool.p, pool.len, 1);
  xfree(pool.p);

  RoTmp tmp = { NULL, 0, 0 };
  for (uint32_t i = 0; i < g_nunwind; i++) {
    const FuncUnwind *u = &g_unwind[i];
    if (!(u->saved & UW_HAS_LR)) continue;

    tmp.len = 0;
    uint32_t prev = 0;
    for (uint32_t k = 0; k < u->nsites; k++) {
      site_delta(&tmp, m, u->func_index, u->sites[k].pc, &prev);
      rotmp_uleb(&tmp, u->sites[k].first);
      rotmp_uleb(&tmp, u->sites[k].n);
    }
    uw_sites[i] = rodata_intern(o, tmp.p, tmp.len, 1);
    tmp.len = 0;
    for (uint32_t k = 0; k < u->nclauses; k++) {
      rotmp_u32(&tmp, u->clauses[k].kind);
      rotmp_u32(&tmp, u->clauses[k].tag);
      rotmp_u32(&tmp, u->clauses[k].pad);
      rotmp_u32(&tmp, 0);
    }
    uw_clauses[i] = rodata_intern(o, tmp.p, tmp.len, 4);

    tmp.len = 0;
    prev = 0;
    for (uint32_t k = 0; k < u->ngcsites; k++) {
      site_delta(&tmp, m, u->func_index, u->gcsites[k].pc, &prev);
      rotmp_uleb(&tmp, site_first[i] ? site_first[i][k] : 0);
      rotmp_uleb(&tmp, u->gcsites[k].n);
    }
    uw_gcsites[i] = rodata_intern(o, tmp.p, tmp.len, 1);
  }
  xfree(tmp.p);
  for (uint32_t i = 0; i < g_nunwind; i++) xfree(site_first[i]);
  xfree(site_first);
  buf_align(&o->rodata, 8);
  buf_align(&o->data, 8);
  uint64_t unwind_off = o->data.len;
  uint32_t nunwind = 0;
  for (uint32_t i = 0; i < g_nunwind; i++) {
    const FuncUnwind *u = &g_unwind[i];
    if (!(u->saved & UW_HAS_LR)) continue;
    nunwind++;
    func_ptr(o, m, u->func_index);
    buf_u32(&o->data, u->code_len);
    buf_u32(&o->data, u->frame);
    buf_u32(&o->data, u->saved_bytes);
    buf_u32(&o->data, u->exn_slot);
    buf_u32(&o->data, u->fpsave_off);
    buf_u32(&o->data, u->saved);
    buf_u32(&o->data, u->nsites);
    buf_u32(&o->data, u->ngcsites);

    data_ptr(o, 1, uw_sites[i]);
    data_ptr(o, 1, uw_clauses[i]);
    data_ptr(o, 1, uw_gcsites[i]);
  }
  xfree(uw_sites); xfree(uw_clauses);
  xfree(uw_gcsites); xfree(uw_gcroots);

  uint64_t ro_after_uw = o->rodata.len;
  uint64_t *refoffs_off = xcalloc(m->ntypes ? m->ntypes : 1, sizeof(uint64_t));
  uint64_t *fields_off = xcalloc(m->ntypes ? m->ntypes : 1, sizeof(uint64_t));
  uint32_t *nrefs = xcalloc(m->ntypes ? m->ntypes : 1, sizeof(uint32_t));
  buf_align(&o->rodata, 8);

  RoTmp tf = { NULL, 0, 0 };
  for (uint32_t i = 0; i < m->ntypes; i++) {
    tf.len = 0;
    if (!(m->type_entry && !m->type_entry[i]) && m->types[i].kind == CT_STRUCT)
      for (uint32_t j = 0; j < m->types[i].nfields; j++)
        if (vt_is_ref(m->types[i].fields[j].type)) {
          rotmp_u32(&tf, m->types[i].hdr_size + m->types[i].fields[j].offset);
          nrefs[i]++;
        }
    refoffs_off[i] = rodata_intern(o, tf.p, tf.len, 4);
  }
  buf_align(&o->rodata, 8);

  for (uint32_t i = 0; i < m->ntypes; i++) {
    tf.len = 0;
    if (!(m->type_entry && !m->type_entry[i]) && m->types[i].kind == CT_STRUCT)
      for (uint32_t j = 0; j < m->types[i].nfields; j++) {
        rotmp_u32(&tf, m->types[i].hdr_size + m->types[i].fields[j].offset);
        rotmp_u32(&tf, vt_width(m->types[i].fields[j].type));
      }
    fields_off[i] = rodata_intern(o, tf.p, tf.len, 4);
  }
  xfree(tf.p);
  buf_align(&o->rodata, 8);
  uint64_t gctypes_off = o->rodata.len;

  uint32_t ngctypes = 0;
  for (uint32_t i = 0; i < m->ntypes; i++) {
    const CompType *t = &m->types[i];
    bool live = !m->type_entry || m->type_entry[i];
    if (!live) continue;
    ngctypes++;
    uint64_t at = o->rodata.len;
    buf_u32(&o->rodata, (uint32_t)(int32_t)(int64_t)(rtt_off[i] - at));
    buf_u32(&o->rodata, (uint32_t)(int32_t)(int64_t)(refoffs_off[i] - at));
    buf_u32(&o->rodata, (uint32_t)(int32_t)(int64_t)(fields_off[i] - at));

    buf_u32(&o->rodata, t->kind == CT_ARRAY ? vt_width(t->fields[0].type)
                                            : t->body_size);
    buf_u32(&o->rodata, nrefs[i]);
    buf_u32(&o->rodata, t->kind == CT_STRUCT ? t->nfields : 0u);
    buf_u8(&o->rodata, t->kind == CT_ARRAY ? 1u : 0u);
    buf_u8(&o->rodata, t->kind == CT_ARRAY && vt_is_ref(t->fields[0].type)
                         ? 1u : 0u);

    buf_u8(&o->rodata, (uint8_t)(t->kind == CT_FUNC ? GC_HDR_BYTES
                                                    : t->hdr_size));
    buf_u8(&o->rodata, 0);
  }
  xfree(refoffs_off); xfree(fields_off); xfree(nrefs);

  uint64_t ro_after_types = o->rodata.len;
  uint64_t *exnrefs_off = xcalloc(m->ntags ? m->ntags : 1, sizeof(uint64_t));
  uint32_t *exnrefs_n = xcalloc(m->ntags ? m->ntags : 1, sizeof(uint32_t));
  buf_align(&o->rodata, 8);
  {
    RoTmp t = { NULL, 0, 0 };
    for (uint32_t i = 0; i < m->ntags; i++) {
      const CompType *ty = &m->types[m->tags[i].type_index];
      t.len = 0;
      for (uint32_t j = 0; j < ty->nparams; j++)
        if (vt_is_ref(ty->params[j])) {
          rotmp_u32(&t, EXN_VALS_OFF + ARG_STACK_SLOT * j);
          exnrefs_n[i]++;
        }
      exnrefs_off[i] = rodata_intern(o, t.p, t.len, 4);
    }
    xfree(t.p);
  }
  buf_align(&o->rodata, 8);
  uint64_t exntypes_off = o->rodata.len;
  for (uint32_t i = 0; i < m->ntags; i++) {
    const CompType *t = &m->types[m->tags[i].type_index];
    uint64_t at = o->rodata.len;

    buf_u32(&o->rodata, 0);
    buf_u32(&o->rodata, (uint32_t)(int32_t)(int64_t)(exnrefs_off[i] - at));
    buf_u32(&o->rodata, 0);

    buf_u32(&o->rodata, (uint32_t)(EXN_VALS_OFF - GC_HDR_WIDE +
                                   ARG_STACK_SLOT * t->nparams));
    buf_u32(&o->rodata, exnrefs_n[i]);
    buf_u32(&o->rodata, 0);
    buf_u8(&o->rodata, 2u);
    buf_u8(&o->rodata, 0);
    buf_u8(&o->rodata, GC_HDR_WIDE);
    buf_u8(&o->rodata, 0);
  }
  xfree(exnrefs_off); xfree(exnrefs_n);

  buf_align(&o->data, 8);
  uint64_t desc_off = o->data.len;
  obj_intern_sym(o, module_sym(), SYM_GLOBAL_DATA, 2, desc_off);

  buf_u32(&o->data, m->nmemories);
  buf_u32(&o->data, 0); buf_u32(&o->data, 0); buf_u32(&o->data, 0);
  buf_u32(&o->data, m->ntables);
  buf_u32(&o->data, 0); buf_u32(&o->data, 0); buf_u32(&o->data, 0);
  buf_u32(&o->data, m->nglobals);
  buf_u32(&o->data, m->ndatas);
  buf_u32(&o->data, m->nelems);
  buf_u32(&o->data, nexports);
  data_ptr(o, 2, globals_off);
  data_ptr(o, 2, datasegs_off);
  data_ptr(o, 1, blob_off);
  data_ptr(o, 2, elemsegs_off);
  data_ptr(o, 2, entries_off);
  data_ptr(o, 2, exports_off);

  buf_u32(&o->data, m->has_start ? 1u : 0u);
  buf_u32(&o->data, m->has_start ? m->start_func : 0u);
  buf_u32(&o->data, g_guard_pages ? 1u : 0u);
  buf_u32(&o->data, m->nfuncs);
  data_ptr(o, 2, funcrefs_off);
  buf_u32(&o->data, m->nimports);
  buf_u32(&o->data, nimported_funcs);
  data_ptr(o, 2, imports_off);
  buf_u32(&o->data, g_det_profile ? 1u : 0u);
  buf_u32(&o->data, nconsts);
  data_ptr(o, 2, tables_off);
  data_ptr(o, 2, consts_off);
  data_ptr(o, 2, memories_off);
  buf_u32(&o->data, m->ntags);
  buf_u32(&o->data, nunwind);
  data_ptr(o, 2, tags_off);
  data_ptr(o, 2, unwind_off);
  buf_u32(&o->data, ngctypes);
  buf_u32(&o->data, m->max_const_depth);
  data_ptr(o, 1, gctypes_off);
  data_ptr(o, 1, exntypes_off);

  if (ngctypes) {
    uint32_t sym = obj_intern_sym(o, "warmc_rt_alloc", SYM_UNDEF_DATA, -1, 0);
    obj_add_reloc(o, REL_ABS64, 2, o->data.len, sym, 0);
  }
  buf_u64(&o->data, 0);

  if (ngctypes || g_profile) {
    uint32_t sym = obj_intern_sym(o, "warmc_rt_report", SYM_UNDEF_FUNC, -1, 0);
    obj_add_reloc(o, REL_ABS64, 2, o->data.len, sym, 0);
  }
  buf_u64(&o->data, 0);

  if (pool_have) data_ptr(o, 1, pool_off);
  else buf_u64(&o->data, 0);

  if (getenv("WARMC_TABLE_STATS")) {
    uint64_t dead_bytes = 0;
    uint32_t dead_segs = 0;
    for (uint32_t i = 0; i < m->ndatas; i++)
      if (m->data_read && !m->data_read[i]) { dead_segs++; dead_bytes += m->datas[i].len; }
    fprintf(stderr, "tables: %u/%u unwind records (%u leaves dropped), "
                    "%llu rodata bytes shared over %u payloads, "
                    "%u/%u data segments dropped (%llu bytes), "
                    "%u constant-pool instructions shared\n",
            nunwind, g_nunwind, g_nunwind - nunwind,
            (unsigned long long)g_ro_shared, g_nro,
            dead_segs, m->ndatas, (unsigned long long)dead_bytes,
            g_cp_shared);

    uint64_t nsite = 0, nclause = 0, ngcsite = 0, nroot = 0;
    for (uint32_t i = 0; i < g_nunwind; i++) {
      if (!(g_unwind[i].saved & UW_HAS_LR)) continue;
      nsite += g_unwind[i].nsites;     nclause += g_unwind[i].nclauses;
      ngcsite += g_unwind[i].ngcsites; nroot += g_unwind[i].ngcroots;
    }
    fprintf(stderr, "  frames: %llu sites (%llu B), %llu clauses (%llu B), "
                    "%llu safepoints (%llu B), %llu roots (%llu B), "
                    "%u root entries shared\n",
            (unsigned long long)nsite, (unsigned long long)nsite * 12,
            (unsigned long long)nclause, (unsigned long long)nclause * 16,
            (unsigned long long)ngcsite, (unsigned long long)ngcsite * 12,
            (unsigned long long)nroot, (unsigned long long)nroot * 4,
            g_rr_shared);
    uint32_t merged; uint64_t merged_bytes;
    codegen_merge_stats(&merged, &merged_bytes);
    fprintf(stderr, "  %u function bodies merged away (%llu bytes)\n",
            merged, (unsigned long long)merged_bytes);
    uint32_t dropped; uint64_t dropped_bytes;
    codegen_dead_stats(&dropped, &dropped_bytes);
    fprintf(stderr, "  %u function bodies nothing reached after inlining "
                    "(%llu bytes)\n", dropped, (unsigned long long)dropped_bytes);

    static const char *const NAMES[] = {
      "consts", "globals", "datasegs", "entries", "elemsegs", "exports",
      "funcrefs", "imports", "tables", "memories", "tags", "unwind", "desc" };
    const uint64_t offs[] = {
      consts_off, globals_off, datasegs_off, entries_off, elemsegs_off,
      exports_off, funcrefs_off, imports_off, tables_off, memories_off,
      tags_off, unwind_off, desc_off, o->data.len };
    for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
      fprintf(stderr, "  data %-9s %8llu\n", NAMES[i],
              (unsigned long long)(offs[i + 1] - offs[i]));
    fprintf(stderr, "  data %-9s %8llu\n", "TOTAL",
            (unsigned long long)o->data.len);
    static const char *const RONAMES[] = {
      "blob", "expstrings", "displays", "supers+imports", "unwind",
      "type tables", "gctypes", "exntypes" };
    const uint64_t rooffs[] = {
      0, ro_after_blob, ro_after_expstr, ro_after_rtt, ro_after_supers,
      ro_after_uw, gctypes_off, ro_after_types, o->rodata.len };
    for (size_t i = 0; i < sizeof RONAMES / sizeof RONAMES[0]; i++)
      fprintf(stderr, "  rodata %-14s %8llu\n", RONAMES[i],
              (unsigned long long)(rooffs[i + 1] - rooffs[i]));
    fprintf(stderr, "  rodata %-14s %8llu\n", "TOTAL",
            (unsigned long long)o->rodata.len);
  }

  xfree(imp_module); xfree(imp_name);
  xfree(seg_blob); xfree(seg_len); xfree(seg_first);
  xfree(exp_name); xfree(exp_ptypes); xfree(exp_rtypes); xfree(rtt_off);
  xfree(exp_supers); xfree(exp_flags); xfree(exp_heap); xfree(exp_has_supers);
  xfree(g_init); xfree(d_init); xfree(e_init); xfree(t_init); xfree(item_init);
  xfree(d_off); xfree(d_off_const);
}

void codegen_rename_funcs(Module *m, const char *prefix) {
  for (uint32_t i = 0; i < m->nfuncs; i++) {
    char name[80];
    snprintf(name, sizeof name, "%s_%s", prefix, m->funcs[i].sym);
    xfree(m->funcs[i].sym);
    m->funcs[i].sym = xstrndup(name, strlen(name));
  }
}

void codegen_emit_component(const struct LinkPlan *p, ObjBuilder *o) {
  obj_intern_sym(o, g_anchor[1], SYM_LOCAL_DATA, 1, 0);
  obj_intern_sym(o, g_anchor[2], SYM_LOCAL_DATA, 2, 0);

  uint32_t nbind_name = 0;
  for (uint32_t i = 0; i < p->ninsts; i++) nbind_name += p->insts[i].nbinds;
  uint64_t *bind_name = xcalloc(nbind_name ? nbind_name : 1, sizeof(uint64_t));
  uint64_t *bind_host = xcalloc(nbind_name ? nbind_name : 1, sizeof(uint64_t));
  uint32_t bn = 0;
  for (uint32_t i = 0; i < p->ninsts; i++) {
    const LinkInst *in = &p->insts[i];
    for (uint32_t j = 0; j < in->nbinds; j++, bn++) {
      if (in->binds[j].kind == LD_HOST) {
        bind_host[bn] = emit_string(o, in->binds[j].host_name);
        continue;
      }
      const Module *src = p->modules[p->insts[in->binds[j].inst].module].m;
      bind_name[bn] = emit_name(o, src->exports[in->binds[j].exp].name,
                                src->exports[in->binds[j].exp].name_len);
    }
  }
  uint64_t *exp_name = xcalloc(p->nexports ? p->nexports : 1, sizeof(uint64_t));
  uint64_t *exp_core = xcalloc(p->nexports ? p->nexports : 1, sizeof(uint64_t));
  for (uint32_t i = 0; i < p->nexports; i++) {
    const LinkCanon *lc = &p->canons[p->funcs[p->exports[i].func].canon];
    const Module *m = p->modules[p->insts[lc->core.inst].module].m;
    exp_name[i] = emit_string(o, p->exports[i].name);
    exp_core[i] = emit_name(o, m->exports[lc->core.exp].name,
                            m->exports[lc->core.exp].name_len);
  }
  uint64_t *mod_name = xcalloc(p->nmodules ? p->nmodules : 1, sizeof(uint64_t));
  for (uint32_t i = 0; i < p->nmodules; i++)
    mod_name[i] = emit_string(o, p->modules[i].prefix);

  buf_align(&o->data, 8);
  uint64_t *binds_off = xcalloc(p->ninsts ? p->ninsts : 1, sizeof(uint64_t));
  bn = 0;
  for (uint32_t i = 0; i < p->ninsts; i++) {
    binds_off[i] = o->data.len;
    for (uint32_t j = 0; j < p->insts[i].nbinds; j++, bn++) {
      buf_u32(&o->data, p->insts[i].binds[j].inst);
      buf_u32(&o->data, 0);
      if (bind_host[bn]) buf_u64(&o->data, 0);
      else data_ptr(o, 1, bind_name[bn]);
      if (bind_host[bn]) data_ptr(o, 1, bind_host[bn]);
      else buf_u64(&o->data, 0);
    }
  }

  uint64_t insts_off = o->data.len;
  for (uint32_t i = 0; i < p->ninsts; i++) {
    char sym[80];
    snprintf(sym, sizeof sym, "%s_module", p->modules[p->insts[i].module].prefix);
    uint32_t s = obj_intern_sym(o, sym, SYM_UNDEF_DATA, -1, 0);
    obj_add_reloc(o, REL_ABS64, 2, o->data.len, s, 0);
    buf_u64(&o->data, 0);
    data_ptr(o, 2, binds_off[i]);
  }

  uint64_t exports_off = o->data.len;
  for (uint32_t i = 0; i < p->nexports; i++) {
    const LinkCanon *lc = &p->canons[p->funcs[p->exports[i].func].canon];
    data_ptr(o, 1, exp_name[i]);
    buf_u32(&o->data, lc->core.inst);
    buf_u32(&o->data, 0);
    data_ptr(o, 1, exp_core[i]);
  }

  uint64_t desc_off = o->data.len;
  buf_u32(&o->data, p->ninsts);
  buf_u32(&o->data, p->nexports);
  data_ptr(o, 2, insts_off);
  data_ptr(o, 2, exports_off);

  uint64_t ptr_off = o->data.len;
  obj_intern_sym(o, "warmc_component", SYM_GLOBAL_DATA, 2, ptr_off);
  data_ptr(o, 2, desc_off);

  uint64_t mods_off = o->data.len;
  for (uint32_t i = 0; i < p->nmodules; i++) {
    char sym[80];
    snprintf(sym, sizeof sym, "%s_module", p->modules[i].prefix);
    uint32_t s = obj_intern_sym(o, sym, SYM_UNDEF_DATA, -1, 0);
    obj_add_reloc(o, REL_ABS64, 2, o->data.len, s, 0);
    buf_u64(&o->data, 0);
  }
  uint64_t names_off = o->data.len;
  for (uint32_t i = 0; i < p->nmodules; i++) data_ptr(o, 1, mod_name[i]);

  obj_intern_sym(o, "warmc_modules", SYM_GLOBAL_DATA, 2, o->data.len);
  data_ptr(o, 2, mods_off);
  obj_intern_sym(o, "warmc_module_names", SYM_GLOBAL_DATA, 2, o->data.len);
  data_ptr(o, 2, names_off);
  obj_intern_sym(o, "warmc_nmodules", SYM_GLOBAL_DATA, 2, o->data.len);
  buf_u32(&o->data, p->nmodules);
  buf_u32(&o->data, 0);

  xfree(bind_name); xfree(bind_host); xfree(exp_name); xfree(exp_core);
  xfree(mod_name); xfree(binds_off);
}
