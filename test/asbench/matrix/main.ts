// 64x64 f64 matrix multiply, three nested loops over a flat Float64Array.
// The inner loop's b access is strided by n, which is the access pattern that
// decides whether the bounds check can be hoisted and whether anything can be
// vectorised.
import { print, bits } from "../wasi";

const N = 64;

function mul(c: Float64Array, a: Float64Array, b: Float64Array): void {
  for (let i = 0; i < N; i++) {
    for (let j = 0; j < N; j++) {
      let s: f64 = 0;
      for (let k = 0; k < N; k++) {
        s += unchecked(a[i * N + k]) * unchecked(b[k * N + j]);
      }
      unchecked(c[i * N + j] = s);
    }
  }
}

let a = new Float64Array(N * N);
let b = new Float64Array(N * N);
let c = new Float64Array(N * N);
for (let i = 0; i < N * N; i++) {
  a[i] = <f64>((i * 7) % 13) * 0.25;
  b[i] = <f64>((i * 11) % 17) * 0.125;
}
let s: f64 = 0;
for (let r = 0; r < 250; r++) {
  a[r % (N * N)] = <f64>r;
  mul(c, a, b);
  s += c[0] + c[N * N - 1];
}
print("s = " + bits(s));
