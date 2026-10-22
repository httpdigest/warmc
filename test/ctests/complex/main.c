// <complex.h>: double complex and float complex arithmetic (multiplication
// and division go through compiler-rt's __muldc3 / __divdc3, which handle the
// infinite and NaN cases C's Annex G requires), creal/cimag/conj/cabs/carg,
// cexp, clog, csqrt, cpow, csin, ccos, ctan, the hyperbolic and inverse
// functions, CMPLX building values with signed zeros, the branch cuts that
// those zeros select, and a few applications: roots of unity, a quadratic
// solver, a small DFT and its inverse, and Mandelbrot escape counts.
//
// A double complex is two f64 values passed and returned through memory on
// wasm32, so every call here also checks the by-value struct ABI.  Results
// are printed to 15 significant digits; NaNs are classified, not printed.
#include <complex.h>
#include <math.h>
#include <stdio.h>

static void pc(const char *name, double complex z) {
  double re = creal(z), im = cimag(z);
  printf(" %s=", name);
  if (isnan(re)) printf("nan");
  else printf("%.15g", re);
  printf(",");
  if (isnan(im)) printf("nan");
  else printf("%.15g", im);
}

double complex zs[] = {
  0.0, 1.0, -1.0, 1.0 * I, -2.0 * I, 1.0 + 1.0 * I, -3.0 + 4.0 * I, 0.5 - 0.25 * I,
  2.0 + 0.0 * I, -0.75 - 1.5 * I, 10.0 + 10.0 * I, 1e-3 + 2e3 * I,
};
#define NZ (sizeof zs / sizeof zs[0])

static void arithmetic(void) {
  printf("-- arithmetic\n");
  for (unsigned i = 0; i < NZ; i++) {
    double complex a = zs[i], b = zs[(i * 5 + 3) % NZ];
    printf("a=%g%+gi b=%g%+gi\n ", creal(a), cimag(a), creal(b), cimag(b));
    pc("a+b", a + b);
    pc("a-b", a - b);
    pc("a*b", a * b);
    if (b != 0) pc("a/b", a / b);
    pc("conj", conj(a));
    pc("-a", -a);
    printf("\n  |a|=%.15g arg=%.15g eq=%d\n", cabs(a), carg(a), a == b);
  }
}

static void functions(void) {
  printf("-- elementary functions\n");
  for (unsigned i = 1; i < NZ; i++) {
    double complex z = zs[i] * 0.5;
    printf("z=%g%+gi\n ", creal(z), cimag(z));
    pc("exp", cexp(z));
    pc("log", clog(z));
    pc("sqrt", csqrt(z));
    printf("\n ");
    pc("sin", csin(z));
    pc("cos", ccos(z));
    pc("tan", ctan(z));
    printf("\n ");
    pc("sinh", csinh(z));
    pc("cosh", ccosh(z));
    pc("tanh", ctanh(z));
    printf("\n ");
    pc("asin", casin(z));
    pc("acos", cacos(z));
    pc("atan", catan(z));
    printf("\n ");
    pc("asinh", casinh(z));
    pc("acosh", cacosh(z));
    pc("atanh", catanh(z));
    printf("\n ");
    pc("pow", cpow(z, 1.5 - 0.5 * I));
    pc("proj", cproj(z));
    printf("\n");
  }
}

static void branch_cuts(void) {
  printf("-- signed zeros and branch cuts\n");
  double complex a = CMPLX(-4.0, 0.0), b = CMPLX(-4.0, -0.0);
  pc("sqrt(-4+0i)", csqrt(a));
  pc("sqrt(-4-0i)", csqrt(b));
  printf("\n");
  pc("log(-1+0i)", clog(CMPLX(-1.0, 0.0)));
  pc("log(-1-0i)", clog(CMPLX(-1.0, -0.0)));
  printf("\n");
  printf(" carg: %.15g %.15g %.15g %.15g\n", carg(CMPLX(-1.0, 0.0)), carg(CMPLX(-1.0, -0.0)),
         carg(CMPLX(0.0, -0.0)), carg(CMPLX(-0.0, 0.0)));
  pc("asin(2+0i)", casin(CMPLX(2.0, 0.0)));
  pc("asin(2-0i)", casin(CMPLX(2.0, -0.0)));
  printf("\n");
  printf(" signbit imag of CMPLX(1,-0): %d, of 1-0.0*I: %d\n", signbit(cimag(CMPLX(1.0, -0.0))) != 0,
         signbit(cimag(1.0 - 0.0 * I)) != 0);
}

