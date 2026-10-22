// u128 and i128: a Miller-Rabin primality test over 64-bit odd numbers, whose
// modular multiply is a 64x64->128 product reduced by a 128-bit remainder, and
// a signed dot product of i64 pairs summed exactly in an i128.
//
// The same program as zigbench/wide128, from a different front end over the same
// LLVM.  wasm has no 128-bit type and no high-half multiply, so every `mulmod`
// is a call to `__multi3` and then one to `__umodti3`, and every term of the dot
// product is a `__multi3` -- Rust's own compiler-builtins this time rather than
// compiler-rt, compiled into the module.  Those, and the `__divti3` in main, take
// a pointer to a 16-byte stack slot as their first argument and return through
// it, so every 128-bit result makes a round trip through memory; the i128 sum
// in between is two i64 adds with the carry recovered by a compare.  The trial
// division by the twelve small primes is unrolled into twelve `i64.rem_u` by
// constants.
//
// Small leaf calls that return through memory, in a hot loop, are a different
// thing to optimise from anything else in this corpus, and the pair with
// zigbench says whether two builds of those helpers cost warmc the same.

#[inline(always)]
fn mulmod(a: u64, b: u64, m: u64) -> u64 {
    (a as u128 * b as u128 % m as u128) as u64
}

fn powmod(mut b: u64, mut e: u64, m: u64) -> u64 {
    let mut r = 1u64;
    b %= m;
    while e > 0 {
        if e & 1 == 1 {
            r = mulmod(r, b, m);
        }
        b = mulmod(b, b, m);
        e >>= 1;
    }
    r
}

// Deterministic Miller-Rabin for every 64-bit n with these seven bases.
#[inline(never)]
fn is_prime(n: u64) -> bool {
    if n < 2 {
        return false;
    }
    for p in [2u64, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37] {
        if n % p == 0 {
            return n == p;
        }
    }
    let s = (n - 1).trailing_zeros();
    let d = (n - 1) >> s;
    'base: for a in [2u64, 325, 9375, 28178, 450775, 9780504, 1795265022] {
        let a = a % n;
        if a == 0 {
            continue;
        }
        let mut x = powmod(a, d, n);
        if x == 1 || x == n - 1 {
            continue;
        }
        for _ in 1..s {
            x = mulmod(x, x, n);
            if x == n - 1 {
                continue 'base;
            }
        }
        return false;
    }
    true
}

#[inline(never)]
fn dot_i128(a: &[i64], b: &[i64]) -> i128 {
    // Exact: a product of two i64 is an i128, and so is a sum of a few thousand
    // of them, so nothing here wraps.
    let mut s: i128 = 0;
    for (x, y) in a.iter().zip(b) {
        s += *x as i128 * *y as i128;
    }
    s
}

fn main() {
    let base = std::hint::black_box(0xffff_ffff_0000_0001u64 >> 1);
    let mut primes = 0u64;
    let mut last = 0u64;
    for i in 0..48000u64 {
        let n = (base.wrapping_add(i.wrapping_mul(2654435761) << 1)) | 1;
        if is_prime(n) {
            primes += 1;
            last ^= n;
        }
    }
    let mut x = 0x9e3779b97f4a7c15u64;
    let mut a = Vec::with_capacity(1024);
    let mut b = Vec::with_capacity(1024);
    for _ in 0..1024 {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        a.push(x as i64);
        b.push((x.rotate_left(29)) as i64);
    }
    let mut acc: i128 = 0;
    for r in 0..8000u32 {
        b[r as usize & 1023] = r as i64 - 1000;
        let d = dot_i128(std::hint::black_box(&a), std::hint::black_box(&b));
        acc = acc.wrapping_add(d / 1_000_003);
    }
    println!("primes = {} last = {} acc = {}", primes, last, acc);
}
