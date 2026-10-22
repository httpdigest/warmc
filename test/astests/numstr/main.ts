// Numbers to strings and back.  Integer toString at every width and in
// radixes 2, 8, 10, 16 and 36 (AssemblyScript's itoa uses a two-digits-at-a-
// time table for decimal, a count-leading-zeros path for powers of two and
// 64-bit division for the rest), the extreme values of each type; f64 and f32
// toString through the Grisu-style dtoa (shortest round-trip digits, the
// exponent forms for large and small magnitudes, -0, NaN, Infinity,
// subnormals), toFixed-free formatting done by hand; and parsing back with
// parseInt (radix prefixes, signs, junk after the digits), I32.parseInt /
// I64.parseInt, parseFloat and F64.parseFloat, checking that a printed f64
// parses back to the same bits.  dtoa is wide integer arithmetic (u64
// multiplies, shifts by computed amounts, clz) and a miscompile there changes
// a digit.
import { print, bits } from "../wasi";

const i32s: i32[] = [0, 1, -1, 9, 10, -10, 99, 100, 12345, -12345, 0x7fffffff, -0x80000000, 1000000000, -999999999];
const i64s: i64[] = [0, -1, 4294967296, -4294967297, 1000000000000, 0x7fffffffffffffff, -0x8000000000000000, 123456789012345678, -987654321098765432];
const u64s: u64[] = [0, 0xffffffffffffffff, 0x8000000000000000, 10000000000000000000, 18446744073709551615 - 1];
const radixes: i32[] = [2, 8, 10, 16, 36, 3, 7];

const doubles: f64[] = [
  0.0, -0.0, 1.0, -1.0, 0.1, 0.2, 0.3, 1.0 / 3.0, 2.0 / 3.0, 123.456, -9876.54321, 1e21, 1e-7, 1.5e-7, 123e-20,
  1e100, 1.7976931348623157e308, 5e-324, 2.2250738585072014e-308, 9007199254740993.0, 4.35, 0.000001, 1e6,
  123456789012345680000.0, 3.141592653589793, 2.718281828459045, NaN, Infinity, -Infinity, 100.0, 1e15, 1e16, 1e-6,
];
const floats: f32[] = [0.0, 0.1, 1.0 / 3.0, 3.4028235e38, 1.4e-45, 1.17549435e-38, 16777216.0, 16777217.0, 0.3, 100.5, -2.75e-5, NaN];

function fixed(x: f64, d: i32): string {
  // Round to d decimals by scaling, then insert the point: done in integers.
  let neg = x < 0;
  let scale = Math.pow(10.0, <f64>d);
  let v = <u64>Math.round(Math.abs(x) * scale);
  let p = <u64>scale;
  let ip = v / p, fp = v % p;
  return (neg ? "-" : "") + ip.toString() + (d > 0 ? "." + fp.toString().padStart(d, "0") : "");
}

function run(): void {
  for (let i = 0; i < i32s.length; i++) {
    let v = i32s[i];
    let line = "i32 " + v.toString();
    for (let r = 0; r < radixes.length; r++) line += " r" + radixes[r].toString() + "=" + v.toString(radixes[r]);
    line += " u=" + (<u32>v).toString() + " u16=" + (<u32>v).toString(16) + " i8=" + (<i8>v).toString() + " u16v=" + (<u16>v).toString();
    print(line);
  }
  for (let i = 0; i < i64s.length; i++) {
    let v = i64s[i];
    let line = "i64 " + v.toString();
    for (let r = 0; r < radixes.length; r++) line += " r" + radixes[r].toString() + "=" + v.toString(radixes[r]);
    print(line);
  }
  for (let i = 0; i < u64s.length; i++) {
    let v = u64s[i];
    print("u64 " + v.toString() + " hex=" + v.toString(16) + " b36=" + v.toString(36) + " oct=" + v.toString(8));
  }
  for (let i = 0; i < doubles.length; i++) {
    let x = doubles[i];
    let s = x.toString();
    let back = parseFloat(s);
    print("f64 " + bits(x) + " " + s + " back=" + (isNaN(x) && isNaN(back) ? "nan" : reinterpret<i64>(back) == reinterpret<i64>(x) ? "same" : "other " + bits(back)) +
      " fixed2=" + (isFinite(x) && Math.abs(x) < 1e15 ? fixed(x, 2) : "-") + " fixed5=" + (isFinite(x) && Math.abs(x) < 1e12 ? fixed(x, 5) : "-"));
  }
  for (let i = 0; i < floats.length; i++) {
    let x = floats[i];
    print("f32 " + x.toString() + " as64=" + (<f64>x).toString());
  }
  // Powers of two and ten through dtoa.
  let p2 = "";
  for (let e = -10; e <= 70; e += 8) p2 += Math.pow(2.0, <f64>e).toString() + " ";
  print("pow2 " + p2);
  let p10 = "";
  for (let e = -25; e <= 25; e += 5) p10 += Math.pow(10.0, <f64>e).toString() + " ";
  print("pow10 " + p10);
  // Parsing.
  const ints = ["0", "42", "-42", "+17", "0x1F", "0X1f", "0b101", "0o17", "  12", "12abc", "abc", "", "-0", "2147483647", "2147483648", "-2147483649", "9007199254740993", "077"];
  for (let i = 0; i < ints.length; i++) {
    print("parseInt " + "'" + ints[i] + "' -> " + bits(parseInt(ints[i])) + " i32=" + I32.parseInt(ints[i]).toString() +
      " i64=" + I64.parseInt(ints[i]).toString() + " r16=" + bits(parseInt(ints[i], 16)) + " r8=" + bits(parseInt(ints[i], 8)));
  }
  const fls = ["0", "1.5", "-2.25", "1e10", "1E-5", ".5", "5.", "  3.25", "3.25xyz", "Infinity", "-Infinity", "NaN", "abc", "1e400", "1e-400",
    "0.1", "0.30000000000000004", "123456789012345678901234567890", "4.9406564584124654e-324", "2.2250738585072011e-308"];
  for (let i = 0; i < fls.length; i++) {
    print("parseFloat '" + fls[i] + "' -> " + bits(parseFloat(fls[i])) + " " + F64.parseFloat(fls[i]).toString() +
      " f32=" + F32.parseFloat(fls[i]).toString());
  }
  // A sweep: format and reparse a spread of doubles, counting round-trips.
  let x = 1.0;
  let ok = 0, total = 0;
  let h: i64 = 0;
  for (let k = 0; k < 300; k++) {
    x = x * 1.37 + 0.011;
    if (x > 1e30) x = x * 1e-35;
    let s = x.toString();
    total++;
    if (parseFloat(s) == x) ok++;
    for (let c = 0; c < s.length; c++) h = h * 131 + <i64>s.charCodeAt(c);
    if (k % 30 == 29) print("sweep " + k.toString() + " " + s + " ok=" + ok.toString() + "/" + total.toString() + " h=" + h.toString());
  }
}
run();
