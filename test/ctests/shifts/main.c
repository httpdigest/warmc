// Shifts and rotates at 8, 16, 32 and 64 bits: logical right shift of
// unsigned values, arithmetic right shift of negative signed ones, left shifts
// that push bits off the top of an unsigned value, every shift count from 0 to
// width-1 with the count both constant and variable, and rotates written as
// the (x << n) | (x >> (w - n)) idiom that compilers turn into a rotate
// instruction, plus the __builtin_rotate* forms.
//
// wasm's shifts take the count modulo the width, and a narrow type is shifted
// in an i32, so `(uint8_t)x >> n` needs its operand zero-extended first and an
// int8_t needs a sign extension -- the extension and the masking of the count
// are each something a code generator can get subtly wrong when it folds or
// fuses.  Counts are kept strictly below the width; shifting a negative value
// left is avoided, as C leaves it undefined.
#include <stdint.h>
#include <stdio.h>

uint64_t pat[] = {
  0x0000000000000001ull, 0x8000000000000000ull, 0x00000000ffffffffull,
  0xffffffff00000000ull, 0x0123456789abcdefull, 0xfedcba9876543210ull,
  0x5a5a5a5a5a5a5a5aull, 0xffffffffffffffffull, 0x00000000000000f0ull,
  0x8000000080000080ull,
};
#define NP (sizeof pat / sizeof pat[0])
int counts[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 47, 63};
#define NC (sizeof counts / sizeof counts[0])

static uint8_t rotl8(uint8_t x, unsigned n) {
  n &= 7;
  return (uint8_t)((x << n) | (x >> ((8 - n) & 7)));
}
static uint16_t rotr16(uint16_t x, unsigned n) {
  n &= 15;
  return (uint16_t)((x >> n) | (x << ((16 - n) & 15)));
}
static uint32_t rotl32(uint32_t x, unsigned n) {
  return (x << (n & 31)) | (x >> ((32 - n) & 31));
}
static uint32_t rotr32(uint32_t x, unsigned n) {
  return (x >> (n & 31)) | (x << ((32 - n) & 31));
}
static uint64_t rotl64(uint64_t x, unsigned n) {
  return (x << (n & 63)) | (x >> ((64 - n) & 63));
}
static uint64_t rotr64(uint64_t x, unsigned n) {
  return (x >> (n & 63)) | (x << ((64 - n) & 63));
}

static void shifts64(void) {
  printf("-- 64-bit variable shifts\n");
  for (unsigned i = 0; i < NP; i++) {
    for (unsigned j = 0; j < NC; j++) {
      int n = counts[j];
      uint64_t u = pat[i];
      int64_t s = (int64_t)u;
      printf("%016llx %2d shl=%016llx shr=%016llx sar=%016llx rol=%016llx "
             "ror=%016llx\n",
             (unsigned long long)u, n, (unsigned long long)(u << n),
             (unsigned long long)(u >> n), (unsigned long long)(s >> n),
             (unsigned long long)rotl64(u, n), (unsigned long long)rotr64(u, n));
    }
  }
}

static void shifts32(void) {
  printf("-- 32-bit variable shifts\n");
  for (unsigned i = 0; i < NP; i++) {
    for (unsigned j = 0; j < NC; j++) {
      int n = counts[j] & 31;
      uint32_t u = (uint32_t)(pat[i] >> (i & 1 ? 32 : 0));
      int32_t s = (int32_t)u;
      printf("%08x %2d shl=%08x shr=%08x sar=%08x rol=%08x ror=%08x "
             "brol=%08x\n",
             u, n, u << n, u >> n, (uint32_t)(s >> n), rotl32(u, n),
             rotr32(u, n), __builtin_rotateleft32(u, n));
    }
  }
}

