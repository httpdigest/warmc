// Pointers and arrays: pointer arithmetic and differences, walking arrays
// forwards and backwards, pointers to pointers, arrays of pointers, row-major
// multi-dimensional arrays indexed directly and through pointers to rows,
// variable-length arrays (one- and two-dimensional, passed to functions with
// their sizes), flexible array members allocated to size, restrict-qualified
// copies, const and volatile access, char pointers over other objects' bytes,
// and the in-bounds one-past-the-end pointer used as a loop limit.
//
// In wasm every pointer is an i32 offset into linear memory and every access
// an i32 add folded into a load's offset immediate where the compiler can;
// VLAs move the shadow stack pointer by a runtime amount and must restore it
// on every exit.  Each section prints the elements it touched.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void arithmetic(void) {
  printf("-- pointer arithmetic\n");
  int a[20];
  for (int i = 0; i < 20; i++) a[i] = i * i - 7 * i;
  int *p = a, *end = a + 20;
  int sum = 0;
  while (p < end) sum += *p++;
  printf("sum=%d diff=%td\n", sum, end - a);
  for (ptrdiff_t k = 19; k >= 1; k -= 3) {  // never forms a pointer before a
    int *q = a + k;
    printf("%td:%d ", q - a, *q);
  }
  printf("\n");
  int *mid = &a[10];
  printf("mid[-3]=%d mid[4]=%d *(mid-10)=%d 3[a]=%d\n", mid[-3], mid[4],
         *(mid - 10), 3[a]);
  // pointers compared and subtracted across a 2D array of structs
  struct { short s; char c; } arr[8];
  for (int i = 0; i < 8; i++) {
    arr[i].s = (short)(i * 1000 - 3000);
    arr[i].c = (char)('a' + i);
  }
  char *bytes = (char *)arr;
  printf("struct stride=%td, c of arr[5] via bytes=%c\n",
         (char *)&arr[1] - (char *)&arr[0], bytes[5 * sizeof arr[0] + offsetof(__typeof__(arr[0]), c)]);
  // walk an int array as unsigned char to read its little-endian bytes
  uint32_t w[3] = {0x01020304u, 0xa0b0c0d0u, 0xdeadbeefu};
  const unsigned char *b = (const unsigned char *)w;
  for (size_t i = 0; i < sizeof w; i++) printf("%02x", b[i]);
  printf("\n");
}

static void pointer_to_pointer(void) {
  printf("-- pointers to pointers\n");
  const char *words[] = {"delta", "alpha", "echo", "charlie", "bravo", "foxtrot"};
  const char **pp = words;
  const char **last = words + 5;
  // bubble sort through pointer-to-pointer
  for (const char **i = pp; i < last; i++)
    for (const char **j = last; j > i; j--)
      if (strcmp(*(j - 1), *j) > 0) {
        const char *t = *j;
        *j = *(j - 1);
        *(j - 1) = t;
      }
  for (int i = 0; i < 6; i++) printf("%s%c", words[i], i < 5 ? ' ' : '\n');
  int x = 1, y = 2, z = 3;
  int *ptrs[3] = {&x, &y, &z};
  int **ppi = ptrs;
  int ***pppi = &ppi;
  **ppi += 10;
  *(*pppi)[1] *= 7;
  ***pppi -= 1;
  *ptrs[2] = **ppi + *ptrs[1];
  printf("x=%d y=%d z=%d\n", x, y, z);
  // swap via pointer to pointer
  int *pa = &x, *pb = &z;
  int **sa = &pa, **sb = &pb;
  int *t = *sa;
  *sa = *sb;
  *sb = t;
  printf("*pa=%d *pb=%d\n", *pa, *pb);
}

static void multidim(void) {
  printf("-- multi-dimensional arrays\n");
  int m[5][7];
  for (int i = 0; i < 5; i++)
    for (int j = 0; j < 7; j++) m[i][j] = i * 10 + j;
  int (*row)[7] = m;
  for (int i = 0; i < 5; i++, row++)
    printf("row %d: %d %d %d sum=%d\n", i, (*row)[0], (*row)[3], (*row)[6],
           (*row)[0] + (*row)[1] + (*row)[2] + (*row)[3] + (*row)[4] + (*row)[5] + (*row)[6]);
  int flat[35];
  memcpy(flat, m, sizeof m);
  printf("flat[17]=%d flat[34]=%d\n", flat[17], flat[34]);
  // transpose into a 7x5
  int t[7][5];
  for (int i = 0; i < 5; i++)
    for (int j = 0; j < 7; j++) t[j][i] = m[i][j];
  for (int j = 0; j < 7; j++) {
    for (int i = 0; i < 5; i++) printf("%3d", t[j][i]);
    printf("\n");
  }
  char grid[4][3][5];
  memset(grid, '.', sizeof grid);
  for (int a = 0; a < 4; a++)
    for (int b = 0; b < 3; b++) grid[a][b][(a + b) % 5] = (char)('A' + a * 3 + b);
  for (int a = 0; a < 4; a++) {
    for (int b = 0; b < 3; b++) printf("%.5s ", grid[a][b]);
    printf("\n");
  }
  printf("sizeof grid=%zu grid[1]=%zu grid[1][2]=%zu\n", sizeof grid,
         sizeof grid[1], sizeof grid[1][2]);
}

