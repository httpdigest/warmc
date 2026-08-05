// Bulk copies, at the sizes where the decision about how to copy changes.
//
// Rust does not use `memory.copy` for this.  Even with `+bulk-memory` enabled
// the copy comes out as a byte-and-word loop from `compiler_builtins`, compiled
// into the module -- so unlike gobench/memcpy, which hands warmc a
// `memory.copy` and measures warmc's runtime helper, this kernel measures
// warmc's ordinary codegen over somebody else's copy loop.  The two rows
// answer different questions and both are worth having.
//
// The sizes are chosen around the thresholds: a few bytes, a word, an unaligned
// tail, and past the point where a copy loop's per-iteration cost stops
// mattering next to its start-up.

#[inline(never)]
fn copy(dst: &mut [u8], src: &[u8], n: usize) {
    dst[..n].copy_from_slice(&src[..n]);
}

fn main() {
    let src = vec![7u8; 8192];
    let mut dst = vec![0u8; 8192];
    // Cycled rather than run one size at a time, so no single size gets the
    // branch predictor to itself and the row is an average over the mix.
    let sizes = [15usize, 64, 255, 1024, 4096];
    let mut s: u64 = 0;
    for r in 0..10000000u32 {
        let n = sizes[(r % 5) as usize];
        copy(std::hint::black_box(&mut dst), std::hint::black_box(&src), n);
        // Reads a byte back so the copy is observed and cannot be dropped.
        s = s.wrapping_add(dst[n - 1] as u64);
    }
    println!("s = {}", s);
}