static void shifts_narrow(void) {
  printf("-- 8/16-bit shifts\n");
  for (unsigned i = 0; i < NP; i++) {
    uint8_t b = (uint8_t)pat[i];
    int8_t sb = (int8_t)(pat[i] >> 56);
    uint16_t h = (uint16_t)(pat[i] >> 8);
    int16_t sh = (int16_t)(pat[i] >> 48);
    for (int n = 0; n < 8; n++) {
      uint8_t l8 = (uint8_t)(b << n);
      uint8_t r8 = (uint8_t)(b >> n);
      int8_t a8 = (int8_t)(sb >> n);
      uint16_t l16 = (uint16_t)(h << (2 * n));
      uint16_t r16 = (uint16_t)(h >> (2 * n));
      int16_t a16 = (int16_t)(sh >> (2 * n));
      printf("%02x %d l=%02x r=%02x a=%d rol=%02x | %04x l=%04x r=%04x a=%d "
             "ror=%04x\n",
             b, n, l8, r8, a8, rotl8(b, n), h, l16, r16, a16,
             rotr16(h, 2 * n + 1));
    }
  }
}

// Constant counts: each one its own instruction rather than a register count.
#define CS(x, k)                                                              \
  printf("k=%2d %016llx %016llx %016llx %08x %08x\n", k,                      \
         (unsigned long long)((x) << k), (unsigned long long)((x) >> k),      \
         (unsigned long long)((int64_t)(x) >> k),                             \
         (uint32_t)((uint32_t)(x) << (k & 31)),                               \
         (uint32_t)((int32_t)(uint32_t)(x) >> (k & 31)))

static void constant_counts(uint64_t x) {
  printf("-- constant counts on %016llx\n", (unsigned long long)x);
  CS(x, 0); CS(x, 1); CS(x, 3); CS(x, 7); CS(x, 8); CS(x, 12); CS(x, 16);
  CS(x, 24); CS(x, 31); CS(x, 32); CS(x, 33); CS(x, 40); CS(x, 48);
  CS(x, 56); CS(x, 62); CS(x, 63);
}

static void field_extract(void) {
  printf("-- field extraction and insertion\n");
  for (unsigned i = 0; i < NP; i++) {
    uint64_t x = pat[i];
    for (int lo = 0; lo < 64; lo += 13) {
      for (int w = 1; w <= 20 && lo + w <= 64; w += 6) {
        uint64_t mask = (w == 64) ? ~0ull : ((1ull << w) - 1);
        uint64_t f = (x >> lo) & mask;
        int64_t sf = (int64_t)(f << (64 - w)) >> (64 - w);  // sign-extend field
        uint64_t ins = (x & ~(mask << lo)) | ((~f & mask) << lo);
        printf("%016llx lo=%2d w=%2d f=%llx sf=%lld ins=%016llx\n",
               (unsigned long long)x, lo, w, (unsigned long long)f,
               (long long)sf, (unsigned long long)ins);
      }
    }
  }
}

static void shift_loops(void) {
  printf("-- shift loops\n");
  uint64_t x = 1;
  for (int i = 0; i < 64; i += 5) {
    printf("%2d %016llx %llu\n", i, (unsigned long long)(x << i),
           (unsigned long long)((~0ull) >> i));
  }
  int64_t neg = -1234567890123ll;
  for (int i = 0; i < 64; i += 7) printf("sar %2d %lld\n", i, (long long)(neg >> i));
  int32_t n32 = -100;
  for (int i = 0; i < 32; i += 3) printf("sar32 %2d %d %u\n", i, n32 >> i, (uint32_t)n32 >> i);
  // Multiplication and division by powers of two through shifts.
  for (int i = 0; i < 12; i++) {
    int32_t v = -1000 + 371 * i;
    printf("v=%d v<<3 as mul=%d v/8=%d v>>3=%d (v+7)>>3=%d\n", v, v * 8, v / 8,
           v >> 3, (v + 7) >> 3);
  }
}

int main(void) {
  shifts64();
  shifts32();
  shifts_narrow();
  constant_counts(pat[4]);
  constant_counts(pat[5]);
  constant_counts(pat[9]);
  field_extract();
  shift_loops();
  return 0;
}
