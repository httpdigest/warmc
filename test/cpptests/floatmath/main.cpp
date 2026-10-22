// Floating point and <cmath>, with operands read through volatiles so the
// calls happen at run time: the basic operations at the edges (signed
// zeros, infinities, NaN propagation and comparisons, subnormals, overflow
// to infinity and underflow to zero, float/double conversions and their
// rounding), the rounding functions (floor, ceil, trunc, round, nearbyint,
// rint, lround, llround, lrint) at halfway cases, decomposition (frexp,
// ldexp, scalbn, ilogb, logb, modf, nextafter, copysign), fmod/remainder/
// remquo, fma, fmin/fmax/fdim, hypot, and the transcendental functions
// (sqrt, cbrt, pow, exp, exp2, expm1, log family, trig and inverse trig
// across all quadrants, hyperbolic, erf, tgamma, lgamma) over a grid of
// arguments; then numeric_limits, fpclassify, and two numerical
// algorithms (Kahan summation and Newton iteration) whose results depend on
// every rounding step.
//
// Values are printed with %a as well as in decimal where the exact bits
// matter.  A NaN's sign and payload are not deterministic in wasm (the
// spec lets an engine produce any NaN from an arithmetic operation), so
// NaN results are printed only as "nan" and never through signbit or a
// bit pattern.
#include <algorithm>
#include <cfloat>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

volatile double vd[] = {0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 1.5, 2.5, -2.5, 3.0, 0.1, 1e-310, -1e-310,
                        1e308, -1e308, 123.456, -987.654, 3.141592653589793, 1e-5, 7.0};
volatile float vf[] = {0.0f, -0.0f, 1.0f, -1.0f, 0.1f, 1e-40f, 3.4e38f, -2.5f, 16777217.0f, 0.333333f};

const char *fmt(double x, char *buf) {
  if (std::isnan(x)) return "nan";
  if (std::isinf(x)) return x > 0 ? "inf" : "-inf";
  std::snprintf(buf, 48, "%.17g", x);
  return buf;
}

#define P(x) fmt((x), bufs[__COUNTER__ % 8])
char bufs[8][48];

uint64_t bitsOf(double d) {
  uint64_t u;
  std::memcpy(&u, &d, sizeof u);
  return u;
}

}  // namespace

