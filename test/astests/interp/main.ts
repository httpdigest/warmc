// A small interpreter: a compiler from an infix toy language (assignments,
// while loops, if/else, comparisons, arithmetic, print) to a stack bytecode,
// and a dispatch loop that runs it.  The dispatch loop is a dense switch over
// opcodes -- a br_table inside a loop, with the operand stack in a
// StaticArray<i64> and the program counter, stack pointer and variables all
// live across every iteration -- the shape of every interpreter, and the one
// where a register allocator's choices across many small blocks matter.  Also
// a Brainfuck interpreter with precomputed bracket jumps running a program
// that prints, and a tree-walking evaluator over the same toy language's AST
// whose output must match the bytecode VM's.
import { print } from "../wasi";

const enum Op { PUSH, LOAD, STORE, ADD, SUB, MUL, DIV, MOD, LT, LE, EQ, NE, JMP, JZ, PRINT, NEG, AND, SHL, HALT }

// ---- lexer/parser producing both bytecode and an AST ----
class Node {
  constructor(public kind: i32, public v: i64, public a: Node | null = null, public b: Node | null = null, public c: Node | null = null) {}
}
// kinds: 0 num, 1 var, 2 binop(v=op char), 3 assign(v=var), 4 while, 5 if, 6 print, 7 seq, 8 neg

let src = "";
let p = 0;
let code: i64[] = [];

function skip(): void { while (p < src.length && (src.charCodeAt(p) == 32 || src.charCodeAt(p) == 10)) p++; }
function peekc(): i32 { skip(); return p < src.length ? src.charCodeAt(p) : 0; }
function isAlpha(c: i32): bool { return c >= 97 && c <= 122; }
function ident(): string { skip(); let s = p; while (p < src.length && isAlpha(src.charCodeAt(p))) p++; return src.substring(s, p); }
function emit(x: i64): i32 { code.push(x); return code.length - 1; }

function primary(): Node {
  let c = peekc();
  if (c == 40) { p++; let n = expr(); peekc(); p++; return n; }
  if (c == 45) { p++; let n = primary(); emit(Op.NEG); return new Node(8, 0, n); }
  if (isAlpha(c)) { let id = ident(); let v = <i64>(id.charCodeAt(0) - 97); emit(Op.LOAD); emit(v); return new Node(1, v); }
  let v: i64 = 0;
  while (p < src.length && src.charCodeAt(p) >= 48 && src.charCodeAt(p) <= 57) { v = v * 10 + <i64>(src.charCodeAt(p) - 48); p++; }
  emit(Op.PUSH); emit(v);
  return new Node(0, v);
}
function mul(): Node {
  let l = primary();
  while (true) {
    let c = peekc();
    if (c == 42 || c == 47 || c == 37 || c == 38) {
      p++;
      let r = primary();
      emit(c == 42 ? Op.MUL : c == 47 ? Op.DIV : c == 37 ? Op.MOD : Op.AND);
      l = new Node(2, c, l, r);
    } else if (c == 60 && p + 1 < src.length && src.charCodeAt(p + 1) == 60) {
      p += 2; let r = primary(); emit(Op.SHL); l = new Node(2, 1, l, r);
    } else return l;
  }
}
function add(): Node {
  let l = mul();
  while (true) {
    let c = peekc();
    if (c == 43 || c == 45) { p++; let r = mul(); emit(c == 43 ? Op.ADD : Op.SUB); l = new Node(2, c, l, r); }
    else return l;
  }
}
function expr(): Node {
  let l = add();
  let c = peekc();
  if (c == 60 && src.charCodeAt(p + 1) != 60) {
    p++;
    if (src.charCodeAt(p) == 61) { p++; let r = add(); emit(Op.LE); return new Node(2, 2, l, r); }
    let r = add(); emit(Op.LT); return new Node(2, 60, l, r);
  }
  if (c == 61 && src.charCodeAt(p + 1) == 61) { p += 2; let r = add(); emit(Op.EQ); return new Node(2, 3, l, r); }
  if (c == 33 && src.charCodeAt(p + 1) == 61) { p += 2; let r = add(); emit(Op.NE); return new Node(2, 4, l, r); }
  return l;
}
function stmt(): Node {
  let save = p;
  let id = ident();
  if (id == "while") {
    let top = code.length;
    let cond = expr();
    emit(Op.JZ); let patch = emit(0);
    let body = block();
    emit(Op.JMP); emit(<i64>top);
    code[patch] = <i64>code.length;
    return new Node(4, 0, cond, body);
  }
  if (id == "if") {
    let cond = expr();
    emit(Op.JZ); let patch = emit(0);
    let then = block();
    let els: Node | null = null;
    let save2 = p;
    if (ident() == "else") {
      emit(Op.JMP); let patch2 = emit(0);
      code[patch] = <i64>code.length;
      els = block();
      code[patch2] = <i64>code.length;
    } else { p = save2; code[patch] = <i64>code.length; }
    return new Node(5, 0, cond, then, els);
  }
  if (id == "print") { let e = expr(); emit(Op.PRINT); peekc(); p++; return new Node(6, 0, e); }
  // assignment
  let v = <i64>(id.charCodeAt(0) - 97);
  peekc(); p++; // '='
  let e = expr();
  emit(Op.STORE); emit(v);
  peekc(); p++; // ';'
  return new Node(3, v, e);
}
function block(): Node {
  peekc(); p++; // '{'
  let first: Node | null = null;
  let seq: Node | null = null;
  while (peekc() != 125) {
    let s = stmt();
    let n = new Node(7, 0, s);
    if (seq === null) first = n; else seq.b = n;
    seq = n;
  }
  p++;
  if (first === null) return new Node(7, 0);
  return first;
}

