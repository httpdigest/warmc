// Const generics: `Mat<N>` wraps `[[f64; N]; N]` and is passed and returned by
// value, and one generic `chain::<N>` -- a running product of plane rotations,
// applied to a vector every fourth step -- is instantiated for N = 3, 4 and 6.
// A slice pattern summarises each result.
//
// With N a constant, every loop in `mul` and `apply` has a known trip count, and
// what LLVM does with that depends on N.  For 3 and 4 it unrolls the product
// completely and scalar-replaces the accumulating matrix, so the loop of
// `chain::<3>` is 36 `f64.mul`s on wasm locals whose only memory traffic is the
// nine loads of the rotation being multiplied in.  For 6 it keeps a loop over
// rows and leaves the matrices in linear memory.  So one source gives both
// extremes of what an array by value can become, side by side.  `summarise`
// matches `[first, mid @ .., last]` and recurses on the middle, which is a
// `br_table` on the slice's length.
//
// matrix is the 64x64 case through a flat Vec, whose cost is loops and strided
// loads; these are the small fixed-size matrices of graphics and physics code,
// whose cost is straight-line floating point and register pressure.

#[derive(Clone, Copy)]
struct Mat<const N: usize>([[f64; N]; N]);

impl<const N: usize> Mat<N> {
    fn identity() -> Self {
        let mut m = [[0.0; N]; N];
        for i in 0..N {
            m[i][i] = 1.0;
        }
        Mat(m)
    }
    fn mul(self, o: Self) -> Self {
        let mut r = [[0.0; N]; N];
        for i in 0..N {
            for j in 0..N {
                let mut s = 0.0;
                for k in 0..N {
                    s += self.0[i][k] * o.0[k][j];
                }
                r[i][j] = s;
            }
        }
        Mat(r)
    }
    fn apply(&self, v: [f64; N]) -> [f64; N] {
        let mut r = [0.0; N];
        for i in 0..N {
            for k in 0..N {
                r[i] += self.0[i][k] * v[k];
            }
        }
        r
    }
    fn trace(&self) -> f64 {
        (0..N).map(|i| self.0[i][i]).sum()
    }
}

// A rotation by `t` in the plane of axes (p, q), and a small shear, so the
// products stay bounded and every entry is nonzero after a few steps.
fn rot<const N: usize>(p: usize, q: usize, t: f64) -> Mat<N> {
    let mut m = Mat::<N>::identity();
    let (s, c) = t.sin_cos();
    m.0[p][p] = c;
    m.0[q][q] = c;
    m.0[p][q] = -s;
    m.0[q][p] = s;
    m
}

#[inline(never)]
fn chain<const N: usize>(steps: u32, seed: f64) -> (f64, [f64; N]) {
    let mut gens = Vec::new();
    for p in 0..N {
        let q = (p + 1) % N;
        gens.push(rot::<N>(p, q, seed + p as f64 * 0.37));
    }
    let mut acc = Mat::<N>::identity();
    let mut v = [1.0; N];
    for i in 0..steps {
        acc = acc.mul(gens[i as usize % N]);
        if i % 4 == 0 {
            v = acc.apply(v);
        }
    }
    (acc.trace(), v)
}

// Slice patterns over the result vector: the first, last and middle by
// destructuring rather than indexing.
fn summarise(v: &[f64]) -> f64 {
    match v {
        [] => 0.0,
        [x] => *x,
        [a, b] => a - b,
        [first, mid @ .., last] => first * 3.0 + last + summarise(mid) * 0.5,
    }
}

fn main() {
    let steps = std::hint::black_box(60000u32);
    let mut bits = 0u64;
    for r in 0..24u32 {
        let seed = 0.1 + r as f64 * 0.013;
        let (t3, v3) = chain::<3>(steps, seed);
        let (t4, v4) = chain::<4>(steps, seed);
        let (t6, v6) = chain::<6>(steps, seed);
        let x = t3 + t4 + t6 + summarise(&v3) + summarise(&v4) + summarise(&v6);
        bits = bits.rotate_left(9) ^ x.to_bits();
    }
    println!("bits = {}", bits);
}
