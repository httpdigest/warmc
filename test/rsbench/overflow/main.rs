// The three ways Rust does arithmetic that might not fit, each of which becomes
// different wasm.
//
// `wrapping_*` is the bare opcode.  `checked_*` computes the value and a
// did-it-overflow flag and returns an `Option`, which on AArch64 is an `adds`
// and a `cset` -- warmc has to notice that the flag it just set is the one the
// branch wants rather than recomputing the comparison.  `saturating_*` is that
// plus a select.
//
// The comparison worth making is with asbench: AssemblyScript puts *trapping*
// overflow checks in its loop bodies, and those checks were one of the two
// things blocking its kernels from vectorising at all.  Rust's are values rather
// than traps, so they do not have the same ordering constraints -- this row says
// what the cheaper form costs.

#[inline(never)]
fn checked(a: u64, b: u64) -> u64 {
    let mut s = 0u64;
    if let Some(v) = a.checked_add(b) {
        s ^= v;
    }
    if let Some(v) = a.checked_mul(b | 1) {
        s ^= v;
    }
    if let Some(v) = a.checked_sub(b) {
        s ^= v;
    }
    s
}

#[inline(never)]
fn saturating(a: i64, b: i64) -> i64 {
    a.saturating_add(b) ^ a.saturating_sub(b) ^ a.saturating_mul(b | 1)
}

#[inline(never)]
fn wrapping(a: u64, b: u64) -> u64 {
    a.wrapping_add(b) ^ a.wrapping_sub(b) ^ a.wrapping_mul(b | 1)
}

fn main() {
    let mut s: u64 = 0;
    let mut t: i64 = 0;
    for r in 0..20000000u32 {
        // Large enough that the overflow cases really are taken sometimes, so
        // the branches are not uniformly one way.
        let a = (r as u64).wrapping_mul(0x9e37_79b9_7f4a_7c15);
        let b = (r as u64) << 40 | 1;
        s = s.wrapping_add(checked(a, b));
        s = s.wrapping_add(wrapping(a, b));
        t = t.wrapping_add(saturating(a as i64, b as i64));
    }
    println!("s = {} t = {}", s, t);
}
