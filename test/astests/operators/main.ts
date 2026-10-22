// Operator overloading through AssemblyScript's decorators: @operator("+"),
// "-", "*", "/", "==", "!=", "<", "[]", "[]=", @operator.prefix("-"), "!",
// "~" and @operator.postfix("++").  Each overload is an ordinary method that
// the compiler calls where the operator appears, so `a + b * c` on objects is
// a chain of calls that allocate, and compound assignment `a += b` is a call
// and a reassignment.  Three types: a 2-D vector of f64, a complex number
// used for Mandelbrot iteration, and a fixed-point Q16.16 number on i32 whose
// multiply needs an i64 intermediate; plus a bit-set indexed with [] and []=.
import { print, bits } from "../wasi";

class V2 {
  constructor(public x: f64, public y: f64) {}
  @operator("+") add(o: V2): V2 { return new V2(this.x + o.x, this.y + o.y); }
  @operator("-") sub(o: V2): V2 { return new V2(this.x - o.x, this.y - o.y); }
  @operator("*") scale(o: V2): V2 { return new V2(this.x * o.x, this.y * o.y); }
  @operator("==") eq(o: V2): bool { return this.x == o.x && this.y == o.y; }
  @operator("!=") ne(o: V2): bool { return !(this.x == o.x && this.y == o.y); }
  @operator.prefix("-") neg(): V2 { return new V2(-this.x, -this.y); }
  dot(o: V2): f64 { return this.x * o.x + this.y * o.y; }
  toString(): string { return "(" + this.x.toString() + "," + this.y.toString() + ")"; }
}

class Cx {
  constructor(public re: f64, public im: f64) {}
  @operator("+") add(o: Cx): Cx { return new Cx(this.re + o.re, this.im + o.im); }
  @operator("*") mul(o: Cx): Cx {
    return new Cx(this.re * o.re - this.im * o.im, this.re * o.im + this.im * o.re);
  }
  @operator("/") div(o: Cx): Cx {
    let d = o.re * o.re + o.im * o.im;
    return new Cx((this.re * o.re + this.im * o.im) / d, (this.im * o.re - this.re * o.im) / d);
  }
  mag2(): f64 { return this.re * this.re + this.im * this.im; }
}

// Q16.16 fixed point in an i32.
class Fx {
  constructor(public raw: i32) {}
  static of(n: f64): Fx { return new Fx(<i32>(n * 65536.0)); }
  @operator("+") add(o: Fx): Fx { return new Fx(this.raw + o.raw); }
  @operator("-") sub(o: Fx): Fx { return new Fx(this.raw - o.raw); }
  @operator("*") mul(o: Fx): Fx { return new Fx(<i32>((<i64>this.raw * <i64>o.raw) >> 16)); }
  @operator("/") div(o: Fx): Fx { return new Fx(<i32>((<i64>this.raw << 16) / <i64>o.raw)); }
  @operator("<") lt(o: Fx): bool { return this.raw < o.raw; }
  @operator("==") eq(o: Fx): bool { return this.raw == o.raw; }
  @operator.prefix("-") neg(): Fx { return new Fx(-this.raw); }
  @operator.prefix("~") floor(): Fx { return new Fx(this.raw & ~0xffff); }
  @operator.prefix("!") isZero(): bool { return this.raw == 0; }
  @operator.postfix("++") inc(): Fx { return new Fx(this.raw + 65536); }
  toString(): string {
    let neg = this.raw < 0;
    let r = <u32>(neg ? -this.raw : this.raw);
    let frac = (<u64>(r & 0xffff) * 100000) >> 16;
    return (neg ? "-" : "") + (r >> 16).toString() + "." + frac.toString().padStart(5, "0");
  }
}

class Bits {
  words: StaticArray<u32>;
  constructor(public n: i32) { this.words = new StaticArray<u32>((n + 31) >> 5); }
  @operator("[]") get(i: i32): bool { return (this.words[i >> 5] >> (<u32>i & 31) & 1) != 0; }
  @operator("[]=") set(i: i32, v: bool): void {
    let m = <u32>1 << (<u32>i & 31);
    if (v) this.words[i >> 5] |= m; else this.words[i >> 5] &= ~m;
  }
  count(): i32 { let c = 0; for (let i = 0; i < this.words.length; i++) c += <i32>popcnt(this.words[i]); return c; }
}

function run(): void {
  let a = new V2(1.5, -2.0), b = new V2(0.25, 4.0), c = new V2(3.0, 3.0);
  let r = a + b * c - -a;
  print("v2 " + r.toString() + " dot=" + r.dot(a).toString() + " eq=" + (r == r ? "1" : "0") +
    " ne=" + (r != a ? "1" : "0") + " eq2=" + (a + b == b + a ? "1" : "0"));
  let acc = new V2(0, 0);
  for (let i = 0; i < 12; i++) {
    acc += new V2(<f64>i, <f64>(i * i)) * new V2(0.5, -0.125);
    if (i % 3 == 2) print("acc " + i.toString() + " " + acc.toString());
  }
  // Mandelbrot escape counts on a small grid, all through the overloads.
  for (let row = 0; row < 12; row++) {
    let line = "";
    for (let col = 0; col < 32; col++) {
      let c0 = new Cx(-2.2 + <f64>col * 0.09, -1.2 + <f64>row * 0.2);
      let z = new Cx(0, 0);
      let n = 0;
      while (n < 40 && z.mag2() <= 4.0) { z = z * z + c0; n++; }
      line += String.fromCharCode(n >= 40 ? 35 : 97 + (n % 26));
    }
    print("mb " + line);
  }
  let q = new Cx(3.0, -4.0) / new Cx(1.0, 2.0);
  print("cdiv " + bits(q.re) + " " + bits(q.im));
  // Fixed point.
  let x = Fx.of(3.25), y = Fx.of(-1.5), z = Fx.of(0.1);
  print("fx " + (x + y).toString() + " " + (x - y).toString() + " " + (x * y).toString() + " " +
    (x / y).toString() + " " + (-x).toString() + " " + (~y).toString() + " " + (~x).toString() +
    " " + (x < y ? "lt" : "ge") + " " + (!z ? "zero" : "nonzero") + " " + (!(z - z) ? "zero" : "nonzero"));
  let s = Fx.of(1.0);
  for (let i = 0; i < 20; i++) {
    s = s * Fx.of(1.1) + z;
    s++;
    if (i % 4 == 3) print("fx-run " + i.toString() + " " + s.toString() + " raw=" + s.raw.toString());
  }
  // Newton's method for sqrt(2) in fixed point.
  let g = Fx.of(1.0), two = Fx.of(2.0);
  for (let i = 0; i < 6; i++) {
    g = (g + two / g) / two;
    print("fx-sqrt " + i.toString() + " " + g.toString());
  }
  // Sieve on the [] / []= bit set.
  let bs = new Bits(500);
  for (let i = 2; i < 500; i++) bs[i] = true;
  for (let i = 2; i * i < 500; i++) if (bs[i]) for (let j = i * i; j < 500; j += i) bs[j] = false;
  let primes = "";
  for (let i = 0; i < 500; i++) if (bs[i] && i > 400) primes += i.toString() + " ";
  print("bits count=" + bs.count().toString() + " top=" + primes);
}
run();
