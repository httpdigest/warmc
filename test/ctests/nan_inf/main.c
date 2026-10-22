// NaN, infinity, signed zero and subnormals: how they arise (0/0, inf-inf,
// sqrt(-1), overflow, underflow), how every comparison operator treats NaN
// (all false except !=), fpclassify, isnan, isinf, isfinite, isnormal,
// signbit, isunordered and the isgreater family, fmin/fmax ignoring a NaN,
// arithmetic absorbing infinities, and min/max idioms that differ on -0 and
// NaN depending on how they are written.
//
// wasm's f32/f64 compares are IEEE, but a code generator that turns `!(a < b)`
// into `a >= b`, swaps the operands of a select or uses a native min
// instruction for a C ternary changes the answer exactly when a NaN or a
// negative zero is involved.  NaN payloads and signs are never printed, since
// they are not portable; only classification results are.
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

double zero = 0.0, one = 1.0, big = 1e308, tiny = 1e-308;

static const char *cls(double x) {
  switch (fpclassify(x)) {
    case FP_NAN: return "nan";
    case FP_INFINITE: return signbit(x) ? "-inf" : "+inf";
    case FP_ZERO: return signbit(x) ? "-zero" : "+zero";
    case FP_SUBNORMAL: return signbit(x) ? "-sub" : "+sub";
    case FP_NORMAL: return signbit(x) ? "-norm" : "+norm";
  }
  return "?";
}
static const char *clsf(float x) {
  switch (fpclassify(x)) {
    case FP_NAN: return "nan";
    case FP_INFINITE: return signbit(x) ? "-inf" : "+inf";
    case FP_ZERO: return signbit(x) ? "-zero" : "+zero";
    case FP_SUBNORMAL: return signbit(x) ? "-sub" : "+sub";
    case FP_NORMAL: return signbit(x) ? "-norm" : "+norm";
  }
  return "?";
}

static void sources(void) {
  printf("-- where specials come from\n");
  double v[] = {
    zero / zero, one / zero, -one / zero, big * 10, -big * 10, tiny / 1e10,
    tiny * tiny, -tiny * tiny, sqrt(-one), log(zero), log(-one), exp(1000),
    exp(-1000), (one / zero) - (one / zero), (one / zero) * zero, fmod(one, zero),
    pow(zero, -one), atan(one / zero), -zero, zero * -one, one - one, -(one - one),
    DBL_MIN / 2, DBL_TRUE_MIN, -DBL_TRUE_MIN / 2, DBL_MAX + DBL_MAX / 1e17,
    HUGE_VAL, -HUGE_VAL, NAN, INFINITY,
  };
  for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
    double x = v[i];
    printf("%2u %-6s isnan=%d isinf=%d isfinite=%d isnormal=%d signbit=%d",
           i, cls(x), isnan(x), isinf(x) != 0, isfinite(x), isnormal(x),
           signbit(x) != 0 && !isnan(x));
    if (!isnan(x)) printf(" val=%g", x);
    printf("\n");
  }
  float f[] = {(float)zero / (float)zero, 1.0f / (float)zero, (float)big,
               (float)tiny, FLT_MIN / 4, -FLT_TRUE_MIN, -(float)zero, 3.0f};
  for (unsigned i = 0; i < sizeof f / sizeof f[0]; i++)
    printf("f%u %s isnan=%d isinf=%d\n", i, clsf(f[i]), isnan(f[i]),
           isinf(f[i]) != 0);
}

static void comparisons(void) {
  printf("-- comparisons with nan/inf/zero\n");
  double inf = one / zero;
  double nan = zero / zero;
  double nz = -zero;
  double xs[] = {nan, inf, -inf, zero, nz, one, -one};
  const char *nm[] = {"nan", "inf", "-inf", "0", "-0", "1", "-1"};
  for (int i = 0; i < 7; i++) {
    for (int j = 0; j < 7; j++) {
      double a = xs[i], b = xs[j];
      printf("%4s %4s: < %d <= %d == %d != %d > %d >= %d !< %d !>= %d unord %d "
             "isgt %d isle %d islg %d\n",
             nm[i], nm[j], a < b, a <= b, a == b, a != b, a > b, a >= b,
             !(a < b), !(a >= b), isunordered(a, b), isgreater(a, b),
             islessequal(a, b), islessgreater(a, b));
    }
  }
}

