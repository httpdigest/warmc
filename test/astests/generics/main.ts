// Generics: AssemblyScript monomorphises, so each instantiation is its own
// code, and compile-time type tests (isInteger, isFloat, isSigned,
// isReference, isString, sizeof, nameof) pick one arm per copy.  Covered:
// generic functions over every numeric type, generic classes (a growable
// vector, a min-heap ordered by a comparator, a pair, an optional), a generic
// class constrained to an interface (`T extends Keyed`) so its methods can
// call the interface on T, generic methods on a non-generic class, nested
// generics (Vec<Pair<i32,string>>), and generic code instantiated at
// reference types.  A copy given the wrong type's arithmetic (an i64 add
// where an i32 wrap was meant, or an f32 rounding missing) shows in the sums.
import { print } from "../wasi";

function describe<T>(x: T): string {
  let s = nameof<T>() + " size=" + sizeof<T>().toString();
  if (isInteger<T>()) s += " int" + (isSigned<T>() ? " signed" : " unsigned");
  if (isFloat<T>()) s += " float";
  if (isReference<T>()) s += " ref";
  if (isString<T>()) s += " string";
  return s;
}

// One body: sum of squares with wrapping at T's width for integers.
function sumSquares<T>(n: i32): T {
  let acc: T = <T>0;
  for (let i = 1; i <= n; i++) {
    let v = <T>i * <T>37;
    acc += v * v;
  }
  return acc;
}

function maxOf<T>(xs: T[]): T {
  let m = xs[0];
  for (let i = 1; i < xs.length; i++) if (xs[i] > m) m = xs[i];
  return m;
}

class Vec<T> {
  data: StaticArray<T>;
  len: i32 = 0;
  constructor(cap: i32 = 2) { this.data = new StaticArray<T>(cap); }
  push(v: T): void {
    if (this.len == this.data.length) {
      let nd = new StaticArray<T>(this.data.length * 2);
      for (let i = 0; i < this.len; i++) nd[i] = this.data[i];
      this.data = nd;
    }
    this.data[this.len++] = v;
  }
  get(i: i32): T { return this.data[i]; }
  map<U>(f: (x: T) => U): Vec<U> {
    let r = new Vec<U>(this.len + 1);
    for (let i = 0; i < this.len; i++) r.push(f(this.data[i]));
    return r;
  }
}

class Heap<T> {
  items: T[] = [];
  constructor(public less: (a: T, b: T) => bool) {}
  push(v: T): void {
    let a = this.items;
    a.push(v);
    let i = a.length - 1;
    while (i > 0) {
      let p = (i - 1) >> 1;
      if (!this.less(a[i], a[p])) break;
      let t = a[i]; a[i] = a[p]; a[p] = t;
      i = p;
    }
  }
  pop(): T {
    let a = this.items;
    let top = a[0];
    let last = a.pop();
    if (a.length > 0) {
      a[0] = last;
      let i = 0;
      while (true) {
        let l = 2 * i + 1, r = l + 1, m = i;
        if (l < a.length && this.less(a[l], a[m])) m = l;
        if (r < a.length && this.less(a[r], a[m])) m = r;
        if (m == i) break;
        let t = a[i]; a[i] = a[m]; a[m] = t;
        i = m;
      }
    }
    return top;
  }
  get size(): i32 { return this.items.length; }
}

class Pair<A, B> {
  constructor(public first: A, public second: B) {}
  swap(): Pair<B, A> { return new Pair<B, A>(this.second, this.first); }
}

interface Keyed {
  key(): i32;
}
class Item implements Keyed {
  constructor(public k: i32, public label: string) {}
  key(): i32 { return this.k; }
}
class Other implements Keyed {
  constructor(public v: f64) {}
  key(): i32 { return <i32>(this.v * 10.0); }
}

// Constrained: T's key() is callable because T extends Keyed.
class Index<T extends Keyed> {
  buckets: Array<Array<T>> = [];
  constructor(n: i32) { for (let i = 0; i < n; i++) this.buckets.push(new Array<T>()); }
  add(x: T): void { this.buckets[<i32>(<u32>x.key() % <u32>this.buckets.length)].push(x); }
  count(k: i32): i32 {
    let b = this.buckets[<i32>(<u32>k % <u32>this.buckets.length)];
    let c = 0;
    for (let i = 0; i < b.length; i++) if (b[i].key() == k) c++;
    return c;
  }
}

