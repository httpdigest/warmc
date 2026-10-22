// Integer arithmetic at every width from i8 to u64: the plain operators, the
// checked/wrapping/saturating/overflowing families, signed division and
// remainder at the edges, and shifts by amounts that wrap.
//
// Every operand goes through `black_box`, so what runs is the arithmetic the
// back end chose for the type rather than a constant LLVM folded.  The narrow
// widths matter most: wasm has no i8 or i16, so an `i8` add is an i32 add whose
// result has to be sign- or zero-extended before the next compare, and a
// missing extension shows here as a wrong `overflowing_add` flag or a wrong
// `saturating_sub` clamp.  The 64-bit rows reach the edges (MIN / -1, MIN % -1)
// where wasm's own `div_s` traps and Rust's checked forms must answer first.

use std::convert::TryFrom;
use std::hint::black_box as bb;

macro_rules! family {
    ($t:ty, $vals:expr) => {{
        let vals: &[$t] = &$vals;
        println!("--- {} ---", stringify!($t));
        for &a in vals {
            for &b in vals {
                let (a, b) = (bb(a), bb(b));
                println!(
                    "{} {}: w+{} w-{} w*{} c+{:?} c-{:?} c*{:?} s+{} s-{} s*{} o+{:?} o-{:?} o*{:?}",
                    a,
                    b,
                    a.wrapping_add(b),
                    a.wrapping_sub(b),
                    a.wrapping_mul(b),
                    a.checked_add(b),
                    a.checked_sub(b),
                    a.checked_mul(b),
                    a.saturating_add(b),
                    a.saturating_sub(b),
                    a.saturating_mul(b),
                    a.overflowing_add(b),
                    a.overflowing_sub(b),
                    a.overflowing_mul(b),
                );
                println!(
                    "   c/{:?} c%{:?} w/{:?} w%{:?} cmp {:?} min {} max {}",
                    a.checked_div(b),
                    a.checked_rem(b),
                    if b != 0 { Some(a.wrapping_div(b)) } else { None },
                    if b != 0 { Some(a.wrapping_rem(b)) } else { None },
                    a.cmp(&b),
                    a.min(b),
                    a.max(b),
                );
            }
        }
    }};
}

macro_rules! shifts {
    ($t:ty, $v:expr) => {{
        let v: $t = bb($v);
        let bits = <$t>::BITS;
        let mut line = String::new();
        for s in [0u32, 1, 3, bits / 2, bits - 1, bits, bits + 1, 2 * bits + 3] {
            let s = bb(s);
            line.push_str(&format!(
                "[{}: {:?} {:?} {} {}] ",
                s,
                v.checked_shl(s),
                v.checked_shr(s),
                v.wrapping_shl(s),
                v.wrapping_shr(s)
            ));
        }
        println!("{} shifts of {}: {}", stringify!($t), v, line);
    }};
}

fn neg_family() {
    for v in [i8::MIN, -1, 0, 1, i8::MAX] {
        let v = bb(v);
        println!(
            "i8 {}: neg {:?} abs {:?} wneg {} wabs {} uabs {} sabs {} sneg {} signum {} pos {} neg {}",
            v,
            v.checked_neg(),
            v.checked_abs(),
            v.wrapping_neg(),
            v.wrapping_abs(),
            v.unsigned_abs(),
            v.saturating_abs(),
            v.saturating_neg(),
            v.signum(),
            v.is_positive(),
            v.is_negative()
        );
    }
    for v in [i32::MIN, -7, 0, 7, i32::MAX] {
        let v = bb(v);
        println!(
            "i32 {}: neg {:?} abs {:?} wneg {} uabs {} sneg {} signum {}",
            v,
            v.checked_neg(),
            v.checked_abs(),
            v.wrapping_neg(),
            v.unsigned_abs(),
            v.saturating_neg(),
            v.signum()
        );
    }
    for v in [i64::MIN, -123456789012, 0, 99, i64::MAX] {
        let v = bb(v);
        println!(
            "i64 {}: neg {:?} abs {:?} wneg {} uabs {} sneg {} signum {}",
            v,
            v.checked_neg(),
            v.checked_abs(),
            v.wrapping_neg(),
            v.unsigned_abs(),
            v.saturating_neg(),
            v.signum()
        );
    }
}

