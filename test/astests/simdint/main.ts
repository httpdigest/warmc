// Integer SIMD at every lane shape -- i8x16, i16x8, i32x4, i64x2: add, sub,
// mul, neg, abs, saturating add/sub (signed and unsigned), min/max, avgr_u,
// popcnt, q15mulr_sat, dot, extmul, extadd_pairwise, narrow and extend,
// shifts by variable counts (wasm takes the count modulo the lane width, so
// a shift by 9 on i8 lanes is a shift by 1 -- arm64's vector shifts do not
// mask, so the count has to be masked or the result is 0), every comparison
// signed and unsigned, bitmask, all_true/any_true, and the bitwise ops
// (and/or/xor/andnot/not/bitselect).  Inputs are loaded from memory filled
// at run time, so binaryen cannot fold any of it, and every result is
// printed as four 32-bit words.  Lane values include each type's min, max,
// -1 and 0 so the saturating and narrowing edges are all reached.
import { print, hex32 } from "../wasi";

// A v128 as four 32-bit words in hex, lane 0 first.  Kept here rather than in
// wasi.ts: asc warns about any exported function with a v128 in its signature.
function hexv(v: v128): string {
  return hex32(i32x4.extract_lane(v, 0)) + "." + hex32(i32x4.extract_lane(v, 1)) + "." +
         hex32(i32x4.extract_lane(v, 2)) + "." + hex32(i32x4.extract_lane(v, 3));
}

const NV = 6;
const BUF = memory.data(16 * NV, 16);

function vec(i: i32): v128 { return v128.load(BUF + (<usize>i << 4)); }

function fill(): void {
  // vector 0 and 1: the edges at byte, half and word granularity
  const edge: u8[] = [0x00, 0x7f, 0x80, 0xff, 0x01, 0xfe, 0x81, 0x7e, 0x00, 0x80, 0xff, 0x7f, 0x40, 0xc0, 0x10, 0xef];
  for (let i = 0; i < 16; i++) store<u8>(BUF + <usize>i, edge[i]);
  store<u32>(BUF + 16, 0x80000000); store<u32>(BUF + 20, 0x7fffffff);
  store<u32>(BUF + 24, 0xffff8000); store<u32>(BUF + 28, 0x00017fff);
  // vectors 2..5: pseudo-random
  let x: u32 = 0x9e3779b9;
  for (let i = 32; i < 16 * NV; i += 4) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    store<u32>(BUF + <usize>i, x);
  }
}

