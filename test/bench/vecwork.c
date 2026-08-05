/* Loops the vectoriser is meant to reach, for measuring it.
 *
 * `workload.c` is the suite that says whether the compiler is fast on
 * ordinary code, and none of its hot loops is vectorisable -- neither by
 * warmc nor by LLVM, which refuses every one of them for a reason it will
 * print.  So it says nothing about this pass either way, and a second module
 * is the honest way to measure one: these are counted loops over linear
 * memory with independent iterations, which is the shape the pass exists for.
 *
 * Each array is 8 KiB so the whole working set stays in L1 and the numbers
 * are about the code rather than about the memory system.  Each export runs
 * its kernel `reps` times over the same data and folds the result into one
 * scalar, so nothing can be dropped and a wrong lane cannot hide.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int i32;
typedef unsigned long long u64;
typedef long long i64;

#define EXPORT __attribute__((visibility("default"), used))
#define N 2048

static i32 ia[N], ib[N], ic[2 * N];
static double fa[N], fb[N], fc[N];
static i64 la[N], lb[N], lc[N];
static i32 pair[2 * N];
static u8 ba[N], bb[N], bc[N];
static u16 ha[N], hb[N], hc[N];

static u32 seed;
static u32 rnd(void) {
  seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
  return seed;
}

/* The outputs are reset as well as the inputs, and that is not tidiness: the
 * timing harness warms up for a fixed *duration*, so a faster build calls the
 * export more times before the timed run, and any kernel that carried state
 * between calls would return a different answer on the faster side and look
 * like a miscompile. */
static void init(void) {
  seed = 0x2545F491u;
  for (u32 i = 0; i < N; i++) {
    ia[i] = (i32)rnd(); ib[i] = (i32)rnd(); ic[i] = 0;
    pair[2 * i] = (i32)rnd(); pair[2 * i + 1] = (i32)rnd();
    la[i] = (i64)(((u64)rnd() << 32) | rnd());
    lb[i] = (i64)(((u64)rnd() << 32) | rnd());
    lc[i] = 0;
    ba[i] = (u8)rnd(); bb[i] = (u8)rnd(); bc[i] = 0;
    ha[i] = (u16)rnd(); hb[i] = (u16)rnd(); hc[i] = 0;
    fa[i] = (double)(i32)rnd() * 0.5e-9;
    fb[i] = (double)(i32)rnd() * 0.25e-9 + 1.0;
    fc[i] = 0.0;
  }
}

static u32 sum_i32(const i32 *p) {
  u32 h = 2166136261u;
  for (u32 i = 0; i < N; i++) { h ^= (u32)p[i]; h *= 16777619u; }
  return h;
}

static u64 sum_i64(const i64 *p) {
  u64 h = 1469598103934665603ull;
  for (u32 i = 0; i < N; i++) { h ^= (u64)p[i]; h *= 1099511628211ull; }
  return h;
}

/* ---- integer elementwise: the plainest thing there is ---- */
EXPORT u32 v_axpy(u32 reps) {
  init();
  for (u32 r = 0; r < reps; r++)
    for (u32 i = 0; i < N; i++) ic[i] = ia[i] * 3 + ib[i];
  return sum_i32(ic);
}

/* ---- a longer expression, so the loads are amortised over real work ---- */
EXPORT u32 v_blend(u32 reps) {
  init();
  for (u32 r = 0; r < reps; r++)
    for (u32 i = 0; i < N; i++)
      ic[i] = ((ia[i] ^ ib[i]) * 5 + (ia[i] >> 3)) & ~(ib[i] << 2);
  return sum_i32(ic);
}

/* ---- in place, which is the distance-zero case ---- */
EXPORT u32 v_scale(u32 reps) {
  init();
  for (u32 r = 0; r < reps; r++)
    for (u32 i = 0; i < N; i++) ic[i] = ic[i] * 5 + ia[i];
  return sum_i32(ic);
}

/* ---- 64-bit lanes: two per vector rather than four ---- */
EXPORT u64 v_mix64(u32 reps) {
  init();
  for (u32 r = 0; r < reps; r++)
    for (u32 i = 0; i < N; i++) lc[i] = (la[i] ^ lb[i]) + (la[i] & lb[i]);
  return sum_i64(lc);
}

