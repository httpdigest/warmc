// Text in and numbers out, and back again: `parse` and integer formatting.
//
// `parse::<i64>` is a per-byte loop with a digit range check, an overflow-checked
// multiply-accumulate, and a `Result` at the end -- so it is the `optionchain`
// and `overflow` shapes arriving together inside a real standard-library
// function.  Formatting is the reverse: repeated division by ten, which LLVM
// turns into a magic multiply, so this is also where warmc's constant-divisor
// path meets somebody else's code rather than a synthetic case.
//
// `split` is here because iterating substrings is what programs actually do
// before parsing anything, and it is a byte scan with a comparison per byte.

#[inline(never)]
fn sum_fields(s: &str) -> i64 {
    let mut t: i64 = 0;
    for f in s.split(',') {
        // Errors are counted rather than ignored, so the failing path is taken
        // by real inputs and not merely present.
        match f.parse::<i64>() {
            Ok(v) => t = t.wrapping_add(v),
            Err(_) => t = t.wrapping_sub(1),
        }
    }
    t
}

#[inline(never)]
fn render(vals: &[i64]) -> String {
    let mut s = String::with_capacity(vals.len() * 8);
    for (i, v) in vals.iter().enumerate() {
        if i > 0 {
            s.push(',');
        }
        // `to_string` on an integer is the division-by-ten loop.
        s.push_str(&v.to_string());
    }
    s
}

fn main() {
    let vals: Vec<i64> = (0..200)
        .map(|i| (i as i64).wrapping_mul(2654435761) % 1000000)
        .collect();
    let mut t: i64 = 0;
    for r in 0..36000u32 {
        let s = render(std::hint::black_box(&vals));
        t = t.wrapping_add(sum_fields(std::hint::black_box(&s)));
        // One field made unparseable every few rounds, so the error arm is
        // exercised rather than merely compiled.
        if r % 8 == 0 {
            let bad = format!("{},x{}", s, r);
            t = t.wrapping_add(sum_fields(&bad));
        }
    }
    println!("t = {}", t);
}
