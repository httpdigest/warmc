// Float and double arithmetic and the conversions between them and the
// integers: the four operations on a table that includes subnormals, values
// near the overflow threshold and negative zero, printed in hex-float so every
// bit of the result is visible; int<->float conversions both ways at 32 and 64
// bits, signed and unsigned, including the values above 2^53 or 2^24 that
// must round to nearest-even, and the unsigned 64-bit conversions that wasm
// has single instructions for but most hardware does not.
//
// Converting an out-of-range float to an integer is undefined in C, so the
// direct casts here stay in range; saturation is exercised through clamping
// helpers written in C and through clang's wasm builtins for the saturating
// truncation instructions, whose behaviour wasm defines for every input.
// Sums are done in float and in double, naively and with Kahan compensation,
// so that a compiler which reassociates or keeps a float in double precision
// prints a different line.
#include <float.h>
#include <stdint.h>
#include <stdio.h>

double dv[] = {
  0.0, -0.0, 1.0, -1.0, 0.5, 0.1, -0.3, 3.0, 1e-300, 4.9406564584124654e-324,
  2.2250738585072014e-308, 1.7976931348623157e308, 1e308, 123456789.0,
  -9.87654321e-5, 6.02214076e23, 1.0 / 3.0, 9007199254740992.0,
  9007199254740993.0, 1e16, 2.5, -2.5, 3.5, 0.49999999999999994,
};
#define NDV (sizeof dv / sizeof dv[0])
float fv[] = {
  0.0f, -0.0f, 1.0f, -1.0f, 0.1f, 0.2f, 1e-38f, 1.4e-45f, 3.4028235e38f,
  16777216.0f, 16777217.0f, 0.33333334f, -7.25f, 1e10f, 2.5f, -0.5f,
};
#define NFV (sizeof fv / sizeof fv[0])

static void double_ops(void) {
  printf("-- double + - * /\n");
  for (unsigned i = 0; i < NDV; i++) {
    double a = dv[i], b = dv[(i * 7 + 2) % NDV];
    printf("%a %a: %a %a %a", a, b, a + b, a - b, a * b);
    if (b != 0) printf(" %a", a / b);
    printf(" | %.17g %.17g\n", a + b, a * b);
  }
}

static void float_ops(void) {
  printf("-- float + - * /\n");
  for (unsigned i = 0; i < NFV; i++) {
    float a = fv[i], b = fv[(i * 5 + 3) % NFV];
    float s = a + b, d = a - b, m = a * b;
    printf("%a %a: %a %a %a", a, b, s, d, m);
    if (b != 0) {
      float q = a / b;
      printf(" %a", q);
    }
    printf(" | %.9g %.9g\n", s, m);
  }
}

static void int_to_float(void) {
  printf("-- integer to floating\n");
  int64_t is[] = {0, 1, -1, 16777215, 16777217, -16777219, 2147483647,
                  -2147483647 - 1, 9007199254740993ll, -9007199254740995ll,
                  INT64_MAX, INT64_MIN, 1ll << 62, (1ll << 53) + 1};
  for (unsigned i = 0; i < sizeof is / sizeof is[0]; i++) {
    int64_t v = is[i];
    int32_t w = (int32_t)v;
    printf("%lld: d=%a f=%a | i32 %d: d=%a f=%a\n", (long long)v, (double)v,
           (float)v, w, (double)w, (float)w);
  }
  uint64_t us[] = {0, 1, 0x7fffffffffffffffull, 0x8000000000000000ull,
                   0x8000000000000001ull, 0xfffffffffffff800ull,
                   0xfffffffffffffc00ull, 0xffffffffffffffffull,
                   0x8000000000000400ull, 0x8000000000000c00ull,
                   0x00000000ffffffffull, 0x0000000080000001ull};
  for (unsigned i = 0; i < sizeof us / sizeof us[0]; i++) {
    uint64_t v = us[i];
    uint32_t w = (uint32_t)v;
    printf("%llu: d=%a f=%a | u32 %u: d=%a f=%a\n", (unsigned long long)v,
           (double)v, (float)v, w, (double)w, (float)w);
  }
}

static void float_to_int(void) {
  printf("-- floating to integer (in range)\n");
  double ds[] = {0.0, -0.0, 0.9, -0.9, 1.5, -1.5, 2147483647.9, -2147483648.9,
                 4294967295.5, 1e18, -1e18, 9223372036854774784.0,
                 9223372036854775808.0, 18446744073709549568.0, 123.456};
  for (unsigned i = 0; i < sizeof ds / sizeof ds[0]; i++) {
    double d = ds[i];
    printf("%.17g:", d);
    if (d > -2147483649.0 && d < 2147483648.0) printf(" i32=%d", (int32_t)d);
    if (d > -1.0 && d < 4294967296.0) printf(" u32=%u", (uint32_t)d);
    if (d >= -9223372036854775808.0 && d < 9223372036854775808.0)
      printf(" i64=%lld", (long long)(int64_t)d);
    if (d > -1.0 && d < 18446744073709551616.0)
      printf(" u64=%llu", (unsigned long long)(uint64_t)d);
    float f = (float)d;
    if (f > -2147483904.0f && f < 2147483648.0f) printf(" f->i32=%d", (int32_t)f);
    if (f > -1.0f && f < 18446744073709551616.0f)
      printf(" f->u64=%llu", (unsigned long long)(uint64_t)f);
    printf("\n");
  }
}

