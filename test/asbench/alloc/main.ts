// Allocating short-lived objects and dropping them.  AssemblyScript's
// incremental collector is compiled into the module, so unlike a host-provided
// GC every part of this -- the TLSF free-list walk, the mark step interleaved
// with the mutator, the write barrier -- is wasm that warmc compiled.  That
// makes it a regime none of the C or Go benchmarks reach.
//
// The escape is real: `keep` holds one of them, so nothing here is an
// allocation a compiler is allowed to delete.
import { print } from "../wasi";

class Point { constructor(public x: i32, public y: i32) {} }

let keep: Point = new Point(0, 0);
let total = 0;
for (let r = 0; r < 5000000; r++) {
  let p = new Point(r, r * 3);
  total += p.x ^ p.y;
  if ((r & 8191) == 0) keep = p;
}
print("total = " + total.toString() + " keep = " + (keep.x + keep.y).toString());
