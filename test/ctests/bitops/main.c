// Bit-manipulation builtins and the loops they replace: popcount, clz, ctz,
// ffs, parity, bswap at 16/32/64 bits, clrsb, bit reversal, plus the
// hand-written versions (Kernighan's popcount, a binary-search clz, byte-wise
// bswap) checked against the builtins on the same inputs.
//
// wasm has i32/i64 popcnt, clz and ctz, but no bswap, parity or ffs, so those
// become sequences; and clz/ctz of zero are defined in wasm while the C
// builtins leave them undefined, so the program only asks the builtins about
// non-zero values and lets the hand-written loops cover zero.  A compiler that
// recognises the loops as the instruction, or that folds an extension into a
// count, gets compared against the one that did not.
#include <stdint.h>
#include <stdio.h>

uint64_t words[] = {
  0, 1, 2, 3, 0x80, 0xff, 0x100, 0x8000, 0xffff, 0x10000, 0x7fffffff,
  0x80000000, 0xffffffff, 0x100000000ull, 0x8000000000000000ull,
  0xffffffffffffffffull, 0x0123456789abcdefull, 0xf0f0f0f0f0f0f0f0ull,
  0x00ff00ff00ff00ffull, 0x5555555555555555ull, 0xdeadbeefcafebabeull,
  0x0000010000000000ull, 0x8000000000000001ull, 12345678987654321ull,
};
#define NW (sizeof words / sizeof words[0])

static int pop_loop(uint64_t x) {
  int n = 0;
  while (x) {
    x &= x - 1;
    n++;
  }
  return n;
}
static int clz_loop(uint64_t x) {
  if (!x) return 64;
  int n = 0;
  if (!(x >> 32)) { n += 32; x <<= 32; }
  if (!(x >> 48)) { n += 16; x <<= 16; }
  if (!(x >> 56)) { n += 8; x <<= 8; }
  if (!(x >> 60)) { n += 4; x <<= 4; }
  if (!(x >> 62)) { n += 2; x <<= 2; }
  if (!(x >> 63)) { n += 1; }
  return n;
}
static int ctz_loop(uint64_t x) {
  if (!x) return 64;
  int n = 0;
  while (!(x & 1)) {
    x >>= 1;
    n++;
  }
  return n;
}
static uint64_t bswap_loop(uint64_t x) {
  uint64_t r = 0;
  for (int i = 0; i < 8; i++) {
    r = (r << 8) | (x & 0xff);
    x >>= 8;
  }
  return r;
}
static uint32_t rev32(uint32_t x) {
  x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
  x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
  x = ((x >> 4) & 0x0f0f0f0fu) | ((x & 0x0f0f0f0fu) << 4);
  return __builtin_bswap32(x);
}
static uint64_t rev64_loop(uint64_t x) {
  uint64_t r = 0;
  for (int i = 0; i < 64; i++) {
    r = (r << 1) | (x & 1);
    x >>= 1;
  }
  return r;
}

static void builtins64(void) {
  printf("-- 64-bit\n");
  for (unsigned i = 0; i < NW; i++) {
    uint64_t x = words[i];
    int pc = __builtin_popcountll(x);
    int cl = x ? __builtin_clzll(x) : 64;
    int ct = x ? __builtin_ctzll(x) : 64;
    int ff = __builtin_ffsll((long long)x);
    int par = __builtin_parityll(x);
    int crs = __builtin_clrsbll((long long)x);
    printf("%016llx pop=%d/%d clz=%d/%d ctz=%d/%d ffs=%d par=%d clrsb=%d "
           "bswap=%016llx/%016llx rev=%016llx\n",
           (unsigned long long)x, pc, pop_loop(x), cl, clz_loop(x), ct,
           ctz_loop(x), ff, par, crs, (unsigned long long)__builtin_bswap64(x),
           (unsigned long long)bswap_loop(x), (unsigned long long)rev64_loop(x));
  }
}

