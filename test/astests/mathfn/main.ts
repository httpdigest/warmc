// Math, AssemblyScript's f64 libm (a port of musl compiled into the module):
// sin/cos/tan, asin/acos/atan/atan2, sinh/cosh/tanh, asinh/acosh/atanh,
// exp/expm1/exp2, log/log1p/log2/log10, pow, cbrt, hypot, fround, clz32,
// imul, round, sign and f64 `%` (an fmod, also compiled in), and the
// range-reduction paths each one takes for tiny, ordinary, huge, negative,
// infinite and NaN arguments.  Each function is long straight-line f64 code
// full of bit manipulation (reinterpret to i64, shifts, masks) and polynomial
// evaluation, so a miscompile anywhere in warmc's float or i64 paths changes
// some bit of some result.  Printed as bits, with a NaN result printed as
// "nan" (its sign and payload are not fixed by the spec).
import { print, bits, canon64 } from "../wasi";

const xs: f64[] = [
  0.0, -0.0, 1e-310, 1e-20, 0.25, 0.5, -0.5, 0.75, 1.0, -1.0, 1.5, 2.0,
  Math.PI / 4, Math.PI / 2, Math.PI, -Math.PI, 3.0, 10.0, -10.0, 100.0,
  709.0, 710.0, -745.0, 1e10, 1e22, 1e300, Infinity, -Infinity, NaN,
];

function one(x: f64): string {
  return "x " + bits(x) +
    " sin" + bits(Math.sin(x)) + " cos" + bits(Math.cos(x)) + " tan" + bits(Math.tan(x)) +
    " asin" + bits(Math.asin(x)) + " acos" + bits(Math.acos(x)) + " atan" + bits(Math.atan(x)) +
    " sinh" + bits(Math.sinh(x)) + " cosh" + bits(Math.cosh(x)) + " tanh" + bits(Math.tanh(x)) +
    " asinh" + bits(Math.asinh(x)) + " acosh" + bits(Math.acosh(x)) + " atanh" + bits(Math.atanh(x));
}

function two(x: f64): string {
  return "y " + bits(x) +
    " exp" + bits(Math.exp(x)) + " expm1" + bits(Math.expm1(x)) + " exp2" + bits(Math.exp2(x)) +
    " log" + bits(Math.log(x)) + " log1p" + bits(Math.log1p(x)) + " log2" + bits(Math.log2(x)) +
    " log10" + bits(Math.log10(x)) + " cbrt" + bits(Math.cbrt(x)) +
    " round" + bits(Math.round(x)) + " fround" + bits(Math.fround(x)) +
    " clz32 " + Math.clz32(x).toString() + " sign" + bits(Math.sign(x));
}

function pairs(x: f64, y: f64): string {
  return "p " + bits(x) + " " + bits(y) + " pow" + bits(Math.pow(x, y)) +
    " atan2" + bits(Math.atan2(x, y)) + " hypot" + bits(Math.hypot(x, y)) +
    " rem" + bits(x % y) + " imul " + Math.imul(x, y).toString() +
    " max" + bits(Math.max(x, y)) + " min" + bits(Math.min(x, y));
}

function run(): void {
  for (let i = 0; i < xs.length; i++) print(one(xs[i]));
  for (let i = 0; i < xs.length; i++) print(two(xs[i]));
  for (let i = 0; i < xs.length; i += 2) {
    for (let j = 1; j < xs.length; j += 3) print(pairs(xs[i], xs[j]));
  }
  // pow corner cases from the IEEE/C99 table.
  const pb: f64[] = [-1.0, 1.0, -0.0, 0.0, 2.0, -2.0, 0.5, -8.0, Infinity, -Infinity, NaN];
  const pe: f64[] = [0.0, -0.0, 1.0, -1.0, 0.5, 3.0, -3.0, 2.0, 1e300, Infinity, -Infinity, NaN, 1.0 / 3.0];
  for (let i = 0; i < pb.length; i++) {
    let line = "pow " + bits(pb[i]);
    for (let j = 0; j < pe.length; j++) line += " " + bits(Math.pow(pb[i], pe[j]));
    print(line);
  }
  // A sweep through sin/cos/exp/log on a fine grid, folded into a checksum
  // per decade, so the many paths between the listed points are hit too.
  let x = -20.0;
  let acc: i64 = 0;
  let k = 0;
  while (x < 20.0) {
    let v = Math.sin(x) + Math.cos(x * 1.5) + Math.exp(x * 0.25) - Math.log(Math.abs(x) + 1e-3) +
      Math.atan(x) * Math.tanh(x * 0.5) + Math.cbrt(x) + Math.pow(Math.abs(x) + 0.5, 1.25);
    acc = acc * 31 + <i64>canon64(v);
    x += 0.0137;
    k++;
    if (k % 300 == 0) print("sweep " + k.toString() + " " + bits(v) + " " + acc.toString());
  }
  print("sweep end " + k.toString() + " " + acc.toString());
  // Large-argument trig: the Payne-Hanek path.
  const big: f64[] = [1e6, 1e15, 3.141592653589793e20, 1.2345678e100, 1e308, -7e200];
  for (let i = 0; i < big.length; i++) {
    print("big " + bits(big[i]) + " " + bits(Math.sin(big[i])) + " " + bits(Math.cos(big[i])) +
      " " + bits(Math.tan(big[i])));
  }
}
run();
