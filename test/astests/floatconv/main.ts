// Conversions between floats and integers, and between the two float widths.
// AssemblyScript builds with nontrapping-f2i, so a cast like <i32>x is
// i32.trunc_sat_f64_s: truncation toward zero, NaN to 0, out-of-range values
// clamped to the type's min/max -- never a trap.  Every float -> int pair
// (f32/f64 to i32/u32/i64/u64, plus the narrow types, which saturate at 32
// bits and then wrap) is driven at the boundaries: just inside and outside
// 2^31, 2^32, 2^63 and 2^64, negative values to unsigned (0), -0.5, -1+ulp.
// The int -> float direction rounds to nearest-even, which matters above 2^24
// (f32) and 2^53 (f64), and u64 -> f32 has a double-rounding trap for
// implementations that go through f64.  Also f64 <-> f32 demote/promote with
// overflow, underflow to subnormals, and NaN; reinterpret both ways.  A NaN
// out of demote/promote prints as "nan" (the spec leaves its sign and payload
// open); reinterpreted bit patterns print exactly, NaNs included.
import { print, bits, bits32, hex32, hex64 } from "../wasi";

const fd: f64[] = [
  0.0, -0.0, 0.4, 0.5, 0.9999999999999999, 1.0, -0.5, -0.9999, -1.0, -1.5, 2.5,
  127.9, 128.0, -128.5, 255.5, 256.0, 32767.9, 65535.99, 65536.0,
  2147483647.0, 2147483647.5, 2147483648.0, -2147483648.0, -2147483648.9, -2147483649.0,
  4294967295.0, 4294967295.9, 4294967296.0, 1e15, -1e15,
  9223372036854774784.0, 9223372036854775808.0, -9223372036854775808.0, -9223372036854777856.0,
  18446744073709549568.0, 18446744073709551616.0, 1e30, -1e30, Infinity, -Infinity, NaN,
];

function f2i(x: f64): string {
  return "d2i " + bits(x) + " i32=" + (<i32>x).toString() + " u32=" + (<u32>x).toString() +
    " i64=" + (<i64>x).toString() + " u64=" + (<u64>x).toString() +
    " i8=" + (<i8>x).toString() + " u8=" + (<u8>x).toString() +
    " i16=" + (<i16>x).toString() + " u16=" + (<u16>x).toString() +
    " bool=" + (<bool>x ? "1" : "0");
}

function f2i32(x: f32): string {
  return "f2i " + bits32(x) + " i32=" + (<i32>x).toString() + " u32=" + (<u32>x).toString() +
    " i64=" + (<i64>x).toString() + " u64=" + (<u64>x).toString() +
    " u8=" + (<u8>x).toString() + " i16=" + (<i16>x).toString();
}

const ii: i64[] = [
  0, 1, -1, 16777216, 16777217, 16777218, 16777219, -16777217, 0x7fffffff, -0x80000000,
  0xffffffff, 9007199254740992, 9007199254740993, 9007199254740995, -9007199254740993,
  0x7fffffffffffffff, -0x8000000000000000, 0x7ffffffffffffdff, 0x0020000020000001,
  0x1000000000000801, 1234567890123456789,
];
const uu: u64[] = [
  0, 0xffffffffffffffff, 0x8000000000000000, 0x8000000000000401, 0x8000008000000001,
  0xffffff7fffffffff, 0xffffff8000000000, 0xfffffffffffff400, 0x0000000100000001, 0x8000000000000001,
];

function i2f(x: i64): string {
  return "i2f " + x.toString() + " d=" + bits(<f64>x) + " f=" + bits32(<f32>x) +
    " i32d=" + bits(<f64><i32>x) + " i32f=" + bits32(<f32><i32>x) +
    " u32d=" + bits(<f64><u32>x) + " u32f=" + bits32(<f32><u32>x) +
    " i8f=" + bits32(<f32><i8>x) + " u16d=" + bits(<f64><u16>x);
}

function u2f(x: u64): string {
  return "u2f " + x.toString() + " d=" + bits(<f64>x) + " f=" + bits32(<f32>x) +
    " via-d=" + bits32(<f32><f64>x);
}

const dd: f64[] = [
  0.1, -0.1, 1.0 / 3.0, 3.4028234663852886e38, 3.4028235677973366e38, 3.4028236e38, 1e39, -1e39,
  1.1754943508222875e-38, 1.401298464324817e-45, 7.006492321624085e-46, 7.006492321624087e-46,
  1e-50, -1e-50, 16777217.0, 16777219.0, 1.0000000596046448, 1.0000000596046450, NaN, Infinity,
];

function demote(x: f64): string {
  let f = <f32>x;
  return "demote " + bits(x) + " f=" + bits32(f) + " back=" + bits(<f64>f) +
    " exact=" + (<f64>f == x ? "1" : "0");
}

function run(): void {
  for (let i = 0; i < fd.length; i++) print(f2i(fd[i]));
  for (let i = 0; i < fd.length; i++) print(f2i32(<f32>fd[i]));
  for (let i = 0; i < ii.length; i++) print(i2f(ii[i]));
  for (let i = 0; i < uu.length; i++) print(u2f(uu[i]));
  for (let i = 0; i < dd.length; i++) print(demote(dd[i]));
  // reinterpret both ways, at both widths.
  const pats: i64[] = [0, 0x8000000000000000, 0x3ff0000000000000, 0x7ff0000000000000, 0x7ff8000000000001, 0x0000000000000001, 0x400921fb54442d18];
  for (let i = 0; i < pats.length; i++) {
    let x = reinterpret<f64>(pats[i]);
    let y = reinterpret<f32>(<i32>pats[i] ^ <i32>(pats[i] >> 32));
    print("reinterp " + hex64(pats[i]) + " -> " + hex64(reinterpret<u64>(x)) +
      " f32 " + hex32(reinterpret<u32>(y)) + " isNaN=" + (isNaN(x) ? "1" : "0") + (isNaN(y) ? "1" : "0"));
  }
  // Round-trips: int -> float -> int for a sweep of 64-bit values, counting
  // which survive exactly in f64 and in f32.
  let x: u64 = 1;
  let ok64 = 0, ok32 = 0;
  let line = "";
  for (let k = 0; k < 128; k++) {
    let v = <i64>(x >> <u64>(k & 63));
    if (<i64><f64>v == v) ok64++;
    if (<i64><f32>v == v) ok32++;
    x = x * 6364136223846793005 + 1442695040888963407;
    if ((k & 15) == 15) {
      line = "roundtrip " + k.toString() + " ok64=" + ok64.toString() + " ok32=" + ok32.toString() +
        " last=" + bits(<f64>v) + " " + bits32(<f32><u64>v);
      print(line);
    }
  }
  // Float -> int inside a loop with the float computed: trunc of a running value.
  let f = -3.75;
  let s: i64 = 0;
  for (let k = 0; k < 60; k++) {
    s += <i64><i32>f + <i64><u32>f + <i64><u8>f;
    f = f * -1.9 + 0.3;
    if (k % 10 == 9) print("trunc-run " + k.toString() + " " + bits(f) + " " + s.toString());
  }
}
run();
