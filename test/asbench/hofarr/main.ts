// Array#map, filter, reduce, some, every and findIndex, each given a function
// reference.  AssemblyScript has no closures over locals, so a callback is a
// static Function object whose first word is a table index, and every element
// costs a store of the argument count to the $~argumentsLength global, a load
// of that index, and a call_indirect.  binaryen inlines the array methods
// themselves into the loop, but the callbacks stay indirect: they are picked
// from small tables by the round number, so the producer cannot know them.
// The one literal lambda, passed to `some`, is still loaded from a constant
// address in the data segment and called indirectly -- a target a consumer
// could resolve if it can tell that address is never written.
//
// map and filter allocate a new array each round, so the allocator runs too.
// sortarr has one call_indirect per comparison inside a library sort; here the
// indirect calls sit in loops that the producer has already inlined.
import { print } from "../wasi";

const N = 2048;

function triple(x: i32, i: i32, a: Array<i32>): i32 { return (x * 3) ^ i; }
function square(x: i32, i: i32, a: Array<i32>): i32 { return x * x - i; }
function odd(x: i32, i: i32, a: Array<i32>): bool { return (x & 1) != 0; }
function small(x: i32, i: i32, a: Array<i32>): bool { return (x & 0xff) < 0x60; }
function add(acc: i32, x: i32, i: i32, a: Array<i32>): i32 { return acc + (x >> 2); }
function xorRot(acc: i32, x: i32, i: i32, a: Array<i32>): i32 { return rotl<i32>(acc, 5) ^ x; }

// The callbacks are picked per round from tables, so which function each
// higher-order call receives is data, not something the producer can see.
let maps: Array<(x: i32, i: i32, a: Array<i32>) => i32> = [triple, square];
let preds: Array<(x: i32, i: i32, a: Array<i32>) => bool> = [odd, small];
let folds: Array<(acc: i32, x: i32, i: i32, a: Array<i32>) => i32> = [add, xorRot];

let a = new Array<i32>(N);
for (let i = 0; i < N; i++) a[i] = i * 2654435761;

let total = 0;
for (let r = 0; r < 9750; r++) {
  a[r & (N - 1)] = r;
  let m = maps[r & 1];
  let p = preds[(r >> 1) & 1];
  let f = folds[(r >> 2) & 1];
  let b = a.map<i32>(m);
  let c = b.filter(p);
  let s = c.reduce<i32>(f, r);
  let any = c.some((x: i32, i: i32, a: Array<i32>): bool => x == 12345);
  let all = b.every(p);
  let at = b.findIndex(p);
  total += s + c.length + at + (any ? 7 : 0) + (all ? 11 : 0);
}
print("total = " + total.toString());
