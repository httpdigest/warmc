// Tree and heap data structures: an AVL tree with insertion, deletion,
// rotations, height and balance invariants checked after every batch, rank
// and range queries; a binary min-heap priority queue driving Dijkstra's
// shortest paths on a generated grid graph; a trie over generated words with
// prefix counting; union-find with path compression and union by rank
// counting connected components; and a segment tree answering range-sum and
// range-min queries under point updates, checked against brute force.
//
// Pointer-chasing structures with recursive invariants: a rotation that
// updates the wrong child pointer or height breaks an invariant that the next
// check reports, and an off-by-one in a heap's sift changes a path length.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rs = 7;
static uint32_t rnd(void) {
  rs ^= rs << 13;
  rs ^= rs >> 17;
  rs ^= rs << 5;
  return rs;
}

typedef struct Avl { int key, height, size; struct Avl *l, *r; } Avl;
static int h(const Avl *n) { return n ? n->height : 0; }
static int sz(const Avl *n) { return n ? n->size : 0; }
static void upd(Avl *n) {
  n->height = 1 + (h(n->l) > h(n->r) ? h(n->l) : h(n->r));
  n->size = 1 + sz(n->l) + sz(n->r);
}
static Avl *rot_r(Avl *y) {
  Avl *x = y->l;
  y->l = x->r;
  x->r = y;
  upd(y);
  upd(x);
  return x;
}
static Avl *rot_l(Avl *x) {
  Avl *y = x->r;
  x->r = y->l;
  y->l = x;
  upd(x);
  upd(y);
  return y;
}
static int rotations;
static Avl *balance(Avl *n) {
  upd(n);
  int b = h(n->l) - h(n->r);
  if (b > 1) {
    if (h(n->l->l) < h(n->l->r)) { n->l = rot_l(n->l); rotations++; }
    rotations++;
    return rot_r(n);
  }
  if (b < -1) {
    if (h(n->r->r) < h(n->r->l)) { n->r = rot_r(n->r); rotations++; }
    rotations++;
    return rot_l(n);
  }
  return n;
}
static Avl *avl_insert(Avl *n, int key) {
  if (!n) {
    n = malloc(sizeof *n);
    n->key = key;
    n->l = n->r = NULL;
    n->height = n->size = 1;
    return n;
  }
  if (key < n->key) n->l = avl_insert(n->l, key);
  else if (key > n->key) n->r = avl_insert(n->r, key);
  else return n;
  return balance(n);
}
static Avl *avl_remove_min(Avl *n, Avl **min) {
  if (!n->l) {
    *min = n;
    return n->r;
  }
  n->l = avl_remove_min(n->l, min);
  return balance(n);
}
static Avl *avl_delete(Avl *n, int key) {
  if (!n) return NULL;
  if (key < n->key) n->l = avl_delete(n->l, key);
  else if (key > n->key) n->r = avl_delete(n->r, key);
  else {
    Avl *l = n->l, *r = n->r;
    free(n);
    if (!r) return l;
    Avl *m;
    r = avl_remove_min(r, &m);
    m->l = l;
    m->r = r;
    return balance(m);
  }
  return balance(n);
}
static int avl_check(const Avl *n, long lo, long hi) {
  if (!n) return 1;
  if (n->key <= lo || n->key >= hi) return 0;
  int b = h(n->l) - h(n->r);
  if (b < -1 || b > 1) return 0;
  if (n->height != 1 + (h(n->l) > h(n->r) ? h(n->l) : h(n->r))) return 0;
  if (n->size != 1 + sz(n->l) + sz(n->r)) return 0;
  return avl_check(n->l, lo, n->key) && avl_check(n->r, n->key, hi);
}
static int avl_rank(const Avl *n, int key) {  // number of keys < key
  int r = 0;
  while (n) {
    if (key <= n->key) n = n->l;
    else {
      r += sz(n->l) + 1;
      n = n->r;
    }
  }
  return r;
}
static int avl_kth(const Avl *n, int k) {  // 0-based
  while (n) {
    int ls = sz(n->l);
    if (k < ls) n = n->l;
    else if (k == ls) return n->key;
    else {
      k -= ls + 1;
      n = n->r;
    }
  }
  return -1;
}
static void avl_free(Avl *n) {
  if (!n) return;
  avl_free(n->l);
  avl_free(n->r);
  free(n);
}

