// Recursion: plain (factorial in i64, fibonacci both naive and memoised),
// mutual (isEven/isOdd, a recursive-descent expression parser whose
// expr/term/factor call each other), deep but bounded (a 4000-frame linear
// recursion with scalar arguments, which stays inside every engine's native
// stack, and 1000 frames of one holding managed references, which use the
// 32 KiB shadow stack), non-tail with work after the call (Ackermann,
// Hanoi, permutations by swapping), and recursion through a function
// reference.  Each result is printed, and the call counts with them: a
// spilled argument restored wrong after a call changes the counts.
import { print } from "../wasi";

let calls = 0;

function fact(n: i64): i64 { calls++; return n <= 1 ? 1 : n * fact(n - 1); }
function fib(n: i32): i64 { calls++; return n < 2 ? <i64>n : fib(n - 1) + fib(n - 2); }

let memo = new Map<i32, i64>();
function fibm(n: i32): i64 {
  calls++;
  if (n < 2) return <i64>n;
  if (memo.has(n)) return memo.get(n);
  let r = fibm(n - 1) + fibm(n - 2);
  memo.set(n, r);
  return r;
}

function isEven(n: u32): bool { return n == 0 ? true : isOdd(n - 1); }
function isOdd(n: u32): bool { return n == 0 ? false : isEven(n - 1); }

function ack(m: i32, n: i32): i32 {
  calls++;
  if (m == 0) return n + 1;
  if (n == 0) return ack(m - 1, 1);
  return ack(m - 1, ack(m, n - 1));
}

let moves = "";
let moveCount = 0;
function hanoi(n: i32, from: i32, to: i32, via: i32): void {
  if (n == 0) return;
  hanoi(n - 1, from, via, to);
  moveCount++;
  if (moveCount <= 20) moves += from.toString() + to.toString() + " ";
  hanoi(n - 1, via, to, from);
}

// Deep scalar recursion: a sum with the argument and an accumulator both
// live across the call.
function deep(n: i32, k: i64): i64 {
  if (n == 0) return k;
  let r = deep(n - 1, k * 3 + <i64>n);
  return r ^ (<i64>n << 7);
}

// Deep recursion holding a managed reference across each call.
class Box { constructor(public v: i32, public next: Box | null) {} }
function chain(n: i32, prev: Box | null): i32 {
  if (n == 0) {
    let s = 0;
    let b = prev;
    while (b !== null) { s += b.v; b = b.next; }
    return s;
  }
  let b = new Box(n * 7 % 13, prev);
  let r = chain(n - 1, b);
  return r + b.v; // b must still be valid after the deep call
}

let perms: string[] = [];
function permute(a: i32[], k: i32): void {
  if (k == a.length) { perms.push(a.join("")); return; }
  for (let i = k; i < a.length; i++) {
    let t = a[k]; a[k] = a[i]; a[i] = t;
    permute(a, k + 1);
    t = a[k]; a[k] = a[i]; a[i] = t;
  }
}

// Recursive descent: expr := term (('+'|'-') term)*, term := factor (('*'|'/'|'%') factor)*,
// factor := number | '-' factor | '(' expr ')'
let src = "";
let pos = 0;
function peek(): i32 { while (pos < src.length && src.charCodeAt(pos) == 32) pos++; return pos < src.length ? src.charCodeAt(pos) : 0; }
function factor(): i64 {
  let c = peek();
  if (c == 45) { pos++; return -factor(); }
  if (c == 40) { pos++; let v = expr(); peek(); pos++; return v; }
  let v: i64 = 0;
  while (pos < src.length && src.charCodeAt(pos) >= 48 && src.charCodeAt(pos) <= 57) { v = v * 10 + <i64>(src.charCodeAt(pos) - 48); pos++; }
  return v;
}
function term(): i64 {
  let v = factor();
  while (true) {
    let c = peek();
    if (c == 42) { pos++; v *= factor(); }
    else if (c == 47) { pos++; let d = factor(); v = d == 0 ? 0 : v / d; }
    else if (c == 37) { pos++; let d = factor(); v = d == 0 ? 0 : v % d; }
    else return v;
  }
}
function expr(): i64 {
  let v = term();
  while (true) {
    let c = peek();
    if (c == 43) { pos++; v += term(); }
    else if (c == 45) { pos++; v -= term(); }
    else return v;
  }
}
function evaluate(s: string): i64 { src = s; pos = 0; return expr(); }

// Recursion through a function reference held in a global: every level is
// a call_indirect back into the same function.
let digitRef: (n: i32) => i32 = (n: i32): i32 => n;
function digitSum(n: i32): i32 { return n < 10 ? n : (n % 10) + digitRef(n / 10); }
function digitProd(n: i32): i32 { return n < 10 ? max(n, 1) : max(n % 10, 1) * digitRef(n / 10); }

function gcd(a: i64, b: i64): i64 { return b == 0 ? a : gcd(b, a % b); }

function run(): void {
  for (let n: i64 = 0; n <= 25; n += 5) { calls = 0; let v = fact(n); print("fact " + n.toString() + " = " + v.toString() + " calls=" + calls.toString()); }
  for (let n = 0; n <= 24; n += 4) { calls = 0; let v = fib(n); print("fib " + n.toString() + " = " + v.toString() + " calls=" + calls.toString()); }
  calls = 0;
  print("fibm 90 = " + fibm(90).toString() + " calls=" + calls.toString());
  let ev = "";
  for (let n: u32 = 0; n < 40; n += 3) ev += (isEven(n) ? "E" : "O");
  print("even/odd " + ev + " deep " + (isEven(9999) ? "E" : "O") + (isOdd(10001) ? "O" : "E"));
  for (let m = 0; m <= 3; m++) {
    let line = "";
    for (let n = 0; n <= 4; n++) { calls = 0; let v = ack(m, n); line += v.toString() + "(" + calls.toString() + ") "; }
    print("ack " + m.toString() + ": " + line);
  }
  hanoi(10, 1, 3, 2);
  print("hanoi moves=" + moveCount.toString() + " first " + moves);
  print("deep 4000 = " + deep(4000, 1).toString());
  print("deep 777 = " + deep(777, -5).toString());
  print("chain 1000 = " + chain(1000, null).toString());
  permute([1, 2, 3, 4], 0);
  print("perms " + perms.length.toString() + " " + perms.join(","));
  const exprs = ["1+2*3", "(1+2)*3", "-4*-(2+3)", "100/7%4", "2*(3+(4-1)*5)-6/2", "((((7))))", "1-2-3-4", "9*9*9*9*9*9*9*9*9*9", "17 % 5 * -3 + 10 / 0"];
  for (let i = 0; i < exprs.length; i++) print("eval " + exprs[i] + " = " + evaluate(exprs[i]).toString());
  digitRef = digitSum;
  let a = digitRef(987654321), b = digitRef(0x7fffffff);
  digitRef = digitProd;
  print("digitsum " + a.toString() + " " + b.toString() + " digitprod " + digitRef(987654321).toString() + " " + digitRef(0x7fffffff).toString());
  print("gcd " + gcd(1071, 462).toString() + " " + gcd(0x7fffffffffffffff, 0x1000000000000001).toString() + " " + gcd(832040, 514229).toString());
}
run();