function binops(ia: i32, ib: i32): void {
  let a = vec(ia), b = vec(ib);
  let tag = " " + ia.toString() + "," + ib.toString() + " ";
  print("i8.add" + tag + hexv(i8x16.add(a, b)) + " sub " + hexv(i8x16.sub(a, b)));
  print("i8.sat" + tag + hexv(i8x16.add_sat_s(a, b)) + " " + hexv(i8x16.add_sat_u(a, b)) + " " +
    hexv(i8x16.sub_sat_s(a, b)) + " " + hexv(i8x16.sub_sat_u(a, b)));
  print("i8.minmax" + tag + hexv(i8x16.min_s(a, b)) + " " + hexv(i8x16.min_u(a, b)) + " " +
    hexv(i8x16.max_s(a, b)) + " " + hexv(i8x16.max_u(a, b)) + " avgr " + hexv(i8x16.avgr_u(a, b)));
  print("i8.cmp" + tag + hexv(i8x16.eq(a, b)) + " " + hexv(i8x16.ne(a, b)) + " " + hexv(i8x16.lt_s(a, b)) + " " +
    hexv(i8x16.lt_u(a, b)) + " " + hexv(i8x16.le_s(a, b)) + " " + hexv(i8x16.gt_u(a, b)) + " " + hexv(i8x16.ge_s(a, b)) + " " + hexv(i8x16.ge_u(a, b)));
  print("i8.narrow" + tag + hexv(i8x16.narrow_i16x8_s(a, b)) + " " + hexv(i8x16.narrow_i16x8_u(a, b)));
  print("i16.arith" + tag + hexv(i16x8.add(a, b)) + " " + hexv(i16x8.sub(a, b)) + " " + hexv(i16x8.mul(a, b)) +
    " q15 " + hexv(i16x8.q15mulr_sat_s(a, b)));
  print("i16.sat" + tag + hexv(i16x8.add_sat_s(a, b)) + " " + hexv(i16x8.add_sat_u(a, b)) + " " +
    hexv(i16x8.sub_sat_s(a, b)) + " " + hexv(i16x8.sub_sat_u(a, b)) + " avgr " + hexv(i16x8.avgr_u(a, b)));
  print("i16.minmax" + tag + hexv(i16x8.min_s(a, b)) + " " + hexv(i16x8.min_u(a, b)) + " " +
    hexv(i16x8.max_s(a, b)) + " " + hexv(i16x8.max_u(a, b)));
  print("i16.cmp" + tag + hexv(i16x8.eq(a, b)) + " " + hexv(i16x8.lt_s(a, b)) + " " + hexv(i16x8.lt_u(a, b)) + " " +
    hexv(i16x8.gt_s(a, b)) + " " + hexv(i16x8.le_u(a, b)) + " " + hexv(i16x8.ge_s(a, b)));
  print("i16.narrow-ext" + tag + hexv(i16x8.narrow_i32x4_s(a, b)) + " " + hexv(i16x8.narrow_i32x4_u(a, b)) + " " +
    hexv(i16x8.extmul_low_i8x16_s(a, b)) + " " + hexv(i16x8.extmul_high_i8x16_u(a, b)));
  print("i32.arith" + tag + hexv(i32x4.add(a, b)) + " " + hexv(i32x4.sub(a, b)) + " " + hexv(i32x4.mul(a, b)) +
    " dot " + hexv(i32x4.dot_i16x8_s(a, b)));
  print("i32.minmax" + tag + hexv(i32x4.min_s(a, b)) + " " + hexv(i32x4.min_u(a, b)) + " " +
    hexv(i32x4.max_s(a, b)) + " " + hexv(i32x4.max_u(a, b)));
  print("i32.cmp" + tag + hexv(i32x4.eq(a, b)) + " " + hexv(i32x4.ne(a, b)) + " " + hexv(i32x4.lt_s(a, b)) + " " +
    hexv(i32x4.lt_u(a, b)) + " " + hexv(i32x4.le_s(a, b)) + " " + hexv(i32x4.gt_u(a, b)) + " " + hexv(i32x4.ge_u(a, b)));
  print("i32.extmul" + tag + hexv(i32x4.extmul_low_i16x8_s(a, b)) + " " + hexv(i32x4.extmul_high_i16x8_s(a, b)) + " " +
    hexv(i32x4.extmul_low_i16x8_u(a, b)) + " " + hexv(i32x4.extmul_high_i16x8_u(a, b)));
  print("i64.arith" + tag + hexv(i64x2.add(a, b)) + " " + hexv(i64x2.sub(a, b)) + " " + hexv(i64x2.mul(a, b)));
  print("i64.cmp" + tag + hexv(i64x2.eq(a, b)) + " " + hexv(i64x2.ne(a, b)) + " " + hexv(i64x2.lt_s(a, b)) + " " +
    hexv(i64x2.le_s(a, b)) + " " + hexv(i64x2.gt_s(a, b)) + " " + hexv(i64x2.ge_s(a, b)));
  print("i64.extmul" + tag + hexv(i64x2.extmul_low_i32x4_s(a, b)) + " " + hexv(i64x2.extmul_high_i32x4_s(a, b)) + " " +
    hexv(i64x2.extmul_low_i32x4_u(a, b)) + " " + hexv(i64x2.extmul_high_i32x4_u(a, b)));
  print("bits" + tag + hexv(v128.and(a, b)) + " " + hexv(v128.or(a, b)) + " " + hexv(v128.xor(a, b)) + " " +
    hexv(v128.andnot(a, b)) + " sel " + hexv(v128.bitselect(a, b, vec((ia + ib) % NV))));
}

