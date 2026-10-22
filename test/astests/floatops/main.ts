// f32 and f64 arithmetic and the IEEE corners: add/sub/mul/div rounding,
// signed zeros (1/-0 is -inf, -0 == 0 but its bits differ), infinities and
// NaN through every operator, comparisons with NaN all false (and != true),
// min/max (wasm's propagate NaN and order -0 below +0, unlike arm64's fmaxnm
// and unlike a compare-and-select), copysign, abs and neg (bit operations
// that keep a NaN's payload), sqrt, floor/ceil/trunc/nearest with halfway
// cases (nearest is ties-to-even), and isNaN/isFinite.  Every result is
// printed as its bits, so a wrong rounding or a lost sign of zero is a diff --
// except a NaN from arithmetic, whose sign and payload the spec leaves open,
// which prints as "nan".  The inputs, and neg/abs/copysign of them, keep their
// exact bits (rawbits): the spec fixes those, NaN payloads included.
// f32 arithmetic must round at f32 after every operation, not once at the end.
import { print, bits, bits32, rawbits, rawbits32 } from "../wasi";

const d: f64[] = [
  0.0, -0.0, 1.0, -1.0, 0.5, -2.5, 3.0, 1e-300, -1e300, 1.7976931348623157e308,
  4.9e-324, 2.2250738585072014e-308, Infinity, -Infinity, NaN, 0.1, 1.0 / 3.0,
  2.5, 3.5, -0.5, 1e16, 9007199254740993.0, 123.456,
];
const f: f32[] = [
  0.0, -0.0, 1.0, -1.0, 0.5, -2.5, 3.0, 1e-38, -3.4e38, 3.4028234e38, 1.4e-45,
  Infinity, -Infinity, NaN, 0.1, 0.3333333, 2.5, 3.5, -0.5, 16777217.0, 123.456,
];

function bool01(b: bool): string { return b ? "1" : "0"; }

function ops64(a: f64, b: f64): string {
  return "d " + rawbits(a) + " " + rawbits(b) + " +" + bits(a + b) + " -" + bits(a - b) +
    " *" + bits(a * b) + " /" + bits(a / b) + " min" + bits(min(a, b)) + " max" + bits(max(a, b)) +
    " cs" + rawbits(copysign<f64>(a, b)) + " cmp" + bool01(a < b) + bool01(a <= b) + bool01(a > b) +
    bool01(a >= b) + bool01(a == b) + bool01(a != b) + bool01(!(a < b));
}

function ops32(a: f32, b: f32): string {
  return "f " + rawbits32(a) + " " + rawbits32(b) + " +" + bits32(a + b) + " -" + bits32(a - b) +
    " *" + bits32(a * b) + " /" + bits32(a / b) + " min" + bits32(min(a, b)) + " max" + bits32(max(a, b)) +
    " cs" + rawbits32(copysign<f32>(a, b)) + " cmp" + bool01(a < b) + bool01(a <= b) + bool01(a > b) +
    bool01(a >= b) + bool01(a == b) + bool01(a != b);
}

function unary64(a: f64): string {
  return "u64 " + rawbits(a) + " neg" + rawbits(-a) + " abs" + rawbits(Math.abs(a)) +
    " sqrt" + bits(Math.sqrt(a)) + " floor" + bits(Math.floor(a)) + " ceil" + bits(Math.ceil(a)) +
    " trunc" + bits(Math.trunc(a)) + " nearest" + bits(nearest(a)) +
    " isNaN" + bool01(isNaN(a)) + " isFinite" + bool01(isFinite(a)) +
    " self==" + bool01(a == a) + " sign" + bits(Math.sign(a)) + " signbit" + bool01(Math.signbit(a));
}

function unary32(a: f32): string {
  return "u32 " + rawbits32(a) + " neg" + rawbits32(-a) + " abs" + rawbits32(Mathf.abs(a)) +
    " sqrt" + bits32(Mathf.sqrt(a)) + " floor" + bits32(Mathf.floor(a)) + " ceil" + bits32(Mathf.ceil(a)) +
    " trunc" + bits32(Mathf.trunc(a)) + " nearest" + bits32(nearest(a)) +
    " isNaN" + bool01(isNaN(a)) + " isFinite" + bool01(isFinite(a)) + " sign" + bits32(Mathf.sign(a));
}

// Fused-looking expressions that must not be fused: a*b+c in wasm rounds the
// product first, and arm64's fmadd would not.
function noFma(a: f64, b: f64, c: f64): f64 { return a * b + c; }
function noFma32(a: f32, b: f32, c: f32): f32 { return a * b - c; }

function run(): void {
  for (let i = 0; i < d.length; i++) {
    for (let j = 0; j < d.length; j += 2) print(ops64(d[i], d[j]));
  }
  for (let i = 0; i < f.length; i++) {
    for (let j = 1; j < f.length; j += 2) print(ops32(f[i], f[j]));
  }
  for (let i = 0; i < d.length; i++) print(unary64(d[i]));
  for (let i = 0; i < f.length; i++) print(unary32(f[i]));
  // NaN payloads through abs/neg/copysign (bit operations, payload kept).
  let nan1 = reinterpret<f64>(<i64>0x7ff8000000012345);
  let nan2 = reinterpret<f64>(<i64>0xfff4000000000abc);
  let nanf = reinterpret<f32>(<i32>0x7fc01234);
  print("payload " + rawbits(-nan1) + " " + rawbits(Math.abs(nan2)) + " " + rawbits(copysign<f64>(nan1, -1.0)) +
    " " + rawbits32(-nanf) + " " + rawbits32(Mathf.abs(-nanf)));
  // Rounding: products and sums that round differently in f32 and f64,
  // and the no-fma check.
  let t: f64 = 1.0 + 1e-16 * 3.0;
  print("round " + bits(t) + " " + bits(0.1 + 0.2) + " " + bits(<f64>(<f32>0.1 + <f32>0.2)));
  for (let i = 0; i + 2 < d.length; i++) {
    print("fma " + bits(noFma(d[i], d[i + 1], d[i + 2])) + " " +
      bits(noFma(0.1 * <f64>i, 3.0000000000000004, -0.30000000000000004 * <f64>i)) + " " +
      bits32(noFma32(<f32>0.1 * <f32>i, <f32>3.0000002, <f32>0.3 * <f32>i)));
  }
  // Accumulation in f32 must round each step.
  let sf: f32 = 0, sd: f64 = 0;
  for (let i = 1; i <= 1000; i++) {
    sf += <f32>1.0 / <f32>i;
    sd += 1.0 / <f64>i;
    if (i % 100 == 0) print("harm " + i.toString() + " " + bits32(sf) + " " + bits(sd));
  }
  // Kahan summation: the compensation term only works if no step is
  // reassociated or contracted.
  let sum = 0.0, c = 0.0;
  for (let i = 0; i < 1000; i++) {
    let y = 0.1 * <f64>(i % 7) + 1e-10 - c;
    let tt = sum + y;
    c = (tt - sum) - y;
    sum = tt;
  }
  print("kahan " + bits(sum) + " " + bits(c));
}
run();
