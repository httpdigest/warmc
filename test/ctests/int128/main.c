// __int128 and unsigned __int128: add and subtract with the carry crossing
// the 64-bit halves, multiply (a 64x64 multiply-high plus cross terms in wasm,
// which has no 128-bit type), division and remainder (compiler-rt's __divti3,
// __udivti3, __modti3, __umodti3), shifts by every count 0..127, comparisons
// whose answer is decided by the low half when the high halves tie, and
// conversions to and from double and 64-bit integers.
//
// Every 128-bit value lives in two i64 locals or a pair of stack slots, so this
// is the test of a code generator's multi-register values: a carry that is
// lost, a high half taken from the wrong register, or a shift that only
// handles counts below 64.  Decimal printing is done by repeated division, so
// the divide routines are exercised again on every line.
#include <stdint.h>
#include <stdio.h>

typedef unsigned __int128 u128;
typedef __int128 i128;

static u128 mk(uint64_t hi, uint64_t lo) { return ((u128)hi << 64) | lo; }

static void put_u128(u128 v) {
  char buf[48];
  int n = 0;
  do {
    buf[n++] = (char)('0' + (int)(v % 10));
    v /= 10;
  } while (v);
  while (n) putchar(buf[--n]);
}
static void put_i128(i128 v) {
  if (v < 0) {
    putchar('-');
    put_u128(-(u128)v);
  } else {
    put_u128((u128)v);
  }
}
static void put_hex(u128 v) {
  printf("%016llx%016llx", (unsigned long long)(v >> 64), (unsigned long long)v);
}

uint64_t parts[][2] = {
  {0, 0}, {0, 1}, {0, 0xffffffffffffffffull}, {1, 0},
  {0x7fffffffffffffffull, 0xffffffffffffffffull},
  {0x8000000000000000ull, 0}, {0xffffffffffffffffull, 0xffffffffffffffffull},
  {0x0123456789abcdefull, 0xfedcba9876543210ull}, {0, 10000000000000000000ull},
  {0x4b3b4ca85a86c47aull, 0x098a224000000000ull},  // 10^38
  {0xffffffffffffffffull, 0xfffffffffffffff6ull},  // -10
  {0, 3}, {2, 5}, {0xdeadbeefull, 0xcafebabe00000000ull},
};
#define NP (sizeof parts / sizeof parts[0])

static void arith(void) {
  printf("-- add sub mul\n");
  for (unsigned i = 0; i < NP; i++) {
    u128 a = mk(parts[i][0], parts[i][1]);
    u128 b = mk(parts[(i * 5 + 3) % NP][0], parts[(i * 5 + 3) % NP][1]);
    printf("a=");
    put_hex(a);
    printf(" b=");
    put_hex(b);
    printf("\n  a+b=");
    put_hex(a + b);
    printf(" a-b=");
    put_hex(a - b);
    printf("\n  a*b=");
    put_hex(a * b);
    printf(" -a=");
    put_hex(-a);
    printf(" ~b=");
    put_hex(~b);
    printf("\n  ult=%d ule=%d slt=%d sle=%d eq=%d\n", a < b, a <= b,
           (i128)a < (i128)b, (i128)a <= (i128)b, a == b);
  }
}

static void division(void) {
  printf("-- div mod\n");
  for (unsigned i = 0; i < NP; i++) {
    u128 a = mk(parts[i][0], parts[i][1]);
    for (unsigned j = 1; j < NP; j += 3) {
      u128 b = mk(parts[j][0], parts[j][1]);
      if (b == 0) continue;
      printf("u ");
      put_u128(a);
      printf(" / ");
      put_u128(b);
      printf(" = ");
      put_u128(a / b);
      printf(" r ");
      put_u128(a % b);
      putchar('\n');
      i128 sa = (i128)a, sb = (i128)b;
      i128 min = (i128)((u128)1 << 127);
      if (sa == min && sb == -1) continue;
      printf("s ");
      put_i128(sa);
      printf(" / ");
      put_i128(sb);
      printf(" = ");
      put_i128(sa / sb);
      printf(" r ");
      put_i128(sa % sb);
      putchar('\n');
    }
  }
}