/* ---- double-precision elementwise ---- */
EXPORT u64 v_fmul(u32 reps) {
  init();
  for (u32 r = 0; r < reps; r++)
    for (u32 i = 0; i < N; i++) fc[i] = fa[i] * fb[i] + fa[i];
  u64 bits;
  double acc = 0.0;
  for (u32 i = 0; i < N; i++) acc = acc * 1.0000001 + fc[i];
  __builtin_memcpy(&bits, &acc, 8);
  return bits;
}

/* ---- a square root per element, which is the one place the vector unit is
 *      not just wider but the same latency for four times the work ---- */
EXPORT u64 v_fsqrt(u32 reps) {
  init();
  for (u32 r = 0; r < reps; r++)
    for (u32 i = 0; i < N; i++)
      fc[i] = __builtin_sqrt(fa[i] * fa[i] + fb[i] * fb[i]);
  u64 bits;
  double acc = 0.0;
  for (u32 i = 0; i < N; i++) acc = acc * 1.0000001 + fc[i];
  __builtin_memcpy(&bits, &acc, 8);
  return bits;
}

/* ---- an integer reduction, which needs the horizontal fold on the way out */
EXPORT u32 v_sum(u32 reps) {
  init();
  u32 s = 0;
  for (u32 r = 0; r < reps; r++)
    for (u32 i = 0; i < N; i++) s += (u32)ia[i] * 3u;
  return s;
}

/* ---- a bitwise fold, which has no horizontal instruction at all ---- */
EXPORT u32 v_xor(u32 reps) {
  init();
  u32 s = 0x12345678u;
  for (u32 r = 0; r < reps; r++)
    for (u32 i = 0; i < N; i++) s ^= (u32)ia[i] & (u32)ib[i];
  return s;
}

/* ---- sub-word lanes: sixteen bytes at a time is sixteen elements ---- */
EXPORT u32 v_bxor(u32 reps) {
  init();
  for (u32 r = 0; r < reps; r++)
    for (u32 i = 0; i < N; i++) bc[i] = ba[i] ^ bb[i];
  u32 h = 2166136261u;
  for (u32 i = 0; i < N; i++) { h ^= bc[i]; h *= 16777619u; }
  return h;
}

EXPORT u32 v_bmul(u32 reps) {
  init();
  for (u32 r = 0; r < reps; r++)
    for (u32 i = 0; i < N; i++) bc[i] = (u8)(ba[i] * 31 + bb[i]);
  u32 h = 2166136261u;
  for (u32 i = 0; i < N; i++) { h ^= bc[i]; h *= 16777619u; }
  return h;
}

EXPORT u32 v_hadd(u32 reps) {
  init();
  for (u32 r = 0; r < reps; r++)
    for (u32 i = 0; i < N; i++) hc[i] = (u16)((ha[i] + hb[i]) * 5);
  u32 h = 2166136261u;
  for (u32 i = 0; i < N; i++) { h ^= hc[i]; h *= 16777619u; }
  return h;
}

/* ---- a lane-wise maximum, and a clamp, out of selects ---- */
EXPORT u32 v_max(u32 reps) {
  init();
  for (u32 r = 0; r < reps; r++)
    for (u32 i = 0; i < N; i++) ic[i] = ia[i] > ib[i] ? ia[i] : ib[i];
  return sum_i32(ic);
}

EXPORT u32 v_clamp(u32 reps) {
  init();
  for (u32 r = 0; r < reps; r++)
    for (u32 i = 0; i < N; i++) {
      i32 v = ia[i] < 0 ? 0 : ia[i];
      ic[i] = v > 255 ? 255 : v;
    }
  return sum_i32(ic);
}

/* ---- two runtime base pointers ----
 * `noinline` keeps the pointers as parameters, so neither disjointness nor
 * overlap is decidable at compile time and the loop is entered through the
 * runtime alias check rather than the static one. */
__attribute__((noinline)) static void axpy_p(i32 *d, const i32 *s, u32 n) {
  for (u32 i = 0; i < n; i++) d[i] = s[i] * 3 + 1;
}

EXPORT u32 v_ptr(u32 reps) {
  init();
  for (u32 r = 0; r < reps; r++) axpy_p(ic, ia, N);
  return sum_i32(ic);
}

/* ---- an `if` in the body ----
 * The diamond is flattened: both paths are computed and a lane mask picks
 * between them.  `v_cond` stores on every iteration; `v_cstore` stores only
 * on some, which the vector form does by writing back what was already in the
 * lanes it must not change. */
