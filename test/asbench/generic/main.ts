// Generic classes and functions: one ring buffer and one windowed fold,
// instantiated at i32, i64, f64 and f32, plus a two-parameter Pair that is
// built and swapped every round.  AssemblyScript monomorphises, so there is no
// boxing and no dictionary passing -- every instantiation is its own code, and
// `isFloat<T>()` is decided per copy at compile time, so each copy of `mix`
// keeps only its own arm: the integer ones an `i32.shr_s` or `i64.shr_s`, the
// float ones a multiply by 0.75.  binaryen inlines the fold into the loop and
// leaves `Ring<T>#at` out of line, one function per element type, each with its
// own shadow-stack frame and a bounds check against the StaticArray's header.
//
// What that adds up to is four copies of one loop that differ only in the type
// of their loads and their arithmetic, so the row exercises warmc's i32, i64,
// f32 and f64 paths through identical control flow, and the calls into the
// four `at` functions are the kind an inliner has to decide about.
import { print, bits } from "../wasi";

// A fixed-capacity ring buffer over any element type.  The capacity is a power
// of two so the wrap is a mask.
class Ring<T> {
  buf: StaticArray<T>;
  head: i32 = 0;
  constructor(public cap: i32) { this.buf = new StaticArray<T>(cap); }
  push(v: T): void {
    this.buf[this.head] = v;
    this.head = (this.head + 1) & (this.cap - 1);
  }
  at(i: i32): T { return this.buf[(this.head + i) & (this.cap - 1)]; }
}

// One body, three meanings: the branch on `isFloat<T>()` is decided per
// instantiation at compile time, so each copy keeps only its own arm.
function mix<T>(a: T, b: T): T {
  if (isFloat<T>()) {
    // @ts-ignore: T is a float here
    return a * <T>0.75 + b;
  } else {
    // @ts-ignore: T is an integer here
    return (a ^ (b >> <T>3)) + b;
  }
}

// A windowed fold over the last `w` elements, generic in the element type.
function window<T>(r: Ring<T>, w: i32): T {
  let acc = r.at(0);
  for (let i = 1; i < w; i++) acc = mix<T>(acc, r.at(i));
  return acc;
}

class Pair<A, B> {
  constructor(public a: A, public b: B) {}
}

function swapped<A, B>(p: Pair<A, B>): Pair<B, A> {
  return new Pair<B, A>(p.b, p.a);
}

const CAP = 64;
let ri = new Ring<i32>(CAP);
let rl = new Ring<i64>(CAP);
let rf = new Ring<f64>(CAP);
let rs = new Ring<f32>(CAP);

let ti: i32 = 0;
let tl: i64 = 0;
let tf: f64 = 0;
let ts: f32 = 0;
let x: u32 = 12345;
for (let r = 0; r < 780000; r++) {
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  ri.push(<i32>(x & 0xffff));
  rl.push(<i64>x * 0x9e3779b97f4a7c15);
  rf.push(<f64>(x & 1023) * 0.001);
  rs.push(<f32>(x & 255) * 0.25);
  let w = 8 + (r & 31);
  ti += window<i32>(ri, w);
  tl ^= window<i64>(rl, w);
  tf += window<f64>(rf, w);
  ts += window<f32>(rs, w) * 0.001;
  let p = swapped<i32, f64>(new Pair<i32, f64>(ti, tf));
  ti ^= <i32>p.a;
}
print("ti = " + ti.toString() + " tl = " + tl.toString());
print("tf = " + bits(tf) + " ts = " + bits(<f64>ts));
