// Arbitrary-precision arithmetic on uint32 limbs: a factorial by repeated
// multiplication by a small number, its square by schoolbook multiplication,
// the square printed in decimal by repeated division by 10^9, and a Fibonacci
// number by repeated addition -- what a C program without GMP writes.
//
// Each limb product is a uint64_t from two zero-extending i64.load32_u, an
// i64.mul and adds, with the carry taken as the high half by i64.shr_u and the
// low half stored with i64.store32.  The decimal conversion is an i64.div_u by
// the constant 10^9 per limb, which LLVM leaves as a division because wasm has
// no 64-bit high multiply to turn it into.  Every loop carries its carry or
// remainder from one limb to the next, so the row measures the latency of
// 64-bit multiply, divide and add as much as their throughput.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef struct { uint32_t *d; size_t n; } Big;   // little-endian limbs

static void mul_small(Big *a, uint32_t m) {
  uint64_t carry = 0;
  for (size_t i = 0; i < a->n; i++) {
    uint64_t t = (uint64_t)a->d[i] * m + carry;
    a->d[i] = (uint32_t)t;
    carry = t >> 32;
  }
  if (carry) a->d[a->n++] = (uint32_t)carry;
}

// r = a + b; r may be a.
static void add(Big *r, const Big *a, const Big *b) {
  size_t n = a->n > b->n ? a->n : b->n;
  uint64_t carry = 0;
  for (size_t i = 0; i < n; i++) {
    uint64_t t = carry + (i < a->n ? a->d[i] : 0) + (i < b->n ? b->d[i] : 0);
    r->d[i] = (uint32_t)t;
    carry = t >> 32;
  }
  r->n = n;
  if (carry) r->d[r->n++] = (uint32_t)carry;
}

// Schoolbook: a 32x32->64 multiply-add per pair of limbs.
static void mul(Big *r, const Big *a, const Big *b) {
  memset(r->d, 0, (a->n + b->n) * sizeof *r->d);
  for (size_t i = 0; i < a->n; i++) {
    uint64_t carry = 0, ai = a->d[i];
    for (size_t j = 0; j < b->n; j++) {
      uint64_t t = ai * b->d[j] + r->d[i + j] + carry;
      r->d[i + j] = (uint32_t)t;
      carry = t >> 32;
    }
    r->d[i + b->n] = (uint32_t)carry;
  }
  r->n = a->n + b->n;
  while (r->n && !r->d[r->n - 1]) r->n--;
}

// Divides in place and returns the remainder: a 64-bit by 32-bit division per limb.
static uint32_t divmod_small(Big *a, uint32_t m) {
  uint64_t rem = 0;
  for (size_t i = a->n; i-- > 0;) {
    uint64_t cur = rem << 32 | a->d[i];
    a->d[i] = (uint32_t)(cur / m);
    rem = cur % m;
  }
  while (a->n && !a->d[a->n - 1]) a->n--;
  return (uint32_t)rem;
}

static Big make(size_t cap) {
  Big b = { calloc(cap, sizeof(uint32_t)), 0 };
  return b;
}

int main(void) {
  enum { CAP = 4096 };
  Big f = make(CAP), sq = make(2 * CAP), fa = make(CAP), fb = make(CAP);
  uint64_t check = 0;

  // 5000!, then its square, then the square in decimal.
  f.d[0] = 1; f.n = 1;
  for (uint32_t k = 2; k <= 5000; k++) mul_small(&f, k);
  mul(&sq, &f, &f);
  size_t limbs = sq.n;
  uint64_t digits = 0, dsum = 0;
  while (sq.n) {
    uint32_t chunk = divmod_small(&sq, 1000000000u);
    for (int j = 0; j < 9; j++) { dsum += chunk % 10; chunk /= 10; }
    digits += 9;
  }
  check = check * 1000003 + limbs * 7 + dsum;

  // Fibonacci by repeated addition, two numbers leapfrogging.
  fa.d[0] = 0; fa.n = 1;
  fb.d[0] = 1; fb.n = 1;
  for (int k = 0; k < 36000; k++) {
    add(&fa, &fa, &fb);
    add(&fb, &fb, &fa);
  }
  for (size_t i = 0; i < fb.n; i += 3) check = check * 31 + fb.d[i];
  check += fb.n;

  printf("digits = %llu dsum = %llu fib_limbs = %zu check = %016llx\n",
         (unsigned long long)digits, (unsigned long long)dsum, fb.n, (unsigned long long)check);
  free(f.d); free(sq.d); free(fa.d); free(fb.d);
  return 0;
}
