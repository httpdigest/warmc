// Integer promotions and the usual arithmetic conversions, the corners where
// C's answer surprises: unsigned char and unsigned short promoting to (signed)
// int, comparisons between signed and unsigned operands (-1 < 1u is false),
// int vs unsigned long long and long long vs unsigned (where the wider signed
// type wins on wasm32 because it can hold every unsigned value), narrowing
// conversions that wrap modulo 2^N, sign extension of char and short through
// casts, ~ and - on small unsigned types, shifts of promoted operands, the
// conditional operator's common type, bool conversion of non-zero values
// including fractions and NaN, integer-to-float conversions inside mixed
// expressions, and compound assignment narrowing back to the target's width.
//
// Every one of these is a zero- or sign-extension, a wrap, or a choice
// between signed and unsigned compare that the compiler had to place; a
// back end that tracks a value's width wrongly gets one of them wrong.  The
// operands are read from volatile objects so the code is not folded away.
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static volatile unsigned char vuc[] = {0, 1, 127, 128, 200, 255};
static volatile signed char vsc[] = {0, 1, -1, 127, -128, -100};
static volatile unsigned short vus[] = {0, 1, 32767, 32768, 65535, 40000};
static volatile short vss[] = {0, -1, 32767, -32768, 1000, -12345};
static volatile int vi[] = {0, 1, -1, INT_MAX, INT_MIN, 100000};
static volatile unsigned vu[] = {0, 1, 0xffffffffu, 0x80000000u, 100000u, 7u};
static volatile long long vll[] = {0, -1, LLONG_MAX, LLONG_MIN, 1ll << 40, -5};
static volatile unsigned long long vull[] = {0, 1, ULLONG_MAX, 1ull << 63, 1ull << 32, 42};

static void small_types(void) {
  printf("-- small unsigned types promote to int\n");
  for (int i = 0; i < 6; i++) {
    unsigned char a = vuc[i];
    unsigned short b = vus[i];
    int notc = ~a;            // int, so negative
    unsigned char notc8 = (unsigned char)~a;
    int negc = -a;
    printf("uc=%3u ~=%d (u8 %u) -=%d cmp(~a>0)=%d b=%5u ~b=%d b*2=%d b>>1=%d a-b=%d (a-b)>0u=%d\n", a,
           notc, notc8, negc, ~a > 0, b, ~b, b * 2, b >> 1, a - b, (unsigned)(a - b) > 0u);
  }
  for (int i = 0; i < 6; i++) {
    signed char c = vsc[i];
    short s = vss[i];
    printf("sc=%4d as unsigned=%u as u8=%u as u16=%u | ss=%6d as unsigned=%u as u16=%u as u8=%u as s8=%d\n", c,
           (unsigned)c, (unsigned char)c, (unsigned short)c, s, (unsigned)s, (unsigned short)s, (unsigned char)s,
           (signed char)s);
  }
}

static void mixed_compare(void) {
  printf("-- signed/unsigned comparisons\n");
  for (int i = 0; i < 6; i++) {
    for (int j = 0; j < 6; j++) {
      int a = vi[i];
      unsigned b = vu[j];
      long long c = vll[j];
      unsigned long long d = vull[i];
      printf("%d%d%d%d%d%d ", a < b, (long long)a < b, a < c, c < d, a == b, (unsigned char)a == (unsigned char)b);
    }
    printf("\n");
  }
  printf("-1 < 1u: %d, -1 < 1ll: %d, -1L < 1u: %d, -1LL < 1u: %d\n", -1 < 1u, -1 < 1ll, -1L < 1u, -1LL < 1u);
  printf("types: sizeof(1u + 1ll)=%zu sizeof(1 + 1ull)=%zu (-1 + 0u)=%u ((char)-1 == 255)=%d\n", sizeof(1u + 1ll),
         sizeof(1 + 1ull), -1 + 0u, (char)-1 == 255);
}

