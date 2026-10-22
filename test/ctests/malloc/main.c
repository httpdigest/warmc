// The heap: malloc/free of many sizes in interleaved orders, calloc's zeroing
// (checked after the same memory was dirtied and freed), realloc growing and
// shrinking while keeping the prefix, a doubling dynamic array, a singly and a
// doubly linked list with insertion, deletion and reversal, a binary tree
// built and freed node by node, aligned_alloc / posix_memalign alignment, and
// a pool of fixed-size blocks recycled through a free list.
//
// wasi-libc's malloc is dlmalloc inside the module, growing linear memory
// with memory.grow when it runs out, so this exercises the allocator's bins
// and boundary tags as compiled code, plus a few memory.grow calls.  Only
// contents and counts are printed, never addresses, since an address is the
// allocator's business; alignment is printed as a remainder.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rng = 12345;
static uint32_t next(void) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}

static void churn(void) {
  printf("-- malloc/free churn\n");
  enum { SLOTS = 200 };
  unsigned char *slot[SLOTS] = {0};
  size_t size[SLOTS] = {0};
  unsigned long long checksum = 0;
  int allocs = 0, frees = 0, verified = 0;
  for (int round = 0; round < 6000; round++) {
    int i = (int)(next() % SLOTS);
    if (slot[i]) {
      // verify the fill pattern survived everything else's allocations
      for (size_t k = 0; k < size[i]; k++)
        if (slot[i][k] != (unsigned char)(i + k)) {
          printf("corruption in slot %d at %zu\n", i, k);
          break;
        }
      verified++;
      checksum = checksum * 31 + size[i];
      free(slot[i]);
      slot[i] = NULL;
      frees++;
    } else {
      uint32_t r = next();
      size_t n = (r & 3) == 0 ? (r >> 8) % 5000 + 1 : (r >> 8) % 100 + 1;
      slot[i] = malloc(n);
      size[i] = n;
      for (size_t k = 0; k < n; k++) slot[i][k] = (unsigned char)(i + k);
      allocs++;
    }
    if (round % 1000 == 999) printf("round %d: allocs=%d frees=%d verified=%d checksum=%llu\n", round + 1, allocs, frees, verified, checksum);
  }
  for (int i = 0; i < SLOTS; i++) free(slot[i]);
}

static void zeroing(void) {
  printf("-- calloc after dirty frees\n");
  for (int round = 0; round < 5; round++) {
    size_t n = 1000 + (size_t)round * 3000;
    char *dirty = malloc(n);
    memset(dirty, 0xab, n);
    free(dirty);
    int *z = calloc(n / sizeof(int), sizeof(int));
    size_t nonzero = 0;
    for (size_t i = 0; i < n / sizeof(int); i++) nonzero += z[i] != 0;
    printf("calloc(%zu ints): nonzero=%zu\n", n / sizeof(int), nonzero);
    free(z);
  }
  // calloc overflow must fail rather than wrap.  Kept in a volatile so that
  // clang cannot elide the call and assume it succeeded.
  static void *volatile keep;
  keep = calloc((size_t)-1 / 2, 4);
  void *p = keep;
  printf("calloc overflow -> %s\n", p ? "non-null" : "NULL");
  free(p);
}

static void reallocs(void) {
  printf("-- realloc grow and shrink\n");
  size_t n = 4;
  int *a = malloc(n * sizeof *a);
  for (size_t i = 0; i < n; i++) a[i] = (int)(i * i);
  size_t sizes[] = {7, 100, 3, 1000, 50, 20000, 10, 1};
  for (int s = 0; s < 8; s++) {
    size_t m = sizes[s];
    a = realloc(a, m * sizeof *a);
    size_t keep = n < m ? n : m;
    int ok = 1;
    for (size_t i = 0; i < keep; i++)
      if (a[i] != (int)(i * i)) ok = 0;
    for (size_t i = keep; i < m; i++) a[i] = (int)(i * i);
    printf("realloc %zu -> %zu: prefix %s, last=%d\n", n, m, ok ? "kept" : "LOST", a[m - 1]);
    n = m;
  }
  free(a);
  char *s = realloc(NULL, 5);  // realloc(NULL) is malloc
  strcpy(s, "abcd");
  s = realloc(s, 9);
  strcat(s, "efgh");
  printf("string grown: %s\n", s);
  free(s);
}

typedef struct { int *data; size_t len, cap; } Vec;
static void vec_push(Vec *v, int x) {
  if (v->len == v->cap) {
    v->cap = v->cap ? v->cap * 2 : 1;
    v->data = realloc(v->data, v->cap * sizeof *v->data);
  }
  v->data[v->len++] = x;
}

static void dynarray(void) {
  printf("-- doubling array\n");
  Vec v = {0};
  long long sum = 0;
  for (int i = 0; i < 100000; i++) {
    vec_push(&v, i * 3 - 7);
    if ((i & (i + 1)) == 0) printf("len=%zu cap=%zu\n", v.len, v.cap);
  }
  for (size_t i = 0; i < v.len; i++) sum += v.data[i];
  printf("final len=%zu cap=%zu sum=%lld v[777]=%d\n", v.len, v.cap, sum, v.data[777]);
  free(v.data);
}

typedef struct DNode { int key; struct DNode *prev, *next; } DNode;

