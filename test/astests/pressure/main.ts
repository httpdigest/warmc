// Register pressure and calling convention: functions with 12-16 parameters
// of mixed i32/i64/f32/f64 type (more than arm64's eight integer and eight
// float argument registers, so the rest go on the stack, interleaved by
// type), many values live across calls (each must be in a callee-saved
// register or spilled and reloaded), long expressions with more temporaries
// than registers, and globals read and written by callees between uses in
// the caller (a cached copy in a register would go stale).  Also a function
// that calls itself with its arguments rotated, so every argument slot is
// read from every other.  Each step prints, so a swapped or stale value
// shows where it happened.
import { print, bits } from "../wasi";

let g1: i32 = 1;
let g2: i64 = 2;
let g3: f64 = 3.0;

function bumpGlobals(k: i32): i32 {
  g1 = g1 * 3 + k;
  g2 = g2 * 5 - <i64>k;
  g3 = g3 * 0.5 + <f64>k;
  return g1 & 7;
}

function many(a: i32, b: i64, c: f32, d: f64, e: i32, f: i64, g: f32, h: f64,
              i: i32, j: i64, k: f32, l: f64, m: i32, n: i64, o: f32, p: f64): f64 {
  return <f64>a * 1.0 + <f64>b * 2.0 + <f64>c * 3.0 + d * 4.0 + <f64>e * 5.0 + <f64>f * 6.0 + <f64>g * 7.0 + h * 8.0 +
    <f64>i * 9.0 + <f64>j * 10.0 + <f64>k * 11.0 + l * 12.0 + <f64>m * 13.0 + <f64>n * 14.0 + <f64>o * 15.0 + p * 16.0;
}

// A second body of the same signature, so that calls picked at run time from
// a table stay real calls (binaryen cannot inline a call_indirect).
function many2(a: i32, b: i64, c: f32, d: f64, e: i32, f: i64, g: f32, h: f64,
               i: i32, j: i64, k: f32, l: f64, m: i32, n: i64, o: f32, p: f64): f64 {
  return (<f64>a - <f64>b) * <f64>c + d - <f64>e * <f64>f + <f64>g / (h + 1e-3) - <f64>i + <f64>(j >> 3) * <f64>k - l +
    <f64>(m ^ 5) - <f64>n * 0.5 + <f64>o + p;
}
let manyTable: Array<(a: i32, b: i64, c: f32, d: f64, e: i32, f: i64, g: f32, h: f64,
                      i: i32, j: i64, k: f32, l: f64, m: i32, n: i64, o: f32, p: f64) => f64> = [many, many2];

function ints12(a: i32, b: i32, c: i32, d: i32, e: i32, f: i32, g: i32, h: i32, i: i32, j: i32, k: i32, l: i32): i32 {
  return a - b * 2 + c * 3 - d * 4 + e * 5 - f * 6 + g * 7 - h * 8 + i * 9 - j * 10 + k * 11 - l * 12;
}

function longs10(a: i64, b: i64, c: i64, d: i64, e: i64, f: i64, g: i64, h: i64, i: i64, j: i64): i64 {
  return ((a ^ b) + (c ^ d)) * ((e ^ f) + (g ^ h)) - (i * j);
}

function floats10(a: f32, b: f32, c: f32, d: f32, e: f32, f: f32, g: f32, h: f32, i: f32, j: f32): f32 {
  return (a + b) * (c - d) + (e * f) / (g + <f32>1.5) - h * i + j;
}

// Rotating arguments through a recursion.
function rot(depth: i32, a: i64, b: i32, c: f64, d: i64, e: i32, f: f64, g: i64, h: i32, i: f64, j: i64): i64 {
  if (depth == 0) return a + <i64>b * 3 + <i64>c * 5 + d * 7 + <i64>e * 11 + <i64>f * 13 + g * 17 + <i64>h * 19 + <i64>i * 23 + j * 29;
  let r = rot(depth - 1, j, <i32>a, <f64>b, <i64>c, <i32>d, <f64>e, <i64>f, <i32>g, <f64>h, <i64>i);
  return r * 31 + a - <i64>h;
}

// Many locals live across calls.
function live(seed: i32): string {
  let a = seed, b = seed * 3, c = seed ^ 0x55, d = seed + 1000;
  let e: i64 = <i64>seed << 33, f: i64 = <i64>seed * -7;
  let x = <f64>seed * 0.1, y = <f64>seed * -2.5, z = <f32>seed * <f32>0.25;
  let u = <u32>seed * 2654435761, v: u64 = <u64>seed * 0x9e3779b97f4a7c15;
  let k1 = bumpGlobals(a);
  let k2 = bumpGlobals(b & 15);
  let s1 = many(a, e, z, x, b, f, z, y, c, e, z, x, d, f, z, y) +
    manyTable[seed & 1](d, f, <f32>y, x, c, e, <f32>x, y, b, f, z, <f64>u, a, e, z, <f64>v);
  let k3 = bumpGlobals(c & 15);
  let s2 = ints12(a, b, c, d, k1, k2, k3, g1, <i32>u, <i32>v, <i32>e, <i32>(v >> 32));
  let s3 = longs10(e, f, <i64>a, <i64>b, <i64>u, <i64>v, g2, <i64>k1, <i64>k3, <i64>d);
  let s4 = floats10(z, <f32>x, <f32>y, <f32>a, <f32>g3, z, <f32>k2, <f32>s1, <f32>s2, <f32>d);
  // every local still live here
  let tail = <i64>a + <i64>b + <i64>c + <i64>d + e + f + <i64>u + <i64>v + <i64>k1 + <i64>k2 + <i64>k3;
  return "live " + seed.toString() + " s1=" + bits(s1) + " s2=" + s2.toString() + " s3=" + s3.toString() +
    " s4=" + bits(<f64>s4) + " tail=" + tail.toString() + " xyz=" + bits(x + y + <f64>z) +
    " g=" + g1.toString() + "," + g2.toString() + "," + bits(g3);
}

