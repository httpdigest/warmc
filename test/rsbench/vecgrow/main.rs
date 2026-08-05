// `Vec::push` up to a size that reallocates several times, so the growth path
// and the copy it does are both in the measurement.
//
// A push is a bounds-ish test against the capacity, a store, and an increment,
// with a call to the slow path on the round where the buffer doubles.  The
// interesting question for warmc is whether the common path stays a test and a
// store -- the slow path is a cold call and should be out of the way, and if it
// is not, the loop pays for the register pressure of a call it almost never
// makes.  That is the same shape as the never-taken bounds-check branch in
// `gather`, reached a different way.

#[inline(never)]
fn fill(n: usize) -> Vec<u64> {
    let mut v = Vec::new();
    for i in 0..n {
        v.push((i as u64).wrapping_mul(2654435761));
    }
    v
}

#[inline(never)]
fn fill_reserved(n: usize) -> Vec<u64> {
    // The same loop with the growth taken out, so the pair says what the
    // reallocation and the capacity doubling actually cost.
    let mut v = Vec::with_capacity(n);
    for i in 0..n {
        v.push((i as u64).wrapping_mul(2654435761));
    }
    v
}

fn main() {
    let mut s: u64 = 0;
    for r in 0..80000u32 {
        let a = fill(std::hint::black_box(2000));
        let b = fill_reserved(std::hint::black_box(2000));
        s = s.wrapping_add(a[r as usize % a.len()]);
        s = s.wrapping_add(b[r as usize % b.len()]);
    }
    println!("s = {}", s);
}
