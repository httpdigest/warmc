// C11 _Generic selection and C99 compound literals and designated
// initialisers: a type-name macro over every arithmetic type (including the
// ones that differ only in signedness, and char being distinct from both
// signed and unsigned char), type-generic max/abs/print macros, _Generic
// seeing through promotions only where C says it does, compound literals as
// function arguments, as lvalues, at file scope and in loops (a fresh object
// per evaluation in block scope), arrays sized by their designators, nested
// designators into structs and arrays, out-of-order and overriding
// designators, _Static_assert, _Alignas/_Alignof and anonymous struct/union
// members.
//
// Most of this is resolved at compile time, so what reaches the code
// generator is the data it produces: initialised stack objects built from
// many small stores, and memcpy'd compound literals.
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TYPE_NAME(x) _Generic((x),                                           \
  _Bool: "_Bool", char: "char", signed char: "signed char",                  \
  unsigned char: "unsigned char", short: "short",                            \
  unsigned short: "unsigned short", int: "int", unsigned: "unsigned",        \
  long: "long", unsigned long: "unsigned long", long long: "long long",       \
  unsigned long long: "unsigned long long", float: "float",                  \
  double: "double", long double: "long double", char *: "char *",            \
  const char *: "const char *", int *: "int *", void *: "void *",            \
  default: "other")

#define PRINT(x) _Generic((x),                                               \
  int: print_int, long long: print_ll, double: print_dbl, float: print_flt,  \
  char *: print_str, const char *: print_str, unsigned: print_uint,          \
  default: print_other)(x)

static void print_int(int v) { printf("int:%d ", v); }
static void print_uint(unsigned v) { printf("uint:%u ", v); }
static void print_ll(long long v) { printf("ll:%lld ", v); }
static void print_dbl(double v) { printf("dbl:%g ", v); }
static void print_flt(float v) { printf("flt:%g ", (double)v); }
static void print_str(const char *v) { printf("str:%s ", v); }
static void print_other(long double v) { (void)v; printf("other "); }

#define GMAX(a, b) _Generic((a) + (b), int: imax, long long: llmax, double: dmax, default: dmax)(a, b)
static int imax(int a, int b) { return a > b ? a : b; }
static long long llmax(long long a, long long b) { return a > b ? a : b; }
static double dmax(double a, double b) { return a > b ? a : b; }

static void generic(void) {
  printf("-- _Generic type names\n");
  char c = 'a';
  signed char sc = -1;
  unsigned char uc = 1;
  short s = 2;
  unsigned short us = 3;
  _Bool b = 1;
  float f = 1.5f;
  long double ld = 2.0L;
  int arr[3] = {0};
  const char *cs = "x";
  printf("%s | %s | %s | %s | %s | %s\n", TYPE_NAME(c), TYPE_NAME(sc), TYPE_NAME(uc), TYPE_NAME(s),
         TYPE_NAME(us), TYPE_NAME(b));
  printf("promoted: %s %s %s %s %s\n", TYPE_NAME(c + 0), TYPE_NAME(+uc), TYPE_NAME(s * s),
         TYPE_NAME(us + 1u), TYPE_NAME(b + b));
  printf("%s %s %s %s %s\n", TYPE_NAME(1), TYPE_NAME(1u), TYPE_NAME(1l), TYPE_NAME(1ul), TYPE_NAME(1ll));
  printf("%s %s %s %s\n", TYPE_NAME(1ull), TYPE_NAME(f), TYPE_NAME(1.0), TYPE_NAME(ld));
  printf("%s %s %s %s\n", TYPE_NAME(f * 2), TYPE_NAME(f * 2.0), TYPE_NAME(1 + 1ll), TYPE_NAME(1u + 1));
  printf("%s %s %s %s\n", TYPE_NAME(arr), TYPE_NAME(cs), TYPE_NAME((void *)0), TYPE_NAME("lit"));
  printf("%s %s %s %s\n", TYPE_NAME('a'), TYPE_NAME(sizeof(int)), TYPE_NAME((ptrdiff_t)0), TYPE_NAME(c ? 1u : 2));
  printf("%s %s %s\n", TYPE_NAME((int8_t)0), TYPE_NAME((uint16_t)0), TYPE_NAME((int64_t)0));
  PRINT(42);
  PRINT(42u);
  PRINT(42ll);
  PRINT(4.5);
  PRINT(4.5f);
  PRINT("text");
  PRINT(cs);
  PRINT(ld);
  PRINT(c);  // char goes to default
  printf("\n");
  printf("max: %d %lld %g %g\n", GMAX(3, 9), GMAX(3ll, -9ll), GMAX(2.5, 1), GMAX(1.0f, 0.5f));
}

typedef struct { int x, y; } Pt;
typedef struct { Pt a, b; const char *label; int tags[4]; } Seg;
static int area(const Pt *p, int n) {
  int s = 0;
  for (int i = 0; i < n; i++) {
    const Pt *q = &p[(i + 1) % n];
    s += p[i].x * q->y - q->x * p[i].y;
  }
  return s;
}
static Pt mid(Pt a, Pt b) { return (Pt){(a.x + b.x) / 2, (a.y + b.y) / 2}; }

static Pt *file_scope_lit = (Pt[]){{1, 1}, {2, 2}, {3, 3}};

