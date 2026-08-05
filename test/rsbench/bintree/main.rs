// A pointer-linked tree built and torn down repeatedly: allocation traffic with
// no garbage collector behind it.
//
// This is the kernel where Rust differs most from the rest of the corpus.
// gobench/bintree hands warmc TinyGo's collector and asbench/bintree hands it
// AssemblyScript's incremental one, and in both of those the allocation is a
// bump and the *collector* is the cost -- shadow-stack stores in every loop
// body, write barriers, mark work.  Rust has none of that: `Box` is dlmalloc
// compiled into the module and `drop` is a free at a point the compiler chose.
// So this row measures an allocator's own code rather than a collector's, and
// the difference between the three rows is the difference between three memory
// managers, all of them inside the module.

struct Node {
    l: Option<Box<Node>>,
    r: Option<Box<Node>>,
    v: u64,
}

#[inline(never)]
fn build(depth: u32, v: u64) -> Box<Node> {
    if depth == 0 {
        Box::new(Node { l: None, r: None, v })
    } else {
        Box::new(Node {
            l: Some(build(depth - 1, v * 2)),
            r: Some(build(depth - 1, v * 2 + 1)),
            v,
        })
    }
}

#[inline(never)]
fn total(n: &Node) -> u64 {
    let mut s = n.v;
    if let Some(l) = &n.l {
        s = s.wrapping_add(total(l));
    }
    if let Some(r) = &n.r {
        s = s.wrapping_add(total(r));
    }
    s
}

fn main() {
    let mut s: u64 = 0;
    for r in 0..3000u32 {
        // Depth 11 is 4095 nodes, so each round allocates and frees that many.
        let t = build(std::hint::black_box(11), r as u64);
        s = s.wrapping_add(total(&t));
        // `t` is dropped here, which is the whole tree freed one node at a
        // time -- the traversal a collector would not have to do.
    }
    println!("s = {}", s);
}
