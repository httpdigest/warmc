// The bit builtins at every width they exist for: popcnt, clz, ctz, rotl,
// rotr and bswap on i32 and i64 (and bswap on i16/u16, which AssemblyScript
// open-codes), with rotate counts past the width and negative, plus the
// classic bit tricks built from them -- isolate lowest set bit, round up to a
// power of two, reverse bits, parity, log2, bit interleaving.  clz and ctz of
// zero must give the width, rotations take their count modulo the width, and
// arm64 has no rotl so warmc negates the count for ror: each is a place where
// an off-by-one shows.
import { print, hex32, hex64 } from "../wasi";

const v32: u32[] = [
  0, 1, 2, 3, 0x80000000, 0xffffffff, 0x7fffffff, 0x00010000, 0x0000ffff,
  0x12345678, 0x87654321, 0xdeadbeef, 0x55555555, 0xaaaaaaaa, 0x00f000f0,
  0x40000000, 0x00000100, 0xfffffffe,
];
const v64: u64[] = [
  0, 1, 0x8000000000000000, 0xffffffffffffffff, 0x00000000ffffffff,
  0xffffffff00000000, 0x0123456789abcdef, 0xfedcba9876543210, 0x5555555555555555,
  0x0000000100000000, 0x0000000080000000, 0x8000000000000001, 0x00ff00ff00ff00ff,
];
const counts: i32[] = [0, 1, 4, 7, 8, 15, 16, 31, 32, 33, 63, 64, 65, -1, -8, 100];

function b32(x: u32): string {
  return "b32 " + hex32(x) + " popcnt=" + popcnt(x).toString() + " clz=" + clz(x).toString() +
    " ctz=" + ctz(x).toString() + " bswap=" + hex32(bswap(x)) +
    " bswap16=" + hex32(<u32>bswap(<u16>x)) + " ibswap16=" + (<i32>bswap(<i16>x)).toString() +
    " lowbit=" + hex32(x & (0 - x)) + " clrlow=" + hex32(x & (x - 1)) +
    " pow2=" + (x != 0 && (x & (x - 1)) == 0 ? "1" : "0");
}

function b64(x: u64): string {
  return "b64 " + hex64(x) + " popcnt=" + popcnt(x).toString() + " clz=" + clz(x).toString() +
    " ctz=" + ctz(x).toString() + " bswap=" + hex64(bswap(x)) +
    " lowbit=" + hex64(x & (0 - x)) + " clrlow=" + hex64(x & (x - 1));
}

function rot32(x: u32, k: i32): string {
  return "rot32 " + hex32(x) + " " + k.toString() + " l=" + hex32(rotl(x, <u32>k)) +
    " r=" + hex32(rotr(x, <u32>k)) + " lr=" + hex32(rotr(rotl(x, <u32>k), <u32>k)) +
    " il=" + hex32(<u32>rotl(<i32>x, k));
}

function rot64(x: u64, k: i32): string {
  return "rot64 " + hex64(x) + " " + k.toString() + " l=" + hex64(rotl(x, <u64>k)) +
    " r=" + hex64(rotr(x, <u64>k));
}

// Rotates by constants, which the code generator emits as an immediate.
function rotConst(x: u32, y: u64): string {
  return "rotc " + hex32(rotl(x, 1)) + " " + hex32(rotl(x, 13)) + " " + hex32(rotr(x, 7)) +
    " " + hex32(rotl(x, 31)) + " " + hex64(rotl(y, 1)) + " " + hex64(rotl(y, 33)) +
    " " + hex64(rotr(y, 17)) + " " + hex64(rotr(y, 63));
}

function reverse32(x: u32): u32 {
  x = ((x >> 1) & 0x55555555) | ((x & 0x55555555) << 1);
  x = ((x >> 2) & 0x33333333) | ((x & 0x33333333) << 2);
  x = ((x >> 4) & 0x0f0f0f0f) | ((x & 0x0f0f0f0f) << 4);
  return bswap(x);
}

function nextPow2(x: u32): u32 {
  return x <= 1 ? 1 : <u32>1 << (32 - clz(x - 1));
}

// Morton order: interleave the bits of two 16-bit coordinates.
function spread(x: u32): u32 {
  x &= 0xffff;
  x = (x | (x << 8)) & 0x00ff00ff;
  x = (x | (x << 4)) & 0x0f0f0f0f;
  x = (x | (x << 2)) & 0x33333333;
  x = (x | (x << 1)) & 0x55555555;
  return x;
}

function tricks(x: u32): string {
  let log2 = x == 0 ? -1 : 31 - <i32>clz(x);
  return "trick " + hex32(x) + " rev=" + hex32(reverse32(x)) + " next2=" + nextPow2(x).toString() +
    " log2=" + log2.toString() + " parity=" + (popcnt(x) & 1).toString() +
    " morton=" + hex32(spread(x) | (spread(x >> 16) << 1)) +
    " ctz-by-popcnt=" + (x == 0 ? "32" : popcnt((x & (0 - x)) - 1).toString());
}

function run(): void {
  for (let i = 0; i < v32.length; i++) print(b32(v32[i]));
  for (let i = 0; i < v64.length; i++) print(b64(v64[i]));
  for (let i = 0; i < v32.length; i += 3) {
    for (let j = 0; j < counts.length; j++) print(rot32(v32[i], counts[j]));
  }
  for (let i = 0; i < v64.length; i += 2) {
    for (let j = 0; j < counts.length; j++) print(rot64(v64[i], counts[j]));
  }
  for (let i = 0; i < v32.length; i++) print(rotConst(v32[i], v64[i % v64.length]));
  for (let i = 0; i < v32.length; i++) print(tricks(v32[i]));
  // popcnt / clz / ctz over a sweep, summed, with the counts as loop data.
  let x: u32 = 0x9e3779b9;
  let y: u64 = 0x243f6a8885a308d3;
  let s1 = 0, s2 = 0, s3 = 0;
  let t1: u64 = 0, t2: u64 = 0;
  for (let k = 0; k < 200; k++) {
    x = x * 0x01000193 ^ <u32>k;
    y = rotl(y, <u64>(x & 63)) ^ <u64>x;
    s1 += popcnt(x); s2 += clz(x >> (<u32>k & 31)); s3 += ctz(x << (<u32>k & 31));
    t1 += popcnt(y) + clz(y >> <u64>(k & 63)); t2 += ctz(y << <u64>(k & 63)) ^ bswap(y);
    if ((k % 25) == 24) {
      print("sweep " + k.toString() + " " + s1.toString() + " " + s2.toString() + " " +
        s3.toString() + " " + t1.toString() + " " + hex64(t2));
    }
  }
}
run();
