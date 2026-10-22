// Static storage: globals with and without initialisers (the zero-filled
// ones live in .bss and the rest in the data segment wasm-ld writes into
// linear memory), const lookup tables of every element type, tables of
// pointers to strings and to other globals (relocated addresses resolved at
// link time), arrays of structs with nested designated initialisers, static
// locals that keep their value across calls, function-scope counters,
// generators driven by static state, and a const string table indexed by an
// enum.
//
// All of this is the module's data section: a wrong offset or a segment
// applied in the wrong place shows up as a wrong table entry.  Mutations
// through non-const globals are read back through other functions so the
// compiler cannot keep them in registers.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int zero_int;
long long zero_ll;
double zero_dbl;
char zero_buf[64];
int counter = 100;
double ratio = 0.375;
const char *greeting = "hello, statics";
char mutable_text[] = "mutable text";
static const uint8_t crc_seed[8] = {0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15};
static const int16_t sine_q15[16] = {0, 6393, 12539, 18204, 23170, 27245, 30273, 32137,
                                     32767, 32137, 30273, 27245, 23170, 18204, 12539, 6393};
static const uint64_t powers10[] = {1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull, 1000000ull,
                                    10000000ull, 100000000ull, 1000000000ull, 10000000000ull,
                                    100000000000000000ull, 10000000000000000000ull};
static const float ftab[] = {0.5f, -1.25f, 3.0e10f, 1e-20f};
static const double dtab[] = {1.0 / 3.0, 2.718281828459045, -0.0, 1e300};

typedef enum { RED, GREEN, BLUE, CYAN = 10, MAGENTA, YELLOW = -3, BLACK } Color;
static const char *const color_names[] = {
  [RED] = "red", [GREEN] = "green", [BLUE] = "blue", [CYAN] = "cyan", [MAGENTA] = "magenta",
};

typedef struct { const char *name; int dims[3]; struct { float lo, hi; } range; const int *ref; } Spec;
int shared_a = 11, shared_b = 22;
Spec specs[] = {
  {"first", {1, 2, 3}, {0.5f, 1.5f}, &shared_a},
  {.name = "second", .dims = {[2] = 9}, .range.hi = 7.0f, .ref = &shared_b},
  {.ref = &counter, .name = "third", .range = {-1, 1}},
  [5] = {"sixth", {6, 6, 6}, {6, 6}, &shared_a},
};
const int nspecs = sizeof specs / sizeof specs[0];

int *const ptr_table[] = {&shared_a, &shared_b, &counter, &zero_int};
const char *const *const name_ptr = &color_names[1];
static char matrix[3][4] = {{'a', 'b'}, {'c'}, {'d', 'e', 'f', 'g'}};
static int partial[10] = {1, 2, 3};
static struct { int a; char b[3]; } nested_init[] = {{1, "xy"}, {2, {'p', 'q', 'r'}}, {.b = "z"}};

static int call_count(void) {
  static int calls;
  return ++calls;
}
static uint32_t lcg(void) {
  static uint32_t state = 2463534242u;
  state = state * 1664525u + 1013904223u;
  return state;
}
static const char *fizz(int reset) {
  static int n;
  static char buf[16];
  if (reset) n = 0;
  n++;
  if (n % 15 == 0) return "FizzBuzz";
  if (n % 3 == 0) return "Fizz";
  if (n % 5 == 0) return "Buzz";
  snprintf(buf, sizeof buf, "%d", n);
  return buf;
}
static int fib_next(void) {
  static int a = 0, b = 1;
  int r = a;
  int t = a + b;
  a = b;
  b = t;
  return r;
}

__attribute__((noinline)) static void mutate(void) {
  zero_int += 5;
  zero_ll -= 1;
  zero_dbl += 0.25;
  strcpy(zero_buf, "now set");
  counter *= 3;
  ratio /= 2;
  mutable_text[0] = 'M';
  shared_a++;
  specs[1].dims[0] = -4;
}

int main(void) {
  printf("-- zero-initialised\n");
  printf("%d %lld %g [%s] %d\n", zero_int, zero_ll, zero_dbl, zero_buf, zero_buf[63]);
  printf("-- initialised scalars\n");
  printf("%d %g %s %s\n", counter, ratio, greeting, mutable_text);
  mutate();
  printf("after mutate: %d %lld %g [%s] %d %g %s\n", zero_int, zero_ll, zero_dbl, zero_buf, counter, ratio, mutable_text);
  mutate();
  printf("again: %d %lld %g %d %g %d\n", zero_int, zero_ll, zero_dbl, counter, ratio, shared_a);
  printf("-- const tables\n");
  for (int i = 0; i < 8; i++) printf("%02x ", crc_seed[i]);
  printf("\n");
  int sum = 0;
  for (int i = 0; i < 16; i++) {
    printf("%d ", sine_q15[i]);
    sum += sine_q15[i];
  }
  printf("sum=%d\n", sum);
  for (unsigned i = 0; i < sizeof powers10 / sizeof powers10[0]; i++) printf("%llu ", (unsigned long long)powers10[i]);
  printf("\n%a %a %a %a | %a %a %a %a\n", (double)ftab[0], (double)ftab[1], (double)ftab[2], (double)ftab[3],
         dtab[0], dtab[1], dtab[2], dtab[3]);
  printf("-- enum-indexed names\n");
  printf("enum values: %d %d %d %d %d %d %d\n", RED, GREEN, BLUE, CYAN, MAGENTA, YELLOW, BLACK);
  for (unsigned i = 0; i < sizeof color_names / sizeof color_names[0]; i++)
    printf("%u:%s ", i, color_names[i] ? color_names[i] : "(null)");
  printf("\nname_ptr -> %s, next %s\n", *name_ptr, name_ptr[1]);
  printf("-- designated initialisers\n");
  printf("nspecs=%d\n", nspecs);
  for (int i = 0; i < nspecs; i++) {
    Spec *s = &specs[i];
    printf("%d: %s dims=%d,%d,%d range=%g..%g ref=%d\n", i, s->name ? s->name : "(null)", s->dims[0],
           s->dims[1], s->dims[2], (double)s->range.lo, (double)s->range.hi, s->ref ? *s->ref : -1);
  }
  printf("-- pointer tables\n");
  for (int i = 0; i < 4; i++) printf("%d ", *ptr_table[i]);
  *ptr_table[3] = 77;
  printf("| zero_int now %d\n", zero_int);
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 4; c++) printf("%c", matrix[r][c] ? matrix[r][c] : '.');
    printf(" ");
  }
  printf("\n");
  for (int i = 0; i < 10; i++) printf("%d", partial[i]);
  printf("\n");
  for (int i = 0; i < 3; i++)
    printf("{%d,%c%c%c} ", nested_init[i].a, nested_init[i].b[0] ? nested_init[i].b[0] : '_',
           nested_init[i].b[1] ? nested_init[i].b[1] : '_', nested_init[i].b[2] ? nested_init[i].b[2] : '_');
  printf("\n-- static locals\n");
  for (int i = 0; i < 5; i++) printf("call %d ", call_count());
  printf("\n");
  for (int i = 0; i < 8; i++) printf("%u ", lcg());
  printf("\n");
  for (int i = 0; i < 20; i++) printf("%s ", fizz(i == 0));
  printf("\n");
  for (int i = 0; i < 20; i++) printf("%s ", fizz(i == 10));
  printf("\n");
  for (int i = 0; i < 30; i++) printf("%d ", fib_next());
  printf("\n");
  return 0;
}
