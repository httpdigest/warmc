// A chain of `instanceof` tests, which is AssemblyScript's answer to a type
// switch: each one compares the object's runtime id against a range, so the
// cost is the chain's length and the branch predictor's opinion of it.
// gobench/typeswitch is the Go equivalent.
import { print } from "../wasi";

class Base { constructor(public v: i32) {} }
class A extends Base {}
class B extends Base {}
class C extends Base {}
class D extends Base {}

let n = 1024;
let a = new Array<Base>(n);
for (let i = 0; i < n; i++) {
  let m = i & 3;
  if (m == 0) a[i] = new A(i);
  else if (m == 1) a[i] = new B(i);
  else if (m == 2) a[i] = new C(i);
  else a[i] = new D(i);
}
let total = 0;
for (let r = 0; r < 90000; r++) {
  for (let i = 0; i < n; i++) {
    let o = a[i];
    if (o instanceof A) total += o.v;
    else if (o instanceof B) total += o.v * 2;
    else if (o instanceof C) total += o.v * 3;
    else if (o instanceof D) total += o.v * 5;
  }
}
print("total = " + total.toString());
