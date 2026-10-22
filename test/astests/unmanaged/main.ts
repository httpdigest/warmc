// @unmanaged classes: header-less structs in linear memory that the
// collector never sees, allocated by hand (heap.alloc and a bump allocator
// over memory.data) and addressed with changetype<T>(address).  Field layout
// from offsetof<T>("field") and sizeof/alignof (AssemblyScript aligns each
// field to its own size); an array of structs walked by pointer stride; a
// doubly linked free list threaded through the structs' own fields; a binary
// search tree whose child links are raw pointers; struct copy with
// memory.copy; and null as address 0.  Field accesses are loads and stores
// at constant offsets from a computed base, the addressing mode warmc folds
// into its loads, and the misaligned f64 field in a packed record tests that
// folding with an unaligned base.
import { print, hex32 } from "../wasi";

@unmanaged
class Vec3 {
  x: f64;
  y: f64;
  z: f64;
}

@unmanaged
class Rec {
  tag: u8;
  flags: u16;
  id: i32;
  weight: f64;
  next: Rec | null;
  prev: Rec | null;
}

@unmanaged
class BNode {
  key: i32;
  val: i64;
  left: BNode | null;
  right: BNode | null;
}

// A bump allocator over a static arena.
const ARENA_SIZE: usize = 16384;
const ARENA = memory.data(<i32>ARENA_SIZE, 16);
let bumpTop: usize = 0;
function bump(size: usize, align: usize): usize {
  bumpTop = (bumpTop + align - 1) & ~(align - 1);
  let p = ARENA + bumpTop;
  bumpTop += size;
  return p;
}

function insert(root: BNode | null, key: i32, val: i64): BNode {
  if (root === null) {
    let n = changetype<BNode>(bump(offsetof<BNode>(), 8));
    n.key = key; n.val = val; n.left = null; n.right = null;
    return n;
  }
  if (key < root.key) root.left = insert(root.left, key, val);
  else if (key > root.key) root.right = insert(root.right, key, val);
  else root.val += val;
  return root;
}

function inorder(n: BNode | null, out: string[]): void {
  if (n === null) return;
  inorder(n.left, out);
  out.push(n.key.toString() + ":" + n.val.toString());
  inorder(n.right, out);
}

function height(n: BNode | null): i32 {
  return n === null ? 0 : 1 + max(height(n.left), height(n.right));
}

function run(): void {
  print("layout Vec3 size=" + offsetof<Vec3>().toString() + " y@" + offsetof<Vec3>("y").toString() + " z@" + offsetof<Vec3>("z").toString());
  print("layout Rec size=" + offsetof<Rec>().toString() + " flags@" + offsetof<Rec>("flags").toString() + " id@" + offsetof<Rec>("id").toString() +
    " weight@" + offsetof<Rec>("weight").toString() + " next@" + offsetof<Rec>("next").toString() + " prev@" + offsetof<Rec>("prev").toString());
  print("layout BNode size=" + offsetof<BNode>().toString() + " sizeof<usize>=" + sizeof<usize>().toString() +
    " alignof<f64>=" + alignof<f64>().toString() + " alignof<u16>=" + alignof<u16>().toString());

  // Array of structs by stride.
  const N = 20;
  const SZ = offsetof<Vec3>();
  let base = heap.alloc(<usize>N * SZ);
  for (let i = 0; i < N; i++) {
    let v = changetype<Vec3>(base + <usize>i * SZ);
    v.x = <f64>i; v.y = <f64>(i * i) * 0.5; v.z = -<f64>i * 0.25;
  }
  // in-place update: rotate each vector by 90 degrees about z and scale
  for (let i = 0; i < N; i++) {
    let v = changetype<Vec3>(base + <usize>i * SZ);
    let t = v.x; v.x = -v.y; v.y = t; v.z *= 2.0;
  }
  let line = "";
  let sx = 0.0, sy = 0.0, sz = 0.0;
  for (let p = base; p < base + <usize>N * SZ; p += SZ) {
    let v = changetype<Vec3>(p);
    sx += v.x; sy += v.y; sz += v.z;
    if (((p - base) / SZ) % 4 == 0) line += "(" + v.x.toString() + "," + v.y.toString() + "," + v.z.toString() + ")";
  }
  print("vec3 " + line + " sum=" + sx.toString() + "," + sy.toString() + "," + sz.toString());
  // Struct copy by memory.copy.
  let copy = changetype<Vec3>(heap.alloc(SZ));
  memory.copy(changetype<usize>(copy), base + 7 * SZ, SZ);
  copy.z = 99.0;
  print("copy " + copy.x.toString() + " " + copy.y.toString() + " " + copy.z.toString() +
    " orig.z=" + changetype<Vec3>(base + 7 * SZ).z.toString());
  heap.free(changetype<usize>(copy));
  heap.free(base);

  // A packed byte buffer with records at odd addresses, read through Rec.
  let raw = heap.alloc(512);
  memory.fill(raw, 0, 512);
  let recs: Rec[] = [];
  let rs = offsetof<Rec>();
  for (let i = 0; i < 8; i++) {
    let r = changetype<Rec>(raw + 1 + <usize>i * (rs + 3)); // deliberately misaligned
    r.tag = <u8>(0x41 + i); r.flags = <u16>(i * 0x1111); r.id = i * -1000; r.weight = <f64>i / 3.0;
    r.next = null; r.prev = null;
    recs.push(r);
  }
  // Doubly link them in a shuffled order.
  let order: i32[] = [3, 0, 6, 1, 7, 2, 5, 4];
  for (let i = 0; i < order.length; i++) {
    let r = recs[order[i]];
    r.next = i + 1 < order.length ? recs[order[i + 1]] : null;
    r.prev = i > 0 ? recs[order[i - 1]] : null;
  }
  let fwd = "", bwd = "";
  let cur: Rec | null = recs[order[0]];
  let last: Rec | null = null;
  while (cur !== null) { fwd += String.fromCharCode(cur.tag) + cur.id.toString() + " "; last = cur; cur = cur.next; }
  cur = last;
  while (cur !== null) { bwd += String.fromCharCode(cur.tag) + hex32(cur.flags) + " "; cur = cur.prev; }
  print("fwd " + fwd);
  print("bwd " + bwd);
  // Unlink every other one and walk again.
  cur = recs[order[0]];
  while (cur !== null && cur.next !== null) {
    let skip = cur.next!;
    cur.next = skip.next;
    if (skip.next !== null) skip.next!.prev = cur;
    cur = cur.next;
  }
  fwd = "";
  let wsum = 0.0;
  cur = recs[order[0]];
  while (cur !== null) { fwd += String.fromCharCode(cur.tag) + " "; wsum += cur.weight; cur = cur.next; }
  print("unlinked " + fwd + "w=" + wsum.toString() + " null=" + changetype<usize>(recs[order[7]].next).toString());
  heap.free(raw);

  // A BST of raw nodes in the bump arena.
  let root: BNode | null = null;
  let x: u32 = 2463534242;
  for (let i = 0; i < 200; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    root = insert(root, <i32>(x % 97) - 48, <i64>i);
  }
  let out: string[] = [];
  inorder(root, out);
  print("bst n=" + out.length.toString() + " h=" + height(root).toString() + " used=" + bumpTop.toString());
  for (let i = 0; i < out.length; i += 12) print("  " + out.slice(i, i + 12).join(" "));
}
run();
