// Sorting and searching written in C: insertion sort, shell sort, heapsort,
// an introsort-style quicksort with median-of-three and an insertion-sort
// cutoff, top-down and bottom-up merge sort, LSD radix sort on 32-bit keys
// (including negative ones via the sign-bit flip), counting sort, and a
// stable sort of records checked for stability; then binary search
// (lower_bound/upper_bound), interpolation search, quickselect for order
// statistics, and the next-permutation algorithm.
//
// Each sort runs on the same several inputs -- random, sorted, reversed,
// all-equal, few distinct values, organ-pipe -- and prints a digest of its
// output plus whether it is sorted and a permutation of the input, so a
// miscompiled comparison or index shows up as a wrong digest on one line.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N 600

static uint32_t rs = 1;
static uint32_t rnd(void) {
  rs = rs * 1103515245u + 12345u;
  return rs >> 1;
}

static void make(int kind, int *a, int n) {
  for (int i = 0; i < n; i++) {
    switch (kind) {
      case 0: a[i] = (int)(rnd() % 20001) - 10000; break;
      case 1: a[i] = i * 3 - 500; break;
      case 2: a[i] = (n - i) * 7; break;
      case 3: a[i] = 42; break;
      case 4: a[i] = (int)(rnd() % 4) - 2; break;
      case 5: a[i] = i < n / 2 ? i : n - i; break;
      default: {
        int x = (int)rnd();  // sequenced: two calls in one expression are not
        a[i] = x - (int)(rnd() / 2);
        break;
      }
    }
  }
}
static const char *kinds[] = {"random", "sorted", "reversed", "equal", "few", "organ", "wide"};
#define NKINDS 7

static uint32_t digest(const int *a, int n) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < n; i++) h = (h ^ (uint32_t)a[i]) * 16777619u;
  return h;
}
static int is_sorted(const int *a, int n) {
  for (int i = 1; i < n; i++)
    if (a[i - 1] > a[i]) return 0;
  return 1;
}
static long long sum(const int *a, int n) {
  long long s = 0;
  for (int i = 0; i < n; i++) s += a[i];
  return s;
}

static void insertion(int *a, int n) {
  for (int i = 1; i < n; i++) {
    int v = a[i], j = i - 1;
    while (j >= 0 && a[j] > v) {
      a[j + 1] = a[j];
      j--;
    }
    a[j + 1] = v;
  }
}
static void shell(int *a, int n) {
  static const int gaps[] = {701, 301, 132, 57, 23, 10, 4, 1};
  for (int g = 0; g < 8; g++) {
    int gap = gaps[g];
    for (int i = gap; i < n; i++) {
      int v = a[i], j = i;
      while (j >= gap && a[j - gap] > v) {
        a[j] = a[j - gap];
        j -= gap;
      }
      a[j] = v;
    }
  }
}
static void sift(int *a, int start, int end) {
  int root = start;
  while (2 * root + 1 <= end) {
    int child = 2 * root + 1, sw = root;
    if (a[sw] < a[child]) sw = child;
    if (child + 1 <= end && a[sw] < a[child + 1]) sw = child + 1;
    if (sw == root) return;
    int t = a[root]; a[root] = a[sw]; a[sw] = t;
    root = sw;
  }
}
static void heapsort_(int *a, int n) {
  for (int s = (n - 2) / 2; s >= 0; s--) sift(a, s, n - 1);
  for (int end = n - 1; end > 0; end--) {
    int t = a[end]; a[end] = a[0]; a[0] = t;
    sift(a, 0, end - 1);
  }
}
static void quick(int *a, int lo, int hi) {
  while (hi - lo > 12) {
    int mid = lo + (hi - lo) / 2;
    if (a[mid] < a[lo]) { int t = a[mid]; a[mid] = a[lo]; a[lo] = t; }
    if (a[hi] < a[lo]) { int t = a[hi]; a[hi] = a[lo]; a[lo] = t; }
    if (a[hi] < a[mid]) { int t = a[hi]; a[hi] = a[mid]; a[mid] = t; }
    int pivot = a[mid];
    int i = lo, j = hi;
    while (i <= j) {
      while (a[i] < pivot) i++;
      while (a[j] > pivot) j--;
      if (i <= j) {
        int t = a[i]; a[i] = a[j]; a[j] = t;
        i++;
        j--;
      }
    }
    // recurse into the smaller half, loop on the larger
    if (j - lo < hi - i) {
      quick(a, lo, j);
      lo = i;
    } else {
      quick(a, i, hi);
      hi = j;
    }
  }
  insertion(a + lo, hi - lo + 1);
}
static void quicksort_(int *a, int n) { if (n > 1) quick(a, 0, n - 1); }
static int tmpbuf[N];
static void merge_td(int *a, int n) {
  if (n < 2) return;
  int h = n / 2;
  merge_td(a, h);
  merge_td(a + h, n - h);
  int i = 0, j = h, k = 0;
  while (i < h && j < n) tmpbuf[k++] = a[j] < a[i] ? a[j++] : a[i++];
  while (i < h) tmpbuf[k++] = a[i++];
  while (j < n) tmpbuf[k++] = a[j++];
  memcpy(a, tmpbuf, (size_t)n * sizeof *a);
}
static void merge_bu(int *a, int n) {
  for (int w = 1; w < n; w *= 2) {
    for (int lo = 0; lo < n; lo += 2 * w) {
      int mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n;
      int i = lo, j = mid, k = lo;
      while (i < mid && j < hi) tmpbuf[k++] = a[j] < a[i] ? a[j++] : a[i++];
      while (i < mid) tmpbuf[k++] = a[i++];
      while (j < hi) tmpbuf[k++] = a[j++];
    }
    memcpy(a, tmpbuf, (size_t)n * sizeof *a);
  }
}
static void radix(int *a, int n) {
  static uint32_t buf[2][N];
  for (int i = 0; i < n; i++) buf[0][i] = (uint32_t)a[i] ^ 0x80000000u;
  int src = 0;
  for (int shift = 0; shift < 32; shift += 8) {
    int count[257] = {0};
    for (int i = 0; i < n; i++) count[((buf[src][i] >> shift) & 0xff) + 1]++;
    for (int b = 0; b < 256; b++) count[b + 1] += count[b];
    for (int i = 0; i < n; i++) buf[1 - src][count[(buf[src][i] >> shift) & 0xff]++] = buf[src][i];
    src = 1 - src;
  }
  for (int i = 0; i < n; i++) a[i] = (int)(buf[src][i] ^ 0x80000000u);
}
static int cmp(const void *x, const void *y) {
  int a = *(const int *)x, b = *(const int *)y;
  return (a > b) - (a < b);
}
static void libc_qsort(int *a, int n) { qsort(a, (size_t)n, sizeof *a, cmp); }

