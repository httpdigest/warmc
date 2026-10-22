// Variadic functions written with stdarg: sums over int, long long and double
// arguments, a mini-printf that walks a format string and pulls each argument
// at the width it names, va_copy to scan the list twice, a v-function taking
// a va_list from a wrapper, a sentinel-terminated list of strings, struct
// arguments passed through `...` by value, and promoted types (char, short and
// float arrive as int and double).  vsnprintf is driven through a wrapper too.
//
// clang for wasm32 packs the variadic arguments into a buffer in the caller's
// frame, each at its natural alignment (8 for double and long long), and
// passes its address; va_arg is a load and an aligned bump of the pointer.
// Mixing 4- and 8-byte arguments makes the padding between them matter.
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int sum_ints(int n, ...) {
  va_list ap;
  va_start(ap, n);
  int s = 0;
  for (int i = 0; i < n; i++) s += va_arg(ap, int);
  va_end(ap);
  return s;
}
static long long sum_ll(int n, ...) {
  va_list ap;
  va_start(ap, n);
  long long s = 0;
  for (int i = 0; i < n; i++) s = s * 3 + va_arg(ap, long long);
  va_end(ap);
  return s;
}
static double sum_dbl(int n, ...) {
  va_list ap;
  va_start(ap, n);
  double s = 0;
  for (int i = 0; i < n; i++) s = s * 0.5 + va_arg(ap, double);
  va_end(ap);
  return s;
}

// Mini printf: i=int, l=long long, d=double, s=string, c=char, p=struct by value
typedef struct { int16_t a; int32_t b; double c; } Triple;

static int vmini(char *out, size_t cap, const char *fmt, va_list ap) {
  size_t n = 0;
  char tmp[64];
  for (const char *p = fmt; *p; p++) {
    int k = 0;
    switch (*p) {
      case 'i': k = snprintf(tmp, sizeof tmp, "%d", va_arg(ap, int)); break;
      case 'u': k = snprintf(tmp, sizeof tmp, "%u", va_arg(ap, unsigned)); break;
      case 'l': k = snprintf(tmp, sizeof tmp, "%lld", va_arg(ap, long long)); break;
      case 'd': k = snprintf(tmp, sizeof tmp, "%.4f", va_arg(ap, double)); break;
      case 's': k = snprintf(tmp, sizeof tmp, "%s", va_arg(ap, const char *)); break;
      case 'c': k = snprintf(tmp, sizeof tmp, "%c", (char)va_arg(ap, int)); break;
      case 'p': {
        Triple t = va_arg(ap, Triple);
        k = snprintf(tmp, sizeof tmp, "{%d,%d,%g}", t.a, t.b, t.c);
        break;
      }
      default: tmp[0] = *p; k = 1; break;
    }
    for (int j = 0; j < k && n + 1 < cap; j++) out[n++] = tmp[j];
    if (n + 1 < cap) out[n++] = ' ';
  }
  out[n] = 0;
  return (int)n;
}
static int mini(char *out, size_t cap, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vmini(out, cap, fmt, ap);
  va_end(ap);
  return n;
}

// Scans twice: first through a copy to count numeric args, then for real.
static void twice(const char *fmt, ...) {
  va_list ap, cp;
  va_start(ap, fmt);
  va_copy(cp, ap);
  int ints = 0, dbls = 0;
  long long isum = 0;
  double dsum = 0;
  for (const char *p = fmt; *p; p++) {
    if (*p == 'i') { ints++; isum += va_arg(cp, int); }
    else if (*p == 'd') { dbls++; dsum += va_arg(cp, double); }
    else if (*p == 'l') { ints++; isum += va_arg(cp, long long); }
    else if (*p == 's') (void)va_arg(cp, const char *);
  }
  va_end(cp);
  char buf[256];
  vmini(buf, sizeof buf, fmt, ap);
  va_end(ap);
  printf("[%s] ints=%d isum=%lld dbls=%d dsum=%g\n", buf, ints, isum, dbls, dsum);
}

static size_t concat(char *out, ...) {
  va_list ap;
  va_start(ap, out);
  size_t n = 0;
  const char *s;
  while ((s = va_arg(ap, const char *)) != NULL) {
    size_t k = strlen(s);
    memcpy(out + n, s, k);
    n += k;
  }
  out[n] = 0;
  va_end(ap);
  return n;
}

