// An Array<T> of references, filled and rescanned.  Every store into it is a
// reference store into a managed object, which is the one operation the
// incremental collector charges for: the barrier has to record the old value
// so a concurrent mark cannot lose it.  This is the kernel where that cost is
// the whole loop rather than a fraction of it.
import { print } from "../wasi";

class Cell { constructor(public v: i32) {} }

let n = 4096;
let a = new Array<Cell>(n);
for (let i = 0; i < n; i++) a[i] = new Cell(i);
let total = 0;
for (let r = 0; r < 900; r++) {
  for (let i = 0; i < n; i++) a[i] = new Cell(i ^ r);
  for (let i = 0; i < n; i++) total += a[i].v & 15;
}
print("total = " + total.toString());
