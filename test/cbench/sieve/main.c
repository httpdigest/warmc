// A bit-packed sieve of Eratosthenes: the inner loop clears one bit per
// multiple, a shift, a mask and a read-modify-write of a 64-bit word, with a
// stride that changes for every prime.
//
// The row measures the strided read-modify-write and the 64-bit shifts on a
// 32-bit address space, which is what bitsets, bloom filters and allocator
// bitmaps all do.
#include <stdio.h>
#include <stdint.h>
#include <string.h>

enum { N = 2000000 };
static uint64_t bits[N / 64 + 1];

int main(void) {
  uint64_t total = 0;
  for (int r = 0; r < 12; r++) {
    memset(bits, 0xff, sizeof bits);
    bits[0] &= ~3ull;
    for (uint32_t p = 2; (uint64_t)p * p < N; p++)
      if (bits[p >> 6] >> (p & 63) & 1)
        for (uint32_t m = p * p; m < N; m += p) bits[m >> 6] &= ~(1ull << (m & 63));
    uint32_t count = 0;
    for (uint32_t i = 0; i < N; i++) count += bits[i >> 6] >> (i & 63) & 1;
    total += count + (uint64_t)r;
  }
  printf("total = %llu\n", (unsigned long long)total);
  return 0;
}