static void narrowing(void) {
  printf("-- narrowing conversions\n");
  for (int i = 0; i < 6; i++) {
    long long v = vll[i];
    unsigned long long u = vull[i];
    int x = vi[i];
    printf("ll=%lld -> i32=%d u32=%u i16=%d u16=%u i8=%d u8=%u | ull -> i32=%d u8=%u | int %d -> i8=%d u16=%u\n", v,
           (int)v, (unsigned)v, (short)v, (unsigned short)v, (signed char)v, (unsigned char)v, (int)u,
           (unsigned char)u, x, (signed char)x, (unsigned short)x);
  }
  for (int i = 0; i < 6; i++) {
    uint8_t acc8 = vuc[i];
    int16_t acc16 = vss[i];
    uint32_t acc32 = vu[i];
    for (int k = 0; k < 5; k++) {
      acc8 += 100;      // computed in int, narrowed back
      acc8 *= 3;
      acc16 -= 20000;   // wraps modulo 2^16 in the conversion
      acc32 *= 3;
      acc32 -= 0x10000000u;
    }
    printf("compound: acc8=%u acc16=%d acc32=%u\n", acc8, acc16, acc32);
  }
}

static void shifts_and_bool(void) {
  printf("-- shifts of promoted operands, bool\n");
  for (int i = 0; i < 6; i++) {
    unsigned char a = vuc[i];
    unsigned short b = vus[i];
    unsigned u = vu[i];
    // a << 23 is an int shift of a value < 256: fits
    printf("a<<23=%d (unsigned)a<<24=%u b<<15=%d u>>31=%u (u8)(a<<4)=%u (a<<4)>>4=%d\n", a << 23,
           (unsigned)a << 24, b << 15, u >> 31, (unsigned char)(a << 4), (a << 4) >> 4);
  }
  double ds[] = {0.0, -0.0, 0.5, -1e-300, 2.0};
  for (int i = 0; i < 5; i++) {
    bool b = ds[i];
    int ii = (int)ds[i];
    printf("bool(%g)=%d int=%d !!=%d\n", ds[i], b, ii, !!ds[i]);
  }
  volatile double nan = 0.0;
  nan = nan / nan;
  bool bn = nan;
  printf("bool(nan)=%d\n", bn);
  bool flags[4] = {vi[0], vi[1], vi[2], vi[3]};
  printf("bool from ints: %d %d %d %d sum=%d\n", flags[0], flags[1], flags[2], flags[3],
         flags[0] + flags[1] + flags[2] + flags[3]);
  bool t = 1;
  t += 1;  // still true
  bool f = 0;
  f = f - 1;  // -1 converts to true
  printf("bool arithmetic: t=%d f=%d\n", t, f);
}

static void conditional(void) {
  printf("-- conditional operator common type\n");
  for (int i = 0; i < 6; i++) {
    int c = i & 1;
    long long r1 = c ? vi[i] : vu[i];          // int vs unsigned -> unsigned
    long long r2 = c ? vi[i] : vll[i];         // -> long long
    double r3 = c ? vi[i] : 0.5;               // -> double
    unsigned long long r4 = c ? vsc[i] : vull[i];  // -> unsigned long long
    printf("c=%d r1=%lld r2=%lld r3=%g r4=%llu\n", c, r1, r2, r3, r4);
  }
}

static void mixed_float(void) {
  printf("-- mixed integer/float expressions\n");
  for (int i = 0; i < 6; i++) {
    int a = vi[i];
    unsigned u = vu[i];
    long long l = vll[i];
    unsigned long long q = vull[i];
    float f = 1.5f;
    printf("a/2.0=%.10g a*f=%.9g u+0.5=%.10g l*1.0=%.17g q*1.0=%.17g u/2=%u (float)u=%.9g\n", a / 2.0,
           (double)(a * f), u + 0.5, l * 1.0, q * 1.0, u / 2, (double)(float)u);
  }
  // integer division happens before the conversion
  int n = 7, d = 2;
  printf("7/2*1.0=%g 1.0*7/2=%g (double)7/2=%g 7/(double)2=%g\n", n / d * 1.0, 1.0 * n / d, (double)n / d,
         n / (double)d);
  char c = 'A';
  printf("char arith: %d %c %d\n", c + 1, c + 1, 'z' - c);
  unsigned char uc = 250;
  uc += 10;  // 260 narrows to 4
  printf("uc after += 10: %u\n", uc);
  unsigned short us = 65535;
  us++;
  printf("us after ++: %u\n", us);
  int big = 46341;
  long long sq = (long long)big * big;  // widen before multiplying
  printf("46341^2 = %lld\n", sq);
}

int main(void) {
  small_types();
  mixed_compare();
  narrowing();
  shifts_and_bool();
  conditional();
  mixed_float();
  return 0;
}
