// `sum` written the way Rust is actually written: an iterator chain.
//
// This is the shape no other corpus here produces.  `iter().map().sum()` is
// three generic types stacked on one another, each with a `next` that returns
// an `Option`; the whole tower is monomorphised and inlined until what reaches
// wasm is a loop over a raw pointer with no `Option` left in it.  Whether that
// arrives as good wasm is the question, and whether warmc then does as well on
// it as on the hand-written index loop in `sum` is the comparison.
//
// A pointer-walking loop rather than an indexed one, which is the interesting
// part: the induction variable is an address compared against an end address,
// so there is no separate index to strength-reduce and no length to check.

#[inline(never)]
fn sum_all(a: &[u32]) -> u64 {
    a.iter().map(|&x| x as u64).sum()
}

fn main() {
    let mut a = vec![0u32; 4096];
    for i in 0..a.len() {
        a[i] = (i as u32).wrapping_mul(2654435761);
    }
    let mut s: u64 = 0;
    for r in 0..150000u32 {
        a[(r & 4095) as usize] = r;
        // Opaque for the same reason as in `sum`: a slice LTO can see the
        // length of is a loop with a constant trip count and no end pointer.
        s = s.wrapping_add(sum_all(std::hint::black_box(&a)));
    }
    println!("s = {}", s);
}