// ---- the bytecode VM ----
function vm(prog: StaticArray<i64>, vars: StaticArray<i64>): string {
  let stack = new StaticArray<i64>(64);
  let sp = 0, pc = 0, steps = 0;
  let out = "";
  while (true) {
    steps++;
    let op = <i32>unchecked(prog[pc++]);
    switch (op) {
      case Op.PUSH: unchecked(stack[sp++] = prog[pc++]); break;
      case Op.LOAD: unchecked(stack[sp++] = vars[<i32>prog[pc++]]); break;
      case Op.STORE: unchecked(vars[<i32>prog[pc++]] = stack[--sp]); break;
      case Op.ADD: sp--; unchecked(stack[sp - 1] += stack[sp]); break;
      case Op.SUB: sp--; unchecked(stack[sp - 1] -= stack[sp]); break;
      case Op.MUL: sp--; unchecked(stack[sp - 1] *= stack[sp]); break;
      case Op.DIV: { sp--; let d = unchecked(stack[sp]); unchecked(stack[sp - 1] = d == 0 ? 0 : stack[sp - 1] / d); break; }
      case Op.MOD: { sp--; let d = unchecked(stack[sp]); unchecked(stack[sp - 1] = d == 0 ? 0 : stack[sp - 1] % d); break; }
      case Op.LT: sp--; unchecked(stack[sp - 1] = <i64>(stack[sp - 1] < stack[sp])); break;
      case Op.LE: sp--; unchecked(stack[sp - 1] = <i64>(stack[sp - 1] <= stack[sp])); break;
      case Op.EQ: sp--; unchecked(stack[sp - 1] = <i64>(stack[sp - 1] == stack[sp])); break;
      case Op.NE: sp--; unchecked(stack[sp - 1] = <i64>(stack[sp - 1] != stack[sp])); break;
      case Op.AND: sp--; unchecked(stack[sp - 1] &= stack[sp]); break;
      case Op.SHL: sp--; unchecked(stack[sp - 1] <<= stack[sp]); break;
      case Op.NEG: unchecked(stack[sp - 1] = -stack[sp - 1]); break;
      case Op.JMP: pc = <i32>unchecked(prog[pc]); break;
      case Op.JZ: { let t = <i32>unchecked(prog[pc++]); if (unchecked(stack[--sp]) == 0) pc = t; break; }
      case Op.PRINT: out += unchecked(stack[--sp]).toString() + " "; break;
      case Op.HALT: return out + "| steps=" + steps.toString();
      default: return out + "| bad op " + op.toString();
    }
  }
}

