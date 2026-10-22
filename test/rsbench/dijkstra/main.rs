// `BinaryHeap` as Dijkstra's priority queue: shortest paths from forty sources
// over a generated 20,000-node graph, a ring plus four random edges per node,
// with stale entries skipped on pop rather than decreased in place.
//
// `BinaryHeap` is a max-heap, so the queue holds `Reverse<(u64, u32)>` and every
// comparison is a tuple compared lexicographically and then flipped.  LLVM
// inlines push with its sift-up and pop with its sift-down into the search loop,
// and the tuple compare becomes an `i64` compare with the `i32` tie-break chosen
// by a `select`, so the calls left in `dijkstra` are the heap's `grow_one` and
// the panic paths.  The graph is two flat vectors in CSR form, so relaxation is
// plain loads and compares.
//
// The third Dijkstra in the suite, beside stdgo/dijkstra (container/heap, every
// sift step an interface call) and cppbench/dijkstra (std::priority_queue).
// This one has no indirection left at all, so what the row measures is the sift
// loops' data-dependent branches and a 20,000-entry distance array read in an
// order the heap decides.

use std::cmp::Reverse;
use std::collections::BinaryHeap;

struct Graph {
    start: Vec<u32>,
    edges: Vec<(u32, u32)>,
}

#[inline(never)]
fn make_graph(n: usize, deg: usize, seed: u64) -> Graph {
    let mut x = seed;
    let mut next = || {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        x
    };
    let mut start = Vec::with_capacity(n + 1);
    let mut edges = Vec::with_capacity(n * deg);
    for u in 0..n {
        start.push(edges.len() as u32);
        // A ring edge so every node is reachable, then random ones.
        edges.push((((u + 1) % n) as u32, 1000));
        for _ in 1..deg {
            let r = next();
            let v = (r % n as u64) as u32;
            let w = ((r >> 40) % 997 + 1) as u32;
            edges.push((v, w));
        }
    }
    start.push(edges.len() as u32);
    Graph { start, edges }
}

#[inline(never)]
fn dijkstra(g: &Graph, src: u32, dist: &mut [u64], heap: &mut BinaryHeap<Reverse<(u64, u32)>>) -> u64 {
    for d in dist.iter_mut() {
        *d = u64::MAX;
    }
    heap.clear();
    dist[src as usize] = 0;
    heap.push(Reverse((0, src)));
    let mut pops = 0u64;
    while let Some(Reverse((d, u))) = heap.pop() {
        pops += 1;
        if d > dist[u as usize] {
            continue;
        }
        let (a, b) = (g.start[u as usize] as usize, g.start[u as usize + 1] as usize);
        for &(v, w) in &g.edges[a..b] {
            let nd = d + w as u64;
            if nd < dist[v as usize] {
                dist[v as usize] = nd;
                heap.push(Reverse((nd, v)));
            }
        }
    }
    let mut s = pops;
    for &d in dist.iter() {
        s = s.wrapping_mul(31).wrapping_add(d);
    }
    s
}

fn main() {
    let n = std::hint::black_box(20000usize);
    let g = make_graph(n, 5, 0x9e3779b97f4a7c15);
    let mut dist = vec![0u64; n];
    let mut heap = BinaryHeap::new();
    let mut s = 0u64;
    for r in 0..40u32 {
        let src = r.wrapping_mul(2654435761) % n as u32;
        s = s.wrapping_add(dijkstra(std::hint::black_box(&g), src, &mut dist, &mut heap));
    }
    println!("s = {}", s);
}
