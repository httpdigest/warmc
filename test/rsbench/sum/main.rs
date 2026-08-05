// A widening reduction: u32 elements into a u64 accumulator, reached by index,
// so the bounds check LLVM could not prove away is in the loop body.
//
// Three kernels share this arithmetic and differ only in how they reach the
// elements -- `sum` indexes, `sumraw` uses `get_unchecked`, `sumiter` goes
// through an iterator chain.  The gaps between those three rows are what a
// bounds check and what Rust's iterator abstraction come to after LLVM has had
// its go.  And against gobench/sum and asbench/sum, whose arithmetic is
// identical to this one's, the gap is what three producers over the same
// arithmetic come to.
//
// Two things here are load-bearing rather than decoration.  `#[inline(never)]`
// keeps the reduction a call, and the store into `a` each round keeps it from
// being loop-invariant; without either, LLVM hoists the whole thing out of the
// round loop and the kernel measures process start-up.

// The count is a parameter rather than `a.len()`, and that is the whole reason
// this kernel differs from `sumraw`.  Written as `for i in 0..a.len()` LLVM
// proves every index in range and deletes the check, so all three of these
// kernels compiled to the same code and timed the same to three digits.  Across
// the `#[inline(never)]` boundary `n <= a.len()` is not provable, so the check
// stays -- which is also the honest case: a slice and a count that travelled
// separately is how the check survives in real Rust.
#[inline(never)]
fn sum_all(a: &[u32], n: usize) -> u64 {
    let mut s: u64 = 0;
    for i in 0..n {
        s += a[i] as u64;
    }
    s
}

fn main() {
    let mut a = vec![0u32; 4096];
    for i in 0..a.len() {
        a[i] = (i as u32).wrapping_mul(2654435761);
    }
    // `black_box` because `#[inline(never)]` is not enough on its own.  Fat LTO
    // saw that the only call passes this Vec and its constant length, and
    // specialised `sum_all` down to one argument with the count folded in and
    // the check gone -- all three of `sum`, `sumraw` and `sumiter` compiled to
    // the same function and timed the same to four digits.  An opaque length is
    // what keeps the count a count.
    let n = std::hint::black_box(a.len());
    let mut s: u64 = 0;
    for r in 0..150000u32 {
        a[(r & 4095) as usize] = r;
        s = s.wrapping_add(sum_all(std::hint::black_box(&a), n));
    }
    println!("s = {}", s);
}
