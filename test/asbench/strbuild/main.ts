// Building strings by concatenation.  Every `+` allocates a new string and
// copies both operands, so this kernel is the allocator, memory.copy and the
// garbage collector rather than any loop of its own -- which is what makes it
// the one that says whether warmc's bulk-memory lowering and AssemblyScript's
// TLSF heap get along.
import { print } from "../wasi";

let total = 0;
for (let r = 0; r < 11000; r++) {
  let s = "";
  for (let i = 0; i < 200; i++) s += (i & 15).toString();
  total += s.length + s.charCodeAt(r % s.length);
}
print("total = " + total.toString());
