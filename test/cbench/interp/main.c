// A bytecode interpreter: a fetch, a switch on the opcode, and a jump back,
// which is the inner loop of every scripting language that is written in C
// and compiled to wasm.
//
// The switch lowers to a br_table, and the operand stack lives in linear
// memory, so the row is bounded by the dispatch branch -- which the predictor
// sees as one indirect branch with many targets -- and by the loads and stores
// of a stack the compiler cannot keep in registers.  No other corpus has a
// dispatch loop of this shape.
#include <stdio.h>
#include <stdint.h>

enum { PUSH, ADD, SUB, MUL, DUP, SWAP, JNZ, DEC, LOAD, STORE, XOR, SHR, HALT };

static int64_t run(const int32_t *code, int64_t *vars) {
  int64_t stack[64];
  int sp = 0, pc = 0;
  for (;;) {
    int32_t op = code[pc++];
    switch (op) {
      case PUSH: stack[sp++] = code[pc++]; break;
      case ADD: sp--; stack[sp - 1] += stack[sp]; break;
      case SUB: sp--; stack[sp - 1] -= stack[sp]; break;
      case MUL: sp--; stack[sp - 1] *= stack[sp]; break;
      case DUP: stack[sp] = stack[sp - 1]; sp++; break;
      case SWAP: { int64_t t = stack[sp - 1]; stack[sp - 1] = stack[sp - 2]; stack[sp - 2] = t; break; }
      case JNZ: { int32_t to = code[pc++]; if (stack[--sp]) pc = to; break; }
      case DEC: stack[sp - 1]--; break;
      case LOAD: stack[sp++] = vars[code[pc++]]; break;
      case STORE: vars[code[pc++]] = stack[--sp]; break;
      case XOR: sp--; stack[sp - 1] ^= stack[sp]; break;
      case SHR: sp--; stack[sp - 1] = (int64_t)((uint64_t)stack[sp - 1] >> (stack[sp] & 63)); break;
      case HALT: return stack[sp - 1];
    }
  }
}

int main(void) {
  // acc = 0; n = N; do { acc = (acc * 31 + n) ^ (acc >> 7); n--; } while (n);
  const int32_t code[] = {
    PUSH, 0, STORE, 0,
    PUSH, 3000000, STORE, 1,
    /* 8 */ LOAD, 0, PUSH, 31, MUL, LOAD, 1, ADD,
    LOAD, 0, PUSH, 7, SHR, XOR, STORE, 0,
    LOAD, 1, DEC, DUP, STORE, 1, JNZ, 8,
    LOAD, 0, HALT,
  };
  int64_t vars[4] = {0};
  int64_t r = 0;
  for (int i = 0; i < 4; i++) r = r * 1000003 + run(code, vars) + i;
  printf("r = %lld\n", (long long)r);
  return 0;
}
