// Hand-written hash tables, the kind AssemblyScript code writes when Map's
// generality costs too much: an open-addressing table with linear probing
// and backward-shift deletion over parallel StaticArrays of u64 keys and i32
// values; a Robin Hood variant that tracks probe distances; and a chained
// table of string keys with FNV-1a hashing and a growable bucket array.
// Hash functions are multiply-xorshift mixes (wyhash/murmur finalisers) in
// u64 and u32, masked to a power-of-two capacity.  Every table is
// cross-checked against Map after each phase of inserts, updates and
// deletes, and the probe statistics are printed, since they depend on every
// bit of every hash.
import { print } from "../wasi";

function mix64(x: u64): u64 {
  x ^= x >> 33; x *= 0xff51afd7ed558ccd;
  x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53;
  x ^= x >> 33;
  return x;
}

function fnv1a(s: string): u32 {
  let h: u32 = 2166136261;
  for (let i = 0; i < s.length; i++) { let c = s.charCodeAt(i); h ^= c & 0xff; h *= 16777619; h ^= c >> 8; h *= 16777619; }
  return h;
}

const EMPTY: u64 = 0xffffffffffffffff;

class LinearTable {
  keys: StaticArray<u64>;
  vals: StaticArray<i32>;
  size: i32 = 0;
  mask: i32;
  probes: i64 = 0;
  constructor(capLog: i32) {
    let cap = 1 << capLog;
    this.mask = cap - 1;
    this.keys = new StaticArray<u64>(cap);
    this.vals = new StaticArray<i32>(cap);
    for (let i = 0; i < cap; i++) this.keys[i] = EMPTY;
  }
  private slot(k: u64): i32 { return <i32>(mix64(k) & <u64>this.mask); }
  grow(): void {
    let ok = this.keys, ov = this.vals;
    let cap = (this.mask + 1) * 2;
    this.mask = cap - 1;
    this.keys = new StaticArray<u64>(cap);
    this.vals = new StaticArray<i32>(cap);
    for (let i = 0; i < cap; i++) this.keys[i] = EMPTY;
    this.size = 0;
    for (let i = 0; i < ok.length; i++) if (ok[i] != EMPTY) this.set(ok[i], ov[i]);
  }
  set(k: u64, v: i32): void {
    if ((this.size + 1) * 4 > (this.mask + 1) * 3) this.grow();
    let i = this.slot(k);
    while (true) {
      this.probes++;
      let cur = unchecked(this.keys[i]);
      if (cur == EMPTY) { unchecked(this.keys[i] = k); unchecked(this.vals[i] = v); this.size++; return; }
      if (cur == k) { unchecked(this.vals[i] = v); return; }
      i = (i + 1) & this.mask;
    }
  }
  get(k: u64, dflt: i32): i32 {
    let i = this.slot(k);
    while (true) {
      this.probes++;
      let cur = unchecked(this.keys[i]);
      if (cur == EMPTY) return dflt;
      if (cur == k) return unchecked(this.vals[i]);
      i = (i + 1) & this.mask;
    }
  }
  delete(k: u64): bool {
    let i = this.slot(k);
    while (true) {
      let cur = this.keys[i];
      if (cur == EMPTY) return false;
      if (cur == k) break;
      i = (i + 1) & this.mask;
    }
    // backward-shift deletion
    let j = i;
    while (true) {
      j = (j + 1) & this.mask;
      let kj = this.keys[j];
      if (kj == EMPTY) break;
      let home = this.slot(kj);
      // move kj into the hole at i if its home is not in (i, j]
      let between = i <= j ? (home > i && home <= j) : (home > i || home <= j);
      if (!between) { this.keys[i] = kj; this.vals[i] = this.vals[j]; i = j; }
    }
    this.keys[i] = EMPTY;
    this.size--;
    return true;
  }
}

class RobinHood {
  keys: StaticArray<u64>;
  vals: StaticArray<i64>;
  dist: StaticArray<i8>;
  size: i32 = 0;
  maxDist: i32 = 0;
  constructor(public capLog: i32) {
    let cap = 1 << capLog;
    this.keys = new StaticArray<u64>(cap);
    this.vals = new StaticArray<i64>(cap);
    this.dist = new StaticArray<i8>(cap);
    for (let i = 0; i < cap; i++) this.dist[i] = -1;
  }
  insert(k: u64, v: i64): void {
    let mask = (1 << this.capLog) - 1;
    let i = <i32>(mix64(k ^ 0x9e3779b97f4a7c15) >> <u64>(64 - this.capLog));
    let d: i8 = 0;
    while (true) {
      if (this.dist[i] < 0) { this.keys[i] = k; this.vals[i] = v; this.dist[i] = d; this.size++; this.maxDist = max(this.maxDist, <i32>d); return; }
      if (this.keys[i] == k) { this.vals[i] += v; return; }
      if (this.dist[i] < d) {
        let tk = this.keys[i], tv = this.vals[i], td = this.dist[i];
        this.keys[i] = k; this.vals[i] = v; this.dist[i] = d;
        k = tk; v = tv; d = td;
        this.maxDist = max(this.maxDist, <i32>this.dist[i]);
      }
      i = (i + 1) & mask;
      d++;
    }
  }
  find(k: u64): i64 {
    let mask = (1 << this.capLog) - 1;
    let i = <i32>(mix64(k ^ 0x9e3779b97f4a7c15) >> <u64>(64 - this.capLog));
    let d: i8 = 0;
    while (this.dist[i] >= d) {
      if (this.keys[i] == k) return this.vals[i];
      i = (i + 1) & mask; d++;
    }
    return -1;
  }
}

