// C11 <stdatomic.h> on a single thread: atomic_int, atomic_uint, atomic_llong,
// atomic_bool, atomic_flag and an _Atomic pointer and struct; load, store,
// exchange, fetch_add/sub/or/and/xor at 8, 16, 32 and 64 bits with every
// memory order, compare_exchange_strong and _weak in success and failure
// (where the expected value is written back), test_and_set / clear, the
// compound assignment operators on _Atomic variables, lock-free queries, a
// spinlock and a ticket counter built from them, and fences.
//
// wasi-sdk builds for wasm32-wasip1 without the threads feature, so clang
// lowers these to plain loads and stores and read-modify-write sequences (a
// struct is kept to 8 bytes: wasi-sdk has no libcalls for larger atomics).
// Single-threaded, every result is determined, which makes the returned old
// values and the failure write-back of compare-exchange the thing checked.
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static atomic_int ai = 5;
static atomic_uint au = 0xfffffff0u;
static atomic_llong all = -1;
static atomic_ullong aull = 0;
static _Atomic uint8_t a8 = 250;
static _Atomic int16_t a16 = -100;
static atomic_bool ab = false;
static atomic_flag flag = ATOMIC_FLAG_INIT;
static int pool[4] = {10, 20, 30, 40};
static _Atomic(int *) aptr = pool;

typedef struct { int x, y; } Pair;
static _Atomic Pair apair;

static void fetch_ops(void) {
  printf("-- fetch ops\n");
  memory_order orders[] = {memory_order_relaxed, memory_order_acquire, memory_order_release,
                           memory_order_acq_rel, memory_order_seq_cst};
  for (int i = 0; i < 5; i++) {
    int o1 = atomic_fetch_add_explicit(&ai, 7 * i, orders[i]);
    int o2 = atomic_fetch_sub_explicit(&ai, 3, orders[i]);
    int o3 = atomic_fetch_or_explicit(&ai, 0x100 << i, orders[i]);
    int o4 = atomic_fetch_and_explicit(&ai, ~(1 << i), orders[i]);
    int o5 = atomic_fetch_xor_explicit(&ai, 0x55, orders[i]);
    printf("i=%d old: %d %d %d %d %d now %d\n", i, o1, o2, o3, o4, o5, atomic_load(&ai));
  }
  for (int i = 0; i < 6; i++) {
    unsigned o = atomic_fetch_add(&au, 5u);  // wraps through zero
    printf("au %u -> %u\n", o, atomic_load(&au));
  }
  for (int i = 0; i < 5; i++) {
    long long o = atomic_fetch_add(&all, 0x100000000ll * (i + 1));
    unsigned long long u = atomic_fetch_xor(&aull, 0x8000000000000001ull >> i);
    printf("all %lld -> %lld | aull %llx -> %llx\n", o, atomic_load(&all), u, atomic_load(&aull));
  }
  for (int i = 0; i < 5; i++) {
    uint8_t o8 = atomic_fetch_add(&a8, 3);
    int16_t o16 = atomic_fetch_sub(&a16, 10000);
    printf("a8 %u -> %u | a16 %d -> %d\n", o8, atomic_load(&a8), o16, atomic_load(&a16));
  }
}

static void exchange_cas(void) {
  printf("-- exchange and compare-exchange\n");
  int old = atomic_exchange(&ai, 1000);
  printf("exchange old=%d now=%d\n", old, atomic_load(&ai));
  for (int attempt = 0; attempt < 6; attempt++) {
    int expected = 1000 + (attempt % 2) * attempt;
    bool ok = atomic_compare_exchange_strong(&ai, &expected, attempt * 11 + 1000);
    printf("strong attempt %d: ok=%d expected now %d value %d\n", attempt, ok, expected, atomic_load(&ai));
    if (ok) atomic_store(&ai, 1000);
  }
  // weak CAS in a retry loop: increment by doubling
  int v = atomic_load(&ai);
  for (int i = 0; i < 5; i++) {
    int exp = v;
    while (!atomic_compare_exchange_weak(&ai, &exp, exp * 2 + 1)) {
    }
    v = atomic_load(&ai);
    printf("doubled to %d (from %d)\n", v, exp);
  }
  long long e = 5;
  bool ok = atomic_compare_exchange_strong_explicit(&all, &e, 6, memory_order_acq_rel, memory_order_acquire);
  printf("llong cas from wrong 5: ok=%d e=%lld\n", ok, e);
  ok = atomic_compare_exchange_strong(&all, &e, 6);
  printf("llong cas retry: ok=%d value=%lld\n", ok, atomic_load(&all));
  bool b = atomic_exchange(&ab, true);
  bool exp_b = false;
  bool ok2 = atomic_compare_exchange_strong(&ab, &exp_b, false);
  printf("bool: old=%d cas ok=%d exp=%d now=%d\n", b, ok2, exp_b, atomic_load(&ab));
}

