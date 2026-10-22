// Hash tables written in C: an open-addressing table with linear probing,
// tombstones on delete and rehashing on growth, keyed by 64-bit integers
// through a multiplicative hash; a separately chained table keyed by strings
// through FNV-1a, used for word frequency counting over generated text; and
// a Robin Hood table whose displacement statistics are reported.  Every
// operation's outcome (inserted, updated, found, missing, deleted) is
// cross-checked against a plain array that holds the truth.
//
// This is the load/store/compare churn of real programs: hashes built from
// multiplies and xor-shifts, masks to the table size, probe loops with early
// exits, and strcmp on collisions.  Statistics are printed every few thousand
// operations so a lost entry is located near where it went missing.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t mix64(uint64_t x) {
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdull;
  x ^= x >> 33;
  x *= 0xc4ceb9fe1a85ec53ull;
  x ^= x >> 33;
  return x;
}

enum { EMPTY, FULL, TOMB };
typedef struct { uint64_t key; int64_t val; uint8_t state; } Slot;
typedef struct { Slot *s; size_t cap, len, tombs; size_t probes, rehashes; } Open;

static void open_init(Open *t, size_t cap) {
  t->s = calloc(cap, sizeof(Slot));
  t->cap = cap;
  t->len = t->tombs = t->probes = 0;
}
static void open_put(Open *t, uint64_t k, int64_t v);
static void open_grow(Open *t) {
  Slot *old = t->s;
  size_t oc = t->cap;
  size_t keep_probes = t->probes, keep_re = t->rehashes;
  open_init(t, oc * 2);
  for (size_t i = 0; i < oc; i++)
    if (old[i].state == FULL) open_put(t, old[i].key, old[i].val);
  t->probes = keep_probes;
  t->rehashes = keep_re + 1;
  free(old);
}
static void open_put(Open *t, uint64_t k, int64_t v) {
  if ((t->len + t->tombs + 1) * 4 > t->cap * 3) open_grow(t);
  size_t m = t->cap - 1, i = (size_t)mix64(k) & m, tomb = (size_t)-1;
  for (;;) {
    t->probes++;
    Slot *s = &t->s[i];
    if (s->state == EMPTY) {
      if (tomb != (size_t)-1) {
        s = &t->s[tomb];
        t->tombs--;
      }
      s->key = k;
      s->val = v;
      s->state = FULL;
      t->len++;
      return;
    }
    if (s->state == TOMB) {
      if (tomb == (size_t)-1) tomb = i;
    } else if (s->key == k) {
      s->val = v;
      return;
    }
    i = (i + 1) & m;
  }
}
static Slot *open_find(Open *t, uint64_t k) {
  size_t m = t->cap - 1, i = (size_t)mix64(k) & m;
  for (;;) {
    t->probes++;
    Slot *s = &t->s[i];
    if (s->state == EMPTY) return NULL;
    if (s->state == FULL && s->key == k) return s;
    i = (i + 1) & m;
  }
}
static int open_del(Open *t, uint64_t k) {
  Slot *s = open_find(t, k);
  if (!s) return 0;
  s->state = TOMB;
  t->len--;
  t->tombs++;
  return 1;
}

static void open_addressing(void) {
  printf("-- open addressing vs reference\n");
  enum { KEYS = 3000 };
  static int64_t truth[KEYS];
  static uint8_t present[KEYS];
  Open t;
  t.rehashes = 0;
  open_init(&t, 16);
  uint32_t r = 1;
  int mismatches = 0;
  long found = 0, missing = 0, deleted = 0;
  for (int op = 1; op <= 30000; op++) {
    r = r * 1664525u + 1013904223u;
    int idx = (int)((r >> 8) % KEYS);
    uint64_t key = (uint64_t)idx * 0x9e3779b97f4a7c15ull + 17;
    int kind = (int)(r >> 29);
    if (kind < 4) {
      int64_t v = (int64_t)op * 31 - idx;
      open_put(&t, key, v);
      truth[idx] = v;
      present[idx] = 1;
    } else if (kind < 6) {
      Slot *s = open_find(&t, key);
      if (s) found++;
      else missing++;
      if ((s != NULL) != present[idx] || (s && s->val != truth[idx])) mismatches++;
    } else {
      int d = open_del(&t, key);
      deleted += d;
      if (d != present[idx]) mismatches++;
      present[idx] = 0;
    }
    if (op % 5000 == 0)
      printf("op %d: len=%zu cap=%zu tombs=%zu rehashes=%zu found=%ld missing=%ld deleted=%ld mismatches=%d\n", op,
             t.len, t.cap, t.tombs, t.rehashes, found, missing, deleted, mismatches);
  }
  size_t live = 0;
  int64_t vsum = 0;
  for (int i = 0; i < KEYS; i++)
    if (present[i]) {
      live++;
      Slot *s = open_find(&t, (uint64_t)i * 0x9e3779b97f4a7c15ull + 17);
      if (!s || s->val != truth[i]) mismatches++;
      else vsum += s->val;
    }
  printf("final live=%zu len=%zu vsum=%lld mismatches=%d probes=%zu\n", live, t.len, (long long)vsum, mismatches,
         t.probes);
  free(t.s);
}

