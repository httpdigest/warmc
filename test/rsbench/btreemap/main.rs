// `BTreeMap`: an ordered map, so a search is a walk down a B-tree with a linear
// scan of up to eleven keys at each node.
//
// The contrast with `mapint` is the point of having both.  A hash lookup is one
// long dependency chain (SipHash) and then a nearly random memory access; a
// B-tree lookup is a short chain and a scan whose branches are data-dependent, at
// each of three or four levels.  So the two rows are bounded by different things,
// and a change that helps one need not help the other -- which is exactly the
// kind of pair that stops a single row being read as a general result.
//
// No other corpus here has an ordered-map kernel, so this shape is new to the
// suite rather than a third copy of one already in it.

use std::collections::BTreeMap;

#[inline(never)]
fn build(n: u32) -> BTreeMap<u32, u64> {
    let mut m = BTreeMap::new();
    for i in 0..n {
        // Inserted in scrambled order so the tree is not built as a sorted
        // run, which is a special case of the insert path.
        m.insert(i.wrapping_mul(2654435761), i as u64);
    }
    m
}

#[inline(never)]
fn probe(m: &BTreeMap<u32, u64>, n: u32) -> u64 {
    let mut s: u64 = 0;
    for i in 0..n {
        if let Some(&v) = m.get(&i.wrapping_mul(2654435761)) {
            s = s.wrapping_add(v);
        } else {
            s = s.wrapping_add(1);
        }
    }
    s
}

#[inline(never)]
fn walk(m: &BTreeMap<u32, u64>) -> u64 {
    // An in-order traversal, which is the operation a hash map cannot do at all
    // and is a pointer chase with no arithmetic to hide the latency behind.
    let mut s: u64 = 0;
    for (k, v) in m.iter() {
        s = s.wrapping_add(*k as u64 ^ *v);
    }
    s
}

fn main() {
    let m = build(std::hint::black_box(4000));
    let mut s: u64 = 0;
    for _ in 0..2000u32 {
        s = s.wrapping_add(probe(std::hint::black_box(&m), std::hint::black_box(8000)));
        s = s.wrapping_add(walk(std::hint::black_box(&m)));
    }
    println!("s = {}", s);
}
