// First-class functions as AssemblyScript has them: function references
// stored in variables, arrays, class fields and maps; passed as callbacks to
// user code and to Array methods; returned from functions; and arrow
// functions that read globals (AssemblyScript has no closures over locals,
// so "state" lives in globals or is passed in).  Every indirect call is a
// call_indirect through the table with a signature check, and a callback's
// argument count is passed in a global for optional parameters, which this
// exercises with a function taking a defaulted parameter.  Also a dispatch
// table keyed by opcode and function composition through a pipeline array.
import { print } from "../wasi";

type I2I = (x: i32) => i32;
type II2I = (a: i32, b: i32) => i32;
type F2F = (x: f64) => f64;

function inc(x: i32): i32 { return x + 1; }
function dbl(x: i32): i32 { return x * 2; }
function sq(x: i32): i32 { return x * x; }
function negate(x: i32): i32 { return -x; }
function collatz(x: i32): i32 { return (x & 1) ? 3 * x + 1 : x >> 1; }

let counter = 0;
let scaleBy = 3;

function applyN(f: I2I, x: i32, n: i32): i32 {
  for (let i = 0; i < n; i++) x = f(x);
  return x;
}

function compose(fs: I2I[], x: i32): i32 {
  for (let i = 0; i < fs.length; i++) x = fs[i](x);
  return x;
}

function pick(k: i32): I2I {
  switch (k & 3) {
    case 0: return inc;
    case 1: return dbl;
    case 2: return sq;
    default: return negate;
  }
}

function withDefault(a: i32, b: i32 = 10): i32 { return a * 100 + b; }

class Button {
  clicks: i32 = 0;
  constructor(public label: string, public onClick: (b: Button) => string) {}
  click(): string { this.clicks++; return this.onClick(this); }
}

function integrate(f: F2F, a: f64, b: f64, n: i32): f64 {
  let h = (b - a) / <f64>n;
  let s = 0.5 * (f(a) + f(b));
  for (let i = 1; i < n; i++) s += f(a + <f64>i * h);
  return s * h;
}

function run(): void {
  let fs: I2I[] = [inc, dbl, sq, negate, collatz];
  for (let i = 0; i < fs.length; i++) {
    let line = "fn" + i.toString();
    for (let x = -3; x <= 7; x += 2) line += " " + fs[i](x).toString();
    print(line);
  }
  for (let k = 0; k < 8; k++) print("applyN " + k.toString() + " " + applyN(pick(k), k + 1, 3).toString());
  print("compose " + compose([inc, dbl, sq, negate, inc], 4).toString() + " " +
    compose([collatz, collatz, collatz, collatz, collatz, collatz], 27).toString());

  // Arrows reading and writing globals.
  let bump: I2I = (x: i32): i32 => { counter += x; return counter * scaleBy; };
  for (let i = 1; i <= 5; i++) print("bump " + bump(i).toString() + " counter=" + counter.toString());
  scaleBy = -2;
  print("bump after scale " + bump(1).toString());

  // Binary operators in a table, dispatched by opcode.
  let ops: II2I[] = [
    (a: i32, b: i32): i32 => a + b,
    (a: i32, b: i32): i32 => a - b,
    (a: i32, b: i32): i32 => a * b,
    (a: i32, b: i32): i32 => b == 0 ? 0 : a / b,
    (a: i32, b: i32): i32 => a << (b & 31),
    (a: i32, b: i32): i32 => max(a, b),
  ];
  let acc = 7;
  for (let i = 0; i < 30; i++) {
    acc = ops[i % ops.length](acc, (i * 5) % 11 - 3);
    if (i % 5 == 4) print("ops " + i.toString() + " " + acc.toString());
  }

  // Callbacks to Array methods: named functions and arrows.
  let xs: i32[] = [];
  for (let i = 0; i < 20; i++) xs.push((i * 37) % 23 - 11);
  let mapped = xs.map<i32>((x: i32, i: i32, a: Array<i32>): i32 => x * i);
  let filtered = xs.filter((x: i32, i: i32, a: Array<i32>): bool => x > 0);
  let total = xs.reduce<i32>((acc: i32, x: i32, i: i32, a: Array<i32>): i32 => acc + x * x, 0);
  let fromRight = xs.reduceRight<string>((acc: string, x: i32, i: i32, a: Array<i32>): string => acc + x.toString() + ",", "");
  print("map " + mapped.join(" "));
  print("filter " + filtered.join(" "));
  print("reduce " + total.toString() + " right " + fromRight);
  print("some " + (xs.some((x: i32, i: i32, a: Array<i32>): bool => x == 11) ? "1" : "0") +
    " every " + (xs.every((x: i32, i: i32, a: Array<i32>): bool => x > -12) ? "1" : "0") +
    " find " + xs.findIndex((x: i32, i: i32, a: Array<i32>): bool => x < -8).toString() +
    " findLast " + xs.findLastIndex((x: i32, i: i32, a: Array<i32>): bool => x < -8).toString());
  xs.forEach((x: i32, i: i32, a: Array<i32>): void => { counter += x; });
  print("forEach counter " + counter.toString());

  // Optional parameter through a direct call and an indirect one.
  print("default " + withDefault(1).toString() + " " + withDefault(2, 3).toString());
  let wd = withDefault;
  print("default-indirect " + wd(4, 5).toString());

  // Functions as fields.
  let btns: Button[] = [
    new Button("ok", (b: Button): string => b.label + " pressed " + b.clicks.toString()),
    new Button("cancel", (b: Button): string => b.clicks > 2 ? "stop" : "go " + b.label),
  ];
  for (let i = 0; i < 4; i++) print("click " + btns[0].click() + " / " + btns[1].click());

  // Numeric integration with function arguments.
  let fns: F2F[] = [
    (x: f64): f64 => x * x,
    (x: f64): f64 => Math.sin(x),
    (x: f64): f64 => 1.0 / (1.0 + x * x),
    (x: f64): f64 => Math.exp(-x * x),
  ];
  for (let i = 0; i < fns.length; i++) {
    print("integrate " + i.toString() + " " + integrate(fns[i], 0.0, 2.0, 64).toString() + " " +
      integrate(fns[i], -1.0, 1.0, 100).toString());
  }
  // Sorting with comparator references chosen at run time.
  let cmps: Array<(a: i32, b: i32) => i32> = [
    (a: i32, b: i32): i32 => a - b,
    (a: i32, b: i32): i32 => b - a,
    (a: i32, b: i32): i32 => (abs(a) - abs(b)) * 100 + (a - b),
    (a: i32, b: i32): i32 => (a & 3) - (b & 3) != 0 ? (a & 3) - (b & 3) : a - b,
  ];
  for (let i = 0; i < cmps.length; i++) {
    let c = xs.slice(0);
    c.sort(cmps[i]);
    print("sort " + i.toString() + " " + c.join(","));
  }
}
run();
