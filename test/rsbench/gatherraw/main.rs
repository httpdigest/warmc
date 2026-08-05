// `gather` with the one check that could not be hoisted taken out by hand.
//
// The pair is the honest measurement of what a Rust bounds check costs, because
// the `sum`/`sumraw` pair turned out to measure a check that LLVM had already
// lifted out of the loop.  Here it is a compare and a not-taken branch per
// element, and what warmc decides to do with the never-taken side of it -- keep
// it inline or put it out of the way -- is visible in this row against the last.

#[inline(never)]
fn gather(a: &[u32], idx: &[u32]) -> u64 {
    let mut s: u64 = 0;
    for i in 0..idx.len() {
        // Sound because every element of `idx` is built below as a value mod
        // 4096 and `a` is 4096 long.
        let j = unsafe { *idx.get_unchecked(i) } as usize;
        s = s.wrapping_add(unsafe { *a.get_unchecked(j) } as u64);
    }
    s
}

fn main() {
    let mut a = vec![0u32; 4096];
    let mut idx = vec![0u32; 4096];
    for i in 0..4096 {
        a[i] = (i as u32).wrapping_mul(2654435761);
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
