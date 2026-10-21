// malloc and free of mixed sizes, with a window of live blocks so the
// allocator's free lists are always partly full: wasi-libc's dlmalloc, which
// is the allocator nearly every C and C++ program compiled to wasm carries.
//
// asbench/alloc measures AssemblyScript's TLSF and gobench/alloc TinyGo's
// collector; this is the third linear-memory allocator, the one whose fast
// path is a bin index, a list unlink and a header write.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

int main(void) {
  enum { WINDOW = 1024 };
  void *live[WINDOW] = {0};
  uint32_t x = 99, sum = 0;
  for (int i = 0; i < 3000000; i++) {
    x = x * 1103515245u + 12345u;
    int slot = (x >> 7) % WINDOW;
    if (live[slot]) {
      sum += ((unsigned char *)live[slot])[0];
      free(live[slot]);
    }
    size_t n = 8 + ((x >> 17) % ((x & 0x10000) ? 2048 : 96));
    unsigned char *p = malloc(n);
    p[0] = (unsigned char)i;
    p[n - 1] = (unsigned char)(i >> 8);
    live[slot] = p;
  }
  for (int i = 0; i < WINDOW; i++) free(live[i]);
  printf("sum = %u\n", sum);
  return 0;
}
