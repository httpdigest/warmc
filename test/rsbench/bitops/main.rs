// The bit intrinsics, each of which is one wasm opcode and should be one
// AArch64 instruction.
//
// `rotate_left` is the one worth watching: wasm has `i64.rotl` and AArch64 has
// only `ror`, so a left rotate is a right rotate by the complement, and getting
// that complement computed once rather than per iteration is warmc's job.
// test/vecbench found a 4.7x on exactly this shape, so this row is where the
// scalar version of it shows up.
//
// `count_ones` is `popcnt`, which on AArch64 is a vector instruction with a
// round trip through an FP register at each end -- the sort of thing that is
// cheap in isolation and expensive on a dependency chain.

#[inline(never)]
fn mix(x: u64, k: u32) -> u64 {
    let a = x.rotate_left(k % 64);
    let b = x.rotate_right((k % 64) + 1);
    let c = x.count_ones() as u64;
    let d = x.leading_zeros() as u64;
    let e = x.trailing_zeros() as u64;
    let f = x.swap_bytes();
    a ^ b ^ (c << 8) ^ (d << 16) ^ (e << 24) ^ f
}

fn main() {
    let mut x: u64 = 0x0123_4567_89ab_cdef;
    let mut s: u64 = 0;
    for r in 0..30000000u32 {
        // The rotate amount varies, so it is not a constant the compiler can
        // fold into a single shift.
        x = mix(x, r);
        // Keeps the chain from settling into a fixed point.
        x = x.wrapping_add(r as u64 | 1);
        s = s.wrapping_add(x);
    }
    println!("s = {}", s);
}