int main() {
  const double inf = std::numeric_limits<double>::infinity();
  volatile double vnan = std::numeric_limits<double>::quiet_NaN();
  volatile double vinf = inf;
  double nan = vnan;

  std::printf("== special values\n");
  double z = vd[0], nz = vd[1], one = vd[2];
  std::printf("0==-0 %d, 1/0=%s 1/-0=%s, signbit(-0)=%d, -0+0 signbit=%d, -0*-1=%s\n", z == nz,
              P(one / z), P(one / nz), std::signbit(nz), std::signbit(nz + z), P(nz * -1));
  std::printf("inf-inf=%s inf*0=%s 0/0=%s inf/inf=%s sqrt(-1)=%s\n", P(vinf - vinf), P(vinf * z), P(z / z),
              P(vinf / vinf), P(std::sqrt(-one)));
  std::printf("nan==nan %d nan!=nan %d nan<1 %d nan>1 %d nan<=nan %d isnan %d isunordered %d\n", nan == nan,
              nan != nan, nan < 1, nan > 1, nan <= nan, std::isnan(nan), std::isunordered(nan, 1.0));
  std::printf("nan+1=%s max(nan,1)=%s fmax(nan,1)=%s fmin(1,nan)=%s\n", P(nan + 1), P(std::max(nan, 1.0)),
              P(std::fmax(nan, 1.0)), P(std::fmin(1.0, nan)));
  std::printf("inf>max %d -inf<lowest %d isinf %d isfinite(max) %d\n", vinf > DBL_MAX, -vinf < -DBL_MAX,
              std::isinf(-vinf), std::isfinite(DBL_MAX));

  std::printf("== overflow, underflow, subnormals\n");
  volatile double big = 1e308, tiny = 1e-308;
  std::printf("big*10=%s -big*10=%s tiny/1e10=%s tiny/1e300=%s\n", P(big * 10), P(-big * 10), P(tiny / 1e10),
              P(tiny / 1e300));
  double sub = vd[11];
  std::printf("1e-310 isnormal=%d fpclass=%d bits=%016" PRIx64 " *1e10=%s\n", std::isnormal(sub),
              std::fpclassify(sub) == FP_SUBNORMAL, bitsOf(sub), P(sub * 1e10));
  std::printf("denorm_min=%a min=%a max=%a epsilon=%a\n", std::numeric_limits<double>::denorm_min(),
              DBL_MIN, DBL_MAX, DBL_EPSILON);
  std::printf("float denorm_min=%a min=%a max=%a epsilon=%a\n", (double)std::numeric_limits<float>::denorm_min(),
              (double)FLT_MIN, (double)FLT_MAX, (double)FLT_EPSILON);
  double halving = 1.0;
  int steps = 0;
  while (halving > 0) {
    halving /= vd[4] * 4;  // /2
    ++steps;
  }
  std::printf("halvings to zero=%d\n", steps);
  for (auto &f : vf) {
    float x = f;
    double widened = x;
    float back = (float)widened;
    float sq = x * x;
    std::printf("float %-14a widened=%-22a back-same=%d sq=%-14a half=%a\n", (double)x, widened, back == x,
                (double)sq, (double)(x * 0.5f));
  }
  for (auto &d : vd) {
    double x = d;
    if (std::fabs(x) > FLT_MAX) {
      std::printf("double %-24a -> (outside float range)\n", x);
      continue;
    }
    float f = (float)x;
    std::printf("double %-24a -> float %-16a -> double %a\n", x, (double)f, (double)(float)(double)f);
  }

  std::printf("== rounding\n");
  const double rv[] = {-2.5, -1.5, -0.5, -0.4999999999999999, 0.5, 1.5, 2.5, 3.5, 2.4999, -7.9, 1e15 + 0.5};
  for (double r : rv) {
    volatile double vr = r;
    double x = vr;
    // long is 32 bits on wasm32, so lround/lrint only for values that fit
    bool fitsLong = std::fabs(x) < 2e9;
    std::printf("%22.17g: floor=%g ceil=%g trunc=%g round=%g nearbyint=%g rint=%g lround=%ld llround=%lld lrint=%ld\n",
                x, std::floor(x), std::ceil(x), std::trunc(x), std::round(x), std::nearbyint(x), std::rint(x),
                fitsLong ? std::lround(x) : 0L, std::llround(x), fitsLong ? std::lrint(x) : 0L);
  }
  for (double r : rv) {
    volatile float vr = (float)r;
    float x = vr;
    std::printf("f%14.7g: floorf=%g ceilf=%g truncf=%g roundf=%g rintf=%g\n", (double)x, (double)std::floor(x),
                (double)std::ceil(x), (double)std::trunc(x), (double)std::round(x), (double)std::rint(x));
  }

  std::printf("== decomposition\n");
  for (auto &d : vd) {
    double x = d;
    int e = 0;
    double m = std::frexp(x, &e);
    double ip;
    double fp = std::modf(x, &ip);
    std::printf("%-24a frexp=(%a,%d) ldexp(m,e)==x %d ilogb=%d logb=%s modf=(%s,%s) scalbn(x,-3)=%a\n", x, m, e,
                std::ldexp(m, e) == x, x == 0 ? -999 : std::ilogb(x), P(std::logb(x)), P(ip), P(fp),
                std::scalbn(x, -3));
  }
  std::printf("nextafter(1,2)=%a nextafter(1,0)=%a nextafter(0,1)=%a nextafter(-0,-1)=%a\n",
              std::nextafter(one, 2.0), std::nextafter(one, 0.0), std::nextafter(z, 1.0), std::nextafter(nz, -1.0));
  std::printf("nextafterf(1,2)=%a copysign(3,-0)=%g copysign(-3,+0)=%g\n", (double)std::nextafter(1.0f, 2.0f),
              std::copysign(3.0, nz), std::copysign(-3.0, z));

  std::printf("== remainders and fma\n");
  const double pairs[][2] = {{5.5, 2}, {-5.5, 2}, {5.5, -2}, {7, 3.5}, {1e20, 3}, {0.3, 0.1}, {10, 0.75}, {-0.0, 1}};
  for (auto &p : pairs) {
    volatile double va = p[0], vb = p[1];
    double a = va, b = vb;
    int quo = 0;
    double rq = std::remquo(a, b, &quo);
    std::printf("a=%-8g b=%-6g fmod=%-22.17g remainder=%-22.17g remquo=(%g,%d)\n", a, b, std::fmod(a, b),
                std::remainder(a, b), rq, quo & 7);
  }
  volatile double fa = 1.0 + DBL_EPSILON, fb = 1.0 - DBL_EPSILON, fc = -1.0;
  double xa = fa, xb = fb, xc = fc;
  std::printf("fma(1+e,1-e,-1)=%a plain=%a\n", std::fma(xa, xb, xc), xa * xb + xc);
  std::printf("fma(0.1,10,-1)=%a fmaf=%a\n", std::fma(vd[10], 10.0, -1.0), (double)std::fma((float)vd[10], 10.0f, -1.0f));
  std::printf("fdim(5,3)=%g fdim(3,5)=%g fmax(-0,0)=%g fmin(2,-3)=%g\n", std::fdim(5.0, 3.0), std::fdim(3.0, 5.0),
              std::fmax(nz, z), std::fmin(2.0, -3.0));
  std::printf("hypot(3,4)=%.17g hypot(1e300,1e300)=%.17g hypot(3,4,12)=%.17g\n", std::hypot(3.0, 4.0),
              std::hypot(1e300, 1e300), std::hypot(3.0, 4.0, 12.0));

  std::printf("== powers, roots, exponentials, logarithms\n");
  const double xs[] = {0.0, 0.25, 0.5, 1.0, 2.0, 2.718281828459045, 10.0, 100.0, 1e-8, 1e10};
  for (double xv : xs) {
    volatile double vx = xv;
    double x = vx;
    std::printf("x=%-10g sqrt=%-22.17g cbrt=%-22.17g exp=%-22.17g exp2=%-22.17g expm1=%.17g\n", x, std::sqrt(x),
                std::cbrt(x), std::exp(x), std::exp2(x), std::expm1(x));
    std::printf("            log=%s log2=%s log10=%s log1p=%.17g\n", P(std::log(x)), P(std::log2(x)),
                P(std::log10(x)), std::log1p(x));
  }
  const double powCases[][2] = {{2, 10},   {2, -3},  {2, 0.5},   {-8, 1.0 / 3}, {-2, 3},  {0, 0},
                                {0, -1},   {-0.0, -1}, {1, 1e300}, {10, -310},   {1.0001, 10000}, {9, 0.5}};
  for (auto &pc : powCases) {
    volatile double vb = pc[0], ve = pc[1];
    std::printf("pow(%g, %g) = %s\n", (double)vb, (double)ve, P(std::pow((double)vb, (double)ve)));
  }
  std::printf("powf(2,0.5)=%.9g sqrtf(2)=%.9g cbrt(-27)=%g\n", (double)std::pow(2.0f, (float)vd[4]),
              (double)std::sqrt((float)(vd[2] * 2)), std::cbrt(-27.0 * vd[2]));

  std::printf("== trigonometry\n");
  for (int deg = -360; deg <= 360; deg += 45) {
    volatile double vr = deg * 3.141592653589793 / 180.0;
    double r = vr;
    std::printf("deg=%4d sin=% .17f cos=% .17f tan=%s\n", deg, std::sin(r), std::cos(r),
                (deg % 180 == 90 || deg % 180 == -90) ? "(pole)" : P(std::tan(r)));
  }
  for (double y : {1.0, -1.0, 0.0}) {
    for (double x : {1.0, -1.0, 0.0, -0.0}) {
      volatile double vy = y, vx = x;
      std::printf("atan2(%g,%g)=% .17f ", (double)vy, (double)vx, std::atan2((double)vy, (double)vx));
    }
    std::printf("\n");
  }
  for (double a : {-1.0, -0.5, 0.0, 0.5, 1.0, 2.0}) {
    volatile double va = a;
    double x = va;
    std::printf("x=%4g asin=%s acos=%s atan=% .17f\n", x, P(std::asin(x)), P(std::acos(x)), std::atan(x));
  }
  std::printf("large arg: sin(1e6)=%.17g cos(1e22)=%.17g sinf(100)=%.9g\n", std::sin(1e6 * vd[2]),
              std::cos(1e22 * vd[2]), (double)std::sin(100.0f * (float)vd[2]));
  for (double h : {-3.0, -0.5, 0.0, 0.5, 1.0, 20.0}) {
    volatile double vh = h;
    double x = vh;
    std::printf("x=%4g sinh=%.17g cosh=%.17g tanh=%.17g asinh=%.17g atanh=%s acosh=%s\n", x, std::sinh(x),
                std::cosh(x), std::tanh(x), std::asinh(x), P(std::atanh(x)), P(std::acosh(x)));
  }

  std::printf("== special functions\n");
  for (double g : {0.5, 1.0, 1.5, 2.0, 3.0, 4.5, 10.0, 20.0, -0.5, -2.5, 171.0, 172.0}) {
    volatile double vg = g;
    double x = vg;
    std::printf("x=%6g tgamma=%s lgamma=%s erf=%.17g erfc=%.17g\n", x, P(std::tgamma(x)), P(std::lgamma(x)),
                std::erf(x), std::erfc(x));
  }

  std::printf("== limits and classification\n");
  std::printf("double: digits=%d digits10=%d max_digits10=%d min_exp=%d max_exp=%d has_denorm=%d\n",
              std::numeric_limits<double>::digits, std::numeric_limits<double>::digits10,
              std::numeric_limits<double>::max_digits10, std::numeric_limits<double>::min_exponent,
              std::numeric_limits<double>::max_exponent, (int)std::numeric_limits<double>::has_denorm);
  std::printf("float: digits=%d digits10=%d lowest=%a round_error=%a\n", std::numeric_limits<float>::digits,
              std::numeric_limits<float>::digits10, (double)std::numeric_limits<float>::lowest(),
              (double)std::numeric_limits<float>::round_error());
  for (auto &d : vd) {
    double x = d;
    int c = std::fpclassify(x);
    const char *cn = c == FP_ZERO ? "zero" : c == FP_SUBNORMAL ? "subnormal" : c == FP_NORMAL ? "normal"
                   : c == FP_INFINITE ? "infinite" : "nan";
    std::printf("%-12g %-9s signbit=%d isnormal=%d\n", x, cn, std::signbit(x), std::isnormal(x));
  }
  std::printf("%-12s %-9s\n", "inf", std::fpclassify(vinf) == FP_INFINITE ? "infinite" : "?");
  std::printf("%-12s %-9s\n", "nan", std::fpclassify(nan) == FP_NAN ? "nan" : "?");

  std::printf("== numerical algorithms\n");
  {
    double naive = 0, kahan = 0, comp = 0;
    for (int i = 0; i < 100000; i++) {
      double v = vd[10];  // 0.1
      naive += v;
      double y = v - comp;
      double t = kahan + y;
      comp = (t - kahan) - y;
      kahan = t;
    }
    std::printf("sum of 100000 x 0.1: naive=%.17g kahan=%.17g\n", naive, kahan);
    float fsum = 0;
    for (int i = 1; i <= 10000; i++) fsum += 1.0f / (float)(i + (int)vd[0]);
    std::printf("float harmonic(10000)=%.9g\n", (double)fsum);
    double x = vd[19];  // 7
    double r = x;
    for (int i = 0; i < 8; i++) {
      r = r - (r * r * r - x) / (3 * r * r);
      std::printf("newton cbrt(7) iter %d: %.17g\n", i, r);
    }
    double pi = 0;
    for (int k = 0; k < 2000; k++) pi += (k % 2 ? -4.0 : 4.0) / (2 * k + 1 + vd[0]);
    std::printf("leibniz pi(2000)=%.17g\n", pi);
    double e = 1, term = 1;
    for (int k = 1; k < 20; k++) {
      term /= k + vd[0];
      e += term;
    }
    std::printf("e by series=%.17g diff from exp(1)=%g\n", e, e - std::exp(vd[2]));
  }
  return 0;
}
