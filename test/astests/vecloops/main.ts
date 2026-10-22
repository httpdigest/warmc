// Scalar loops of the shapes an auto-vectoriser takes on, written plainly in
// AssemblyScript so that the vector code, if any, is warmc's own: element-wise
// maps over every typed-array element type (with the narrow types' wrap and
// sign extension on every element), clamps and abs written as selects,
// min/max and sum/xor reductions in each width (integer ones may be
// reassociated, f32/f64 ones may not -- the sum must be the sequential one),
// conversions between element widths in one loop (u8 -> f32, i16 -> i32,
// f64 -> i32 with saturation), trip counts of every residue modulo the vector
// width including 0 and 1, in-place updates through overlapping subarrays
// (a[i] = a[i-1] + ... is a true dependence and must stay serial; a[i] =
// a[i+1] is not), reversed and strided loops, and an early-exit search.
// Each loop's result is printed as a checksum.
import { print, bits, bits32, canon32, canon64 } from "../wasi";

let seed: u32 = 0x12345678;
function rnd(): u32 { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }

function sumI8(a: Int8Array, n: i32): i32 { let s = 0; for (let i = 0; i < n; i++) s += a[i]; return s; }
function sumU8(a: Uint8Array, n: i32): u32 { let s: u32 = 0; for (let i = 0; i < n; i++) s += a[i]; return s; }
function sumI16(a: Int16Array, n: i32): i32 { let s = 0; for (let i = 0; i < n; i++) s += a[i]; return s; }
function sumI64(a: Int64Array, n: i32): i64 { let s: i64 = 0; for (let i = 0; i < n; i++) s += a[i]; return s; }
function sumF32(a: Float32Array, n: i32): f32 { let s: f32 = 0; for (let i = 0; i < n; i++) s += a[i]; return s; }
function sumF64(a: Float64Array, n: i32): f64 { let s: f64 = 0; for (let i = 0; i < n; i++) s += a[i]; return s; }
function xorI32(a: Int32Array, n: i32): i32 { let s = 0; for (let i = 0; i < n; i++) s ^= a[i] * (i + 1); return s; }
function minmaxI16(a: Int16Array, n: i32): string {
  let lo: i16 = 32767, hi: i16 = -32768;
  for (let i = 0; i < n; i++) { let v = a[i]; lo = v < lo ? v : lo; hi = v > hi ? v : hi; }
  return lo.toString() + "/" + hi.toString();
}
function minmaxU8(a: Uint8Array, n: i32): string {
  let lo: u8 = 255, hi: u8 = 0;
  for (let i = 0; i < n; i++) { lo = min(lo, a[i]); hi = max(hi, a[i]); }
  return lo.toString() + "/" + hi.toString();
}
function maxF32(a: Float32Array, n: i32): f32 { let m: f32 = -Infinity; for (let i = 0; i < n; i++) m = max(m, a[i]); return m; }

function ck8(a: Uint8Array, n: i32): u32 { let h: u32 = 0; for (let i = 0; i < n; i++) h = h * 31 + a[i]; return h; }
function cki16(a: Int16Array, n: i32): u32 { let h: u32 = 0; for (let i = 0; i < n; i++) h = h * 31 + <u32><u16>a[i]; return h; }
function cki32(a: Int32Array, n: i32): u32 { let h: u32 = 0; for (let i = 0; i < n; i++) h = h * 31 + <u32>a[i]; return h; }
function ckf32(a: Float32Array, n: i32): u32 { let h: u32 = 0; for (let i = 0; i < n; i++) h = h * 31 + canon32(a[i]); return h; }

const N = 260;

