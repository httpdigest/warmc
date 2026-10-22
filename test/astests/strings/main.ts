// String operations, AssemblyScript's UTF-16 strings in linear memory:
// concatenation (including in a loop, which allocates every time), length,
// charCodeAt/charAt/at/codePointAt, String.fromCharCode, substring with
// swapped and out-of-range arguments, substr and slice with negatives,
// indexOf/lastIndexOf/includes with start positions, startsWith/endsWith,
// split by separators (empty, single char, multi-char, limit), replace and
// replaceAll (including the empty pattern), toUpperCase/toLowerCase,
// trim/trimStart/trimEnd, padStart/padEnd, repeat, comparisons (< and ==
// compare contents, not pointers), localeCompare, template literals, and
// strings as Map keys.  Many of these are memory.copy / memory.compare
// loops over u16 units, the code a byte-oriented miscompile breaks.
import { print } from "../wasi";

function q(s: string): string { return "\"" + s + "\""; }

function run(): void {
  let s = "Hello, AssemblyScript World";
  print("len " + s.length.toString() + " " + q(s));
  let codes = "";
  for (let i = 0; i < s.length; i += 3) codes += s.charCodeAt(i).toString() + s.charAt(i) + " ";
  print("codes " + codes + "at-1=" + s.at(-1) + " at0=" + s.at(0));
  print("substring " + q(s.substring(7, 21)) + q(s.substring(21, 7)) + q(s.substring(-5, 5)) +
    q(s.substring(20)) + q(s.substring(100)));
  print("substr " + q(s.substr(7, 8)) + q(s.substr(-5)) + q(s.substr(-5, 2)) + q(s.substr(3, -1)));
  print("slice " + q(s.slice(7, -6)) + q(s.slice(-11)) + q(s.slice(-3, -1)) + q(s.slice(5, 2)));
  print("indexOf " + s.indexOf("o").toString() + " " + s.indexOf("o", 5).toString() + " " +
    s.indexOf("World").toString() + " " + s.indexOf("world").toString() + " " + s.indexOf("").toString() +
    " last " + s.lastIndexOf("o").toString() + " " + s.lastIndexOf("o", 10).toString() + " " +
    s.lastIndexOf("l").toString() + " inc " + (s.includes("Script") ? "1" : "0") + (s.includes("script") ? "1" : "0"));
  print("starts " + (s.startsWith("Hello") ? "1" : "0") + (s.startsWith("Assembly", 7) ? "1" : "0") +
    (s.startsWith("ello") ? "1" : "0") + " ends " + (s.endsWith("World") ? "1" : "0") +
    (s.endsWith("Script", 21) ? "1" : "0") + (s.endsWith("") ? "1" : "0"));
  print("case " + s.toUpperCase() + " | " + s.toLowerCase());
  print("replace " + s.replace("o", "0") + " | " + s.replaceAll("o", "0") + " | " + s.replaceAll("l", "") +
    " | " + "aaa".replaceAll("a", "aa") + " | " + "abc".replaceAll("", "-") + " | " + "abc".replace("", "^"));
  // split
  let parts = "a,b,,c,".split(",");
  print("split1 " + parts.length.toString() + " " + parts.join("|"));
  parts = "one::two::three".split("::");
  print("split2 " + parts.length.toString() + " " + parts.join("|"));
  parts = "abc".split("");
  print("split3 " + parts.length.toString() + " " + parts.join("|"));
  parts = "x y z w".split(" ", 2);
  print("split4 " + parts.length.toString() + " " + parts.join("|"));
  parts = "no-separator".split(";");
  print("split5 " + parts.length.toString() + " " + parts.join("|"));
  // trim and pad
  let w = "  \t padded \n ";
  print("trim " + q(w.trim()) + q(w.trimStart()) + q(w.trimEnd()));
  print("pad " + q("7".padStart(4, "0")) + q("abc".padEnd(8, "xy")) + q("toolong".padStart(3, "*")) +
    q("5".padStart(6)) + q("ab".repeat(4)) + q("z".repeat(0)));
  // comparison is by content
  let a = "apple", b = "app" + "le", c = "apricot";
  print("cmp " + (a == b ? "eq" : "ne") + " " + (a < c ? "lt" : "ge") + " " + (c < a ? "lt" : "ge") + " " +
    (a <= b ? "le" : "gt") + " " + ("Z" < "a" ? "lt" : "ge") + " " + ("" < "a" ? "lt" : "ge") + " " +
    a.localeCompare(c).toString() + " " + c.localeCompare(a).toString() + " " + a.localeCompare(b).toString());
  // fromCharCode and building strings
  let alpha = "";
  for (let i = 0; i < 26; i++) alpha += String.fromCharCode(65 + ((i * 7) % 26));
  print("from " + alpha + " " + String.fromCharCode(0x48, 0x69) + " " + String.fromCharCodes([0x41, 0x53]));
  // template literals with expressions of every kind
  let n = 42, f = 2.5, flag = true;
  let tl = `n=${n} f=${f} sum=${<f64>n + f} flag=${flag} nested=${`<${n * 2}>`} call=${s.substring(0, 5)}`;
  print("template " + tl);
  // concatenation in a loop, then a reversal and a palindrome check
  let built = "";
  for (let i = 0; i < 50; i++) built += (i % 10).toString();
  let rev = "";
  for (let i = built.length - 1; i >= 0; i--) rev += built.charAt(i);
  print("built " + built + " rev " + rev);
  const pals = ["racecar", "level", "abca", "", "a", "Aa", "noon", "abcdefgfedcba"];
  let pr = "";
  for (let i = 0; i < pals.length; i++) {
    let p = pals[i], ok = true;
    for (let j = 0, k = p.length - 1; j < k; j++, k--) if (p.charCodeAt(j) != p.charCodeAt(k)) { ok = false; break; }
    pr += (ok ? "Y" : "N");
  }
  print("palindromes " + pr);
  // Word frequency with string keys.
  let text = "the cat and the dog and the bird saw the cat and the dog run";
  let words = text.split(" ");
  let freq = new Map<string, i32>();
  for (let i = 0; i < words.length; i++) freq.set(words[i], freq.has(words[i]) ? freq.get(words[i]) + 1 : 1);
  let ks = freq.keys();
  let fr = "";
  for (let i = 0; i < ks.length; i++) fr += ks[i] + "=" + freq.get(ks[i]).toString() + " ";
  print("freq " + fr);
  // Caesar shift and run-length encoding.
  let caesar = "";
  let msg = "Attack at Dawn, 1234!";
  for (let i = 0; i < msg.length; i++) {
    let ch = msg.charCodeAt(i);
    if (ch >= 65 && ch <= 90) ch = 65 + (ch - 65 + 13) % 26;
    else if (ch >= 97 && ch <= 122) ch = 97 + (ch - 97 + 13) % 26;
    caesar += String.fromCharCode(ch);
  }
  print("rot13 " + caesar);
  let rle = "", src = "aaabccddddde" + "f".repeat(12) + "gg";
  for (let i = 0; i < src.length;) {
    let j = i;
    while (j < src.length && src.charCodeAt(j) == src.charCodeAt(i)) j++;
    rle += (j - i).toString() + src.charAt(i);
    i = j;
  }
  print("rle " + rle);
  // Many substrings, compared and hashed.
  let h: u32 = 2166136261;
  for (let i = 0; i < s.length; i++) {
    for (let j = i + 1; j <= s.length; j += 4) {
      let sub = s.substring(i, j);
      for (let k = 0; k < sub.length; k++) { h ^= sub.charCodeAt(k); h *= 16777619; }
    }
  }
  print("subhash " + h.toString());
}
run();
