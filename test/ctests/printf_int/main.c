// printf of integers, characters and strings: %d %i %u %o %x %X %c %s %%
// with every length modifier (hh, h, l, ll, j, z, t), field widths and
// precisions given literally and through *, negative * widths meaning left
// justification, the -, +, space, 0 and # flags in combination, precision on
// strings, and sprintf/snprintf building strings piece by piece.
//
// printf is a variadic function: each conversion pulls its argument out of
// the caller's va_list at the width the length modifier names, so a value
// stored at the wrong width or a promotion done wrong shows up in the column
// next to it.  The integer formatting itself is musl's loop of divisions by
// the base, with the hh and h modifiers narrowing the promoted int again.
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

long long ivals[] = {0, 1, -1, 7, -42, 127, -128, 255, 256, 32767, -32768,
                     65535, 2147483647, -2147483647 - 1, 4294967295ll,
                     1234567890123ll, LLONG_MAX, LLONG_MIN};
#define NI (sizeof ivals / sizeof ivals[0])

static void plain(void) {
  printf("-- plain conversions\n");
  for (unsigned i = 0; i < NI; i++) {
    long long v = ivals[i];
    int w = (int)v;
    printf("%d %i %u %o %x %X | %lld %llu %llo %llx\n", w, w, (unsigned)w,
           (unsigned)w, (unsigned)w, (unsigned)w, v, (unsigned long long)v,
           (unsigned long long)v, (unsigned long long)v);
  }
}

static void lengths(void) {
  printf("-- length modifiers\n");
  for (unsigned i = 0; i < NI; i++) {
    long long v = ivals[i];
    printf("hhd=%hhd hhu=%hhu hd=%hd hu=%hu hx=%hx ld=%ld lu=%lu jd=%jd ju=%ju "
           "zu=%zu zd=%zd td=%td\n",
           (signed char)v, (unsigned char)v, (short)v, (unsigned short)v,
           (unsigned short)v, (long)v, (unsigned long)v, (intmax_t)v,
           (uintmax_t)v, (size_t)v, (ptrdiff_t)(int)v, (ptrdiff_t)(int)v);
    printf("  PRId8=%" PRId8 " PRIu16=%" PRIu16 " PRIx32=%" PRIx32
           " PRId64=%" PRId64 " PRIX64=%" PRIX64 "\n",
           (int8_t)v, (uint16_t)v, (uint32_t)v, (int64_t)v, (uint64_t)v);
  }
}

static void widths(void) {
  printf("-- widths, precision and flags\n");
  int ws[] = {0, 5, -5, 123456, -7, 1, -1000};
  for (unsigned i = 0; i < sizeof ws / sizeof ws[0]; i++) {
    int v = ws[i];
    printf("[%6d][%-6d][%06d][%+d][% d][%+6d][%-+6d][% 06d]\n", v, v, v, v, v,
           v, v, v);
    printf("[%.3d][%8.3d][%-8.3d][%08.3d][%.0d][%5.0d][%+.2d]\n", v, v, v, v,
           v, v, v);
    printf("[%#o][%#x][%#X][%#8x][%#-8x][%#08x][%#.5x][%.0x]\n", (unsigned)v,
           (unsigned)v, (unsigned)v, (unsigned)v, (unsigned)v, (unsigned)v,
           (unsigned)v, (unsigned)v);
    for (int w = -9; w <= 9; w += 3) printf("[%*d]", w, v);
    for (int p = 0; p <= 8; p += 4) printf("[%.*d][%*.*x]", p, v, p + 2, p, (unsigned)v);
    printf("\n");
  }
}

static void chars_and_strings(void) {
  printf("-- chars and strings\n");
  const char *ss[] = {"", "a", "hello", "hello, world", "tab\there",
                      "UPPER lower 123"};
  for (unsigned i = 0; i < sizeof ss / sizeof ss[0]; i++) {
    const char *s = ss[i];
    printf("[%s][%10s][%-10s][%.3s][%10.2s][%-10.4s][%.0s][%*s][%-*.*s]\n", s,
           s, s, s, s, s, s, 6, s, 8, 2, s);
  }
  for (int c = 32; c < 127; c += 1) {
    printf("%c", c);
    if (c % 32 == 31) printf("\n");
  }
  printf("\n[%c][%3c][%-3c][%%]\n", 'x', 'y', 'z');
  char nonterm[5] = {'a', 'b', 'c', 'd', 'e'};
  printf("[%.5s][%.3s]\n", nonterm, nonterm);  // precision bounds the read
}

static void building(void) {
  printf("-- building strings\n");
  char buf[256];
  int pos = 0;
  for (int i = 0; i < 20; i++)
    pos += sprintf(buf + pos, "%s%d", i ? "," : "", i * i - 50);
  printf("%s (len %d, strlen %zu)\n", buf, pos, strlen(buf));
  for (size_t cap = 1; cap < 20; cap += 3) {
    char small[20];
    memset(small, '.', sizeof small);
    int n = snprintf(small, cap, "%05d-%x-%s", 42, 0xbeef, "tail");
    printf("cap %2zu -> %d [%s]\n", cap, n, small);
  }
  int n1 = snprintf(NULL, 0, "%lld", LLONG_MIN);
  int n2 = snprintf(NULL, 0, "%#llo", (unsigned long long)-1);
  int n3 = snprintf(NULL, 0, "%-300d|", 1);
  printf("measured: %d %d %d\n", n1, n2, n3);
  int c1, c2, c3;
  printf("count: abc%n%5d%n%s%n\n", &c1, 7, &c2, "xyz", &c3);
  printf("counts %d %d %d\n", c1, c2, c3);
  // hex dump of a buffer
  unsigned char data[40];
  for (int i = 0; i < 40; i++) data[i] = (unsigned char)(i * 37 + 11);
  for (int row = 0; row < 40; row += 16) {
    printf("%04x:", row);
    for (int j = row; j < row + 16 && j < 40; j++) printf(" %02x", data[j]);
    printf("\n");
  }
}

static void loops(void) {
  printf("-- many values\n");
  uint32_t x = 12345;
  for (int i = 0; i < 40; i++) {
    x = x * 1103515245u + 12345u;
    int32_t s = (int32_t)x;
    printf("%11d %10u %08x %11o %+6hd %4hhu %-12lld|\n", s, x, x, x, (short)s,
           (unsigned char)x, (long long)s * 1000003);
  }
}

int main(void) {
  plain();
  lengths();
  widths();
  chars_and_strings();
  building();
  loops();
  return 0;
}
