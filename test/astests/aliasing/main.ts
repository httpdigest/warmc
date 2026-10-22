// Memory aliasing, the ground a compiler's load/store optimisations stand on:
// a store followed by a load of a different width or type at an overlapping
// address (store-to-load forwarding must extract the right bytes or give up),
// the same address reached through two different pointer expressions, two
// typed-array views and a DataView over one buffer, a store hidden behind a
// call or a memory.copy/fill between a store and the load that must see it,
// redundant-looking loads in a loop where the loop body writes the location
// (a load hoisted out of the loop would read a stale value), dead-looking
// stores that a later load or call does observe, and unmanaged structs laid
// over each other.  Each probe prints what the load returned.
import { print, hex32, hex64 } from "../wasi";

const B = memory.data(512, 16);

function poke(p: usize, v: u8): void { store<u8>(p, v); }
function poke64(p: usize, v: u64): void { store<u64>(p, v); }
function peek32(p: usize): u32 { return load<u32>(p); }

@unmanaged class Pair { a: i32; b: i32; }
@unmanaged class Wide { ab: i64; }

function forwarding(base: usize, x: u64): string {
  store<u64>(base, x);
  let lo = load<u32>(base), hi = load<u32>(base + 4);
  let mid = load<u32>(base + 2);
  let b3 = load<u8>(base + 3), s5 = load<i8>(base + 5), h7 = load<u16>(base + 7);
  let f = load<f64>(base);
  store<u16>(base + 3, 0xabcd);
  let after = load<u64>(base);
  store<f32>(base + 4, -1.5);
  let mixed = load<u64>(base);
  return "fwd " + hex64(x) + " lo=" + hex32(lo) + " hi=" + hex32(hi) + " mid=" + hex32(mid) + " b3=" + b3.toString() +
    " s5=" + s5.toString() + " h7=" + h7.toString() + " f=" + hex64(reinterpret<u64>(f)) + " after=" + hex64(after) + " mixed=" + hex64(mixed);
}

function twoPaths(base: usize, k: i32): string {
  let p = base + <usize>(k * 8);
  let q = (base | 0) + <usize>(k << 3);          // same address, different expression
  let r = base + <usize>(k * 8 + 4) - 4;         // and another
  store<i32>(p, 111);
  store<i32>(q, 222);
  let v1 = load<i32>(p);
  store<i32>(r, v1 + 1);
  let v2 = load<i32>(q);
  store<u8>(p + 1, 0x7f);
  let v3 = load<i32>(r);
  return "paths " + k.toString() + " " + v1.toString() + " " + v2.toString() + " " + hex32(v3);
}

function behindCalls(base: usize): string {
  store<u32>(base, 0x11223344);
  poke(base + 2, 0xee);                // callee writes a byte inside the word
  let a = load<u32>(base);
  store<u64>(base + 8, 0);
  poke64(base + 8, 0x0102030405060708);
  let b = load<u16>(base + 14);
  store<u32>(base + 16, 0xdeadbeef);
  memory.copy(base + 16, base, 2);     // overwrite half of it
  let c = load<u32>(base + 16);
  memory.fill(base + 18, 0x5a, 1);
  let d = load<u32>(base + 16);
  store<u32>(base + 20, 7);
  store<u32>(base + 20, 8);            // first store dead...
  let e = peek32(base + 20);           // ...but only the second is read
  store<u32>(base + 24, 9);
  let f = peek32(base + 24);           // a call reads the first store
  store<u32>(base + 24, 10);
  return "calls " + hex32(a) + " " + b.toString() + " " + hex32(c) + " " + hex32(d) + " " + e.toString() + " " + f.toString() + " " + load<u32>(base + 24).toString();
}