typedef void (*Sorter)(int *, int);
static const struct { const char *name; Sorter fn; } sorters[] = {
  {"insertion", insertion}, {"shell", shell}, {"heap", heapsort_}, {"quick", quicksort_},
  {"merge-td", merge_td}, {"merge-bu", merge_bu}, {"radix", radix}, {"qsort", libc_qsort},
};

static void sorts(void) {
  printf("-- sorting algorithms\n");
  static int orig[N], a[N];
  for (int k = 0; k < NKINDS; k++) {
    rs = (uint32_t)k * 7919u + 1;
    make(k, orig, N);
    printf("%s: input digest %08x sum %lld\n", kinds[k], digest(orig, N), sum(orig, N));
    uint32_t ref = 0;
    for (unsigned s = 0; s < sizeof sorters / sizeof sorters[0]; s++) {
      memcpy(a, orig, sizeof a);
      sorters[s].fn(a, N);
      uint32_t d = digest(a, N);
      if (s == 0) ref = d;
      printf("  %-9s sorted=%d same_sum=%d digest=%08x %s min=%d max=%d med=%d\n", sorters[s].name,
             is_sorted(a, N), sum(a, N) == sum(orig, N), d, d == ref ? "agree" : "DIFFER", a[0], a[N - 1], a[N / 2]);
    }
  }
}

typedef struct { int key; int seq; } Rec;
static void stable_insertion(Rec *r, int n) {
  for (int i = 1; i < n; i++) {
    Rec v = r[i];
    int j = i - 1;
    while (j >= 0 && r[j].key > v.key) {
      r[j + 1] = r[j];
      j--;
    }
    r[j + 1] = v;
  }
}
static void counting(Rec *r, int n, int maxkey) {
  int count[64] = {0};
  Rec out[100];
  for (int i = 0; i < n; i++) count[r[i].key + 1]++;
  for (int k = 0; k < maxkey; k++) count[k + 1] += count[k];
  for (int i = 0; i < n; i++) out[count[r[i].key]++] = r[i];
  memcpy(r, out, (size_t)n * sizeof *r);
}