function unops(i: i32): void {
  let a = vec(i);
  let tag = " " + i.toString() + " ";
  print("un.i8" + tag + hexv(i8x16.neg(a)) + " " + hexv(i8x16.abs(a)) + " pop " + hexv(i8x16.popcnt(a)) +
    " mask " + i8x16.bitmask(a).toString() + " all " + (i8x16.all_true(a) ? "1" : "0") + " any " + (v128.any_true(a) ? "1" : "0"));
  print("un.i16" + tag + hexv(i16x8.neg(a)) + " " + hexv(i16x8.abs(a)) + " mask " + i16x8.bitmask(a).toString() +
    " all " + (i16x8.all_true(a) ? "1" : "0") + " extlo " + hexv(i16x8.extend_low_i8x16_s(a)) + " exthi " +
    hexv(i16x8.extend_high_i8x16_u(a)) + " pairs " + hexv(i16x8.extadd_pairwise_i8x16_s(a)) + " " + hexv(i16x8.extadd_pairwise_i8x16_u(a)));
  print("un.i32" + tag + hexv(i32x4.neg(a)) + " " + hexv(i32x4.abs(a)) + " mask " + i32x4.bitmask(a).toString() +
    " all " + (i32x4.all_true(a) ? "1" : "0") + " extlo " + hexv(i32x4.extend_low_i16x8_u(a)) + " exthi " +
    hexv(i32x4.extend_high_i16x8_s(a)) + " pairs " + hexv(i32x4.extadd_pairwise_i16x8_s(a)) + " " + hexv(i32x4.extadd_pairwise_i16x8_u(a)));
  print("un.i64" + tag + hexv(i64x2.neg(a)) + " " + hexv(i64x2.abs(a)) + " mask " + i64x2.bitmask(a).toString() +
    " all " + (i64x2.all_true(a) ? "1" : "0") + " extlo " + hexv(i64x2.extend_low_i32x4_s(a)) + " exthi " + hexv(i64x2.extend_high_i32x4_u(a)));
  print("un.not" + tag + hexv(v128.not(a)));
}

function shifts(i: i32, k: i32): void {
  let a = vec(i);
  let tag = " " + i.toString() + "<<" + k.toString() + " ";
  print("sh.i8" + tag + hexv(i8x16.shl(a, k)) + " " + hexv(i8x16.shr_s(a, k)) + " " + hexv(i8x16.shr_u(a, k)));
  print("sh.i16" + tag + hexv(i16x8.shl(a, k)) + " " + hexv(i16x8.shr_s(a, k)) + " " + hexv(i16x8.shr_u(a, k)));
  print("sh.i32" + tag + hexv(i32x4.shl(a, k)) + " " + hexv(i32x4.shr_s(a, k)) + " " + hexv(i32x4.shr_u(a, k)));
  print("sh.i64" + tag + hexv(i64x2.shl(a, k)) + " " + hexv(i64x2.shr_s(a, k)) + " " + hexv(i64x2.shr_u(a, k)));
}

function run(): void {
  fill();
  for (let i = 0; i < NV; i++) {
    print("vec " + i.toString() + " " + hexv(vec(i)));
  }
  for (let i = 0; i < NV; i++) {
    binops(i, i);
    binops(i, (i + 1) % NV);
    binops(i, (i + 3) % NV);
  }
  for (let i = 0; i < NV; i++) unops(i);
  const ks: i32[] = [0, 1, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, -1];
  for (let j = 0; j < ks.length; j++) shifts(j % NV, ks[j]);
  // Constant shift counts, emitted as immediates.
  let a = vec(2);
  print("shc " + hexv(i8x16.shl(a, 3)) + " " + hexv(i16x8.shr_s(a, 5)) + " " + hexv(i32x4.shr_u(a, 31)) + " " +
    hexv(i64x2.shl(a, 40)) + " " + hexv(i8x16.shr_s(a, 7)) + " " + hexv(i64x2.shr_s(a, 63)));
  // A SIMD reduction loop over a byte buffer, against the scalar sum.
  let data = new Uint8Array(1024);
  for (let i = 0; i < 1024; i++) data[i] = <u8>(i * 151 + (i >> 3));
  let acc16 = i16x8.splat(0);
  let acc32 = i32x4.splat(0);
  let mx = i8x16.splat(0);
  for (let i = 0; i < 1024; i += 16) {
    let v = v128.load(data.dataStart + <usize>i);
    let w = i16x8.extadd_pairwise_i8x16_u(v);
    acc16 = i16x8.add_sat_u(acc16, w);
    acc32 = i32x4.add(acc32, i32x4.extadd_pairwise_i16x8_u(w));
    mx = i8x16.max_u(mx, v);
  }
  let simdSum = i32x4.extract_lane(acc32, 0) + i32x4.extract_lane(acc32, 1) + i32x4.extract_lane(acc32, 2) + i32x4.extract_lane(acc32, 3);
  let scalar = 0;
  for (let i = 0; i < 1024; i++) scalar += data[i];
  print("reduce simd=" + simdSum.toString() + " scalar=" + scalar.toString() + " sat16=" + hexv(acc16) + " max=" + hexv(mx));
}
run();