static long trace(int n, int mat[n][n]) {
  long s = 0;
  for (int i = 0; i < n; i++) s += mat[i][i];
  return s;
}
static void fill(int rows, int cols, double a[rows][cols]) {
  for (int i = 0; i < rows; i++)
    for (int j = 0; j < cols; j++) a[i][j] = (i + 1) * 0.5 + (j - 2) * 0.25;
}

static void vlas(void) {
  printf("-- variable-length arrays\n");
  for (int n = 1; n <= 9; n += 2) {
    int buf[n];
    for (int i = 0; i < n; i++) buf[i] = n * 100 + i;
    int mat[n][n];
    for (int i = 0; i < n; i++)
      for (int j = 0; j < n; j++) mat[i][j] = (i + 1) * (j + 2);
    printf("n=%d sizeof buf=%zu sizeof mat=%zu last=%d trace=%ld\n", n,
           sizeof buf, sizeof mat, buf[n - 1], trace(n, mat));
  }
  for (int r = 1; r <= 4; r++) {
    int c = 6 - r;
    double a[r][c];
    fill(r, c, a);
    printf("%dx%d:", r, c);
    for (int i = 0; i < r; i++)
      for (int j = 0; j < c; j++) printf(" %g", a[i][j]);
    printf("\n");
  }
  // VLA in a loop body: stack must be released each iteration
  long total = 0;
  for (int k = 0; k < 2000; k++) {
    int len = 100 + (k % 37) * 13;
    char tmp[len];
    memset(tmp, k & 0x7f, (size_t)len);
    total += tmp[len - 1] + tmp[len / 2];
  }
  printf("vla loop total=%ld\n", total);
}

typedef struct {
  size_t len;
  int tag;
  double data[];
} Series;

static Series *series_new(size_t n, int tag) {
  Series *s = malloc(sizeof(Series) + n * sizeof(double));
  s->len = n;
  s->tag = tag;
  for (size_t i = 0; i < n; i++) s->data[i] = (double)i / (tag + 1);
  return s;
}

typedef struct {
  unsigned short n;
  char text[];
} Str;

static void flexible(void) {
  printf("-- flexible array members\n");
  printf("sizeof Series=%zu offsetof data=%zu\n", sizeof(Series),
         offsetof(Series, data));
  for (int t = 0; t < 4; t++) {
    Series *s = series_new((size_t)(3 + t * 4), t);
    double sum = 0;
    for (size_t i = 0; i < s->len; i++) sum += s->data[i];
    printf("tag %d len %zu sum %.6f last %.6f\n", s->tag, s->len, sum,
           s->data[s->len - 1]);
    free(s);
  }
  const char *msgs[] = {"hi", "flexible", "array members at the end"};
  for (int i = 0; i < 3; i++) {
    size_t n = strlen(msgs[i]);
    Str *s = malloc(sizeof(Str) + n + 1);
    s->n = (unsigned short)n;
    memcpy(s->text, msgs[i], n + 1);
    for (size_t j = 0; j < n; j++)
      if (s->text[j] >= 'a' && s->text[j] <= 'z') s->text[j] = (char)(s->text[j] - 32);
    printf("%u:%s\n", s->n, s->text);
    free(s);
  }
}

static void copy_restrict(int *restrict dst, const int *restrict src, int n) {
  for (int i = 0; i < n; i++) dst[i] = src[i] * 2 + 1;
}

static void qualifiers(void) {
  printf("-- restrict, const, volatile\n");
  int src[16], dst[16];
  for (int i = 0; i < 16; i++) src[i] = i - 8;
  copy_restrict(dst, src, 16);
  for (int i = 0; i < 16; i++) printf("%d ", dst[i]);
  printf("\n");
  volatile int v = 0;
  for (int i = 0; i < 100; i++) v += i;
  printf("volatile sum=%d\n", v);
  const int table[] = {5, 3, 8, 1};
  const int *cp = table;
  int best = *cp;
  for (const int *q = cp + 1; q != table + 4; q++)
    if (*q > best) best = *q;
  printf("max=%d\n", best);
  // aliasing through a char pointer is allowed
  double d = 1.0;
  unsigned char *dc = (unsigned char *)&d;
  dc[7] = 0x40;
  printf("d after byte write=%g\n", d);
  // intptr round trip and alignment arithmetic (values printed relative)
  char block[64];
  char *aligned = (char *)(((uintptr_t)block + 15) & ~(uintptr_t)15);
  printf("aligned offset in [0,15]: %d, mod 16 = %u\n",
         (int)(aligned - block) <= 15, (unsigned)((uintptr_t)aligned % 16));
  printf("null compare: %d %d\n", (char *)NULL == 0, block != NULL);
}

int main(void) {
  arithmetic();
  pointer_to_pointer();
  multidim();
  vlas();
  flexible();
  qualifiers();
  return 0;
}
