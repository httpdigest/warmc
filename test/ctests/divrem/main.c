// Division and remainder, signed and unsigned, at 8, 16, 32 and 64 bits:
// negative dividends and divisors (C truncates toward zero and the remainder
// takes the dividend's sign), powers of two (which compilers turn into shifts
// with a bias for negative dividends), and constant divisors (which become a
// multiply by a magic reciprocal and a shift).  Also div/ldiv/lldiv, floor
// and Euclidean division built on top of truncation, and digit loops.
//
// INT_MIN / -1 is left out: it is undefined in C and traps in wasm.  So is
// any division by zero.  What remains is every case where a strength-reduced
// division, a sign fix-up or a narrow operand's extension can be off by one.
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int64_t nums[] = {
  0, 1, -1, 2, -2, 3, -3, 7, -7, 8, -8, 9, -9, 100, -100, 127, -128, 255,
  -256, 1000, -1001, 32767, -32768, 65535, 2147483647, -2147483647,
  -2147483647 - 1, 4294967295ll, 1234567890123ll, -1234567890123ll,
  INT64_MAX, INT64_MIN + 1, INT64_MIN,
};
#define NN (sizeof nums / sizeof nums[0])
int64_t dens[] = {1, -1, 2, -2, 3, -3, 4, 5, -5, 7, 8, -8, 10, 16, -16, 17,
                  100, 255, 256, -1000, 65536, 2147483647, -2147483647 - 1};
#define ND (sizeof dens / sizeof dens[0])

static void div64(void) {
  printf("-- int64 / %%\n");
  for (unsigned i = 0; i < NN; i++) {
    int64_t a = nums[i];
    printf("%lld:", (long long)a);
    for (unsigned j = 0; j < ND; j += 2) {
      int64_t b = dens[j];
      if (a == INT64_MIN && b == -1) continue;
      printf(" %lld,%lld", (long long)(a / b), (long long)(a % b));
    }
    printf("\n");
  }
}

static void div32(void) {
  printf("-- int32 / %% and uint32 / %%\n");
  for (unsigned i = 0; i < NN; i++) {
    int32_t a = (int32_t)nums[i];
    uint32_t ua = (uint32_t)nums[i];
    printf("%d|%u:", a, ua);
    for (unsigned j = 1; j < ND; j += 2) {
      int32_t b = (int32_t)dens[j];
      uint32_t ub = (uint32_t)dens[j];
      if (b == 0) continue;
      if (!(a == INT32_MIN && b == -1)) printf(" %d,%d", a / b, a % b);
      printf(" u%u,%u", ua / ub, ua % ub);
    }
    printf("\n");
  }
}

static void div_narrow(void) {
  printf("-- int8/int16/uint8/uint16\n");
  for (unsigned i = 0; i < NN; i++) {
    int8_t a8 = (int8_t)nums[i];
    uint8_t u8 = (uint8_t)nums[i];
    int16_t a16 = (int16_t)nums[i];
    uint16_t u16 = (uint16_t)nums[i];
    for (unsigned j = 0; j < ND; j += 3) {
      int8_t b8 = (int8_t)dens[j];
      int16_t b16 = (int16_t)dens[j];
      uint8_t ub8 = (uint8_t)dens[j];
      uint16_t ub16 = (uint16_t)dens[j];
      // narrow operands promote to int, so -128 / -1 is fine here
      if (b8) printf("[%d/%d=%d %%=%d] ", a8, b8, (int8_t)(a8 / b8), a8 % b8);
      if (b16) printf("[%d/%d=%d %%=%d] ", a16, b16, a16 / b16, a16 % b16);
      if (ub8) printf("[u%u/%u=%u] ", u8, ub8, u8 / ub8);
      if (ub16) printf("[u%u%%%u=%u] ", u16, ub16, u16 % ub16);
    }
    printf("\n");
  }
}

static void udiv64(void) {
  printf("-- uint64 / %%\n");
  for (unsigned i = 0; i < NN; i++) {
    uint64_t a = (uint64_t)nums[i];
    printf("%llu:", (unsigned long long)a);
    for (unsigned j = 0; j < ND; j += 2) {
      uint64_t b = (uint64_t)dens[j];
      printf(" %llu,%llu", (unsigned long long)(a / b),
             (unsigned long long)(a % b));
    }
    printf("\n");
  }
}