EXPORT u32 v_cload(u32 reps) {
  init();
  for (u32 r = 0; r < reps; r++)
    for (u32 i = 0; i < N; i++) {
      i32 v = ia[i];
      if (v > 0) v += ib[i];        /* the load of ib is speculated */
      ic[i] = v;
    }
  return sum_i32(ic);
}

EXPORT u32 v_cstore(u32 reps) {
  init();
  for (u32 r = 0; r < reps; r++)
    for (u32 i = 0; i < N; i++)
      if (ia[i] > 0) ic[i] = ia[i] * 3;
  return sum_i32(ic);
}

/* ---- an array of pairs against two arrays of scalars ----
 * The pair array is loaded whole and separated with `uzp`, so exactly the
 * bytes the loop reads are read. */
EXPORT u32 v_deint(u32 reps) {
  init();
  for (u32 r = 0; r < reps; r++)
    for (u32 i = 0; i < N; i++) { ia[i] = pair[2 * i]; ib[i] = pair[2 * i + 1]; }
  return sum_i32(ia) ^ sum_i32(ib);
}

/* ---- lanes that compute different functions ----
 * One array at one stride; what alternates is the operation, not the address.
 * Each half is built on every other lane and `zip` puts them back. */
EXPORT u32 v_alt(u32 reps) {
  init();
  for (u32 r = 0; r < reps; r++)
    for (u32 i = 0; i < N; i++) {
      i32 a = pair[2 * i], b = pair[2 * i + 1];
      ic[2 * i] = a - b;
      ic[2 * i + 1] = a + b;
    }
  return sum_i32(ic);
}

/* ---- a counter that runs backwards ----
 * `for (i = n; i-- > 0;)` visits every element in the other order and is
 * fully parallel; which lane holds which iteration is reversed and nothing
 * depends on it. */
EXPORT u32 v_down(u32 reps) {
  init();
  for (u32 r = 0; r < reps; r++)
    for (u32 i = N; i-- > 0; ) ic[i] = ia[i] * 3 + ib[i];
  return sum_i32(ic);
}

/* ---- a stride wider than one vector ----
 * clang unrolls this by four, so one scalar iteration moves 32 bytes and the
 * body gets two vectors chained rather than one. */
EXPORT u64 v_wide(u32 reps) {
  init();
  for (u32 r = 0; r < reps; r++)
    for (u32 i = 0; i < N; i++) lc[i] = la[i] + 1;
  return sum_i64(lc);
}

/* ---- one counter per array ----
 * Written as pointer walking, which is what a producer emits for three
 * separate arrays: three induction variables marching in lockstep rather than
 * one index shared between them.  Their starting points are still constants,
 * so the dependence test stays static and no runtime check is built. */
__attribute__((noinline)) static void miv_p(i32 *d, const i32 *a, const i32 *b,
                                            u32 n) {
  const i32 *end = a + n;
  while (a != end) *d++ = *a++ * 3 + *b++;
}

EXPORT u32 v_miv(u32 reps) {
  init();
  for (u32 r = 0; r < reps; r++) miv_p(ic, ia, ib, N);
  return sum_i32(ic);
}

/* ---- the cost of the guard, isolated ----
 * A loop entered far more often than it iterates pays for the trip count, the
 * wrap check and the alias checks and never runs a vector iteration.  This is
 * the shape that would regress if that guard were expensive, so it is
 * measured rather than assumed. */
EXPORT u32 v_short(u32 reps) {
  init();
  for (u32 r = 0; r < reps; r++)
    for (u32 b = 0; b < N; b += 4) axpy_p(ic + b, ia + b, 3);
  return sum_i32(ic);
}

/* A variable trip count, so the differential suite can walk every remainder
 * the packing can leave on *producer* output rather than only on hand-written
 * wat.  Not benchmarked -- it exists to be checked. */
EXPORT u32 v_tail(u32 n) {
  if (n > N) n = N;
  init();
  for (u32 i = 0; i < n; i++) ic[i] = ia[i] * 3 + ib[i];
  u32 h = 2166136261u;
  for (u32 i = 0; i < n; i++) { h ^= (u32)ic[i]; h *= 16777619u; }
  return h;
}

EXPORT u32 startup(u32 x) { return x; }
