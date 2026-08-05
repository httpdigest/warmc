// `sum` with the bounds check taken out by hand, and nothing else changed.
//
// The pair exists so the bounds check has a price rather than a reputation.
// Rust's is not AssemblyScript's: a failed check here branches to a panic that
// `-C panic=abort` has already reduced to a trap, so what the check costs is a
// compare and a never-taken branch per element, and the question is whether
// warmc's block layout puts that branch's cold side out of the way.
//
// See `sum` for why the store into `a` and the `#[inline(never)]` are needed.

// Same signature as `sum`'s, count and all, so the two differ by exactly the
// check and not by what LLVM can prove about the loop bound.
#[inline(never)]
fn sum_all(a: &[u32], n: usize) -> u64 {
    let mut s: u64 = 0;
    for i in 0..n {
        // Sound because `main` passes `a.len()`, which is what `get_unchecked`
        // asks the reader to verify and the compiler then takes on trust.
        s += unsafe { *a.get_unchecked(i) } as u64;
    }
    s
}

fn main() {
    let mut a = vec![0u32; 4096];
    for i in 0..a.len() {
        a[i] = (i as u32).wrapping_mul(2654435761);
    }
    // See `sum` for why these are opaque: without it LTO folds the length in
    // and this kernel and that one become the same code.
    let n = std::hint::black_box(a.len());
    let mut s: u64 = 0;
    for r in 0..150000u32 {
        a[(r & 4095) as usize] = r;
        s = s.wrapping_add(sum_all(std::hint::black_box(&a), n));
    }
    println!("s = {}", s);
}
