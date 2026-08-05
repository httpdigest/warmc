// Walking a `&str` by character: UTF-8 decoding with the length of each
// sequence taken from its lead byte.
//
// The counterpart of gobench/utf8scan, which is the kernel whose whole gap
// turned out to be block layout -- warmc was 1.41x wasmtime there and is now
// 0.95x, and none of the three pieces that fixed it showed a win alone.  So this
// row exists to say whether that result was about the shape or about TinyGo:
// Rust's `chars()` is the same decode written by a different front end, and if
// the layout work was really about the shape then this row should be at parity
// too, without anything further being done.
//
// A decode is a load, a compare against four ranges, and a switch on the answer:
// short forward branches with data-dependent outcomes, which is exactly what a
// layout pass either helps or does not.

#[inline(never)]
fn scan(s: &str) -> u64 {
    let mut n: u64 = 0;
    for c in s.chars() {
        n = n.wrapping_add(c as u64);
    }
    n
}

#[inline(never)]
fn scan_bytes(s: &str) -> u64 {
    // The same walk without the decode, so the pair separates the cost of
    // decoding from the cost of touching the bytes.
    let mut n: u64 = 0;
    for b in s.bytes() {
        n = n.wrapping_add(b as u64);
    }
    n
}

fn main() {
    // A mix of one-, two- and three-byte sequences, so the branch inside the
    // decode is not predictable from the last one.
    let mut s = String::new();
    for i in 0..3000u32 {
        s.push(char::from_u32(0x41 + i % 26).unwrap());
        s.push(char::from_u32(0xa0 + i % 0x300).unwrap());
        s.push(char::from_u32(0x2000 + i % 0x500).unwrap());
    }
    let mut t: u64 = 0;
    for _ in 0..15000u32 {
        t = t.wrapping_add(scan(std::hint::black_box(&s)));
        t = t.wrapping_add(scan_bytes(std::hint::black_box(&s)));
    }
    println!("t = {}", t);
}
