// Float SIMD: f32x4 and f64x2 add/sub/mul/div, sqrt, neg/abs, min/max
// (NaN-propagating with -0 ordered below +0, which arm64's fmin/fmax match
// and x86's minps/maxps do not) and pmin/pmax (the "pseudo" ones, defined as
// b < a ? b : a, which return the first operand when either is NaN and keep
// the first of two zeros), ceil/floor/trunc/nearest,
// every comparison (NaN lanes compare false, ne true), conversions --
// f32x4.convert_i32x4_s/u, f64x2.convert_low_i32x4_s/u, the saturating
// i32x4.trunc_sat_f32x4_s/u and trunc_sat_f64x2_s/u_zero (NaN to 0,
// out-of-range clamped), f32x4.demote_f64x2_zero and f64x2.promote_low_f32x4
// -- and lane extract/replace/splat.  Lanes hold NaN, +-0, +-inf, subnormals,
// halfway values for nearest, and values just past the i32/u32 limits.
// Results are printed as bits, with a NaN lane from arithmetic printed as
// "nan" (the spec leaves its sign and payload open); inputs, neg/abs and
// pmin/pmax, whose bits the spec fixes, are printed raw, NaNs included.
import { print, hex32, hex64, canon32, canon64 } from "../wasi";

// Raw words: for integer results, and for float results whose bits the spec
// fixes (inputs, neg/abs, pmin/pmax, which select an operand, lane moves).
function hexv(v: v128): string {
  return hex32(i32x4.extract_lane(v, 0)) + "." + hex32(i32x4.extract_lane(v, 1)) + "." +
         hex32(i32x4.extract_lane(v, 2)) + "." + hex32(i32x4.extract_lane(v, 3));
}

// Float lanes from arithmetic: a NaN lane prints as "nan", because the spec
// leaves the sign and payload of an arithmetic NaN to the engine (x64 makes
// negative ones, arm64 positive).
function lf(x: f32): string { return isNaN(x) ? "nan" : hex32(reinterpret<u32>(x)); }
function ld(x: f64): string { return isNaN(x) ? "nan" : hex64(reinterpret<u64>(x)); }
function nf4(v: v128): string {
  return lf(f32x4.extract_lane(v, 0)) + "." + lf(f32x4.extract_lane(v, 1)) + "." +
         lf(f32x4.extract_lane(v, 2)) + "." + lf(f32x4.extract_lane(v, 3));
}
function nd2(v: v128): string {
  return ld(f64x2.extract_lane(v, 0)) + "." + ld(f64x2.extract_lane(v, 1));
}

const F32: f32[] = [
  0.0, -0.0, 1.0, -1.5, 2.5, -2.5, 0.5, 3.5, NaN, Infinity, -Infinity, 1e-40,
  2147483520.0, 2147483648.0, -2147483904.0, 4294967040.0, 4294967296.0, -0.75, 1e30, 7.25,
];
const F64: f64[] = [
  0.0, -0.0, 1.0, -1.5, 2.5, -2.5, 0.5, -3.5, NaN, Infinity, -Infinity, 5e-324,
  2147483647.5, 2147483648.0, -2147483648.9, -2147483649.0, 4294967295.9, 4294967296.0, 1e300, 0.1,
];
const BUF = memory.data(16 * 32, 16);

function f4(i: i32): v128 {
  let p = BUF;
  for (let k = 0; k < 4; k++) store<f32>(p + (<usize>k << 2), F32[(i + k * 3) % F32.length]);
  return v128.load(p);
}
function d2(i: i32): v128 {
  let p = BUF + 16;
  store<f64>(p, F64[i % F64.length]);
  store<f64>(p + 8, F64[(i * 7 + 3) % F64.length]);
  return v128.load(p);
}

function f32ops(i: i32, j: i32): void {
  let a = f4(i), b = f4(j);
  let t = " " + i.toString() + "," + j.toString() + " ";
  print("f32.arith" + t + nf4(f32x4.add(a, b)) + " " + nf4(f32x4.sub(a, b)) + " " + nf4(f32x4.mul(a, b)) + " " + nf4(f32x4.div(a, b)));
  print("f32.minmax" + t + nf4(f32x4.min(a, b)) + " " + nf4(f32x4.max(a, b)) + " " + hexv(f32x4.pmin(a, b)) + " " + hexv(f32x4.pmax(a, b)));
  print("f32.cmp" + t + hexv(f32x4.eq(a, b)) + " " + hexv(f32x4.ne(a, b)) + " " + hexv(f32x4.lt(a, b)) + " " +
    hexv(f32x4.le(a, b)) + " " + hexv(f32x4.gt(a, b)) + " " + hexv(f32x4.ge(a, b)));
}

function f64ops(i: i32, j: i32): void {
  let a = d2(i), b = d2(j);
  let t = " " + i.toString() + "," + j.toString() + " ";
  print("f64.arith" + t + nd2(f64x2.add(a, b)) + " " + nd2(f64x2.sub(a, b)) + " " + nd2(f64x2.mul(a, b)) + " " + nd2(f64x2.div(a, b)));
  print("f64.minmax" + t + nd2(f64x2.min(a, b)) + " " + nd2(f64x2.max(a, b)) + " " + hexv(f64x2.pmin(a, b)) + " " + hexv(f64x2.pmax(a, b)));
  print("f64.cmp" + t + hexv(f64x2.eq(a, b)) + " " + hexv(f64x2.ne(a, b)) + " " + hexv(f64x2.lt(a, b)) + " " +
    hexv(f64x2.le(a, b)) + " " + hexv(f64x2.gt(a, b)) + " " + hexv(f64x2.ge(a, b)));
}

