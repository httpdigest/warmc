// `sort_unstable`, which is pattern-defeating quicksort compiled into the
// module.
//
// A sort is the least loop-like kernel here: recursion, a partition whose branch
// goes both ways unpredictably, an insertion sort for short runs, and a heapsort
// fallback. Almost none of it vectorises and all of it depends on comparisons
// the hardware cannot predict, so this row is about branches and calls rather
// than arithmetic.
//
// gobench/sortints is the same algorithm class from Go's `slices.Sort`, and the
// runtime-helpers work found a 16.5% there once `memmove` stopped being the
// bottleneck; this row says whether that was Go's sort or sorting in general.

#[inline(never)]
fn sorted_sum(v: &mut [i32]) -> i64 {
    v.sort_unstable();
    // Position-weighted so a wrong order is a wrong answer rather than the same
    // total.  A plain sum is invariant under permutation, which would let a
    // miscompiled sort pass.
    let mut s: i64 = 0;
    for (i, &x) in v.iter().enumerate() {
        s = s.wrapping_add((i as i64 + 1).wrapping_mul(x as i64));
    }
    s
}

fn main() {
    let mut s: i64 = 0;
    let mut v = vec![0i32; 4000];
    for r in 0..4200u32 {
        // Refilled each round: a sorted input is a different algorithm's
        // best case and would measure the detection of it rather than the sort.
        let mut x = r.wrapping_add(1);
        for i in 0..v.len() {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            v[i] = x as i32;
        }
        s = s.wrapping_add(sorted_sum(std::hint::black_box(&mut v)));
    }
    println!("s = {}", s);
}
