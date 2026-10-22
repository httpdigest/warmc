// Loop shapes an optimiser rewrites: counted loops up and down, with strides
// of 1, 3 and -2, unsigned counters that stop at zero, 8- and 16-bit counters
// that wrap, induction variables of several widths derived from one another,
// reductions (sum, product, min/max, xor, and the float sum whose order
// matters), prefix sums and other loop-carried recurrences, loops with early
// exits from the middle, loop-invariant expressions that may be hoisted and
// conditions that may be unswitched, nested loops with triangular and
// dependent bounds, loop interchange candidates over a matrix, a loop whose
// trip count is zero, and while loops whose condition has side effects.
//
// These are the patterns that LICM, loop rotation, strength reduction, and
// induction-variable widening transform; each prints its result so a loop
// that ran one time too many or too few, or a hoisted value that was not
// invariant after all, changes a line.  Bounds come from volatile objects so
// the trip counts are not known at compile time.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static volatile int vn = 100, vzero = 0, vthree = 3, vneg = -7;
static volatile unsigned vun = 37;
static int data[256];
static double fdata[256];

static void init(void) {
  for (int i = 0; i < 256; i++) {
    data[i] = (int)((i * 2654435761u) >> 20) - 2048;
    fdata[i] = (i % 17) * 0.1 - (i % 5) * 0.37 + 1e-3 * i;
  }
}

static void counted(void) {
  printf("-- counted loops\n");
  int n = vn;
  long long s1 = 0, s3 = 0, s4 = 0;
  uint64_t s2 = 0;  // wraps: unsigned
  for (int i = 0; i < n; i++) s1 += data[i];
  for (int i = n - 1; i >= 0; i--) s2 = s2 * 3 + (uint64_t)data[i];
  for (int i = 1; i < n; i += 3) s3 += data[i] ^ i;
  for (int i = n; i > 0; i -= 2) s4 -= data[i] * (i & 7);
  printf("up=%lld down=%llu stride3=%lld down2=%lld\n", s1, (unsigned long long)s2, s3, s4);
  unsigned cnt = 0;
  for (unsigned u = vun; u > 0; u--) cnt += u * u;
  unsigned cnt2 = 0;
  for (unsigned u = vun; u-- > 0;) cnt2 += u;
  printf("unsigned down: %u %u\n", cnt, cnt2);
  int iters = 0;
  for (uint8_t b = 250; b != 4; b++) iters++;  // wraps through 255 -> 0
  int iters16 = 0;
  for (uint16_t w = 65530; w != 10; w += 3) iters16++;
  printf("u8 wrap iterations=%d u16 wrap iterations=%d\n", iters, iters16);
  int zero_trip = 0;
  for (int i = vzero; i < vzero; i++) zero_trip++;
  for (int i = 10; i < vneg; i++) zero_trip++;
  printf("zero-trip=%d\n", zero_trip);
  // several induction variables of different widths
  int16_t a16 = -300;
  uint32_t b32 = 7;
  int64_t c64 = -1;
  for (int i = 0; i < n; i++) {
    a16 = (int16_t)(a16 + 13);
    b32 = b32 * 5 + (uint32_t)i;
    c64 += (int64_t)i * i - a16;
  }
  printf("ivs: %d %u %lld\n", a16, b32, (long long)c64);
}

