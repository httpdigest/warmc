// Mathf, the f32 libm: sin/cos/tan, asin/acos/atan/atan2, sinh/cosh/tanh,
// exp/expm1/exp2, log/log1p/log2/log10, pow, cbrt, hypot, round, sign and
// f32 `%`.  It is not Math at a narrower type: most of it promotes to f64,
// evaluates there and demotes, and the trig keeps an i64 path for large
// arguments, so one call mixes f32, f64 and integer code and converts
// between them repeatedly.  Results are printed as f32 bits; a one-ulp
// difference from a double rounding (computing in f64 where f32 was meant,
// or the reverse) shows.  A NaN result prints as "nan", since the spec does
// not fix its sign or payload.
import { print, bits32, canon32 } from "../wasi";

const xs: f32[] = [
  0.0, -0.0, 1e-40, 1e-10, 0.25, 0.5, -0.5, 0.75, 1.0, -1.0, 1.5, 2.0,
  0.7853982, 1.5707964, 3.1415927, -3.1415927, 3.0, 10.0, -10.0, 88.0, 89.0,
  -104.0, 1e6, 1e20, 3.4e38, Infinity, -Infinity, NaN,
];

function one(x: f32): string {
  return "x " + bits32(x) +
    " sin" + bits32(Mathf.sin(x)) + " cos" + bits32(Mathf.cos(x)) + " tan" + bits32(Mathf.tan(x)) +
    " asin" + bits32(Mathf.asin(x)) + " acos" + bits32(Mathf.acos(x)) + " atan" + bits32(Mathf.atan(x)) +
    " sinh" + bits32(Mathf.sinh(x)) + " cosh" + bits32(Mathf.cosh(x)) + " tanh" + bits32(Mathf.tanh(x)) +
    " asinh" + bits32(Mathf.asinh(x)) + " acosh" + bits32(Mathf.acosh(x)) + " atanh" + bits32(Mathf.atanh(x));
}

function two(x: f32): string {
  return "y " + bits32(x) +
    " exp" + bits32(Mathf.exp(x)) + " expm1" + bits32(Mathf.expm1(x)) + " exp2" + bits32(Mathf.exp2(x)) +
    " log" + bits32(Mathf.log(x)) + " log1p" + bits32(Mathf.log1p(x)) + " log2" + bits32(Mathf.log2(x)) +
    " log10" + bits32(Mathf.log10(x)) + " cbrt" + bits32(Mathf.cbrt(x)) +
    " round" + bits32(Mathf.round(x)) + " sign" + bits32(Mathf.sign(x)) +
    " sqrt" + bits32(Mathf.sqrt(x));
}

function pairs(x: f32, y: f32): string {
  return "p " + bits32(x) + " " + bits32(y) + " pow" + bits32(Mathf.pow(x, y)) +
    " atan2" + bits32(Mathf.atan2(x, y)) + " hypot" + bits32(Mathf.hypot(x, y)) +
    " rem" + bits32(x % y) + " max" + bits32(Mathf.max(x, y)) + " min" + bits32(Mathf.min(x, y));
}

function run(): void {
  for (let i = 0; i < xs.length; i++) print(one(xs[i]));
  for (let i = 0; i < xs.length; i++) print(two(xs[i]));
  for (let i = 0; i < xs.length; i += 2) {
    for (let j = 1; j < xs.length; j += 3) print(pairs(xs[i], xs[j]));
  }
  // A grid sweep folded into checksums.
  let x: f32 = -12.0;
  let acc: i32 = 0;
  let k = 0;
  while (x < 12.0) {
    let v = Mathf.sin(x) * Mathf.cos(x) + Mathf.exp(x * <f32>0.3) - Mathf.log(Mathf.abs(x) + <f32>0.01) +
      Mathf.atan(x) + Mathf.cbrt(x) + Mathf.pow(Mathf.abs(x) + <f32>0.5, <f32>1.5) + Mathf.tanh(x);
    acc = acc * 31 + <i32>canon32(v);
    x += <f32>0.0173;
    k++;
    if (k % 100 == 0) print("sweep " + k.toString() + " " + bits32(v) + " " + acc.toString());
  }
  print("sweep end " + k.toString() + " " + acc.toString());
  const big: f32[] = [1e5, 3e7, 1.2345e20, 3.4e38, -9.87e30];
  for (let i = 0; i < big.length; i++) {
    print("big " + bits32(big[i]) + " " + bits32(Mathf.sin(big[i])) + " " + bits32(Mathf.cos(big[i])) +
      " " + bits32(Mathf.tan(big[i])));
  }
}
run();
