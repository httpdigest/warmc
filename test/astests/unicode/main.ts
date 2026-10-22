// Unicode: AssemblyScript strings are UTF-16, so anything outside the BMP is
// a surrogate pair.  String.UTF8.encode/decode and String.UTF16.encode/decode
// round-trip text with 1-, 2-, 3- and 4-byte UTF-8 sequences; byteLength
// counts; codePointAt on a high surrogate returns the full code point and on
// a low one returns the low half; fromCodePoint builds pairs; lone
// surrogates are encoded as their 3-byte (WTF-8) forms; a null-terminated
// encode; decodeUnsafe of a buffer with a truncated tail; a hand-written
// UTF-8 decoder and encoder (shift and mask chains over bytes) checked
// against the library's; and case mapping beyond ASCII (toUpperCase of the
// German sharp s expands to two characters, Greek and Cyrillic change case,
// which goes through the library's binary-searched case tables).  Bytes are
// printed as hex.
import { print } from "../wasi";

function hexBytes(buf: ArrayBuffer): string {
  let u = Uint8Array.wrap(buf);
  let s = "";
  for (let i = 0; i < u.length; i++) s += u[i].toString(16).padStart(2, "0");
  return s;
}

function units(s: string): string {
  let r = "";
  for (let i = 0; i < s.length; i++) r += (i > 0 ? " " : "") + s.charCodeAt(i).toString(16);
  return r;
}

// Hand-written UTF-8 encoder over code points.
function myEncode(s: string): Uint8Array {
  let out = new Uint8Array(s.length * 3 + 4);
  let n = 0;
  for (let i = 0; i < s.length; i++) {
    let c = <u32>s.codePointAt(i);
    if (c >= 0x10000) i++;
    else if (c >= 0xd800 && c < 0xe000) c = 0xfffd;
    if (c < 0x80) out[n++] = <u8>c;
    else if (c < 0x800) { out[n++] = <u8>(0xc0 | (c >> 6)); out[n++] = <u8>(0x80 | (c & 0x3f)); }
    else if (c < 0x10000) { out[n++] = <u8>(0xe0 | (c >> 12)); out[n++] = <u8>(0x80 | ((c >> 6) & 0x3f)); out[n++] = <u8>(0x80 | (c & 0x3f)); }
    else {
      out[n++] = <u8>(0xf0 | (c >> 18)); out[n++] = <u8>(0x80 | ((c >> 12) & 0x3f));
      out[n++] = <u8>(0x80 | ((c >> 6) & 0x3f)); out[n++] = <u8>(0x80 | (c & 0x3f));
    }
  }
  return out.subarray(0, n);
}

// Hand-written decoder: UTF-8 bytes to code points, summed and listed.
function myDecode(b: Uint8Array): string {
  let r = "";
  let i = 0;
  while (i < b.length) {
    let c = <u32>b[i];
    let cp: u32;
    if (c < 0x80) { cp = c; i += 1; }
    else if ((c & 0xe0) == 0xc0) { cp = ((c & 0x1f) << 6) | (<u32>b[i + 1] & 0x3f); i += 2; }
    else if ((c & 0xf0) == 0xe0) { cp = ((c & 0x0f) << 12) | ((<u32>b[i + 1] & 0x3f) << 6) | (<u32>b[i + 2] & 0x3f); i += 3; }
    else { cp = ((c & 0x07) << 18) | ((<u32>b[i + 1] & 0x3f) << 12) | ((<u32>b[i + 2] & 0x3f) << 6) | (<u32>b[i + 3] & 0x3f); i += 4; }
    r += String.fromCodePoint(cp);
  }
  return r;
}

function run(): void {
  const samples = [
    "plain ascii", "café crème brûlée", "Γειά σου Κόσμε",
    "Привет", "日本語テキスト", "emoji 😀🎉 end",
    "𐀀 􏿿", "mixed aß€𝄞", "straße", "",
  ];
  for (let k = 0; k < samples.length; k++) {
    let s = samples[k];
    let u8 = String.UTF8.encode(s);
    let back = String.UTF8.decode(u8);
    let u16 = String.UTF16.encode(s);
    let back16 = String.UTF16.decode(u16);
    let mine = myEncode(s);
    let same = mine.length == u8.byteLength;
    let lib = Uint8Array.wrap(u8);
    for (let i = 0; same && i < mine.length; i++) if (mine[i] != lib[i]) same = false;
    print("s" + k.toString() + " len=" + s.length.toString() + " utf8len=" + String.UTF8.byteLength(s).toString() +
      " bytes=" + hexBytes(u8) + " rt8=" + (back == s ? "1" : "0") + " rt16=" + (back16 == s ? "1" : "0") +
      " mine=" + (same ? "same" : "DIFF") + " dec=" + (myDecode(lib) == s ? "same" : "DIFF"));
    print("  units " + units(s));
    print("  upper " + units(s.toUpperCase()) + " lower " + units(s.toLowerCase()));
    let cps = "";
    for (let i = 0; i < s.length; i++) cps += s.codePointAt(i).toString(16) + " ";
    print("  cps " + cps);
  }
  // Lone surrogates: the library encodes each as its own 3-byte sequence.
  let lone = "a\ud800b\udc00c" + String.fromCharCode(0xdbff);
  print("lone " + hexBytes(String.UTF8.encode(lone)) + " units " + units(lone) + " len8=" + String.UTF8.byteLength(lone).toString());
  // Null-terminated encode.
  print("nullterm " + hexBytes(String.UTF8.encode("hié", true)));
  // fromCodePoint across the plane boundaries.
  const cpts: i32[] = [0x41, 0x7f, 0x80, 0x7ff, 0x800, 0xd7ff, 0xe000, 0xfffd, 0xffff, 0x10000, 0x1f600, 0x10ffff];
  for (let i = 0; i < cpts.length; i++) {
    let s = String.fromCodePoint(cpts[i]);
    print("cp " + cpts[i].toString(16) + " units " + units(s) + " utf8 " + hexBytes(String.UTF8.encode(s)) +
      " back " + s.codePointAt(0).toString(16));
  }
  // Decoding a byte buffer built by hand, including a truncated tail.
  let raw = new Uint8Array(12);
  const rb: u8[] = [0x48, 0xc3, 0xa9, 0xe2, 0x82, 0xac, 0xf0, 0x9f, 0x98, 0x80, 0x21, 0xe2];
  for (let i = 0; i < 12; i++) raw[i] = rb[i];
  let dec = String.UTF8.decodeUnsafe(raw.dataStart, 11);
  print("decode " + units(dec) + " len=" + dec.length.toString());
  let dec2 = String.UTF8.decodeUnsafe(raw.dataStart, 12);
  print("decode-trunc " + units(dec2) + " len=" + dec2.length.toString());
}
run();
