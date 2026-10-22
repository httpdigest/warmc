// Integer arithmetic at every width: 8-, 16-, 32- and 64-bit, signed and
// unsigned -- add, sub, mul, negate, complement, the wrap that unsigned
// arithmetic defines, and the overflow flags of __builtin_*_overflow, which
// are how signed overflow is observed without being undefined.  Then the
// widening products (32x32 to 64, 64x64 to 128), signed/unsigned comparisons
// at each width, and a few accumulation loops that wrap many times.
//
// Every operand comes from a global table with external linkage, so clang
// cannot fold the arithmetic away: the generated code does it.  wasm has only
// i32 and i64, so every 8- and 16-bit operation is an i32 operation followed by
// a sign or zero extension that the compiler has to place correctly -- the
// first thing to go wrong when an optimiser drops an extension it believed
// redundant.  Each result is printed so a single wrong carry or extension is a
// one-line diff.
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

uint64_t vals[] = {
  0, 1, 2, 3, 7, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff, 0x10000,
  0x7fffffff, 0x80000000, 0xffffffff, 0x100000000ull, 0x123456789abcdef0ull,
  0x7fffffffffffffffull, 0x8000000000000000ull, 0xfffffffffffffffeull,
  0xffffffffffffffffull, 12345, 0xdeadbeef, 0xcafebabe12345678ull,
  0xfffffffffffff000ull, 0x5555555555555555ull, 0xaaaaaaaaaaaaaaaaull,
};
#define NV (sizeof vals / sizeof vals[0])

#define DEF_U(N, W)                                                           \
  static void test_u##N(void) {                                               \
    printf("-- uint%d_t\n", N);                                               \
    for (unsigned i = 0; i < NV; i++) {                                       \
      uint##N##_t a = (uint##N##_t)vals[i];                                   \
      uint##N##_t b = (uint##N##_t)vals[(i * 7 + 3) % NV];                    \
      uint##N##_t s = (uint##N##_t)((W)a + (W)b);                             \
      uint##N##_t d = (uint##N##_t)((W)a - (W)b);                             \
      uint##N##_t m = (uint##N##_t)((W)a * (W)b);                             \
      uint##N##_t n = (uint##N##_t)(-(W)a);                                   \
      uint##N##_t c = (uint##N##_t)~a;                                        \
      uint##N##_t r1, r2, r3;                                                 \
      int o1 = __builtin_add_overflow(a, b, &r1);                             \
      int o2 = __builtin_sub_overflow(a, b, &r2);                             \
      int o3 = __builtin_mul_overflow(a, b, &r3);                             \
      printf("%2u a=%llx b=%llx add=%llx sub=%llx mul=%llx neg=%llx not=%llx" \
             " of=%d%d%d lt=%d le=%d eq=%d\n",                                \
             i, (unsigned long long)a, (unsigned long long)b,                 \
             (unsigned long long)s, (unsigned long long)d,                    \
             (unsigned long long)m, (unsigned long long)n,                    \
             (unsigned long long)c, o1, o2, o3, a < b, a <= b, a == b);       \
      if (r1 != s || r2 != d || r3 != m) printf("   builtin result mismatch\n"); \
    }                                                                         \
  }

DEF_U(8, unsigned)
DEF_U(16, unsigned)
DEF_U(32, unsigned)
DEF_U(64, uint64_t)

