// Sorting an array of strings.  Two things per comparison that the integer
// sort does not have: an indirect call into the comparator, and a UTF-16
// compare loop that runs until the strings differ.  Array#sort is
// AssemblyScript's own, so the sort itself is wasm too.
import { print } from "../wasi";

let total = 0;
for (let r = 0; r < 1700; r++) {
  let a = new Array<string>();
  let x: u32 = <u32>r * 2654435761 + 12345;
  for (let i = 0; i < 512; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    a.push("key-" + (x % 100000).toString());
  }
  a.sort((p: string, q: string) => p < q ? -1 : (p > q ? 1 : 0));
  total += a[0].length + a[511].charCodeAt(0);
}
print("total = " + total.toString());
