// u8, u16, i8 and i16 arithmetic, none of which wasm has: AssemblyScript keeps
// small integers in i32 locals and wraps them only where the wrap could be
// observed.  The CRC-16 half shows how lazily -- binaryen carries the u16 CRC
// unmasked through all 4096 x 8 steps of shift, xor and a select on bit 15,
// and applies the one `and 0xffff` after the loop.  The i8 half cannot do
// that: a signed compare and a saturating clamp need the true value, so it is
// i32.extend8_s and i32.extend16_s after nearly every operation, with selects
// for the clamps.
//
// These extends are cheap one at a time, but they sit on a serial dependency
// chain, and a consumer that tracks which bits are known can drop the ones that
// are redundant.  gobench/crc goes through Go's table-driven hash/crc32; this
// CRC is the bitwise kind.
import { print } from "../wasi";

// CRC-16/CCITT, bit at a time, entirely in u16 and u8.
function crc16(data: Uint8Array, n: i32, seed: u16): u16 {
  let crc: u16 = seed;
  for (let i = 0; i < n; i++) {
    crc ^= <u16>unchecked(data[i]) << 8;
    for (let b = 0; b < 8; b++) {
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
  }
  return crc;
}

// Signed bytes: a saturating add and a wrapping one side by side.
function mixI8(data: Uint8Array, n: i32): i32 {
  let sat: i8 = 0;
  let wrap: i8 = 0;
  let lo: i16 = 0;
  for (let i = 0; i < n; i++) {
    let v = <i8>unchecked(data[i]);
    let s = <i16>sat + <i16>v;
    sat = s > 127 ? 127 : s < -128 ? -128 : <i8>s;
    wrap += v;
    wrap = wrap * 3 >> 1;
    if (wrap < sat) lo -= <i16>wrap; else lo += <i16>(sat ^ wrap);
  }
  return <i32>sat * 65536 + <i32>wrap * 256 + <i32>lo;
}

const N = 4096;
let data = new Uint8Array(N);
let h: u8 = 7;
for (let i = 0; i < N; i++) {
  h = h * 31 + <u8>i;
  data[i] = h ^ <u8>(i >> 3);
}

let crc: u16 = 0xffff;
let total = 0;
for (let r = 0; r < 3000; r++) {
  data[r & (N - 1)] = <u8>r;
  crc = crc16(data, N, crc);
  total += mixI8(data, N) ^ r;
}
print("crc = " + crc.toString() + " total = " + total.toString());