static void builtins32(void) {
  printf("-- 32-bit\n");
  for (unsigned i = 0; i < NW; i++) {
    uint32_t x = (uint32_t)(words[i] ^ (words[i] >> 32));
    int pc = __builtin_popcount(x);
    int cl = x ? __builtin_clz(x) : 32;
    int ct = x ? __builtin_ctz(x) : 32;
    printf("%08x pop=%d clz=%d ctz=%d ffs=%d par=%d clrsb=%d bswap=%08x "
           "rev=%08x/%08x\n",
           x, pc, cl, ct, __builtin_ffs((int)x), __builtin_parity(x),
           __builtin_clrsb((int)x), __builtin_bswap32(x), rev32(x),
           (uint32_t)(rev64_loop(x) >> 32));
  }
}

static void builtins16(void) {
  printf("-- 16-bit and 8-bit\n");
  for (unsigned i = 0; i < NW; i++) {
    uint16_t h = (uint16_t)(words[i] >> 4);
    uint8_t b = (uint8_t)(words[i] >> 3);
    printf("%04x bswap16=%04x pop=%d clz16=%d | %02x pop=%d clz8=%d ctz8=%d\n",
           h, __builtin_bswap16(h), __builtin_popcount(h),
           h ? __builtin_clz(h) - 16 : 16, b, __builtin_popcount(b),
           b ? __builtin_clz(b) - 24 : 8, b ? __builtin_ctz(b) : 8);
  }
}

static void derived(void) {
  printf("-- derived quantities\n");
  for (unsigned i = 0; i < NW; i++) {
    uint64_t x = words[i];
    int log2f = x ? 63 - __builtin_clzll(x) : -1;
    uint64_t next_pow2 = x <= 1 ? 1 : (x > (1ull << 63) ? 0 : 1ull << (64 - __builtin_clzll(x - 1)));
    uint64_t lowest = x & (~x + 1);
    uint64_t highest = x ? 1ull << log2f : 0;
    uint64_t smeared = x;
    smeared |= smeared >> 1;
    smeared |= smeared >> 2;
    smeared |= smeared >> 4;
    smeared |= smeared >> 8;
    smeared |= smeared >> 16;
    smeared |= smeared >> 32;
    // next number with the same popcount (Gosper's hack), for nonzero x
    uint64_t gosper = 0;
    if (x && x != ~0ull) {
      uint64_t c = x & -x, r = x + c;
      gosper = c ? (((r ^ x) >> 2) / c) | r : 0;
    }
    printf("%016llx log2=%d np2=%llx low=%llx high=%llx smear=%llx gosper=%llx "
           "gray=%llx\n",
           (unsigned long long)x, log2f, (unsigned long long)next_pow2,
           (unsigned long long)lowest, (unsigned long long)highest,
           (unsigned long long)smeared, (unsigned long long)gosper,
           (unsigned long long)(x ^ (x >> 1)));
  }
}

static void bitsets(void) {
  printf("-- bitset operations\n");
  uint64_t set[4] = {0, 0, 0, 0};
  for (int i = 0; i < 256; i += 3) set[i >> 6] |= 1ull << (i & 63);
  for (int i = 0; i < 256; i += 7) set[i >> 6] ^= 1ull << (i & 63);
  for (int w = 0; w < 4; w++)
    printf("set[%d]=%016llx pop=%d\n", w, (unsigned long long)set[w],
           __builtin_popcountll(set[w]));
  // iterate the members using ctz
  int count = 0, sum = 0;
  printf("members:");
  for (int w = 0; w < 4; w++) {
    uint64_t m = set[w];
    while (m) {
      int b = __builtin_ctzll(m);
      int idx = w * 64 + b;
      if (count % 16 == 0) printf("\n ");
      printf(" %d", idx);
      sum += idx;
      count++;
      m &= m - 1;
    }
  }
  printf("\ncount=%d sum=%d\n", count, sum);
  // population counts of all bytes 0..255 grouped
  int hist[9] = {0};
  for (int v = 0; v < 256; v++) hist[__builtin_popcount(v)]++;
  for (int k = 0; k <= 8; k++) printf("pop%d=%d ", k, hist[k]);
  printf("\n");
}

int main(void) {
  builtins64();
  builtins32();
  builtins16();
  derived();
  bitsets();
  return 0;
}
