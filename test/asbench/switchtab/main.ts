// A dense switch, which binaryen emits as a br_table.  The case values are
// contiguous and the selector is data-dependent, so this is the jump table and
// the indirect branch rather than a compare chain -- the one kernel here whose
// control flow warmc has to lower rather than optimise.
import { print } from "../wasi";

function step(k: i32, v: i32): i32 {
  switch (k) {
    case 0: return v + 1;
    case 1: return v ^ 0x2a;
    case 2: return v << 1;
    case 3: return v - 7;
    case 4: return v * 3;
    case 5: return v >> 2;
    case 6: return v | 0x100;
    case 7: return v & 0x3fff;
    case 8: return ~v;
    case 9: return v + 0x1234;
    case 10: return v ^ (v >> 3);
    case 11: return v * 5 + 1;
    default: return v;
  }
}

let v = 1;
for (let r = 0; r < 36000000; r++) {
  v = step(v % 12, v) + r;
  v &= 0x3fffffff;
}
print("v = " + v.toString());
