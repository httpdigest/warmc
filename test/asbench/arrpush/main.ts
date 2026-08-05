// Array#push from empty, so the backing buffer doubles as it goes: a bounds
// test, a capacity test, a store, and every so often an allocate-and-copy of
// everything so far.  gobench/appendgrow is the same shape through Go's
// `append`, and the two allocators behave differently under it.
import { print } from "../wasi";

let total = 0;
for (let r = 0; r < 20000; r++) {
  let a = new Array<i32>();
  for (let i = 0; i < 2000; i++) a.push(i ^ r);
  total += a.length + a[0] + a[1999];
}
print("total = " + total.toString());
