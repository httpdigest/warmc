// Scanning a string with charCodeAt.  AssemblyScript strings are UTF-16, so
// this is a u16 load and a bounds check per character -- not the byte-at-a-time
// UTF-8 decode that gobench/utf8scan measures.  The two kernels look like the
// same program and compile to different loops, which is the reason both exist.
import { print } from "../wasi";

let s = "";
for (let i = 0; i < 400; i++) s += "ascii-é中\u{1f600}-mixed;";

let total = 0;
for (let r = 0; r < 14000; r++) {
  for (let i = 0; i < s.length; i++) total += s.charCodeAt(i) & 7;
}
print("total = " + total.toString() + " len = " + s.length.toString());
