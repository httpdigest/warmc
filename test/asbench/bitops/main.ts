// A dependent chain of the bit builtins on u64: rotl, popcnt, clz, ctz.  Each
// maps to one wasm instruction and each of those maps to one or two arm64
// instructions, so what this measures is the mapping and not much else -- and
// the chain is serial on purpose, so nothing in it overlaps.
import { print } from "../wasi";

let x: u64 = 0x0123456789abcdef;
let acc: u64 = 0;
for (let r = 0; r < 20000000; r++) {
  x = rotl<u64>(x, 7) ^ <u64>r;
  x += <u64>popcnt<u64>(x);
  x ^= <u64>clz<u64>(x) << 3;
  x -= <u64>ctz<u64>(x | 0x100);
  acc ^= x;
}
print("acc = " + acc.toString() + " x = " + x.toString());
