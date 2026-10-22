// StaticArray<T>: a fixed-length managed array whose elements follow the
// object header directly (no separate backing buffer, one indirection fewer
// than Array<T>).  Element types i8 through f64, bool, strings and objects;
// literal initialisers (placed in a data segment and copied); checked and
// unchecked() indexing (unchecked drops the bounds check, so its address
// arithmetic stands alone); conversion to and from Array via fromArray /
// slice / concat; StaticArray of StaticArray; and the in-place algorithms
// people write on fixed buffers -- prefix sums, a rotation, a matrix
// transpose and multiply, a ring buffer with masks.
import { print } from "../wasi";

const PRIMES: StaticArray<i32> = [2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37];
const SMALL: StaticArray<i8> = [-128, -1, 0, 1, 127];
const WEIGHTS: StaticArray<f64> = [0.5, 0.25, 0.125, 0.0625];

class Cell { constructor(public v: i32) {} }

function sa2s<T>(a: StaticArray<T>): string {
  let s = "";
  for (let i = 0; i < a.length; i++) s += (i > 0 ? "," : "") + a[i].toString();
  return s;
}

function matmul(a: StaticArray<f64>, b: StaticArray<f64>, n: i32): StaticArray<f64> {
  let c = new StaticArray<f64>(n * n);
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++) {
      let s = 0.0;
      for (let k = 0; k < n; k++) s += unchecked(a[i * n + k]) * unchecked(b[k * n + j]);
      unchecked(c[i * n + j] = s);
    }
  return c;
}

function run(): void {
  print("primes " + sa2s<i32>(PRIMES) + " len=" + PRIMES.length.toString());
  print("small " + sa2s<i8>(SMALL) + " weights " + sa2s<f64>(WEIGHTS));
  // Every element width.
  let a8 = new StaticArray<u8>(10), a16 = new StaticArray<i16>(10), a32 = new StaticArray<u32>(10);
  let a64 = new StaticArray<i64>(10), af = new StaticArray<f32>(10), ab = new StaticArray<bool>(10);
  for (let i = 0; i < 10; i++) {
    a8[i] = <u8>(i * 53); a16[i] = <i16>(i * -7000); a32[i] = <u32>i * 0x30000001;
    a64[i] = <i64>i * -0x123456789; af[i] = <f32>i / <f32>7; ab[i] = (i % 3) == 0;
  }
  print("u8 " + sa2s<u8>(a8)); print("i16 " + sa2s<i16>(a16)); print("u32 " + sa2s<u32>(a32));
  print("i64 " + sa2s<i64>(a64)); print("f32 " + sa2s<f32>(af)); print("bool " + sa2s<bool>(ab));
  // Unchecked prefix sums and a rotation in place.
  let ps = new StaticArray<i32>(32);
  for (let i = 0; i < 32; i++) unchecked(ps[i] = (i * 7) % 11 - 5);
  for (let i = 1; i < 32; i++) unchecked(ps[i] += ps[i - 1]);
  print("prefix " + sa2s<i32>(ps));
  // rotate left by 5 with three reversals
  function rev(a: StaticArray<i32>, lo: i32, hi: i32): void {
    while (lo < hi) { let t = unchecked(a[lo]); unchecked(a[lo] = a[hi]); unchecked(a[hi] = t); lo++; hi--; }
  }
  rev(ps, 0, 4); rev(ps, 5, 31); rev(ps, 0, 31);
  print("rotated " + sa2s<i32>(ps));
  // Strings and objects.
  let names = new StaticArray<string>(6);
  for (let i = 0; i < 6; i++) names[i] = "n" + (i * i).toString();
  names[2] = names[0] + names[5];
  print("names " + names.join("|") + " idx=" + names.indexOf("n25").toString() + " inc=" + (names.includes("n0n25") ? "1" : "0"));
  let cells = new StaticArray<Cell>(5);
  for (let i = 0; i < 5; i++) cells[i] = new Cell(i * 100);
  cells[4] = cells[1];
  cells[1].v = -1;
  let cs = "";
  for (let i = 0; i < 5; i++) cs += cells[i].v.toString() + " ";
  print("cells " + cs);
  // Conversions with Array.
  let arr: i32[] = [9, 8, 7, 6];
  let fromA = StaticArray.fromArray<i32>(arr);
  arr[0] = 0;
  let sl = PRIMES.slice<StaticArray<i32>>(3, -2);
  let cc = fromA.concat<StaticArray<i32>>(sl);
  let toA = PRIMES.slice<Array<i32>>(0, 4);
  toA.push(99);
  print("fromArray " + sa2s<i32>(fromA) + " slice " + sa2s<i32>(sl) + " concat " + sa2s<i32>(cc) + " toArray " + toA.join(","));
  // Matrices: transpose and multiply.
  let n = 5;
  let m = new StaticArray<f64>(n * n), t = new StaticArray<f64>(n * n);
  for (let i = 0; i < n * n; i++) m[i] = <f64>((i * 13) % 7) - 2.5;
  for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) t[j * n + i] = m[i * n + j];
  let p = matmul(m, t, n);
  for (let i = 0; i < n; i++) {
    let row = "";
    for (let j = 0; j < n; j++) row += p[i * n + j].toString() + " ";
    print("mmt " + row);
  }
  // Nested StaticArrays: a jagged table.
  let tab = new StaticArray<StaticArray<u16>>(6);
  for (let i = 0; i < 6; i++) {
    tab[i] = new StaticArray<u16>(i + 1);
    for (let j = 0; j <= i; j++) tab[i][j] = <u16>((i + 1) * (j + 3) * 1009);
  }
  for (let i = 0; i < 6; i++) print("jag " + sa2s<u16>(tab[i]));
  // Ring buffer with mask indexing.
  let ring = new StaticArray<i64>(16);
  let head = 0;
  let total: i64 = 0;
  for (let i = 0; i < 100; i++) {
    total -= unchecked(ring[head]);
    let v = <i64>i * <i64>i * 1000003 - 7;
    unchecked(ring[head] = v);
    total += v;
    head = (head + 1) & 15;
    if (i % 10 == 9) print("ring " + i.toString() + " window=" + total.toString() + " head=" + head.toString());
  }
  // Weighted average through the const array.
  let wsum = 0.0;
  for (let i = 0; i < PRIMES.length; i++) wsum += <f64>PRIMES[i] * WEIGHTS[i & 3];
  print("weighted " + wsum.toString());
}
run();
