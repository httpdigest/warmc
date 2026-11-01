#include "warmc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <sys/resource.h>
#include <sys/mman.h>
#endif
#ifdef __APPLE__
#include <malloc/malloc.h>
#endif
#ifdef _WIN32
/* The private heap a compile's blocks come from on Windows (xheap_begin),
   its calls declared here: windows.h names types after warmc.h's macros
   (VT_I8 among them). */
#define HEAP_ZERO_MEMORY 0x00000008u
__declspec(dllimport) void *__stdcall HeapCreate(unsigned long options, size_t initial,
                                                 size_t max);
__declspec(dllimport) int __stdcall HeapDestroy(void *heap);
__declspec(dllimport) void *__stdcall HeapAlloc(void *heap, unsigned long flags, size_t n);
__declspec(dllimport) void *__stdcall HeapReAlloc(void *heap, unsigned long flags, void *p,
                                                  size_t n);
__declspec(dllimport) int __stdcall HeapFree(void *heap, unsigned long flags, void *p);
#endif

static _Thread_local DiagCatch *g_catch;

int warmc_exit_status;

/* The object a link is made from, while it exists only for that link: an
   error that ends warmc removes it rather than leave it beside the output
   that was never made. */
static char g_temp[4096];

/* path removed where it is a file of its own: never /dev/null, which a
   compile run as root may have been told to write */
