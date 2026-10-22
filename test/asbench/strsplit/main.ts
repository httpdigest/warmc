// String#split, Array<string>#join, String#replaceAll, substring, indexOf,
// lastIndexOf and startsWith over a comma-separated line, a different suffix
// of it each round.  The big ones are library functions compiled into the
// module and left out of line: split finds each separator with indexOf, copies
// the piece out with memory.copy and appends it through Array#push; join
// measures and then copies; replaceAll searches with indexOf and grows its
// output with __renew.  Every result is a new managed string or array, so
// each round allocates dozens of short objects and drops them.
//
// This is the text-processing shape -- many short strings made and thrown
// away -- so the time is split between UTF-16 scanning loops, memory.copy of a
// few bytes at a time, and the TLSF allocator and incremental collector.
// strbuild only concatenates; this is what parsing code actually calls.
import { print } from "../wasi";

let line = "";
for (let i = 0; i < 40; i++) {
  line += "field" + (i * 37 % 101).toString() + ",";
  if ((i & 3) == 0) line += ",";
  line += "k=v;x" + (i & 7).toString() + ",";
}

let total = 0;
for (let r = 0; r < 12000; r++) {
  // A different suffix of the line each round, so nothing here is invariant.
  let src = line.substring(r & 63);
  let parts = src.split(",");
  let n = parts.length;
  for (let i = 0; i < n; i++) total += parts[i].length;
  let joined = parts.join("|");
  let rep = joined.replaceAll("=", "==").replaceAll(";x", "");
  total += rep.length + rep.charCodeAt(r % rep.length);
  let at = rep.indexOf("field" + (r % 101).toString());
  total += at;
  let mid = rep.substring(r % 50, r % 50 + 24);
  total += mid.lastIndexOf("|") + (mid.startsWith("f") ? 1 : 0);
}
print("total = " + total.toString());
