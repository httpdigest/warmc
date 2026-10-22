#include "warmc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#ifndef _WIN32
#include <sys/resource.h>
#include <sys/mman.h>
#endif

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

/* Blocks of 256 KB and up are mapped for themselves and unmapped when freed.
   The system allocator keeps what it is given back for reuse, so a compile's
   footprint was its passes' peaks added up rather than the largest of them:
   a third of it for a large function, at no measurable cost in time.  A
   mapped block past the size classes keeps its mapping's length in bucket,
   which is then above XB_MAX; bucket 0 is a block from malloc. */
#ifdef _WIN32
#define XB_MAP (XB_MAX + 1)
#else
#define XB_MAP 18
#endif

static void *xmap(size_t len) {
#ifdef _WIN32
  (void)len;
  return NULL;
#else
  void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (p == MAP_FAILED) warmc_fatal("out of memory");
  return p;
#endif
}

static void xunmap(void *p, size_t len) {
#ifdef _WIN32
  (void)p; (void)len;
#else
  munmap(p, len);
#endif
}

_Static_assert(sizeof(XHdr) <= XHDR, "allocation header must fit its padding");

/* One thread-local for the cache and its count: on macOS each is reached
   through a call, and every allocation and free touches both. */
typedef struct { void *cache[XB_MAX + 1]; size_t held; } XCache;
static _Thread_local XCache g_xc;

#define XHOLD_MAX ((size_t)1 << 20)

static unsigned xbucket(size_t need) {
  if (need > ((size_t)1 << XB_MAX)) return 0;
  if (need <= ((size_t)1 << XB_MIN)) return XB_MIN;
  return 64u - (unsigned)__builtin_clzll((unsigned long long)need - 1);
}

