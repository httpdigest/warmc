// Map and Set, AssemblyScript's ordered hash tables (Tyler Close's
// deterministic table: a bucket array of chains plus an entries array in
// insertion order, rehashed when it fills or when enough entries are
// deleted).  Keys of i32, i64, f64, u8 and string; values of numbers, strings
// and objects; set to overwrite (order unchanged), delete then re-add (moves
// to the end), has/get/size after every phase, growth through several
// rehashes and shrinking after mass deletion, keys()/values() in insertion
// order, clear, and f64 keys (compared with ==, so a NaN key never finds
// itself and each set(NaN) adds an entry, while -0 finds 0).  Hashing is
// xxhash-style 32-bit multiply-rotate on the key bytes, so a wrong multiply
// or rotate puts entries in the wrong bucket and a lookup misses.
import { print } from "../wasi";

class Obj { constructor(public n: i32, public s: string) {} }

function mapLine(m: Map<i32, i32>): string {
  let ks = m.keys(), vs = m.values();
  let s = "size=" + m.size.toString() + " [";
  for (let i = 0; i < ks.length; i++) s += (i > 0 ? " " : "") + ks[i].toString() + ":" + vs[i].toString();
  return s + "]";
}

function run(): void {
  let m = new Map<i32, i32>();
  for (let i = 0; i < 10; i++) m.set(i * 7 % 10, i * i);
  print("m1 " + mapLine(m));
  m.set(3, -3); m.set(0, -100);
  print("m2 overwrite " + mapLine(m));
  m.delete(3); m.delete(42); m.set(3, 333);
  print("m3 delete/readd " + mapLine(m) + " has3=" + (m.has(3) ? "1" : "0") + " has42=" + (m.has(42) ? "1" : "0"));
  // Growth through rehashes, with lookups after each doubling.
  let big = new Map<i32, i32>();
  for (let i = 0; i < 5000; i++) {
    big.set(i * 2654435761, i);
    if ((i & (i - 1)) == 0 && i > 0) {
      let found = 0, missing = 0;
      for (let j = 0; j <= i; j++) { if (big.has(j * 2654435761) && big.get(j * 2654435761) == j) found++; }
      for (let j = 0; j < 50; j++) { if (big.has(j * 2654435761 + 1)) missing++; }
      print("grow " + i.toString() + " size=" + big.size.toString() + " found=" + found.toString() + " false+=" + missing.toString());
    }
  }
  // Mass deletion, then iteration order of the survivors.
  for (let i = 0; i < 5000; i++) if (i % 97 != 0) big.delete(i * 2654435761);
  let ks = big.keys(), vs = big.values();
  let line = "";
  for (let i = 0; i < ks.length; i++) line += vs[i].toString() + " ";
  print("after delete size=" + big.size.toString() + " values=" + line);
  for (let i = 0; i < 3000; i += 7) big.set(i, -i);
  print("refill size=" + big.size.toString() + " get(21)=" + big.get(21).toString() + " get(97*2654435761)=" + big.get(97 * 2654435761).toString());
  big.clear();
  print("clear size=" + big.size.toString() + " has0=" + (big.has(0) ? "1" : "0"));

  // i64 and f64 keys.
  let m64 = new Map<i64, string>();
  for (let i = 0; i < 20; i++) m64.set((<i64>i << 40) ^ <i64>(i * 3), "v" + i.toString());
  let l64 = "";
  let k64 = m64.keys();
  for (let i = 0; i < k64.length; i += 3) l64 += k64[i].toString() + "=" + m64.get(k64[i]) + " ";
  print("i64 keys " + l64 + "has=" + (m64.has(<i64>5 << 40 ^ 15) ? "1" : "0") + (m64.has(<i64>5 << 40) ? "1" : "0"));
  let mf = new Map<f64, i32>();
  mf.set(NaN, 1); mf.set(0.0, 2); mf.set(-0.0, 3); mf.set(1.5, 4); mf.set(Infinity, 5); mf.set(NaN, 6);
  let kf = mf.keys(), vf = mf.values();
  let lf = "";
  for (let i = 0; i < kf.length; i++) lf += kf[i].toString() + "=" + vf[i].toString() + " ";
  print("f64 keys size=" + mf.size.toString() + " " + lf + "hasNaN=" + (mf.has(NaN) ? "1" : "0") +
    " get-0=" + mf.get(-0.0).toString() + " get0=" + mf.get(0.0).toString() + " getInf=" + mf.get(Infinity).toString());
  let mu8 = new Map<u8, i32>();
  for (let i = 0; i < 600; i++) mu8.set(<u8>(i * 37), mu8.has(<u8>(i * 37)) ? mu8.get(<u8>(i * 37)) + 1 : 1);
  let ku = mu8.keys();
  let lu = "";
  for (let i = 0; i < ku.length; i += 17) lu += ku[i].toString() + ":" + mu8.get(ku[i]).toString() + " ";
  print("u8 keys size=" + mu8.size.toString() + " " + lu);

  // String keys and object values.
  let ms = new Map<string, Obj>();
  const names = ["alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta", "iota", "kappa"];
  for (let i = 0; i < names.length; i++) ms.set(names[i], new Obj(i, names[i].toUpperCase()));
  for (let i = 0; i < names.length; i += 3) ms.delete(names[i]);
  ms.set("alpha", new Obj(100, "again"));
  ms.get("beta").n += 1000;
  let kss = ms.keys();
  line = "";
  for (let i = 0; i < kss.length; i++) { let o = ms.get(kss[i]); line += kss[i] + "=" + o.n.toString() + "/" + o.s + " "; }
  print("str map " + line + "has(" + "be" + "ta)=" + (ms.has("be" + "ta") ? "1" : "0"));

  // Sets.
  let s = new Set<i32>();
  for (let i = 0; i < 40; i++) s.add((i * i) % 17);
  let sv = s.values();
  print("set i32 size=" + s.size.toString() + " " + sv.join(","));
  s.delete(0); s.delete(16); s.add(0); s.add(100);
  print("set i32 after " + s.values().join(",") + " has4=" + (s.has(4) ? "1" : "0") + " has5=" + (s.has(5) ? "1" : "0"));
  let ss = new Set<string>();
  let words = "to be or not to be that is the question whether tis nobler in the mind to suffer".split(" ");
  for (let i = 0; i < words.length; i++) ss.add(words[i]);
  print("set str size=" + ss.size.toString() + " " + ss.values().join(" "));
  // Set algebra by hand.
  let a = new Set<i32>(), b = new Set<i32>();
  for (let i = 0; i < 30; i++) { if (i % 2 == 0) a.add(i); if (i % 3 == 0) b.add(i); }
  let inter: i32[] = [], uni = new Set<i32>(), diff: i32[] = [];
  let av = a.values(), bv = b.values();
  for (let i = 0; i < av.length; i++) { uni.add(av[i]); if (b.has(av[i])) inter.push(av[i]); else diff.push(av[i]); }
  for (let i = 0; i < bv.length; i++) uni.add(bv[i]);
  print("inter " + inter.join(",") + " diff " + diff.join(",") + " union size=" + uni.size.toString());
  // A set of i64 with churn: add and delete in a sliding window.
  let win = new Set<i64>();
  let sum: i64 = 0;
  for (let i = 0; i < 2000; i++) {
    let k = <i64>i * 0x100000001;
    win.add(k);
    if (i >= 64) win.delete(<i64>(i - 64) * 0x100000001);
    if (i % 250 == 249) {
      let vals = win.values();
      sum = 0;
      for (let j = 0; j < vals.length; j++) sum += vals[j] >> 32;
      print("window " + i.toString() + " size=" + win.size.toString() + " first=" + vals[0].toString() + " sum=" + sum.toString());
    }
  }
}
run();
