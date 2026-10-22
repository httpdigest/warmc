// Interfaces, static members, accessors, enums and namespaces.  A call
// through an interface-typed reference is resolved at run time from the
// object's class id (AssemblyScript emits a stub that switches over every
// implementing class), so the same call site reaches unrelated classes.
// Getters and setters run code on what looks like a field access, with a
// setter that validates and a computed getter; `readonly` fields; static
// fields as counters shared by all instances and static methods as factories;
// enums with explicit and implicit values, used in switches and as array
// indices; and a namespace holding functions, constants and mutable state.
import { print } from "../wasi";

interface Shape {
  area(): f64;
  perimeter(): f64;
  name(): string;
}

interface Scalable {
  scale(k: f64): void;
}

class Circle implements Shape, Scalable {
  constructor(public r: f64) {}
  area(): f64 { return 3.0 * this.r * this.r; }
  perimeter(): f64 { return 6.0 * this.r; }
  name(): string { return "circle"; }
  scale(k: f64): void { this.r *= k; }
}

class Rect implements Shape, Scalable {
  constructor(public w: f64, public h: f64) {}
  area(): f64 { return this.w * this.h; }
  perimeter(): f64 { return 2.0 * (this.w + this.h); }
  name(): string { return "rect"; }
  scale(k: f64): void { this.w *= k; this.h *= k; }
}

class Square extends Rect {
  constructor(s: f64) { super(s, s); }
  name(): string { return "square"; }
}

class Tri implements Shape {
  constructor(public a: f64, public b: f64, public c: f64) {}
  area(): f64 {
    let s = (this.a + this.b + this.c) / 2.0;
    return Math.sqrt(s * (s - this.a) * (s - this.b) * (s - this.c));
  }
  perimeter(): f64 { return this.a + this.b + this.c; }
  name(): string { return "tri"; }
}

function report(s: Shape): string {
  return s.name() + " area=" + s.area().toString() + " perim=" + s.perimeter().toString();
}

// Accessors with logic in them.
class Temperature {
  private _c: f64 = 0;
  static sets: i32 = 0;
  static rejected: i32 = 0;
  get celsius(): f64 { return this._c; }
  set celsius(v: f64) {
    if (v < -273.15) { Temperature.rejected++; return; }
    Temperature.sets++;
    this._c = v;
  }
  get fahrenheit(): f64 { return this._c * 9.0 / 5.0 + 32.0; }
  set fahrenheit(v: f64) { this.celsius = (v - 32.0) * 5.0 / 9.0; }
}

class Counter {
  static instances: i32 = 0;
  static total: i64 = 0;
  readonly serial: i32;
  private n: i32 = 0;
  constructor() { this.serial = ++Counter.instances; }
  static make(start: i32): Counter {
    let c = new Counter();
    c.n = start;
    return c;
  }
  get value(): i32 { return this.n; }
  bump(by: i32): Counter { this.n += by; Counter.total += <i64>by; return this; }
}

enum Color { Red, Green = 5, Blue, Alpha = 100 }
const enum Op { Add = 1, Sub, Mul, Div }

function colorName(c: Color): string {
  switch (c) {
    case Color.Red: return "red";
    case Color.Green: return "green";
    case Color.Blue: return "blue";
    case Color.Alpha: return "alpha";
    default: return "?" + (<i32>c).toString();
  }
}

function apply(op: Op, a: i32, b: i32): i32 {
  switch (op) {
    case Op.Add: return a + b;
    case Op.Sub: return a - b;
    case Op.Mul: return a * b;
    case Op.Div: return b != 0 ? a / b : 0;
  }
  return -1;
}

namespace Geometry {
  export const UNIT: f64 = 2.0;
  export let calls: i32 = 0;
  export function dist(x1: f64, y1: f64, x2: f64, y2: f64): f64 {
    calls++;
    return Math.sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1)) * UNIT;
  }
  export namespace Inner {
    export function twice(x: i32): i32 { return x * 2 + calls; }
  }
}

function run(): void {
  let shapes: Shape[] = [new Circle(1.0), new Rect(2.0, 3.0), new Square(4.0), new Tri(3.0, 4.0, 5.0), new Circle(0.5)];
  for (let i = 0; i < shapes.length; i++) print(report(shapes[i]));
  let scalables: Scalable[] = [shapes[0] as Circle, shapes[1] as Rect, shapes[2] as Square];
  for (let k = 1; k <= 3; k++) {
    for (let i = 0; i < scalables.length; i++) scalables[i].scale(<f64>k * 0.5 + 0.25);
    for (let i = 0; i < shapes.length; i++) print("k" + k.toString() + " " + report(shapes[i]));
  }
  let total = 0.0;
  for (let i = 0; i < 50; i++) total += shapes[(i * 7) % shapes.length].area();
  print("total area " + total.toString());

  let t = new Temperature();
  const temps: f64[] = [25.0, -300.0, 100.0, -273.15, -273.16, 37.5];
  for (let i = 0; i < temps.length; i++) {
    t.celsius = temps[i];
    print("temp set " + temps[i].toString() + " -> c=" + t.celsius.toString() + " f=" + t.fahrenheit.toString());
  }
  t.fahrenheit = 212.0;
  print("temp f212 c=" + t.celsius.toString() + " sets=" + Temperature.sets.toString() + " rejected=" + Temperature.rejected.toString());

  let cs: Counter[] = [];
  for (let i = 0; i < 6; i++) cs.push(Counter.make(i * 10));
  for (let r = 0; r < 4; r++) {
    for (let i = 0; i < cs.length; i++) cs[i].bump(i + r).bump(1);
  }
  for (let i = 0; i < cs.length; i++) print("counter serial=" + cs[i].serial.toString() + " value=" + cs[i].value.toString());
  print("counters " + Counter.instances.toString() + " total=" + Counter.total.toString());

  const colors: i32[] = [0, 5, 6, 100, 7, 1];
  for (let i = 0; i < colors.length; i++) print("color " + colors[i].toString() + " " + colorName(<Color>colors[i]));
  print("enum values " + (<i32>Color.Red).toString() + " " + (<i32>Color.Green).toString() + " " + (<i32>Color.Blue).toString() + " " + (<i32>Color.Alpha).toString());
  for (let op = 1; op <= 5; op++) {
    print("op " + op.toString() + " " + apply(<Op>op, 17, 5).toString() + " " + apply(<Op>op, -9, 4).toString());
  }
  for (let i = 0; i < 5; i++) {
    let d = Geometry.dist(<f64>i, 0.0, 3.0, <f64>(i * 2));
    print("geo " + d.toString() + " calls=" + Geometry.calls.toString() + " twice=" + Geometry.Inner.twice(i).toString());
  }
}
run();
