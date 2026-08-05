// Array<i32>#sort with a comparator: an indirect call per comparison and a
// store per exchange, which is what most real sorting looks like.  The
// comparator is a subtraction rather than a compare so the call is all that is
// left to measure.  gobench/sortints is sort.Slice over the same shape.
import { print } from "../wasi";

let n = 4096;
let total = 0;
for (let r = 0; r < 650; r++) {
  let a = new Array<i32>(n);
  let x: u32 = <u32>r * 2654435761 + 12345;
  for (let i = 0; i < n; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    a[i] = <i32>(x & 0xffffff);
  }
  a.sort((p: i32, q: i32) => p - q);
  total += a[0] + a[n >> 1] + a[n - 1];
}
print("total = " + total.toString());
