// @unmanaged classes: plain structs in linear memory with no object header and
// no collector, allocated once with heap.alloc and addressed as
// changetype<Body>(base + i * size).  A field access is a single f64.load or
// i32.load at a constant offset from the computed address -- no shadow-stack
// spill, no bounds check, no write barrier on the `next` pointer -- and
// load<T>/store<T> with an immediate offset reach the same memory by hand.  The
// layout is packed: offsetof<Body>() is 36, so every other record's f64 fields
// are only 4-byte aligned.
//
// It is the low-level style AssemblyScript code drops into when the managed
// runtime is in the way: a damped-spring update over the array in address
// order, then a 256-step pointer chase through the `next` links.  Most kernels
// here pay the shadow stack around every managed reference; this one shows the
// same kind of loop without it.
import { print, bits } from "../wasi";

@unmanaged
class Body {
  x: f64;
  v: f64;
  k: f64;
  next: Body;
  id: i32;
  hits: i32;
}

const N = 2048;
const SZ = offsetof<Body>();
let base = heap.alloc(<usize>N * SZ);

@inline function body(i: i32): Body {
  return changetype<Body>(base + <usize>i * SZ);
}

// Each body links to another through a raw pointer field, in an order that is
// a fixed permutation of the array: a list with no collector anywhere near it.
for (let i = 0; i < N; i++) {
  let b = body(i);
  b.x = <f64>(i & 63) * 0.25;
  b.v = 0;
  b.k = 0.001 + <f64>(i & 7) * 0.0005;
  b.id = i;
  b.hits = 0;
  b.next = body(<i32>((<u32>i * 1103 + 17) & (N - 1)));
}

let walk: i64 = 0;
let energy: f64 = 0;
for (let r = 0; r < 80000; r++) {
  // A spring step over the array in address order.
  for (let i = 0; i < N; i++) {
    let b = body(i);
    let a = -b.k * b.x - 0.002 * b.v;
    b.v += a;
    b.x += b.v;
  }
  // Raw word loads and stores through the same memory, by offset.
  let p = base + <usize>(r & (N - 1)) * SZ;
  store<i32>(p, load<i32>(p, offsetof<Body>("hits")) + 1, offsetof<Body>("hits"));
  // Chase the list from a different start each round.
  let c = body((r * 7) & (N - 1));
  for (let j = 0; j < 256; j++) {
    walk += <i64>c.id + <i64>c.hits;
    c = c.next;
  }
}
for (let i = 0; i < N; i++) energy += body(i).x * body(i).x + body(i).v;
print("walk = " + walk.toString() + " e = " + bits(energy));
