// `Option` and `Result` threaded through `?`, which is Rust's error handling and
// therefore in the middle of almost every real function.
//
// Each `?` is a discriminant test and a branch that either continues or returns
// early, so a chain of them is a chain of short forward branches with one
// overwhelmingly likely direction.  That makes this a block-layout kernel: the
// early-return paths should end up out of line, and the fall-through path should
// be straight.  The layout work that fixed gobench/utf8scan is what this row is
// sensitive to, reached from a different producer and a different construct.
//
// A niche-optimised `Option` is the interesting half: `Option<&T>` and
// `Option<NonZeroU64>` have no separate tag, so the test is against the value
// itself and the branch is on a comparison rather than on a loaded byte.

use std::num::NonZeroU64;

#[inline(never)]
fn step(x: u64) -> Option<NonZeroU64> {
    // Niche-optimised: this Option is one word wide with zero as the None.
    NonZeroU64::new(x.wrapping_mul(2654435761) >> 3)
}

#[inline(never)]
fn parse_pair(a: u64, b: u64) -> Result<u64, ()> {
    let x = step(a).ok_or(())?;
    let y = step(b).ok_or(())?;
    let s = x.get().checked_add(y.get()).ok_or(())?;
    if s % 17 == 0 {
        return Err(());
    }
    Ok(s)
}

#[inline(never)]
fn chain(seed: u64) -> u64 {
    let mut acc = seed;
    for i in 0..8u64 {
        // Roughly one in seventeen of these takes the error path, so the branch
        // is predictable but not constant -- which is the honest case.
        acc = match parse_pair(acc, acc ^ i) {
            Ok(v) => v,
            Err(()) => acc.wrapping_add(1),
        };
    }
    acc
}

fn main() {
    let mut s: u64 = 0;
    for r in 0..5700000u32 {
        s = s.wrapping_add(chain(std::hint::black_box(r as u64 | 1)));
    }
    println!("s = {}", s);
}
