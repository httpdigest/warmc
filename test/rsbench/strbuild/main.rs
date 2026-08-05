// Building a String: UTF-8 appends, a reallocating buffer, and integer
// formatting.
//
// `push_str` is a capacity test, a `memcpy` of a few bytes and a length update.
// `write!` into a String goes through `core::fmt`, which is a virtual call per
// argument through a `&dyn Write` -- so this kernel has an indirect call in its
// inner loop whether or not the program appears to make one.  That makes it a
// second, unplanned test of warmc's `call_indirect`, and one that arrives from a
// direction gobench/iface does not.

use std::fmt::Write;

#[inline(never)]
fn build(n: u32) -> String {
    let mut s = String::new();
    for i in 0..n {
        s.push_str("item ");
        // Formatting rather than a hand-rolled digit loop, because the point is
        // what core::fmt costs.
        let _ = write!(s, "{}", i);
        s.push(';');
    }
    s
}

fn main() {
    let mut t: u64 = 0;
    for r in 0..50000u32 {
        let s = build(std::hint::black_box(200) + (r & 1));
        // A checksum over the bytes, so the string must actually be built.
        t = t.wrapping_add(s.len() as u64);
        t = t.wrapping_add(s.bytes().map(|b| b as u64).sum::<u64>());
    }
    println!("t = {}", t);
}
