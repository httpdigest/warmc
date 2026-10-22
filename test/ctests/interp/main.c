// A small stack-machine bytecode interpreter run two ways -- a switch in a
// loop and GNU computed goto (labels as values, `goto *table[op]`) -- over a
// handful of programs: arithmetic, a counted loop, calls and returns with a
// frame stack, a recursive Fibonacci, and a sieve in VM memory.  Both
// dispatchers must produce the same results and output, and the switch
// version also traces the first instructions it executes.
//
// This is the shape of every interpreter: a hot loop around one big indirect
// branch, with the VM's state in locals the compiler would like to keep in
// registers.  In wasm both dispatchers become a br_table; computed goto adds
// clang's lowering of indirectbr, where every label whose address is taken
// becomes a target of a single shared dispatch block.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  PUSH, POP, DUP, SWAP, OVER, ADD, SUB, MUL, DIV, MOD, NEG, AND, OR, XOR, SHL, SHR,
  EQ, LT, GT, NOT, JMP, JZ, JNZ, CALL, RET, LOAD, STORE, LOADL, STOREL, PRINT, HALT, NOP,
  NOPS
};
static const char *names[] = {
  "PUSH", "POP", "DUP", "SWAP", "OVER", "ADD", "SUB", "MUL", "DIV", "MOD", "NEG", "AND", "OR", "XOR", "SHL", "SHR",
  "EQ", "LT", "GT", "NOT", "JMP", "JZ", "JNZ", "CALL", "RET", "LOAD", "STORE", "LOADL", "STOREL", "PRINT", "HALT", "NOP",
};

typedef struct {
  int64_t stack[256];
  int sp;
  int frames[64];   // return addresses
  int fbase[64];    // frame base (locals start) per call
  int fp;
  int64_t mem[512];
  int64_t locals[1024];
  int lp;
  long steps;
  char out[512];
  int outlen;
} VM;

static void emit(VM *vm, int64_t v) {
  vm->outlen += snprintf(vm->out + vm->outlen, sizeof vm->out - (size_t)vm->outlen, "%lld ", (long long)v);
}

static int64_t run_switch(VM *vm, const int *code, int trace) {
  int pc = 0;
  for (;;) {
    int op = code[pc++];
    vm->steps++;
    if (trace && vm->steps <= trace) {
      printf("  %3d %-6s sp=%d top=", pc - 1, names[op], vm->sp);
      if (vm->sp) printf("%lld", (long long)vm->stack[vm->sp - 1]);
      printf("\n");
    }
    int64_t a, b;
    switch (op) {
      case PUSH: vm->stack[vm->sp++] = code[pc++]; break;
      case POP: vm->sp--; break;
      case DUP: vm->stack[vm->sp] = vm->stack[vm->sp - 1]; vm->sp++; break;
      case SWAP: a = vm->stack[vm->sp - 1]; vm->stack[vm->sp - 1] = vm->stack[vm->sp - 2]; vm->stack[vm->sp - 2] = a; break;
      case OVER: vm->stack[vm->sp] = vm->stack[vm->sp - 2]; vm->sp++; break;
#define BIN(OPC, EXPR) case OPC: b = vm->stack[--vm->sp]; a = vm->stack[vm->sp - 1]; vm->stack[vm->sp - 1] = (EXPR); break;
      BIN(ADD, (int64_t)((uint64_t)a + (uint64_t)b))
      BIN(SUB, (int64_t)((uint64_t)a - (uint64_t)b))
      BIN(MUL, (int64_t)((uint64_t)a * (uint64_t)b))
      BIN(DIV, b ? (a == INT64_MIN && b == -1 ? a : a / b) : 0)
      BIN(MOD, b ? (b == -1 ? 0 : a % b) : 0)
      BIN(AND, a & b)
      BIN(OR, a | b)
      BIN(XOR, a ^ b)
      BIN(SHL, (int64_t)((uint64_t)a << (b & 63)))
      BIN(SHR, a >> (b & 63))
      BIN(EQ, a == b)
      BIN(LT, a < b)
      BIN(GT, a > b)
      case NEG: vm->stack[vm->sp - 1] = (int64_t)(0 - (uint64_t)vm->stack[vm->sp - 1]); break;
      case NOT: vm->stack[vm->sp - 1] = !vm->stack[vm->sp - 1]; break;
      case JMP: pc = code[pc]; break;
      case JZ: a = vm->stack[--vm->sp]; pc = a ? pc + 1 : code[pc]; break;
      case JNZ: a = vm->stack[--vm->sp]; pc = a ? code[pc] : pc + 1; break;
      case CALL:
        vm->frames[vm->fp] = pc + 2;
        vm->fbase[vm->fp++] = vm->lp;
        vm->lp += code[pc + 1];  // number of locals
        pc = code[pc];
        break;
      case RET:
        vm->lp = vm->fbase[--vm->fp];
        pc = vm->frames[vm->fp];
        break;
      case LOAD: a = vm->stack[vm->sp - 1]; vm->stack[vm->sp - 1] = vm->mem[a & 511]; break;
      case STORE: b = vm->stack[--vm->sp]; a = vm->stack[--vm->sp]; vm->mem[a & 511] = b; break;
      case LOADL: vm->stack[vm->sp++] = vm->locals[vm->fbase[vm->fp - 1] + code[pc++]]; break;
      case STOREL: vm->locals[vm->fbase[vm->fp - 1] + code[pc++]] = vm->stack[--vm->sp]; break;
      case PRINT: emit(vm, vm->stack[--vm->sp]); break;
      case HALT: return vm->sp ? vm->stack[vm->sp - 1] : 0;
      case NOP: break;
      default: return -999;
    }
  }
}

