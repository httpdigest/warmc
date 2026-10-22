// Shifts at every width with counts past the width: wasm masks the count to
// the low 5 bits (i32) or 6 bits (i64), so `x << 33` is `x << 1` and `x >> -1`
// is `x >> 31`.  For i8/i16/u8/u16 AssemblyScript masks the count to the
// narrow width itself (& 7 or & 15) before the i32 shift and wraps the
// result afterwards, so `<u8>x << 9` is `<u8>x << 1` and `<u16>x >>> 16` is
// the value unchanged -- an explicit and that binaryen must keep and warmc
// must not fold into the hardware's own & 31.  Arithmetic and logical right shifts
// of negative values, variable and constant counts, i64 shifted by a count
// computed in i32, and shift pairs that compilers fold into bitfield extracts
// (ubfx/sbfx) and inserts (bfi) on arm64.
import { print, hex32, hex64 } from "../wasi";

const v32: i32[] = [1, -1, 0x7fffffff, -0x80000000, 0x12345678, -0x12345678, 0xff, 0x8000, 0x55aa55aa];
const v64: i64[] = [1, -1, 0x7fffffffffffffff, -0x8000000000000000, 0x0123456789abcdef, -0x0123456789abcdef, 0xffffffff];
const counts: i32[] = [0, 1, 3, 7, 8, 15, 16, 17, 31, 32, 33, 47, 63, 64, 65, 127, 255, 256, -1, -31, -32, -33, 1000];

function sh32(x: i32, k: i32): string {
  return "sh32 " + hex32(x) + " " + k.toString() + " shl=" + hex32(x << k) +
    " sar=" + hex32(x >> k) + " shr=" + hex32(x >>> k) +
    " u8shl=" + (<u8>x << <u8>k).toString() + " i8sar=" + (<i8>x >> <i8>k).toString() +
    " u16shr=" + (<u16>x >>> <u16>k).toString() + " i16shl=" + (<i16>x << <i16>k).toString();
}

function sh64(x: i64, k: i32): string {
  return "sh64 " + hex64(x) + " " + k.toString() + " shl=" + hex64(x << <i64>k) +
    " sar=" + hex64(x >> <i64>k) + " shr=" + hex64(x >>> <i64>k) +
    " ushl=" + hex64(<u64>x << <u64>k) + " ushr=" + hex64(<u64>x >> <u64>k);
}

function shConst(x: i32, y: i64): string {
  return "shc " + hex32(x << 0) + " " + hex32(x << 1) + " " + hex32(x << 31) +
    " " + hex32(x >> 1) + " " + hex32(x >> 31) + " " + hex32(x >>> 1) + " " + hex32(x >>> 31) +
    " " + hex32(x << 33) + " " + hex32(x >> 35) + " " + hex32(x >>> 36) +
    " " + hex64(y << 1) + " " + hex64(y << 63) + " " + hex64(y >> 1) + " " + hex64(y >> 63) +
    " " + hex64(y >>> 1) + " " + hex64(y >>> 63) + " " + hex64(y << 65) + " " + hex64(y >>> 70);
}

// Bitfield extract and insert, the shapes that become ubfx/sbfx/bfi.
function fields(x: u32, y: u64): string {
  let f1 = (x >> 4) & 0xfff;               // ubfx #4, #12
  let f2 = (<i32>x << 8) >> 20;           // sbfx #12, #12
  let f3 = (x & ~(<u32>0xff << 8)) | ((<u32>0x5a) << 8); // bfi
  let g1 = (y >> 20) & 0xfffff;
  let g2 = (<i64>y << 3) >> 40;
  let g3 = (y & ~(<u64>0xffff << 24)) | (<u64>(x & 0xffff) << 24);
  return "field " + hex32(x) + " " + hex32(f1) + " " + hex32(<u32>f2) + " " + hex32(f3) +
    " " + hex64(g1) + " " + hex64(<u64>g2) + " " + hex64(g3);
}

function run(): void {
  for (let i = 0; i < v32.length; i++) {
    for (let j = 0; j < counts.length; j++) print(sh32(v32[i], counts[j]));
  }
  for (let i = 0; i < v64.length; i++) {
    for (let j = 0; j < counts.length; j++) print(sh64(v64[i], counts[j]));
  }
  for (let i = 0; i < v32.length; i++) print(shConst(v32[i], v64[i % v64.length]));
  let x: u32 = 0x9e3779b9;
  let y: u64 = 0xbf58476d1ce4e5b9;
  for (let k = 0; k < 24; k++) {
    print(fields(x, y));
    x = x * 0x2c1b3c6d + 0x297a2d39;
    y = y * 0x94d049bb133111eb + <u64>k;
  }
  // A shift count computed at i32 then widened, and a 128-bit shift by two
  // 64-bit halves for every count.
  let hi: u64 = 0x0123456789abcdef, lo: u64 = 0xfedcba9876543210;
  for (let s = 0; s < 128; s += 5) {
    let rh: u64, rl: u64;
    if (s == 0) { rh = hi; rl = lo; }
    else if (s < 64) { rh = (hi << <u64>s) | (lo >> <u64>(64 - s)); rl = lo << <u64>s; }
    else { rh = lo << <u64>(s - 64); rl = 0; }
    print("shl128 " + s.toString() + " " + hex64(rh) + hex64(rl));
  }
}
run();
