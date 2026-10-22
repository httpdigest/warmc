// printf and snprintf of floating point: %f, %e, %g and %a with and without
// precision, field widths, the -, +, space, 0 and # flags, the * forms that
// take width and precision as arguments, float arguments promoted to double,
// long double (a 128-bit soft float on wasm32), infinities, and the round-half
// cases where the decimal conversion has to decide which way a tie goes.
//
// musl's printf converts with exact multiprecision arithmetic on 32-bit limbs
// (a billion per limb), so every line here runs divisions and multiplications
// by 10^9 and long carry chains -- the integer code a floating-point format
// actually executes.  snprintf's return value and truncation are checked too.
#include <math.h>
#include <stdio.h>
#include <string.h>

double vals[] = {0.0, -0.0, 1.0, -1.0, 0.5, 0.125, 0.1, 1.0 / 3, 2.0 / 3,
                 123.456, -98765.4321, 1e-5, 1.5e-7, 6.02214076e23, 1e100,
                 1.7976931348623157e308, 2.2250738585072014e-308,
                 4.9406564584124654e-324, 9.999999, 99.995, 0.0005, 1e21,
                 123456789012345678.0, 3.14159265358979323846, 2.5, 3.5};
#define NV (sizeof vals / sizeof vals[0])

static void basic(void) {
  printf("-- %%f %%e %%g %%a defaults\n");
  for (unsigned i = 0; i < NV; i++) {
    double v = vals[i];
    if (fabs(v) < 1e30)
      printf("[%f] [%e] [%g] [%a]\n", v, v, v, v);
    else
      printf("[%.3e] [%e] [%g] [%a]\n", v, v, v, v);
  }
}

static void precisions(void) {
  printf("-- precision sweep\n");
  double ps[] = {1.0 / 3, 2.0 / 3, 0.5, 1234.5678, 9.9999999, 1e-10, -0.0625};
  for (unsigned i = 0; i < sizeof ps / sizeof ps[0]; i++) {
    double v = ps[i];
    for (int p = 0; p <= 17; p += (p < 6 ? 1 : 4)) {
      printf("p=%2d %.*f | %.*e | %.*g | %.*a\n", p, p, v, p, v, p, v,
             p > 13 ? 13 : p, v);
    }
  }
}

static void flags(void) {
  printf("-- widths and flags\n");
  double fs[] = {3.14159, -2.5, 0.0, 1e10, -1e-10, 42.0};
  for (unsigned i = 0; i < sizeof fs / sizeof fs[0]; i++) {
    double v = fs[i];
    printf("[%12f][%-12f][%+f][% f][%012.3f][%+012.3f][%-+12.2f]\n", v, v, v,
           v, v, v, v);
    printf("[%15e][%-15.2e][%+.1E][%015.4e][%#.0e][%#.0f][%.0f]\n", v, v, v, v,
           v, v, v);
    printf("[%10g][%-10g][%#g][%#.3g][%G][%010g][%+g][% g]\n", v, v, v, v, v,
           v, v, v);
    printf("[%20a][%-20A][%+.2a][%#a][%020a]\n", v, v, v, v, v);
    for (int w = 0; w < 14; w += 4)
      printf("[%*.*f][%-*.*e]", w, w / 3, v, w, w / 4, v);
    printf("\n");
  }
}

static void ties(void) {
  printf("-- rounding ties\n");
  double ts[] = {0.5, 1.5, 2.5, 3.5, -0.5, -2.5, 0.125, 0.375, 0.0625,
                 1.0005, 1.0015, 1.0025, 2.675, 1.005, 1e15 + 0.5, 5e-324,
                 0.045, 1.45, 8.5, 9.5};
  for (unsigned i = 0; i < sizeof ts / sizeof ts[0]; i++) {
    double v = ts[i];
    printf("%.20g: %.0f %.1f %.2f %.3f %.0e %.1e %.2g\n", v, v, v, v, v, v, v,
           v);
  }
}