function run(): void {
  let i8 = new Int8Array(N), u8 = new Uint8Array(N), i16 = new Int16Array(N), u16 = new Uint16Array(N);
  let i32 = new Int32Array(N), i64 = new Int64Array(N), f32 = new Float32Array(N), f64 = new Float64Array(N);
  for (let i = 0; i < N; i++) {
    let r = rnd();
    i8[i] = <i8>r; u8[i] = <u8>(r >> 8); i16[i] = <i16>(r >> 3); u16[i] = <u16>(r >> 11);
    i32[i] = <i32>r; i64[i] = <i64>r * <i64>(<i32>(r >> 16)); f32[i] = <f32>(<i32>r) * <f32>1e-7; f64[i] = <f64>(<i32>(r ^ 0x5555)) * 1.25e-6;
  }
  // Reductions at every trip count residue.
  const ns: i32[] = [0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 255, 260];
  for (let k = 0; k < ns.length; k++) {
    let n = ns[k];
    print("reduce n=" + n.toString() + " i8=" + sumI8(i8, n).toString() + " u8=" + sumU8(u8, n).toString() +
      " i16=" + sumI16(i16, n).toString() + " i64=" + sumI64(i64, n).toString() + " xor=" + xorI32(i32, n).toString() +
      " f32=" + bits32(sumF32(f32, n)) + " f64=" + bits(sumF64(f64, n)) + " mm16=" + minmaxI16(i16, n) +
      " mm8=" + minmaxU8(u8, n) + " maxf=" + bits32(maxF32(f32, n)));
  }
  // Element-wise maps, narrow types wrapping.
  for (let k = 0; k < ns.length; k += 3) {
    let n = ns[k];
    let o8 = new Uint8Array(N), o16 = new Int16Array(N), o32 = new Int32Array(N), oF = new Float32Array(N);
    for (let i = 0; i < n; i++) o8[i] = u8[i] * 3 + <u8>i;                         // wraps at 8 bits
    for (let i = 0; i < n; i++) o16[i] = <i16>(i16[i] >> 2) - <i16>i8[i];           // arithmetic shift, sign-extended input
    for (let i = 0; i < n; i++) o32[i] = i32[i] < 0 ? -i32[i] : i32[i];             // abs as select
    for (let i = 0; i < n; i++) oF[i] = <f32>u8[i] * <f32>0.5 + <f32>i16[i];          // widen and convert
    print("map n=" + n.toString() + " u8=" + ck8(o8, N).toString() + " i16=" + cki16(o16, N).toString() +
      " abs=" + cki32(o32, N).toString() + " f32=" + ckf32(oF, N).toString());
  }
  // Clamps and saturation, with an i32 intermediate and a narrow store.
  let clamped = new Uint8Array(N), sat16 = new Int16Array(N);
  for (let i = 0; i < N; i++) {
    let v = <i32>i16[i] >> 4;
    clamped[i] = <u8>(v < 0 ? 0 : v > 255 ? 255 : v);
  }
  for (let i = 0; i < N; i++) {
    let s = <i32>i16[i] + <i32>i16[N - 1 - i];
    sat16[i] = <i16>(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
  }
  print("clamp " + ck8(clamped, N).toString() + " sat16 " + cki16(sat16, N).toString() + " mm=" + minmaxU8(clamped, N) + " " + minmaxI16(sat16, N));
  // Width conversions in one loop.
  let fromU8 = new Float32Array(N), fromI16 = new Int32Array(N), fromF64 = new Int32Array(N), toI8 = new Int8Array(N);
  for (let i = 0; i < N; i++) fromU8[i] = <f32>u8[i];
  for (let i = 0; i < N; i++) fromI16[i] = <i32>i16[i] * 3;
  for (let i = 0; i < N; i++) fromF64[i] = <i32>(f64[i] * 1e6);   // saturating
  for (let i = 0; i < N; i++) toI8[i] = <i8>(i32[i] >> 24);
  let s8 = 0;
  for (let i = 0; i < N; i++) s8 += toI8[i];
  print("convert " + ckf32(fromU8, N).toString() + " " + cki32(fromI16, N).toString() + " " + cki32(fromF64, N).toString() + " " + s8.toString());
  // Dependences: a[i] depends on a[i-1] (serial), a[i] from a[i+1] (forward, vectorisable).
  let dep = new Int32Array(N);
  for (let i = 0; i < N; i++) dep[i] = i32[i] & 0xff;
  for (let i = 1; i < N; i++) dep[i] = dep[i - 1] * 3 + dep[i];
  let fwd = new Int32Array(N);
  for (let i = 0; i < N; i++) fwd[i] = i;
  for (let i = 0; i < N - 1; i++) fwd[i] = fwd[i + 1] * 2 - fwd[i];
  let dep2 = new Int32Array(N);
  for (let i = 0; i < N; i++) dep2[i] = i & 15;
  for (let i = 4; i < N; i++) dep2[i] += dep2[i - 4]; // distance-4 dependence, exactly one i32x4
  print("deps serial=" + cki32(dep, N).toString() + " fwd=" + cki32(fwd, N).toString() + " d4=" + cki32(dep2, N).toString() + " last=" + dep2[N - 1].toString());
  // Overlapping subarrays of one buffer.
  let buf = new Int32Array(N + 8);
  for (let i = 0; i < N + 8; i++) buf[i] = i * 7;
  let lo = buf.subarray(0, N), hi = buf.subarray(3, N + 3);
  for (let i = 0; i < N; i++) hi[i] = lo[i] + 1;  // writes ahead of reads: a carried dependence through memory
  print("overlap " + cki32(buf, N + 8).toString() + " " + buf[N + 2].toString());
  let buf2 = new Int32Array(N + 8);
  for (let i = 0; i < N + 8; i++) buf2[i] = i * 7;
  let lo2 = buf2.subarray(0, N), hi2 = buf2.subarray(3, N + 3);
  for (let i = 0; i < N; i++) lo2[i] = hi2[i] + 1; // reads ahead of writes: no carried dependence
  print("overlap2 " + cki32(buf2, N + 8).toString() + " " + buf2[N - 1].toString());
  // Reverse and strided loops.
  let rev = new Int16Array(N);
  for (let i = N - 1; i >= 0; i--) rev[N - 1 - i] = i16[i] ^ <i16>i;
  let strided: i64 = 0;
  for (let i = 0; i < N; i += 3) strided += i64[i] - <i64>i32[(i * 5) % N];
  let gathered: f64 = 0;
  for (let i = 0; i < N; i++) gathered += f64[(i * 37) % N] * <f64>i8[i];
  print("rev " + cki16(rev, N).toString() + " strided " + strided.toString() + " gather " + bits(gathered));
  // Early exit: first index where a running sum passes a threshold.
  for (let t = 0; t < 5; t++) {
    let limit = 1000 + t * 7000;
    let acc = 0, at = -1;
    for (let i = 0; i < N; i++) { acc += u8[i]; if (acc > limit) { at = i; break; } }
    let firstNeg = -1;
    for (let i = t * 30; i < N; i++) if (i16[i] < -30000 + t * 5000) { firstNeg = i; break; }
    print("exit t=" + t.toString() + " at=" + at.toString() + " acc=" + acc.toString() + " firstNeg=" + firstNeg.toString());
  }
  // u16 arithmetic: multiply-high and average with the wrap kept.
  let avg = new Uint16Array(N), mh = new Uint16Array(N);
  for (let i = 0; i < N; i++) avg[i] = <u16>((<u32>u16[i] + <u32>u16[N - 1 - i] + 1) >> 1);
  for (let i = 0; i < N; i++) mh[i] = <u16>((<u32>u16[i] * <u32>u16[(i + 1) % N]) >> 16);
  let sa: u32 = 0, sm: u32 = 0;
  for (let i = 0; i < N; i++) { sa = sa * 17 + avg[i]; sm = sm * 13 + mh[i]; }
  print("u16 avg " + sa.toString() + " mulhi " + sm.toString());
  // i64 lanes: shifts, compares and selects.
  let r64 = new Int64Array(N);
  for (let i = 0; i < N; i++) { let v = i64[i]; r64[i] = v > 0 ? v >> 3 : (v << 2) ^ -1; }
  let h64: u64 = 0;
  for (let i = 0; i < N; i++) h64 = h64 * 0x100000001b3 + <u64>r64[i];
  print("i64 select " + h64.toString());
  // f64 loops: a*b+c (no fusion allowed), sqrt and division.
  let fo = new Float64Array(N);
  for (let i = 0; i < N; i++) fo[i] = f64[i] * f64[(i + 1) % N] + f64[(i + 2) % N];
  let fs: u64 = 0;
  for (let i = 0; i < N; i++) fs = fs * 31 + canon64(fo[i]);
  for (let i = 0; i < N; i++) fo[i] = Math.sqrt(Math.abs(fo[i])) / (1.0 + <f64>i);
  let fs2: u64 = 0;
  for (let i = 0; i < N; i++) fs2 = fs2 * 31 + canon64(fo[i]);
  print("f64 fma-free " + fs.toString() + " sqrtdiv " + fs2.toString());
}
run();