static int64_t run_goto(VM *vm, const int *code) {
  static void *table[NOPS] = {
    &&L_PUSH, &&L_POP, &&L_DUP, &&L_SWAP, &&L_OVER, &&L_ADD, &&L_SUB, &&L_MUL, &&L_DIV, &&L_MOD, &&L_NEG,
    &&L_AND, &&L_OR, &&L_XOR, &&L_SHL, &&L_SHR, &&L_EQ, &&L_LT, &&L_GT, &&L_NOT, &&L_JMP, &&L_JZ, &&L_JNZ,
    &&L_CALL, &&L_RET, &&L_LOAD, &&L_STORE, &&L_LOADL, &&L_STOREL, &&L_PRINT, &&L_HALT, &&L_NOP,
  };
  int pc = 0;
  int64_t a, b;
#define NEXT do { vm->steps++; goto *table[code[pc++]]; } while (0)
  NEXT;
L_PUSH: vm->stack[vm->sp++] = code[pc++]; NEXT;
L_POP: vm->sp--; NEXT;
L_DUP: vm->stack[vm->sp] = vm->stack[vm->sp - 1]; vm->sp++; NEXT;
L_SWAP: a = vm->stack[vm->sp - 1]; vm->stack[vm->sp - 1] = vm->stack[vm->sp - 2]; vm->stack[vm->sp - 2] = a; NEXT;
L_OVER: vm->stack[vm->sp] = vm->stack[vm->sp - 2]; vm->sp++; NEXT;
#define GBIN(L, EXPR) L: b = vm->stack[--vm->sp]; a = vm->stack[vm->sp - 1]; vm->stack[vm->sp - 1] = (EXPR); NEXT;
  GBIN(L_ADD, (int64_t)((uint64_t)a + (uint64_t)b))
  GBIN(L_SUB, (int64_t)((uint64_t)a - (uint64_t)b))
  GBIN(L_MUL, (int64_t)((uint64_t)a * (uint64_t)b))
  GBIN(L_DIV, b ? (a == INT64_MIN && b == -1 ? a : a / b) : 0)
  GBIN(L_MOD, b ? (b == -1 ? 0 : a % b) : 0)
  GBIN(L_AND, a & b)
  GBIN(L_OR, a | b)
  GBIN(L_XOR, a ^ b)
  GBIN(L_SHL, (int64_t)((uint64_t)a << (b & 63)))
  GBIN(L_SHR, a >> (b & 63))
  GBIN(L_EQ, a == b)
  GBIN(L_LT, a < b)
  GBIN(L_GT, a > b)
L_NEG: vm->stack[vm->sp - 1] = (int64_t)(0 - (uint64_t)vm->stack[vm->sp - 1]); NEXT;
L_NOT: vm->stack[vm->sp - 1] = !vm->stack[vm->sp - 1]; NEXT;
L_JMP: pc = code[pc]; NEXT;
L_JZ: a = vm->stack[--vm->sp]; pc = a ? pc + 1 : code[pc]; NEXT;
L_JNZ: a = vm->stack[--vm->sp]; pc = a ? code[pc] : pc + 1; NEXT;
L_CALL:
  vm->frames[vm->fp] = pc + 2;
  vm->fbase[vm->fp++] = vm->lp;
  vm->lp += code[pc + 1];
  pc = code[pc];
  NEXT;
L_RET:
  vm->lp = vm->fbase[--vm->fp];
  pc = vm->frames[vm->fp];
  NEXT;
L_LOAD: a = vm->stack[vm->sp - 1]; vm->stack[vm->sp - 1] = vm->mem[a & 511]; NEXT;
L_STORE: b = vm->stack[--vm->sp]; a = vm->stack[--vm->sp]; vm->mem[a & 511] = b; NEXT;
L_LOADL: vm->stack[vm->sp++] = vm->locals[vm->fbase[vm->fp - 1] + code[pc++]]; NEXT;
L_STOREL: vm->locals[vm->fbase[vm->fp - 1] + code[pc++]] = vm->stack[--vm->sp]; NEXT;
L_PRINT: emit(vm, vm->stack[--vm->sp]); NEXT;
L_NOP: NEXT;
L_HALT: return vm->sp ? vm->stack[vm->sp - 1] : 0;
}