static void reductions(void) {
  printf("-- reductions\n");
  int n = vn * 2;
  long long sum = 0;
  uint64_t prod = 1;
  int mn = data[0], mx = data[0], mni = 0, mxi = 0;
  unsigned x = 0;
  int pos = 0, neg = 0;
  for (int i = 0; i < n; i++) {
    sum += data[i];
    prod *= (uint64_t)(data[i] | 1);
    if (data[i] < mn) { mn = data[i]; mni = i; }
    if (data[i] > mx) { mx = data[i]; mxi = i; }
    x ^= (unsigned)data[i] << (i & 15);
    pos += data[i] > 0;
    neg += data[i] < 0;
  }
  printf("sum=%lld prod=%016llx min=%d@%d max=%d@%d xor=%08x pos=%d neg=%d\n", sum, (unsigned long long)prod, mn,
         mni, mx, mxi, x, pos, neg);
  double fs = 0, fsr = 0, fdot = 0;
  float ff = 0;
  for (int i = 0; i < n; i++) fs += fdata[i];
  for (int i = n - 1; i >= 0; i--) fsr += fdata[i];
  for (int i = 0; i < n; i++) fdot += fdata[i] * fdata[(i * 7) & 255];
  for (int i = 0; i < n; i++) ff += (float)fdata[i];
  printf("fsum=%a reversed=%a dot=%a float=%a\n", fs, fsr, fdot, (double)ff);
}

static void recurrences(void) {
  printf("-- recurrences\n");
  int n = vn;
  static long long pre[256];
  pre[0] = data[0];
  for (int i = 1; i < n; i++) pre[i] = pre[i - 1] + data[i];
  printf("prefix: %lld %lld %lld\n", pre[10], pre[n / 2], pre[n - 1]);
  int a[64];
  memcpy(a, data, sizeof a);
  for (int i = 2; i < 64; i++) a[i] = a[i - 1] - a[i - 2] / 2;  // depends on two back
  printf("two-back: %d %d %d\n", a[20], a[40], a[63]);
  for (int i = 62; i >= 0; i--) a[i] += a[i + 1] / 3;  // backward dependency
  printf("backward: %d %d %d\n", a[0], a[31], a[62]);
  uint32_t fa = 0, fb = 1;
  for (int i = 0; i < n; i++) {
    uint32_t t = fa + fb;
    fa = fb;
    fb = t;
  }
  printf("fib mod 2^32 (n=%d): %u\n", n, fa);
  double y = 1.0;
  for (int i = 0; i < 50; i++) y = y - (y * y - 2.0) / (2.0 * y);  // Newton sqrt 2
  printf("newton=%.17g\n", y);
}

static int find_first(const int *a, int n, int pred_k) {
  for (int i = 0; i < n; i++) {
    if (a[i] % 97 == pred_k) return i;
    if (a[i] == 12345) break;
  }
  return -1;
}

static void early_exit(void) {
  printf("-- early exits\n");
  int n = vn * 2;
  for (int k = -5; k <= 5; k += 2) printf("first %%97==%d at %d\n", k, find_first(data, n, k));
  int i = 0, steps = 0;
  while (1) {
    steps++;
    if (data[i & 255] > 1500) break;
    i += 7;
    if (i > 5000) break;
  }
  printf("found big at i=%d after %d steps\n", i, steps);
  int found = -1;
  for (int r = 0; r < 16 && found < 0; r++)
    for (int c = 0; c < 16; c++)
      if (data[r * 16 + c] > 1900) {
        found = r * 16 + c;
        break;
      }
  printf("first > 1900 at %d\n", found);
  // do-while with continue: the continue jumps to the condition
  int k = 0, acc = 0;
  do {
    k++;
    if (k % 4 == 0) continue;
    acc += k;
  } while (k < vn / 4);
  printf("do-while acc=%d k=%d\n", acc, k);
}

static void invariants(void) {
  printf("-- invariant code and unswitching\n");
  int n = vn;
  int three = vthree;
  long long r1 = 0, r2 = 0;
  for (int i = 0; i < n; i++) {
    int inv = three * three + vthree;  // volatile read each time: not invariant
    r1 += data[i] * inv;
    int inv2 = three * 7 + 1;          // truly invariant
    r2 += (data[i] + inv2) * (three > 2 ? 1 : -1);
  }
  printf("r1=%lld r2=%lld\n", r1, r2);
  int mode = vthree & 1;
  long long acc = 0;
  for (int i = 0; i < n; i++) {
    if (mode) acc += data[i];
    else acc -= data[i];
    if (i == n / 2) mode = !mode;  // mode is not invariant after all
  }
  printf("switched-mode acc=%lld\n", acc);
  int stores[8] = {0};
  for (int i = 0; i < n; i++) stores[i & 7] += data[i];  // store to memory each pass
  for (int i = 0; i < 8; i++) printf("%d ", stores[i]);
  printf("\n");
  int g = 0;
  for (int i = 0; i < n; i++) {
    g += i;
    data[255] = g;  // a store the hoister must not move out
  }
  printf("last store=%d\n", data[255]);
}

