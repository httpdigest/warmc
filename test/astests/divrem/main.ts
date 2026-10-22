// Division and remainder at every width with negative operands: wasm's
// div_s truncates toward zero and rem_s takes the dividend's sign, which
// differs from floor division -- so -7 / 2 is -3 and -7 % 2 is -1.  Both
// variable divisors (arm64 sdiv/udiv, with msub for the remainder) and
// constant divisors (which compilers turn into multiply-high and shift
// sequences with a correction for negative dividends) are covered, including
// powers of two, where the signed case needs a bias before the shift, and the
// awkward constants 3, 7, 10, 641, 0x7fffffff and -1.  Divisors are never zero
// and INT_MIN / -1 is never asked of a variable divisor, so nothing traps.
// Floor division and Euclidean modulo, built on top, are printed too.
import { print } from "../wasi";

const dividends: i32[] = [
  0, 1, -1, 2, -2, 6, -6, 7, -7, 99, -99, 100, -100, 1000, -1000, 65535, -65536,
  0x7fffffff, -0x7fffffff, -0x80000000, 123456789, -123456789, 0x40000000,
];
const divisors: i32[] = [1, -1, 2, -2, 3, -3, 7, -7, 10, -10, 16, -16, 641, 1000, 0x7fffffff, -0x80000000];
const dividends64: i64[] = [
  0, 1, -1, 7, -7, 100, -100, 0x7fffffff, -0x80000000, 0x100000000, -0x100000001,
  0x7fffffffffffffff, -0x7fffffffffffffff, 0x0123456789abcdef, -0x0123456789abcdef,
  1000000000000000000,
];
const divisors64: i64[] = [1, -1, 2, 3, -3, 10, -10, 1024, 0x100000000, 1000000007, -0x8000000000000000];

function var32(a: i32, b: i32): string {
  let q = (a == -0x80000000 && b == -1) ? a : a / b;
  let r = (a == -0x80000000 && b == -1) ? 0 : a % b;
  let ua = <u32>a, ub = <u32>b;
  let fq = q - ((r != 0 && ((r ^ b) < 0)) ? 1 : 0);
  let em = r < 0 ? r + abs(b) : r;
  return "v32 " + a.toString() + " " + b.toString() + " q=" + q.toString() + " r=" + r.toString() +
    " uq=" + (ua / ub).toString() + " ur=" + (ua % ub).toString() +
    " floor=" + fq.toString() + " emod=" + em.toString() +
    " recon=" + (q * b + r == a ? "ok" : "BAD");
}

function const32(a: i32): string {
  let u = <u32>a;
  return "c32 " + a.toString() +
    " /2=" + (a / 2).toString() + " %2=" + (a % 2).toString() +
    " /3=" + (a / 3).toString() + " %3=" + (a % 3).toString() +
    " /-3=" + (a / -3).toString() + " %-3=" + (a % -3).toString() +
    " /7=" + (a / 7).toString() + " %7=" + (a % 7).toString() +
    " /10=" + (a / 10).toString() + " %10=" + (a % 10).toString() +
    " /16=" + (a / 16).toString() + " %16=" + (a % 16).toString() +
    " /-16=" + (a / -16).toString() + " /641=" + (a / 641).toString() +
    " %1000=" + (a % 1000).toString() + " /max=" + (a / 0x7fffffff).toString() +
    " /min=" + (a / -0x80000000).toString() + " %min=" + (a % -0x80000000).toString() +
    " u/3=" + (u / 3).toString() + " u%3=" + (u % 3).toString() +
    " u/7=" + (u / 7).toString() + " u/10=" + (u / 10).toString() +
    " u%10=" + (u % 10).toString() + " u/16=" + (u / 16).toString() +
    " u%16=" + (u % 16).toString() + " u/641=" + (u / 641).toString() +
    " u/0x80000001=" + (u / 0x80000001).toString();
}

