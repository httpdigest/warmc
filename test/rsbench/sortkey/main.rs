// Sorting records with closures: a stable `sort_by_key` on one field, a
// `sort_unstable_by` on a comparison chained from two keys, and another on an
// f32 field through `total_cmp`, with `dedup_by_key` and `retain` compacting the
// vector in between.
//
// sortints sorts bare i32s with one algorithm.  Here every sort is monomorphised
// for its closure, so the module carries a separate copy per call site:
// driftsort, the stable sort with its run detection, merges and quicksort of its
// own, for `sort_by_key`, and for each `sort_unstable_by` an ipnsort -- quicksort,
// a heapsort fallback, insertion sort and the sort4/sort8 networks for short
// runs -- with the comparator inlined into every copy.  The elements are 16-byte
// structs moved by value, and `total_cmp` becomes the integer trick on the f32's
// bits, an arithmetic shift and an xor, rather than a float compare.
//
// `dedup_by_key` and `retain` are the compaction loops that follow a sort in
// real code: a read cursor, a write cursor, and a copy under a condition that
// depends on the data.

#[derive(Clone, Copy)]
struct Rec {
    id: u32,
    group: u16,
    score: i32,
    weight: f32,
}

#[inline(never)]
fn make(n: usize, seed: u64, out: &mut Vec<Rec>) {
    out.clear();
    let mut x = seed;
    for i in 0..n {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        out.push(Rec {
            id: i as u32,
            group: (x % 97) as u16,
            score: ((x >> 20) % 2001) as i32 - 1000,
            weight: ((x >> 40) & 0xffff) as f32 / 256.0,
        });
    }
}

#[inline(never)]
fn process(v: &mut Vec<Rec>) -> u64 {
    // Stable sort on a key extracted by a closure: equal groups keep their
    // input order, so the result is fully determined.
    v.sort_by_key(|r| r.group);
    let mut s = 0u64;
    for (i, r) in v.iter().enumerate().step_by(97) {
        s = s.wrapping_add((r.id as u64) << (i & 15));
    }
    // An unstable sort under a three-way comparison chained from two keys,
    // descending on score and ascending on id; ids are unique, so this order
    // is total and the unstable sort is deterministic too.
    v.sort_unstable_by(|a, b| b.score.cmp(&a.score).then_with(|| a.id.cmp(&b.id)));
    // Keep the best record per score, then drop the light ones.
    v.dedup_by_key(|r| r.score);
    v.retain(|r| r.weight >= 32.0);
    for r in v.iter() {
        s = s.wrapping_mul(31).wrapping_add(r.id as u64 ^ (r.score as u64));
    }
    // And a float key, which has no `Ord` and goes through `total_cmp`.
    v.sort_unstable_by(|a, b| a.weight.total_cmp(&b.weight).then(a.id.cmp(&b.id)));
    s.wrapping_add(v.len() as u64).wrapping_add(v[v.len() / 2].id as u64)
}

fn main() {
    let n = std::hint::black_box(20000);
    let mut v = Vec::new();
    let mut s = 0u64;
    for r in 0..160u64 {
        make(n, 0x9e3779b97f4a7c15 ^ r, &mut v);
        s = s.rotate_left(5) ^ process(std::hint::black_box(&mut v));
    }
    println!("s = {}", s);
}
