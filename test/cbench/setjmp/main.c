// setjmp and longjmp as C programs use them for errors: a deep call chain
// whose innermost function sometimes gives up and jumps back to a handler.
//
// wasi-sdk implements the pair with the exception-handling proposal, so a
// longjmp is a throw and a setjmp a try_table -- this is the C corpus's
// exception kernel, the counterpart of ktbench/exn, through a different
// producer and a different unwinding library.
#include <setjmp.h>
#include <stdio.h>
#include <stdint.h>

static jmp_buf env;

static int64_t leaf(int64_t v) {
  if ((v & 63) == 0) longjmp(env, (int)(v & 0x7fff) + 1);
  return v * 3 + 1;
}

static int64_t mid(int64_t v) { return leaf(v + 1) ^ (v >> 2); }
static int64_t top(int64_t v) { return mid(v * 5) + v; }

int main(void) {
  int64_t sum = 0;
  int jumps = 0;
  for (int64_t i = 0; i < 10000000; i++) {
    int r = setjmp(env);
    if (r) {
      jumps++;
      sum += r;
      continue;
    }
    sum += top(i);
  }
  printf("sum = %lld jumps = %d\n", (long long)sum, jumps);
  return 0;
}