void *xmalloc(size_t n) {
  if (!n) n = 1;
  size_t need = n + XHDR;
  if (need < n) warmc_fatal("out of memory");
  unsigned b = xbucket(need);
  XHdr *h;
  XCache *xc = &g_xc;
  if (b && b < XB_MAP && xc->cache[b]) {
    h = xc->cache[b];
    xc->cache[b] = *(void **)((char *)h + XHDR);
    xc->held -= (size_t)1 << b;
  } else if (b >= XB_MAP) {
    h = xmap((size_t)1 << b);
  } else if (!b && XB_MAP <= XB_MAX) {
    h = xmap(need);
    h->size = n;
    h->bucket = need;
    return (char *)h + XHDR;
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
  if (h->bucket > XB_MAX) { xunmap(h, h->bucket); return; }
  size_t sz = (size_t)1 << h->bucket;
  if (h->bucket >= XB_MAP) { xunmap(h, sz); return; }
  XCache *xc = &g_xc;
  if (xc->held + sz > XHOLD_MAX) { free(h); return; }
  xc->held += sz;
  *(void **)((char *)h + XHDR) = xc->cache[h->bucket];
  xc->cache[h->bucket] = h;
}

size_t xmalloc_fit(size_t n) {
  if (n + XHDR < n) return n;
  unsigned b = xbucket(n + XHDR);
  return b ? ((size_t)1 << b) - XHDR : n;
}

void *xcalloc(size_t n, size_t sz) {
  if (!n) n = 1;
  if (!sz) sz = 1;
  if (n > (size_t)-1 / sz) warmc_fatal("out of memory");
  size_t bytes = n * sz;
  if (bytes + XHDR > ((size_t)1 << XB_MAX) && XB_MAP > XB_MAX) {
    XHdr *h = calloc(1, bytes + XHDR);
    if (!h) warmc_fatal("out of memory");
    h->size = bytes;
    h->bucket = 0;
    return (char *)h + XHDR;
  }
  void *p = xmalloc(bytes);
  /* a fresh mapping is zero already, and its pages stay untouched */
  if (xbucket(bytes + XHDR) < XB_MAP && bytes + XHDR <= ((size_t)1 << XB_MAX))
    memset(p, 0, bytes);
  return p;
}

void *xrealloc(void *p, size_t n) {
  if (!p) return xmalloc(n);
  if (!n) n = 1;
  XHdr *h = (XHdr *)((char *)p - XHDR);
  size_t cap = h->bucket > XB_MAX ? h->bucket - XHDR
             : h->bucket ? ((size_t)1 << h->bucket) - XHDR : h->size;
  if (n <= cap) {
    h->size = n;
    return p;
  }
  if (h->bucket > XB_MAX) {
    void *q = xmalloc(n);
    memcpy(q, p, h->size);
    xfree(p);
    return q;
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

/* The WARMC_ switches, copied out of the environment once: getenv takes a
   lock and walks every variable, and the switches are asked about per
   function and per pass, which came to 2% of a compile with none set.
   Nothing in the compiler changes its environment. */
static const char **g_wenv;
static size_t g_nwenv;
static pthread_once_t g_wenv_once = PTHREAD_ONCE_INIT;

static void wenv_init(void) {
#ifdef _WIN32
  char **env = _environ;
#else
  extern char **environ;
  char **env = environ;
#endif
  size_t n = 0;
  for (char **e = env; e && *e; e++)
    if (!strncmp(*e, "WARMC_", 6)) n++;
  g_wenv = malloc((n ? n : 1) * sizeof(char *));
  if (!g_wenv) return;
  for (char **e = env; e && *e; e++)
    if (!strncmp(*e, "WARMC_", 6)) g_wenv[g_nwenv++] = *e;
}

const char *warmc_getenv(const char *name) {
  pthread_once(&g_wenv_once, wenv_init);
  size_t len = strlen(name);
  for (size_t i = 0; i < g_nwenv; i++)
    if (!strncmp(g_wenv[i], name, len) && g_wenv[i][len] == '=')
      return g_wenv[i] + len + 1;
  return NULL;
}

double peak_rss_mb(void) {
#ifdef _WIN32
  return 0;
#else
  struct rusage ru;
  if (getrusage(RUSAGE_SELF, &ru) != 0) return 0;
#ifdef __APPLE__
  return (double)ru.ru_maxrss / (1024.0 * 1024.0);
#else
  return (double)ru.ru_maxrss / 1024.0;
#endif
#endif
}

void xcache_release(void) {
  for (unsigned b = 0; b <= XB_MAX; b++) {
    void *p = g_xc.cache[b];
    while (p) {
      void *next = *(void **)((char *)p + XHDR);
      free(p);
      p = next;
    }
    g_xc.cache[b] = NULL;
  }
  g_xc.held = 0;
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

void buf_reserve(Buf *b, size_t extra) {
  if (b->len + extra <= b->cap) return;
  size_t cap = b->cap ? b->cap : 256;
  while (cap < b->len + extra) cap *= 2;
  uint8_t *p = xrealloc(b->data, cap);
  if (!p) { fprintf(stderr, "warmc: out of memory\n"); exit(1); }
  b->data = p;
  b->cap = cap;
}
void buf_u8(Buf *b, uint8_t v) { buf_reserve(b, 1); b->data[b->len++] = v; }
void buf_u16(Buf *b, uint16_t v) { buf_u8(b, v & 0xff); buf_u8(b, v >> 8); }
void buf_u32(Buf *b, uint32_t v) { buf_u16(b, v & 0xffff); buf_u16(b, v >> 16); }
void buf_u64(Buf *b, uint64_t v) { buf_u32(b, (uint32_t)v); buf_u32(b, (uint32_t)(v >> 32)); }
void buf_bytes(Buf *b, const void *p, size_t n) {
  buf_reserve(b, n); memcpy(b->data + b->len, p, n); b->len += n;
}
void buf_zero(Buf *b, size_t n) {
  buf_reserve(b, n); memset(b->data + b->len, 0, n); b->len += n;
}
void buf_align(Buf *b, size_t align) {
  while (b->len % align) buf_u8(b, 0);
}
void buf_free(Buf *b) { xfree(b->data); b->data = NULL; b->len = b->cap = 0; }
