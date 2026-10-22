// Bit-level integer methods at every width: leading/trailing zeros and ones,
// count_ones, rotate_left/right, swap_bytes, reverse_bits, to/from big- and
// little-endian bytes, pow and its checked/wrapping forms, abs_diff, isqrt,
// ilog2/ilog10, next_power_of_two, rem_euclid and div_euclid.
//
// wasm has clz, ctz and popcnt only at 32 and 64 bits, and no byte swap or bit
// reverse at all, so the narrow widths are an i32 op plus a correction (clz of
// a u8 is clz32 minus 24) and swap_bytes / reverse_bits are shift-and-mask
// ladders LLVM writes out.  Those corrections and ladders are exactly where an
// instruction selector that pattern-matches them back into native rev/rbit/clz
// can get a width or an offset wrong.

use std::hint::black_box as bb;

macro_rules! bits_row {
    ($t:ty, $v:expr) => {{
        let v: $t = bb($v);
        println!(
            "{} {:#x}: lz {} tz {} lo {} to {} ones {} zeros {} rl3 {:#x} rr5 {:#x} swap {:#x} rev {:#x} pow2? {}",
            stringify!($t),
            v,
            v.leading_zeros(),
            v.trailing_zeros(),
            v.leading_ones(),
            v.trailing_ones(),
            v.count_ones(),
            v.count_zeros(),
            v.rotate_left(bb(3)),
            v.rotate_right(bb(5)),
            v.swap_bytes(),
            v.reverse_bits(),
            v.is_power_of_two(),
        );
        println!(
            "   be {:?} le {:?} npot {:?} ilog2 {:?} ilog10 {:?} isqrt {}",
            v.to_be_bytes(),
            v.to_le_bytes(),
            v.checked_next_power_of_two(),
            v.checked_ilog2(),
            v.checked_ilog10(),
            v.isqrt()
        );
        let back = <$t>::from_be_bytes(v.to_be_bytes()) == v
            && <$t>::from_le_bytes(v.to_le_bytes()) == v
            && <$t>::from_ne_bytes(v.to_ne_bytes()) == v
            && v.swap_bytes().swap_bytes() == v
            && v.reverse_bits().reverse_bits() == v
            && v.rotate_left(7).rotate_right(7) == v;
        println!("   roundtrip {}", back);
    }};
}

macro_rules! signed_row {
    ($t:ty, $v:expr) => {{
        let v: $t = bb($v);
        println!(
            "{} {}: lz {} tz {} ones {} swap {} rev {} rl1 {} be {:?}",
            stringify!($t),
            v,
            v.leading_zeros(),
            v.trailing_zeros(),
            v.count_ones(),
            v.swap_bytes(),
            v.reverse_bits(),
            v.rotate_left(1),
            v.to_be_bytes()
        );
    }};
}

fn pows() {
    for base in [0u32, 1, 2, 3, 7, 10, 255, 65535] {
        let mut line = format!("u32 {:>5}:", base);
        for e in [0u32, 1, 2, 5, 8, 16, 31, 32] {
            let (b, e) = (bb(base), bb(e));
            line.push_str(&format!(" {}^{}={:?}/{}", b, e, b.checked_pow(e), b.wrapping_pow(e)));
        }
        println!("{}", line);
    }
    for base in [-3i64, -2, 2, 3, 10, 1 << 20] {
        for e in [0u32, 1, 13, 39, 40, 62, 63, 64] {
            let (b, e) = (bb(base), bb(e));
            println!(
                "i64 {}^{} = {:?} wrap {} sat {} ovf {:?}",
                b,
                e,
                b.checked_pow(e),
                b.wrapping_pow(e),
                b.saturating_pow(e),
                b.overflowing_pow(e)
            );
        }
    }
    for b in [2u8, 3, 15, 16] {
        for e in [0u32, 1, 2, 3, 4, 7, 8] {
            let (b, e) = (bb(b), bb(e));
            println!("u8 {}^{} = {:?} {} {}", b, e, b.checked_pow(e), b.wrapping_pow(e), b.saturating_pow(e));
        }
    }
}