static void shifts(void) {
  printf("-- shifts\n");
  u128 x = mk(0x8123456789abcdefull, 0xfedcba9876543211ull);
  for (int n = 0; n < 128; n += 5) {
    printf("%3d shl=", n);
    put_hex(x << n);
    printf(" shr=");
    put_hex(x >> n);
    printf(" sar=");
    put_hex((u128)((i128)x >> n));
    putchar('\n');
  }
  int special[] = {63, 64, 65, 127};
  for (int k = 0; k < 4; k++) {
    int n = special[k];
    printf("%3d shl=", n);
    put_hex(x << n);
    printf(" shr=");
    put_hex(x >> n);
    printf(" sar=");
    put_hex((u128)((i128)x >> n));
    putchar('\n');
  }
}

static void conversions(void) {
  printf("-- conversions\n");
  double ds[] = {0.0, 1.0, -1.0, 1e10, -1e10, 1.8446744073709552e19,
                 3.4e38, -1.7e38, 123456789012345678901234567.0, 0.999,
                 -0.999, 9007199254740993.0};
  for (unsigned i = 0; i < sizeof ds / sizeof ds[0]; i++) {
    printf("%.17g -> ", ds[i]);
    if (ds[i] < 1.7e38) {  // the signed range ends at 2^127 = 1.70141e38
      i128 s = (i128)ds[i];
      put_i128(s);
      printf(" -> %.17g", (double)s);
    }
    if (ds[i] >= 0) {
      u128 u = (u128)ds[i];
      printf(" u ");
      put_u128(u);
      printf(" -> %.17g", (double)u);
    }
    putchar('\n');
  }
  for (unsigned i = 0; i < NP; i++) {
    u128 a = mk(parts[i][0], parts[i][1]);
    printf("%.17g %.17g %.9g %lld %llu\n", (double)a, (double)(i128)a,
           (float)(i128)a, (long long)(int64_t)a, (unsigned long long)(uint64_t)a);
  }
  int64_t smalls[] = {0, -1, 1, INT64_MIN, INT64_MAX, -123456789};
  for (unsigned i = 0; i < 6; i++) {
    i128 w = smalls[i];
    u128 uw = (uint64_t)smalls[i];
    printf("sext ");
    put_hex((u128)w);
    printf(" zext ");
    put_hex(uw);
    printf(" sq=");
    put_i128(w * w);
    putchar('\n');
  }
}

static void factorials(void) {
  printf("-- factorials and fibonacci\n");
  u128 f = 1;
  for (int n = 1; n <= 34; n++) {
    f *= (u128)n;
    printf("%d! = ", n);
    put_u128(f);
    putchar('\n');
  }
  u128 a = 0, b = 1;
  for (int n = 0; n < 185; n++) {
    u128 t = a + b;
    a = b;
    b = t;
    if (n % 12 == 0 || n > 180) {
      printf("fib(%d) = ", n + 1);
      put_u128(a);
      putchar('\n');
    }
  }
}

static u128 powmod(u128 b, u128 e, u128 m) {
  u128 r = 1;
  b %= m;
  while (e) {
    if (e & 1) r = r * b % m;
    b = b * b % m;
    e >>= 1;
  }
  return r;
}

static void modular(void) {
  printf("-- modular arithmetic\n");
  // modulus below 2^63 so the products fit in 128 bits
  u128 m = 9223372036854775783ull;  // a prime
  for (int i = 1; i < 12; i++) {
    u128 base = (u128)i * 1000003u + 7;
    u128 e = (u128)i * 0x9e3779b97f4a7c15ull;
    printf("%d: ", i);
    put_u128(powmod(base, e, m));
    printf(" fermat=");
    put_u128(powmod(base, m - 1, m));
    putchar('\n');
  }
}

int main(void) {
  arith();
  division();
  shifts();
  conversions();
  factorials();
  modular();
  return 0;
}
