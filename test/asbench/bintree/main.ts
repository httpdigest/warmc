// Building and walking a tree of class instances: the reference-heavy shape,
// where each node is a heap object with two reference fields and every store
// into one goes through the collector's write barrier.  The gobench kernel of
// the same name measures TinyGo's collector; this one measures
// AssemblyScript's, and they are not the same algorithm.
import { print } from "../wasi";

class Node {
  left: Node | null = null;
  right: Node | null = null;
  constructor(public v: i32) {}
}

function build(depth: i32, v: i32): Node {
  let n = new Node(v);
  if (depth > 0) {
    n.left = build(depth - 1, v * 2 + 1);
    n.right = build(depth - 1, v * 2 + 2);
  }
  return n;
}

function walk(n: Node | null): i32 {
  if (n == null) return 0;
  let t = <Node>n;
  return t.v + walk(t.left) + walk(t.right);
}

let total = 0;
for (let r = 0; r < 400; r++) {
  total += walk(build(12, r)) & 0xffff;
}
print("total = " + total.toString());
