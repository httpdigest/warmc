// stdarg: functions declared with `...` that walk their arguments with va_arg,
// choosing each one's type from a format string, with va_copy to walk them
// twice and a v-function handed the va_list -- printf's calling convention,
// written out so that the program's own code is what runs.
//
// wasm has no variadic calls.  clang has the caller store the variadic
// arguments into a buffer in its own shadow-stack frame, each at its natural
// alignment, and pass the buffer's address as a trailing i32.  A va_list is
// that pointer, va_arg is a load and a bump -- rounded up to 8 first for a
// double or a long long -- and va_copy copies the pointer.  So every call is a
// run of stores that the callee reads straight back, the traffic that
// gobench/variadic's argument slice is in Go, here with no allocator involved.
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

typedef struct { int32_t x, y; } Pt;

// A format string chooses the type of each argument, as in printf and every
// logging or serialisation helper built on stdarg.
__attribute__((noinline)) uint64_t vpack(const char *spec, va_list ap) {
  uint64_t h = 1469598103934665603ull;
  for (const char *p = spec; *p; p++) {
    uint64_t v = 0;
    switch (*p) {
      case 'c': v = (unsigned char)va_arg(ap, int); break;
      case 'i': v = (uint32_t)va_arg(ap, int); break;
      case 'l': v = (uint64_t)va_arg(ap, long long); break;
      case 'd': v = (uint64_t)(int64_t)(va_arg(ap, double) * 1024.0); break;
      case 's': {
        const char *s = va_arg(ap, const char *);
        while (*s) v = v * 131 + (unsigned char)*s++;
        break;
      }
      case 'p': {
        const Pt *q = va_arg(ap, const Pt *);
        v = (uint64_t)(q->x * 7 + q->y);
        break;
      }
    }
    h = (h ^ v) * 1099511628211ull;
  }
  return h;
}

// Walks its arguments twice: once through a va_copy to count the integers,
// then hands the original list on, the way a v-function wrapper does.
__attribute__((noinline)) uint64_t pack(const char *spec, ...) {
  va_list ap, aq;
  va_start(ap, spec);
  va_copy(aq, ap);
  uint64_t ints = 0;
  for (const char *p = spec; *p && *p != 'd'; p++) {
    if (*p == 'i' || *p == 'c') ints += (uint32_t)va_arg(aq, int);
    else if (*p == 'l') ints += (uint64_t)va_arg(aq, long long);
    else if (*p == 's') (void)va_arg(aq, const char *);
    else (void)va_arg(aq, const Pt *);
  }
  va_end(aq);
  uint64_t h = vpack(spec, ap);
  va_end(ap);
  return h + ints;
}

__attribute__((noinline)) int64_t sum(int n, ...) {
  va_list ap;
  va_start(ap, n);
  int64_t s = 0;
  for (int i = 0; i < n; i++) s = s * 3 + va_arg(ap, int);
  va_end(ap);
  return s;
}

__attribute__((noinline)) double mean(int n, ...) {
  va_list ap;
  va_start(ap, n);
  double s = 0;
  for (int i = 0; i < n; i++) s += va_arg(ap, double);
  va_end(ap);
  return s / n;
}

int main(void) {
  static const char *names[8] = { "alpha", "beta", "gamma", "delta", "eps", "zeta", "eta", "theta" };
  Pt pts[16];
  for (int i = 0; i < 16; i++) pts[i] = (Pt){ i * 3 - 7, 100 - i * i };
  uint64_t h = 0;
  int64_t s = 0;
  double m = 0;
  uint32_t x = 42;
  for (int i = 0; i < 5500000; i++) {
    x = x * 1664525u + 1013904223u;
    int a = (int)(x >> 3), b = (int)(x >> 11) - 100000;
    switch (x >> 30) {
      case 0: h += pack("ilds", a, (long long)a * 3, (double)(x & 0xffff) / 3.0, names[x & 7]); break;
      case 1: h += pack("pip", &pts[x & 15], b, &pts[(x >> 4) & 15]); break;
      case 2: h += pack("cliild", 'a' + (x & 15), (long long)b * 1048576, a, b, (long long)a * b, -1.5 * b); break;
      default: h += pack("i", b); break;
    }
    s += sum(1 + (int)(x & 3), a & 0xff, b, a >> 4, b ^ a);
    if ((i & 7) == 0) m += mean(3, (double)a, (double)b * 0.5, 0.125);
  }
  printf("h = %016llx s = %lld m = %.6f\n", (unsigned long long)h, (long long)s, m);
  return 0;
}
