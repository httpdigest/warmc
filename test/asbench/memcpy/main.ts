// Bulk memory: typed-array set and copyWithin, which AssemblyScript lowers to
// memory.copy.  Sizes are a mix of short and long so both the tail and the
// wide path run, and the offsets are odd so nothing is conveniently aligned.
import { print } from "../wasi";

let n = 1 << 16;
let a = new Uint8Array(n);
let b = new Uint8Array(n);
for (let i = 0; i < n; i++) a[i] = <u8>(i * 7);
let total: u32 = 0;
for (let r = 0; r < 220000; r++) {
  b.set(a);
  b.copyWithin(1, 0, 4096);
  b.copyWithin(4097, 4096, 4096 + 37);
  a[r & (n - 1)] = <u8>r;
    // Widened one at a time on purpose: AssemblyScript keeps u8 + u8 in u8, so
  // summing three bytes here without the casts wraps, and this checksum came
  // out zero every round until it did.
  total += <u32>unchecked(b[1]) + <u32>unchecked(b[4098]) + <u32>unchecked(b[n - 1]);
}
print("total = " + total.toString());
