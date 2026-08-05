// f64 dot product over two Float64Arrays: the shape a vectoriser should take,
// with the accumulator dependence that stops it unless reassociation is
// allowed, and two checked accesses per iteration rather than one.
//
// Prints the accumulator's bits, so a one-ulp difference in the order of the
// adds is a failure here rather than a rounding footnote.
import { print, bits } from "../wasi";

function dot(a: Float64Array, b: Float64Array): f64 {
  let s: f64 = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * b[i];
  return s;
}

let n = 2048;
let a = new Float64Array(n);
let b = new Float64Array(n);
for (let i = 0; i < n; i++) {
  a[i] = <f64>i * 0.5;
  b[i] = 1.0 / (<f64>i + 1.0);
}
let s: f64 = 0;
for (let r = 0; r < 20000; r++) {
  a[r & 2047] = <f64>r;
  s += dot(a, b);
}
print("s = " + bits(s));
