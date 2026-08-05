// UTF-16 to UTF-8 and back, through String.UTF8.  A real conversion in both
// directions over a string with two-, three- and four-byte characters in it,
// so both the encoder's branchy classification and the decoder's continuation
// handling run -- and both allocate, which is why the checksum is taken from
// the bytes rather than from the string.
import { print } from "../wasi";

let s = "";
for (let i = 0; i < 200; i++) s += "kéy-中文-\u{1f600}\u{1f601};";

let total: u32 = 0;
for (let r = 0; r < 10000; r++) {
  let buf = String.UTF8.encode(s);
  let b = Uint8Array.wrap(buf);
  let h: u32 = 2166136261;
  for (let i = 0; i < b.length; i++) h = (h ^ unchecked(b[i])) * 16777619;
  total ^= h;
  let back = String.UTF8.decode(buf);
  total += <u32>back.length;
}
print("total = " + total.toString());
