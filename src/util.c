#include "warmc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__attribute__((noreturn))
static void diag(const char *tag, int code, const char *fmt, va_list ap) {
  fprintf(stderr, "warmc: %s: ", tag);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  exit(code);
}

void warmc_fatal(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  diag("error", EXIT_FATAL, fmt, ap);
  va_end(ap);
}

void warmc_malformed(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  diag("malformed", EXIT_MALFORMED, fmt, ap);
  va_end(ap);
}

void warmc_invalid(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  diag("invalid", EXIT_INVALID, fmt, ap);
  va_end(ap);
}

typedef struct { size_t size; size_t bucket; } XHdr;

#define XHDR   ((size_t)16)
#define XB_MIN 5
#define XB_MAX 22

_Static_assert(sizeof(XHdr) <= XHDR, "allocation header must fit its padding");

static _Thread_local void *g_xcache[XB_MAX + 1];
static _Thread_local size_t g_xheld;

#define XHOLD_MAX ((size_t)1 << 20)

static unsigned xbucket(size_t need) {
  if (need > ((size_t)1 << XB_MAX)) return 0;
  unsigned b = XB_MIN;
  while (((size_t)1 << b) < need) b++;
  return b;
}

void *xmalloc(size_t n) {
  if (!n) n = 1;
  size_t need = n + XHDR;
  if (need < n) warmc_fatal("out of memory");
  unsigned b = xbucket(need);
  XHdr *h;
  if (b && g_xcache[b]) {
    h = g_xcache[b];
    g_xcache[b] = *(void **)((char *)h + XHDR);
    g_xheld -= (size_t)1 << b;
  } else {
    h = malloc(b ? ((size_t)1 << b) : need);
    if (!h) warmc_fatal("out of memory");
  }
  h->size = n;
  h->bucket = b;
  return (char *)h + XHDR;
}

void xfree(void *p) {
  if (!p) return;
  XHdr *h = (XHdr *)((char *)p - XHDR);
  if (!h->bucket) { free(h); return; }
  size_t sz = (size_t)1 << h->bucket;
  if (g_xheld + sz > XHOLD_MAX) { free(h); return; }
  g_xheld += sz;
  *(void **)((char *)h + XHDR) = g_xcache[h->bucket];
  g_xcache[h->bucket] = h;
}

void *xcalloc(size_t n, size_t sz) {
  if (!n) n = 1;
  if (!sz) sz = 1;
  if (n > (size_t)-1 / sz) warmc_fatal("out of memory");
  size_t bytes = n * sz;
  if (bytes + XHDR > ((size_t)1 << XB_MAX)) {
    XHdr *h = calloc(1, bytes + XHDR);
    if (!h) warmc_fatal("out of memory");
    h->size = bytes;
    h->bucket = 0;
    return (char *)h + XHDR;
  }
  void *p = xmalloc(bytes);
  memset(p, 0, bytes);
  return p;
}

void *xrealloc(void *p, size_t n) {
  if (!p) return xmalloc(n);
  if (!n) n = 1;
  XHdr *h = (XHdr *)((char *)p - XHDR);
  size_t cap = h->bucket ? ((size_t)1 << h->bucket) - XHDR : h->size;
  if (n <= cap) {
    h->size = n;
    return p;
  }

  /* Callers grow arrays through here assuming realloc's amortization; a
     block past the largest class must keep extending in place, or a linear
     growth pattern turns into a full copy per step. */
  if (!h->bucket) {
    size_t need = n + XHDR;
    if (need < n) warmc_fatal("out of memory");
    XHdr *g = realloc(h, need);
    if (!g) warmc_fatal("out of memory");
    g->size = n;
    return (char *)g + XHDR;
  }
  void *q = xmalloc(n);
  memcpy(q, p, h->size);
  xfree(p);
  return q;
}

void xcache_release(void) {
  for (unsigned b = 0; b <= XB_MAX; b++) {
    void *p = g_xcache[b];
    while (p) {
      void *next = *(void **)((char *)p + XHDR);
      free(p);
      p = next;
    }
    g_xcache[b] = NULL;
  }
  g_xheld = 0;
}
char *xstrndup(const char *s, size_t n) {
  char *p = xmalloc(n + 1);
  memcpy(p, s, n);
  p[n] = 0;
  return p;
}

