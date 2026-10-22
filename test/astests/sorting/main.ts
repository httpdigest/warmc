// Sorting, two ways.  First the library: Array#sort and typed-array sort with
// the default comparator (numeric for numbers -- AssemblyScript does not sort
// numbers as strings -- NaN last for floats, and for i32 a plain `a - b` that
// overflows on wide ranges) and with user comparators
// called indirectly (descending, by key, by string length then lexically,
// on objects).  AssemblyScript sorts small arrays by insertion sort and
// larger ones by weak-heap sort, so sizes on both sides of that threshold are
// used.  Then hand-written algorithms on the same data -- quicksort
// (Hoare), merge sort, heap sort, shell sort, LSD radix sort on u32 and
// counting sort -- each checked against the library result, with a
// checksum and the first elements printed.
import { print } from "../wasi";

class Rec { constructor(public key: i32, public name: string) {} }

let seed: u32 = 12345;
function rnd(): u32 { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }

function head(a: i32[], n: i32): string { return a.slice(0, n).join(","); }
function sig(a: i32[]): i64 { let s: i64 = 0; for (let i = 0; i < a.length; i++) s = s * 31 + <i64>a[i]; return s; }

function quick(a: i32[], lo: i32, hi: i32): void {
  while (lo < hi) {
    let p = a[(lo + hi) >>> 1];
    let i = lo - 1, j = hi + 1;
    while (true) {
      do i++; while (a[i] < p);
      do j--; while (a[j] > p);
      if (i >= j) break;
      let t = a[i]; a[i] = a[j]; a[j] = t;
    }
    // recurse into the smaller half, loop on the larger
    if (j - lo < hi - j) { quick(a, lo, j); lo = j + 1; } else { quick(a, j + 1, hi); hi = j; }
  }
}

function mergeSort(a: i32[], tmp: i32[], lo: i32, hi: i32): void {
  if (hi - lo < 2) return;
  let mid = (lo + hi) >> 1;
  mergeSort(a, tmp, lo, mid);
  mergeSort(a, tmp, mid, hi);
  let i = lo, j = mid, k = lo;
  while (i < mid && j < hi) tmp[k++] = a[i] <= a[j] ? a[i++] : a[j++];
  while (i < mid) tmp[k++] = a[i++];
  while (j < hi) tmp[k++] = a[j++];
  for (k = lo; k < hi; k++) a[k] = tmp[k];
}

function siftDown(a: i32[], i: i32, n: i32): void {
  while (true) {
    let l = 2 * i + 1;
    if (l >= n) return;
    let m = l + 1 < n && a[l + 1] > a[l] ? l + 1 : l;
    if (a[i] >= a[m]) return;
    let t = a[i]; a[i] = a[m]; a[m] = t;
    i = m;
  }
}
function heapSort(a: i32[]): void {
  let n = a.length;
  for (let i = (n >> 1) - 1; i >= 0; i--) siftDown(a, i, n);
  for (let e = n - 1; e > 0; e--) { let t = a[0]; a[0] = a[e]; a[e] = t; siftDown(a, 0, e); }
}

function shellSort(a: i32[]): void {
  let gaps: i32[] = [701, 301, 132, 57, 23, 10, 4, 1];
  for (let g = 0; g < gaps.length; g++) {
    let gap = gaps[g];
    for (let i = gap; i < a.length; i++) {
      let t = a[i];
      let j = i;
      while (j >= gap && a[j - gap] > t) { a[j] = a[j - gap]; j -= gap; }
      a[j] = t;
    }
  }
}

// LSD radix on u32 viewed through i32 by flipping the sign bit.
function radixSort(a: i32[]): void {
  let n = a.length;
  let src = new Uint32Array(n), dst = new Uint32Array(n);
  for (let i = 0; i < n; i++) src[i] = <u32>a[i] ^ 0x80000000;
  let count = new Int32Array(257);
  for (let shift: u32 = 0; shift < 32; shift += 8) {
    count.fill(0);
    for (let i = 0; i < n; i++) count[((src[i] >> shift) & 0xff) + 1]++;
    for (let b = 0; b < 256; b++) count[b + 1] += count[b];
    for (let i = 0; i < n; i++) dst[count[(src[i] >> shift) & 0xff]++] = src[i];
    let t = src; src = dst; dst = t;
  }
  for (let i = 0; i < n; i++) a[i] = <i32>(src[i] ^ 0x80000000);
}

function countingSort(a: i32[], lo: i32, hi: i32): void {
  let c = new Int32Array(hi - lo + 1);
  for (let i = 0; i < a.length; i++) c[a[i] - lo]++;
  let k = 0;
  for (let v = 0; v < c.length; v++) for (let r = 0; r < c[v]; r++) a[k++] = v + lo;
}

function same(a: i32[], b: i32[]): string {
  if (a.length != b.length) return "LEN";
  for (let i = 0; i < a.length; i++) if (a[i] != b[i]) return "DIFF@" + i.toString();
  return "same";
}