static void specials(void) {
  printf("-- infinities and big exponents\n");
  double inf = INFINITY;
  printf("[%f][%e][%g][%a][%F][%E][%G][%A]\n", inf, -inf, inf, -inf, inf,
         -inf, inf, inf);
  printf("[%10f][%-10f][%+f][%010f]\n", inf, inf, inf, -inf);
  for (int e = -320; e <= 308; e += 37) {
    double v = 1.234567890123 * pow(10, e);
    printf("1.23e%d: %.6e %.10g %a\n", e, v, v, v);
  }
  printf("%.0f\n", 1e300);
  printf("%.40f\n", 1.0 / 3);
  printf("%.60e\n", 0.1);
  printf("%.30g\n", 2.0 / 3);
  printf("%f\n", 1.7976931348623157e308 / 1e290);
}

static void floats_and_long_double(void) {
  printf("-- float and long double arguments\n");
  float f[] = {0.1f, 1.0f / 3, 16777217.0f, 3.4028235e38f, 1.17549435e-38f,
               -2.75f};
  for (unsigned i = 0; i < sizeof f / sizeof f[0]; i++)
    printf("%f %.9g %e %a\n", f[i], f[i], f[i], f[i]);
  // wasi-libc's printf is linked without long double support, so a long
  // double is printed as its frexpl exponent and the mantissa split into two
  // doubles: the soft-float arithmetic is what is being checked.
  long double ld[] = {1.0L / 3, 0.1L, 1e4000L, -2.5L,
                      1.18973149535723176502e+4932L, 3.36210314311209350626e-4932L};
  for (unsigned i = 0; i < sizeof ld / sizeof ld[0]; i++) {
    int e;
    long double m = frexpl(ld[i], &e);
    double hi = (double)m;
    double lo = (double)(m - hi);
    printf("ld e=%d m=%a + %a (%.17g)\n", e, hi, lo, hi);
  }
  long double acc = 1;
  for (int i = 1; i <= 30; i++) {
    acc *= (long double)i / 3;
    if (i % 6 == 0) {
      double hi = (double)acc;
      printf("ld %d: %.17g + %a\n", i, hi, (double)(acc - hi));
    }
  }
}

static void snprintf_checks(void) {
  printf("-- snprintf returns and truncation\n");
  char buf[32];
  for (size_t cap = 0; cap <= 24; cap += 3) {
    memset(buf, '#', sizeof buf);
    int n = snprintf(buf, cap, "%.6f|%e", 3.14159265358979, -1e-7);
    buf[31] = 0;
    printf("cap=%2zu n=%d buf=", cap, n);
    // print up to cap bytes, showing where the terminator landed
    for (size_t i = 0; i < cap + 2 && i < 31; i++)
      putchar(buf[i] ? buf[i] : '0');
    putchar('\n');
  }
  char big[400];
  int n = snprintf(big, sizeof big, "%.300f", 1e-200);
  printf("long: n=%d tail=%s\n", n, big + n - 20);
  n = snprintf(NULL, 0, "%g %g %g", 1e-300, 1e300, 0.1);
  printf("NULL size: %d\n", n);
  // %n stores the count so far
  int a = 0, b = 0;
  snprintf(buf, sizeof buf, "%f%n %e%n", 1.0, &a, 2.0, &b);
  printf("%%n: %d %d [%s]\n", a, b, buf);
}

static void table(void) {
  printf("-- a table of numbers\n");
  printf("%-6s|%12s|%12s|%12s\n", "n", "sqrt", "exp", "1/n");
  for (int i = 1; i <= 20; i++)
    printf("%-6d|%12.6f|%12.5g|%12.4e\n", i, sqrt(i), exp(i), 1.0 / i);
}

int main(void) {
  basic();
  precisions();
  flags();
  ties();
  specials();
  floats_and_long_double();
  snprintf_checks();
  table();
  return 0;
}
