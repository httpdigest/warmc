// `HashMap` with the default hasher: SipHash-1-3 over the key, then a
// SIMD-shaped probe of a control byte array.
//
// The counterpart of gobench/maphash and asbench/mapint, and the one the
// optimisation work keeps coming back to -- mapint is one of the three kernels
// still above wasmtime, and the measured reason there is not instruction count
// but spill placement: 91 frame accesses inside loops against Cranelift's 51 in
// total, including a reload of the loop-invariant hash key on every probe
// iteration.  Rust's map is a different implementation of the same shape
// (hashbrown rather than Go's buckets), so if that diagnosis is about the shape
// rather than about Go's map, it should show up in this row too.
//
// SipHash is a long dependency chain of rotates and adds with almost no
// instruction-level parallelism, which makes this kernel latency-bound in a way
// most of the corpus is not.

use std::collections::HashMap;

#[inline(never)]
fn build(n: u32) -> HashMap<u32, u64> {
    let mut m = HashMap::new();
    for i in 0..n {
        m.insert(i.wrapping_mul(2654435761), i as u64);
    }
    m
}

#[inline(never)]
fn probe(m: &HashMap<u32, u64>, n: u32) -> u64 {
    let mut s: u64 = 0;
    for i in 0..n {
        // Half of these hit and half miss, so the probe loop's exit branch is
        // not predictable.
        if let Some(&v) = m.get(&i.wrapping_mul(2654435761)) {
            s = s.wrapping_add(v);
        } else {
            s = s.wrapping_add(1);
        }
    }
    s
}

fn main() {
    let m = build(std::hint::black_box(2000));
    let mut s: u64 = 0;
    for _ in 0..10000u32 {
        s = s.wrapping_add(probe(std::hint::black_box(&m),
                                 std::hint::black_box(4000)));
    }
    println!("s = {}", s);
}
