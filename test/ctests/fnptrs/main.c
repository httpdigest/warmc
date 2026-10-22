// Function pointers: dispatch tables indexed at run time, callbacks passed
// into higher-order helpers (map, fold, filter, compose), function pointers
// stored in structs as a hand-rolled vtable, functions returning function
// pointers, qsort with comparators on ints, doubles, strings and structs by
// several keys (including a stable-by-index tiebreak, since qsort is not
// stable), and bsearch for present and absent keys.
//
// Every one of these is a call_indirect in wasm: the pointer is an index into
// the module's function table and the call checks the callee's signature at
// run time.  Calls through pointers of several signatures are mixed so that
// each table slot is called with the type it was declared with.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*BinOp)(int, int);
static int op_add(int a, int b) { return a + b; }
static int op_sub(int a, int b) { return a - b; }
static int op_mul(int a, int b) { return a * b; }
static int op_div(int a, int b) { return b ? a / b : 0; }
static int op_mod(int a, int b) { return b ? a % b : 0; }
static int op_min(int a, int b) { return a < b ? a : b; }
static int op_max(int a, int b) { return a > b ? a : b; }
static int op_xor(int a, int b) { return a ^ b; }
static BinOp table[] = {op_add, op_sub, op_mul, op_div, op_mod, op_min, op_max, op_xor};
static const char *names[] = {"add", "sub", "mul", "div", "mod", "min", "max", "xor"};

static void dispatch(void) {
  printf("-- dispatch table\n");
  int xs[] = {17, -5, 0, 100, -33, 7};
  for (unsigned i = 0; i < 8; i++) {
    printf("%s:", names[i]);
    for (unsigned j = 0; j < 6; j++) printf(" %d", table[i](xs[j], xs[(j + 1) % 6]));
    printf("\n");
  }
  // a tiny RPN calculator whose operators come from the table
  const char *prog = "3 4 + 5 * 2 - 7 % 9 x 6 M 2 m 100 / 8 +";
  int stack[16], sp = 0;
  for (const char *p = prog; *p; p++) {
    if (*p == ' ') continue;
    if (*p >= '0' && *p <= '9') {
      int v = 0;
      while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
      p--;
      stack[sp++] = v;
      continue;
    }
    const char *ops = "+-*/%mMx";
    const char *f = strchr(ops, *p);
    int b = stack[--sp], a = stack[--sp];
    stack[sp++] = table[f - ops](a, b);
    printf("after %c: %d\n", *p, stack[sp - 1]);
  }
}

static void map(int *a, int n, int (*f)(int)) {
  for (int i = 0; i < n; i++) a[i] = f(a[i]);
}
static long long fold(const int *a, int n, long long init, long long (*f)(long long, int)) {
  for (int i = 0; i < n; i++) init = f(init, a[i]);
  return init;
}
static int filter(const int *a, int n, int *out, int (*pred)(int)) {
  int k = 0;
  for (int i = 0; i < n; i++)
    if (pred(a[i])) out[k++] = a[i];
  return k;
}
static int sq(int x) { return x * x; }
static int dec(int x) { return x - 3; }
static int neg(int x) { return -x; }
static int is_even(int x) { return (x & 1) == 0; }
static int is_pos(int x) { return x > 0; }
static long long sum(long long acc, int x) { return acc + x; }
static long long hornr(long long acc, int x) { return acc * 7 + x; }
static long long mx(long long acc, int x) { return x > acc ? x : acc; }

typedef int (*IntFn)(int);
static IntFn pick(int k) {
  switch (k % 3) {
    case 0: return sq;
    case 1: return dec;
    default: return neg;
  }
}

static void higher_order(void) {
  printf("-- callbacks\n");
  int a[12];
  for (int i = 0; i < 12; i++) a[i] = i - 4;
  for (int round = 0; round < 6; round++) {
    IntFn f = pick(round);
    map(a, 12, f);
    int out[12];
    int ne = filter(a, 12, out, is_even), np;
    printf("round %d:", round);
    for (int i = 0; i < 12; i++) printf(" %d", a[i]);
    printf(" | even=%d", ne);
    int out2[12];
    np = filter(a, 12, out2, is_pos);
    printf(" pos=%d sum=%lld horner=%lld max=%lld\n", np, fold(a, 12, 0, sum),
           fold(a, 12, 1, hornr), fold(a, 12, -1000000, mx));
    for (int i = 0; i < 12; i++) a[i] = a[i] % 1000;
  }
}

