// Every integer type converted to every other: i8, u8, i16, u16, i32, u32,
// i64, u64, isize, usize and bool.  Narrowing is a wrap, widening is a sign
// or zero extension decided by the *source* type, and a chain of casts must
// apply each step in turn (i64 -> i8 -> u32 is not i64 -> u32).  In wasm that
// is i32.wrap_i64, i64.extend_i32_s/u, i32.extend8_s/16_s, i64.extend8/16/32_s
// and masks; on arm64 most of it folds into sxtb/uxth/sxtw operand forms,
// which is where a wrong choice of signedness hides.  isize and usize are
// 32-bit on wasm32, and a u32 index added to a 64-bit offset must zero-extend.
import { print, hex64 } from "../wasi";

const src: i64[] = [
  0, 1, -1, 0x7f, 0x80, 0xff, 0x100, -0x80, -0x81, 0x7fff, 0x8000, 0xffff,
  0x10000, -0x8000, 0x7fffffff, 0x80000000, 0xffffffff, 0x100000000,
  -0x80000000, -0x80000001, 0x123456789abcdef0, -0x123456789abcdef0,
  0x7fffffffffffffff, -0x8000000000000000, 0x00000000ffff8080,
];

function from64(x: i64): string {
  let a = <i8>x, b = <u8>x, c = <i16>x, d = <u16>x, e = <i32>x, f = <u32>x;
  let g = <isize>x, h = <usize>x, u = <u64>x;
  return "64 " + hex64(x) + " i8=" + a.toString() + " u8=" + b.toString() +
    " i16=" + c.toString() + " u16=" + d.toString() + " i32=" + e.toString() +
    " u32=" + f.toString() + " isize=" + g.toString() + " usize=" + h.toString() +
    " u64=" + u.toString() + " bool=" + (<bool>x ? "1" : "0");
}

// From each narrow type back up to 64 bits: the extension is the source's.
function widen(x: i64): string {
  let a = <i8>x, b = <u8>x, c = <i16>x, d = <u16>x, e = <i32>x, f = <u32>x;
  return "up " + hex64(x) +
    " i8>i64=" + hex64(<i64>a) + " u8>i64=" + hex64(<i64>b) +
    " i16>i64=" + hex64(<i64>c) + " u16>i64=" + hex64(<i64>d) +
    " i32>i64=" + hex64(<i64>e) + " u32>i64=" + hex64(<i64>f) +
    " i8>u64=" + hex64(<u64>a) + " i32>u64=" + hex64(<u64>e) +
    " i8>u32=" + (<u32>a).toString() + " i16>u32=" + (<u32>c).toString() +
    " u8>i32=" + (<i32>b).toString() + " i8>u16=" + (<u16>a).toString();
}

// Chains: every intermediate step must wrap or extend on its own terms.
function chains(x: i64): string {
  return "chain " + hex64(x) +
    " i64>i8>u32=" + (<u32><i8>x).toString() +
    " i64>u8>i64=" + (<i64><u8>x).toString() +
    " i64>i16>u8>i32=" + (<i32><u8><i16>x).toString() +
    " i64>u16>i8>i64=" + (<i64><i8><u16>x).toString() +
    " i64>i32>u16>u64=" + (<u64><u16><i32>x).toString() +
    " i64>u32>i16>u64=" + hex64(<u64><i16><u32>x) +
    " i64>usize>i64=" + (<i64><usize>x).toString() +
    " i64>isize>u64=" + hex64(<u64><isize>x);
}

// Mixed-width expressions: the narrow operand widens before the operation.
function mixed(x: i64, y: i64): string {
  let a = <i8>x, b = <u16>y, c = <i32>x, d = <u32>y;
  return "mix " + hex64(x) + " " + hex64(y) +
    " i8+u16=" + (<i32>a + <i32>b).toString() +
    " i32*u32w=" + (<i64>c * <i64>d).toString() +
    " u32+i64=" + (<i64>d + x).toString() +
    " i8*i64=" + (<i64>a * y).toString() +
    " (u64)i32>>40=" + hex64(<u64>c >> 40) + " (i64)i32>>40=" + hex64(<i64>c >> 40) +
    " cmp=" + (<i64>c < <i64>d ? "lt" : "ge") + " ucmp=" + (<u64>c < <u64>d ? "lt" : "ge");
}

// usize arithmetic and pointer-sized offsets.
function sizes(i: u32, off: u64): string {
  let p: usize = <usize>i;
  let q: isize = <isize>i;
  let wide = off + <u64>p;
  let back = <usize>(wide);
  return "size " + i.toString() + " " + off.toString() + " p*8=" + (p * 8).toString() +
    " q>>1=" + (q >> 1).toString() + " p>>1=" + (p >> 1).toString() +
    " wide=" + wide.toString() + " back=" + back.toString() +
    " sizeof=" + sizeof<usize>().toString() + "," + sizeof<isize>().toString();
}

function run(): void {
  let n = src.length;
  for (let i = 0; i < n; i++) print(from64(src[i]));
  for (let i = 0; i < n; i++) print(widen(src[i]));
  for (let i = 0; i < n; i++) print(chains(src[i]));
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < n; j += 4) print(mixed(src[i], src[j]));
  }
  for (let i = 0; i < n; i += 2) print(sizes(<u32>src[i], <u64>src[n - 1 - i] & 0xffffffffff));
  // A table of byte values, extended through every route and summed.
  let tot: i64 = 0;
  for (let v = -300; v <= 300; v += 7) {
    let x = <i64>v * 0x01010101;
    tot += <i64><i8>x + <i64><u8>x + <i64><i16>x + <i64><u16>x + <i64><u32><i8>x;
    if ((v & 15) == 0) print("tot " + v.toString() + " " + tot.toString());
  }
  print("tot final " + tot.toString());
}
run();