function run(): void {
  const sizes: i32[] = [0, 1, 2, 3, 7, 16, 33, 64, 65, 200, 1000];
  for (let si = 0; si < sizes.length; si++) {
    let n = sizes[si];
    let base: i32[] = [];
    for (let i = 0; i < n; i++) base.push(<i32>(rnd() % 2001) - 1000);
    let lib = base.slice(0); lib.sort();
    let desc = base.slice(0); desc.sort((x: i32, y: i32): i32 => y - x);
    let byMod = base.slice(0);
    byMod.sort((x: i32, y: i32): i32 => { let d = (x & 7) - (y & 7); return d != 0 ? d : x - y; });
    print("n=" + n.toString() + " lib=" + head(lib, 8) + " sig=" + sig(lib).toString() +
      " desc=" + head(desc, 5) + " mod=" + head(byMod, 6));
    let q = base.slice(0); if (n > 0) quick(q, 0, n - 1);
    let m = base.slice(0); mergeSort(m, new Array<i32>(n), 0, n);
    let h = base.slice(0); heapSort(h);
    let sh = base.slice(0); shellSort(sh);
    let rx = base.slice(0); radixSort(rx);
    let ct = base.slice(0); countingSort(ct, -1000, 1000);
    print("  quick=" + same(q, lib) + " merge=" + same(m, lib) + " heap=" + same(h, lib) +
      " shell=" + same(sh, lib) + " radix=" + same(rx, lib) + " count=" + same(ct, lib));
  }
  // Wide-range radix against the library.
  let wide: i32[] = [];
  for (let i = 0; i < 500; i++) wide.push(<i32>rnd());
  // The default i32 comparator is `a - b`, which overflows across the full
  // range, so the library's default order is not sorted here; it is still
  // deterministic and printed.  A three-way comparator gives the true order.
  let wd = wide.slice(0); wd.sort();
  let wl = wide.slice(0); wl.sort((a: i32, b: i32): i32 => i32(a > b) - i32(a < b));
  let wr = wide.slice(0); radixSort(wr);
  let wq = wide.slice(0); quick(wq, 0, wq.length - 1);
  let wh = wide.slice(0); heapSort(wh);
  print("wide default " + head(wd, 4) + " sig=" + sig(wd).toString());
  print("wide " + head(wl, 4) + " radix=" + same(wr, wl) + " quick=" + same(wq, wl) +
    " heap=" + same(wh, wl) + " sig=" + sig(wl).toString());

  // Floats: NaN, infinities, signed zeros with the default comparator.
  let fs: f64[] = [3.5, NaN, -1.0, Infinity, 0.0, -0.0, -Infinity, 2.0, NaN, 1e-300, -1e300];
  fs.sort();
  print("f64 " + fs.join(" "));
  let f32s = new Float32Array(9);
  const fv: f32[] = [2.5, -1.25, NaN, 0.0, -0.0, 7.0, -Infinity, 1e-30, -3.0];
  for (let i = 0; i < 9; i++) f32s[i] = fv[i];
  f32s.sort();
  let line = "";
  for (let i = 0; i < 9; i++) line += f32s[i].toString() + " ";
  print("f32 " + line);
  // Typed arrays: default sort is numeric and signedness-aware.
  let i8s = new Int8Array(12), u8s = new Uint8Array(12), u32s = new Uint32Array(12), i64s = new Int64Array(12);
  for (let i = 0; i < 12; i++) { let r = rnd(); i8s[i] = <i8>r; u8s[i] = <u8>r; u32s[i] = r; i64s[i] = <i64>r * <i64>(i - 6); }
  i8s.sort(); u8s.sort(); u32s.sort(); i64s.sort();
  print("i8 " + i8s.join(",")); print("u8 " + u8s.join(","));
  print("u32 " + u32s.join(",")); print("i64 " + i64s.join(","));
  // Strings and objects.
  let words = "the quick brown fox jumps over the lazy dog and runs far away from the big bad wolf".split(" ");
  let w1 = words.slice(0); w1.sort();
  print("words " + w1.join(" "));
  let w2 = words.slice(0);
  w2.sort((a: string, b: string): i32 => a.length != b.length ? a.length - b.length : (a < b ? -1 : a > b ? 1 : 0));
  print("bylen " + w2.join(" "));
  let recs: Rec[] = [];
  for (let i = 0; i < 70; i++) recs.push(new Rec(<i32>(rnd() % 10), "r" + i.toString()));
  recs.sort((a: Rec, b: Rec): i32 => a.key != b.key ? a.key - b.key : (a.name < b.name ? -1 : 1));
  line = "";
  for (let i = 0; i < recs.length; i += 5) line += recs[i].key.toString() + ":" + recs[i].name + " ";
  print("recs " + line);
}
run();