// Programs.  Each starts with CALL entry,nlocals and HALT, so the body runs in
// a frame of its own and LOADL/STOREL address its locals.  Jump targets are
// absolute indices into the array; the numbers in comments are those indices.
static const int prog_arith[] = {
  CALL, 4, 4, HALT,
  /*4*/ PUSH, 7, PUSH, 6, MUL, DUP, PRINT, PUSH, 100, SWAP, SUB, DUP, PRINT, PUSH, 3, DIV, DUP, PRINT,
  PUSH, -17, PUSH, 5, MOD, PRINT, PUSH, 1, PUSH, 40, SHL, DUP, PRINT, PUSH, 12, SHR, PRINT,
  PUSH, 0xff, PUSH, 0x0f, XOR, PUSH, 0x30, OR, PUSH, 0xfc, AND, PRINT, PUSH, 5, NEG, PRINT,
  PUSH, 3, PUSH, 4, LT, PUSH, 3, PUSH, 4, GT, PUSH, 9, PUSH, 9, EQ, PRINT, PRINT, PRINT, PUSH, 0, NOT, PRINT,
  PUSH, 2, PUSH, 9, OVER, PRINT, PRINT, PRINT, RET,
};
// sum of i*i for i in 1..200 with a loop; local 0 = acc, local 1 = i
static const int prog_loop[] = {
  CALL, 4, 4, HALT,
  /*4*/ PUSH, 0, STOREL, 0,
  /*8*/ PUSH, 1, STOREL, 1,
  /*12*/ LOADL, 1, PUSH, 200, GT, JNZ, 38,
  /*19*/ LOADL, 0, LOADL, 1, LOADL, 1, MUL, ADD, STOREL, 0,
  /*29*/ LOADL, 1, PUSH, 1, ADD, STOREL, 1, JMP, 12,
  /*38*/ LOADL, 0, DUP, PRINT, RET,
};
// recursive fib(n) = n < 2 ? n : fib(n-1) + fib(n-2), argument passed on the stack
static const int prog_fib[] = {
  PUSH, 0, CALL, 7, 1, HALT, NOP,
  /*7*/ STOREL, 0, LOADL, 0, PUSH, 2, LT, JZ, 19, LOADL, 0, RET,
  /*19*/ LOADL, 0, PUSH, 1, SUB, CALL, 7, 1, LOADL, 0, PUSH, 2, SUB, CALL, 7, 1, ADD, RET,
};
// sieve of Eratosthenes over mem[0..500), then count the primes;
// local 0 = i, local 1 = j, local 2 = count
static const int prog_sieve[] = {
  CALL, 4, 4, HALT,
  /*4*/ PUSH, 2, STOREL, 0,
  /*8*/ LOADL, 0, LOADL, 0, MUL, PUSH, 500, LT, JZ, 60,
  /*18*/ LOADL, 0, LOAD, JNZ, 51,
  /*23*/ LOADL, 0, LOADL, 0, MUL, STOREL, 1,
  /*30*/ LOADL, 1, PUSH, 500, LT, JZ, 51,
  /*37*/ LOADL, 1, PUSH, 1, STORE, LOADL, 1, LOADL, 0, ADD, STOREL, 1, JMP, 30,
  /*51*/ LOADL, 0, PUSH, 1, ADD, STOREL, 0, JMP, 8,
  /*60*/ PUSH, 0, STOREL, 2, PUSH, 2, STOREL, 0,
  /*68*/ LOADL, 0, PUSH, 500, LT, JZ, 93,
  /*75*/ LOADL, 0, LOAD, NOT, LOADL, 2, ADD, STOREL, 2,
  /*84*/ LOADL, 0, PUSH, 1, ADD, STOREL, 0, JMP, 68,
  /*93*/ LOADL, 2, DUP, PRINT, RET,
};