typedef struct Entry { char *word; int count; struct Entry *next; } Entry;
typedef struct { Entry **b; size_t nb, n; } Chained;
static uint32_t fnv1a(const char *s) {
  uint32_t h = 2166136261u;
  while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
  return h;
}
static Entry *ch_get(Chained *t, const char *w, int create) {
  uint32_t h = fnv1a(w) % (uint32_t)t->nb;
  for (Entry *e = t->b[h]; e; e = e->next)
    if (!strcmp(e->word, w)) return e;
  if (!create) return NULL;
  Entry *e = malloc(sizeof *e);
  e->word = strdup(w);
  e->count = 0;
  e->next = t->b[h];
  t->b[h] = e;
  t->n++;
  return e;
}
static int by_count(const void *a, const void *b) {
  const Entry *x = *(const Entry *const *)a, *y = *(const Entry *const *)b;
  if (x->count != y->count) return y->count - x->count;
  return strcmp(x->word, y->word);
}

static void chaining(void) {
  printf("-- chained table: word counts\n");
  static const char *syll[] = {"ka", "lo", "mi", "ne", "su", "ta", "ri", "po", "ve", "zu"};
  Chained t = {calloc(257, sizeof(Entry *)), 257, 0};
  uint32_t r = 4242;
  long total = 0;
  for (int i = 0; i < 20000; i++) {
    char w[16] = "";
    r = r * 1103515245u + 12345u;
    int len = 1 + (int)((r >> 16) % 3);
    uint32_t x = r;
    for (int k = 0; k < len; k++) {
      // skewed choice so that some words are much more frequent
      int s = (int)((x >> (k * 5)) & 31);
      strcat(w, syll[s < 10 ? s : (s < 20 ? 0 : (s < 26 ? 1 : 2))]);
    }
    ch_get(&t, w, 1)->count++;
    total++;
  }
  Entry **all = malloc(t.n * sizeof *all);
  size_t n = 0, longest = 0, empty = 0;
  for (size_t b = 0; b < t.nb; b++) {
    size_t chain = 0;
    for (Entry *e = t.b[b]; e; e = e->next) {
      all[n++] = e;
      chain++;
    }
    if (chain > longest) longest = chain;
    if (!chain) empty++;
  }
  qsort(all, n, sizeof *all, by_count);
  printf("distinct=%zu total=%ld longest chain=%zu empty buckets=%zu\n", t.n, total, longest, empty);
  for (size_t i = 0; i < 15 && i < n; i++) printf("%-8s %d\n", all[i]->word, all[i]->count);
  printf("...\n");
  for (size_t i = n - 5; i < n; i++) printf("%-8s %d\n", all[i]->word, all[i]->count);
  const char *probe[] = {"ka", "kaka", "zuzuzu", "nope", "lomi", "x"};
  for (int i = 0; i < 6; i++) {
    Entry *e = ch_get(&t, probe[i], 0);
    printf("lookup %s -> %d\n", probe[i], e ? e->count : -1);
  }
  for (size_t i = 0; i < n; i++) {
    free(all[i]->word);
    free(all[i]);
  }
  free(all);
  free(t.b);
}

typedef struct { uint32_t key; uint32_t val; int8_t dist; } RH;
static void robin_hood(void) {
  printf("-- robin hood hashing\n");
  enum { CAP = 1024 };
  static RH tab[CAP];
  for (int i = 0; i < CAP; i++) tab[i].dist = -1;
  int inserted = 0, maxdist = 0;
  for (uint32_t k = 1; inserted < 900; k++) {
    uint32_t key = k * 2654435761u;
    RH cur = {key, k, 0};
    uint32_t i = (uint32_t)(mix64(key) & (CAP - 1));
    for (;;) {
      if (tab[i].dist < 0) {
        tab[i] = cur;
        break;
      }
      if (tab[i].dist < cur.dist) {
        RH t = tab[i];
        tab[i] = cur;
        cur = t;
      }
      cur.dist++;
      if (cur.dist > maxdist) maxdist = cur.dist;
      i = (i + 1) & (CAP - 1);
    }
    inserted++;
  }
  int hist[16] = {0};
  long distsum = 0;
  for (int i = 0; i < CAP; i++)
    if (tab[i].dist >= 0) {
      hist[tab[i].dist < 15 ? tab[i].dist : 15]++;
      distsum += tab[i].dist;
    }
  printf("inserted=%d maxdist=%d mean*1000=%ld\n", inserted, maxdist, distsum * 1000 / inserted);
  for (int d = 0; d < 16; d++)
    if (hist[d]) printf("dist %2d: %d\n", d, hist[d]);
  // lookups with early termination on displacement
  int hits = 0, misses = 0, steps = 0;
  for (uint32_t k = 1; k <= 1000; k++) {
    uint32_t key = k * 2654435761u;
    uint32_t i = (uint32_t)(mix64(key) & (CAP - 1));
    int d = 0, hit = 0;
    while (tab[i].dist >= d) {
      steps++;
      if (tab[i].key == key) {
        hit = tab[i].val == k;
        break;
      }
      d++;
      i = (i + 1) & (CAP - 1);
    }
    if (hit) hits++;
    else misses++;
  }
  printf("lookups: hits=%d misses=%d steps=%d\n", hits, misses, steps);
}

int main(void) {
  open_addressing();
  chaining();
  robin_hood();
  return 0;
}