static void literals(void) {
  printf("-- compound literals\n");
  printf("square area=%d\n", area((Pt[]){{0, 0}, {4, 0}, {4, 4}, {0, 4}}, 4));
  printf("triangle area=%d\n", area((Pt[]){{0, 0}, {10, 0}, {0, 7}}, 3));
  Pt m = mid((Pt){-4, 10}, (Pt){.y = 2, .x = 8});
  printf("mid=(%d,%d)\n", m.x, m.y);
  int *p = (int[]){5, 4, 3, 2, 1};
  p[2] = 99;
  for (int i = 0; i < 5; i++) printf("%d ", p[i]);
  printf("\n");
  (void)(Pt){1, 2}.x;  // an lvalue, unused
  Pt *lp = &(Pt){7, 8};
  lp->y *= 10;
  printf("through pointer: %d %d\n", lp->x, lp->y);
  for (int i = 0; i < 3; i++) file_scope_lit[i].x += i * 100;
  for (int i = 0; i < 3; i++) printf("fs[%d]=(%d,%d) ", i, file_scope_lit[i].x, file_scope_lit[i].y);
  printf("\n");
  // a block-scope compound literal is a new object each time it is evaluated
  int sum = 0;
  for (int i = 0; i < 5; i++) {
    int *q = (int[3]){i, i * i, i * i * i};
    sum += q[0] + q[1] + q[2];
  }
  printf("loop literal sum=%d\n", sum);
  const char *names = (const char[]){'a', 'b', 'c', 0};
  printf("char literal: %s, string from const array literal: %zu\n", names, strlen(names));
  memcpy(&m, &(Pt){-1, -2}, sizeof m);
  printf("memcpy from literal: %d %d\n", m.x, m.y);
}

static void designated(void) {
  printf("-- designated initialisers\n");
  int sparse[] = {[3] = 30, [7] = 70, [1] = 10, [7] = 77, 78, 79};
  printf("sizeof sparse=%zu:", sizeof sparse / sizeof sparse[0]);
  for (size_t i = 0; i < sizeof sparse / sizeof sparse[0]; i++) printf(" %d", sparse[i]);
  printf("\n");
  Seg s = {.b = {.y = 5}, .label = "seg", .a.x = 3, .tags = {[2] = 2, [0] = 9}, .b.x = -1};
  printf("seg a=(%d,%d) b=(%d,%d) %s tags=%d,%d,%d,%d\n", s.a.x, s.a.y, s.b.x, s.b.y, s.label, s.tags[0],
         s.tags[1], s.tags[2], s.tags[3]);
  Seg arr[] = {[1].a = {1, 1}, [1].tags[3] = 4, [2] = {.label = "two"}, [0].label = "zero"};
  for (int i = 0; i < 3; i++)
    printf("arr[%d] a=(%d,%d) label=%s tag3=%d\n", i, arr[i].a.x, arr[i].a.y, arr[i].label ? arr[i].label : "-",
           arr[i].tags[3]);
  int grid[3][4] = {[1] = {[2] = 5}, [2][3] = 9, [0][0] = 1};
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 4; c++) printf("%d", grid[r][c]);
    printf(" ");
  }
  printf("\n");
  char msg[10] = {[0] = 'h', [1] = 'i', [5] = '!'};
  for (int i = 0; i < 10; i++) printf("%c", msg[i] ? msg[i] : '_');
  printf("\n");
  struct { int a; union { int i; float f; } u; char c; } st = {.u.f = 1.5f, .c = 'q', .a = -3};
  printf("st %d %g %c\n", st.a, (double)st.u.f, st.c);
}

struct Anon {
  int kind;
  union {
    struct { int w, h; };
    struct { double radius; };
  };
};

static void misc(void) {
  printf("-- static_assert, alignment, anonymous members\n");
  _Static_assert(sizeof(int) == 4, "int is 32-bit");
  _Static_assert(sizeof(void *) == 4, "wasm32 pointers");
  _Static_assert(_Alignof(double) == 8, "double alignment");
  alignas(32) char buf[40];
  alignas(double) unsigned char raw[sizeof(double)];
  printf("alignof: char=%zu short=%zu int=%zu ll=%zu double=%zu ldouble=%zu max_align=%zu\n",
         alignof(char), alignof(short), alignof(int), alignof(long long), alignof(double),
         alignof(long double), alignof(max_align_t));
  printf("buf aligned 32: %d raw aligned 8: %d\n", (int)((uintptr_t)buf % 32 == 0), (int)((uintptr_t)raw % 8 == 0));
  struct Anon shapes[3] = {{.kind = 0, .w = 3, .h = 4}, {.kind = 1, .radius = 2.5}, {0, {{7, 1}}}};
  for (int i = 0; i < 3; i++) {
    if (shapes[i].kind == 0) printf("rect %dx%d area %d\n", shapes[i].w, shapes[i].h, shapes[i].w * shapes[i].h);
    else printf("circle r=%g\n", shapes[i].radius);
  }
  printf("sizeof Anon=%zu offsetof w=%zu h=%zu radius=%zu\n", sizeof(struct Anon), offsetof(struct Anon, w),
         offsetof(struct Anon, h), offsetof(struct Anon, radius));
}

int main(void) {
  generic();
  literals();
  designated();
  misc();
  return 0;
}