static void infinities(void) {
  printf("-- infinite operands (Annex G)\n");
  double inf = INFINITY;
  double complex big = CMPLX(inf, 1.0);
  double complex z = CMPLX(2.0, 3.0);
  double complex p = big * z;
  double complex q = z / CMPLX(0.0, 0.0);
  double complex r = z / big;
  printf(" inf*z: isinf=%d %d  z/0: isinf=%d  z/inf: %g %g\n", isinf(creal(p)) != 0, isinf(cimag(p)) != 0,
         isinf(creal(q)) || isinf(cimag(q)), creal(r), cimag(r));
  printf(" cabs(inf+nan i)=%g\n", cabs(CMPLX(inf, NAN)));
  double complex e = cexp(CMPLX(-inf, 0.0));
  printf(" cexp(-inf)=%g,%g\n", creal(e), cimag(e));
}

static void float_complex(void) {
  printf("-- float complex\n");
  float complex f = 1.5f + 2.5f * I, g = -0.5f + 1.0f * I;
  for (int i = 0; i < 6; i++) {
    float complex m = f * g, d = f / g;
    printf(" f=%.7g%+.7gi m=%.7g%+.7gi d=%.7g%+.7gi abs=%.7g exp=%.7g%+.7gi\n", (double)crealf(f),
           (double)cimagf(f), (double)crealf(m), (double)cimagf(m), (double)crealf(d), (double)cimagf(d),
           (double)cabsf(f), (double)crealf(cexpf(f * 0.25f)), (double)cimagf(cexpf(f * 0.25f)));
    f = m * 0.3f + d;
  }
}

static void applications(void) {
  printf("-- roots of unity\n");
  for (int n = 3; n <= 8; n++) {
    double complex sum = 0, prod = 1;
    for (int k = 0; k < n; k++) {
      double complex w = cexp(2 * M_PI * I * k / n);
      sum += w;
      prod *= w;
    }
    printf(" n=%d |sum|<1e-12: %d prod=%.12f%+.12fi\n", n, cabs(sum) < 1e-12, creal(prod), cimag(prod));
  }
  printf("-- quadratics\n");
  double coef[][3] = {{1, -3, 2}, {1, 2, 5}, {2, 0, 8}, {1, -2, 1}, {3, 1, -7}};
  for (int i = 0; i < 5; i++) {
    double a = coef[i][0], b = coef[i][1], c = coef[i][2];
    double complex d = csqrt(b * b - 4 * a * c + 0 * I);
    double complex r1 = (-b + d) / (2 * a), r2 = (-b - d) / (2 * a);
    double complex check = a * r1 * r1 + b * r1 + c;
    printf(" %gx^2%+gx%+g: r1=%.10f%+.10fi r2=%.10f%+.10fi residual<1e-9=%d\n", a, b, c, creal(r1),
           cimag(r1), creal(r2), cimag(r2), cabs(check) < 1e-9);
  }
  printf("-- DFT round trip\n");
  enum { N = 16 };
  double complex x[N], X[N], y[N];
  for (int n = 0; n < N; n++) x[n] = sin(n * 0.7) + 0.5 * cos(n * 2.1) + (n % 3) * 0.1 * I;
  for (int k = 0; k < N; k++) {
    X[k] = 0;
    for (int n = 0; n < N; n++) X[k] += x[n] * cexp(-2 * M_PI * I * k * n / N);
  }
  double err = 0;
  for (int n = 0; n < N; n++) {
    y[n] = 0;
    for (int k = 0; k < N; k++) y[n] += X[k] * cexp(2 * M_PI * I * k * n / N);
    y[n] /= N;
    double e = cabs(y[n] - x[n]);
    if (e > err) err = e;
  }
  for (int k = 0; k < N; k++) printf(" X[%2d]=%.9f%+.9fi\n", k, creal(X[k]), cimag(X[k]));
  printf(" roundtrip err<1e-12: %d\n", err < 1e-12);
  printf("-- mandelbrot\n");
  for (int row = 0; row < 12; row++) {
    printf(" ");
    for (int col = 0; col < 40; col++) {
      double complex c = CMPLX(-2.2 + col * 0.075, -1.1 + row * 0.2);
      double complex z = 0;
      int it = 0;
      while (it < 60 && cabs(z) <= 2) {
        z = z * z + c;
        it++;
      }
      putchar(" .:-=+*#%@"[it * 9 / 60]);
    }
    printf("\n");
  }
}

int main(void) {
  arithmetic();
  functions();
  branch_cuts();
  infinities();
  float_complex();
  applications();
  return 0;
}
