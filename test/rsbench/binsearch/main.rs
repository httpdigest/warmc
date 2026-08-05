// Binary search: a loop whose branch is unpredictable by construction.
//
// Every iteration halves the range on a comparison whose outcome is data, so the
// predictor is right about half the time and the loop is bounded by branch
// misprediction rather than by its instruction count.  That makes this the one
// kernel in the corpus where reducing instructions should visibly *not* help,
// which is worth having: it is a control for the rest of the corpus, where a
// change that lowers the instruction count usually lowers the time too.
//
// `partition_point` is the branchless-ish sibling -- the standard library writes
// it as a conditional move rather than a branch -- so the pair says whether
// warmc keeps a `csel` as a `csel` or turns it back into a branch.

#[inline(never)]
fn find(v: &[u32], x: u32) -> usize {
    match v.binary_search(&x) {
        Ok(i) => i,
        Err(i) => i,
    }
}

#[inline(never)]
fn part(v: &[u32], x: u32) -> usize {
    v.partition_point(|&y| y < x)
}

fn main() {
    let n = 8192usize;
    let v: Vec<u32> = (0..n as u32).map(|i| i.wrapping_mul(3) | 1).collect();
    let mut s: u64 = 0;
    for r in 0..7000000u32 {
        // A stride coprime with the length, so successive searches land in
        // unrelated parts of the array and share no cached path.
        let key = ((r as u64 * 7919) % (n as u64 * 3)) as u32;
        s = s.wrapping_add(find(std::hint::black_box(&v), key) as u64);
        s = s.wrapping_add(part(std::hint::black_box(&v), key) as u64);
    }
    println!("s = {}", s);
}
