// snprintf of integers, hex and floating point into a buffer: the formatting
// half of every C program's I/O, and in wasi-libc one large varargs function
// whose float path is a multiprecision conversion.
//
// It is the one kernel here that spends its time in a big, branchy, cold-ish
// library routine rather than in a loop the program wrote, which is the kind of
// code a compiler sees most of in real modules and benchmarks least.
#include <stdio.h>
#include <stdint.h>

int main(void) {
  char out[128];
  uint64_t sum = 0;
  uint32_t x = 3;
  for (int i = 0; i < 300000; i++) {
    x = x * 1103515245u + 12345u;
    int n = snprintf(out, sizeof out, "%d:%x:%.3f:%s:%lld", (int)(x >> 3) - 1000000,
                     x, (double)(x % 100000) / 7.0, (i & 1) ? "odd" : "even",
                     (long long)x * 1000003);
    for (int j = 0; j < n; j++) sum = sum * 31 + (unsigned char)out[j];
  }
  printf("sum = %llu\n", (unsigned long long)sum);
  return 0;
}
