// Text to numbers through the C library: strtol with base 0 over decimal and
// hex integers, atoi over the same fields, and strtod over floats with short
// and full 17-digit mantissas and with exponents -- the reading half that
// printf is the writing half of, and what every config, CSV and protocol
// parser calls.
//
// These are musl's scanners: strtol goes through __intscan and strtod through
// __floatscan, which reads the string through a FILE set up over it and, for a
// mantissa longer than a double holds exactly, gathers the digits nine at a
// time into base-10^9 limbs in a 512-byte stack frame and rounds from those.
// atoi is musl's own short loop.  Like the printf kernel it spends its time in
// big, branchy library code rather than in a loop the program wrote.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

enum { SIZE = 1 << 17 };
static char text[SIZE + 64];

int main(void) {
  // Fields of a few kinds, written once with snprintf: decimal and hex
  // integers, floats with a short and a full-precision mantissa, and exponents.
  uint32_t x = 99991;
  size_t at = 0;
  while (at < SIZE) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    int32_t v = (int32_t)x >> (x & 15);
    double d = (double)v / (double)(1 + (x >> 20));
    char *o = text + at;
    switch (x % 6) {
      case 0: at += (size_t)snprintf(o, 40, "%d ", v); break;
      case 1: at += (size_t)snprintf(o, 40, "0x%x, ", (uint32_t)v >> 4); break;
      case 2: at += (size_t)snprintf(o, 40, "f%.4f ", d); break;
      case 3: at += (size_t)snprintf(o, 40, "f%.17g ", d); break;
      case 4: at += (size_t)snprintf(o, 40, "f%.6e ", d * 1e-7); break;
      default: at += (size_t)snprintf(o, 40, "  %u\t", x >> 9); break;
    }
  }
  text[at] = 0;

  int64_t isum = 0;
  double fsum = 0;
  uint32_t fields = 0, asum = 0;
  for (int r = 0; r < 60; r++) {
    const char *p = text;
    char *end;
    for (;;) {
      while (*p == ' ' || *p == ',' || *p == '\t') p++;
      if (!*p) break;
      if (*p == 'f') {
        fsum += strtod(p + 1, &end);
      } else {
        // Base 0 takes 0x as hex and anything else as decimal.
        long v = strtol(p, &end, 0);
        isum += v;
        asum += (uint32_t)atoi(p);
      }
      // A field the last round's damage left unparseable is skipped.
      p = end > p ? end : p + 1;
      fields++;
    }
    text[(size_t)r * 4099 % at] = ' ';
  }
  printf("fields = %u isum = %lld asum = %u fsum = %.17g\n", fields, (long long)isum, asum, fsum);
  return 0;
}
