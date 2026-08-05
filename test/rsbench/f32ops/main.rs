// Single-precision arithmetic, which wasm keeps distinct from double all the way
// down and which AArch64 has its own register width for.
//
// Worth a row of its own because f32 is where a compiler is most tempted to be
// wrong: every operation must round to single precision, so computing in double
// and rounding once at the end gives different answers, and the wasm spec does
// not allow it.  The differential against wasmtime is the check -- both engines
// run the same module, so a warmc that widened somewhere would disagree in the
// last bits and the printed bit pattern would say so.
//
// It is also the shape with the most to gain from vectorising: four f32 lanes to
// a v128 against two for f64, so the same loop is worth twice as much here.

#[inline(never)]
fn combine(a: &[f32], b: &[f32], out: &mut [f32]) {
    for i in 0..out.len() {
        // A mix of the operations that have distinct AArch64 forms, all of them
        // single-precision.
        let x = a[i];
        let y = b[i];
        out[i] = (x * y + x).sqrt() + (x - y).abs() + x.min(y) * x.max(y);
    }
}

#[inline(never)]
fn reduce(a: &[f32]) -> f32 {
    // A single-precision reduction: the accumulator must round at every step,
    // so this cannot be reassociated and cannot be widened.
    let mut s = 0.0f32;
    for &x in a {
        s = s * 1.0000001 + x;
    }
    s
}

fn main() {
    let n = 2048;
    let mut a = vec![0f32; n];
    let mut b = vec![0f32; n];
    let mut out = vec![0f32; n];
    for i in 0..n {
        a[i] = (i % 97) as f32 * 0.125 + 1.0;
        b[i] = (i % 53) as f32 * 0.25 + 1.0;
    }
    let mut s = 0.0f32;
    for r in 0..34000u32 {
        a[(r as usize) % n] = (r % 1000) as f32 + 1.0;
        combine(std::hint::black_box(&a), std::hint::black_box(&b),
                std::hint::black_box(&mut out));
        // Reduced mod something each round so the accumulator stays finite:
        // an f32 that reaches infinity makes every later round measure
        // arithmetic on infinity instead of on numbers.
        s = (s + reduce(std::hint::black_box(&out))) % 1.0e9;
    }
    println!("s = {}", s.to_bits());
}