static void avl(void) {
  printf("-- AVL tree\n");
  Avl *root = NULL;
  static uint8_t in[5000];
  for (int batch = 0; batch < 8; batch++) {
    for (int i = 0; i < 600; i++) {
      int k = (int)(rnd() % 5000);
      if (batch % 3 == 2 && in[k]) {
        root = avl_delete(root, k);
        in[k] = 0;
      } else if (!in[k]) {
        root = avl_insert(root, k);
        in[k] = 1;
      }
    }
    int count = 0;
    for (int k = 0; k < 5000; k++) count += in[k];
    printf("batch %d: size=%d (truth %d) height=%d valid=%d rotations=%d", batch, sz(root), count, h(root),
           avl_check(root, -1, 5000), rotations);
    printf(" rank(2500)=%d kth(0)=%d kth(mid)=%d kth(last)=%d\n", avl_rank(root, 2500), avl_kth(root, 0),
           avl_kth(root, sz(root) / 2), avl_kth(root, sz(root) - 1));
  }
  for (int lo = 0; lo < 5000; lo += 1250)
    printf("keys in [%d,%d): %d\n", lo, lo + 1000, avl_rank(root, lo + 1000) - avl_rank(root, lo));
  avl_free(root);
}

typedef struct { int dist, node; } HeapItem;
typedef struct { HeapItem *a; int n, cap; } Heap;
static void heap_push(Heap *hp, HeapItem it) {
  if (hp->n == hp->cap) {
    hp->cap = hp->cap ? hp->cap * 2 : 16;
    hp->a = realloc(hp->a, (size_t)hp->cap * sizeof *hp->a);
  }
  int i = hp->n++;
  while (i > 0) {
    int p = (i - 1) / 2;
    if (hp->a[p].dist < it.dist || (hp->a[p].dist == it.dist && hp->a[p].node <= it.node)) break;
    hp->a[i] = hp->a[p];
    i = p;
  }
  hp->a[i] = it;
}
static HeapItem heap_pop(Heap *hp) {
  HeapItem top = hp->a[0], last = hp->a[--hp->n];
  int i = 0;
  for (;;) {
    int c = 2 * i + 1;
    if (c >= hp->n) break;
    if (c + 1 < hp->n && (hp->a[c + 1].dist < hp->a[c].dist ||
                          (hp->a[c + 1].dist == hp->a[c].dist && hp->a[c + 1].node < hp->a[c].node)))
      c++;
    if (last.dist < hp->a[c].dist || (last.dist == hp->a[c].dist && last.node <= hp->a[c].node)) break;
    hp->a[i] = hp->a[c];
    i = c;
  }
  if (hp->n) hp->a[i] = last;
  return top;
}

static void dijkstra(void) {
  printf("-- heap-based Dijkstra on a grid\n");
  enum { W = 30, H = 20 };
  static int cost[H][W], dist[H][W];
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      cost[y][x] = 1 + (int)(rnd() % 9);
      dist[y][x] = 1 << 30;
    }
  Heap hp = {0};
  dist[0][0] = cost[0][0];
  heap_push(&hp, (HeapItem){cost[0][0], 0});
  int pops = 0, maxheap = 0;
  while (hp.n) {
    if (hp.n > maxheap) maxheap = hp.n;
    HeapItem it = heap_pop(&hp);
    pops++;
    int x = it.node % W, y = it.node / W;
    if (it.dist > dist[y][x]) continue;
    static const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
    for (int d = 0; d < 4; d++) {
      int nx = x + dx[d], ny = y + dy[d];
      if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
      int nd = it.dist + cost[ny][nx];
      if (nd < dist[ny][nx]) {
        dist[ny][nx] = nd;
        heap_push(&hp, (HeapItem){nd, ny * W + nx});
      }
    }
  }
  printf("pops=%d maxheap=%d corner=%d\n", pops, maxheap, dist[H - 1][W - 1]);
  for (int y = 0; y < H; y += 4) {
    for (int x = 0; x < W; x += 3) printf("%4d", dist[y][x]);
    printf("\n");
  }
  // heap sort of random items through the same heap
  for (int i = 0; i < 200; i++) heap_push(&hp, (HeapItem){(int)(rnd() % 100), i});
  int prev = -1, ordered = 1;
  long long sig = 0;
  while (hp.n) {
    HeapItem it = heap_pop(&hp);
    if (it.dist < prev) ordered = 0;
    prev = it.dist;
    sig = sig * 7 + it.node;
    sig %= 1000000007;
  }
  printf("heap drain ordered=%d sig=%lld\n", ordered, sig);
  free(hp.a);
}

typedef struct Trie { struct Trie *kid[26]; int words, prefixes; } Trie;
static void trie_add(Trie *t, const char *w) {
  for (; *w; w++) {
    int c = *w - 'a';
    if (!t->kid[c]) t->kid[c] = calloc(1, sizeof(Trie));
    t = t->kid[c];
    t->prefixes++;
  }
  t->words++;
}
static const Trie *trie_find(const Trie *t, const char *p) {
  for (; *p && t; p++) t = t->kid[*p - 'a'];
  return t;
}
static int trie_nodes(const Trie *t) {
  int n = 1;
  for (int i = 0; i < 26; i++)
    if (t->kid[i]) n += trie_nodes(t->kid[i]);
  return n;
}
static void trie_free(Trie *t) {
  for (int i = 0; i < 26; i++)
    if (t->kid[i]) trie_free(t->kid[i]);
  free(t);
}
static void trie_list(const Trie *t, char *buf, int depth, int *budget) {
  if (*budget <= 0) return;
  if (t->words) {
    buf[depth] = 0;
    printf(" %s(%d)", buf, t->words);
    (*budget)--;
  }
  for (int i = 0; i < 26; i++)
    if (t->kid[i]) {
      buf[depth] = (char)('a' + i);
      trie_list(t->kid[i], buf, depth + 1, budget);
    }
}

