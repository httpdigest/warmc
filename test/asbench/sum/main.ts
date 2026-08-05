// A widening reduction: u32 elements into a u64 accumulator, over a
// Uint32Array, with the bounds check AssemblyScript emits on every access.
//
// Two comparisons live in this one kernel.  Against gobench/sum the arithmetic
// is identical and the wasm is not, so the gap between those rows is what
// binaryen's output costs relative to LLVM's.  Against asbench/sumraw the
// source differs by one `unchecked()`, so the gap between *those* rows is what
// the bounds check costs after warmc has had its go at eliminating it.
//
// The store into a[] each round is load-bearing.  Without it the reduction is
// loop-invariant and binaryen hoists the whole thing.
import { print } from "../wasi";

function sumAll(a: Uint32Array): u64 {
  let s: u64 = 0;
  for (let i = 0; i < a.length; i++) s += <u64>a[i];
  return s;
}

let a = new Uint32Array(4096);
for (let i = 0; i < a.length; i++) a[i] = <u32>i * 2654435761;
let s: u64 = 0;
for (let r = 0; r < 20000; r++) {
  a[r & 4095] = <u32>r;
  s += sumAll(a);
}
print("s = " + s.toString());
