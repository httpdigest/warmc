// Recursion: plain (factorial, Fibonacci, Ackermann on small arguments),
// mutual (is_even/is_odd, a recursive-descent expression parser whose
// expr/term/factor call each other), deep but bounded (a 6,000-frame linked
// walk and a 1,500-deep recursion carrying a local array), tail-recursive
// accumulators, Hofstadter's nested female/male sequences, divide and
// conquer (merge sort, power by squaring), backtracking (N-queens,
// permutations, subset sums), memoised Catalan numbers and the Tower of
// Hanoi.  The depths stay inside wasmtime's default 512 KiB stack and the
// shadow stack clang reserves.
//
// Every frame here lives partly on wasm's own call stack and partly on
// clang's shadow stack in linear memory, for locals whose address is taken or
// that are too big for registers; a frame that is set up or torn down wrong
// corrupts its caller, which these computations check on the way back up.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t fact(unsigned n) { return n <= 1 ? 1 : n * fact(n - 1); }
static uint64_t fib(unsigned n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
static unsigned ack(unsigned m, unsigned n) {
  if (m == 0) return n + 1;
  if (n == 0) return ack(m - 1, 1);
  return ack(m - 1, ack(m, n - 1));
}

static int is_odd(unsigned n);
static int is_even(unsigned n) { return n == 0 ? 1 : is_odd(n - 1); }
static int is_odd(unsigned n) { return n == 0 ? 0 : is_even(n - 1); }

// Hofstadter's female and male sequences: mutual recursion with nesting.
static int hm(int n);
static int hf(int n) { return n == 0 ? 1 : n - hm(hf(n - 1)); }
static int hm(int n) { return n == 0 ? 0 : n - hf(hm(n - 1)); }

// Recursive-descent parser: expr := term (('+'|'-') term)*,
// term := unary (('*'|'/'|'%') unary)*, unary := '-' unary | power,
// power := atom ('^' unary)?, atom := number | '(' expr ')'
static const char *src;
static long long parse_expr(void);
static void skip(void) { while (*src == ' ') src++; }
static long long parse_unary(void);
static long long parse_atom(void) {
  skip();
  if (*src == '(') {
    src++;
    long long v = parse_expr();
    skip();
    if (*src == ')') src++;
    return v;
  }
  long long v = 0;
  while (*src >= '0' && *src <= '9') v = v * 10 + (*src++ - '0');
  return v;
}
static long long ipow(long long b, long long e) {
  if (e <= 0) return 1;
  long long h = ipow(b, e / 2);
  return e % 2 ? h * h * b : h * h;
}
static long long parse_power(void) {
  long long b = parse_atom();
  skip();
  if (*src == '^') {
    src++;
    return ipow(b, parse_unary());
  }
  return b;
}
static long long parse_unary(void) {
  skip();
  if (*src == '-') {
    src++;
    return -parse_unary();
  }
  return parse_power();
}
static long long parse_term(void) {
  long long v = parse_unary();
  for (;;) {
    skip();
    char op = *src;
    if (op != '*' && op != '/' && op != '%') return v;
    src++;
    long long r = parse_unary();
    if (op == '*') v *= r;
    else if (r == 0) v = 0;
    else if (op == '/') v /= r;
    else v %= r;
  }
}
static long long parse_expr(void) {
  long long v = parse_term();
  for (;;) {
    skip();
    char op = *src;
    if (op != '+' && op != '-') return v;
    src++;
    long long r = parse_term();
    v = op == '+' ? v + r : v - r;
  }
}

typedef struct Link { int v; struct Link *next; } Link;
static Link chain[6000];
static long long walk(const Link *l) { return l ? l->v + 2 * (walk(l->next) % 1000003) : 0; }

// Each frame carries an array whose address escapes, so it lives in the
// shadow stack, and is read back after the deeper frames have come and gone.
__attribute__((noinline)) static uint32_t mix8(const uint32_t *p, uint32_t h) {
  for (int i = 0; i < 8; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
static uint32_t deep_frames(int depth, uint32_t seed) {
  uint32_t local[8];
  for (int i = 0; i < 8; i++) local[i] = seed * (uint32_t)(i + 1) + (uint32_t)depth;
  uint32_t below = depth ? deep_frames(depth - 1, seed * 69069u + 1) : 0;
  return mix8(local, below);
}

static uint64_t sum_tail(uint64_t n, uint64_t acc) { return n == 0 ? acc : sum_tail(n - 1, acc + n * n); }
static unsigned gcd(unsigned a, unsigned b) { return b ? gcd(b, a % b) : a; }

static void merge_sort(int *a, int *tmp, int n) {
  if (n < 2) return;
  int h = n / 2;
  merge_sort(a, tmp, h);
  merge_sort(a + h, tmp, n - h);
  int i = 0, j = h, k = 0;
  while (i < h && j < n) tmp[k++] = a[i] <= a[j] ? a[i++] : a[j++];
  while (i < h) tmp[k++] = a[i++];
  while (j < n) tmp[k++] = a[j++];
  memcpy(a, tmp, (size_t)n * sizeof *a);
}

static int queens_count;
static int queens_first[12];
static int queens_have_first;
static void queens(int n, int row, int *cols, unsigned used, unsigned d1, unsigned d2) {
  if (row == n) {
    if (!queens_have_first) {
      memcpy(queens_first, cols, (size_t)n * sizeof *cols);
      queens_have_first = 1;
    }
    queens_count++;
    return;
  }
  for (int c = 0; c < n; c++) {
    unsigned a = 1u << c, b = 1u << (row + c), d = 1u << (row - c + n - 1);
    if ((used & a) || (d1 & b) || (d2 & d)) continue;
    cols[row] = c;
    queens(n, row + 1, cols, used | a, d1 | b, d2 | d);
  }
}

static int perm_count;
static void permute(char *s, int k, int n) {
  if (k == n) {
    if (perm_count < 30 || perm_count % 97 == 0) printf("%s%c", s, ' ');
    perm_count++;
    return;
  }
  for (int i = k; i < n; i++) {
    char t = s[k]; s[k] = s[i]; s[i] = t;
    permute(s, k + 1, n);
    t = s[k]; s[k] = s[i]; s[i] = t;
  }
}

static int subset_sums(const int *a, int n, int target) {
  if (target == 0) return 1;
  if (n == 0 || target < 0) return 0;
  return subset_sums(a + 1, n - 1, target - a[0]) + subset_sums(a + 1, n - 1, target);
}

static int hanoi_moves;
static uint32_t hanoi_hash;
static void hanoi(int n, int from, int to, int via) {
  if (n == 0) return;
  hanoi(n - 1, from, via, to);
  hanoi_moves++;
  hanoi_hash = (hanoi_hash * 31) ^ (uint32_t)(from * 3 + to + n * 7);
  if (hanoi_moves <= 15) printf("disk %d %c->%c\n", n, 'A' + from, 'A' + to);
  hanoi(n - 1, via, to, from);
}

// Count binary trees with n nodes (Catalan numbers) by recursion with memo.
static uint64_t memo[40];
static uint64_t catalan(int n) {
  if (n <= 1) return 1;
  if (memo[n]) return memo[n];
  uint64_t s = 0;
  for (int i = 0; i < n; i++) s += catalan(i) * catalan(n - 1 - i);
  return memo[n] = s;
}

int main(void) {
  printf("-- simple\n");
  for (unsigned n = 0; n <= 20; n++) printf("%u! = %llu\n", n, (unsigned long long)fact(n));
  for (unsigned n = 0; n <= 27; n += 3) printf("fib(%u) = %llu\n", n, (unsigned long long)fib(n));
  for (unsigned m = 0; m <= 3; m++)
    for (unsigned n = 0; n <= 4; n++) printf("ack(%u,%u)=%u\n", m, n, ack(m, n));
  printf("-- mutual\n");
  for (unsigned n = 0; n < 2000; n += 333) printf("even(%u)=%d odd(%u)=%d\n", n, is_even(n), n, is_odd(n));
  printf("F:");
  for (int n = 0; n < 25; n++) printf(" %d", hf(n));
  printf("\nM:");
  for (int n = 0; n < 25; n++) printf(" %d", hm(n));
  printf("\n");
  const char *exprs[] = {"1+2*3", "(1+2)*3", "-2^3", "2^3^2", "100/7%4",
                         "-(3-10)*-(2+2)", "((((5))))", "2^10-1000+3*(4-(5*6))",
                         "17%5*3-8/3", "1-2-3-4-5", "2*-3^2", "0/0+7"};
  for (unsigned i = 0; i < sizeof exprs / sizeof exprs[0]; i++) {
    src = exprs[i];
    printf("%s = %lld\n", exprs[i], parse_expr());
  }
  printf("-- deep\n");
  for (int i = 0; i < 6000; i++) {
    chain[i].v = i % 97;
    chain[i].next = i + 1 < 6000 ? &chain[i + 1] : NULL;
  }
  printf("walk=%lld\n", walk(chain));
  printf("frames=%u %u\n", deep_frames(1500, 1), deep_frames(100, 7));
  printf("sum_tail=%llu gcd=%u %u\n", (unsigned long long)sum_tail(10000, 0), gcd(1071, 462), gcd(832040, 514229));
  printf("-- divide and conquer\n");
  int a[100], tmp[100];
  uint32_t r = 99;
  for (int i = 0; i < 100; i++) {
    r = r * 1664525u + 1013904223u;
    a[i] = (int)(r >> 22) - 512;
  }
  merge_sort(a, tmp, 100);
  for (int i = 0; i < 100; i++) printf("%d%c", a[i], i % 20 == 19 ? '\n' : ' ');
  for (int e = 0; e <= 62; e += 7) printf("3^%d=%lld\n", e / 2, ipow(3, e / 2));
  printf("-- backtracking\n");
  for (int n = 1; n <= 9; n++) {
    int cols[12];
    queens_count = 0;
    queens_have_first = 0;
    queens(n, 0, cols, 0, 0, 0);
    printf("queens(%d)=%d", n, queens_count);
    if (queens_have_first) {
      printf(" first:");
      for (int i = 0; i < n; i++) printf(" %d", queens_first[i]);
    }
    printf("\n");
  }
  char s[] = "abcdef";
  permute(s, 0, 6);
  printf("\npermutations=%d final=%s\n", perm_count, s);
  int items[] = {3, 34, 4, 12, 5, 2, 7, 1, 9, 15, 8, 6};
  for (int t = 0; t <= 60; t += 6) printf("subsets summing to %d: %d\n", t, subset_sums(items, 12, t));
  hanoi(10, 0, 2, 1);
  printf("hanoi moves=%d hash=%u\n", hanoi_moves, hanoi_hash);
  for (int n = 0; n < 36; n += 5) printf("catalan(%d)=%llu\n", n, (unsigned long long)catalan(n));
  return 0;
}
