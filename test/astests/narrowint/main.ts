// i8, u8, i16 and u16, which wasm does not have: AssemblyScript keeps them in
// i32 locals and wraps (sign- or zero-extends) where the wrap could be seen --
// before a compare, a division, a shift right, a store, a widening cast, a
// call.  This exercises each of those sites: wrapping add/sub/mul/neg,
// increments past the top, compares of wrapped values, right shifts of
// negative bytes, small types passed to and returned from functions, stored
// to and loaded from typed arrays and memory, and accumulators that wrap on
// every step.  binaryen drops wraps it thinks are dead, so a wrong guess by
// either binaryen's model or warmc's known-bits tracking shows as a diff.
import { print } from "../wasi";

const raw: i32[] = [
  0, 1, -1, 2, 127, 128, -128, -129, 255, 256, 257, 0x7fff, 0x8000, -0x8000,
  0xffff, 0x10000, 0x12345, -0x12345, 200, -200, 1000, 30000, -30000, 65535,
];

// Small types as parameters and results: the callee must see a wrapped
// value even if the caller held an unwrapped one in a register.
function addI8(a: i8, b: i8): i8 { return a + b; }
function mulI8(a: i8, b: i8): i8 { return a * b; }
function subU8(a: u8, b: u8): u8 { return a - b; }
function mulU8(a: u8, b: u8): u8 { return a * b; }
function addI16(a: i16, b: i16): i16 { return a + b; }
function mulI16(a: i16, b: i16): i16 { return a * b; }
function subU16(a: u16, b: u16): u16 { return a - b; }
function mulU16(a: u16, b: u16): u16 { return a * b; }
function divI8(a: i8, b: i8): i8 { return b == 0 ? 0 : a / b; }
function remU8(a: u8, b: u8): u8 { return b == 0 ? 0 : a % b; }
function divI16(a: i16, b: i16): i16 { return b == 0 ? 0 : a / b; }
function remU16(a: u16, b: u16): u16 { return b == 0 ? 0 : a % b; }

function casts(x: i32): string {
  let a = <i8>x, b = <u8>x, c = <i16>x, d = <u16>x;
  return "cast " + x.toString() + " i8=" + a.toString() + " u8=" + b.toString() +
    " i16=" + c.toString() + " u16=" + d.toString() +
    " i8>>1=" + (a >> 1).toString() + " u8>>1=" + (b >> 1).toString() +
    " i16>>3=" + (c >> 3).toString() + " u16>>3=" + (d >> 3).toString() +
    " i8<0=" + (a < 0 ? "1" : "0") + " i16<0=" + (c < 0 ? "1" : "0") +
    " -i8=" + (<i8>-a).toString() + " -u8=" + (<u8>-b).toString() +
    " ~i8=" + (<i8>~a).toString() + " ~u16=" + (<u16>~d).toString() +
    " i8->u16=" + (<u16>a).toString() + " u8->i16=" + (<i16>b).toString() +
    " i16->u8=" + (<u8>c).toString() + " u16->i8=" + (<i8>d).toString() +
    " i8->i64=" + (<i64>a).toString() + " u16->i64=" + (<i64>d).toString() +
    " i16->u32=" + (<u32>c).toString();
}

function pairs(x: i32, y: i32): string {
  return "pair " + x.toString() + " " + y.toString() +
    " addI8=" + addI8(<i8>x, <i8>y).toString() + " mulI8=" + mulI8(<i8>x, <i8>y).toString() +
    " subU8=" + subU8(<u8>x, <u8>y).toString() + " mulU8=" + mulU8(<u8>x, <u8>y).toString() +
    " addI16=" + addI16(<i16>x, <i16>y).toString() + " mulI16=" + mulI16(<i16>x, <i16>y).toString() +
    " subU16=" + subU16(<u16>x, <u16>y).toString() + " mulU16=" + mulU16(<u16>x, <u16>y).toString() +
    " divI8=" + divI8(<i8>x, <i8>y).toString() + " remU8=" + remU8(<u8>x, <u8>y).toString() +
    " divI16=" + divI16(<i16>x, <i16>y).toString() + " remU16=" + remU16(<u16>x, <u16>y).toString() +
    " ltI8=" + (<i8>x < <i8>y ? "1" : "0") + " ltU8=" + (<u8>x < <u8>y ? "1" : "0") +
    " ltI16=" + (<i16>x < <i16>y ? "1" : "0") + " eqU16=" + (<u16>x == <u16>y ? "1" : "0");
}

function run(): void {
  let n = raw.length;
  for (let i = 0; i < n; i++) print(casts(raw[i]));
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < n; j += 2) print(pairs(raw[i], raw[j]));
  }

  // Counters that wrap: a u8 past 255, an i8 past 127, a u16 down past 0.
  let c8: u8 = 250;
  let s8: i8 = 120;
  let c16: u16 = 3;
  let s16: i16 = 32760;
  let line = "";
  for (let k = 0; k < 16; k++) {
    c8++; s8 += 1; c16--; s16 += 2;
    line += c8.toString() + "/" + s8.toString() + "/" + c16.toString() + "/" + s16.toString() + " ";
  }
  print("wrap " + line);

  // A u8 accumulator: the sum modulo 256 of a run, with the loop condition on
  // the wrapped value itself, which terminates only if the wrap is right.
  let acc: u8 = 0;
  let steps = 0;
  do {
    acc += 37;
    steps++;
  } while (acc != 0 && steps < 1000);
  print("u8 cycle steps=" + steps.toString());
  let acc16: u16 = 1;
  steps = 0;
  do {
    acc16 = acc16 * 3;
    steps++;
  } while (acc16 != 1 && steps < 100000);
  print("u16 order of 3 = " + steps.toString());

  // Bytes through a typed array and back: stored values truncate, loaded
  // values extend by the array's signedness.
  let i8a = new Int8Array(n);
  let u8a = new Uint8Array(n);
  let i16a = new Int16Array(n);
  let u16a = new Uint16Array(n);
  for (let i = 0; i < n; i++) {
    i8a[i] = <i8>raw[i]; u8a[i] = <u8>raw[i];
    i16a[i] = <i16>raw[i]; u16a[i] = <u16>raw[i];
  }
  let sum: i32 = 0;
  for (let i = 0; i < n; i++) {
    sum += <i32>i8a[i] * 3 + <i32>u8a[i] - <i32>i16a[i] + <i32>u16a[i];
    print("arr " + i.toString() + " " + i8a[i].toString() + " " + u8a[i].toString() +
      " " + i16a[i].toString() + " " + u16a[i].toString() + " sum=" + sum.toString());
  }
  // Signed byte arithmetic with a saturating clamp, mixed with a wrapping one.
  let sat: i8 = 0, wr: i8 = 0;
  for (let i = 0; i < n; i++) {
    let v = i8a[i];
    let t = <i16>sat + <i16>v;
    sat = t > 127 ? 127 : t < -128 ? -128 : <i8>t;
    wr = <i8>(wr + v) * 3;
    print("sat " + v.toString() + " -> " + sat.toString() + " wr=" + wr.toString());
  }
  // bool, the narrowest of all: from integers, and arithmetic on it.
  let bs = "";
  for (let i = 0; i < n; i++) {
    let b = <bool>raw[i];
    let b8 = <bool><u8>raw[i];
    bs += (b ? "T" : "F") + (b8 ? "t" : "f") + (<i32>b + <i32>b8).toString();
  }
  print("bool " + bs);
}
run();
