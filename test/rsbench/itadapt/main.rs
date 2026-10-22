// Iterator adaptors chained the way Rust is written: `windows`, `zip` with one
// side `rev`ersed and the other `step_by`, `scan` cut short by `take_while`,
// `flat_map` into a range whose length is data, `chain`, `skip`, `enumerate`,
// and `chunks` with a ragged tail into `filter_map` and `max_by_key`.
//
// sumiter is one `map` and a `sum`; this is what longer chains leave behind.
// Every adaptor is a generic struct whose `next` returns an `Option`, and all of
// it is inlined: none of the five functions makes a call or keeps a bounds
// check.  But the shapes that come out differ a great deal.  `windows(3)` and the
// zip become single loops and `scan` a loop with a `select` for the clamp, while
// `flat_map` keeps `Flatten`'s front and back inner iterators as state and comes
// out as six loops, and `chunks(5)` with its min and max as ten loops and
// forty-five `select`s.
//
// So this row is warmc over LLVM's rendering of state machines the source never
// wrote down, which is most of what idiomatic Rust compiles to.

#[inline(never)]
fn window_peaks(a: &[i32]) -> u64 {
    // `windows(3)`: a local maximum is a middle element above both neighbours.
    a.windows(3).filter(|w| w[1] > w[0] && w[1] > w[2]).map(|w| w[1] as u32 as u64).sum()
}

#[inline(never)]
fn zip_rev(a: &[i32], b: &[i32]) -> i64 {
    // `zip` with one side reversed and the other strided.
    a.iter().step_by(2).zip(b.iter().rev()).map(|(x, y)| *x as i64 * *y as i64).fold(0i64, |s, p| s.wrapping_add(p))
}

#[inline(never)]
fn scan_runs(a: &[i32]) -> u64 {
    // `scan` carrying a running sum that resets whenever it goes negative, and
    // `take_while` stopping the whole chain at the first huge prefix.
    a.iter()
        .scan(0i64, |acc, &x| {
            *acc = (*acc + x as i64).max(0);
            Some(*acc)
        })
        .take_while(|&s| s < 1 << 40)
        .map(|s| s as u64)
        .fold(0u64, |t, s| t.wrapping_mul(3).wrapping_add(s))
}

#[inline(never)]
fn flat_ranges(a: &[i32]) -> u64 {
    // `flat_map` into a short inner range whose length is data, then `chain`
    // and `skip`.
    a.iter()
        .flat_map(|&x| 0..(x as u32 & 7))
        .chain(a.iter().map(|&x| x as u32 >> 29))
        .skip(3)
        .enumerate()
        .map(|(i, v)| (i as u64) ^ v as u64)
        .sum()
}

#[inline(never)]
fn chunk_minmax(a: &[i32]) -> i64 {
    // `chunks` with a ragged tail, then `filter_map` and `max_by_key`.
    a.chunks(5)
        .filter_map(|c| {
            let lo = *c.iter().min()?;
            let hi = *c.iter().max()?;
            if hi - lo > 1000 { Some(hi as i64 - lo as i64) } else { None }
        })
        .enumerate()
        .max_by_key(|&(i, d)| d ^ i as i64)
        .map(|(i, d)| d + i as i64)
        .unwrap_or(-1)
}

fn main() {
    let mut x = 0x2545f4914f6cdd1du64;
    let mut a: Vec<i32> = Vec::with_capacity(4099);
    for _ in 0..4099 {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        a.push((x >> 32) as i32 >> 8);
    }
    let b: Vec<i32> = a.iter().map(|v| v.wrapping_mul(7) >> 3).collect();
    let mut s = 0u64;
    for r in 0..3000u32 {
        let i = (r as usize * 2654435761) % a.len();
        a[i] = a[i].wrapping_add(r as i32);
        let a = std::hint::black_box(&a[..]);
        let b = std::hint::black_box(&b[..]);
        s = s.wrapping_add(window_peaks(a));
        s = s.wrapping_add(zip_rev(a, b) as u64);
        s = s.wrapping_add(scan_runs(a));
        s = s.wrapping_add(flat_ranges(a));
        s = s.wrapping_add(chunk_minmax(a) as u64);
    }
    println!("s = {}", s);
}
