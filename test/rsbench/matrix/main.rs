// A 64x64 f64 matrix multiply: three nested loops over flat `Vec<f64>`.
//
// The innermost loop carries one accumulator and strides one operand by the row
// length, which is the shape that asks the register allocator to keep an f64
// live across a loop and asks the addressing modes to fold a scaled index.  It
// is also the shape `pass_loop_close` was written for, so the row is worth
// watching when that pass changes.
//
// Indexed rather than iterator-based on purpose: the strided access does not
// fall out of an iterator, and writing it with `chunks` would measure a
// different thing.  `chunks` gets its own kernel.

const N: usize = 64;

#[inline(never)]
fn matmul(a: &[f64], b: &[f64], c: &mut [f64]) {
    for i in 0..N {
        for j in 0..N {
            let mut s = 0.0f64;
            for k in 0..N {
                s += a[i * N + k] * b[k * N + j];
            }
            c[i * N + j] = s;
        }
    }
}

fn main() {
    let mut a = vec![0.0f64; N * N];
    let mut b = vec![0.0f64; N * N];
    let mut c = vec![0.0f64; N * N];
    for i in 0..N * N {
        a[i] = (i % 17) as f64 * 0.5;
        b[i] = (i % 23) as f64 * 0.25;
    }
    let mut s = 0.0f64;
    for r in 0..3000 {
        // Perturbs one input per round so the multiply is not loop-invariant.
        a[r % (N * N)] = r as f64;
        matmul(&a, &b, &mut c);
        s += c[r % (N * N)];
    }
    println!("s = {}", s.to_bits());
}
