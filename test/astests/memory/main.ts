// Linear memory by hand: load<T>/store<T> of every type (i8/u8/i16/u16/
// i32/u32/i64/u64/f32/f64) with the immediate offset operand, at aligned and
// misaligned addresses; the narrow loads' sign and zero extension into i32
// and i64 (i64.load8_s etc. via load<i8> widened); the narrow stores'
// truncation; memory.fill and memory.copy (overlapping in both directions,
// zero length, unaligned ends); memory.compare; memory.data for static
// buffers; heap.alloc/realloc/free; memory.size and memory.grow; and
// changetype between a managed reference and its address, with
// unchecked() element reads through raw pointers.  Every byte region is
// dumped as hex after each step.
import { print, hex32, hex64 } from "../wasi";

const BUF: usize = memory.data(256);
const STATIC_BYTES = memory.data<u8>([1, 2, 3, 4, 5, 6, 7, 8, 0x80, 0xff, 0x7f, 0x00, 0xaa, 0x55, 0xde, 0xad]);

function dump(p: usize, n: i32): string {
  let s = "";
  for (let i = 0; i < n; i++) s += load<u8>(p + <usize>i).toString(16).padStart(2, "0");
  return s;
}

class Thing {
  a: i32 = 0x11111111;
  b: i64 = 0x2222222233333333;
  c: f64 = 1.5;
  d: u8 = 0x44;
}