// A vtable of shape operations.
typedef struct Shape Shape;
typedef struct {
  const char *name;
  double (*area)(const Shape *);
  double (*perimeter)(const Shape *);
  void (*scale)(Shape *, double);
} ShapeOps;
struct Shape {
  const ShapeOps *ops;
  double a, b;
};
static double rect_area(const Shape *s) { return s->a * s->b; }
static double rect_per(const Shape *s) { return 2 * (s->a + s->b); }
static double circ_area(const Shape *s) { return 3.14159265358979 * s->a * s->a; }
static double circ_per(const Shape *s) { return 2 * 3.14159265358979 * s->a; }
static double tri_area(const Shape *s) { return 0.5 * s->a * s->b; }
static double tri_per(const Shape *s) {
  double h = s->a * s->a + s->b * s->b, r = h;
  for (int i = 0; i < 30; i++) r = 0.5 * (r + h / r);  // Newton sqrt
  return s->a + s->b + r;
}
static void scale_both(Shape *s, double k) { s->a *= k; s->b *= k; }
static void scale_first(Shape *s, double k) { s->a *= k; }
static const ShapeOps RECT = {"rect", rect_area, rect_per, scale_both};
static const ShapeOps CIRC = {"circle", circ_area, circ_per, scale_first};
static const ShapeOps TRI = {"triangle", tri_area, tri_per, scale_both};

static void vtables(void) {
  printf("-- vtables\n");
  Shape shapes[] = {{&RECT, 3, 4}, {&CIRC, 1.5, 0}, {&TRI, 3, 4},
                    {&RECT, 0.5, 10}, {&CIRC, 2, 0}, {&TRI, 5, 12}};
  for (int round = 0; round < 3; round++) {
    double total = 0;
    for (unsigned i = 0; i < sizeof shapes / sizeof shapes[0]; i++) {
      Shape *s = &shapes[i];
      double a = s->ops->area(s), p = s->ops->perimeter(s);
      total += a;
      printf("%-8s area=%.6f perim=%.6f\n", s->ops->name, a, p);
      s->ops->scale(s, 1.5);
    }
    printf("total %.6f\n", total);
  }
}

static int cmp_int(const void *a, const void *b) {
  int x = *(const int *)a, y = *(const int *)b;
  return (x > y) - (x < y);
}
static int cmp_int_desc(const void *a, const void *b) { return cmp_int(b, a); }
static int cmp_dbl(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b;
  return (x > y) - (x < y);
}
static int cmp_str(const void *a, const void *b) {
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}
static int cmp_len_then_alpha(const void *a, const void *b) {
  const char *x = *(const char *const *)a, *y = *(const char *const *)b;
  size_t lx = strlen(x), ly = strlen(y);
  if (lx != ly) return lx < ly ? -1 : 1;
  return strcmp(x, y);
}
typedef struct { char name[12]; int age; int idx; } Rec;
static int cmp_rec(const void *a, const void *b) {
  const Rec *x = a, *y = b;
  if (x->age != y->age) return x->age - y->age;
  return x->idx - y->idx;  // stability by original position
}
static int cmp_abs_mod(const void *a, const void *b) {
  int x = abs(*(const int *)a) % 10, y = abs(*(const int *)b) % 10;
  if (x != y) return x - y;
  return cmp_int(a, b);
}