function var64(a: i64, b: i64): string {
  let q = (a == -0x8000000000000000 && b == -1) ? a : a / b;
  let r = (a == -0x8000000000000000 && b == -1) ? 0 : a % b;
  return "v64 " + a.toString() + " " + b.toString() + " q=" + q.toString() + " r=" + r.toString() +
    " uq=" + (<u64>a / <u64>b).toString() + " ur=" + (<u64>a % <u64>b).toString() +
    " recon=" + (q * b + r == a ? "ok" : "BAD");
}

function const64(a: i64): string {
  let u = <u64>a;
  return "c64 " + a.toString() +
    " /3=" + (a / 3).toString() + " %3=" + (a % 3).toString() +
    " /10=" + (a / 10).toString() + " %10=" + (a % 10).toString() +
    " /-7=" + (a / -7).toString() + " %-7=" + (a % -7).toString() +
    " /1024=" + (a / 1024).toString() + " %1024=" + (a % 1024).toString() +
    " /1e9+7=" + (a / 1000000007).toString() + " %1e9+7=" + (a % 1000000007).toString() +
    " u/10=" + (u / 10).toString() + " u%10=" + (u % 10).toString() +
    " u/3=" + (u / 3).toString() + " u/2^32=" + (u / 0x100000000).toString() +
    " u%2^32=" + (u % 0x100000000).toString() + " u/big=" + (u / 0x8000000000000001).toString();
}

// Narrow types divide after extension: an i8 -128 / -1 is 128, which wraps
// back to -128 only when stored as i8.
function narrow(a: i32, b: i32): string {
  let a8 = <i8>a, b8 = <i8>b, a16 = <i16>a, b16 = <i16>b;
  let ua8 = <u8>a, ub8 = <u8>b;
  if (b8 == 0) b8 = 1;
  if (b16 == 0) b16 = 1;
  if (ub8 == 0) ub8 = 1;
  return "n " + a.toString() + " " + b.toString() +
    " i8/=" + (<i8>(a8 / b8)).toString() + " i8%=" + (<i8>(a8 % b8)).toString() +
    " i16/=" + (<i16>(a16 / b16)).toString() + " i16%=" + (<i16>(a16 % b16)).toString() +
    " u8/=" + (ua8 / ub8).toString() + " u8%=" + (ua8 % ub8).toString();
}

function run(): void {
  for (let i = 0; i < dividends.length; i++) {
    for (let j = 0; j < divisors.length; j++) print(var32(dividends[i], divisors[j]));
  }
  for (let i = 0; i < dividends.length; i++) print(const32(dividends[i]));
  for (let i = 0; i < dividends64.length; i++) {
    for (let j = 0; j < divisors64.length; j++) print(var64(dividends64[i], divisors64[j]));
  }
  for (let i = 0; i < dividends64.length; i++) print(const64(dividends64[i]));
  for (let i = 0; i < dividends.length; i += 2) {
    for (let j = 0; j < dividends.length; j += 3) print(narrow(dividends[i], dividends[j]));
  }
  // Decimal digits by repeated division, the commonest constant divide.
  let digits = "";
  let x: u64 = 0xfedcba9876543210;
  while (x != 0) { digits = String.fromCharCode(48 + <i32>(x % 10)) + digits; x /= 10; }
  print("digits " + digits + " " + (0xfedcba9876543210).toString());
  // gcd and modular exponentiation: remainder in a loop with both operands
  // changing.
  for (let i = 1; i < dividends.length; i++) {
    let a = abs(<i64>dividends[i]) + 1, b = abs(<i64>dividends[i - 1]) + 1;
    while (b != 0) { let t = a % b; a = b; b = t; }
    let base: u64 = <u64>dividends[i] & 0xffffff, e: u64 = <u64>(i * 977 + 3), m: u64 = 1000000007, acc: u64 = 1;
    base %= m;
    while (e != 0) { if (e & 1) acc = acc * base % m; base = base * base % m; e >>= 1; }
    print("gcd " + i.toString() + " " + a.toString() + " pow=" + acc.toString());
  }
}
run();