// An expression with ~40 simultaneous temporaries.
function wide(xs: StaticArray<i64>): i64 {
  let a0 = xs[0], a1 = xs[1], a2 = xs[2], a3 = xs[3], a4 = xs[4], a5 = xs[5], a6 = xs[6], a7 = xs[7];
  let a8 = xs[8], a9 = xs[9], a10 = xs[10], a11 = xs[11], a12 = xs[12], a13 = xs[13], a14 = xs[14], a15 = xs[15];
  let b0 = a0 * a15, b1 = a1 * a14, b2 = a2 * a13, b3 = a3 * a12, b4 = a4 * a11, b5 = a5 * a10, b6 = a6 * a9, b7 = a7 * a8;
  let c0 = a0 ^ a1, c1 = a2 ^ a3, c2 = a4 ^ a5, c3 = a6 ^ a7, c4 = a8 ^ a9, c5 = a10 ^ a11, c6 = a12 ^ a13, c7 = a14 ^ a15;
  let d0 = b0 + c7, d1 = b1 + c6, d2 = b2 + c5, d3 = b3 + c4, d4 = b4 + c3, d5 = b5 + c2, d6 = b6 + c1, d7 = b7 + c0;
  return (d0 * d1 - d2 * d3) ^ (d4 * d5 - d6 * d7) ^ (a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10 + a11 + a12 + a13 + a14 + a15) ^
    (b0 | b1 | b2 | b3) ^ (b4 & b5 & b6 & b7) ^ (c0 + c1 + c2 + c3 + c4 + c5 + c6 + c7);
}

function wideF(xs: StaticArray<f64>): f64 {
  let a0 = xs[0], a1 = xs[1], a2 = xs[2], a3 = xs[3], a4 = xs[4], a5 = xs[5], a6 = xs[6], a7 = xs[7];
  let a8 = xs[8], a9 = xs[9], a10 = xs[10], a11 = xs[11], a12 = xs[12], a13 = xs[13], a14 = xs[14], a15 = xs[15];
  let a16 = xs[16], a17 = xs[17], a18 = xs[18], a19 = xs[19], a20 = xs[20], a21 = xs[21], a22 = xs[22], a23 = xs[23];
  let s = ((a0 * a1 + a2 * a3) * (a4 * a5 + a6 * a7) + (a8 * a9 + a10 * a11) * (a12 * a13 + a14 * a15)) /
    ((a16 + a17) * (a18 + a19) + (a20 + a21) * (a22 + a23) + 1.0);
  return s + a0 - a23 + a11 * a12 - a5 / (a19 + 2.0);
}

function run(): void {
  for (let s = -3; s <= 40; s += 4) print(live(s));
  print("many " + bits(many(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16)) + " " +
    bits(many(-1, -0x100000000, <f32>0.5, -0.25, 0x7fffffff, 0x7fffffffffffffff, <f32>-1e30, 1e300, -0x80000000, -1, NaN, -0.0, 3, 4, Infinity, 5)));
  print("ints12 " + ints12(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12).toString() + " " + ints12(-1, 0x7fffffff, -0x80000000, 3, 0, 0, 0, 0, 0, 0, 0, 100).toString());
  print("longs10 " + longs10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10).toString() + " " + longs10(-1, 0x123456789, -0x7fffffff00000000, 5, 6, 7, 8, 9, 0x100000000, 0x100000000).toString());
  print("floats10 " + bits(<f64>floats10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10)));
  for (let dpt = 0; dpt <= 12; dpt += 3) {
    print("rot " + dpt.toString() + " " + rot(dpt, 1, 2, 3.5, 4, 5, 6.25, 7, 8, 9.75, 10).toString() + " " +
      rot(dpt, -0x100000000, -2, -3.5e3, 0x7fffffff, -5, 1e6, -7, 0x7fffffff, -9.75, 0x12345).toString());
  }
  let xs = new StaticArray<i64>(16), fs = new StaticArray<f64>(24);
  let r: u64 = 88172645463325252;
  for (let k = 0; k < 8; k++) {
    for (let i = 0; i < 16; i++) { r ^= r << 13; r ^= r >> 7; r ^= r << 17; xs[i] = <i64>(r >> <u64>(k * 4)); }
    for (let i = 0; i < 24; i++) { r ^= r << 13; r ^= r >> 7; r ^= r << 17; fs[i] = <f64>(<i32>(r >> 40)) * 1e-3; }
    print("wide " + k.toString() + " " + wide(xs).toString() + " " + bits(wideF(fs)));
  }
}
run();