// Constant divisors: each line divides by literals, so clang emits
// multiply-high sequences, not div instructions.
static void constants(void) {
  printf("-- constant divisors\n");
  for (unsigned i = 0; i < NN; i++) {
    int32_t a = (int32_t)nums[i];
    uint32_t u = (uint32_t)nums[i];
    int64_t l = nums[i];
    uint64_t ul = (uint64_t)nums[i];
    printf("%d: %d %d %d %d %d %d | %d %d %d %d | %u %u %u %u %u | %lld %lld "
           "%lld %lld | %llu %llu %llu\n",
           a, a / 2, a / 3, a / 7, a / 10, a / -4, a / 1000, a % 2, a % 3,
           a % 16, a % -10, u / 3, u / 7, u / 10, u % 10, u / 641,
           (long long)(l / 3), (long long)(l / 10), (long long)(l % 7),
           (long long)(l / 1000000007), (unsigned long long)(ul / 10),
           (unsigned long long)(ul % 1000), (unsigned long long)(ul / 3));
  }
}

static void powers_of_two(void) {
  printf("-- powers of two\n");
  for (unsigned i = 0; i < NN; i++) {
    int32_t a = (int32_t)nums[i];
    int64_t l = nums[i];
    printf("%lld: %d %d %d %d %d %d | %lld %lld %lld %lld\n", (long long)l,
           a / 2, a % 2, a / 8, a % 8, a / 65536, a % 65536,
           (long long)(l / 4), (long long)(l % 4), (long long)(l / 4096),
           (long long)(l % 4294967296ll));
  }
}

static int64_t floordiv(int64_t a, int64_t b) {
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
  return q;
}
static int64_t floormod(int64_t a, int64_t b) {
  int64_t r = a % b;
  if (r != 0 && ((r < 0) != (b < 0))) r += b;
  return r;
}
static int64_t euclidmod(int64_t a, int64_t b) {
  int64_t r = a % b;
  if (r < 0) r += b < 0 ? -b : b;
  return r;
}

static void flavours(void) {
  printf("-- floor and euclidean\n");
  for (int a = -9; a <= 9; a += 2) {
    for (int b = -4; b <= 4; b++) {
      if (!b) continue;
      printf("(%d,%d t%d/%d f%lld/%lld e%lld) ", a, b, a / b, a % b,
             (long long)floordiv(a, b), (long long)floormod(a, b),
             (long long)euclidmod(a, b));
    }
    printf("\n");
  }
}

static void stdlib_div(void) {
  printf("-- div ldiv lldiv abs\n");
  for (unsigned i = 0; i < NN; i++) {
    for (unsigned j = 0; j < ND; j += 4) {
      int a = (int)nums[i], b = (int)dens[j];
      if (b == 0 || (a == INT32_MIN && b == -1)) continue;
      div_t d = div(a, b);
      ldiv_t ld = ldiv((long)a, (long)b);
      long long la = nums[i], lb = dens[j];
      if (la == INT64_MIN && lb == -1) continue;
      lldiv_t lld = lldiv(la, lb);
      printf("div(%d,%d)=%d,%d ldiv=%ld,%ld lldiv(%lld,%lld)=%lld,%lld\n", a,
             b, d.quot, d.rem, ld.quot, ld.rem, la, lb, lld.quot, lld.rem);
    }
    if (nums[i] != INT64_MIN) {
      int a = (int)nums[i];
      if (a != INT32_MIN)
        printf("abs(%d)=%d labs=%ld llabs=%lld\n", a, abs(a), labs((long)a),
               llabs(nums[i]));
    }
  }
}

static void digits(void) {
  printf("-- digit loops\n");
  for (unsigned i = 0; i < NN; i++) {
    uint64_t v = (uint64_t)nums[i];
    char buf[80];
    int n = 0;
    for (unsigned base = 2; base <= 36; base += 7) {
      uint64_t x = v;
      n = 0;
      do {
        unsigned d = (unsigned)(x % base);
        buf[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        x /= base;
      } while (x);
      printf("b%u:", base);
      while (n) putchar(buf[--n]);
      putchar(' ');
    }
    // digit sum of a signed value, truncating toward zero
    int64_t s = nums[i], ds = 0;
    while (s) {
      ds += s % 10;
      s /= 10;
    }
    printf("dsum=%lld\n", (long long)ds);
  }
}

static void gcds(void) {
  printf("-- gcd/lcm\n");
  for (unsigned i = 0; i < NN; i++) {
    for (unsigned j = 0; j < ND; j += 5) {
      uint64_t a = (uint64_t)(nums[i] < 0 ? -(nums[i] + 1) : nums[i]);
      uint64_t b = (uint64_t)(dens[j] < 0 ? -(dens[j] + 1) : dens[j]);
      uint64_t x = a, y = b;
      while (y) {
        uint64_t t = x % y;
        x = y;
        y = t;
      }
      printf("gcd(%llu,%llu)=%llu ", (unsigned long long)a,
             (unsigned long long)b, (unsigned long long)x);
    }
    printf("\n");
  }
}

int main(void) {
  div64();
  div32();
  div_narrow();
  udiv64();
  constants();
  powers_of_two();
  flavours();
  stdlib_div();
  digits();
  gcds();
  return 0;
}
