// unsigned __int128 for 64-bit modular multiplication: a deterministic
// Miller-Rabin test over random odd numbers of every width from 21 to 64 bits,
// with the mulmod at its centre written as (u128)a * b % m, the way
// number-theory, hashing and cryptography code in C does it.
//
// wasm32 has no 128-bit integers and no high-half multiply, so the product is
// a call to compiler-rt's __multi3, four i64.mul on 32-bit halves, and the
// remainder a call to __umodti3, whose 64-bit-divisor path normalises with
// i64.clz and divides 32-bit digits with i64.div_u.  Both return their 128-bit
// result through a slot in the caller's shadow-stack frame.  So one C
// expression is two calls, a round trip through memory and a long division, a
// cost the source does not show; the trial division in front of it is plain
// i64.rem_u.
#include <stdio.h>
#include <stdint.h>

typedef unsigned __int128 u128;

static uint64_t mulmod(uint64_t a, uint64_t b, uint64_t m) { return (uint64_t)((u128)a * b % m); }

static uint64_t powmod(uint64_t a, uint64_t e, uint64_t m) {
  uint64_t r = 1;
  a %= m;
  while (e) {
    if (e & 1) r = mulmod(r, a, m);
    a = mulmod(a, a, m);
    e >>= 1;
  }
  return r;
}

// Deterministic Miller-Rabin for every 64-bit n: these twelve bases are enough.
static int is_prime(uint64_t n) {
  static const uint64_t small[12] = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37 };
  if (n < 2) return 0;
  for (int i = 0; i < 12; i++) {
    if (n == small[i]) return 1;
    if (n % small[i] == 0) return 0;
  }
  uint64_t d = n - 1;
  int s = 0;
  while (!(d & 1)) { d >>= 1; s++; }
  for (int i = 0; i < 12; i++) {
    uint64_t x = powmod(small[i], d, n);
    if (x == 1 || x == n - 1) continue;
    int k = 1;
    for (; k < s; k++) {
      x = mulmod(x, x, n);
      if (x == n - 1) break;
    }
    if (k == s) return 0;
  }
  return 1;
}

int main(void) {
  uint64_t x = 0x9e3779b97f4a7c15ull, sum = 0;
  uint32_t primes = 0;
  // Odd candidates of every size from 2^20 to 2^64, so the 128-bit products
  // have high halves of every width.
  for (int i = 0; i < 80000; i++) {
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    uint64_t n = (x >> (i % 44)) | 1 | (1ull << (63 - i % 44));
    if (is_prime(n)) {
      primes++;
      sum = sum * 31 + n;
    }
  }
  printf("primes = %u sum = %016llx\n", primes, (unsigned long long)sum);
  return 0;
}
