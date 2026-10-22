// sscanf and the stdio formatting round trip: %d %i %u %o %x with length
// modifiers, %f %e %g %a into float and double, %s with a field width,
// scansets %[a-z] and %[^,], %c reading a fixed count without a terminator,
// %n reporting how far the scan got, assignment suppression with %*, literal
// matching and whitespace in the format, the return value on partial matches
// and on input failure (EOF), and records parsed from a CSV-like table; then
// fmemopen and open_memstream for reading and writing a stream in memory.
//
// musl's scanf shares its number parsers with strtol and strtod and drives
// them from a small state machine over the format; the variadic argument
// list is written through pointers of every width.  Each call prints its
// return value along with every variable it was meant to fill.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void integers(void) {
  printf("-- integer conversions\n");
  const char *in[] = {"42 -17 0x1f 077 101", "  +5   -0  0X10 0 7", "2147483647 -2147483648 ffffffff 777 -1",
                      "12abc", "", "   ", "x 1", "9 8"};
  for (unsigned i = 0; i < sizeof in / sizeof in[0]; i++) {
    int a = -999, b = -999, d = -999;
    unsigned c = 999;
    long long e = -999;
    int n = sscanf(in[i], "%d %i %x %o %lld", &a, &b, &c, &d, &e);
    printf("[%s] -> %d: %d %d %u %d %lld\n", in[i], n, a, b, c, d, e);
  }
  signed char hh = 0;
  short h = 0;
  long l = 0;
  unsigned long long ull = 0;
  size_t z = 0;
  intmax_t j = 0;
  int n = sscanf("-100 30000 -2000000000 18446744073709551615 4000000000 -9000000000000000000",
                 "%hhd %hd %ld %llu %zu %jd", &hh, &h, &l, &ull, &z, &j);
  printf("widths -> %d: %d %d %ld %llu %zu %jd\n", n, hh, h, l, ull, z, j);
  unsigned char uhh = 0;
  unsigned short uh = 0;
  n = sscanf("255 65535", "%hhu %hu", &uhh, &uh);
  printf("unsigned narrow -> %d: %u %u\n", n, uhh, uh);
  int x1, x2, x3;
  n = sscanf("0x7fFF 0Xabc -0x10", "%i %i %i", &x1, &x2, &x3);
  printf("%%i hex -> %d: %d %d %d\n", n, x1, x2, x3);
  n = sscanf("010 08 0", "%i %i %i", &x1, &x2, &x3);
  printf("%%i octal -> %d: %d %d %d\n", n, x1, x2, x3);
}

static void floats(void) {
  printf("-- floating conversions\n");
  const char *in[] = {"3.14159 -2.5e-3 1e10", "0x1.8p3 inf -nan", ".5 5. -0", "1e30 1e-300 7", "abc 1 2",
                      "6.02214076e23 1.602176634e-19 299792458"};
  for (unsigned i = 0; i < sizeof in / sizeof in[0]; i++) {
    float f = -1;
    double d = -1, g = -1;
    int n = sscanf(in[i], "%f %lf %lg", &f, &d, &g);
    printf("[%s] -> %d:", in[i], n);
    printf(" f=");
    if (f != f) printf("nan");
    else printf("%.9g", (double)f);
    printf(" d=");
    if (d != d) printf("nan");
    else printf("%.17g", d);
    printf(" g=");
    if (g != g) printf("nan");
    else printf("%.17g", g);
    printf("\n");
  }
  double a, b;
  int n = sscanf("1.5e+2xyz 2", "%le%*s %la", &a, &b);
  printf("suppressed tail -> %d: %g %g\n", n, a, b);
}