static void lists(void) {
  printf("-- doubly linked list\n");
  DNode *head = NULL, *tail = NULL;
  for (int i = 0; i < 20; i++) {
    DNode *n = malloc(sizeof *n);
    n->key = (i * 7) % 20;
    n->next = NULL;
    n->prev = tail;
    if (tail) tail->next = n;
    else head = n;
    tail = n;
  }
  // delete every node whose key is a multiple of 3
  for (DNode *n = head; n;) {
    DNode *nx = n->next;
    if (n->key % 3 == 0) {
      if (n->prev) n->prev->next = n->next;
      else head = n->next;
      if (n->next) n->next->prev = n->prev;
      else tail = n->prev;
      free(n);
    }
    n = nx;
  }
  printf("forward:");
  for (DNode *n = head; n; n = n->next) printf(" %d", n->key);
  printf("\nbackward:");
  for (DNode *n = tail; n; n = n->prev) printf(" %d", n->key);
  // insertion sort by relinking
  DNode *sorted = NULL;
  for (DNode *n = head; n;) {
    DNode *nx = n->next;
    if (!sorted || n->key < sorted->key) {
      n->next = sorted;
      n->prev = NULL;
      if (sorted) sorted->prev = n;
      sorted = n;
    } else {
      DNode *c = sorted;
      while (c->next && c->next->key <= n->key) c = c->next;
      n->next = c->next;
      n->prev = c;
      if (c->next) c->next->prev = n;
      c->next = n;
    }
    n = nx;
  }
  printf("\nsorted:");
  int count = 0;
  for (DNode *n = sorted; n;) {
    DNode *nx = n->next;
    printf(" %d", n->key);
    free(n);
    count++;
    n = nx;
  }
  printf(" (%d nodes freed)\n", count);
}

typedef struct Tree { int key; struct Tree *l, *r; } Tree;
static Tree *insert(Tree *t, int k) {
  if (!t) {
    t = malloc(sizeof *t);
    t->key = k;
    t->l = t->r = NULL;
    return t;
  }
  if (k < t->key) t->l = insert(t->l, k);
  else if (k > t->key) t->r = insert(t->r, k);
  return t;
}
static int height(const Tree *t) {
  if (!t) return 0;
  int a = height(t->l), b = height(t->r);
  return 1 + (a > b ? a : b);
}
static int count_free(Tree *t) {
  if (!t) return 0;
  int n = 1 + count_free(t->l) + count_free(t->r);
  free(t);
  return n;
}
static void inorder(const Tree *t, int *out, int *n) {
  if (!t) return;
  inorder(t->l, out, n);
  out[(*n)++] = t->key;
  inorder(t->r, out, n);
}

static void trees(void) {
  printf("-- heap-allocated trees\n");
  for (int round = 0; round < 4; round++) {
    Tree *t = NULL;
    for (int i = 0; i < 500; i++) t = insert(t, (int)(next() % 1000));
    int keys[1000], n = 0;
    inorder(t, keys, &n);
    int sorted = 1;
    for (int i = 1; i < n; i++)
      if (keys[i - 1] >= keys[i]) sorted = 0;
    int h = height(t);
    int freed = count_free(t);  // separately: argument order is unspecified
    printf("round %d: distinct=%d height=%d sorted=%d min=%d max=%d freed=%d\n", round, n,
           h, sorted, keys[0], keys[n - 1], freed);
  }
}

static void alignment(void) {
  printf("-- aligned allocation\n");
  size_t aligns[] = {8, 16, 32, 64, 128, 256, 4096};
  for (int i = 0; i < 7; i++) {
    void *p = aligned_alloc(aligns[i], aligns[i] * 3);
    void *q = NULL;
    int rc = posix_memalign(&q, aligns[i], 100);
    printf("align %zu: aligned_alloc mod=%u posix_memalign rc=%d mod=%u\n", aligns[i],
           (unsigned)((uintptr_t)p % aligns[i]), rc, (unsigned)((uintptr_t)q % aligns[i]));
    free(p);
    free(q);
  }
  void *m = malloc(1);
  printf("malloc(1) mod 8=%u mod 16=%u\n", (unsigned)((uintptr_t)m % 8), (unsigned)((uintptr_t)m % 16));
  free(m);
  free(NULL);
  printf("free(NULL) ok\n");
}

typedef union Block { union Block *next; char payload[48]; } Block;
static void pool(void) {
  printf("-- fixed-size pool\n");
  Block *arena = malloc(64 * sizeof(Block));
  Block *freelist = NULL;
  for (int i = 63; i >= 0; i--) {
    arena[i].next = freelist;
    freelist = &arena[i];
  }
  Block *live[64];
  int nlive = 0;
  unsigned hits = 0;
  for (int step = 0; step < 2000; step++) {
    if ((next() % 3) && freelist) {
      Block *b = freelist;
      freelist = b->next;
      snprintf(b->payload, sizeof b->payload, "block-%d", step);
      live[nlive++] = b;
    } else if (nlive) {
      int k = (int)(next() % (unsigned)nlive);
      Block *b = live[k];
      hits += (unsigned)strlen(b->payload) * (unsigned)(b - arena);
      live[k] = live[--nlive];
      b->next = freelist;
      freelist = b;
    }
  }
  int nfree = 0;
  for (Block *b = freelist; b; b = b->next) nfree++;
  printf("live=%d free=%d hits=%u\n", nlive, nfree, hits);
  free(arena);
}

int main(void) {
  churn();
  zeroing();
  reallocs();
  dynarray();
  lists();
  trees();
  alignment();
  pool();
  return 0;
}