static void flags_and_ptrs(void) {
  printf("-- flag, pointer, struct\n");
  for (int i = 0; i < 4; i++) {
    bool was = atomic_flag_test_and_set(&flag);
    printf("tas %d: was %d\n", i, was);
    if (i == 1) atomic_flag_clear(&flag);
  }
  int *p = atomic_fetch_add(&aptr, 2);
  printf("ptr fetch_add: old->%d new->%d\n", *p, *atomic_load(&aptr));
  p = atomic_fetch_sub(&aptr, 1);
  printf("ptr fetch_sub: old->%d new->%d\n", *p, *atomic_load(&aptr));
  int *expected = pool;
  bool ok = atomic_compare_exchange_strong(&aptr, &expected, pool + 3);
  printf("ptr cas wrong: ok=%d expected->%d\n", ok, *expected);
  ok = atomic_compare_exchange_strong(&aptr, &expected, pool + 3);
  printf("ptr cas right: ok=%d now->%d\n", ok, *atomic_load(&aptr));
  Pair pr = {3, 4};
  atomic_store(&apair, pr);
  Pair got = atomic_exchange(&apair, ((Pair){-1, -2}));
  Pair now = atomic_load(&apair);
  printf("pair old=(%d,%d) now=(%d,%d)\n", got.x, got.y, now.x, now.y);
  Pair want = {-1, -2};
  ok = atomic_compare_exchange_strong(&apair, &want, ((Pair){7, 8}));
  now = atomic_load(&apair);
  printf("pair cas ok=%d now=(%d,%d)\n", ok, now.x, now.y);
  printf("lock-free: int=%d llong=%d pair=%d ptr=%d flagish=%d\n", atomic_is_lock_free(&ai),
         atomic_is_lock_free(&all), atomic_is_lock_free(&apair), atomic_is_lock_free(&aptr),
         ATOMIC_INT_LOCK_FREE);
}

static void operators(void) {
  printf("-- compound operators on _Atomic\n");
  _Atomic int x = 10;
  _Atomic long long y = 1;
  _Atomic unsigned short z = 65530;
  for (int i = 0; i < 8; i++) {
    x += i;
    x *= 2;  // not a single RMW in C, but still defined on _Atomic
    x -= 3;
    x |= 1 << i;
    x ^= 0x0f;
    y <<= 3;
    y += x;
    z += 3;
    int pre = ++x;
    int post = x--;
    printf("i=%d x=%d y=%lld z=%u pre=%d post=%d\n", i, (int)x, (long long)y, (unsigned)z, pre, post);
  }
}

typedef struct { atomic_flag f; int holders; int max_holders; } Spin;
static void lock(Spin *s) {
  while (atomic_flag_test_and_set_explicit(&s->f, memory_order_acquire)) {
  }
  s->holders++;
  if (s->holders > s->max_holders) s->max_holders = s->holders;
}
static void unlock(Spin *s) {
  s->holders--;
  atomic_flag_clear_explicit(&s->f, memory_order_release);
}

static void structures(void) {
  printf("-- spinlock and ticket counter\n");
  Spin s = {ATOMIC_FLAG_INIT, 0, 0};
  long total = 0;
  for (int i = 0; i < 1000; i++) {
    lock(&s);
    total += i;
    unlock(&s);
  }
  printf("total=%ld max_holders=%d holders=%d\n", total, s.max_holders, s.holders);
  atomic_uint next_ticket = 0, serving = 0;
  unsigned served = 0;
  for (int i = 0; i < 50; i++) {
    unsigned t = atomic_fetch_add(&next_ticket, 1);
    while (atomic_load_explicit(&serving, memory_order_acquire) != t) {
    }
    served += t;
    atomic_fetch_add_explicit(&serving, 1, memory_order_release);
  }
  atomic_thread_fence(memory_order_seq_cst);
  atomic_signal_fence(memory_order_acq_rel);
  printf("tickets=%u serving=%u served=%u\n", atomic_load(&next_ticket), atomic_load(&serving), served);
  // reference counting
  atomic_int refs = 1;
  int frees = 0;
  for (int i = 0; i < 10; i++) atomic_fetch_add_explicit(&refs, 1, memory_order_relaxed);
  for (int i = 0; i < 11; i++)
    if (atomic_fetch_sub_explicit(&refs, 1, memory_order_acq_rel) == 1) frees++;
  printf("refs=%d frees=%d\n", atomic_load(&refs), frees);
}

int main(void) {
  fetch_ops();
  exchange_cas();
  flags_and_ptrs();
  operators();
  structures();
  return 0;
}
