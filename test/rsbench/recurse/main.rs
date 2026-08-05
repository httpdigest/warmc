// Recursion, so calls rather than loops: prologue, epilogue and the return
// address, at a depth that will not fit in registers.
//
// The comparison this sets up is with gobench/recurse and asbench/recurse, and
// what differs between them is not the arithmetic but what a call costs each
// producer.  Rust has no scheduler and no stack-growth check, so a call here is
// as close to bare as wasm gets -- which makes this the row that isolates
// warmc's own call overhead.

#[inline(never)]
fn fib(n: u32) -> u64 {
    if n < 2 {
        n as u64
    } else {
        fib(n - 1) + fib(n - 2)
    }
}

// Mutual recursion, which no amount of inlining flattens into a loop.
#[inline(never)]
fn even(n: u32) -> u64 {
    if n == 0 { 1 } else { odd(n - 1) }
}

#[inline(never)]
fn odd(n: u32) -> u64 {
    if n == 0 { 0 } else { even(n - 1) }
}

fn main() {
    let mut s: u64 = 0;
    for r in 0..2600u32 {
        s = s.wrapping_add(fib(std::hint::black_box(22) + (r & 1)));
        // The depth is bounded rather than growing with `r`: a recursion whose
        // depth rises through the run measures the stack getting deeper as much
        // as it measures the calls.
        s = s.wrapping_add(even(std::hint::black_box(2000) + (r & 7)));
    }
    println!("s = {}", s);
}
