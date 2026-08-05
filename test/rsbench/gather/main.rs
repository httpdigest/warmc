// A bounds check that cannot be hoisted, which is what `sum` turned out not to
// have.
//
// In `sum` the index runs 0..n, so LLVM proves the whole range at once: it
// compares the length against n-1 before the loop, traps there if it must, and
// leaves an unchecked body behind.  That is why `sum`, `sumraw` and `sumiter`
// time the same to four digits -- the check is real, and it executes once per
// call rather than once per element.
//
// Here the index is loaded from another array, so there is no range to prove and
// the compare stays in the body.  This is the kernel where a bounds check has a
// price, and `gatherraw` is the same loop without it, so the pair says what that
// price is.  It is also a scattered read, so it asks warmc's addressing modes to
// fold a load-scaled-by-4 whose index came from a load.

#[inline(never)]
fn gather(a: &[u32], idx: &[u32]) -> u64 {
    let mut s: u64 = 0;
    for i in 0..idx.len() {
        s = s.wrapping_add(a[idx[i] as usize] as u64);
    }
    s
}

fn main() {
    let mut a = vec![0u32; 4096];
    let mut idx = vec![0u32; 4096];
    for i in 0..4096 {
        a[i] = (i as u32).wrapping_mul(2654435761);
        // A stride coprime with 4096 walks every slot in an order the hardware
        // prefetcher does not predict, without needing a random generator whose
        // own cost would land in the measurement.
        idx[i] = ((i * 2053 + 1013) % 4096) as u32;
    }
    let mut s: u64 = 0;
    for r in 0..130000u32 {
        a[(r & 4095) as usize] = r;
        s = s.wrapping_add(gather(std::hint::black_box(&a),
                                  std::hint::black_box(&idx)));
    }
    println!("s = {}", s);
}