class Util {
  static clamp<T>(x: T, lo: T, hi: T): T { return x < lo ? lo : x > hi ? hi : x; }
  static fill<T>(n: i32, v: T): T[] { let a = new Array<T>(n); for (let i = 0; i < n; i++) a[i] = v; return a; }
}

function i32Less(a: i32, b: i32): bool { return a < b; }
function strLonger(a: string, b: string): bool { return a.length > b.length || (a.length == b.length && a < b); }
function pairLess(a: Pair<i32, string>, b: Pair<i32, string>): bool { return a.first < b.first; }

function run(): void {
  print(describe<i8>(0)); print(describe<u8>(0)); print(describe<i16>(0)); print(describe<u16>(0));
  print(describe<i32>(0)); print(describe<u32>(0)); print(describe<i64>(0)); print(describe<u64>(0));
  print(describe<f32>(0)); print(describe<f64>(0)); print(describe<bool>(false));
  print(describe<string>("")); print(describe<Item>(new Item(1, "x")));
  for (let n = 1; n <= 401; n += 50) {
    print("sumsq " + n.toString() + " u8=" + sumSquares<u8>(n).toString() + " i16=" + sumSquares<i16>(n).toString() +
      " i32=" + sumSquares<i32>(n).toString() + " u32=" + sumSquares<u32>(n).toString() +
      " i64=" + sumSquares<i64>(n).toString() + " f32=" + sumSquares<f32>(n).toString() +
      " f64=" + sumSquares<f64>(n).toString());
  }
  print("max " + maxOf<i32>([3, -1, 77, 12]).toString() + " " + maxOf<u8>([3, 255, 9]).toString() + " " +
    maxOf<f64>([0.5, -2.0, 1.25]).toString() + " " + maxOf<string>(["pear", "apple", "zoo", "fig"]) + " " +
    maxOf<i64>([-5, -0x7fffffffffff, -1]).toString());

  let v = new Vec<i32>();
  for (let i = 0; i < 40; i++) v.push(i * i - 20 * i);
  let vs = v.map<string>((x: i32): string => "<" + x.toString() + ">");
  let vf = v.map<f64>((x: i32): f64 => <f64>x / 8.0);
  let line = "";
  for (let i = 0; i < vs.len; i += 3) line += vs.get(i) + vf.get(i).toString() + " ";
  print("vec " + v.len.toString() + " " + line);

  let h = new Heap<i32>(i32Less);
  let x: u32 = 99;
  for (let i = 0; i < 60; i++) { x = x * 1664525 + 1013904223; h.push(<i32>(x >> 20) - 2048); }
  line = "";
  while (h.size > 0) line += h.pop().toString() + " ";
  print("heap " + line);
  let hs = new Heap<string>(strLonger);
  const words = ["a", "banana", "kiwi", "apple", "fig", "cherry", "date", "elderberry", "grape", "lime"];
  for (let i = 0; i < words.length; i++) hs.push(words[i]);
  line = "";
  while (hs.size > 0) line += hs.pop() + " ";
  print("heap-str " + line);
  let hp = new Heap<Pair<i32, string>>(pairLess);
  for (let i = 0; i < words.length; i++) hp.push(new Pair<i32, string>((i * 7) % 10, words[i]));
  line = "";
  while (hp.size > 0) { let p = hp.pop().swap(); line += p.first + ":" + p.second.toString() + " "; }
  print("heap-pair " + line);

  let idx = new Index<Item>(7);
  for (let i = 0; i < 100; i++) idx.add(new Item((i * i) % 23, "i" + i.toString()));
  line = "";
  for (let k = 0; k < 23; k++) line += idx.count(k).toString() + " ";
  print("index " + line);
  let idx2 = new Index<Other>(5);
  for (let i = 0; i < 50; i++) idx2.add(new Other(<f64>(i % 9) * 0.31));
  line = "";
  for (let k = 0; k < 30; k++) line += idx2.count(k).toString() + " ";
  print("index2 " + line);

  print("clamp " + Util.clamp<i32>(-5, 0, 10).toString() + " " + Util.clamp<i32>(50, 0, 10).toString() + " " +
    Util.clamp<f64>(0.5, 0.0, 1.0).toString() + " " + Util.clamp<u8>(200, 10, 100).toString() + " " +
    Util.clamp<string>("m", "b", "k"));
  let filled = Util.fill<Pair<string, f32>>(3, new Pair<string, f32>("z", 1.5));
  filled[1] = new Pair<string, f32>("y", -2.25);
  for (let i = 0; i < filled.length; i++) print("fill " + filled[i].first + " " + filled[i].second.toString());
}
run();