static void nested(void) {
  printf("-- nested loops\n");
  int n = vn / 4;
  long long tri = 0, dep = 0;
  for (int i = 0; i < n; i++)
    for (int j = 0; j <= i; j++) tri += (i - j) * (j + 1);
  for (int i = 1; i < n; i++)
    for (int j = i; j < n; j += i) dep += j / i;
  printf("triangular=%lld dependent=%lld\n", tri, dep);
  static int m[32][32];
  for (int i = 0; i < 32; i++)
    for (int j = 0; j < 32; j++) m[i][j] = (i * 31 + j * 17) % 23 - 11;
  long long rowmajor = 0, colmajor = 0;
  for (int i = 0; i < 32; i++)
    for (int j = 0; j < 32; j++) rowmajor = (rowmajor * 3 + m[i][j]) & 0xffffffffffll;
  for (int j = 0; j < 32; j++)
    for (int i = 0; i < 32; i++) colmajor = (colmajor * 3 + m[i][j]) & 0xffffffffffll;
  printf("rowmajor=%lld colmajor=%lld\n", rowmajor, colmajor);
  // matrix multiply with i-k-j order and a transpose
  static int a[16][16], b[16][16], c[16][16], t[16][16];
  for (int i = 0; i < 16; i++)
    for (int j = 0; j < 16; j++) {
      a[i][j] = m[i][j];
      b[i][j] = m[j][i + 8];
      c[i][j] = 0;
    }
  for (int i = 0; i < 16; i++)
    for (int k = 0; k < 16; k++)
      for (int j = 0; j < 16; j++) c[i][j] += a[i][k] * b[k][j];
  for (int i = 0; i < 16; i++)
    for (int j = 0; j < 16; j++) t[j][i] = c[i][j];
  for (int i = 0; i < 16; i += 5) {
    for (int j = 0; j < 16; j += 3) printf("%6d", t[i][j]);
    printf("\n");
  }
  // stencil with in-place update and boundary handling
  double g[40];
  for (int i = 0; i < 40; i++) g[i] = i == 20 ? 100.0 : 0.0;
  for (int step = 0; step < 30; step++) {
    double prev = g[0];
    for (int i = 1; i < 39; i++) {
      double cur = g[i];
      g[i] = 0.25 * prev + 0.5 * cur + 0.25 * g[i + 1];
      prev = cur;
    }
  }
  for (int i = 10; i < 31; i += 2) printf("%.6f ", g[i]);
  printf("\n");
}

static int counter;
static int next_val(void) { return counter++ < 20 ? counter * counter % 13 : -1; }

static void side_effects(void) {
  printf("-- conditions with side effects\n");
  int v, total = 0, count = 0;
  counter = 0;
  while ((v = next_val()) >= 0) {
    total += v;
    count++;
  }
  printf("read %d values total %d counter %d\n", count, total, counter);
  const char *s = "  leading spaces and words";
  const char *p = s;
  while (*p == ' ') p++;
  int words = 0;
  while (*p) {
    words++;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
  }
  printf("skipped %td, words %d\n", (ptrdiff_t)(strchr(s, 'l') - s), words);
  int i = 0, j = 100;
  while (i++ < j--) {
  }
  printf("met at i=%d j=%d\n", i, j);
}

int main(void) {
  init();
  counted();
  reductions();
  recurrences();
  early_exit();
  invariants();
  nested();
  side_effects();
  return 0;
}