function unary(i: i32): void {
  let a = f4(i), d = d2(i);
  let t = " " + i.toString() + " ";
  print("f32.un" + t + hexv(a) + " neg " + hexv(f32x4.neg(a)) + " abs " + hexv(f32x4.abs(a)) + " sqrt " + nf4(f32x4.sqrt(a)));
  print("f32.round" + t + nf4(f32x4.ceil(a)) + " " + nf4(f32x4.floor(a)) + " " + nf4(f32x4.trunc(a)) + " " + nf4(f32x4.nearest(a)));
  print("f32.conv" + t + "s " + hexv(i32x4.trunc_sat_f32x4_s(a)) + " u " + hexv(i32x4.trunc_sat_f32x4_u(a)) +
    " promote " + nd2(f64x2.promote_low_f32x4(a)));
  print("f64.un" + t + hexv(d) + " neg " + hexv(f64x2.neg(d)) + " abs " + hexv(f64x2.abs(d)) + " sqrt " + nd2(f64x2.sqrt(d)));
  print("f64.round" + t + nd2(f64x2.ceil(d)) + " " + nd2(f64x2.floor(d)) + " " + nd2(f64x2.trunc(d)) + " " + nd2(f64x2.nearest(d)));
  print("f64.conv" + t + "s " + hexv(i32x4.trunc_sat_f64x2_s_zero(d)) + " u " + hexv(i32x4.trunc_sat_f64x2_u_zero(d)) +
    " demote " + nf4(f32x4.demote_f64x2_zero(d)));
}

function intconv(i: i32): void {
  let p = BUF + 32;
  const iv: i32[] = [0, 1, -1, 16777217, -16777217, 0x7fffffff, -0x80000000, 123456789, 0x7fffff80, -7];
  for (let k = 0; k < 4; k++) store<i32>(p + (<usize>k << 2), iv[(i + k) % iv.length]);
  let v = v128.load(p);
  print("int.conv " + hexv(v) + " f32s " + nf4(f32x4.convert_i32x4_s(v)) + " f32u " + nf4(f32x4.convert_i32x4_u(v)) +
    " f64s " + nd2(f64x2.convert_low_i32x4_s(v)) + " f64u " + nd2(f64x2.convert_low_i32x4_u(v)));
}

function run(): void {
  for (let i = 0; i < F32.length; i += 2) {
    f32ops(i, i + 1);
    f32ops(i, (i * 5 + 3) % F32.length);
  }
  for (let i = 0; i < F64.length; i += 2) {
    f64ops(i, i + 1);
    f64ops(i, (i * 5 + 3) % F64.length);
  }
  for (let i = 0; i < 20; i++) unary(i);
  for (let i = 0; i < 10; i++) intconv(i);
  // Lane access: extract, replace, splat.
  let v = f4(3);
  let w = f32x4.replace_lane(v, 2, <f32>-9.5);
  print("lanes " + f32x4.extract_lane(v, 0).toString() + " " + f32x4.extract_lane(v, 3).toString() + " " + hexv(w) +
    " splat " + hexv(f32x4.splat(<f32>F32[6])) + " " + hexv(f64x2.splat(F64[3])));
  let d = d2(5);
  let e = f64x2.replace_lane(d, 1, F64[19]);
  print("lanes64 " + f64x2.extract_lane(d, 0).toString() + " " + f64x2.extract_lane(e, 1).toString() + " " + hexv(e));
  // A dot product and an axpy in SIMD against scalar code.
  let n = 64;
  let xs = new Float32Array(n), ys = new Float32Array(n);
  let xd = new Float64Array(n), yd = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    xs[i] = <f32>(i % 9) * <f32>0.37 - <f32>1.1; ys[i] = <f32>(i % 5) * <f32>-0.61 + <f32>2.0;
    xd[i] = <f64>xs[i] * 1.0000001; yd[i] = <f64>ys[i] / 3.0;
  }
  let acc = f32x4.splat(0);
  let accd = f64x2.splat(0);
  for (let i = 0; i < n; i += 4) {
    acc = f32x4.add(acc, f32x4.mul(v128.load(xs.dataStart + (<usize>i << 2)), v128.load(ys.dataStart + (<usize>i << 2))));
  }
  for (let i = 0; i < n; i += 2) {
    let a = v128.load(xd.dataStart + (<usize>i << 3));
    let y = v128.load(yd.dataStart + (<usize>i << 3));
    v128.store(yd.dataStart + (<usize>i << 3), f64x2.add(f64x2.mul(f64x2.splat(2.5), a), y));
    accd = f64x2.add(accd, f64x2.mul(a, a));
  }
  let lanes = [f32x4.extract_lane(acc, 0), f32x4.extract_lane(acc, 1), f32x4.extract_lane(acc, 2), f32x4.extract_lane(acc, 3)];
  let s4 = (lanes[0] + lanes[1]) + (lanes[2] + lanes[3]);
  let sc: f32 = 0;
  let sc4: StaticArray<f32> = [0, 0, 0, 0];
  for (let i = 0; i < n; i++) sc4[i & 3] += xs[i] * ys[i];
  sc = (sc4[0] + sc4[1]) + (sc4[2] + sc4[3]);
  print("dot simd=" + hex32(canon32(s4)) + " scalar-lanes=" + hex32(canon32(sc)) +
    " norm2=" + hex32(<u32>(canon64(f64x2.extract_lane(accd, 0) + f64x2.extract_lane(accd, 1)) >> 32)) +
    " axpy[7]=" + yd[7].toString() + " axpy[63]=" + yd[63].toString());
}
run();
