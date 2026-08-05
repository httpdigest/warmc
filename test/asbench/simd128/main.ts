// Explicit v128, written by hand through AssemblyScript's SIMD builtins rather
// than produced by a vectoriser.  That is the point: every other vector test in
// the tree gets its v128 from clang or from warmc's own vectoriser, and this
// one gets it from binaryen, which chooses different lane operations and
// different shuffles for the same arithmetic.
import { print } from "../wasi";

let n = 4096;
let a = new Int32Array(n);
let b = new Int32Array(n);
for (let i = 0; i < n; i++) { a[i] = i * 3; b[i] = i ^ 0x5a5a; }

function vsum(a: Int32Array, b: Int32Array): i32 {
  let acc = i32x4.splat(0);
  let pa = a.dataStart, pb = b.dataStart;
  for (let i = 0; i < a.length; i += 4) {
    let va = v128.load(pa + (<usize>i << 2));
    let vb = v128.load(pb + (<usize>i << 2));
    let m = i32x4.max_s(va, i32x4.sub(vb, va));
    acc = i32x4.add(acc, i32x4.mul(m, i32x4.splat(3)));
  }
  return i32x4.extract_lane(acc, 0) + i32x4.extract_lane(acc, 1) +
         i32x4.extract_lane(acc, 2) + i32x4.extract_lane(acc, 3);
}

let total = 0;
for (let r = 0; r < 260000; r++) {
  a[r & (n - 1)] = r;
  total += vsum(a, b);
}
print("total = " + total.toString());
