// math.h over a grid of arguments: sqrt, cbrt, hypot, pow, exp, exp2, expm1,
// log, log2, log10, log1p, sin, cos, tan, asin, acos, atan, atan2, sinh,
// cosh, tanh, fmod, remainder, remquo, floor, ceil, round, trunc, rint,
// nearbyint, lround, llrint, fma, frexp, ldexp, scalbn, modf, ilogb, logb,
// copysign, nextafter, fdim, fmin, fmax, and their float versions.
//
// wasi-libc's libm is musl's, compiled into the module, so these are long
// branchy routines full of bit manipulation on the representation (extracting
// exponents through unions, comparing high words) with only sqrt, floor, ceil,
// trunc, nearest, abs, copysign, min and max mapped to single wasm
// instructions.  Results are printed with %.17g / %.9g so a last-place
// difference is visible; arguments that would produce a NaN are routed through
// isnan so no payload is printed.
#include <math.h>
#include <stdio.h>

double xs[] = {0.0, -0.0, 0.25, 0.5, 1.0, -1.0, 1.5, 2.0, -2.5, 3.0, 10.0,
               -7.75, 100.0, 0.001, 1e-10, 12345.678, -0.75, 0.999999,
               700.0, -700.0, 3.141592653589793, 1e15, 2.5, -3.5};
#define NX (sizeof xs / sizeof xs[0])

static void pr(const char *name, double v) {
  if (isnan(v))
    printf(" %s=nan", name);
  else
    printf(" %s=%.17g", name, v);
}
static void prf(const char *name, float v) {
  if (isnan(v))
    printf(" %s=nan", name);
  else
    printf(" %s=%.9g", name, (double)v);
}

static void unary(void) {
  printf("-- unary double\n");
  for (unsigned i = 0; i < NX; i++) {
    double x = xs[i];
    printf("x=%g\n ", x);
    pr("sqrt", sqrt(x));
    pr("cbrt", cbrt(x));
    pr("exp", exp(x));
    pr("exp2", exp2(x / 4));
    pr("expm1", expm1(x / 8));
    printf("\n ");
    pr("log", log(fabs(x) + 0.5));
    pr("log2", log2(fabs(x) + 1));
    pr("log10", log10(fabs(x) + 2));
    pr("log1p", log1p(fabs(x)));
    printf("\n ");
    pr("sin", sin(x));
    pr("cos", cos(x));
    pr("tan", tan(x));
    pr("atan", atan(x));
    printf("\n ");
    pr("asin", asin(x / 1000.0));
    pr("acos", acos(x / 1000.0));
    pr("sinh", sinh(x / 100));
    pr("cosh", cosh(x / 100));
    pr("tanh", tanh(x));
    printf("\n");
  }
}

static void rounding(void) {
  printf("-- rounding\n");
  double rs[] = {0.0, -0.0, 0.4, 0.5, 0.6, 1.5, 2.5, -0.5, -1.5, -2.5, 2.4999,
                 -2.5001, 1e15 + 0.5, 4503599627370495.5, 4503599627370497.0,
                 -1e300, 123.456, -123.456};
  for (unsigned i = 0; i < sizeof rs / sizeof rs[0]; i++) {
    double x = rs[i];
    printf("%.17g: floor=%g ceil=%g round=%g trunc=%g rint=%g nearby=%g", x,
           floor(x), ceil(x), round(x), trunc(x), rint(x), nearbyint(x));
    if (fabs(x) < 2e9) printf(" lround=%ld lrint=%ld", lround(x), lrint(x));
    if (fabs(x) < 1e18) printf(" llround=%lld llrint=%lld", llround(x), llrint(x));
    double ip;
    double fp = modf(x, &ip);
    printf(" modf=%g,%g\n", ip, fp);
  }
  float fs[] = {0.5f, 1.5f, 2.5f, -2.5f, 8388607.5f, -0.4f, 3.75f};
  for (unsigned i = 0; i < sizeof fs / sizeof fs[0]; i++) {
    float x = fs[i];
    printf("f%.9g: floor=%g ceil=%g round=%g trunc=%g rint=%g lrint=%ld\n",
           (double)x, (double)floorf(x), (double)ceilf(x), (double)roundf(x),
           (double)truncf(x), (double)rintf(x), lrintf(x));
  }
}

