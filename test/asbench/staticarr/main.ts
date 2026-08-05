// StaticArray<i32> against the same loop over Array<i32>.  A StaticArray is
// the elements and nothing else -- no separate backing buffer, no length
// indirection -- so the pair of loops in here differ by one load per access
// and by what the bounds check has to read.  Both are idiomatic
// AssemblyScript; which one a program uses is a real decision, and this says
// what it is worth.
import { print } from "../wasi";

const N = 4096;

function overStatic(a: StaticArray<i32>): i32 {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i];
  return s;
}

function overArray(a: Array<i32>): i32 {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i];
  return s;
}

let sa = new StaticArray<i32>(N);
let da = new Array<i32>(N);
for (let i = 0; i < N; i++) { sa[i] = i * 3; da[i] = i * 3; }

let s = 0;
let d = 0;
for (let r = 0; r < 12000; r++) {
  sa[r & (N - 1)] = r;
  da[r & (N - 1)] = r;
  s += overStatic(sa);
  d += overArray(da);
}
print("s = " + s.toString() + " d = " + d.toString() + " same = " + (s == d ? "1" : "0"));
