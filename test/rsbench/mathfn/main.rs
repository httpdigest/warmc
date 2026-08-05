// The transcendentals, which on wasm are library code rather than opcodes.
//
// `sqrt` is an opcode and the rest are not: `sin`, `exp`, `ln` and `powf` come
// from wasi-libc, compiled into the module, so this kernel measures warmc on
// somebody else's carefully written scalar floating-point code -- long
// polynomial evaluations with a table lookup and a lot of bit-twiddling of
// doubles through integer registers.
//
// It is the third answer to a question the other corpora have already asked
// twice: gobench/mathfn is TinyGo over Go's libm, asbench/mathfn is
// AssemblyScript over its own, and this is LLVM over wasi-libc's.  A cost that
// shows up in all three is warmc's; one that shows up in a single row is that
// libm's.
//
// The result is printed as bits, not digits.  Rust's float formatting is
// shortest-round-trip, so digits would in fact distinguish a one-ulp
// disagreement, but bits say so without asking anyone to trust that -- and they
// keep the formatting machinery out of the module.

#[inline(never)]
fn kernel(x: f64) -> f64 {
    x.sqrt() + x.sin() + (-x).exp() + (x + 1.0).ln() + x.powf(1.5)
}

fn main() {
    let mut s = 0.0f64;
    // Runs 0.0 up to just under 4.0, which keeps every one of these in the
    // range where it does real work: no subnormal, no infinity, no NaN, and
    // nothing that any of them special-cases on the way in.
    for i in 0..10000000u32 {
        s += kernel((i % 4000) as f64 * 0.001);
    }
    println!("s = {}", s.to_bits());
}