static void binary(void) {
  printf("-- binary double\n");
  for (unsigned i = 0; i < NX; i++) {
    double x = xs[i], y = xs[(i * 7 + 5) % NX];
    printf("x=%g y=%g\n ", x, y);
    pr("pow", pow(fabs(x), y / 8));
    pr("atan2", atan2(x, y));
    pr("hypot", hypot(x, y));
    pr("fmod", y != 0 ? fmod(x, y) : 0);
    printf("\n ");
    pr("remainder", y != 0 ? remainder(x, y) : 0);
    int q = 0;
    double rq = y != 0 ? remquo(x, y, &q) : 0;
    pr("remquo", rq);
    printf(" q%%8=%d", y != 0 ? q % 8 : 0);
    pr("fdim", fdim(x, y));
    pr("fmin", fmin(x, y));
    pr("fmax", fmax(x, y));
    printf("\n ");
    pr("copysign", copysign(x, y));
    pr("nextafter", nextafter(x, y));
    pr("fma", fma(x, y, -x * y));
    pr("fma2", fma(x, 3.0, y));
    printf("\n");
  }
  printf("pow special:");
  pr("0^0", pow(0, 0));
  pr("2^-1074", pow(2, -1074));
  pr("2^1023", pow(2, 1023));
  pr("-8^(1/3)", pow(-8, 1.0 / 3));
  pr("-2^3", pow(-2, 3));
  pr("-2^-3", pow(-2, -3));
  pr("1^inf", pow(1, INFINITY));
  pr("10^-5", pow(10, -5));
  printf("\n");
}

static void decompose(void) {
  printf("-- frexp ldexp scalbn ilogb logb\n");
  for (unsigned i = 0; i < NX; i++) {
    double x = xs[i];
    int e;
    double m = frexp(x, &e);
    printf("%g: frexp=%.17g,%d ldexp=%.17g scalbn=%.17g", x, m, e,
           ldexp(m, e), scalbn(x, -3));
    if (x != 0) printf(" ilogb=%d logb=%g", ilogb(x), logb(x));
    printf(" signbit=%d\n", signbit(x) != 0);
  }
  double sub = ldexp(1.0, -1060);
  int e;
  double m = frexp(sub, &e);
  printf("subnormal: %a frexp=%a,%d ilogb=%d next=%a prev=%a\n", sub, m, e,
         ilogb(sub), nextafter(sub, 1.0), nextafter(sub, -1.0));
  printf("nextafter(0,1)=%a nextafter(1,2)=%a nextafter(1,0)=%a\n",
         nextafter(0.0, 1.0), nextafter(1.0, 2.0), nextafter(1.0, 0.0));
  printf("nextafterf(1,2)=%a nexttoward=%a\n", (double)nextafterf(1.0f, 2.0f),
         (double)nexttowardf(1.0f, 0.0L));
}

static void floats(void) {
  printf("-- float versions\n");
  for (unsigned i = 0; i < NX; i++) {
    float x = (float)xs[i];
    if (fabsf(x) > 1e6f) continue;
    printf("x=%g", (double)x);
    prf("sqrtf", sqrtf(x));
    prf("expf", expf(x / 16));
    prf("logf", logf(fabsf(x) + 1));
    prf("sinf", sinf(x));
    prf("cosf", cosf(x));
    prf("tanf", tanf(x / 4));
    prf("atan2f", atan2f(x, 2.0f));
    prf("powf", powf(fabsf(x), 0.75f));
    prf("fmodf", fmodf(x, 0.7f));
    prf("cbrtf", cbrtf(x));
    prf("hypotf", hypotf(x, 3.0f));
    prf("fmaf", fmaf(x, x, -1.0f));
    printf("\n");
  }
}

static void identities(void) {
  printf("-- identities over a sweep\n");
  double maxerr1 = 0, maxerr2 = 0, maxerr3 = 0, sum = 0;
  for (int i = -200; i <= 200; i++) {
    double t = i * 0.0471;
    double s = sin(t), c = cos(t);
    double e1 = fabs(s * s + c * c - 1);
    double e2 = fabs(exp(log(fabs(t) + 1)) - (fabs(t) + 1));
    double e3 = fabs(atan2(s, c) - remainder(t, 2 * M_PI));
    if (e1 > maxerr1) maxerr1 = e1;
    if (e2 > maxerr2) maxerr2 = e2;
    if (e3 > maxerr3) maxerr3 = e3;
    sum += s * 3 + c * 5 + tanh(t) + sqrt(fabs(t));
    if (i % 40 == 0) printf("t=%.4f sin=%.17g cos=%.17g\n", t, s, c);
  }
  printf("err sin2cos2=%.3g explog=%.3g atan2=%.3g sum=%.17g\n", maxerr1,
         maxerr2, maxerr3, sum);
}

int main(void) {
  unary();
  rounding();
  binary();
  decompose();
  floats();
  identities();
  return 0;
}