static void sorting(void) {
  printf("-- qsort and bsearch\n");
  int a[40];
  uint32_t r = 7;
  for (int i = 0; i < 40; i++) {
    r = r * 1103515245u + 12345u;
    a[i] = (int)(r >> 16) % 2001 - 1000;
  }
  qsort(a, 40, sizeof a[0], cmp_int);
  for (int i = 0; i < 40; i++) printf("%d ", a[i]);
  printf("\n");
  int probes[] = {a[0], a[17], a[39], 5000, -5000, a[20] + 1};
  for (int i = 0; i < 6; i++) {
    int *hit = bsearch(&probes[i], a, 40, sizeof a[0], cmp_int);
    printf("bsearch %d -> %s", probes[i], hit ? "found" : "absent");
    if (hit) printf(" value %d", *hit);
    printf("\n");
  }
  qsort(a, 40, sizeof a[0], cmp_int_desc);
  for (int i = 0; i < 40; i += 4) printf("%d ", a[i]);
  printf("\n");
  qsort(a, 40, sizeof a[0], cmp_abs_mod);
  for (int i = 0; i < 40; i++) printf("%d ", a[i]);
  printf("\n");
  double d[15];
  for (int i = 0; i < 15; i++) d[i] = ((i * 7919) % 31 - 15) / 4.0;
  qsort(d, 15, sizeof d[0], cmp_dbl);
  for (int i = 0; i < 15; i++) printf("%g ", d[i]);
  printf("\n");
  const char *w[] = {"pear", "fig", "apple", "kiwi", "banana", "date", "plum",
                     "cherry", "lime", "grape", "melon", "nut"};
  qsort(w, 12, sizeof w[0], cmp_str);
  for (int i = 0; i < 12; i++) printf("%s ", w[i]);
  printf("\n");
  qsort(w, 12, sizeof w[0], cmp_len_then_alpha);
  for (int i = 0; i < 12; i++) printf("%s ", w[i]);
  printf("\n");
  const char *key = "lime";
  qsort(w, 12, sizeof w[0], cmp_str);
  const char **found = bsearch(&key, w, 12, sizeof w[0], cmp_str);
  printf("lime at %td\n", found ? found - w : -1);
  Rec recs[10] = {{"ann", 30, 0}, {"bob", 25, 0}, {"cy", 30, 0}, {"di", 22, 0},
                  {"ed", 25, 0}, {"flo", 41, 0}, {"gus", 22, 0}, {"hal", 30, 0},
                  {"ivy", 19, 0}, {"jo", 41, 0}};
  for (int i = 0; i < 10; i++) recs[i].idx = i;
  qsort(recs, 10, sizeof recs[0], cmp_rec);
  for (int i = 0; i < 10; i++) printf("%s/%d ", recs[i].name, recs[i].age);
  printf("\n");
}

typedef double (*Unary)(double);
static double twice(double x) { return 2 * x; }
static double plus1(double x) { return x + 1; }
static double half(double x) { return x / 2; }
static double apply_chain(const Unary *fs, int n, double x) {
  for (int i = 0; i < n; i++) x = fs[i](x);
  return x;
}
typedef struct { const char *name; void (*fn)(int *, int); } Command;
static void cmd_inc(int *acc, int v) { *acc += v; }
static void cmd_dbl(int *acc, int v) { (void)v; *acc *= 2; }
static void cmd_neg(int *acc, int v) { (void)v; *acc = -*acc; }
static void cmd_set(int *acc, int v) { *acc = v; }

static void chains(void) {
  printf("-- composed chains and command tables\n");
  Unary fs[] = {twice, plus1, half, twice, twice, plus1};
  for (int n = 0; n <= 6; n++) printf("chain %d: %g\n", n, apply_chain(fs, n, 1.25));
  Command cmds[] = {{"inc", cmd_inc}, {"dbl", cmd_dbl}, {"neg", cmd_neg}, {"set", cmd_set}};
  const char *script[] = {"set", "inc", "dbl", "dbl", "neg", "inc", "dbl", "set", "neg"};
  int args[] = {5, 3, 0, 0, 0, 100, 0, -9, 0};
  int acc = 0;
  for (int i = 0; i < 9; i++) {
    for (int c = 0; c < 4; c++)
      if (!strcmp(cmds[c].name, script[i])) cmds[c].fn(&acc, args[i]);
    printf("%s %d -> %d\n", script[i], args[i], acc);
  }
}

int main(void) {
  dispatch();
  higher_order();
  vtables();
  sorting();
  chains();
  return 0;
}