static int64_t last_mem[512];

static void run_both(const char *name, const int *code, int len, int trace) {
  static VM v1, v2;
  memset(&v1, 0, sizeof v1);
  memset(&v2, 0, sizeof v2);
  v1.lp = v2.lp = 4;
  v1.fbase[0] = v2.fbase[0] = 0;
  v1.fp = v2.fp = 1;  // dummy outer frame
  (void)len;
  if (trace) printf("trace of %s:\n", name);
  int64_t r1 = run_switch(&v1, code, trace);
  int64_t r2 = run_goto(&v2, code);
  printf("%s: switch=%lld (steps %ld) goto=%lld (steps %ld) out=[%s] same_out=%d\n", name, (long long)r1, v1.steps,
         (long long)r2, v2.steps, v1.out, strcmp(v1.out, v2.out) == 0);
  printf("  same memory=%d same stack depth=%d\n", memcmp(v1.mem, v2.mem, sizeof v1.mem) == 0, v1.sp == v2.sp);
  memcpy(last_mem, v1.mem, sizeof last_mem);
}

int main(void) {
  run_both("arith", prog_arith, (int)(sizeof prog_arith / sizeof prog_arith[0]), 30);
  run_both("loop", prog_loop, (int)(sizeof prog_loop / sizeof prog_loop[0]), 25);
  for (int n = 0; n <= 20; n += 4) {
    static int fibcode[64];
    memcpy(fibcode, prog_fib, sizeof prog_fib);
    fibcode[1] = n;
    char name[16];
    snprintf(name, sizeof name, "fib(%d)", n);
    run_both(name, fibcode, (int)(sizeof prog_fib / sizeof prog_fib[0]), 0);
  }
  run_both("sieve", prog_sieve, (int)(sizeof prog_sieve / sizeof prog_sieve[0]), 0);
  printf("primes marked: mem[2..30]:");
  for (int i = 2; i <= 30; i++) printf("%d", (int)!last_mem[i]);
  printf("\n");
  return 0;
}
