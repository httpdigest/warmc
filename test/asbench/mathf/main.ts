// Mathf: AssemblyScript's single-precision libm -- sin, cos, exp, log, pow,
// atan and sqrt -- applied across a Float32Array.  Like Math it is compiled
// into the module rather than imported, but it is not Math at a narrower type.
// sin and cos stay out of line and do their work in f64 (promote, reduce,
// polynomial, demote, with an i64 path for large arguments); exp, log, pow
// and atan are inlined into the loop, mostly as f64 arithmetic on promoted
// arguments; sqrt is a single f32.sqrt.  So one iteration mixes f32 and f64
// arithmetic and converts between them many times over.
//
// mathfn is the f64 version.  The f32 accumulators are printed as bits
// (widened to f64, which is exact), so a one-ulp disagreement in any f32
// rounding or any promote/demote is a failure rather than a footnote.
import { print, bits } from "../wasi";

const N = 1024;
let xs = new Float32Array(N);
let ys = new Float32Array(N);
for (let i = 0; i < N; i++) xs[i] = <f32>(i + 1) * 0.003;

let s: f32 = 0;
let t: f32 = 0;
for (let r = 0; r < 10000; r++) {
  xs[r & (N - 1)] = <f32>(r & 4095) * 0.0007 + 0.01;
  for (let i = 0; i < N; i++) {
    let x = unchecked(xs[i]);
    let y = Mathf.sin(x) * Mathf.cos(x) + Mathf.sqrt(x) - Mathf.log(x + 1.0);
    y += Mathf.exp(-x) + Mathf.pow(x, 1.5) + Mathf.atan(x);
    unchecked(ys[i] = y);
  }
  let acc: f32 = 0;
  for (let i = 0; i < N; i++) acc += unchecked(ys[i]);
  s += acc * 0.0001;
  t = Mathf.max(t, Mathf.abs(ys[r & (N - 1)] - Mathf.floor(ys[r & (N - 1)])));
}
print("s = " + bits(<f64>s) + " t = " + bits(<f64>t));
