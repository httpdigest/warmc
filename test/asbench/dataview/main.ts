// DataView: a buffer of 24-byte records written and parsed field by field, at
// widths from one byte to eight and in both byte orders, with some fields read
// back at a different offset or order than they were written.  binaryen
// inlines every get and set, so each is a range check (a negative offset, or
// offset + size past byteLength, traps), an unaligned load or store at
// dataStart + offset, and for big-endian a byte swap spelled out in wasm.  Wasm
// has no bswap instruction, so AssemblyScript writes a 32-bit swap as two masks,
// an i32.rotl, an i32.rotr and an or, a 64-bit one with i64.rotr, and swaps the
// floats as integers through reinterpret.
//
// This is the binary-format parsing shape.  It is worth measuring for the
// swap idiom, which an arm64 back end can turn into one rev if it recognises
// it, and for the range checks, which repeat the same comparison per field.
import { print, bits } from "../wasi";

const REC = 24;
const NREC = 512;
let buf = new ArrayBuffer(REC * NREC);
let dv = new DataView(buf);

let total: u32 = 0;
let ftotal: f64 = 0;
let x: u32 = 2463534242;
for (let r = 0; r < 32000; r++) {
  // Write the records: a mix of widths and of byte orders.
  for (let i = 0; i < NREC; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    let o = i * REC;
    dv.setUint16(o, <u16>x, false);
    dv.setUint32(o + 2, x ^ <u32>r, false);
    dv.setInt16(o + 6, <i16>(x >> 7), true);
    dv.setFloat32(o + 8, <f32>(x & 0xffff) * 0.5, false);
    dv.setUint32(o + 12, x * 31, true);
    dv.setFloat64(o + 16, <f64>(x >> 8) * 0.125, false);
  }
  // Parse them back, some at the width and order they were written in and
  // some deliberately not.
  for (let i = 0; i < NREC; i++) {
    let o = i * REC;
    let a = <u32>dv.getUint16(o, false);
    let b = dv.getUint32(o + 2, false);
    let c = <u32>dv.getInt16(o + 6, true);
    let d = dv.getFloat32(o + 8, false);
    let e = dv.getUint32(o + 12, true);
    let f = dv.getFloat64(o + 16, false);
    let g = dv.getUint32(o + 4, true);
    let h = <u32>dv.getInt8(o + 13);
    total = (total ^ a) * 16777619 + b + c + e + (g >> 3) + h;
    ftotal += <f64>d + f;
  }
}
print("total = " + total.toString() + " f = " + bits(ftotal));
