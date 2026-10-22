// SIMD data movement: i8x16.shuffle with many lane patterns (identity,
// broadcast, reverse, interleave low/high, picks across both operands,
// rotations by byte -- arm64 has ext, zip, uzp, trn, rev and tbl, and a
// compiler picks one per pattern, so each pattern is a separate code path),
// i8x16.swizzle with out-of-range indices (which give 0), splat at every
// shape, extract_lane signed and unsigned and replace_lane at every lane,
// v128.load_splat / load_ext (load8x8_s/u, load16x4, load32x2) / load_zero /
// load_lane / store_lane at unaligned addresses, and three small kernels
// built from them: a 4x4 i32 transpose, a byte search via eq+bitmask+ctz,
// and hex encoding by table lookup with swizzle.
import { print, hex32 } from "../wasi";

function hexv(v: v128): string {
  return hex32(i32x4.extract_lane(v, 0)) + "." + hex32(i32x4.extract_lane(v, 1)) + "." +
         hex32(i32x4.extract_lane(v, 2)) + "." + hex32(i32x4.extract_lane(v, 3));
}

const BUF = memory.data(256, 16);

function setup(): void {
  let x: u32 = 0x2545f491;
  for (let i = 0; i < 256; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    store<u8>(BUF + <usize>i, <u8>(x >> 11));
  }
}

