// Managed allocation under the incremental collector (AssemblyScript's ITCSM
// over a TLSF allocator, both compiled into the module).  Binary trees built
// and dropped repeatedly, a linked list that is spliced and unlinked so its
// nodes become garbage while others stay reachable, objects holding arrays
// holding objects, a graph with cycles, strings built and discarded in
// loops, and explicit __collect() calls between phases.  Everything live
// is reached through the shadow stack and through object fields, so a value
// warmc keeps only in a register when the collector runs -- or a write
// barrier it drops -- frees something still in use, and a later checksum
// over the live structure differs.  Heap totals are not printed (they are
// the runtime's business); only what the program can see.
import { print } from "../wasi";

class Tree {
  left: Tree | null = null;
  right: Tree | null = null;
  constructor(public v: i32) {}
}

function build(depth: i32, v: i32): Tree {
  let t = new Tree(v);
  if (depth > 0) {
    t.left = build(depth - 1, v * 2);
    t.right = build(depth - 1, v * 2 + 1);
  }
  return t;
}

function check(t: Tree | null): i64 {
  if (t === null) return 0;
  return <i64>t.v + check(t.left) * 3 - check(t.right);
}

function count(t: Tree | null): i32 {
  return t === null ? 0 : 1 + count(t.left) + count(t.right);
}

class LNode {
  next: LNode | null = null;
  payload: string;
  data: Array<i32>;
  constructor(public id: i32) {
    this.payload = "node-" + id.toString();
    this.data = [id, id * 2, id * 3];
  }
}

class GNode {
  edges: GNode[] = [];
  mark: i32 = 0;
  constructor(public id: i32) {}
}

function reach(start: GNode, stamp: i32): i32 {
  let stack: GNode[] = [start];
  start.mark = stamp;
  let n = 0;
  while (stack.length > 0) {
    let g = stack.pop();
    n++;
    for (let i = 0; i < g.edges.length; i++) {
      let e = g.edges[i];
      if (e.mark != stamp) { e.mark = stamp; stack.push(e); }
    }
  }
  return n;
}

function run(): void {
  // Phase 1: a long-lived tree survives while short-lived ones churn.
  let keep = build(10, 1);
  print("keep count=" + count(keep).toString() + " check=" + check(keep).toString());
  for (let r = 0; r < 60; r++) {
    let tmp = build(4 + (r % 8), r);
    let c = check(tmp);
    if (r % 4 == 0) print("churn " + r.toString() + " nodes=" + count(tmp).toString() + " check=" + c.toString());
    if (r % 25 == 24) __collect();
  }
  print("keep again count=" + count(keep).toString() + " check=" + check(keep).toString());
  // Replace subtrees of the long-lived tree, dropping the old ones.
  let t: Tree | null = keep;
  for (let d = 0; d < 8 && t !== null; d++) {
    t.right = build(3, -d);
    t = t.left;
  }
  __collect();
  print("grafted count=" + count(keep).toString() + " check=" + check(keep).toString());

  // Phase 2: a linked list with unlinking.
  let head = new LNode(0);
  let tail = head;
  for (let i = 1; i < 2000; i++) { let n = new LNode(i); tail.next = n; tail = n; }
  // unlink every node whose id is not a multiple of 3 or 7
  let p: LNode | null = head;
  while (p !== null) {
    let nx = p.next;
    while (nx !== null && nx.id % 3 != 0 && nx.id % 7 != 0) nx = nx.next;
    p.next = nx;
    p = nx;
  }
  __collect();
  let sum: i64 = 0, len = 0, plen = 0;
  p = head;
  while (p !== null) { sum += <i64>p.data[2] - <i64>p.data[1]; plen += p.payload.length; len++; p = p.next; }
  print("list len=" + len.toString() + " sum=" + sum.toString() + " payload chars=" + plen.toString());
  // Grow the payloads in place: old strings become garbage.
  p = head;
  let k = 0;
  while (p !== null) { if (k % 5 == 0) p.payload = p.payload + "/" + p.payload.length.toString(); p.data.push(k); p = p.next; k++; }
  __collect();
  p = head; plen = 0; sum = 0;
  while (p !== null) { plen += p.payload.length; sum += <i64>p.data[p.data.length - 1]; p = p.next; }
  print("list grown payload chars=" + plen.toString() + " lastsum=" + sum.toString());

  // Phase 3: a graph with cycles, edges rewired between collections.
  let nodes: GNode[] = [];
  for (let i = 0; i < 300; i++) nodes.push(new GNode(i));
  for (let i = 0; i < 300; i++) {
    nodes[i].edges.push(nodes[(i * 7 + 1) % 300]);
    if (i % 4 == 0) nodes[i].edges.push(nodes[(i * 13 + 5) % 300]);
  }
  print("graph reach=" + reach(nodes[0], 1).toString());
  // Drop the array's references to all but a few roots; the cycles keep
  // what is reachable alive.
  let roots: GNode[] = [nodes[0], nodes[150]];
  nodes = [];
  __collect();
  print("graph reach after drop=" + reach(roots[0], 2).toString() + " " + reach(roots[1], 3).toString());
  for (let i = 0; i < 20; i++) {
    let extra = new GNode(1000 + i);
    extra.edges.push(roots[i & 1]);
    roots[i & 1].edges.push(extra);
    if (i % 5 == 4) { __collect(); print("graph grow " + i.toString() + " reach=" + reach(roots[0], 10 + i).toString()); }
  }

  // Phase 4: string churn with a running hash of what is kept.
  let kept: string[] = [];
  let h: u32 = 0;
  for (let i = 0; i < 3000; i++) {
    let s = "s" + (i * 2654435761).toString(16) + ":" + i.toString();
    if (i % 100 == 0) kept.push(s);
    for (let j = 0; j < s.length; j++) h = h * 31 + s.charCodeAt(j);
    if (i % 1000 == 999) __collect();
  }
  print("strings kept=" + kept.length.toString() + " h=" + h.toString() + " first=" + kept[0] + " last=" + kept[kept.length - 1]);

  // Phase 5: arrays of arrays, reallocated as they grow.
  let rows = new Array<Array<f64>>();
  for (let i = 0; i < 40; i++) {
    let row = new Array<f64>();
    for (let j = 0; j < i * 3; j++) row.push(<f64>(i * j) * 0.5);
    rows.push(row);
    if (i % 8 == 7) { rows[i >> 1] = new Array<f64>(); __collect(); }
  }
  let tot = 0.0, cells = 0;
  for (let i = 0; i < rows.length; i++) { cells += rows[i].length; for (let j = 0; j < rows[i].length; j++) tot += rows[i][j]; }
  print("rows cells=" + cells.toString() + " total=" + tot.toString());
  print("final keep check=" + check(keep).toString());
}
run();
