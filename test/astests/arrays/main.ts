// Array<T>, the growable managed array: push/pop/shift/unshift (which move
// every element), splice deleting at both ends and the middle (it takes no
// items to insert in AssemblyScript), slice with negative indices, concat, join, reverse, indexOf /
// lastIndexOf / includes (including NaN, which includes finds and indexOf
// does not), fill with ranges, copyWithin, flat, at(), length truncation and
// extension, and arrays of strings, of f64, of i64, of objects and of arrays.
// Growth reallocates the backing buffer, so element addresses move and every
// later access must reload them.  The array is printed after every step.
import { print } from "../wasi";

class P { constructor(public id: i32, public tag: string) {} }

function show(a: i32[]): string { return "[" + a.join(",") + "] len=" + a.length.toString(); }

function run(): void {
  let a: i32[] = [];
  for (let i = 0; i < 10; i++) { a.push(i * 3 - 7); }
  print("push " + show(a));
  print("pop " + a.pop().toString() + " " + a.pop().toString() + " " + show(a));
  print("shift " + a.shift().toString() + " " + show(a));
  a.unshift(100); a.unshift(-100);
  print("unshift " + show(a));
  let removed = a.splice(2, 3);
  print("splice(2,3) removed=" + show(removed) + " a=" + show(a));
  removed = a.splice(-2);
  print("splice(-2) removed=" + show(removed) + " a=" + show(a));
  removed = a.splice(0, 0);
  print("splice(0,0) removed=" + show(removed) + " a=" + show(a));
  for (let i = 0; i < 5; i++) a.splice(1, 0); // no-ops
  print("slice(1,-1) " + show(a.slice(1, -1)) + " slice(-3) " + show(a.slice(-3)) + " slice(5,2) " + show(a.slice(5, 2)));
  let b = a.concat([7, 8, 9]).concat(a);
  print("concat " + show(b));
  b.reverse();
  print("reverse " + show(b));
  print("indexOf 8=" + b.indexOf(8).toString() + " -100=" + b.indexOf(-100).toString() +
    " 999=" + b.indexOf(999).toString() + " last -100=" + b.lastIndexOf(-100).toString() +
    " from=" + b.indexOf(-100, 3).toString() + " includes 9=" + (b.includes(9) ? "1" : "0") +
    " includes 10=" + (b.includes(10) ? "1" : "0"));
  b.fill(0, 2, 5);
  print("fill(0,2,5) " + show(b));
  b.fill(-1, -3);
  print("fill(-1,-3) " + show(b));
  b.copyWithin(0, 5, 9);
  print("copyWithin(0,5,9) " + show(b));
  print("at 0=" + b.at(0).toString() + " -1=" + b.at(-1).toString() + " -2=" + b.at(-2).toString());
  b.length = 4;
  print("truncate " + show(b));
  b.length = 7;
  print("extend " + show(b));

  // Growth: push enough to reallocate several times, checking contents.
  let g: i32[] = [];
  let sum: i64 = 0;
  for (let i = 0; i < 3000; i++) {
    g.push(i ^ (i >> 3));
    if ((i & (i + 1)) == 0) {
      let s: i64 = 0;
      for (let j = 0; j < g.length; j++) s += <i64>g[j] * <i64>(j + 1);
      print("grow " + g.length.toString() + " weighted=" + s.toString());
    }
  }
  while (g.length > 5) { sum += <i64>g.pop(); if (g.length % 500 == 0) print("shrink " + g.length.toString() + " " + sum.toString()); }
  // shift/unshift churn: a queue.
  let q: i32[] = [];
  let out = "";
  for (let i = 0; i < 40; i++) {
    q.push(i);
    if (i % 3 == 2) out += q.shift().toString() + " ";
    if (i % 7 == 6) q.unshift(-i);
  }
  print("queue out=" + out + " left=" + show(q));

  // Floats: NaN equality differences between indexOf and includes.
  let f: f64[] = [1.5, NaN, -0.0, 0.0, Infinity, 2.25];
  print("f64 indexOf NaN=" + f.indexOf(NaN).toString() + " includes NaN=" + (f.includes(NaN) ? "1" : "0") +
    " indexOf 0=" + f.indexOf(0.0).toString() + " indexOf -0=" + f.indexOf(-0.0).toString() + " join=" + f.join("|"));
  let l: i64[] = [0x7fffffffffffffff, -1, 0, -0x8000000000000000];
  l.push(42); l.unshift(-42);
  print("i64 " + l.join(";") + " idx=" + l.indexOf(-0x8000000000000000).toString());
  let s: string[] = ["delta", "alpha", "charlie", "bravo"];
  s.push("echo");
  // AssemblyScript's splice only deletes; insertion is splice + push + concat.
  let tail = s.splice(1);
  s.push("ALPHA"); s.push("alpha2");
  s = s.concat(tail.slice(1));
  print("str " + s.join("/") + " idx=" + s.indexOf("bravo").toString() + " inc=" + (s.includes("alpha") ? "1" : "0"));
  // Arrays of objects and arrays of arrays.
  let ps: P[] = [];
  for (let i = 0; i < 8; i++) ps.push(new P(i * 11 % 8, "p" + i.toString()));
  let pr = ps.slice(2, 6).reverse();
  let line = "";
  for (let i = 0; i < pr.length; i++) line += pr[i].tag + "=" + pr[i].id.toString() + " ";
  print("objs " + line + "idx=" + ps.indexOf(pr[1]).toString());
  let grid: i32[][] = [];
  for (let r = 0; r < 5; r++) {
    let row: i32[] = [];
    for (let c = 0; c <= r; c++) row.push(r == 0 || c == 0 || c == r ? 1 : grid[r - 1][c - 1] + grid[r - 1][c]);
    grid.push(row);
  }
  for (let r = 0; r < grid.length; r++) print("pascal " + grid[r].join(" "));
  let flat = grid.flat();
  print("flat " + show(flat));
  // Array.isArray and new Array with length.
  let pre = new Array<i32>(6);
  for (let i = 0; i < pre.length; i++) pre[i] = i * i;
  print("prealloc " + show(pre) + " isArray=" + (isArray(pre) ? "1" : "0"));
}
run();
