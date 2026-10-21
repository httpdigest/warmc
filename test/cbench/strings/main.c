// The C string functions over a table of short and long strings: strlen,
// strcmp, strchr, strstr, memchr and memcmp, as wasi-libc implements them --
// a word at a time where it can, so the row exercises unaligned wide loads and
// the bit tricks that find a zero byte in a word.
//
// rsbench/utf8scan and asbench/strscan scan bytes with loops the producer
// wrote; this is the libc a C program links instead.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

int main(void) {
  enum { N = 256 };
  static char buf[N][200];
  uint32_t x = 7;
  for (int i = 0; i < N; i++) {
    int len = 8 + (i * 37) % 180;
    for (int j = 0; j < len; j++) {
      x = x * 1664525u + 1013904223u;
      buf[i][j] = (char)('a' + (x >> 24) % 23);
    }
    buf[i][len] = 0;
  }
  uint64_t sum = 0;
  for (int r = 0; r < 1800; r++) {
    for (int i = 0; i < N; i++) {
      const char *s = buf[i], *t = buf[(i * 7 + r) % N];
      sum += strlen(s);
      sum += (uint64_t)(strcmp(s, t) > 0);
      const char *c = strchr(s, 'w');
      sum += c ? (uint64_t)(c - s) : 3;
      const char *k = strstr(s, "ab");
      sum += k ? (uint64_t)(k - s) : 5;
      const void *m = memchr(s, 'q', strlen(s));
      sum += m ? 1 : 2;
      sum += (uint64_t)(memcmp(s, t, 8) < 0);
    }
  }
  printf("sum = %llu\n", (unsigned long long)sum);
  return 0;
}
