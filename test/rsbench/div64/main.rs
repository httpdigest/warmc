// 64-bit division by a divisor the compiler cannot see, so it is a real divide.
//
// AArch64 has `udiv` and `sdiv` but no remainder instruction, so `%` is a divide
// followed by a multiply-subtract, and asking for both quotient and remainder of
// the same pair should reuse the one divide rather than issue two.  Whether it
// does is what this kernel is for.
//
// The divisor is `black_box`ed on purpose.  A constant divisor is a different
// transformation entirely -- warmc turns it into a multiply by a magic number,
// which test/magic.py checks against exact arithmetic -- and it would measure
// none of the above.

#[inline(never)]
fn divmod(x: u64, d: u64) -> u64 {
    (x / d) ^ (x % d)
}

#[inline(never)]
fn sdivmod(x: i64, d: i64) -> i64 {
    (x / d) ^ (x % d)
}

fn main() {
    let mut s: u64 = 0;
    let mut t: i64 = 0;
    for r in 0..40000000u32 {
        // Never zero, and varying, so neither divide folds away.
        let d = std::hint::black_box((r as u64 % 1000) + 1);
        let x = (r as u64).wrapping_mul(2654435761);
        s = s.wrapping_add(divmod(x, d));
        // Signed as well: the rounding rule differs and so does the code.
        t = t.wrapping_add(sdivmod(x as i64 | 1, -(d as i64) - 1));
    }
    println!("s = {} t = {}", s, t);
}