static void strings(void) {
  printf("-- strings, scansets, chars\n");
  char w1[16], w2[16], w3[16];
  int n = sscanf("hello world again", "%s %5s %15s", w1, w2, w3);
  printf("-> %d: [%s] [%s] [%s]\n", n, w1, w2, w3);
  n = sscanf("abcdefghij", "%3s%4s%s", w1, w2, w3);
  printf("widths -> %d: [%s] [%s] [%s]\n", n, w1, w2, w3);
  n = sscanf("key=value;rest", "%[a-z]=%[^;];%s", w1, w2, w3);
  printf("scanset -> %d: [%s] [%s] [%s]\n", n, w1, w2, w3);
  n = sscanf("ABC123def", "%[A-Z]%[0-9]%[a-z]", w1, w2, w3);
  printf("ranges -> %d: [%s] [%s] [%s]\n", n, w1, w2, w3);
  n = sscanf("]bracket-x", "%[]a-z]%[-]%s", w1, w2, w3);
  printf("special set members -> %d: [%s] [%s] [%s]\n", n, w1, w2, w3);
  char c3[4] = {0}, c1 = 0;
  n = sscanf("  xyz Q", "%3c %c", c3, &c1);
  printf("%%c -> %d: [%s] [%c]\n", n, c3, c1);
  n = sscanf("  xyz", "%c", &c1);
  printf("%%c keeps leading space -> %d: [%c]\n", n, c1);
  int pos1 = -1, pos2 = -1, v = 0;
  n = sscanf("   12345   tail", " %n%d%n", &pos1, &v, &pos2);
  printf("%%n -> %d: v=%d pos1=%d pos2=%d\n", n, v, pos1, pos2);
  int hh = 0, mm = 0, ss = 0;
  n = sscanf("12:34:56", "%d:%d:%d", &hh, &mm, &ss);
  printf("time -> %d: %02d-%02d-%02d\n", n, hh, mm, ss);
  n = sscanf("12:34", "%d:%d:%d", &hh, &mm, &ss);
  printf("partial time -> %d\n", n);
  n = sscanf("12-34", "%d:%d", &hh, &mm);
  printf("literal mismatch -> %d (hh=%d)\n", n, hh);
  n = sscanf("", "%d", &hh);
  printf("empty input -> %d (EOF is %d)\n", n, EOF);
  n = sscanf("100%", "%d%%", &hh);
  printf("percent literal -> %d: %d\n", n, hh);
}

typedef struct { char name[16]; int qty; double price; char code; } Item;

static void records(void) {
  printf("-- CSV-like records\n");
  const char *table = "widget,12,3.50,A\n"
                      "gadget,0,129.99,B\n"
                      "thingamajig,1000,0.01,C\n"
                      "broken,line\n"
                      "doohickey,-3,-2.5e1,D\n";
  const char *p = table;
  double total = 0;
  int good = 0, bad = 0;
  while (*p) {
    Item it;
    int used = 0;
    int n = sscanf(p, "%15[^,],%d,%lf,%c%n", it.name, &it.qty, &it.price, &it.code, &used);
    const char *nl = strchr(p, '\n');
    if (n == 4) {
      printf("%-12s qty=%5d price=%8.2f code=%c used=%d\n", it.name, it.qty, it.price, it.code, used);
      total += it.qty * it.price;
      good++;
    } else {
      printf("bad record (%d fields): %.*s\n", n, (int)(nl ? nl - p : (long)strlen(p)), p);
      bad++;
    }
    p = nl ? nl + 1 : p + strlen(p);
  }
  printf("good=%d bad=%d total=%.2f\n", good, bad, total);
}

static void memstreams(void) {
  printf("-- fmemopen and open_memstream\n");
  char text[] = "10 20 30\nforty 50\n60.5\n";
  FILE *f = fmemopen(text, strlen(text), "r");
  int a, b, c;
  int n = fscanf(f, "%d %d %d", &a, &b, &c);
  printf("fscanf -> %d: %d %d %d\n", n, a, b, c);
  char word[16];
  n = fscanf(f, "%15s %d", word, &a);
  printf("fscanf -> %d: %s %d\n", n, word, a);
  double d;
  n = fscanf(f, "%lf", &d);
  printf("fscanf -> %d: %g\n", n, d);
  n = fscanf(f, "%d", &a);
  printf("at end -> %d feof=%d\n", n, feof(f) != 0);
  rewind(f);
  char line[32];
  int lines = 0;
  while (fgets(line, sizeof line, f)) {
    line[strcspn(line, "\n")] = 0;
    printf("line %d: [%s] ftell=%ld\n", ++lines, line, ftell(f));
  }
  fclose(f);
  char *buf = NULL;
  size_t len = 0;
  FILE *w = open_memstream(&buf, &len);
  for (int i = 0; i < 12; i++) fprintf(w, "%d:%x;", i * i, i * 255);
  fputs("|end", w);
  fputc('!', w);
  fflush(w);
  printf("memstream len=%zu [%s]\n", len, buf);
  fprintf(w, "%s", "+more");
  fclose(w);
  printf("after close len=%zu [%s]\n", len, buf);
  free(buf);
}

int main(void) {
  integers();
  floats();
  strings();
  records();
  memstreams();
  return 0;
}