fn division_table() {
    // Truncating, Euclidean and floor-like division side by side.
    let nums = [-17i32, -16, -1, 0, 1, 16, 17, i32::MIN, i32::MAX];
    let dens = [-5i32, -1, 1, 3, 5, 16];
    for &n in &nums {
        let mut line = format!("{:>11}:", n);
        for &d in &dens {
            let (n, d) = (bb(n), bb(d));
            let q = n.wrapping_div(d);
            let r = n.wrapping_rem(d);
            let qe = n.checked_div_euclid(d);
            let re = n.checked_rem_euclid(d);
            line.push_str(&format!(" [{}/{} = {} r {} e {:?} {:?}]", n, d, q, r, qe, re));
        }
        println!("{}", line);
    }
    let unums = [0u32, 1, 7, 100, 4096, u32::MAX];
    for &n in &unums {
        let mut line = format!("{:>10}:", n);
        for d in [1u32, 2, 3, 7, 10, 4096, u32::MAX] {
            let (n, d) = (bb(n), bb(d));
            line.push_str(&format!(" {}/{}={},{}", n, d, n / d, n % d));
        }
        println!("{}", line);
    }
    let lnums = [i64::MIN, -1_000_000_000_007, -3, 0, 5, 1_000_000_000_007, i64::MAX];
    for &n in &lnums {
        for d in [-1i64, 2, -7, 1_000_003, i64::MAX] {
            let (n, d) = (bb(n), bb(d));
            println!(
                "i64 {} / {} = {:?} rem {:?} eu {:?} {:?} ovf {:?}",
                n,
                d,
                n.checked_div(d),
                n.checked_rem(d),
                n.checked_div_euclid(d),
                n.checked_rem_euclid(d),
                n.overflowing_div(d)
            );
        }
    }
}

fn mixed_casts() {
    let src: [i64; 8] = [-1, 255, 256, -129, 65535, 1 << 31, -(1 << 40), 0x1234_5678_9abc_def0];
    for &v in &src {
        let v = bb(v);
        println!(
            "{}: u8 {} i8 {} u16 {} i16 {} u32 {} i32 {} u64 {} usize {}",
            v, v as u8, v as i8, v as u16, v as i16, v as u32, v as i32, v as u64, v as usize
        );
        println!(
            "   try u8 {:?} i16 {:?} u32 {:?} i32 {:?} u64 {:?}",
            u8::try_from(v).ok(),
            i16::try_from(v).ok(),
            u32::try_from(v).ok(),
            i32::try_from(v).ok(),
            u64::try_from(v).ok()
        );
    }
    for v in [0u8, 1, 127, 128, 200, 255] {
        let v = bb(v);
        let s = v as i8;
        println!(
            "u8 {} -> i8 {} -> i32 {} -> u32 {} ; u8 -> i64 {} ; i8 -> u64 {}",
            v, s, s as i32, s as u32, v as i64, s as u64
        );
    }
}

fn accumulate() {
    // A small mixing loop at each width, where the narrow types wrap often.
    let mut a8 = bb(1u8);
    let mut b16 = bb(1i16);
    let mut c32 = bb(1u32);
    let mut d64 = bb(1i64);
    for i in 0..40u32 {
        a8 = a8.wrapping_mul(31).wrapping_add(i as u8) ^ (a8 >> 3);
        b16 = b16.wrapping_mul(-251).wrapping_sub(i as i16) ^ (b16 >> 5);
        c32 = c32.wrapping_mul(2654435761).rotate_left(5) ^ i;
        d64 = d64.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407) >> 1;
        if i % 4 == 0 {
            println!("step {:2}: {:3} {:6} {:10} {:20}", i, a8, b16, c32, d64);
        }
    }
    println!("final {} {} {} {}", a8, b16, c32, d64);
}

fn main() {
    family!(u8, [0, 1, 2, 15, 16, 127, 128, 200, 255]);
    family!(i8, [i8::MIN, -100, -2, -1, 0, 1, 2, 63, i8::MAX]);
    family!(u16, [0, 3, 255, 256, 40000, u16::MAX]);
    family!(i16, [i16::MIN, -300, -1, 0, 1, 181, i16::MAX]);
    family!(u32, [0, 1, 65535, 65536, 3_000_000_000, u32::MAX]);
    family!(i32, [i32::MIN, -46341, -1, 0, 1, 46341, i32::MAX]);
    family!(u64, [0, 1, 1 << 32, 4_294_967_296_123, u64::MAX / 3, u64::MAX]);
    family!(i64, [i64::MIN, -3_037_000_500, -1, 0, 1, 3_037_000_500, i64::MAX]);
    shifts!(u8, 0b1010_0101);
    shifts!(i8, -77);
    shifts!(u16, 0xbeef);
    shifts!(i16, -12345);
    shifts!(u32, 0xdead_beef);
    shifts!(i32, -0x1234_5678);
    shifts!(u64, 0x0123_4567_89ab_cdef);
    shifts!(i64, -0x0123_4567_89ab_cdef);
    neg_family();
    division_table();
    mixed_casts();
    accumulate();
}
