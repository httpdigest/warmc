// ctype.h and stdlib.h's conversions: every ctype predicate and toupper /
// tolower over all of 0..255 and EOF (in the C locale), then strtol, strtoul,
// strtoll and strtoull in bases 0, 2, 8, 10, 16 and 36 with leading space,
// signs, 0x prefixes, overflow (clamped with ERANGE), and the end pointer
// showing how far each one read; strtod and strtof on decimal, hex-float,
// inf/nan spellings and subnormal or overflowing inputs; atoi, atol, atoll and
// atof; abs, labs, llabs, div and the integer to string helpers written here.
//
// These are table lookups (ctype) and long digit loops with overflow checks
// (strto*), with strtod doing musl's exact decimal-to-binary conversion on
// big integers.  errno is reset before each call and printed after it.
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ctype_table(void) {
  printf("-- ctype over 0..255 (C locale)\n");
  int (*fns[])(int) = {isalnum, isalpha, isblank, iscntrl, isdigit, isgraph,
                       islower, isprint, ispunct, isspace, isupper, isxdigit};
  const char *names = "anbcdglpPsux";
  for (int f = 0; f < 12; f++) {
    printf("%c: ", names[f]);
    int count = 0;
    unsigned sig = 0;
    for (int c = 0; c < 256; c++) {
      int v = fns[f](c) != 0;
      count += v;
      sig = sig * 3 + (unsigned)v * (unsigned)c;
    }
    printf("count=%3d sig=%08x eof=%d\n", count, sig, fns[f](EOF) != 0);
  }
  for (int c = 32; c < 127; c++) {
    putchar(toupper(c));
    if (c % 48 == 31) putchar('\n');
  }
  putchar('\n');
  for (int c = 32; c < 127; c++) {
    putchar(tolower(c));
    if (c % 48 == 31) putchar('\n');
  }
  printf("\ntoupper(200)=%d tolower(EOF)=%d\n", toupper(200), tolower(EOF));
  // class string for printable ASCII
  for (int c = 32; c < 127; c++) {
    char k = isdigit(c) ? 'd' : isupper(c) ? 'U' : islower(c) ? 'l' : ispunct(c) ? 'p' : isspace(c) ? 's' : '?';
    putchar(k);
  }
  putchar('\n');
}

static void show_l(const char *s, int base) {
  char *end;
  errno = 0;
  long v = strtol(s, &end, base);
  int e1 = errno;
  errno = 0;
  unsigned long uv = strtoul(s, &end, base);
  int e2 = errno;
  errno = 0;
  char *end2;
  long long ll = strtoll(s, &end2, base);
  int e3 = errno;
  errno = 0;
  unsigned long long ull = strtoull(s, &end2, base);
  int e4 = errno;
  printf("%-24s b%-2d l=%ld%s ul=%lu%s ll=%lld%s ull=%llu%s used=%td\n", s, base,
         v, e1 == ERANGE ? "!" : "", uv, e2 == ERANGE ? "!" : "", ll,
         e3 == ERANGE ? "!" : "", ull, e4 == ERANGE ? "!" : "", end2 - s);
}

static void integers(void) {
  printf("-- strtol family\n");
  const char *in[] = {"0", "123", "-123", "  +42xyz", "\t\n -0", "0x1F", "0X1fg",
                      "077", "0b101", "z", "Zz", "2147483647", "2147483648",
                      "-2147483648", "-2147483649", "4294967295", "4294967296",
                      "9223372036854775807", "9223372036854775808",
                      "-9223372036854775808", "18446744073709551615",
                      "18446744073709551616", "-1", "0x", "  ", "+-5", "1e5"};
  int bases[] = {0, 10, 16, 8, 2, 36};
  for (unsigned i = 0; i < sizeof in / sizeof in[0]; i++)
    for (int b = 0; b < 6; b++)
      if (b == 0 || i % 3 == (unsigned)b % 3) show_l(in[i], bases[b]);
}

static void show_d(const char *s) {
  char *end;
  errno = 0;
  double d = strtod(s, &end);
  int e = errno;
  errno = 0;
  char *endf;
  float f = strtof(s, &endf);
  int ef = errno;
  printf("%-28s ", s);
  if (isnan(d))
    printf("d=nan");
  else
    printf("d=%.17g (%a)", d, d);
  printf("%s used=%td ", e == ERANGE ? " ERANGE" : "", end - s);
  if (isnan(f))
    printf("f=nan");
  else
    printf("f=%.9g", (double)f);
  printf("%s used=%td\n", ef == ERANGE ? " ERANGE" : "", endf - s);
}

