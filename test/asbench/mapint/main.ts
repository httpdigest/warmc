// Map<i32,i32>: AssemblyScript's hash map, inserted into and looked up.  The
// table lives in linear memory and the probe is a loop, so this is a
// pointer-chasing kernel with an unpredictable load at the end of it --
// gobench/maphash for the other producer.
import { print } from "../wasi";

let total = 0;
for (let r = 0; r < 900; r++) {
  let m = new Map<i32, i32>();
  let x: u32 = <u32>r * 2654435761 + 1;
  for (let i = 0; i < 4000; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    m.set(<i32>(x & 0xffff), i);
  }
  x = <u32>r * 2654435761 + 1;
  for (let i = 0; i < 4000; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    let k = <i32>(x & 0xffff);
    if (m.has(k)) total += m.get(k);
  }
  total += m.size;
}
print("total = " + total.toString());