static void promoted(int n, ...) {
  va_list ap;
  va_start(ap, n);
  for (int i = 0; i < n; i++) {
    int c = va_arg(ap, int);         // char argument
    int s = va_arg(ap, int);         // short argument
    double f = va_arg(ap, double);   // float argument
    unsigned u = va_arg(ap, unsigned);  // unsigned char argument
    printf("promoted: c=%d s=%d f=%.9g u=%u\n", c, s, f, u);
  }
  va_end(ap);
}

static int logf_wrapper(char *buf, size_t cap, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, cap, fmt, ap);
  va_end(ap);
  return n;
}

// Forward a va_list through two levels, consuming some on each level.
static long long level2(va_list *ap, int n) {
  long long s = 0;
  for (int i = 0; i < n; i++) s += va_arg(*ap, long long) * (i + 1);
  return s;
}
static long long level1(int first, ...) {
  va_list ap;
  va_start(ap, first);
  long long a = level2(&ap, first);
  int mid = va_arg(ap, int);
  long long b = level2(&ap, 2);
  va_end(ap);
  return a * 1000 + mid * 100 + b;
}

int main(void) {
  printf("-- homogeneous\n");
  printf("%d %d %d %d\n", sum_ints(0), sum_ints(1, 5), sum_ints(3, 1, 2, 3),
         sum_ints(10, 1, -2, 3, -4, 5, -6, 7, -8, 9, -10));
  for (int k = 1; k <= 8; k++)
    printf("ll k=%d %lld\n", k,
           sum_ll(k, 1ll << 40, -7ll, 123456789012ll, (long long)k, -1ll, 99ll,
                  1ll << 62 >> k, 5ll));
  for (int k = 1; k <= 6; k++)
    printf("dbl k=%d %.17g\n", k, sum_dbl(k, 1.5, -0.25, 1e10, 3.0, 1e-10, 7.75));
  printf("-- mixed\n");
  char buf[256];
  Triple t1 = {-7, 70000, 2.5}, t2 = {32767, -1, -0.125};
  int n = mini(buf, sizeof buf, "i-l-d-s-c", 42, 1234567890123ll, 3.14159, "str", 'Z');
  printf("%d: %s\n", n, buf);
  n = mini(buf, sizeof buf, "cicdcl", 'a', 1, 'b', 2.0, 'c', -3ll);
  printf("%d: %s\n", n, buf);
  n = mini(buf, sizeof buf, "pip", t1, 99, t2);
  printf("%d: %s\n", n, buf);
  n = mini(buf, sizeof buf, "dididididi", 0.5, 1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5);
  printf("%d: %s\n", n, buf);
  n = mini(buf, sizeof buf, "lililil", 1ll, 2, 3ll, 4, 5ll, 6, 7ll);
  printf("%d: %s\n", n, buf);
  n = mini(buf, 12, "ssss", "truncated", "output", "here", "!");
  printf("%d: %s\n", n, buf);
  for (int i = 0; i < 8; i++) {
    n = mini(buf, sizeof buf, "uldi", (unsigned)(i * 1000000007u), (long long)i << 33,
             i / 3.0, -i);
    printf("%d: %s\n", n, buf);
  }
  printf("-- va_copy\n");
  twice("idl", 7, 2.5, 99999999999ll);
  twice("ddsii", 0.1, 0.2, "x", -5, 6);
  twice("lllll", 1ll, 2ll, 3ll, 4ll, 5ll);
  twice("isidid", 1, "two", 3, 4.0, 5, 6.0);
  printf("-- sentinel\n");
  char out[128];
  size_t len = concat(out, "alpha", "-", "beta", "-", "", "gamma", (char *)NULL);
  printf("%zu %s\n", len, out);
  len = concat(out, (char *)NULL);
  printf("%zu [%s]\n", len, out);
  printf("-- promotions\n");
  char c = -5;
  short s = -30000;
  float f = 1.1f;
  unsigned char uc = 250;
  promoted(2, c, s, f, uc, (char)'A', (short)1234, 0.1f, (unsigned char)7);
  printf("-- vsnprintf wrapper\n");
  for (int cap = 4; cap <= 40; cap += 9) {
    char small[64];
    n = logf_wrapper(small, (size_t)cap, "%s:%05d:%-6.2f|%x", "log", 42, 3.14159, 0xabcu);
    printf("cap=%d n=%d [%s]\n", cap, n, small);
  }
  printf("-- va_list passed by pointer\n");
  printf("%lld\n", level1(3, 1ll, 2ll, 3ll, 7, 10ll, 20ll));
  printf("%lld\n", level1(0, 5, -1ll, -2ll));
  printf("%lld\n", level1(1, 1000000ll, -3, 4ll, 5ll));
  return 0;
}