void warmc_remove_file(const char *path) {
  struct stat st;
  if (stat(path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG) remove(path);
}

void warmc_temp_output(const char *path) {
  snprintf(g_temp, sizeof g_temp, "%s", path ? path : "");
}

static void remove_temp(void) {
  if (g_temp[0]) remove(g_temp);
}

static _Thread_local DiagHold *g_hold;

DiagCatch *warmc_diag_catch(DiagCatch *c) {
  DiagCatch *was = g_catch;
  g_catch = c;
  return was;
}

void warmc_diag_rethrow(const DiagCatch *c) {
  g_hold = NULL;
  if (g_catch) {
    DiagCatch *to = g_catch;
    to->tag = c->tag;
    to->code = c->code;
    to->msg = c->msg;
    longjmp(to->jb, 1);
  }
  fprintf(stderr, "warmc: %s: %s\n", c->tag, c->msg);
  remove_temp();
  warmc_exit_status = c->code;
  exit(c->code);
}

__attribute__((noreturn))
static void diag(const char *tag, int code, const char *fmt, va_list ap) {
  g_hold = NULL;
  if (g_catch) {
    DiagCatch *c = g_catch;
    va_list aq;
    va_copy(aq, ap);
    int n = vsnprintf(NULL, 0, fmt, aq);
    va_end(aq);
    c->msg = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (c->msg) {
      vsnprintf(c->msg, (size_t)n + 1, fmt, ap);
      c->tag = tag;
      c->code = code;
      longjmp(c->jb, 1);
    }
  }
  fprintf(stderr, "warmc: %s: ", tag);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  remove_temp();
  warmc_exit_status = code;
  exit(code);
}

typedef struct {
  _Atomic uint32_t next, w;
  uint32_t n;
  void (*fn)(void *, uint32_t, uint32_t);
  void *ctx;
} ParFor;

static void *par_worker(void *arg) {
  ParFor *p = arg;
  uint32_t w = atomic_fetch_add(&p->w, 1);
  for (;;) {
    uint32_t i = atomic_fetch_add(&p->next, 1);
    if (i >= p->n) break;
    p->fn(p->ctx, i, w);
  }
  xcache_release();
  return NULL;
}

void warmc_par_for(uint32_t n, uint32_t nthreads,
                   void (*fn)(void *ctx, uint32_t i, uint32_t w), void *ctx) {
  ParFor p = { 0, 0, n, fn, ctx };
  if (nthreads <= 1) {
    for (uint32_t i = 0; i < n; i++) fn(ctx, i, 0);
    return;
  }
  pthread_t th[64];
  uint32_t started = 0;
  for (uint32_t i = 0; i < nthreads && i < 64; i++)
    if (pthread_create(&th[started], NULL, par_worker, &p) == 0) started++;
  if (!started) par_worker(&p);
  for (uint32_t i = 0; i < started; i++) pthread_join(th[i], NULL);
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

void warmc_hold_open(DiagHold *h) {
  h->outer = g_hold;
  h->msg = NULL;
  g_hold = h;
}

char *warmc_hold_close(DiagHold *h) {
  g_hold = h->outer;
  return h->msg;
}

void warmc_invalid_held(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  if (!g_hold) diag("invalid", EXIT_INVALID, fmt, ap);
  if (!g_hold->msg) {
    va_list aq;
    va_copy(aq, ap);
    int n = vsnprintf(NULL, 0, fmt, aq);
    va_end(aq);
    g_hold->msg = xmalloc(n > 0 ? (size_t)n + 1 : 1);
    g_hold->msg[0] = 0;
    if (n > 0) vsnprintf(g_hold->msg, (size_t)n + 1, fmt, ap);
  }
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

/* ---- a compile's own heap ------------------------------------------------
   Between xheap_begin and xheap_end, what xmalloc takes from the system is
   the compile's own, and xheap_end gives it all back at once: a JIT's
   compile (src/jit.c) runs in a process that goes on, where malloc keeps
   what is freed for the process to use again -- macOS's kept a hundred
   megabytes of a large compile's, with or without a zone of its own, which
   shares the default zone's memory.  The size classes malloc served are
   carved from a buddy heap of chunks the compile maps (below), which gives
   back what it no longer uses as it goes; xheap_end unmaps whatever is
   left, a diagnostic's half-built module and all.  Windows has a heap of its own for it.  No block may outlive the
   heap: the compiler frees what it allocates, its caches included
   (compile_release).  Outside one, blocks are malloc's as they always were. */
#if defined(_WIN32)
static void *g_xheap;
#define SYS_MALLOC(n) (g_xheap ? HeapAlloc(g_xheap, 0, (n)) : malloc(n))
#define SYS_CALLOC(n) (g_xheap ? HeapAlloc(g_xheap, HEAP_ZERO_MEMORY, (n)) : calloc(1, (n)))
#define SYS_REALLOC(p, n) (g_xheap ? HeapReAlloc(g_xheap, 0, (p), (n)) : realloc((p), (n)))
#define SYS_FREE(p) (g_xheap ? (void)HeapFree(g_xheap, 0, (p)) : free(p))
#define XSCOPE 0
#else
#define SYS_MALLOC(n) malloc(n)
#define SYS_CALLOC(n) calloc(1, (n))
#define SYS_REALLOC(p, n) realloc((p), (n))
#define SYS_FREE(p) free(p)
#define XSCOPE g_xscope
static int g_xscope;
#endif
/* which heap: a thread's lists and chunk of an older one are dropped */
static unsigned g_xgen = 1;

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
   through a call, and every allocation and free touches both.  gen is the
   heap its blocks are of. */
typedef struct { void *cache[XB_MAX + 1]; size_t held; unsigned gen; uint32_t count[XB_MAX + 1]; } XCache;
static _Thread_local XCache g_xc;

#if !defined(_WIN32)
/* ---- the buddy heap a compile's blocks come from ---------------------------
   Chunks of 1 MB, each aligned to its size, carved into blocks of the size
   classes by halving; a block freed merges with its buddy, the other half
   of the block it was split from, while that is free too, so that memory a
   compile frees as one size serves the next of any other.  Lists kept per
   class, as malloc's are, hold each class's own high-water: for Go's
   net/http server they came to 94 MB of chunks where no more than 50 MB
   were ever live at once.  A chunk nothing is allocated from goes back to
   the system there and then, past the XSPARE kept for the next.  Its first
   32 KB are a byte per 32-byte unit of it: a block's order where one
   starts, XFREE added while it is free.  The threads' lists stay in front,
   at most XHOLD_SCOPE each, and take from the heap and give back to it a
   batch at a time, under its one lock. */
#define XB_CHUNK 20
#define XCHUNK ((size_t)1 << XB_CHUNK)
#define XNUNITS (XCHUNK >> XB_MIN)
#define XMETA_ORDER 15
#define XFREE 0x80u
/* what a thread holds of a heap's before it gives back half, the classes
   holding most first -- once a megabyte, all of it, kept the lock busy, and
   a quarter of a megabyte took the compile twice as long -- and what it
   takes of a class at once */
#define XHOLD_SCOPE ((size_t)512 << 10)
#define XBATCH_BYTES ((size_t)64 << 10)
#define XBATCH_MAX 16u
_Static_assert(XNUNITS == ((size_t)1 << XMETA_ORDER), "the tags fill the chunk's first block");
_Static_assert(XB_MAP <= XB_CHUNK, "a chunk holds a block of every class it serves");

/* A heap: its free blocks by order, its chunks, and how many of them
   nothing is allocated from.  There are two.  The second is for what
   outlives the work around it -- a function's code and tables once made,
   a body rewritten for the module, what the effects scan keeps of each
   function (xmalloc_long, xkeep) -- which, made among a worker's
   short-lived blocks, held their chunks long after the rest of them was
   free: Go's net/http server's compile kept tens of megabytes of chunks for
   a few megabytes of such blocks. */
typedef struct XFreeBlk { struct XFreeBlk *prev, *next; } XFreeBlk;
typedef struct XBHeap {
  pthread_mutex_t lock;
  XFreeBlk *bfree[XB_CHUNK];
  char *chunks;
  unsigned empty;
} XBHeap;
static XBHeap g_xh[2] = { { PTHREAD_MUTEX_INITIALIZER, { 0 }, 0, 0 },
                          { PTHREAD_MUTEX_INITIALIZER, { 0 }, 0, 0 } };

/* a chunk's own links, count and heap, in the tags of its first block,
   whose own tag is the first byte and which no other block starts in */
typedef struct XChunkHdr {
  uint8_t tag0[8];
  size_t used;
  char *prev, *next;
  XBHeap *heap;
} XChunkHdr;
_Static_assert(sizeof(XChunkHdr) <= ((size_t)1 << (XMETA_ORDER - XB_MIN)),
               "a chunk's header is within its first block's tags");

/* chunks nothing is allocated from, kept for the next ones: as many as
   XSPARE, so that a chunk emptied and wanted again is not mapped again */
#define XSPARE 8

static void *xmap(size_t len);

#define XNEXT(p) (*(void **)((char *)(p) + XHDR))

static char *chunk_of(const void *p) { return (char *)((uintptr_t)p & ~(uintptr_t)(XCHUNK - 1)); }
static size_t unit_of(const char *base, const void *p) { return (size_t)((const char *)p - base) >> XB_MIN; }

static void bpush(XBHeap *H, void *p, unsigned k) {
  XFreeBlk *b = p;
  b->prev = NULL;
  b->next = H->bfree[k];
  if (b->next) b->next->prev = b;
  H->bfree[k] = b;
}

static void bunlink(XBHeap *H, void *p, unsigned k) {
  XFreeBlk *b = p;
  if (b->prev) b->prev->next = b->next;
  else H->bfree[k] = b->next;
  if (b->next) b->next->prev = b->prev;
}

static void chunk_new(XBHeap *H) {
  /* twice the size, then trimmed to the aligned chunk inside */
  char *m = xmap(2 * XCHUNK);
  char *base = (char *)(((uintptr_t)m + XCHUNK - 1) & ~(uintptr_t)(XCHUNK - 1));
  if (base > m) munmap(m, (size_t)(base - m));
  if (base + XCHUNK < m + 2 * XCHUNK) munmap(base + XCHUNK, (size_t)(m + 2 * XCHUNK - (base + XCHUNK)));
  uint8_t *tag = (uint8_t *)base;
  XChunkHdr *ch = (XChunkHdr *)base;
  tag[0] = XMETA_ORDER;
  ch->used = 0;
  ch->heap = H;
  H->empty++;
  ch->prev = NULL;
  ch->next = H->chunks;
  if (H->chunks) ((XChunkHdr *)H->chunks)->prev = base;
  H->chunks = base;
  for (unsigned k = XMETA_ORDER; k < XB_CHUNK; k++) {
    char *blk = base + ((size_t)1 << k);
    tag[unit_of(base, blk)] = (uint8_t)(k | XFREE);
    bpush(H, blk, k);
  }
}

/* a chunk nothing is allocated from: its free blocks are the ones it
   started with, which leave the lists, and it goes back to the system */
static void chunk_release(XBHeap *H, char *base) {
  for (unsigned k = XMETA_ORDER; k < XB_CHUNK; k++) bunlink(H, base + ((size_t)1 << k), k);
  XChunkHdr *ch = (XChunkHdr *)base;
  if (ch->prev) ((XChunkHdr *)ch->prev)->next = ch->next;
  else H->chunks = ch->next;
  if (ch->next) ((XChunkHdr *)ch->next)->prev = ch->prev;
  munmap(base, XCHUNK);
}

static void *buddy_alloc(XBHeap *H, unsigned k) {
  unsigned m = k;
  while (m < XB_CHUNK && !H->bfree[m]) m++;
  if (m == XB_CHUNK) {
    chunk_new(H);
    m = k;
    while (!H->bfree[m]) m++;
  }
  XFreeBlk *blk = H->bfree[m];
  bunlink(H, blk, m);
  char *base = chunk_of(blk);
  uint8_t *tag = (uint8_t *)base;
  while (m > k) {
    m--;
    char *hi = (char *)blk + ((size_t)1 << m);
    tag[unit_of(base, hi)] = (uint8_t)(m | XFREE);
    bpush(H, hi, m);
  }
  tag[unit_of(base, blk)] = (uint8_t)k;
  XChunkHdr *ch = (XChunkHdr *)base;
  if (!ch->used) H->empty--;
  ch->used += (size_t)1 << k;
  return blk;
}

static void buddy_free(void *p, unsigned k) {
  char *base = chunk_of(p);
  uint8_t *tag = (uint8_t *)base;
  XChunkHdr *ch = (XChunkHdr *)base;
  XBHeap *H = ch->heap;
  ch->used -= (size_t)1 << k;
  size_t u = unit_of(base, p);
  tag[u] = 0;
  while (k < XB_CHUNK - 1) {
    size_t bu = u ^ ((size_t)1 << (k - XB_MIN));
    if (tag[bu] != (k | XFREE)) break;
    bunlink(H, base + (bu << XB_MIN), k);
    tag[bu] = 0;
    if (bu < u) u = bu;
    k++;
  }
  tag[u] = (uint8_t)(k | XFREE);
  bpush(H, base + (u << XB_MIN), k);
  if (!ch->used) {
    if (H->empty < XSPARE) H->empty++;
    else chunk_release(H, base);
  }
}

/* a block of class b for the thread, and a batch more onto its list */
static void *xbuddy_take(XCache *xc, unsigned b) {
  unsigned n = (unsigned)(XBATCH_BYTES >> b);
  if (n > XBATCH_MAX) n = XBATCH_MAX;
  if (n < 1) n = 1;
  XBHeap *H = &g_xh[0];
  pthread_mutex_lock(&H->lock);
  void *h = buddy_alloc(H, b);
  for (unsigned i = 1; i < n; i++) {
    void *q = buddy_alloc(H, b);
    XNEXT(q) = xc->cache[b];
    xc->cache[b] = q;
    xc->held += (size_t)1 << b;
    xc->count[b]++;
  }
  pthread_mutex_unlock(&H->lock);
  return h;
}

/* the thread's lists back to the heap until it holds no more than keep,
   the classes holding most first */
static void xbuddy_give(XCache *xc, size_t keep) {
  XBHeap *H = &g_xh[0];
  pthread_mutex_lock(&H->lock);
  while (xc->held > keep) {
    unsigned big = 0;
    size_t most = 0;
    for (unsigned b = XB_MIN; b < XB_MAP; b++)
      if (((size_t)xc->count[b] << b) > most) { most = (size_t)xc->count[b] << b; big = b; }
    if (!most) break;
    while (xc->cache[big] && xc->held > keep) {
      void *p = xc->cache[big];
      xc->cache[big] = XNEXT(p);
      xc->count[big]--;
      xc->held -= (size_t)1 << big;
      buddy_free(p, big);
    }
  }
  pthread_mutex_unlock(&H->lock);
}

/* a block of the second heap's, freed: at once, under its lock */
static bool xbuddy_free_long(void *h, unsigned b) {
  XBHeap *H = ((XChunkHdr *)chunk_of(h))->heap;
  if (H != &g_xh[1]) return false;
  pthread_mutex_lock(&H->lock);
  buddy_free(h, b);
  pthread_mutex_unlock(&H->lock);
  return true;
}
#endif

/* a thread's lists, emptied when they are of another heap than the one in
   use: their blocks went with it */
static XCache *xc_now(void) {
  XCache *xc = &g_xc;
  if (xc->gen != g_xgen) {
    memset(xc->cache, 0, sizeof xc->cache);
    memset(xc->count, 0, sizeof xc->count);
    xc->held = 0;
    xc->gen = g_xgen;
  }
  return xc;
}

void xheap_begin(void) {
  xcache_release();
  g_xgen++;
#if defined(_WIN32)
  if (!g_xheap) g_xheap = HeapCreate(0, 0, 0);
#else
  g_xscope = 1;
#endif
}

void xheap_end(void) {
  xcache_release();
#if defined(_WIN32)
  if (g_xheap) HeapDestroy(g_xheap);
  g_xheap = NULL;
#else
  g_xscope = 0;
  /* what is left -- a block a diagnostic's half-built module held -- goes
     with its chunk */
  for (int i = 0; i < 2; i++) {
    XBHeap *H = &g_xh[i];
    pthread_mutex_lock(&H->lock);
    while (H->chunks) {
      char *next = ((XChunkHdr *)H->chunks)->next;
      munmap(H->chunks, XCHUNK);
      H->chunks = next;
    }
    memset(H->bfree, 0, sizeof H->bfree);
    H->empty = 0;
    pthread_mutex_unlock(&H->lock);
  }
#endif
  g_xgen++;
}

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
  XCache *xc = xc_now();
  if (b && b < XB_MAP && xc->cache[b]) {
    h = xc->cache[b];
    xc->cache[b] = *(void **)((char *)h + XHDR);
    xc->held -= (size_t)1 << b;
    xc->count[b]--;
  } else if (XSCOPE && b && b < XB_MAP) {
#if !defined(_WIN32)
    h = xbuddy_take(xc, b);
#endif
  } else if (b >= XB_MAP) {
    h = xmap((size_t)1 << b);
  } else if (!b && XB_MAP <= XB_MAX) {
    h = xmap(need);
    h->size = n;
    h->bucket = need;
    return (char *)h + XHDR;
  } else {
    h = SYS_MALLOC(b ? ((size_t)1 << b) : need);
    if (!h) warmc_fatal("out of memory");
  }
  h->size = n;
  h->bucket = b;
  return (char *)h + XHDR;
}

/* xmalloc's, from the heap of what outlives the work around it, in a
   compile's heap (above); xmalloc's own outside one */
void *xmalloc_long(size_t n) {
#if !defined(_WIN32)
  if (XSCOPE) {
    if (!n) n = 1;
    size_t need = n + XHDR;
    unsigned b = need > n ? xbucket(need) : 0;
    if (b && b < XB_MAP) {
      XBHeap *H = &g_xh[1];
      pthread_mutex_lock(&H->lock);
      XHdr *h = buddy_alloc(H, b);
      pthread_mutex_unlock(&H->lock);
      h->size = n;
      h->bucket = b;
      return (char *)h + XHDR;
    }
  }
#endif
  return xmalloc(n);
}

/* p, its first n bytes kept and the rest let go, moved to the heap of what
   outlives the work around it: for a block grown to its final size among
   short-lived ones.  Outside a compile's heap, p as it is. */
void *xkeep(void *p, size_t n) {
#if !defined(_WIN32)
  if (XSCOPE && p) {
    void *q = xmalloc_long(n);
    if (n) memcpy(q, p, n);
    xfree(p);
    return q;
  }
#endif
  return p;
}

void xfree(void *p) {
  if (!p) return;
  XHdr *h = (XHdr *)((char *)p - XHDR);
  if (!h->bucket) { SYS_FREE(h); return; }
  if (h->bucket > XB_MAX) { xunmap(h, h->bucket); return; }
  size_t sz = (size_t)1 << h->bucket;
  if (h->bucket >= XB_MAP) { xunmap(h, sz); return; }
#if !defined(_WIN32)
  if (XSCOPE && xbuddy_free_long(h, h->bucket)) return;
#endif
  XCache *xc = xc_now();
#if !defined(_WIN32)
  /* a heap's blocks go back to it, the thread's lists whole once they hold
     more than a little */
  if (XSCOPE && xc->held + sz > XHOLD_SCOPE) xbuddy_give(xc, XHOLD_SCOPE / 2);
#endif
  if (!XSCOPE && xc->held + sz > XHOLD_MAX) { SYS_FREE(h); return; }
  xc->held += sz;
  xc->count[h->bucket]++;
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
    XHdr *h = SYS_CALLOC(bytes + XHDR);
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
    XHdr *g = SYS_REALLOC(h, need);
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

/* settings only a program reads, which main.c's compile cache leaves out of
   its key: a compile that reads one after all is not kept */
bool warmc_runtime_setting(const char *name) {
  return !strncmp(name, "WARMC_GC_", 9) || !strcmp(name, "WARMC_RANDOM_SEED") ||
         !strcmp(name, "WARMC_TIMEOUT") || !strcmp(name, "WARMC_RUN_TIMEOUT") ||
         !strcmp(name, "WARMC_CACHE");
}
static atomic_bool warmc_read_runtime_setting;
bool warmc_runtime_setting_read(void) { return atomic_load(&warmc_read_runtime_setting); }

const char *warmc_getenv(const char *name) {
  /* only a name of the program's settings can be one: WARMC_ then G, R, T
     or C -- the compiler asks for its own switches in its passes */
  char c = name[0] && name[1] && name[2] && name[3] && name[4] && name[5] ? name[6] : 0;
  if ((c == 'G' || c == 'R' || c == 'T' || c == 'C') && warmc_runtime_setting(name))
    atomic_store(&warmc_read_runtime_setting, true);
  pthread_once(&g_wenv_once, wenv_init);
  if (!g_nwenv) return NULL;
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
  XCache *xc = xc_now();
#if !defined(_WIN32)
  /* a heap's blocks back to it, for the threads still compiling */
  if (XSCOPE) xbuddy_give(xc, 0);
#endif
  for (unsigned b = 0; b <= XB_MAX; b++) {
    void *p = xc->cache[b];
    /* in a heap of its own, a block is its chunk's, which goes with it */
    while (p && !XSCOPE) {
      void *next = *(void **)((char *)p + XHDR);
      SYS_FREE(p);
      p = next;
    }
    xc->cache[b] = NULL;
    xc->count[b] = 0;
  }
  xc->held = 0;
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

  static _Thread_local char buf[4][48];
  static _Thread_local unsigned next;
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
  /* text, rodata and data take relocations; the custom sections (3) are
     the module's bytes as they were, and no writer has a table for them */
  if (section < 0 || section > 2)
    warmc_fatal("internal: a relocation in section %d, which takes none", section);
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
  if (!n) return;
  buf_reserve(b, n); memcpy(b->data + b->len, p, n); b->len += n;
}
void buf_zero(Buf *b, size_t n) {
  buf_reserve(b, n); memset(b->data + b->len, 0, n); b->len += n;
}
void buf_align(Buf *b, size_t align) {
  while (b->len % align) buf_u8(b, 0);
}
void buf_free(Buf *b) { xfree(b->data); b->data = NULL; b->len = b->cap = 0; }
