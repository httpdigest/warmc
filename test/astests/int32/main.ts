// i32 and u32 arithmetic: add, sub, mul and negate wrapping at 2^32, signed
// and unsigned comparisons of the same bit patterns, min/max/abs, the
// overflow-checking idioms people write by hand, and multiply-high built from
// i64.  Every operand comes out of an array, so binaryen cannot fold the
// operations away and warmc has to emit each one, with both operands in
// registers and, in a second pass, with the right operand a constant (which
// takes the immediate forms and the strength-reduced multiplies).
import { print, hex32 } from "../wasi";

const vals: i32[] = [
  0, 1, -1, 2, -2, 3, 7, -7, 100, -100, 0x7fff, 0x8000, 0xffff, 0x10000,
  0x7fffffff, -0x80000000, 0x7ffffffe, -0x7fffffff, 0x55555555, -0x55555556,
  123456789, -987654321, 46341, -46341, 65537,
];

function signedOps(a: i32, b: i32): string {
  return "s " + a.toString() + " " + b.toString() +
    " add=" + (a + b).toString() + " sub=" + (a - b).toString() +
    " mul=" + (a * b).toString() + " neg=" + (-a).toString() +
    " lt=" + (a < b ? "1" : "0") + " le=" + (a <= b ? "1" : "0") +
    " gt=" + (a > b ? "1" : "0") + " ge=" + (a >= b ? "1" : "0") +
    " eq=" + (a == b ? "1" : "0") + " ne=" + (a != b ? "1" : "0") +
    " and=" + hex32(a & b) + " or=" + hex32(a | b) + " xor=" + hex32(a ^ b) +
    " not=" + hex32(~a) + " min=" + min(a, b).toString() +
    " max=" + max(a, b).toString();
}

function unsignedOps(a: u32, b: u32): string {
  return "u " + a.toString() + " " + b.toString() +
    " add=" + (a + b).toString() + " sub=" + (a - b).toString() +
    " mul=" + (a * b).toString() + " neg=" + (0 - a).toString() +
    " lt=" + (a < b ? "1" : "0") + " le=" + (a <= b ? "1" : "0") +
    " gt=" + (a > b ? "1" : "0") + " ge=" + (a >= b ? "1" : "0") +
    " min=" + min(a, b).toString() + " max=" + max(a, b).toString();
}

// Constant right operands: each line is one value through a fixed chain of
// immediate operations, the shapes a code generator special-cases.
function constOps(a: i32): string {
  let u = <u32>a;
  return "c " + a.toString() +
    " +1=" + (a + 1).toString() + " -1=" + (a - 1).toString() +
    " +big=" + (a + 0x12345678).toString() + " -big=" + (a - 0x7fffffff).toString() +
    " *3=" + (a * 3).toString() + " *5=" + (a * 5).toString() +
    " *7=" + (a * 7).toString() + " *9=" + (a * 9).toString() +
    " *-1=" + (a * -1).toString() + " *-8=" + (a * -8).toString() +
    " *1000=" + (a * 1000).toString() + " *65536=" + (a * 65536).toString() +
    " *0x10001=" + (a * 0x10001).toString() +
    " &ff=" + (a & 0xff).toString() + " |f0f0=" + hex32(a | 0xf0f0) +
    " ^mask=" + hex32(a ^ 0xaaaaaaaa) +
    " u<1000=" + (u < 1000 ? "1" : "0") + " s<1000=" + (a < 1000 ? "1" : "0") +
    " u>=0x80000000=" + (u >= 0x80000000 ? "1" : "0") +
    " s>-5=" + (a > -5 ? "1" : "0") +
    " abs=" + abs(a).toString() + " eqz=" + (a == 0 ? "1" : "0");
}

// The multiply-high a 64-bit product gives, signed and unsigned.
function mulhi(a: i32, b: i32): string {
  let s = <i64>a * <i64>b;
  let u = <u64><u32>a * <u64><u32>b;
  return "mulhi " + a.toString() + " " + b.toString() +
    " s=" + (<i32>(s >> 32)).toString() + " u=" + (<u32>(u >> 32)).toString() +
    " lo=" + (<i32>s).toString();
}

// Overflow detection by hand: the result of the wrapping add compared with
// its operands, which is how AS code (with no checked arithmetic) does it.
function addOverflows(a: i32, b: i32): bool {
  let r = a + b;
  return ((a ^ r) & (b ^ r)) < 0;
}
function uaddOverflows(a: u32, b: u32): bool {
  return a + b < a;
}
function mulOverflows(a: i32, b: i32): bool {
  let w = <i64>a * <i64>b;
  return w != <i64><i32>w;
}

function run(): void {
  let n = vals.length;
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < n; j += 3) {
      print(signedOps(vals[i], vals[j]));
    }
  }
  for (let i = 0; i < n; i += 2) {
    for (let j = 1; j < n; j += 4) {
      print(unsignedOps(<u32>vals[i], <u32>vals[j]));
    }
  }
  for (let i = 0; i < n; i++) print(constOps(vals[i]));
  for (let i = 0; i < n; i += 2) {
    for (let j = 0; j < n; j += 5) print(mulhi(vals[i], vals[j]));
  }
  let ov = "";
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < n; j++) {
      ov += (addOverflows(vals[i], vals[j]) ? "a" : "-") +
            (uaddOverflows(<u32>vals[i], <u32>vals[j]) ? "u" : "-") +
            (mulOverflows(vals[i], vals[j]) ? "m" : "-");
    }
    print("ovf " + vals[i].toString() + " " + ov);
    ov = "";
  }
  // A long wrapping chain: a linear congruential generator and a running sum
  // whose every step overflows.
  let x: u32 = 1;
  let sum: i32 = 0;
  for (let k = 0; k < 64; k++) {
    x = x * 1664525 + 1013904223;
    sum += <i32>x;
    sum = sum * 31 - <i32>(x >>> 7);
    if ((k & 7) == 7) print("lcg " + k.toString() + " x=" + x.toString() + " sum=" + sum.toString());
  }
}
run();
