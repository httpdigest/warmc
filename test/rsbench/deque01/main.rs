// `VecDeque` used from both ends: a 0-1 breadth-first search over a grid whose
// cells cost nothing or one to enter, which pushes a free step on the front and
// a costly one on the back, so the deque stays sorted by distance without a
// heap.
//
// The same search as cppbench/deque, over a different container.  libc++'s deque
// is a map of blocks; Rust's is one ring buffer, so every push and pop computes a
// physical index that wraps, and std writes that wrap as a compare and a `select`
// that subtracts the capacity rather than as a remainder.  The deque starts at a
// capacity of eight, so the growth path -- a call to `grow` -- runs on the first
// search, and from then on head and tail chase each other round the buffer.
// LLVM did not inline the relaxation closure: it is a function of its own, called
// at four sites per popped cell, with the push_front and push_back inside it.
// The cell's column, `u % 192`, reaches wasm as an `i32.rem_u` by a constant,
// which leaves the magic-number division to warmc.
//
// A search frontier is the use real programs make of a deque, and the front and
// back pushes here interleave in an order the grid decides.

use std::collections::VecDeque;

const W: usize = 192;
const H: usize = 192;

#[inline(never)]
fn make_grid(seed: u64) -> Vec<u8> {
    // 0 = road (free to enter), 1 = field (costs one), 2 = wall.
    let mut g = vec![0u8; W * H];
    let mut x = seed;
    for c in g.iter_mut() {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        let r = (x >> 32) % 10;
        *c = if r < 3 { 0 } else if r < 8 { 1 } else { 2 };
    }
    g
}

#[inline(never)]
fn bfs01(g: &[u8], src: usize, dist: &mut [u32], q: &mut VecDeque<u32>) -> u64 {
    for d in dist.iter_mut() {
        *d = u32::MAX;
    }
    q.clear();
    dist[src] = 0;
    q.push_back(src as u32);
    while let Some(u) = q.pop_front() {
        let u = u as usize;
        let du = dist[u];
        let (ux, uy) = (u % W, u / W);
        let mut relax = |v: usize| {
            let c = g[v];
            if c == 2 {
                return;
            }
            let nd = du + c as u32;
            if nd < dist[v] {
                dist[v] = nd;
                // 0-1 BFS: a free edge goes to the front, a costly one to the
                // back, so the deque stays sorted by distance.
                if c == 0 {
                    q.push_front(v as u32);
                } else {
                    q.push_back(v as u32);
                }
            }
        };
        if ux > 0 {
            relax(u - 1);
        }
        if ux + 1 < W {
            relax(u + 1);
        }
        if uy > 0 {
            relax(u - W);
        }
        if uy + 1 < H {
            relax(u + W);
        }
    }
    let mut s = 0u64;
    for &d in dist.iter() {
        if d != u32::MAX {
            s = s.wrapping_add(d as u64);
        } else {
            s = s.wrapping_add(7);
        }
    }
    s
}

fn main() {
    let g = make_grid(std::hint::black_box(0x2545f4914f6cdd1d));
    let mut dist = vec![0u32; W * H];
    // Deliberately small, so the ring is full and wraps long before the search
    // is done, and grows several times on the first search.
    let mut q: VecDeque<u32> = VecDeque::with_capacity(8);
    let mut s = 0u64;
    for r in 0..130usize {
        let src = (r.wrapping_mul(2654435761) >> 3) % (W * H);
        s = s.wrapping_add(bfs01(std::hint::black_box(&g), src, &mut dist, &mut q));
    }
    println!("s = {}", s);
}
