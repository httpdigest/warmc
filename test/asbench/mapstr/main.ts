// Map<string, i32> and Set<i32>: counting words drawn from a fixed
// vocabulary, then probing with a second, separately built copy of the same
// words.  A string key costs what an integer key does not.  binaryen leaves
// the hash out of line as HASH<String>, an xxHash32 over the string's UTF-16
// bytes (multiplies and rotates), and Map#find compares candidate keys with
// String.__eq inlined into it: the lengths from the object headers, then four
// characters at a time through i64 loads and a u16 tail.  The probe strings
// are distinct objects from the inserted ones, so a hit there is a real
// content compare and never the pointer-equality shortcut.  Every other probe
// deletes its key, so the delete path runs too, and a Set<i32> is filled
// alongside so the integer hash shares the loop with the string one.
//
// mapint is the integer-key map; the gap between the two rows is roughly what
// hashing and comparing strings costs.
import { print } from "../wasi";

const NW = 600;

// Two arrays of equal strings that are distinct objects, so a hit in the map
// is a content comparison and not just the same pointer coming back.
function makeWords(): Array<string> {
  let a = new Array<string>(NW);
  for (let i = 0; i < NW; i++) {
    a[i] = "word-" + (i * 7919 % 1000).toString() + "-" + (i & 7).toString();
  }
  return a;
}

let ins = makeWords();
let probe = makeWords();
let total = 0;
for (let r = 0; r < 960; r++) {
  let m = new Map<string, i32>();
  let seen = new Set<i32>();
  let x: u32 = <u32>r * 2654435761 + 1;
  for (let i = 0; i < 1200; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    let k = ins[<i32>(x % NW)];
    m.set(k, m.has(k) ? m.get(k) + 1 : 1);
    seen.add(<i32>(x & 0x3ff));
  }
  for (let i = 0; i < NW; i += 3) {
    let k = probe[i];
    if (m.has(k)) total += m.get(k) * (i & 15);
    if (i & 1) m.delete(k);
    if (seen.has(i)) total += 1;
  }
  total += m.size + seen.size;
}
print("total = " + total.toString());
