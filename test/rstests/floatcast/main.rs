// Conversions into and out of floats: `as` casts from f32/f64 to every integer
// width (which saturate, and send NaN to 0), integer-to-float rounding,
// f32<->f64, to_bits/from_bits, total_cmp ordering, clamp, classify, and
// rounding at the .5 boundaries.
//
// Rust's saturating cast is wasm's `trunc_sat` at 32 and 64 bits; at 8 and 16
// bits it is that plus a clamp LLVM writes out, and to u128 it is a library
// routine.  Native conversion instructions saturate differently by target
// (arm64's fcvtzs saturates, x86's cvttsd2si returns the "integer indefinite"
// value), so this is a place where a back end can be right on one host and
// wrong on the other.  The bit patterns here come from constants and from
// arithmetic with non-NaN results only, so they are fully determined by wasm.

use std::cmp::Ordering;
use std::hint::black_box as bb;

fn f64_to_ints(x: f64) {
    let x = bb(x);
    println!(
        "{:?}: i8 {} u8 {} i16 {} u16 {} i32 {} u32 {} i64 {} u64 {} isize {} usize {}",
        x, x as i8, x as u8, x as i16, x as u16, x as i32, x as u32, x as i64, x as u64, x as isize, x as usize
    );
}

fn f32_to_ints(x: f32) {
    let x = bb(x);
    println!(
        "{:?}f32: i8 {} u8 {} i16 {} u16 {} i32 {} u32 {} i64 {} u64 {}",
        x, x as i8, x as u8, x as i16, x as u16, x as i32, x as u32, x as i64, x as u64
    );
}

fn ints_to_floats() {
    let vals: [i64; 10] = [
        0,
        -1,
        16_777_216,
        16_777_217,
        -16_777_217,
        9_007_199_254_740_993,
        -9_007_199_254_740_993,
        i64::MAX,
        i64::MIN,
        0x7fff_ffff_ffff_fdff,
    ];
    for &v in &vals {
        let v = bb(v);
        let f = v as f64;
        let g = v as f32;
        println!("i64 {} -> f64 {:?} ({:016x}) f32 {:?} ({:08x})", v, f, f.to_bits(), g, g.to_bits());
    }
    let uvals: [u64; 6] = [u64::MAX, 1 << 63, (1 << 63) + 1025, (1 << 53) + 1, 3, 0xffff_ffff];
    for &v in &uvals {
        let v = bb(v);
        println!("u64 {} -> f64 {:?} f32 {:?}", v, v as f64, v as f32);
    }
    for v in [u32::MAX, 0x8000_0001, 16_777_217] {
        let v = bb(v);
        println!("u32 {} -> f64 {:?} f32 {:?}", v, v as f64, v as f32);
    }
    for v in [i32::MIN, -16_777_219, 2_147_483_647] {
        let v = bb(v);
        println!("i32 {} -> f64 {:?} f32 {:?}", v, v as f64, v as f32);
    }
    for v in [i8::MIN, -1i8, 127] {
        println!("i8 {} -> f32 {:?}", v, bb(v) as f32);
    }
    for v in [0u16, 65535] {
        println!("u16 {} -> f64 {:?}", v, bb(v) as f64);
    }
}

fn widen_narrow() {
    let vals = [0.1f64, 1.0 / 3.0, 1e-40, 1e-46, 3.5e38, -3.5e38, 1e39, 65504.0, 16777217.0, f64::MIN_POSITIVE];
    for &v in &vals {
        let v = bb(v);
        let n = v as f32;
        let w = n as f64;
        println!("{:e} -> f32 {:e} ({:08x}) -> f64 {:e} exact {}", v, n, n.to_bits(), w, w == v);
    }
}

fn bits() {
    let patterns: [u64; 10] = [
        0,
        0x8000_0000_0000_0000,
        0x3ff0_0000_0000_0000,
        0x0000_0000_0000_0001,
        0x000f_ffff_ffff_ffff,
        0x0010_0000_0000_0000,
        0x7fef_ffff_ffff_ffff,
        0x7ff0_0000_0000_0000,
        0xfff0_0000_0000_0000,
        0x4009_21fb_5444_2d18,
    ];
    for &p in &patterns {
        let f = f64::from_bits(bb(p));
        println!(
            "{:016x}: {:e} class {:?} normal {} subnormal {} finite {} sign- {} +1ulp {:e}",
            p,
            f,
            f.classify(),
            f.is_normal(),
            f.is_subnormal(),
            f.is_finite(),
            f.is_sign_negative(),
            f64::from_bits(p.wrapping_add(1))
        );
    }
    let p32: [u32; 6] = [0x0000_0001, 0x0080_0000, 0x3f80_0000, 0x7f7f_ffff, 0xbf00_0000, 0x4049_0fdb];
    for &p in &p32 {
        let f = f32::from_bits(bb(p));
        println!("{:08x}: {:e} class {:?} as f64 {:e}", p, f, f.classify(), f as f64);
    }
    // Next-after by bit stepping, around 1.0 and around 0.
    let mut x = bb(1.0f64);
    for i in 0..5 {
        let up = f64::from_bits(x.to_bits() + 1);
        println!("step {}: {:.20} next {:.20} diff {:e}", i, x, up, up - x);
        x = up;
    }
    println!("next_up(1) {:e} next_down(1) {:e} next_up(-0) {:e}", bb(1.0f64).next_up(), bb(1.0f64).next_down(), bb(-0.0f64).next_up());
}

