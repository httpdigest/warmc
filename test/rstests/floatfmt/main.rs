// Floats to text and back: Display, Debug, `{:.N}` precision, `{:e}` and
// `{:E}`, width, fill, sign and zero padding, and `parse::<f64>` / `<f32>` on
// well-formed and malformed input.
//
// Formatting a float is the largest piece of core a typical program runs:
// Grisu for the shortest representation with a Dragon4 bignum fallback, and an
// exact-digits mode for a fixed precision.  Parsing is Eisel-Lemire with a
// decimal slow path.  Both lean on 64x64->128 multiplies (compiler-builtins'
// `__multi3`, returned through memory) and on long chains of u32/u64 bignum
// arithmetic, so a carry bug anywhere in the integer lowering shows up here as
// a wrong digit.

use std::hint::black_box as bb;

fn show(x: f64) {
    let x = bb(x);
    println!(
        "{} | {:?} | {:.0} | {:.3} | {:.10} | {:e} | {:.4e} | {:E} | {:>14.5} | {:<12.2}| {:^16.1} | {:+} | {:014.3} | {:*>10.1}",
        x, x, x, x, x, x, x, x, x, x, x, x, x, x
    );
}

fn show32(x: f32) {
    let x = bb(x);
    println!("f32 {} | {:?} | {:.3} | {:e} | {:.2e} | {:+010.2}", x, x, x, x, x, x);
}

fn parse_tests() {
    let inputs = [
        "0",
        "-0",
        "1",
        "+1.5",
        "3.14159265358979323846264338327950288",
        "1e10",
        "1E-10",
        "2.2250738585072014e-308",
        "2.2250738585072011e-308",
        "4.9e-324",
        "2.4e-324",
        "1.7976931348623157e308",
        "1.8e308",
        "inf",
        "-infinity",
        "NaN",
        ".5",
        "5.",
        "1_000",
        "",
        " 1",
        "1e",
        "0x10",
        "123456789012345678901234567890",
        "0.000000000000000000000000000001",
        "9007199254740993",
        "1.00000000000000011102230246251565404236316680908203125",
        "1.00000000000000011102230246251565404236316680908203124",
    ];
    for s in inputs {
        match s.parse::<f64>() {
            Ok(v) => println!("parse f64 {:?} = {:?} bits {:016x}", s, v, v.to_bits()),
            Err(e) => println!("parse f64 {:?} error: {}", s, e),
        }
        match s.parse::<f32>() {
            Ok(v) => println!("parse f32 {:?} = {:?}", s, v),
            Err(e) => println!("parse f32 {:?} error: {}", s, e),
        }
    }
}

fn roundtrip() {
    // Shortest repr must round-trip; walk a sequence across many exponents.
    let mut x = bb(1.0f64 / 3.0);
    let mut bad = 0;
    for i in 0..120 {
        let s = format!("{}", x);
        let e = format!("{:e}", x);
        let back: f64 = s.parse().unwrap_or(f64::NAN);
        let back_e: f64 = e.parse().unwrap_or(f64::NAN);
        if back != x || back_e != x {
            bad += 1;
        }
        if i % 6 == 0 {
            println!("rt {:3}: {} {} {}", i, s, e, back.to_bits() == x.to_bits());
        }
        x = x * -7.3 + 0.1;
        if x.abs() > 1e200 {
            x = x / 1e250;
        }
    }
    println!("roundtrip failures {}", bad);
    let mut y = bb(0.1f32);
    for i in 0..40 {
        let s = format!("{:?}", y);
        let back: f32 = s.parse().unwrap_or(f32::NAN);
        if i % 4 == 0 {
            println!("rt32 {:2}: {} ok {}", i, s, back == y);
        }
        y = y * 3.7 - 1.1;
    }
}

fn special() {
    for x in [f64::NAN, f64::INFINITY, f64::NEG_INFINITY, -0.0, 0.0] {
        let x = bb(x);
        println!("[{}] [{:?}] [{:.2}] [{:e}] [{:8}] [{:<8}] [{:+}] [{:08.1}]", x, x, x, x, x, x, x, x);
    }
    println!("min {:?} max {:?} eps {:?} minpos {:?}", f64::MIN, f64::MAX, f64::EPSILON, f64::MIN_POSITIVE);
    println!("f32 min {:?} max {:?} eps {:?} minpos {:?}", f32::MIN, f32::MAX, f32::EPSILON, f32::MIN_POSITIVE);
    let prec = bb(4usize);
    let width = bb(12usize);
    println!(
        "dyn [{:width$.prec$}] [{:>w2$.p2$}] [{:.*}] [{:^width$}]",
        2.718281828,
        9.87654321,
        3,
        1.23456,
        -0.5,
        width = width,
        prec = prec,
        w2 = bb(10usize),
        p2 = bb(2usize)
    );
}

fn table() {
    // A formatted table, the way a report would print one.
    println!("{:>6} | {:>12} | {:>12} | {:>10}", "n", "sqrt", "1/n", "n^1.5");
    for n in 1..=24 {
        let f = bb(n as f64);
        println!("{:>6} | {:>12.8} | {:>12.6e} | {:>10.3}", n, f.sqrt(), 1.0 / f, f.powf(1.5));
    }
}

fn main() {
    let vals = [
        0.0,
        1.0,
        -1.0,
        0.1,
        0.5,
        1.0 / 3.0,
        2.0 / 3.0,
        123.456,
        -9876.54321,
        1e15,
        1e16,
        1e17,
        1.5e300,
        1e-5,
        1e-7,
        5e-324,
        2.5,
        3.5,
        0.125,
        0.0625,
        99.995,
        0.045,
        1234567.0,
        6.02214076e23,
        1.602176634e-19,
        std::f64::consts::PI,
        std::f64::consts::E,
        std::f64::consts::SQRT_2,
        f64::MAX,
        f64::MIN_POSITIVE,
    ];
    for &v in &vals {
        show(v);
    }
    for v in [0.1f32, 1.0 / 3.0, 16777216.0, 1e-45, 3.4e38, -2.5, 0.3] {
        show32(v);
    }
    parse_tests();
    roundtrip();
    special();
    table();
}