// Saturation written in C: clamp first, then convert, so every input is
// defined.  NaN maps to zero, as wasm's saturating truncation does.
static int32_t sat_i32(double d) {
  if (!(d == d)) return 0;
  if (d <= -2147483648.0) return INT32_MIN;
  if (d >= 2147483647.0) return INT32_MAX;
  return (int32_t)d;
}
static uint8_t sat_u8(float f) {
  if (!(f > 0)) return 0;
  if (f >= 255.0f) return 255;
  return (uint8_t)(f + 0.5f);
}

static void saturating(void) {
  printf("-- saturating conversions\n");
  double ds[] = {0.0, -1.0, 1e10, -1e10, 2147483647.5, -2147483648.5, 1e300,
                 -1e300, 4294967296.0, 18446744073709551616.0, 1e19, -0.5,
                 255.4, 300.0, -3.0, 127.5};
  double inf = 1.0 / dv[0];
  for (unsigned i = 0; i < sizeof ds / sizeof ds[0] + 2; i++) {
    double d = i < sizeof ds / sizeof ds[0] ? ds[i] : (i & 1 ? -inf : inf);
    float f = (float)d;
    printf("%g: c_i32=%d c_u8=%u wasm i32=%d u32=%u i64=%lld u64=%llu "
           "fi32=%d fu32=%u fi64=%lld fu64=%llu\n",
           d, sat_i32(d), sat_u8(f), __builtin_wasm_trunc_saturate_s_i32_f64(d),
           __builtin_wasm_trunc_saturate_u_i32_f64(d),
           (long long)__builtin_wasm_trunc_saturate_s_i64_f64(d),
           (unsigned long long)__builtin_wasm_trunc_saturate_u_i64_f64(d),
           __builtin_wasm_trunc_saturate_s_i32_f32(f),
           __builtin_wasm_trunc_saturate_u_i32_f32(f),
           (long long)__builtin_wasm_trunc_saturate_s_i64_f32(f),
           (unsigned long long)__builtin_wasm_trunc_saturate_u_i64_f32(f));
  }
  double nan = inf - inf;
  printf("nan: c_i32=%d wasm i32=%d u64=%llu\n", sat_i32(nan),
         __builtin_wasm_trunc_saturate_s_i32_f64(nan),
         (unsigned long long)__builtin_wasm_trunc_saturate_u_i64_f64(nan));
}

static void float_double(void) {
  printf("-- float <-> double\n");
  for (unsigned i = 0; i < NDV; i++) {
    float f = (float)dv[i];
    double back = f;
    printf("%a -> %a -> %a %s\n", dv[i], f, back, back == dv[i] ? "exact" : "rounded");
  }
  for (unsigned i = 0; i < NFV; i++) {
    double d = fv[i];
    printf("f %a -> d %.17g -> f %a\n", fv[i], d, (float)(d * 3.0));
  }
}

static void sums(void) {
  printf("-- summation\n");
  float fs = 0, fc = 0, fk = 0;
  double ds = 0, dc = 0, dk = 0;
  for (int i = 1; i <= 20000; i++) {
    float tf = 1.0f / (float)i;
    double td = 1.0 / i;
    fs += tf;
    ds += td;
    float yf = tf - fc;
    float tf2 = fk + yf;
    fc = (tf2 - fk) - yf;
    fk = tf2;
    double yd = td - dc;
    double td2 = dk + yd;
    dc = (td2 - dk) - yd;
    dk = td2;
    if (i % 2500 == 0)
      printf("n=%d float=%a kahan=%a double=%a kahan=%a\n", i, fs, fk, ds, dk);
  }
  float p = 1;
  double q = 1;
  for (int i = 0; i < 40; i++) {
    p *= 1.5f;
    q *= -1.25;
    if (i % 5 == 4) printf("pow %d: %a %a\n", i + 1, p, q);
  }
  // halve down through the subnormal range to zero
  double t = 1.0;
  int steps = 0;
  while (t > 0) {
    t /= 2;
    steps++;
    if (steps > 1018 && steps < 1078) printf("2^-%d = %a\n", steps, t);
  }
  printf("halvings to zero: %d\n", steps);
  float tf = 1.0f;
  steps = 0;
  while (tf > 0) {
    tf /= 2;
    steps++;
  }
  printf("float halvings to zero: %d\n", steps);
}

static void compare(void) {
  printf("-- comparisons\n");
  for (unsigned i = 0; i < NDV; i += 2) {
    for (unsigned j = 1; j < NDV; j += 3) {
      double a = dv[i], b = dv[j];
      printf("%d%d%d%d%d%d ", a < b, a <= b, a == b, a != b, a > b, a >= b);
    }
    printf("\n");
  }
  printf("0==-0:%d 1/0>0:%d -1/0<0:%d\n", dv[0] == dv[1], 1 / dv[0] > 0,
         1 / dv[1] < 0);
  printf("eps d=%a f=%a min d=%a f=%a max f=%a dig %d %d mant %d %d\n",
         DBL_EPSILON, FLT_EPSILON, DBL_MIN, FLT_MIN, FLT_MAX, DBL_DIG, FLT_DIG,
         DBL_MANT_DIG, FLT_MANT_DIG);
  printf("1+eps/2==1: %d 1+eps>1: %d\n", 1.0 + DBL_EPSILON / 2 == 1.0,
         1.0 + DBL_EPSILON > 1.0);
}

int main(void) {
  double_ops();
  float_ops();
  int_to_float();
  float_to_int();
  saturating();
  float_double();
  sums();
  compare();
  return 0;
}