static const char *ht_name(HeapType h) {
  switch (h) {
    case HT_FUNC: return "func";
    case HT_EXTERN: return "extern";
    case HT_ANY: return "any";
    case HT_EQ: return "eq";
    case HT_I31: return "i31";
    case HT_STRUCT: return "struct";
    case HT_ARRAY: return "array";
    case HT_EXN: return "exn";
    case HT_NONE: return "none";
    case HT_NOFUNC: return "nofunc";
    case HT_NOEXTERN: return "noextern";
    case HT_NOEXN: return "noexn";
  }
  return NULL;
}

const char *vt_name(ValType t) {
  switch (t) {
    case VT_I32: return "i32";
    case VT_I64: return "i64";
    case VT_F32: return "f32";
    case VT_F64: return "f64";
    case VT_V128: return "v128";

    case VT_I8: return "i8";
    case VT_I16: return "i16";
    case VT_FUNCREF: return "funcref";
    case VT_EXTERNREF: return "externref";
    case VT_BOT: return "bot";
  }
  if (!vt_is_ref(t)) return "?";

  static char buf[4][48];
  static unsigned next;
  char *b = buf[next++ & 3];
  const char *h = ht_name(vt_heap(t));
  if (h) snprintf(b, sizeof buf[0], "(ref %s%s)", vt_nullable(t) ? "null " : "", h);
  else snprintf(b, sizeof buf[0], "(ref %s%u)", vt_nullable(t) ? "null " : "",
                vt_type_index(t));
  return b;
}

static uint32_t sym_hash(const char *s) {
  uint32_t h = 2166136261u;
  for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
  return h;
}

static void sym_ix_put(ObjBuilder *o, uint32_t i) {
  uint32_t mask = o->sym_ix_cap - 1;
  uint32_t h = sym_hash(o->syms[i].name) & mask;
  while (o->sym_ix[h]) h = (h + 1) & mask;
  o->sym_ix[h] = i + 1;
}

static void sym_ix_grow(ObjBuilder *o) {
  uint32_t want = o->sym_ix_cap ? o->sym_ix_cap * 2 : 256;
  xfree(o->sym_ix);
  o->sym_ix = xcalloc(want, sizeof(uint32_t));
  o->sym_ix_cap = want;
  for (uint32_t i = 0; i < o->nsyms; i++) sym_ix_put(o, i);
}

uint32_t obj_add_sym(ObjBuilder *o, const char *name, SymKind kind, int section, uint64_t value) {
  if (o->frozen)
    warmc_fatal("internal: symbol %s created during parallel codegen; add it "
                "to codegen_prewarm_symbols", name);
  if (o->nsyms == o->cap_syms) {
    o->cap_syms = o->cap_syms ? o->cap_syms * 2 : 64;
    o->syms = xrealloc(o->syms, o->cap_syms * sizeof(Sym));
  }
  Sym *s = &o->syms[o->nsyms];
  memset(s, 0, sizeof *s);
  s->name = xstrndup(name, strlen(name));
  s->kind = kind;
  s->section = section;
  s->value = value;
  uint32_t idx = o->nsyms++;

  if ((uint64_t)o->nsyms * 3 >= (uint64_t)o->sym_ix_cap * 2) sym_ix_grow(o);
  else sym_ix_put(o, idx);
  return idx;
}

uint32_t obj_find_sym(ObjBuilder *o, const char *name) {
  if (!o->sym_ix_cap) return UINT32_MAX;
  uint32_t mask = o->sym_ix_cap - 1;
  uint32_t h = sym_hash(name) & mask;
  while (o->sym_ix[h]) {
    uint32_t i = o->sym_ix[h] - 1;
    if (!strcmp(o->syms[i].name, name)) return i;
    h = (h + 1) & mask;
  }
  return UINT32_MAX;
}

uint32_t obj_intern_sym(ObjBuilder *o, const char *name, SymKind kind, int section, uint64_t value) {
  uint32_t i = obj_find_sym(o, name);
  if (i != UINT32_MAX) {

    if (o->syms[i].section < 0 && section >= 0) {
      o->syms[i].kind = kind;
      o->syms[i].section = section;
      o->syms[i].value = value;
    }
    return i;
  }
  return obj_add_sym(o, name, kind, section, value);
}

void obj_add_reloc(ObjBuilder *o, RelKind kind, int section, uint64_t off, uint32_t sym, int64_t addend) {
  if (o->nrelocs == o->cap_relocs) {
    o->cap_relocs = o->cap_relocs ? o->cap_relocs * 2 : 64;
    o->relocs = xrealloc(o->relocs, o->cap_relocs * sizeof(Reloc));
  }
  Reloc *r = &o->relocs[o->nrelocs++];
  r->kind = kind;
  r->section = section;
  r->offset = off;
  r->sym = sym;
  r->addend = addend;
}
