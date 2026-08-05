// `chunks_exact(4)`, which is how Rust is written when the author wants the
// vectoriser to succeed.
//
// The iterator yields a slice of exactly four elements whose length the compiler
// knows, so the bounds checks inside the body are gone before wasm and the loop
// body is four independent lanes of the same arithmetic.  This is the most
// vectorisable shape in the corpus, and it is here because of a measured fact
// about the other producers: AssemblyScript's five vectorisable kernels are all
// unreachable to warmc's vectoriser, blocked by shadow-stack stores and trapping
// overflow checks, and TinyGo's loops needed rotating before they were visible
// at all.  Rust with no collector emits none of that, so this row asks whether
// the vectoriser reaches a producer's real output when nothing is in the way.
//
// Kept scalar in the wasm: the corpus does not enable simd128, so LLVM hands
// warmc four lanes written out and the question is whether warmc puts them back
// together.

#[inline(never)]
fn fold4(a: &[u32]) -> u64 {
    let mut acc = [0u64; 4];
    for c in a.chunks_exact(4) {
        acc[0] = acc[0].wrapping_add(c[0] as u64);
        acc[1] = acc[1].wrapping_add(c[1] as u64);
        acc[2] = acc[2].wrapping_add(c[2] as u64);
        acc[3] = acc[3].wrapping_add(c[3] as u64);
    }
    acc[0] ^ acc[1].rotate_left(16) ^ acc[2].rotate_left(32) ^ acc[3].rotate_left(48)
}

#[inline(never)]
fn scale_add(dst: &mut [f32], src: &[f32]) {
    // Two slices of equal length walked together, which is the other shape a
    // vectoriser wants: no reduction, just lanes in and lanes out.
    for (d, s) in dst.iter_mut().zip(src.iter()) {
        *d = *d * 1.5 + *s;
    }
}

fn main() {
    let mut a = vec![0u32; 4096];
    let mut f = vec![0f32; 4096];
    let g = vec![1.25f32; 4096];
    for i in 0..4096 {
        a[i] = (i as u32).wrapping_mul(2654435761);
        f[i] = (i % 100) as f32;
    }
    let mut s: u64 = 0;
    for r in 0..70000u32 {
        a[(r & 4095) as usize] = r;
        s = s.wrapping_add(fold4(std::hint::black_box(&a)));
        scale_add(std::hint::black_box(&mut f), std::hint::black_box(&g));
        // Rescaled so the f32 accumulator cannot run off to infinity, which
        // would make every later round measure arithmetic on inf.
        if r % 64 == 0 {
            for x in f.iter_mut() {
                *x = *x % 1000.0;
            }
        }
    }
    println!("s = {} f = {}", s, f[7].to_bits());
}
