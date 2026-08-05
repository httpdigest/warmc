// Closures both ways: monomorphised through a generic parameter, and boxed
// behind a `dyn` so the call is indirect.
//
// The pair is the point.  `apply_static` is specialised per closure and the body
// is inlined into the loop, so it should compile to the arithmetic and nothing
// else -- if warmc's output for it is longer than the arithmetic, that is
// warmc's. `apply_dyn` cannot be specialised, so it keeps a call through a
// function pointer plus a captured-environment load, which is the cost of
// dynamic dispatch on a closure rather than on a trait object.
//
// gobench/closures found that closures were *not* where TinyGo's indirect-call
// cost lay; this asks the same of Rust, where the answer should differ because
// the static case really is compiled away.

#[inline(never)]
fn apply_static<F: FnMut(u64) -> u64>(n: u32, seed: u64, mut f: F) -> u64 {
    let mut x = seed;
    for _ in 0..n {
        x = f(x);
    }
    x
}

#[inline(never)]
fn apply_dyn(n: u32, seed: u64, f: &mut dyn FnMut(u64) -> u64) -> u64 {
    let mut x = seed;
    for _ in 0..n {
        x = f(x);
    }
    x
}

fn main() {
    let mut s: u64 = 0;
    for r in 0..500000u32 {
        // A captured variable, so the closure has an environment rather than
        // being a bare function in disguise.
        let k = (r as u64) | 1;
        let mut hits = 0u64;
        s = s.wrapping_add(apply_static(200, r as u64, |x| {
            hits += 1;
            x.wrapping_mul(k) ^ (x >> 7)
        }));
        s = s.wrapping_add(hits);
        let mut f = |x: u64| x.wrapping_mul(k) ^ (x >> 7);
        s = s.wrapping_add(apply_dyn(200, r as u64, &mut f));
    }
    println!("s = {}", s);
}