fn euclid() {
    let nums = [-100i32, -37, -8, -1, 0, 1, 8, 37, 100];
    for d in [-7i32, -3, 3, 7, 8] {
        let mut line = format!("d={:>2}:", d);
        for &n in &nums {
            let (n, d) = (bb(n), bb(d));
            line.push_str(&format!(" {}:{}/{}", n, n.div_euclid(d), n.rem_euclid(d)));
        }
        println!("{}", line);
    }
    for (n, d) in [(i64::MIN + 1, 2i64), (-1, i64::MAX), (i64::MAX, -2), (-9, 4)] {
        let (n, d) = (bb(n), bb(d));
        println!("i64 {} eu {} = {} r {}", n, d, n.div_euclid(d), n.rem_euclid(d));
    }
    for (a, b) in [(3u8, 250u8), (250, 3), (0, 255)] {
        println!("u8 abs_diff {} {} = {}", a, b, bb(a).abs_diff(bb(b)));
    }
    for (a, b) in [(i32::MIN, i32::MAX), (-5, 5), (100, -100), (7, 7)] {
        println!("i32 abs_diff {} {} = {}", a, b, bb(a).abs_diff(bb(b)));
    }
    for (a, b) in [(i64::MIN, i64::MAX), (-1, 1), (0, i64::MIN)] {
        println!("i64 abs_diff {} {} = {}", a, b, bb(a).abs_diff(bb(b)));
    }
    for (a, b) in [(17u32, 5u32), (16, 4), (0, 3), (1, 1000)] {
        let (a, b) = (bb(a), bb(b));
        println!("u32 {} div_ceil {} = {} next_multiple {}", a, b, a.div_ceil(b), a.next_multiple_of(b));
    }
}

fn popcount_sweep() {
    // Every bit position: a single bit, its complement, and a run up to it.
    let mut acc = 0u64;
    for i in 0..64u32 {
        let one = bb(1u64 << i);
        let comp = !one;
        let run = if i == 63 { u64::MAX } else { (1u64 << (i + 1)) - 1 };
        let row = (
            one.leading_zeros(),
            one.trailing_zeros(),
            comp.count_ones(),
            comp.leading_ones(),
            run.count_ones(),
            run.trailing_ones(),
            one.reverse_bits().trailing_zeros(),
            (one as u32).count_ones(),
            (one as u16).leading_zeros(),
            (one as u8).trailing_zeros(),
        );
        acc = acc.wrapping_mul(31).wrapping_add(row.0 as u64 + row.2 as u64 * 7 + row.9 as u64);
        println!("bit {:2}: {:?}", i, row);
    }
    println!("acc {}", acc);
}

fn gray_and_parity() {
    for i in 0..32u32 {
        let i = bb(i);
        let g = i ^ (i >> 1);
        let mut back = g;
        let mut s = 1;
        while s < 32 {
            back ^= back >> s;
            s <<= 1;
        }
        println!(
            "{:2} gray {:05b} back {:2} parity {} isolate {:2} clearlow {:2} spread {:#x}",
            i,
            g,
            back,
            i.count_ones() & 1,
            i & i.wrapping_neg(),
            i & i.wrapping_sub(1),
            spread(i as u16)
        );
    }
}

// Interleave the bits of a u16 with zeros (a Morton-code half).
fn spread(x: u16) -> u32 {
    let mut v = x as u32;
    v = (v | (v << 8)) & 0x00ff_00ff;
    v = (v | (v << 4)) & 0x0f0f_0f0f;
    v = (v | (v << 2)) & 0x3333_3333;
    v = (v | (v << 1)) & 0x5555_5555;
    v
}

fn main() {
    for v in [0u8, 1, 0x80, 0xff, 0x5a, 0x10] {
        bits_row!(u8, v);
    }
    for v in [0u16, 1, 0x8000, 0xffff, 0x1234, 0x0f00] {
        bits_row!(u16, v);
    }
    for v in [0u32, 1, 0x8000_0000, 0xffff_ffff, 0x1234_5678, 0x00f0_0000, 1000] {
        bits_row!(u32, v);
    }
    for v in [0u64, 1, 1 << 63, u64::MAX, 0x0123_4567_89ab_cdef, 1 << 40, 999_999_999_999] {
        bits_row!(u64, v);
    }
    for v in [0usize, 1, 4096, usize::MAX] {
        bits_row!(usize, v);
    }
    for v in [i8::MIN, -1, 0, 1, 0x35] {
        signed_row!(i8, v);
    }
    for v in [i16::MIN, -2, 0x1234] {
        signed_row!(i16, v);
    }
    for v in [i32::MIN, -1, 0, 0x1234_5678] {
        signed_row!(i32, v);
    }
    for v in [i64::MIN, -1, 0x0123_4567_89ab_cdef] {
        signed_row!(i64, v);
    }
    pows();
    euclid();
    popcount_sweep();
    gray_and_parity();
}