static double tern_min(double a, double b) { return a < b ? a : b; }
static double tern_max(double a, double b) { return a > b ? a : b; }
static double tern_min2(double a, double b) { return b < a ? b : a; }

static void minmax(void) {
  printf("-- min and max\n");
  double inf = one / zero;
  double nan = zero / zero;
  double xs[] = {nan, inf, -inf, zero, -zero, one, -one};
  const char *nm[] = {"nan", "inf", "-inf", "0", "-0", "1", "-1"};
  for (int i = 0; i < 7; i++) {
    for (int j = 0; j < 7; j++) {
      double a = xs[i], b = xs[j];
      printf("%4s %4s: fmin=%-5s fmax=%-5s tmin=%-5s tmax=%-5s tmin2=%-5s "
             "fminf=%-5s wmin=%-5s wmax=%-5s\n",
             nm[i], nm[j], cls(fmin(a, b)), cls(fmax(a, b)),
             cls(tern_min(a, b)), cls(tern_max(a, b)), cls(tern_min2(a, b)),
             clsf(fminf((float)a, (float)b)), cls(__builtin_wasm_min_f64(a, b)),
             cls(__builtin_wasm_max_f64(a, b)));
    }
  }
}

static void arithmetic(void) {
  printf("-- arithmetic on specials\n");
  double inf = one / zero;
  double xs[] = {inf, -inf, zero, -zero, one, 1e308, 5e-324};
  const char *nm[] = {"inf", "-inf", "0", "-0", "1", "1e308", "min"};
  for (int i = 0; i < 7; i++) {
    for (int j = 0; j < 7; j++) {
      double a = xs[i], b = xs[j];
      printf("%5s %5s: + %-5s - %-5s * %-5s / %-5s fma %-5s copysign %-5s\n",
             nm[i], nm[j], cls(a + b), cls(a - b), cls(a * b), cls(a / b),
             cls(fma(a, b, one)), cls(copysign(a, b)));
    }
  }
  double nan = zero / zero;
  printf("nan prop: %d %d %d %d %d %d\n", isnan(nan + 1), isnan(nan * 0),
         isnan(-nan), isnan(fabs(nan)), isnan(sqrt(nan)), isnan(pow(nan, 0)));
  printf("pow(nan,0)=%g pow(1,nan)=%g hypot(inf,nan)=%g\n", pow(nan, 0),
         pow(1, nan), hypot(inf, nan));
  printf("nan==nan %d, x!=x %d, self-sub nan %d\n", nan == nan, nan != nan,
         isnan(nan - nan));
}

static void bits(void) {
  printf("-- representations\n");
  double inf = one / zero;
  double xs[] = {inf, -inf, zero, -zero, one, DBL_MAX, DBL_MIN, DBL_TRUE_MIN,
                 -2.0, 0.1};
  for (unsigned i = 0; i < sizeof xs / sizeof xs[0]; i++) {
    uint64_t u;
    memcpy(&u, &xs[i], 8);
    float f = (float)xs[i];
    uint32_t uf;
    memcpy(&uf, &f, 4);
    printf("%-12g %016llx f=%08x\n", xs[i], (unsigned long long)u, uf);
  }
  // build values from bits
  uint64_t pats[] = {0x7ff0000000000000ull, 0x0000000000000001ull,
                     0x000fffffffffffffull, 0x0010000000000000ull,
                     0x7fefffffffffffffull, 0x8000000000000000ull,
                     0x3ff8000000000000ull, 0x7ff8000000000000ull,
                     0xfff0000000000001ull};
  for (unsigned i = 0; i < sizeof pats / sizeof pats[0]; i++) {
    double d;
    memcpy(&d, &pats[i], 8);
    printf("%016llx -> %s", (unsigned long long)pats[i], cls(d));
    if (!isnan(d)) printf(" %a", d);
    printf("\n");
  }
  // counting representable floats between two values via bit patterns
  float a = 1.0f, b = 2.0f;
  uint32_t ua, ub;
  memcpy(&ua, &a, 4);
  memcpy(&ub, &b, 4);
  printf("floats in [1,2): %u\n", ub - ua);
}

int main(void) {
  sources();
  comparisons();
  minmax();
  arithmetic();
  bits();
  return 0;
}
