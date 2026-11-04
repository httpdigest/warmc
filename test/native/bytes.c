/* The C functions test/native/bytes.wat calls: each byte array parameter,
   (array i8) or (array (mut i8)), arrives as the address of the array's
   bytes and their count -- two arguments, in registers or past the eighth
   integer on the stack -- which C reads, or writes, where they are.  No
   libc: on Linux it links with none. */
#include <stddef.h>
#include <stdint.h>

static int64_t sum(const uint8_t *p, size_t n) {
  int64_t s = 0;
  for (size_t i = 0; i < n; i++) s += p[i];
  return s;
}

int64_t nb_sum(const uint8_t *p, size_t n) { return sum(p, n); }

int32_t nb_count(const uint8_t *p, size_t n, int32_t c) {
  int32_t k = 0;
  for (size_t i = 0; i < n; i++) k += p[i] == (uint8_t)c;
  return k;
}

void nb_fill(uint8_t *p, size_t n, int32_t c) {
  for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(c + (int32_t)i);
}

/* as memcmp, then the shorter first */
int32_t nb_cmp(const uint8_t *a, size_t an, const uint8_t *b, size_t bn) {
  size_t n = an < bn ? an : bn;
  for (size_t i = 0; i < n; i++)
    if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
  return an < bn ? -1 : an > bn ? 1 : 0;
}

/* five arrays among numbers: x0, x1-x2, d0, x3-x4, x5-x6, x7 and the
   stack, the rest on the stack */
int64_t nb_many(int32_t k, const uint8_t *a, size_t an, double x, const uint8_t *b, size_t bn,
                uint8_t *c, size_t cn, const uint8_t *d, size_t dn, int64_t m,
                uint8_t *e, size_t en) {
  if (cn) c[0] = 42;
  if (en) e[en - 1] = 43;
  return k + (int64_t)x + sum(a, an) + 10 * sum(b, bn) + 100 * (int64_t)cn +
         1000 * sum(d, dn) + m + 100000 * (int64_t)en;
}

/* where C was handed the bytes: in the collector's heap, the array's own */
int64_t nb_addr(const uint8_t *p, size_t n) {
  (void)n;
  return (int64_t)(uintptr_t)p;
}
