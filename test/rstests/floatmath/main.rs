// f32 and f64 arithmetic and the methods on them: sqrt, cbrt, powi, powf, exp,
// exp2, ln, log2, log10, sin, cos, tan, atan2, sinh, hypot, mul_add, recip,
// abs, signum, copysign, floor/ceil/round/trunc/fract, min/max, rem.
//
// Some of these are single wasm instructions (sqrt, floor, ceil, trunc,
// nearest, min, max, copysign, abs) and the rest are calls into the libm that
// Rust's std compiles into the module.  The single instructions are where the
// native lowering has to match wasm's rules exactly -- wasm's `min` returns NaN
// if either side is NaN and orders -0 below +0, while Rust's `f64::min`
// ignores a NaN, so LLVM wraps the instruction in compares; `nearest` rounds
// half to even while Rust's `round` rounds half away from zero.  The libm
// routines are long straight-line float code, a good test of register
// allocation over d-registers.  Values are printed to 17 significant digits
// and also as raw bits, so a last-ulp difference cannot hide.

use std::hint::black_box as bb;

fn show64(name: &str, v: f64) {
    if v.is_nan() {
        println!("  {:<10} NaN", name);
    } else {
        println!("  {:<10} {:.17e} {:016x}", name, v, v.to_bits());
    }
}

fn show32(name: &str, v: f32) {
    if v.is_nan() {
        println!("  {:<10} NaN", name);
    } else {
        println!("  {:<10} {:.9e} {:08x}", name, v, v.to_bits());
    }
}

fn f64_unary(x: f64) {
    println!("f64 x = {:?}", x);
    let x = bb(x);
    show64("sqrt", x.sqrt());
    show64("cbrt", x.cbrt());
    show64("exp", x.exp());
    show64("exp2", x.exp2());
    show64("exp_m1", x.exp_m1());
    show64("ln", x.ln());
    show64("ln_1p", x.ln_1p());
    show64("log2", x.log2());
    show64("log10", x.log10());
    show64("sin", x.sin());
    show64("cos", x.cos());
    show64("tan", x.tan());
    show64("asin", x.asin());
    show64("atan", x.atan());
    show64("sinh", x.sinh());
    show64("tanh", x.tanh());
    show64("floor", x.floor());
    show64("ceil", x.ceil());
    show64("round", x.round());
    show64("rnd_even", x.round_ties_even());
    show64("trunc", x.trunc());
    show64("fract", x.fract());
    show64("abs", x.abs());
    show64("signum", x.signum());
    show64("recip", x.recip());
    show64("powi3", x.powi(3));
    show64("powi-2", x.powi(-2));
    show64("powf1.5", x.powf(1.5));
    show64("to_deg", x.to_degrees());
}

fn f32_unary(x: f32) {
    println!("f32 x = {:?}", x);
    let x = bb(x);
    show32("sqrt", x.sqrt());
    show32("exp", x.exp());
    show32("ln", x.ln());
    show32("sin", x.sin());
    show32("cos", x.cos());
    show32("atan", x.atan());
    show32("floor", x.floor());
    show32("ceil", x.ceil());
    show32("round", x.round());
    show32("rnd_even", x.round_ties_even());
    show32("trunc", x.trunc());
    show32("fract", x.fract());
    show32("recip", x.recip());
    show32("powi5", x.powi(5));
    show32("powf0.3", x.powf(0.3));
}

fn binary() {
    let vals = [-2.5f64, -0.0, 0.0, 0.75, 1.0, 3.0, 1e300, f64::INFINITY];
    for &a in &vals {
        for &b in &vals {
            let (a, b) = (bb(a), bb(b));
            println!(
                "{:?} {:?}: + {:?} - {:?} * {:?} / {:?} % {:?} min {:?} max {:?} atan2 {:.15} hypot {:?} copysign {:?} lt {} eq {} pcmp {:?}",
                a,
                b,
                a + b,
                a - b,
                a * b,
                a / b,
                a % b,
                a.min(b),
                a.max(b),
                a.atan2(b),
                a.hypot(b),
                a.copysign(b),
                a < b,
                a == b,
                a.partial_cmp(&b)
            );
        }
    }
}