class Entry { constructor(public key: string, public val: i32, public next: Entry | null) {} }
class Chained {
  buckets: Array<Entry | null>;
  size: i32 = 0;
  longest: i32 = 0;
  constructor(n: i32) { this.buckets = new Array<Entry | null>(n); }
  put(k: string, v: i32): void {
    if (this.size >= this.buckets.length * 2) this.rehash();
    let b = <i32>(fnv1a(k) % <u32>this.buckets.length);
    let e = this.buckets[b];
    let len = 0;
    while (e !== null) { if (e.key == k) { e.val = v; return; } e = e.next; len++; }
    this.buckets[b] = new Entry(k, v, this.buckets[b]);
    this.size++;
    this.longest = max(this.longest, len + 1);
  }
  get(k: string): i32 {
    let e = this.buckets[<i32>(fnv1a(k) % <u32>this.buckets.length)];
    while (e !== null) { if (e.key == k) return e.val; e = e.next; }
    return -1;
  }
  rehash(): void {
    let old = this.buckets;
    this.buckets = new Array<Entry | null>(old.length * 2 + 1);
    this.size = 0;
    this.longest = 0;
    for (let i = 0; i < old.length; i++) {
      let e = old[i];
      while (e !== null) { this.put(e.key, e.val); e = e.next; }
    }
  }
}

function run(): void {
  let lt = new LinearTable(4);
  let ref = new Map<u64, i32>();
  let x: u64 = 0x123456789;
  for (let phase = 0; phase < 6; phase++) {
    for (let i = 0; i < 400; i++) {
      x = x * 6364136223846793005 + 1442695040888963407;
      let k = (x >> 40) & 0x3ff; // many repeats
      let op = <i32>(x >> 20) & 3;
      if (op == 3 && phase > 1) { let a = lt.delete(k); let b = ref.delete(k); if (a != b) print("DELETE MISMATCH " + k.toString()); }
      else { lt.set(k, i + phase * 1000); ref.set(k, i + phase * 1000); }
    }
    let mism = 0;
    for (let k: u64 = 0; k < 1024; k++) {
      let want = ref.has(k) ? ref.get(k) : -1;
      if (lt.get(k, -1) != want) mism++;
    }
    print("linear phase " + phase.toString() + " size=" + lt.size.toString() + " ref=" + ref.size.toString() + " cap=" + (lt.mask + 1).toString() +
      " mismatches=" + mism.toString() + " probes=" + lt.probes.toString());
  }
  // Robin Hood with a fixed capacity at high load.
  let rh = new RobinHood(10);
  let refl = new Map<u64, i64>();
  let y: u64 = 7;
  for (let i = 0; i < 900; i++) {
    y = mix64(y + <u64>i);
    let k = y % 1200;
    rh.insert(k, <i64>i);
    refl.set(k, refl.has(k) ? refl.get(k) + <i64>i : <i64>i);
    if (i % 150 == 149) {
      let mism = 0;
      let ks = refl.keys();
      for (let j = 0; j < ks.length; j++) if (rh.find(ks[j]) != refl.get(ks[j])) mism++;
      let absent = 0;
      for (let q: u64 = 1200; q < 1300; q++) if (rh.find(q) != -1) absent++;
      print("robin " + i.toString() + " size=" + rh.size.toString() + " maxDist=" + rh.maxDist.toString() + " mismatches=" + mism.toString() + " false+=" + absent.toString());
    }
  }
  let hist = new Int32Array(16);
  for (let i = 0; i < (1 << rh.capLog); i++) if (rh.dist[i] >= 0) hist[min(<i32>rh.dist[i], 15)]++;
  let hs = "";
  for (let i = 0; i < 16; i++) hs += hist[i].toString() + " ";
  print("robin dist histogram " + hs);
  // Chained string table.
  let ch = new Chained(3);
  let refs = new Map<string, i32>();
  const syll = ["ka", "ri", "to", "mo", "su", "ne", "ha", "yu", "ko", "ra"];
  for (let i = 0; i < 1500; i++) {
    let k = syll[i % 10] + syll[(i * 7) % 10] + syll[(i / 10) % 10] + (i % 3 == 0 ? syll[(i / 100) % 10] : "");
    ch.put(k, i);
    refs.set(k, i);
    if (i % 300 == 299) {
      let ks = refs.keys();
      let mism = 0;
      for (let j = 0; j < ks.length; j++) if (ch.get(ks[j]) != refs.get(ks[j])) mism++;
      print("chained " + i.toString() + " size=" + ch.size.toString() + " buckets=" + ch.buckets.length.toString() +
        " longest=" + ch.longest.toString() + " mismatches=" + mism.toString() + " miss=" + ch.get("nothere").toString());
    }
  }
  print("hashes " + fnv1a("").toString() + " " + fnv1a("a").toString() + " " + fnv1a("hello world").toString() + " " +
    mix64(0).toString() + " " + mix64(1).toString() + " " + mix64(0xffffffffffffffff).toString());
}
run();
