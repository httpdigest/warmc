// i64 and u64 arithmetic: wrapping add, sub, mul and negate at 2^64, signed
// and unsigned comparisons, min/max, bitwise ops, the full 64x64->128 product
// built from 32-bit halves, and 64-bit values split into and joined from i32
// halves.  Operands are loaded from arrays so binaryen cannot fold them; the
// constant-operand pass exercises warmc's immediate and multiply-by-constant
// forms at 64 bits, where the constants do not fit an instruction's immediate.
import { print, hex64 } from "../wasi";

const vals: i64[] = [
  0, 1, -1, 2, -2, 10, -10, 0x7fffffff, 0x80000000, 0xffffffff, 0x100000000,
  -0x80000000, -0x80000001, 0x7fffffffffffffff, -0x8000000000000000,
  0x0123456789abcdef, -0x0123456789abcdef, 0x5555555555555555,
  1000000007, 3037000499, 3037000500, -3037000500, 0x00ff00ff00ff00ff,
];

function signedOps(a: i64, b: i64): string {
  return "s " + a.toString() + " " + b.toString() +
    " add=" + (a + b).toString() + " sub=" + (a - b).toString() +
    " mul=" + (a * b).toString() + " neg=" + (-a).toString() +
    " lt=" + (a < b ? "1" : "0") + " le=" + (a <= b ? "1" : "0") +
    " gt=" + (a > b ? "1" : "0") + " ge=" + (a >= b ? "1" : "0") +
    " eq=" + (a == b ? "1" : "0") +
    " and=" + hex64(a & b) + " or=" + hex64(a | b) + " xor=" + hex64(a ^ b) +
    " not=" + hex64(~a) + " min=" + min(a, b).toString() + " max=" + max(a, b).toString();
}

function unsignedOps(a: u64, b: u64): string {
  return "u " + a.toString() + " " + b.toString() +
    " add=" + (a + b).toString() + " sub=" + (a - b).toString() +
    " mul=" + (a * b).toString() +
    " lt=" + (a < b ? "1" : "0") + " le=" + (a <= b ? "1" : "0") +
    " gt=" + (a > b ? "1" : "0") + " ge=" + (a >= b ? "1" : "0") +
    " min=" + min(a, b).toString() + " max=" + max(a, b).toString();
}

function constOps(a: i64): string {
  let u = <u64>a;
  return "c " + a.toString() +
    " +1=" + (a + 1).toString() + " +2^32=" + (a + 0x100000000).toString() +
    " -big=" + (a - 0x123456789abcdef0).toString() +
    " *3=" + (a * 3).toString() + " *10=" + (a * 10).toString() +
    " *-7=" + (a * -7).toString() + " *2^33=" + (a * 0x200000000).toString() +
    " *golden=" + (a * <i64>0x9e3779b97f4a7c15).toString() +
    " &low=" + hex64(a & 0xffffffff) + " &high=" + hex64(a & <i64>0xffffffff00000000) +
    " |bit40=" + hex64(a | (<i64>1 << 40)) +
    " u<2^32=" + (u < 0x100000000 ? "1" : "0") + " s<0=" + (a < 0 ? "1" : "0") +
    " s>=-2^31=" + (a >= -0x80000000 ? "1" : "0") +
    " abs=" + abs(a).toString() + " eqz=" + (a == 0 ? "1" : "0");
}

// 64x64 -> 128 by schoolbook on 32-bit halves, unsigned and signed.
function mul128(a: u64, b: u64): string {
  let a0 = a & 0xffffffff, a1 = a >> 32;
  let b0 = b & 0xffffffff, b1 = b >> 32;
  let p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
  let mid = (p00 >> 32) + (p01 & 0xffffffff) + (p10 & 0xffffffff);
  let hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
  let lo = (mid << 32) | (p00 & 0xffffffff);
  // signed high = unsigned high minus the corrections for negative operands
  let shi = hi - (<i64>a < 0 ? b : 0) - (<i64>b < 0 ? a : 0);
  return "mul128 " + hex64(a) + " " + hex64(b) + " hi=" + hex64(hi) + " lo=" + hex64(lo) +
    " shi=" + hex64(shi) + " check=" + (lo == a * b ? "ok" : "BAD");
}

// Splitting and joining halves, the pattern every 64-bit value through a
// 32-bit API goes through.
function halves(a: i64): string {
  let lo = <i32>a;
  let hi = <i32>(a >> 32);
  let ulo = <u32>a;
  let back = (<i64>hi << 32) | <i64>ulo;
  let wrong = (<i64>hi << 32) | <i64>lo; // sign-extends lo: differs when lo < 0
  return "halves " + a.toString() + " lo=" + lo.toString() + " hi=" + hi.toString() +
    " back=" + (back == a ? "same" : "DIFF") + " sx=" + wrong.toString();
}

function run(): void {
  let n = vals.length;
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < n; j += 3) print(signedOps(vals[i], vals[j]));
  }
  for (let i = 0; i < n; i += 2) {
    for (let j = 1; j < n; j += 3) print(unsignedOps(<u64>vals[i], <u64>vals[j]));
  }
  for (let i = 0; i < n; i++) print(constOps(vals[i]));
  for (let i = 0; i < n; i += 2) {
    for (let j = 0; j < n; j += 4) print(mul128(<u64>vals[i], <u64>vals[j]));
  }
  for (let i = 0; i < n; i++) print(halves(vals[i]));

  // xorshift64* and a 64-bit FNV-1a over its output: long chains of wrapping
  // multiplies by 64-bit constants.
  let x: u64 = 0x9e3779b97f4a7c15;
  let h: u64 = 0xcbf29ce484222325;
  for (let k = 0; k < 96; k++) {
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    let r = x * 0x2545f4914f6cdd1d;
    for (let b = 0; b < 8; b++) {
      h ^= (r >> (<u64>b * 8)) & 0xff;
      h *= 0x100000001b3;
    }
    if ((k & 7) == 0) print("xs " + k.toString() + " r=" + hex64(r) + " h=" + hex64(h));
  }
  // Accumulate signed products that overflow and compare with the same sum
  // done in u64: the bit patterns must agree.
  let ss: i64 = 0;
  let us: u64 = 0;
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < n; j++) {
      ss += vals[i] * vals[j] - (vals[j] ^ (<i64>i << 50));
      us += <u64>vals[i] * <u64>vals[j] - (<u64>vals[j] ^ (<u64>i << 50));
    }
    print("acc " + i.toString() + " " + ss.toString() + " " + us.toString() +
      " " + (<u64>ss == us ? "agree" : "DISAGREE"));
  }
}
run();