static void floats(void) {
  printf("-- strtod and strtof\n");
  const char *in[] = {"0", "-0", "1", "3.14159", "  -2.5e3xyz", ".5", "5.", "1e",
                      "1e+", "1e-5", "0x1p4", "0x1.8p-1", "-0x.8p1", "0x1P",
                      "inf", "-Infinity", "INFINITYx", "nan", "-NaN(123)", "nanx",
                      "1e308", "1e309", "-1e400", "1e-320", "1e-400",
                      "2.2250738585072011e-308", "4.9e-324", "2.4703282292062328e-324",
                      "0.1", "0.30000000000000004", "123456789012345678901234567890",
                      "9007199254740993", "1.7976931348623157e308",
                      "1.7976931348623159e308", "3.4028235e38", "3.4028236e38",
                      "1.17549435e-38", "1e-45", "  +.e1", "abc", ""};
  for (unsigned i = 0; i < sizeof in / sizeof in[0]; i++) show_d(in[i]);
}

static void atox(void) {
  printf("-- atoi atol atoll atof\n");
  const char *in[] = {"42", "-17", "  +9abc", "0", "2147483647", "-2147483648",
                      "12345678901", "3.99", "-0.5e1", "x"};
  for (unsigned i = 0; i < sizeof in / sizeof in[0]; i++) {
    long long ll = atoll(in[i]);
    // atoi/atol are undefined on overflow, so only call them when it fits
    if (ll >= -2147483647 - 1 && ll <= 2147483647)
      printf("%-12s atoi=%d atol=%ld", in[i], atoi(in[i]), atol(in[i]));
    else
      printf("%-12s (too big for int)", in[i]);
    printf(" atoll=%lld atof=%g\n", ll, atof(in[i]));
  }
}

static char *utoa_base(unsigned long long v, int base, char *buf) {
  char tmp[72];
  int n = 0;
  do {
    int d = (int)(v % (unsigned)base);
    tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
    v /= (unsigned)base;
  } while (v);
  int k = 0;
  while (n) buf[k++] = tmp[--n];
  buf[k] = 0;
  return buf;
}

static void roundtrip(void) {
  printf("-- round trips\n");
  unsigned long long vals[] = {0, 1, 35, 36, 255, 65535, 1234567890123ull,
                               18446744073709551615ull, 0x8000000000000000ull};
  int bases[] = {2, 3, 8, 10, 16, 36};
  for (unsigned i = 0; i < sizeof vals / sizeof vals[0]; i++) {
    for (int b = 0; b < 6; b++) {
      char buf[72];
      utoa_base(vals[i], bases[b], buf);
      char *end;
      unsigned long long back = strtoull(buf, &end, bases[b]);
      printf("%s%s", buf, back == vals[i] && *end == 0 ? " " : "(MISMATCH) ");
    }
    printf("\n");
  }
  // doubles printed with %.17g parse back exactly
  double ds[] = {0.1, 1.0 / 3, 6.02214076e23, 5e-324, 1.7976931348623157e308, -2.5e-17};
  for (unsigned i = 0; i < sizeof ds / sizeof ds[0]; i++) {
    char buf[40];
    snprintf(buf, sizeof buf, "%.17g", ds[i]);
    double back = strtod(buf, NULL);
    printf("%s -> %s\n", buf, back == ds[i] ? "exact" : "MISMATCH");
  }
}

static void absdiv(void) {
  printf("-- abs labs llabs div\n");
  int v[] = {0, 5, -5, 2147483647, -2147483647};
  for (int i = 0; i < 5; i++) printf("abs(%d)=%d labs=%ld llabs=%lld\n", v[i], abs(v[i]), labs((long)v[i]), llabs((long long)v[i] * 1000));
  for (int a = -7; a <= 7; a += 7)
    for (int b = -3; b <= 3; b += 2) {
      div_t d = div(a, b);
      lldiv_t l = lldiv((long long)a * 1000000000000ll, b);
      printf("div(%d,%d)=%d r%d lldiv=%lld r%lld\n", a, b, d.quot, d.rem, l.quot, l.rem);
    }
}

int main(void) {
  ctype_table();
  integers();
  floats();
  atox();
  roundtrip();
  absdiv();
  return 0;
}
