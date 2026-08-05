// A class with more fields than there are registers, copied field by field.
// What this measures is the frame: the copy cannot stay in registers, so every
// field is a load and a store, and the offsets are what the addressing modes
// have to fold.
//
// Every field is read back into the checksum, not just the three that are
// written.  Written-and-never-read fields are stores binaryen is entitled to
// delete, and a kernel that claims to copy twelve of them should not depend on
// it choosing not to.
import { print } from "../wasi";

class Big {
  a: i64 = 0; b: i64 = 0; c: i64 = 0; d: i64 = 0;
  e: i64 = 0; f: i64 = 0; g: i64 = 0; h: i64 = 0;
  i: i64 = 0; j: i64 = 0; k: i64 = 0; l: i64 = 0;
}

function copyInto(dst: Big, src: Big): void {
  dst.a = src.a; dst.b = src.b; dst.c = src.c; dst.d = src.d;
  dst.e = src.e; dst.f = src.f; dst.g = src.g; dst.h = src.h;
  dst.i = src.i; dst.j = src.j; dst.k = src.k; dst.l = src.l;
}

let x = new Big();
let y = new Big();
let total: i64 = 0;
for (let r = 0; r < 12000000; r++) {
  let v = <i64>r;
  x.a = v; x.b = v + 1; x.c = v + 2; x.d = v + 3;
  x.e = v + 4; x.f = v * 3; x.g = v + 6; x.h = v + 7;
  x.i = v + 8; x.j = v + 9; x.k = v + 10; x.l = v ^ 0x5555;
  copyInto(y, x);
  total += y.a + y.b + y.c + y.d + y.e + y.f +
           y.g + y.h + y.i + y.j + y.k + y.l;
}
print("total = " + total.toString());