function loopCarried(n: i32): string {
  let arr = new Int32Array(n);
  for (let i = 0; i < n; i++) arr[i] = i;
  // arr[0] changes on the first iteration; a load hoisted out would be stale
  for (let i = 0; i < n; i++) arr[i] = arr[0] + arr[i] * 2;
  let s1 = 0;
  for (let i = 0; i < n; i++) s1 = s1 * 31 + arr[i];
  // the location read every iteration is written every third iteration
  let p = arr.dataStart;
  let acc = 0;
  for (let i = 0; i < n; i++) {
    acc += load<i32>(p + 4);
    if (i % 3 == 0) store<i32>(p + 4, acc & 0xff);
  }
  // a pointer bumped by the loop and a fixed pointer that it passes over
  memory.fill(B + 448, 0, 32);
  let fixed = B + 448 + 16;
  let walker = B + 448;
  let seen = "";
  for (let i = 0; i < 8; i++) {
    store<i32>(walker, -i);
    seen += load<i32>(fixed).toString() + ",";
    walker += 4;
  }
  return "loop n=" + n.toString() + " s1=" + s1.toString() + " acc=" + acc.toString() + " seen=" + seen;
}

function views(): string {
  let buf = new ArrayBuffer(32);
  let i32v = Int32Array.wrap(buf), u8v = Uint8Array.wrap(buf), f64v = Float64Array.wrap(buf), dv = new DataView(buf);
  i32v[0] = 0x01020304;
  u8v[1] = 0xff;
  let a = i32v[0];
  f64v[1] = 1.0;
  let b = i32v[3];
  dv.setInt16(13, -2, true);
  let c = f64v[1];
  i32v[3] = 0x40000000;
  let d = dv.getFloat64(8, true);
  u8v[16] = 1; u8v[17] = 2;
  let e = dv.getUint32(16, false);
  return "views " + hex32(a) + " " + hex32(b) + " " + hex64(reinterpret<u64>(c)) + " " + d.toString() + " " + hex32(e);
}

function structs(base: usize): string {
  let p = changetype<Pair>(base);
  let w = changetype<Wide>(base);
  let q = changetype<Pair>(base + 4);   // overlaps p.b
  p.a = 1; p.b = 2;
  let x = w.ab;
  w.ab = 0x0000000300000004;
  let y = p.a + p.b * 10;
  q.a = 99;
  let z = p.b;
  q.b = -1;
  let t = load<u32>(base + 8);
  return "structs " + hex64(x) + " " + y.toString() + " " + z.toString() + " " + hex32(t) + " " + hex64(w.ab);
}

function run(): void {
  const xs: u64[] = [0, 0xffffffffffffffff, 0x0123456789abcdef, 0x8000000000000001, 0x3ff0000000000000, 0x7ff8000000000000];
  for (let i = 0; i < xs.length; i++) print(forwarding(B + <usize>(i * 17), xs[i]));
  for (let k = 0; k < 6; k++) print(twoPaths(B + 128, k));
  print(behindCalls(B + 200));
  print(behindCalls(B + 257));   // the same at a misaligned base
  for (let n = 2; n < 40; n += 9) print(loopCarried(n));  // n >= 2: reads arr[1]
  print(views());
  print(structs(B + 300));
  print(structs(B + 333));
  // A swap through memory where both pointers may be the same.
  for (let k = 0; k < 4; k++) {
    let p = B + 400 + <usize>((k & 1) * 4), q = B + 400 + <usize>((k >> 1) * 4);
    store<i32>(B + 400, 10); store<i32>(B + 404, 20);
    let t = load<i32>(p); store<i32>(p, load<i32>(q)); store<i32>(q, t);
    // xor swap, which zeroes the value when p == q
    store<i32>(p, load<i32>(p) ^ load<i32>(q)); store<i32>(q, load<i32>(p) ^ load<i32>(q)); store<i32>(p, load<i32>(p) ^ load<i32>(q));
    print("swap p=" + (k & 1).toString() + " q=" + (k >> 1).toString() + " -> " + load<i32>(B + 400).toString() + " " + load<i32>(B + 404).toString());
  }
}
run();