function run(): void {
  setup();
  let a = v128.load(BUF), b = v128.load(BUF + 16);
  print("a " + hexv(a) + " b " + hexv(b));
  print("shuf identity " + hexv(i8x16.shuffle(a, b, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15)));
  print("shuf second " + hexv(i8x16.shuffle(a, b, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31)));
  print("shuf bcast0 " + hexv(i8x16.shuffle(a, b, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)));
  print("shuf bcast17 " + hexv(i8x16.shuffle(a, b, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17)));
  print("shuf reverse " + hexv(i8x16.shuffle(a, b, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)));
  print("shuf ziplo8 " + hexv(i8x16.shuffle(a, b, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23)));
  print("shuf ziphi8 " + hexv(i8x16.shuffle(a, b, 8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31)));
  print("shuf uzpeven " + hexv(i8x16.shuffle(a, b, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30)));
  print("shuf uzpodd " + hexv(i8x16.shuffle(a, b, 1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31)));
  print("shuf trn1 " + hexv(i8x16.shuffle(a, b, 0, 16, 2, 18, 4, 20, 6, 22, 8, 24, 10, 26, 12, 28, 14, 30)));
  print("shuf ext3 " + hexv(i8x16.shuffle(a, b, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18)));
  print("shuf ext13 " + hexv(i8x16.shuffle(a, b, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28)));
  print("shuf rot5 " + hexv(i8x16.shuffle(a, a, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4)));
  print("shuf rev16 " + hexv(i8x16.shuffle(a, b, 1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14)));
  print("shuf rev32 " + hexv(i8x16.shuffle(a, b, 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12)));
  print("shuf rev64 " + hexv(i8x16.shuffle(a, b, 7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8)));
  print("shuf random " + hexv(i8x16.shuffle(a, b, 31, 0, 7, 22, 9, 9, 16, 3, 28, 12, 1, 30, 5, 19, 14, 25)));
  print("shuf i16 " + hexv(i16x8.shuffle(a, b, 7, 6, 13, 12, 0, 8, 3, 15)));
  print("shuf i32 " + hexv(i32x4.shuffle(a, b, 3, 4, 1, 6)) + " " + hexv(i32x4.shuffle(a, b, 0, 0, 5, 5)) + " " + hexv(i32x4.shuffle(a, b, 2, 3, 0, 1)));
  print("shuf i64 " + hexv(i64x2.shuffle(a, b, 1, 2)) + " " + hexv(i64x2.shuffle(a, b, 3, 0)) + " " + hexv(i64x2.shuffle(a, b, 1, 1)));
  // swizzle with indices including >= 16.
  for (let k = 0; k < 6; k++) {
    let idx = v128.load(BUF + 32 + <usize>(k * 16));
    let masked = v128.and(idx, i8x16.splat(<i8>(k < 3 ? 0x0f : 0x1f)));
    print("swizzle " + k.toString() + " idx " + hexv(masked) + " -> " + hexv(i8x16.swizzle(a, masked)) + " raw -> " + hexv(i8x16.swizzle(b, idx)));
  }
  // splat at every shape from run-time scalars.
  let s8 = load<i8>(BUF + 3), s16 = load<i16>(BUF + 5), s32 = load<i32>(BUF + 9), s64 = load<i64>(BUF + 13);
  print("splat " + hexv(i8x16.splat(s8)) + " " + hexv(i16x8.splat(s16)) + " " + hexv(i32x4.splat(s32)) + " " + hexv(i64x2.splat(s64)));
  // extract (signed and unsigned) and replace at every lane.
  let ex = "";
  for (let l = 0; l < 16; l += 1) {
    switch (l) {
      case 0: ex += i8x16.extract_lane_s(b, 0).toString() + "/" + i8x16.extract_lane_u(b, 0).toString() + " "; break;
      case 5: ex += i8x16.extract_lane_s(b, 5).toString() + "/" + i8x16.extract_lane_u(b, 5).toString() + " "; break;
      case 11: ex += i8x16.extract_lane_s(b, 11).toString() + "/" + i8x16.extract_lane_u(b, 11).toString() + " "; break;
      case 15: ex += i8x16.extract_lane_s(b, 15).toString() + "/" + i8x16.extract_lane_u(b, 15).toString() + " "; break;
    }
  }
  ex += "| " + i16x8.extract_lane_s(b, 1).toString() + "/" + i16x8.extract_lane_u(b, 1).toString() + " " +
    i16x8.extract_lane_s(b, 6).toString() + "/" + i16x8.extract_lane_u(b, 6).toString() + " " +
    i16x8.extract_lane_s(b, 7).toString() + "/" + i16x8.extract_lane_u(b, 7).toString() +
    " | " + i32x4.extract_lane(b, 2).toString() + " " + i64x2.extract_lane(b, 1).toString() + " " + i64x2.extract_lane(a, 0).toString();
  print("extract " + ex);
  let r = a;
  r = i8x16.replace_lane(r, 0, s8); r = i8x16.replace_lane(r, 15, <i8>-2);
  r = i16x8.replace_lane(r, 3, s16); r = i32x4.replace_lane(r, 2, s32 ^ 0x5555);
  print("replace " + hexv(r) + " " + hexv(i64x2.replace_lane(a, 1, s64)) + " " + hexv(i64x2.replace_lane(b, 0, -1)));
  // Loads: splat, extend, zero, lane -- at odd addresses.
  for (let off = 0; off < 5; off++) {
    let p = BUF + 64 + <usize>(off * 3);
    print("load@" + off.toString() + " splat8 " + hexv(v128.load8_splat(p)) + " splat16 " + hexv(v128.load16_splat(p)) +
      " splat32 " + hexv(v128.load32_splat(p)) + " splat64 " + hexv(v128.load64_splat(p)));
    print("  ext " + hexv(v128.load8x8_s(p)) + " " + hexv(v128.load8x8_u(p)) + " " + hexv(v128.load16x4_s(p)) + " " +
      hexv(v128.load16x4_u(p)) + " " + hexv(v128.load32x2_s(p)) + " " + hexv(v128.load32x2_u(p)));
    print("  zero " + hexv(v128.load32_zero(p)) + " " + hexv(v128.load64_zero(p)) +
      " lane " + hexv(v128.load8_lane(p, a, 9)) + " " + hexv(v128.load16_lane(p, a, 2)) + " " +
      hexv(v128.load32_lane(p, b, 3)) + " " + hexv(v128.load64_lane(p, b, 0)));
  }
  // store_lane and unaligned v128.store into a zeroed area.
  let out = BUF + 160;
  memory.fill(out, 0, 64);
  v128.store8_lane(out + 1, b, 7);
  v128.store16_lane(out + 3, b, 5);
  v128.store32_lane(out + 7, a, 1);
  v128.store64_lane(out + 13, a, 1);
  v128.store(out + 23, b);
  let dump = "";
  for (let i = 0; i < 40; i++) dump += load<u8>(out + <usize>i).toString(16).padStart(2, "0");
  print("stores " + dump);

  // 4x4 i32 transpose with shuffles.
  let m = new Int32Array(16);
  for (let i = 0; i < 16; i++) m[i] = i * 10 + (i >> 2);
  let r0 = v128.load(m.dataStart), r1 = v128.load(m.dataStart + 16), r2 = v128.load(m.dataStart + 32), r3 = v128.load(m.dataStart + 48);
  let t0 = i32x4.shuffle(r0, r1, 0, 4, 1, 5), t1 = i32x4.shuffle(r2, r3, 0, 4, 1, 5);
  let t2 = i32x4.shuffle(r0, r1, 2, 6, 3, 7), t3 = i32x4.shuffle(r2, r3, 2, 6, 3, 7);
  let c0 = i64x2.shuffle(t0, t1, 0, 2), c1 = i64x2.shuffle(t0, t1, 1, 3);
  let c2 = i64x2.shuffle(t2, t3, 0, 2), c3 = i64x2.shuffle(t2, t3, 1, 3);
  print("transpose " + hexv(c0) + " " + hexv(c1) + " " + hexv(c2) + " " + hexv(c3));
  // Byte search: positions of a byte in a buffer via eq + bitmask + ctz.
  let text = String.UTF8.encode("the quick brown fox jumps over the lazy dog, then the end of the text..");
  let tp = changetype<usize>(text);
  let tl = text.byteLength;
  let positions = "";
  let needle = i8x16.splat(0x74); // 't'
  let i = 0;
  for (; i + 16 <= tl; i += 16) {
    let mask = i8x16.bitmask(i8x16.eq(v128.load(tp + <usize>i), needle));
    while (mask != 0) {
      positions += (i + <i32>ctz(mask)).toString() + " ";
      mask &= mask - 1;
    }
  }
  for (; i < tl; i++) if (load<u8>(tp + <usize>i) == 0x74) positions += i.toString() + "s ";
  print("search t at " + positions + "(len " + tl.toString() + ")");
  // Hex encoding via swizzle into a digit table.
  let table = v128.load(changetype<usize>(String.UTF8.encode("0123456789abcdef")));
  let src = v128.load(BUF + 224);
  let lo = v128.and(src, i8x16.splat(0x0f));
  let hi = i8x16.shr_u(src, 4);
  let hd = i8x16.swizzle(table, hi), ld = i8x16.swizzle(table, lo);
  let first = i8x16.shuffle(hd, ld, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23);
  let second = i8x16.shuffle(hd, ld, 8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31);
  let tmp = memory.data(32, 16);
  v128.store(tmp, first); v128.store(tmp + 16, second);
  let hexs = String.UTF8.decodeUnsafe(tmp, 32);
  let ref = "";
  for (let k = 0; k < 16; k++) ref += load<u8>(BUF + 224 + <usize>k).toString(16).padStart(2, "0");
  print("hex " + hexs + " ref " + ref + " " + (hexs == ref ? "same" : "DIFF"));
}
run();