// ---- the tree walker ----
let tout = "";
function ev(n: Node, vars: StaticArray<i64>): i64 {
  switch (n.kind) {
    case 0: return n.v;
    case 1: return vars[<i32>n.v];
    case 8: return -ev(n.a!, vars);
    case 2: {
      let l = ev(n.a!, vars), r = ev(n.b!, vars);
      switch (<i32>n.v) {
        case 43: return l + r;
        case 45: return l - r;
        case 42: return l * r;
        case 47: return r == 0 ? 0 : l / r;
        case 37: return r == 0 ? 0 : l % r;
        case 38: return l & r;
        case 1: return l << r;
        case 60: return <i64>(l < r);
        case 2: return <i64>(l <= r);
        case 3: return <i64>(l == r);
        case 4: return <i64>(l != r);
      }
      return 0;
    }
    case 3: vars[<i32>n.v] = ev(n.a!, vars); return 0;
    case 4: while (ev(n.a!, vars) != 0) ev(n.b!, vars); return 0;
    case 5: if (ev(n.a!, vars) != 0) ev(n.b!, vars); else if (n.c !== null) ev(n.c!, vars); return 0;
    case 6: tout += ev(n.a!, vars).toString() + " "; return 0;
    case 7: { let s: Node | null = n; while (s !== null) { if (s.a !== null) ev(s.a!, vars); s = s.b; } return 0; }
  }
  return 0;
}

function runProgram(name: string, text: string): void {
  src = "{" + text + "}";
  p = 0;
  code = [];
  let ast = block();
  emit(Op.HALT);
  let prog = StaticArray.fromArray<i64>(code);
  let vars1 = new StaticArray<i64>(26), vars2 = new StaticArray<i64>(26);
  let r = vm(prog, vars1);
  tout = "";
  ev(ast, vars2);
  let same = true;
  for (let i = 0; i < 26; i++) if (vars1[i] != vars2[i]) same = false;
  print(name + " code=" + code.length.toString() + " vm: " + r);
  print(name + " tree: " + tout + (same ? "vars agree" : "VARS DIFFER"));
}

function brainfuck(prog: string, input: string): string {
  let n = prog.length;
  let jump = new Int32Array(n);
  let stack: i32[] = [];
  for (let i = 0; i < n; i++) {
    let c = prog.charCodeAt(i);
    if (c == 91) stack.push(i);
    else if (c == 93) { let j = stack.pop(); jump[i] = j; jump[j] = i; }
  }
  let tape = new Uint8Array(3000);
  let ptr = 0, ip = 0, inp = 0, steps = 0;
  let out = "";
  while (ip < n) {
    steps++;
    switch (prog.charCodeAt(ip)) {
      case 62: ptr++; break;
      case 60: ptr--; break;
      case 43: tape[ptr]++; break;
      case 45: tape[ptr]--; break;
      case 46: out += String.fromCharCode(tape[ptr]); break;
      case 44: tape[ptr] = inp < input.length ? <u8>input.charCodeAt(inp++) : 0; break;
      case 91: if (tape[ptr] == 0) ip = jump[ip]; break;
      case 93: if (tape[ptr] != 0) ip = jump[ip]; break;
    }
    ip++;
  }
  return out + " (" + steps.toString() + " steps)";
}

function run(): void {
  runProgram("count", "i = 0; s = 0; while i < 10 { s = s + i * i; print s; i = i + 1; }");
  runProgram("fib", "a = 0; b = 1; n = 0; while n < 60 { t = a + b; a = b; b = t; n = n + 1; if n % 10 == 0 { print a; } } print b;");
  runProgram("primes", "n = 2; c = 0; while n < 120 { d = 2; q = 1; while d * d <= n { if n % d == 0 { q = 0; } d = d + 1; } if q { print n; c = c + 1; } n = n + 1; } print c;");
  runProgram("collatz", "m = 0; x = 1; while x < 40 { y = x; k = 0; while y != 1 { if y % 2 == 0 { y = y / 2; } else { y = 3 * y + 1; } k = k + 1; } if m < k { m = k; print x; print k; } x = x + 1; }");
  runProgram("gcd", "a = 1071; b = 462; while b != 0 { t = a % b; a = b; b = t; } print a; a = 0 - 7; print a * a * a; print -a / 2; print -(a % 4);");
  runProgram("bits", "x = 1; i = 0; while i < 70 { x = x << 1; if x & 255 == 0 { x = x + 3; } i = i + 1; if i % 7 == 0 { print x; } }");
  runProgram("nested", "i = 0; t = 0; while i < 12 { j = 0; while j < i { if (i + j) % 3 == 0 { t = t + i * j; } else { t = t - 1; } j = j + 1; } i = i + 1; } print t;");
  print("bf " + brainfuck("++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.", ""));
  print("bf-echo " + brainfuck(",[.,]", "echo me"));
  print("bf-rev " + brainfuck(">,[>,]<[.<]", "reversed!"));
  // add two single digits read from input
  print("bf-add " + brainfuck(",>,[<+>-]<------------------------------------------------.", "34"));
}
run();