static void trie(void) {
  printf("-- trie\n");
  Trie *root = calloc(1, sizeof(Trie));
  for (int i = 0; i < 3000; i++) {
    char w[12];
    int len = 2 + (int)(rnd() % 6);
    for (int k = 0; k < len; k++) w[k] = "abcdeilmnorst"[rnd() % 13];
    w[len] = 0;
    trie_add(root, w);
  }
  printf("nodes=%d\n", trie_nodes(root));
  const char *pre[] = {"a", "st", "rom", "zz", "ee", "tin", "o"};
  for (int i = 0; i < 7; i++) {
    const Trie *n = trie_find(root, pre[i]);
    printf("prefix %s: %d words below, %d exact\n", pre[i], n ? n->prefixes : 0, n ? n->words : 0);
  }
  char buf[16];
  int budget = 25;
  printf("suffixes under m:");
  trie_list(trie_find(root, "m"), buf, 0, &budget);
  printf("\n");
  trie_free(root);
}

static int parent[2000], rnk[2000];
static int find(int x) {
  while (parent[x] != x) {
    parent[x] = parent[parent[x]];
    x = parent[x];
  }
  return x;
}
static int unite(int a, int b) {
  a = find(a);
  b = find(b);
  if (a == b) return 0;
  if (rnk[a] < rnk[b]) { int t = a; a = b; b = t; }
  parent[b] = a;
  if (rnk[a] == rnk[b]) rnk[a]++;
  return 1;
}

static void union_find(void) {
  printf("-- union-find\n");
  for (int i = 0; i < 2000; i++) parent[i] = i, rnk[i] = 0;
  int comps = 2000;
  for (int e = 1; e <= 2400; e++) {
    int a = (int)(rnd() % 2000), b = (int)(rnd() % 2000);
    comps -= unite(a, b);
    if (e % 400 == 0) {
      int maxsize = 0;
      static int size[2000];
      memset(size, 0, sizeof size);
      for (int i = 0; i < 2000; i++) size[find(i)]++;
      for (int i = 0; i < 2000; i++)
        if (size[i] > maxsize) maxsize = size[i];
      int maxrank = 0;
      for (int i = 0; i < 2000; i++)
        if (rnk[i] > maxrank) maxrank = rnk[i];
      printf("edges=%d components=%d largest=%d maxrank=%d\n", e, comps, maxsize, maxrank);
    }
  }
}

enum { SN = 256 };
static long long seg_sum[2 * SN];
static int seg_min[2 * SN];
static int base[SN];
static void seg_set(int i, int v) {
  base[i] = v;
  i += SN;
  seg_sum[i] = v;
  seg_min[i] = v;
  for (i /= 2; i >= 1; i /= 2) {
    seg_sum[i] = seg_sum[2 * i] + seg_sum[2 * i + 1];
    seg_min[i] = seg_min[2 * i] < seg_min[2 * i + 1] ? seg_min[2 * i] : seg_min[2 * i + 1];
  }
}
static void seg_query(int l, int r, long long *s, int *m) {  // [l, r)
  *s = 0;
  *m = 1 << 30;
  for (l += SN, r += SN; l < r; l /= 2, r /= 2) {
    if (l & 1) {
      *s += seg_sum[l];
      if (seg_min[l] < *m) *m = seg_min[l];
      l++;
    }
    if (r & 1) {
      r--;
      *s += seg_sum[r];
      if (seg_min[r] < *m) *m = seg_min[r];
    }
  }
}

static void segment_tree(void) {
  printf("-- segment tree\n");
  for (int i = 0; i < SN; i++) seg_set(i, (int)(rnd() % 1000) - 500);
  int bad = 0;
  for (int q = 0; q < 2000; q++) {
    if (q % 3 == 0) seg_set((int)(rnd() % SN), (int)(rnd() % 2000) - 1000);
    int l = (int)(rnd() % SN), r = (int)(rnd() % SN);
    if (l > r) { int t = l; l = r; r = t; }
    r++;
    long long s;
    int m;
    seg_query(l, r, &s, &m);
    long long bs = 0;
    int bm = 1 << 30;
    for (int i = l; i < r; i++) {
      bs += base[i];
      if (base[i] < bm) bm = base[i];
    }
    if (s != bs || m != bm) bad++;
    if (q % 250 == 0) printf("q%d [%d,%d): sum=%lld min=%d\n", q, l, r, s, m);
  }
  printf("segment tree mismatches=%d\n", bad);
}

int main(void) {
  avl();
  dijkstra();
  trie();
  union_find();
  segment_tree();
  return 0;
}