function run(): void {
  memory.fill(BUF, 0, 256);
  // Stores at every width and offset; aligned and misaligned.
  store<u8>(BUF, 0xab);
  store<i16>(BUF, -2, 1);          // offset 1: misaligned i16
  store<i32>(BUF, 0x12345678, 3);  // offset 3: misaligned i32
  store<i64>(BUF, 0x0102030405060708, 7);
  store<f32>(BUF, 1.5, 15);
  store<f64>(BUF, -0.1, 19);
  store<u16>(BUF + 27, 0xbeef);
  store<i8>(BUF + 29, -1);
  store<i32>(BUF + 32, -0x76543211);
  store<u64>(BUF + 40, 0xfedcba9876543210);
  print("stores " + dump(BUF, 48));
  // Narrow stores truncate the value.
  store<u8>(BUF + 48, 0x1234 as u8);
  store<i8>(BUF + 49, <i8>0x180);
  store<u16>(BUF + 50, <u16>0x12345);
  store<i32>(BUF + 52, <i32>0x123456789);
  print("truncating " + dump(BUF + 48, 8));
  // Loads with extension.
  for (let off = 0; off < 48; off += 5) {
    let p = BUF + <usize>off;
    print("load@" + off.toString() + " i8=" + load<i8>(p).toString() + " u8=" + load<u8>(p).toString() +
      " i16=" + load<i16>(p).toString() + " u16=" + load<u16>(p).toString() + " i32=" + load<i32>(p).toString() +
      " u32=" + load<u32>(p).toString() + " i64=" + load<i64>(p).toString() + " u64=" + hex64(load<u64>(p)) +
      " f32=" + load<f32>(p).toString() + " f64bits=" + hex64(load<u64>(p)) +
      " i8>i64=" + (<i64>load<i8>(p)).toString() + " u16>i64=" + (<i64>load<u16>(p)).toString() +
      " i32>u64=" + hex64(<u64>load<i32>(p)) + " u32>i64=" + (<i64>load<u32>(p)).toString());
  }
  // Immediate offsets reach the same bytes as computed addresses.
  let ok = 0;
  for (let i = 0; i < 32; i++) {
    let p = BUF + <usize>i;
    if (load<u32>(p + 3) == load<u32>(p, 3)) ok++;
    if (load<u16>(p + 7) == load<u16>(p, 7)) ok++;
    if (load<u64>(p + 9) == load<u64>(p, 9)) ok++;
    if (load<i8>(p + 1) == load<i8>(p, 1)) ok++;
  }
  print("offset-forms agree=" + ok.toString() + " imm=" + hex32(load<u32>(BUF, 3)) + " " + hex32(load<u32>(BUF + 1, 2)) +
    " " + hex64(load<u64>(BUF, 7)) + " " + load<f64>(BUF, 19).toString());
  // memory.fill: lengths 0..33 at odd starts.
  for (let n = 0; n < 34; n += 3) {
    memory.fill(BUF + 64, 0xee, 40);
    memory.fill(BUF + 65 + <usize>(n & 3), <u8>(n + 1), <usize>n);
    print("fill n=" + n.toString() + " " + dump(BUF + 64, 40));
  }
  // memory.copy: forward and backward overlaps of every small shift.
  for (let shift = -9; shift <= 9; shift += 2) {
    for (let i = 0; i < 48; i++) store<u8>(BUF + 128 + <usize>i, <u8>(i + 0x30));
    memory.copy(BUF + <usize>(140 + shift), BUF + 140, 21);
    print("copy shift=" + shift.toString() + " " + dump(BUF + 128, 48));
  }
  memory.copy(BUF + 130, BUF + 130, 0);
  memory.copy(BUF, STATIC_BYTES, 16);
  print("static " + dump(BUF, 16) + " cmp=" + memory.compare(BUF, STATIC_BYTES, 16).toString() +
    " " + (memory.compare(BUF, STATIC_BYTES + 1, 8) < 0 ? "lt" : "ge"));
  store<u8>(BUF + 10, 0x80);
  print("compare after edit=" + (memory.compare(BUF, STATIC_BYTES, 16) > 0 ? "gt" : memory.compare(BUF, STATIC_BYTES, 16) < 0 ? "lt" : "eq"));
  // heap.alloc / realloc / free: contents survive realloc.
  let p = heap.alloc(24);
  for (let i = 0; i < 24; i++) store<u8>(p + <usize>i, <u8>(i * 11));
  let q = heap.realloc(p, 200);
  for (let i = 24; i < 200; i++) store<u8>(q + <usize>i, <u8>i);
  let s: u32 = 0;
  for (let i = 0; i < 200; i++) s = s * 31 + load<u8>(q + <usize>i);
  print("heap realloc sum=" + s.toString() + " head=" + dump(q, 24) + " aligned=" + ((q & 15) == 0 ? "1" : "0"));
  heap.free(q);
  let blocks: usize[] = [];
  for (let i = 0; i < 50; i++) { let b = heap.alloc(<usize>(i * 37 % 300 + 1)); memory.fill(b, <u8>i, <usize>(i * 37 % 300 + 1)); blocks.push(b); }
  let chk: u32 = 0;
  for (let i = 0; i < 50; i++) { chk = chk * 7 + load<u8>(blocks[i]) + load<u8>(blocks[i] + <usize>(i * 37 % 300)); if (i % 2 == 0) heap.free(blocks[i]); }
  print("heap blocks chk=" + chk.toString());
  // memory.size / grow.
  let before = memory.size();
  let r = memory.grow(2);
  let after = memory.size();
  let top = <usize>after << 16;
  store<u64>(top - 8, 0x0123456789abcdef);
  print("grow ret==before " + (r == before ? "1" : "0") + " delta=" + (after - before).toString() + " top=" + hex64(load<u64>(top - 8)) +
    " fresh=" + load<u32>(top - 65536).toString());
  // changetype: an object's fields read through its address.
  let t = new Thing();
  let addr = changetype<usize>(t);
  print("thing a=" + hex32(load<u32>(addr, offsetof<Thing>("a"))) + " b=" + hex64(load<u64>(addr, offsetof<Thing>("b"))) +
    " c=" + load<f64>(addr, offsetof<Thing>("c")).toString() + " d=" + load<u8>(addr, offsetof<Thing>("d")).toString() +
    " size=" + offsetof<Thing>().toString());
  store<i32>(addr, -5, offsetof<Thing>("a"));
  store<f64>(addr, 2.25, offsetof<Thing>("c"));
  let t2 = changetype<Thing>(addr);
  print("thing via ref a=" + t2.a.toString() + " c=" + t2.c.toString() + " same=" + (t2 === t ? "1" : "0"));
  // unchecked element reads through dataStart.
  let arr = new Int32Array(16);
  for (let i = 0; i < 16; i++) arr[i] = i * i * (i & 1 ? -1 : 1);
  let ds = arr.dataStart;
  let line = "";
  for (let i = 0; i < 16; i += 3) line += load<i32>(ds + (<usize>i << 2)).toString() + "/" + unchecked(arr[i]).toString() + " ";
  print("dataStart " + line);
  let sarr: StaticArray<u16> = [1, 2, 0xffff, 0x8000];
  let sp = changetype<usize>(sarr);
  print("staticarray raw " + dump(sp, 8) + " i16[2]=" + load<i16>(sp + 4).toString());
}
run();