fn nan_rules() {
    let n = bb(f64::NAN);
    let one = bb(1.0f64);
    println!("nan==nan {} nan<1 {} nan>1 {} nan!=nan {}", n == n, n < one, n > one, n != n);
    println!("min(nan,1) {:?} max(1,nan) {:?} min(nan,nan) is_nan {}", n.min(one), one.max(n), n.min(n).is_nan());
    println!("nan.partial_cmp {:?} sum is_nan {}", n.partial_cmp(&one), (n + one).is_nan());
    println!("0*inf nan {} inf-inf nan {}", (bb(0.0f64) * f64::INFINITY).is_nan(), (f64::INFINITY - bb(f64::INFINITY)).is_nan());
    let n32 = bb(f32::NAN);
    println!("f32 min {:?} max {:?} clamp-ish {:?}", n32.min(2.0), bb(2.0f32).max(n32), n32.max(-1.0).min(1.0));
    println!("signum -0 {:?} +0 {:?} copysign(3,-0) {:?}", bb(-0.0f64).signum(), bb(0.0f64).signum(), bb(3.0f64).copysign(-0.0));
    println!("-0 == 0 {} min(-0,0) bits {:x} max(-0,0) bits {:x}", bb(-0.0f64) == 0.0, bb(-0.0f64).min(0.0).to_bits(), bb(-0.0f64).max(0.0).to_bits());
}

fn fma_and_sums() {
    // mul_add is a single rounding; a*b+c is two.  Chosen so they differ.
    let a = bb(1.0f64 + f64::EPSILON);
    let b = bb(1.0f64 - f64::EPSILON);
    let c = bb(-1.0f64);
    println!("fma {:e} plain {:e}", a.mul_add(b, c), a * b + c);
    let a32 = bb(1.0f32 + f32::EPSILON);
    let b32 = bb(1.0f32 - f32::EPSILON);
    println!("fma32 {:e} plain32 {:e}", a32.mul_add(b32, -1.0), a32 * b32 - 1.0);
    for k in 1..=12 {
        let x = bb(k as f64 * 0.37);
        println!("k {:2}: fma(x,x,-1) {:.17e} poly {:.17e}", k, x.mul_add(x, -1.0), horner(x));
    }
    // Kahan versus naive summation of a series whose terms vary in size.
    let mut naive = 0.0f64;
    let mut kahan = 0.0f64;
    let mut comp = 0.0f64;
    for i in 1..=2000 {
        let t = bb(1.0 / (i as f64 * i as f64));
        naive += t;
        let y = t - comp;
        let s = kahan + y;
        comp = (s - kahan) - y;
        kahan = s;
        if i % 250 == 0 {
            println!("n {:4}: naive {:.17} kahan {:.17}", i, naive, kahan);
        }
    }
    let mut s32 = 0.0f32;
    for i in 0..1000 {
        s32 += bb(0.1f32) * (i % 7) as f32;
    }
    println!("f32 sum {:.9} bits {:08x}", s32, s32.to_bits());
}

fn horner(x: f64) -> f64 {
    let c = [1.0, -0.5, 1.0 / 6.0, -1.0 / 24.0, 1.0 / 120.0];
    let mut r = 0.0f64;
    for &k in c.iter().rev() {
        r = r.mul_add(x, k);
    }
    r
}

fn newton() {
    // Square roots by Newton's method against sqrt, and a few series.
    for v in [2.0f64, 10.0, 12345.678, 1e-8, 1e20] {
        let v = bb(v);
        let mut g = v.max(1.0);
        let mut steps = 0;
        while (g * g - v).abs() > v * 1e-15 && steps < 100 {
            g = 0.5 * (g + v / g);
            steps += 1;
        }
        println!("newton sqrt({:e}) = {:.17e} vs {:.17e} in {} steps", v, g, v.sqrt(), steps);
    }
    let mut pi = 0.0f64;
    for k in 0..200000 {
        let t = 1.0 / (2 * k + 1) as f64;
        pi += if k % 2 == 0 { t } else { -t };
    }
    println!("leibniz pi {:.15}", 4.0 * pi);
    let mut e = 0.0f64;
    let mut fact = 1.0f64;
    for k in 0..20 {
        if k > 0 {
            fact *= k as f64;
        }
        e += 1.0 / fact;
    }
    println!("series e {:.17} const {:.17}", e, std::f64::consts::E);
}

fn trig_table() {
    for deg in (0..=360).step_by(15) {
        let r = bb(deg as f64).to_radians();
        let (s, c) = r.sin_cos();
        let r32 = deg as f32 * std::f32::consts::PI / 180.0;
        println!(
            "{:3} deg: sin {:+.15} cos {:+.15} s2+c2 {:.17} f32 sin {:+.7} cos {:+.7}",
            deg,
            s,
            c,
            s * s + c * c,
            r32.sin(),
            r32.cos()
        );
    }
}

fn main() {
    for x in [0.5f64, 1.0, 2.0, -1.5, 0.1, 100.0, 1e-5, 7.25] {
        f64_unary(x);
    }
    for x in [0.5f32, 2.0, -3.75, 1e-3, 1000.5] {
        f32_unary(x);
    }
    binary();
    nan_rules();
    fma_and_sums();
    newton();
    trig_table();
}
