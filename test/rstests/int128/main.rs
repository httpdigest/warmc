// 128-bit integers: i128 and u128 arithmetic, division, shifts, bit counts,
// formatting and parsing.
//
// wasm has no 128-bit type, so every operation here is LLVM splitting the value
// into two i64 halves: an add is an add with a carry recovered by a compare, a
// multiply is `__multi3` from compiler-builtins (or the inline four-product form
// when one side is known to be small), and division is `__udivti3` /
// `__divti3`, a long loop in the module itself.  Formatting a u128 in decimal is
// a separate routine again (core's `fmt_u128`, which divides by 10^19 using
// 128-bit multiplies).  Those are the pieces a miscompile of carries, of
// `i64.lt_u`, or of a multi-value return through the stack would break.

use std::hint::black_box as bb;

fn mul_table() {
    let vals: [u128; 7] = [
        0,
        1,
        u64::MAX as u128,
        (u64::MAX as u128) + 1,
        0x0123_4567_89ab_cdef_fedc_ba98_7654_3210,
        u128::MAX / 7,
        u128::MAX,
    ];
    for &a in &vals {
        for &b in &vals {
            let (a, b) = (bb(a), bb(b));
            println!(
                "{:x} * {:x}: w {:x} c {:?} o {:?} s {:x}",
                a,
                b,
                a.wrapping_mul(b),
                a.checked_mul(b).map(|v| v % 1_000_000_007),
                a.overflowing_mul(b).1,
                a.saturating_mul(b)
            );
            println!(
                "   + {:x} {:?}  - {:x} {:?}  / {:?}  % {:?}",
                a.wrapping_add(b),
                a.overflowing_add(b).1,
                a.wrapping_sub(b),
                a.overflowing_sub(b).1,
                a.checked_div(b),
                a.checked_rem(b)
            );
        }
    }
}

fn signed_table() {
    let vals: [i128; 7] = [
        i128::MIN,
        -(1i128 << 100) - 12345,
        -1,
        0,
        7,
        (1i128 << 64) + 3,
        i128::MAX,
    ];
    for &a in &vals {
        for &b in &vals {
            let (a, b) = (bb(a), bb(b));
            println!(
                "{} op {}: + {:?} - {:?} * {:?} / {:?} % {:?} eu {:?} {:?} cmp {:?}",
                a,
                b,
                a.checked_add(b),
                a.checked_sub(b),
                a.checked_mul(b),
                a.checked_div(b),
                a.checked_rem(b),
                a.checked_div_euclid(b),
                a.checked_rem_euclid(b),
                a.cmp(&b)
            );
        }
        println!(
            "{}: neg {:?} abs {:?} uabs {} signum {} lz {} tz {} ones {}",
            a,
            a.checked_neg(),
            a.checked_abs(),
            a.unsigned_abs(),
            a.signum(),
            a.leading_zeros(),
            a.trailing_zeros(),
            a.count_ones()
        );
    }
}

fn shifts() {
    let v = bb(0x8000_0000_0000_0001_0000_0000_0000_0003u128);
    let s = bb(-0x7000_0000_0000_0000_0000_0000_0000_0005i128);
    for n in [0u32, 1, 31, 32, 63, 64, 65, 96, 127] {
        let n = bb(n);
        println!(
            "shift {:3}: {:032x} {:032x} {:032x} | {} {}",
            n,
            v << n,
            v >> n,
            v.rotate_left(n),
            s >> n,
            s.wrapping_shl(n)
        );
    }
}

fn factorials_and_fib() {
    let mut f = 1u128;
    for i in 1..=34u128 {
        f *= bb(i);
        println!("{}! = {}", i, f);
    }
    let (mut a, mut b) = (0u128, 1u128);
    for i in 0..186 {
        if i % 5 == 0 || i > 180 {
            println!("fib {} = {}", i, a);
        }
        let c = a + b;
        a = b;
        b = c;
    }
}

fn formats() {
    let vals: [u128; 5] = [0, 9, 10_000_000_000_000_000_000, 123_456_789_012_345_678_901_234_567_890, u128::MAX];
    for &v in &vals {
        let v = bb(v);
        println!("{} | {:#x} | {:#o} | {:>45} | {:e}", v, v, v, v, v);
        println!("   {:b}", v);
    }
    for v in [i128::MIN, -1, i128::MAX] {
        let v = bb(v);
        println!("{} | {:+} | {:x} | {:045}", v, v, v, v);
    }
}

fn parsing() {
    let inputs = [
        "0",
        "340282366920938463463374607431768211455",
        "340282366920938463463374607431768211456",
        "-170141183460469231731687303715884105728",
        "-170141183460469231731687303715884105729",
        "+42",
        "12a",
        "",
        "-",
        "000000000000000000000000000000000000000000000000001",
    ];
    for s in inputs {
        println!("{:?}: u128 {:?} i128 {:?}", s, s.parse::<u128>().ok(), s.parse::<i128>().ok());
    }
    for (s, r) in [("ffffffffffffffffffffffffffffffff", 16), ("zzzzzzzzzzzzzzzzzzzzzzzz", 36), ("-1010", 2)] {
        println!(
            "radix {} {:?}: u128 {:?} i128 {:?}",
            r,
            s,
            u128::from_str_radix(s, r).ok(),
            i128::from_str_radix(s, r).ok()
        );
    }
}

fn mulhi() {
    // The high half of a 64x64 product, and 128-bit modular exponentiation.
    let mut x = bb(0x9e37_79b9_7f4a_7c15u64);
    for _ in 0..12 {
        let p = (x as u128) * (0xd1b5_4a32_d192_ed03u128);
        let hi = (p >> 64) as u64;
        let lo = p as u64;
        println!("{:016x} -> hi {:016x} lo {:016x}", x, hi, lo);
        x = hi ^ lo;
    }
    let m: u128 = bb(0xffff_ffff_ffff_ffc5); // 2^64 - 59, prime
    let mut acc = 1u128;
    let mut base = bb(3u128);
    let mut e = bb(0xffff_ffff_ffff_ffc4u64);
    while e > 0 {
        if e & 1 == 1 {
            acc = acc * base % m;
        }
        base = base * base % m;
        e >>= 1;
    }
    println!("3^(p-1) mod p = {}", acc);
    for k in [2u128, 5, 7, 11, 1_000_003] {
        let mut acc = 1u128;
        let mut b = bb(k);
        let mut e = 1_000_000_007u64;
        while e > 0 {
            if e & 1 == 1 {
                acc = acc * b % m;
            }
            b = b * b % m;
            e >>= 1;
        }
        println!("{}^1000000007 mod p = {}", k, acc);
    }
}

fn float_conv() {
    for v in [0u128, 1, 1 << 53, (1 << 53) + 1, 1 << 100, u128::MAX] {
        let v = bb(v);
        println!("{} as f64 = {:e}, as f32 = {:e}", v, v as f64, v as f32);
    }
    for f in [0.0f64, 1.5, -1.5, 1e20, 1e38, 3.5e38, -1e39, f64::INFINITY] {
        let f = bb(f);
        println!("{:e} as u128 = {} as i128 = {}", f, f as u128, f as i128);
    }
}

fn main() {
    mul_table();
    signed_table();
    shifts();
    factorials_and_fib();
    formats();
    parsing();
    mulhi();
    float_conv();
}