fn total_order() {
    let mut v = vec![
        3.0f64,
        -0.0,
        f64::INFINITY,
        0.0,
        -1.5,
        f64::NEG_INFINITY,
        f64::NAN,
        -f64::NAN,
        1e-320,
        -1e-320,
        f64::MAX,
        f64::MIN,
        2.0,
    ];
    v.sort_by(|a, b| a.total_cmp(b));
    let shown: Vec<String> = v.iter().map(|x| format!("{:?}", x)).collect();
    println!("total order: {}", shown.join(" "));
    let bitsv: Vec<String> = v.iter().map(|x| format!("{:x}", x.to_bits())).collect();
    println!("bits: {}", bitsv.join(" "));
    let pairs = [(-0.0f64, 0.0f64), (f64::NAN, f64::NAN), (1.0, f64::NAN), (-f64::NAN, -1.0)];
    for (a, b) in pairs {
        let (a, b) = (bb(a), bb(b));
        let t = a.total_cmp(&b);
        println!("total_cmp {:?} {:?} = {:?} partial {:?}", a, b, t, a.partial_cmp(&b));
    }
    let mut w: Vec<f32> = vec![2.5, -0.0, 0.0, -7.0, 1e-40, f32::INFINITY, -2.5];
    w.sort_by(|a, b| a.partial_cmp(b).unwrap_or(Ordering::Equal));
    println!("f32 partial sort {:?}", w);
    w.sort_by(|a, b| b.total_cmp(a));
    println!("f32 total desc {:?}", w);
    let m = v.iter().cloned().filter(|x| !x.is_nan()).fold(f64::NEG_INFINITY, f64::max);
    println!("max non-nan {:?}", m);
}

fn clamp_and_round() {
    for x in [-2.5f64, -1.5, -0.5, 0.5, 1.5, 2.5, 3.5, 2.4999999999999996, -0.49999999999999994, 4503599627370497.0] {
        let x = bb(x);
        println!(
            "{:?}: round {:?} even {:?} floor {:?} ceil {:?} trunc {:?} clamp {:?} as i32 {}",
            x,
            x.round(),
            x.round_ties_even(),
            x.floor(),
            x.ceil(),
            x.trunc(),
            x.clamp(-1.0, 2.0),
            x as i32
        );
    }
    for x in [-2.5f32, 0.5, 1.5, 8388609.0, -0.0] {
        let x = bb(x);
        println!("f32 {:?}: round {:?} even {:?} floor {:?} ceil {:?}", x, x.round(), x.round_ties_even(), x.floor(), x.ceil());
    }
    for x in [5i32, -5, 50, 0] {
        println!("int clamp {} -> {}", x, bb(x).clamp(-3, 10));
    }
}

fn main() {
    let f64s = [
        0.0,
        -0.0,
        0.9999,
        -0.9999,
        1.5,
        -1.5,
        127.9,
        128.0,
        -128.5,
        -129.0,
        255.5,
        256.0,
        32767.9,
        65535.5,
        -32769.0,
        2147483647.5,
        2147483648.0,
        -2147483649.0,
        4294967295.9,
        4294967296.0,
        9.2233720368547748e18,
        9.2233720368547758e18,
        -9.2233720368547758e18,
        1.8446744073709550e19,
        1.8446744073709552e19,
        1e300,
        -1e300,
        f64::INFINITY,
        f64::NEG_INFINITY,
        f64::NAN,
        1e-310,
    ];
    for &x in &f64s {
        f64_to_ints(x);
    }
    let f32s = [0.0f32, -0.5, 200.7, -200.7, 70000.0, 3e9, -3e9, 2e19, f32::MAX, f32::NAN, f32::NEG_INFINITY];
    for &x in &f32s {
        f32_to_ints(x);
    }
    ints_to_floats();
    widen_narrow();
    bits();
    total_order();
    clamp_and_round();
}
