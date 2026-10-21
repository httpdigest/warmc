// A table of handlers indexed by data: an event loop, a state machine or a
// command dispatcher, each step an indirect call through a function pointer
// the data chose.
//
// Next to rsbench/dyntrait, which reaches its targets through a vtable per
// object, this reaches them through one static table, so the row is the cost
// of call_indirect itself -- the signature check and the table load -- with a
// target that changes from one call to the next.
#include <stdio.h>
#include <stdint.h>

typedef uint32_t (*Step)(uint32_t state, uint32_t arg);

static uint32_t op_add(uint32_t s, uint32_t a) { return s + a; }
static uint32_t op_mul(uint32_t s, uint32_t a) { return s * (a | 1); }
static uint32_t op_xor(uint32_t s, uint32_t a) { return s ^ (a << 3); }
static uint32_t op_rot(uint32_t s, uint32_t a) { a &= 31; return (s << a) | (s >> ((32 - a) & 31)); }
static uint32_t op_sub(uint32_t s, uint32_t a) { return s - (a >> 1); }
static uint32_t op_mix(uint32_t s, uint32_t a) { return (s ^ (s >> 13)) + a * 0x9e3779b9u; }
static uint32_t op_min(uint32_t s, uint32_t a) { return s < a ? s + 1 : s - a; }
static uint32_t op_neg(uint32_t s, uint32_t a) { return ~s + a; }

static Step table[8] = { op_add, op_mul, op_xor, op_rot, op_sub, op_mix, op_min, op_neg };

int main(void) {
  uint32_t s = 1, x = 7;
  for (int i = 0; i < 18000000; i++) {
    x = x * 1664525u + 1013904223u;
    s = table[x >> 29](s, x);
  }
  printf("s = %u\n", s);
  return 0;
}
