// asbench/sum with the bounds check taken out by hand, and identical
// otherwise -- same array, same counts, same checksum.  It exists to be
// subtracted from that one: on its own the figure says nothing, and the pair
// says what AssemblyScript's checked element access is worth here.
//
// `unchecked()` is not exotic in AssemblyScript.  It is what tuned code in the
// wild uses, so this row is as representative as the other; the two together
// are what neither is alone.
import { print } from "../wasi";

function sumAll(a: Uint32Array): u64 {
  let s: u64 = 0;
  for (let i = 0; i < a.length; i++) s += <u64>unchecked(a[i]);
  return s;
}

let a = new Uint32Array(4096);
for (let i = 0; i < a.length; i++) a[i] = <u32>i * 2654435761;
let s: u64 = 0;
for (let r = 0; r < 20000; r++) {
  a[r & 4095] = <u32>r;
  s += sumAll(a);
}
print("s = " + s.toString());