#define DEF_S(N)                                                              \
  static void test_s##N(void) {                                               \
    printf("-- int%d_t\n", N);                                                \
    for (unsigned i = 0; i < NV; i++) {                                       \
      int##N##_t a = (int##N##_t)vals[i];                                     \
      int##N##_t b = (int##N##_t)vals[(i * 5 + 1) % NV];                      \
      int##N##_t r1, r2, r3, r4;                                              \
      int o1 = __builtin_add_overflow(a, b, &r1);                             \
      int o2 = __builtin_sub_overflow(a, b, &r2);                             \
      int o3 = __builtin_mul_overflow(a, b, &r3);                             \
      int o4 = __builtin_sub_overflow((int##N##_t)0, a, &r4);                 \
      long long wide_add, wide_mul;                                           \
      int ow = __builtin_add_overflow((long long)a, (long long)b, &wide_add); \
      int om = __builtin_mul_overflow((long long)a, (long long)b, &wide_mul); \
      printf("%2u a=%lld b=%lld add=%lld/%d sub=%lld/%d mul=%lld/%d"         \
             " neg=%lld/%d wide=%lld/%d,%lld/%d lt=%d ge=%d sgn=%d\n",        \
             i, (long long)a, (long long)b, (long long)r1, o1, (long long)r2, \
             o2, (long long)r3, o3, (long long)r4, o4, wide_add, ow,          \
             wide_mul, om, a < b, a >= b, (a > 0) - (a < 0));                 \
    }                                                                         \
  }

DEF_S(8)
DEF_S(16)
DEF_S(32)
DEF_S(64)

static void widening(void) {
  printf("-- widening products\n");
  for (unsigned i = 0; i < NV; i += 2) {
    uint32_t a = (uint32_t)vals[i], b = (uint32_t)vals[(i * 3 + 5) % NV];
    int32_t sa = (int32_t)a, sb = (int32_t)b;
    uint64_t um = (uint64_t)a * b;
    int64_t sm = (int64_t)sa * sb;
    uint64_t A = vals[i], B = vals[(i * 11 + 2) % NV];
    unsigned __int128 p = (unsigned __int128)A * B;
    __int128 q = (__int128)(int64_t)A * (int64_t)B;
    printf("%2u u32x32=%016llx s32x32=%lld u64hi=%016llx u64lo=%016llx "
           "s64hi=%016llx s64lo=%016llx\n",
           i, (unsigned long long)um, (long long)sm,
           (unsigned long long)(p >> 64), (unsigned long long)p,
           (unsigned long long)((unsigned __int128)q >> 64),
           (unsigned long long)q);
  }
}

static void mixed(void) {
  printf("-- mixed widths\n");
  for (unsigned i = 0; i < NV; i += 3) {
    int8_t c = (int8_t)vals[i];
    uint8_t uc = (uint8_t)vals[i];
    int16_t h = (int16_t)vals[(i + 4) % NV];
    uint16_t uh = (uint16_t)vals[(i + 4) % NV];
    int32_t w = (int32_t)vals[(i + 9) % NV];
    uint32_t uw = (uint32_t)vals[(i + 9) % NV];
    int64_t l = (int64_t)(c * h) + w;               // promoted to int, fits
    uint32_t m1 = uc * (uint32_t)uh + uw;           // wraps in 32 bits
    int64_t m2 = (int64_t)c * uw;                   // sign-extend then multiply
    uint64_t m3 = (uint64_t)(uint32_t)c * uh;       // c converted modulo 2^32
    int t1 = c + uc;                                // both promote to int
    unsigned t2 = (unsigned)c + uw;
    printf("%2u c=%d uc=%u h=%d uh=%u w=%d uw=%u l=%lld m1=%u m2=%lld m3=%llu "
           "t1=%d t2=%u\n",
           i, c, uc, h, uh, w, uw, (long long)l, m1, (long long)m2,
           (unsigned long long)m3, t1, t2);
  }
}

static void accumulate(void) {
  printf("-- accumulation\n");
  uint8_t s8 = 0;
  uint16_t s16 = 1;
  uint32_t s32 = 2;
  uint64_t s64 = 3;
  int32_t alt = 0;
  for (unsigned r = 0; r < 4; r++) {
    for (unsigned i = 0; i < NV; i++) {
      uint64_t v = vals[i];
      s8 = (uint8_t)(s8 * 31 + (uint8_t)v);
      s16 = (uint16_t)(s16 * 131u + (uint16_t)v);
      s32 = s32 * 2654435761u + (uint32_t)v;
      s64 = s64 * 6364136223846793005ull + v;
      alt += (i & 1) ? (int32_t)(v & 0xffff) : -(int32_t)(v & 0x7fff);
    }
    printf("round %u: s8=%u s16=%u s32=%u s64=%llu alt=%d\n", r, s8, s16, s32,
           (unsigned long long)s64, alt);
  }
  // Count down through zero in unsigned arithmetic: the wrap is defined.
  uint32_t u = 3;
  for (int i = 0; i < 6; i++) {
    printf("u=%u ", u);
    u--;
  }
  printf("\n");
  uint8_t b = 250;
  for (int i = 0; i < 10; i++) {
    printf("%u ", b);
    b += 2;
  }
  printf("\n");
}

static void idioms(void) {
  printf("-- idioms\n");
  for (unsigned i = 0; i < NV; i++) {
    uint32_t a = (uint32_t)vals[i], b = (uint32_t)vals[(i * 13 + 7) % NV];
    uint32_t avg = (a & b) + ((a ^ b) >> 1);          // floor((a+b)/2)
    uint32_t sat = a + b < a ? 0xffffffffu : a + b;   // saturating add
    uint32_t mn = b ^ ((a ^ b) & -(uint32_t)(a < b)); // branch-free min
    uint32_t mx = a ^ ((a ^ b) & -(uint32_t)(a < b)); // branch-free max
    uint32_t diff = a > b ? a - b : b - a;
    int32_t sa = (int32_t)a;
    int32_t sabs = sa < 0 && sa != INT32_MIN ? -sa : sa;
    uint32_t pow2 = a && !(a & (a - 1));
    uint32_t lowbit = a & -a;
    uint32_t clear_low = a & (a - 1);
    printf("%2u avg=%u sat=%u min=%u max=%u diff=%u abs=%d pow2=%u low=%x "
           "clr=%x\n",
           i, avg, sat, mn, mx, diff, sabs, pow2, lowbit, clear_low);
  }
}

int main(void) {
  test_u8();
  test_u16();
  test_u32();
  test_u64();
  test_s8();
  test_s16();
  test_s32();
  test_s64();
  widening();
  mixed();
  accumulate();
  idioms();
  return 0;
}
