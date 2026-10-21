// libc's qsort on an array of records, so every comparison is an indirect
// call from library code back into the program: the shape any C program with
// a callback API has.
//
// rsbench/sortints sorts with the comparison inlined into a monomorphic sort;
// this is the opposite, a generic sort compiled once that reaches its key
// through a function pointer, so the row is bounded by call_indirect and by
// the memmove-style element swaps qsort does on opaque bytes.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct {
  uint32_t key;
  uint32_t tie;
  double weight;
} Rec;

static int by_key(const void *a, const void *b) {
  const Rec *x = a, *y = b;
  if (x->key != y->key) return x->key < y->key ? -1 : 1;
  return (x->tie > y->tie) - (x->tie < y->tie);
}

int main(void) {
  enum { N = 20000 };
  Rec *v = malloc(N * sizeof *v);
  uint64_t sum = 0;
  uint32_t x = 12345;
  for (int r = 0; r < 25; r++) {
    for (int i = 0; i < N; i++) {
      x = x * 1103515245u + 12345u;
      v[i].key = (x >> 8) % 5000;
      v[i].tie = (uint32_t)i;
      v[i].weight = (double)(x & 1023);
    }
    qsort(v, N, sizeof *v, by_key);
    for (int i = 0; i < N; i += 97) sum += v[i].key * 31u + v[i].tie;
  }
  printf("sum = %llu\n", (unsigned long long)sum);
  free(v);
  return 0;
}