static void stability(void) {
  printf("-- stable sorts\n");
  Rec a[100], b[100];
  rs = 99;
  for (int i = 0; i < 100; i++) {
    a[i].key = (int)(rnd() % 10);
    a[i].seq = i;
  }
  memcpy(b, a, sizeof a);
  stable_insertion(a, 100);
  counting(b, 100, 10);
  int stable_a = 1, stable_b = 1, same = 1;
  for (int i = 1; i < 100; i++) {
    if (a[i - 1].key == a[i].key && a[i - 1].seq > a[i].seq) stable_a = 0;
    if (b[i - 1].key == b[i].key && b[i - 1].seq > b[i].seq) stable_b = 0;
  }
  for (int i = 0; i < 100; i++)
    if (a[i].key != b[i].key || a[i].seq != b[i].seq) same = 0;
  printf("insertion stable=%d counting stable=%d identical=%d\n", stable_a, stable_b, same);
  for (int i = 0; i < 100; i += 9) printf("(%d,%d) ", a[i].key, a[i].seq);
  printf("\n");
}

static int lower_bound(const int *a, int n, int v) {
  int lo = 0, hi = n;
  while (lo < hi) {
    int mid = lo + (hi - lo) / 2;
    if (a[mid] < v) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}
static int upper_bound(const int *a, int n, int v) {
  int lo = 0, hi = n;
  while (lo < hi) {
    int mid = lo + (hi - lo) / 2;
    if (a[mid] <= v) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}
static int interp(const int *a, int n, int v) {
  int lo = 0, hi = n - 1, probes = 0;
  while (lo <= hi && v >= a[lo] && v <= a[hi]) {
    probes++;
    int pos = a[hi] == a[lo] ? lo : lo + (int)((long long)(v - a[lo]) * (hi - lo) / (a[hi] - a[lo]));
    if (a[pos] == v) return pos * 100 + probes;
    if (a[pos] < v) lo = pos + 1;
    else hi = pos - 1;
  }
  return -probes;
}
static int quickselect(int *a, int n, int k) {
  int lo = 0, hi = n - 1;
  while (lo < hi) {
    int pivot = a[lo + (hi - lo) / 2], i = lo, j = hi;
    while (i <= j) {
      while (a[i] < pivot) i++;
      while (a[j] > pivot) j--;
      if (i <= j) {
        int t = a[i]; a[i] = a[j]; a[j] = t;
        i++;
        j--;
      }
    }
    if (k <= j) hi = j;
    else if (k >= i) lo = i;
    else return a[k];
  }
  return a[k];
}
static int next_perm(int *a, int n) {
  int i = n - 2;
  while (i >= 0 && a[i] >= a[i + 1]) i--;
  if (i < 0) return 0;
  int j = n - 1;
  while (a[j] <= a[i]) j--;
  int t = a[i]; a[i] = a[j]; a[j] = t;
  for (int l = i + 1, r = n - 1; l < r; l++, r--) {
    t = a[l]; a[l] = a[r]; a[r] = t;
  }
  return 1;
}

static void searching(void) {
  printf("-- searching\n");
  static int a[N];
  rs = 5;
  for (int i = 0; i < N; i++) a[i] = (int)(rnd() % 1000);
  quicksort_(a, N);
  int probes[] = {-1, 0, 1, 250, 499, 500, 777, 998, 999, 1000, a[123], a[456]};
  for (unsigned i = 0; i < sizeof probes / sizeof probes[0]; i++) {
    int v = probes[i];
    int lb = lower_bound(a, N, v), ub = upper_bound(a, N, v);
    printf("v=%d lower=%d upper=%d count=%d interp=%d\n", v, lb, ub, ub - lb, interp(a, N, v));
  }
  static int b[N];
  rs = 17;
  make(0, b, N);
  static int c[N];
  for (int k = 0; k < N; k += 75) {
    memcpy(c, b, sizeof c);
    int q = quickselect(c, N, k);
    memcpy(c, b, sizeof c);
    quicksort_(c, N);
    printf("k=%d select=%d sorted[k]=%d\n", k, q, c[k]);
  }
  int p[5] = {1, 2, 2, 3, 4};
  int count = 1;
  printf("perms:");
  while (next_perm(p, 5)) {
    count++;
    if (count % 6 == 0) printf(" %d%d%d%d%d", p[0], p[1], p[2], p[3], p[4]);
  }
  printf("\ntotal distinct permutations of 12234: %d\n", count);
}

int main(void) {
  sorts();
  stability();
  searching();
  return 0;
}
