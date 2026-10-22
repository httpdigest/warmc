// f64 to text and back: `{}`, `{:.9}` and `{:e}` written into a String, then
// `parse::<f64>` over the same text, five hundred values a round.
//
// Both directions are among the largest pieces of code in core.  Formatting is
// Grisu -- `format_shortest_opt` for `{}` and `{:e}`, `format_exact_opt` for a
// fixed precision -- with Dragon4 over the `Big32x40` bignum behind it for the
// cases Grisu cannot decide.  Parsing is the Eisel-Lemire fast path
// (`lemire::compute_float`) with a decimal-sequence slow path behind it.  Both
// need 64x64->128-bit products, and wasm has no high-half multiply, so each is a
// call to compiler-builtins' `__multi3`, compiled into the module, which returns
// its result through a stack slot.  And `write!` reaches the float formatter the
// way strbuild reaches the integer one: through `core::fmt::write` and a
// `call_indirect` per argument.
//
// strparse and strbuild cover integers; this is the floating-point half, which
// any program that prints or reads a measurement runs.  The values span 2^-40 to
// 2^40 in both signs, so the digit counts and the exponents both vary.

use std::fmt::Write;

#[inline(never)]
fn values(n: usize, seed: u64) -> Vec<f64> {
    let mut x = seed;
    let mut v = Vec::with_capacity(n);
    for _ in 0..n {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        // A 52-bit mantissa and an exponent between 2^-40 and 2^40, signed:
        // finite, normal, and spread across magnitudes, so `{}` writes anything
        // from a dozen leading zeros to a thirteen-digit integer part.
        let m = (x >> 12) as f64 / (1u64 << 52) as f64 + 1.0;
        let e = ((x & 0xff) % 81) as i32 - 40;
        let s = if x & 0x100 != 0 { -1.0 } else { 1.0 };
        v.push(s * m * 2f64.powi(e));
    }
    v
}

#[inline(never)]
fn render(v: &[f64], buf: &mut String) {
    buf.clear();
    for (i, x) in v.iter().enumerate() {
        // Shortest round-trip, fixed precision and exponent form in turn.
        let _ = match i % 3 {
            0 => write!(buf, "{} ", x),
            1 => write!(buf, "{:.9} ", x),
            _ => write!(buf, "{:e} ", x),
        };
    }
}

#[inline(never)]
fn reparse(s: &str, orig: &[f64]) -> (u64, u64) {
    let mut bits = 0u64;
    let mut exact = 0u64;
    for (f, &o) in s.split_ascii_whitespace().zip(orig) {
        let y: f64 = f.parse().unwrap_or(0.0);
        bits = bits.rotate_left(7) ^ y.to_bits();
        if y == o {
            exact += 1;
        }
    }
    (bits, exact)
}

fn main() {
    let mut v = values(std::hint::black_box(500), 0x2545f4914f6cdd1d);
    let mut buf = String::new();
    let mut bits = 0u64;
    let mut exact = 0u64;
    let mut bytes = 0u64;
    for r in 0..1600u32 {
        // One value nudged each round, so no round repeats the one before.
        let i = r as usize % v.len();
        v[i] *= 1.0000001;
        render(std::hint::black_box(&v), &mut buf);
        bytes = bytes.wrapping_add(buf.len() as u64 + buf.as_bytes()[r as usize % buf.len()] as u64);
        let (b, e) = reparse(std::hint::black_box(&buf), &v);
        bits = bits.rotate_left(5) ^ b;
        exact += e;
    }
    println!("bytes = {} exact = {} bits = {}", bytes, exact, bits);
}
