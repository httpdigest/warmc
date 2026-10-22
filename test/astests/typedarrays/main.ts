// Typed arrays and DataView over shared ArrayBuffers.  Int8/Uint8/
// Uint8Clamped/Int16/Uint16/Int32/Uint32/Int64/Uint64/Float32/Float64Array,
// each stored to with out-of-range values (wrapping, or clamping to 0..255
// for Uint8ClampedArray) and read back with
// the element type's extension.  subarray shares the buffer, so a write
// through the view shows in the parent and the reverse; set copies with an
// offset, including from an overlapping view of the same buffer; fill with
// negative ranges; slice copies; indexOf/includes/reverse/map/reduce; and two
// views of different element types over one buffer, so a Float64Array's
// bytes are read as Uint8 and a DataView reads them at every alignment in
// both byte orders.
import { print, bits, rawbits } from "../wasi";

function ints(v: Int32Array): string {
  let s = "";
  for (let i = 0; i < v.length; i++) s += (i > 0 ? "," : "") + v[i].toString();
  return s;
}

function run(): void {
  const raw: i32[] = [0, 1, -1, 127, 128, 255, 256, -129, 32767, 32768, 65535, -65536, 0x7fffffff, -0x80000000, 300, -300];
  let n = raw.length;
  let i8 = new Int8Array(n), u8 = new Uint8Array(n), c8 = new Uint8ClampedArray(n);
  let i16 = new Int16Array(n), u16 = new Uint16Array(n), i32 = new Int32Array(n), u32 = new Uint32Array(n);
  let i64 = new Int64Array(n), u64 = new Uint64Array(n), f32 = new Float32Array(n), f64 = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    let v = raw[i];
    i8[i] = <i8>v; u8[i] = <u8>v; c8[i] = v; // native<u8> is i32: the clamp is the array's
    i16[i] = <i16>v; u16[i] = <u16>v; i32[i] = v; u32[i] = <u32>v;
    i64[i] = <i64>v * 3; u64[i] = <u64>v * 3; f32[i] = <f32>v / 3; f64[i] = <f64>v / 3;
  }
  for (let i = 0; i < n; i++) {
    print("elt " + raw[i].toString() + " i8=" + i8[i].toString() + " u8=" + u8[i].toString() +
      " c8=" + c8[i].toString() + " i16=" + i16[i].toString() + " u16=" + u16[i].toString() +
      " i32=" + i32[i].toString() + " u32=" + u32[i].toString() + " i64=" + i64[i].toString() +
      " u64=" + u64[i].toString() + " f32=" + f32[i].toString() + " f64=" + bits(f64[i]));
  }
  // The spec's float clamp (round half to even), written out, since
  // AssemblyScript's clamped array takes integers.
  let cl = new Uint8ClampedArray(10);
  const fl: f64[] = [-5.0, 0.5, 1.5, 2.5, 3.49, 254.5, 255.5, 300.0, NaN, 127.5];
  let cls = "";
  for (let i = 0; i < 10; i++) {
    let v = fl[i];
    // emulate the spec clamp in AS code, since a cast does not clamp
    let r: u8 = isNaN(v) ? 0 : v <= 0 ? 0 : v >= 255 ? 255 : <u8>nearest(v);
    cl[i] = r;
    cls += cl[i].toString() + " ";
  }
  print("clamped " + cls);

  // subarray shares memory in both directions.
  let base = new Int32Array(20);
  for (let i = 0; i < 20; i++) base[i] = i * 10;
  let sub = base.subarray(5, 15);
  let sub2 = sub.subarray(-4, -1);
  print("sub len=" + sub.length.toString() + " off=" + sub.byteOffset.toString() + " " + ints(sub) +
    " sub2 len=" + sub2.length.toString() + " off=" + sub2.byteOffset.toString() + " " + ints(sub2));
  sub[0] = -1; sub2[2] = -2; base[6] = -3;
  print("shared base=" + ints(base) + " sub=" + ints(sub) + " sub2=" + ints(sub2));
  // set: plain, with offset, and from an overlapping view of the same buffer.
  let tgt = new Int32Array(12);
  let src = new Int32Array(4);
  for (let i = 0; i < 4; i++) src[i] = 100 + i;
  tgt.set(src); tgt.set(src, 6);
  print("set " + ints(tgt));
  let ov = new Int32Array(10);
  for (let i = 0; i < 10; i++) ov[i] = i;
  ov.set(ov.subarray(0, 6), 3);
  print("set overlap fwd " + ints(ov));
  for (let i = 0; i < 10; i++) ov[i] = i;
  ov.set(ov.subarray(4, 10), 1);
  print("set overlap back " + ints(ov));
  // set across element types converts.
  let fsrc = new Float64Array(5);
  for (let i = 0; i < 5; i++) fsrc[i] = <f64>i * 1.75 - 3.0;
  let isrc = new Int16Array(5);
  for (let i = 0; i < 5; i++) isrc[i] = <i16>(i * 20000);
  let dst32 = new Int32Array(10);
  dst32.set(isrc, 0); dst32.set(fsrc, 5);
  print("set convert " + ints(dst32));
  // fill / slice / reverse / indexOf / includes / map / reduce
  let t = new Int32Array(16);
  t.fill(7); t.fill(-1, 3, 6); t.fill(9, -4); t.fill(5, -6, -5);
  print("fill " + ints(t));
  let sl = t.slice(2, -3);
  sl[0] = 1234;
  print("slice " + ints(sl) + " orig2=" + t[2].toString());
  t.reverse();
  print("reverse " + ints(t) + " idx9=" + t.indexOf(9).toString() + " lastIdx-1=" + t.lastIndexOf(-1).toString() +
    " inc5=" + (t.includes(5) ? "1" : "0") + " inc6=" + (t.includes(6) ? "1" : "0"));
  let mapped = t.map((x: i32, i: i32, a: Int32Array): i32 => x * i);
  print("map " + ints(mapped) + " reduce=" + mapped.reduce<i64>((acc: i64, x: i32, i: i32, a: Int32Array): i64 => acc * 3 + <i64>x, 0).toString());

  // Two views of one buffer: f64 bytes as u8, and DataView at odd offsets.
  let buf = new ArrayBuffer(48);
  let fv = Float64Array.wrap(buf);
  let bv = Uint8Array.wrap(buf);
  let wv = Uint16Array.wrap(buf, 8, 8);
  fv[0] = Math.PI; fv[1] = -0.0; fv[2] = 1e-310; fv[3] = 1.0 / 3.0;
  let bs = "";
  for (let i = 0; i < 32; i++) bs += bv[i].toString(16).padStart(2, "0");
  print("bytes " + bs);
  let ws = "";
  for (let i = 0; i < wv.length; i++) ws += wv[i].toString() + " ";
  print("u16 view " + ws);
  let dv = new DataView(buf);
  for (let off = 0; off < 9; off++) {
    print("dv " + off.toString() + " i8=" + dv.getInt8(off).toString() + " u16le=" + dv.getUint16(off, true).toString() +
      " u16be=" + dv.getUint16(off, false).toString() + " i32le=" + dv.getInt32(off, true).toString() +
      " i32be=" + dv.getInt32(off, false).toString() + " i64le=" + dv.getInt64(off, true).toString() +
      " u64be=" + dv.getUint64(off, false).toString() + " f32le=" + dv.getFloat32(off, true).toString() +
      " f64be=" + rawbits(dv.getFloat64(off, false)));  // a load: bits fixed, NaN or not
  }
  for (let off = 33; off < 40; off++) {
    dv.setUint32(off, 0xdeadbeef + <u32>off, (off & 1) == 0);
  }
  dv.setFloat64(40, -2.5, false);
  dv.setInt16(37, -2, true);
  bs = "";
  for (let i = 32; i < 48; i++) bs += bv[i].toString(16).padStart(2, "0");
  print("dv writes " + bs + " back=" + dv.getUint32(34, true).toString() + " " + rawbits(dv.getFloat64(40, false)));
}
run();
