/* The same workload, compiled straight to the host by the host compiler.
 *
 * This is two things at once: an oracle that does not go through wasm at all,
 * and the speed ceiling.  warmc's job is to get its wasm input as close to
 * this as it can, so every number is quoted against it.
 *
 * The CLI and the timing method are deliberately identical to the runtime's,
 * so the two sides are measured the same way and not just similarly.
 */

#include "workload.c"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if defined(__APPLE__)
#include <time.h>
extern int pthread_set_qos_class_self_np(int, int);
static unsigned long long nanos(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static void prefer_fast_core(void) { pthread_set_qos_class_self_np(0x21, 0); }
#else
#include <time.h>
static unsigned long long nanos(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (unsigned long long)ts.tv_sec * 1000000000ull + (unsigned long long)ts.tv_nsec;
}
static void prefer_fast_core(void) { }
#endif

typedef struct {
  const char *name;
  int wide;                 /* 0: returns u32, 1: returns i64 */
  u32 (*f32)(u32);
  i64 (*f64)(u32);
} Case;

static const Case cases[] = {
  { "sha_rounds", 0, sha_rounds, 0 },
  { "sort_check", 0, sort_check, 0 },
  { "matmul",     1, 0, matmul   },
  { "mix64",      1, 0, mix64    },
  { "dispatch",   0, (u32 (*)(u32))dispatch, 0 },
  { "nbody",      1, 0, nbody    },
  { "strings",    0, strings,    0 },
  { "bitops",     0, bitops,     0 },
  { "startup",    0, startup,    0 },
};

#define BENCH_MAX 512
static unsigned long long samples[BENCH_MAX];

static int cmp_u64(const void *a, const void *b) {
  unsigned long long x = *(const unsigned long long *)a, y = *(const unsigned long long *)b;
  return x < y ? -1 : x > y;
}

static int run_one(const Case *c, u32 arg, int reps, int cluster_hot) {
  u32 r32 = 0; i64 r64 = 0;
  if (reps > 0) {
    prefer_fast_core();
    /* Same warm-up and batching rules as the runtime, including only paying
     * for the frequency ramp on the first export of a launch, so both sides
     * are measured alike rather than merely similarly. */
    unsigned long long target = cluster_hot ? 2000000ull : 100000000ull;
    unsigned long long warm = nanos();
    unsigned long long nwarm = 0;
    do {
      if (c->wide) r64 = c->f64(arg); else r32 = c->f32(arg);
      nwarm++;
    } while (nwarm < 3 || nanos() - warm < target);

    unsigned long long one = (nanos() - warm) / nwarm;
    unsigned long long batch = one ? (3000000ull + one - 1) / one : 1;
    if (batch < 1) batch = 1;

    int n = 0;
    unsigned long long spent = 0;
    while (n < reps) {
      unsigned long long t0 = nanos();
      for (unsigned long long k = 0; k < batch; k++) {
        if (c->wide) r64 = c->f64(arg); else r32 = c->f32(arg);
      }
      unsigned long long took = nanos() - t0;
      samples[n++] = (took + batch / 2) / batch;
      spent += took;
      if (n >= 5 && spent > 40000000ull) break;
    }
    qsort(samples, (size_t)n, sizeof samples[0], cmp_u64);
    unsigned long long sum = 0;
    for (int i = 0; i < n; i++) sum += samples[i];
    fprintf(stderr, "bench: %s n=%d min_ns=%llu med_ns=%llu max_ns=%llu mean_ns=%llu\n",
            c->name, n, samples[0], samples[n / 2], samples[n - 1],
            sum / (unsigned)n);
  } else {
    if (c->wide) r64 = c->f64(arg); else r32 = c->f32(arg);
  }
  if (c->wide) printf("i64:%lld\n", (long long)r64);
  else printf("i32:%d\n", (int)r32);
  return 0;
}

int main(int argc, char **argv) {
  int reps = 0;
  if (argc >= 3 && !strcmp(argv[1], "--bench")) {
    reps = atoi(argv[2]);
    if (reps < 1) reps = 1;
    if (reps > BENCH_MAX) reps = BENCH_MAX;
    argc -= 2; argv += 2;
  }
  if (argc < 2) {
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++)
      printf("  %s\n", cases[i].name);
    return 0;
  }
  /* "name arg [-- name arg]..." -- several exports in one launch. */
  for (int i = 1, hot = 0; i < argc; ) {
    int j = i + 1;
    while (j < argc && strcmp(argv[j], "--")) j++;
    const Case *c = 0;
    for (unsigned k = 0; k < sizeof cases / sizeof cases[0]; k++)
      if (!strcmp(cases[k].name, argv[i])) c = &cases[k];
    if (!c) { fprintf(stderr, "native: no such export: %s\n", argv[i]); return 2; }
    u32 arg = j > i + 1 ? (u32)strtoul(argv[i + 1], 0, 0) : 0;
    int rc = run_one(c, arg, reps, hot);
    if (rc) return rc;
    hot = reps > 0;
    i = j + 1;
  }
  return 0;
}
