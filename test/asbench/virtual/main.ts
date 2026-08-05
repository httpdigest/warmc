// Virtual dispatch over a heterogeneous array: the call target is decided by
// the receiver's runtime type, so every one is an indirect call through the
// function table and none of them can be devirtualised.  gobench/iface is the
// same shape through a Go interface.
import { print } from "../wasi";

abstract class Shape { abstract area(): i32; }
class Sq extends Shape { constructor(public s: i32) { super(); } area(): i32 { return this.s * this.s; } }
class Rect extends Shape { constructor(public w: i32, public h: i32) { super(); } area(): i32 { return this.w * this.h; } }
class Tri extends Shape { constructor(public b: i32, public h: i32) { super(); } area(): i32 { return (this.b * this.h) >> 1; } }

let n = 1024;
let shapes = new Array<Shape>(n);
for (let i = 0; i < n; i++) {
  if (i % 3 == 0) shapes[i] = new Sq(i & 31);
  else if (i % 3 == 1) shapes[i] = new Rect(i & 15, i & 7);
  else shapes[i] = new Tri(i & 63, i & 3);
}
let total = 0;
for (let r = 0; r < 45000; r++) {
  for (let i = 0; i < n; i++) total += shapes[i].area();
  total ^= r;
}
print("total = " + total.toString());
