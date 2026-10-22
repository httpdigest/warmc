// Vec and slice methods: push/pop/insert/remove/swap_remove, sort, sort_by,
// sort_by_key, sort_unstable_by, stable sort order on ties, dedup and
// dedup_by_key, retain and retain_mut, drain of a range, splice, split_off,
// extend_from_slice, truncate, resize, binary_search and its _by forms,
// partition_point, rotate_left/right, reverse, swap, fill, copy_from_slice,
// copy_within, split_at, split_first/last, split by predicate, contains,
// starts_with, concat/join, iter().rev().position, select_nth_unstable,
// chunks_mut, and capacity growth through reserve and shrink_to_fit.
//
// These are the memmove-heavy routines in alloc and core: `insert` and
// `remove` shift a tail with `memory.copy` (or an inlined loop), `rotate` uses
// a three-reversal or a block swap depending on size, and the sorts are
// driftsort / ipnsort with small-sort networks for short runs.  Lengths are
// varied so that more than one of those code paths runs.

use std::convert::TryInto;
use std::hint::black_box as bb;

fn lcg(seed: &mut u32) -> u32 {
    *seed = seed.wrapping_mul(1_103_515_245).wrapping_add(12345);
    (*seed >> 8) % 1000
}

fn main() {
    let mut seed = bb(42u32);
    let base: Vec<u32> = (0..40).map(|_| lcg(&mut seed)).collect();
    println!("base {:?}", base);

    let mut v = base.clone();
    v.sort();
    println!("sort {:?}", v);
    let mut d = base.clone();
    d.sort_by(|a, b| b.cmp(a));
    println!("sort desc {:?}", d);
    let mut k = base.clone();
    k.sort_by_key(|x| (x % 10, *x));
    println!("sort_by_key last digit {:?}", k);
    let mut u = base.clone();
    u.sort_unstable_by(|a, b| (a / 100).cmp(&(b / 100)).then(b.cmp(a)));
    println!("sort_unstable_by century then desc {:?}", u);

    // Stability: equal keys keep their original order.
    let mut tagged: Vec<(u32, usize)> = base.iter().enumerate().map(|(i, &x)| (x % 5, i)).collect();
    tagged.sort_by_key(|p| p.0);
    println!("stable {:?}", tagged);
    let stable_ok = tagged.windows(2).all(|w| w[0].0 < w[1].0 || (w[0].0 == w[1].0 && w[0].1 < w[1].1));
    println!("stable ok {}", stable_ok);

    // Larger sorts take the non-small-sort paths.
    for n in [0usize, 1, 2, 7, 20, 33, 100, 1000] {
        let mut s = bb(12345u32 + n as u32);
        let mut big: Vec<u32> = (0..n).map(|_| lcg(&mut s)).collect();
        let sum_before: u64 = big.iter().map(|&x| x as u64).sum();
        let mut big2 = big.clone();
        big.sort();
        big2.sort_unstable();
        let sum_after: u64 = big.iter().map(|&x| x as u64).sum();
        let sorted = big.windows(2).all(|w| w[0] <= w[1]);
        let h = big.iter().fold(0u64, |h, &x| h.wrapping_mul(1_000_003).wrapping_add(x as u64));
        println!("n {:4}: sorted {} same {} sums {} {} hash {:x} min {:?} max {:?}", n, sorted, big == big2, sum_before, sum_after, h, big.first(), big.last());
    }
    let mut strs: Vec<String> = ["pear", "Apple", "fig", "banana", "apple", "Fig", "cherry", "date"].iter().map(|s| s.to_string()).collect();
    strs.sort();
    println!("strings {:?}", strs);
    strs.sort_by_key(|s| s.to_lowercase());
    println!("case-insensitive {:?}", strs);
    strs.sort_by(|a, b| b.len().cmp(&a.len()).then_with(|| a.cmp(b)));
    println!("by len desc {:?}", strs);
    let mut fl = vec![2.5f64, -1.0, 9.75, 0.0, -0.0, 3.25, -7.5];
    fl.sort_by(|a, b| a.total_cmp(b));
    println!("floats {:?}", fl);

    let mut dd = vec![1, 1, 2, 2, 2, 3, 1, 1, 4, 4, 5, 5, 5, 5];
    dd.dedup();
    println!("dedup {:?}", dd);
    let mut dk = vec![10, 11, 12, 20, 25, 31, 39, 40, 41];
    dk.dedup_by_key(|x| *x / 10);
    println!("dedup_by_key {:?}", dk);
    let mut db = vec!["a", "A", "b", "B", "b", "c"];
    db.dedup_by(|a, b| a.eq_ignore_ascii_case(b));
    println!("dedup_by {:?}", db);

    let mut r: Vec<i32> = (1..=30).collect();
    r.retain(|&x| x % 3 != 0 && x % 7 != 0);
    println!("retain {:?}", r);
    r.retain_mut(|x| {
        *x *= 2;
        *x % 4 != 0
    });
    println!("retain_mut {:?}", r);

    let mut dr: Vec<i32> = (0..15).collect();
    let drained: Vec<i32> = dr.drain(3..8).collect();
    println!("drain {:?} left {:?}", drained, dr);
    let tail: Vec<i32> = dr.drain(7..).rev().collect();
    println!("drain tail rev {:?} left {:?}", tail, dr);
    let mut sp: Vec<i32> = (0..10).collect();
    let removed: Vec<i32> = sp.splice(2..5, [100, 200, 300, 400, 500]).collect();
    println!("splice removed {:?} now {:?}", removed, sp);
    let removed2: Vec<i32> = sp.splice(..3, std::iter::empty()).collect();
    println!("splice shrink removed {:?} now {:?}", removed2, sp);
    let back = sp.split_off(6);
    println!("split_off {:?} / {:?}", sp, back);

    let mut m = vec![1, 2, 3];
    m.insert(0, 0);
    m.insert(4, 4);
    m.insert(2, 99);
    println!("insert {:?}", m);
    println!("remove {} swap_remove {} now {:?}", m.remove(2), m.swap_remove(0), m);
    m.extend_from_slice(&[7, 8, 9]);
    m.extend((10..13).map(|x| x * x));
    println!("extend {:?}", m);
    let popped = m.pop();
    println!("pop {:?} last {:?}", popped, m.last());
    m.truncate(4);
    m.resize(7, -1);
    println!("truncate+resize {:?}", m);
    m.resize_with(9, Default::default);
    m.swap(0, 8);
    m.reverse();
    println!("resize_with swap reverse {:?}", m);

    let sorted: Vec<u32> = (0..50).map(|x| x * 3).collect();
    for t in [0u32, 1, 3, 72, 73, 147, 148, 1000] {
        println!(
            "binary_search {} -> {:?} by {:?} pp {}",
            t,
            sorted.binary_search(&bb(t)),
            sorted.binary_search_by(|x| x.cmp(&t)),
            sorted.partition_point(|&x| x < t)
        );
    }
    let dup = [1, 2, 2, 2, 2, 3, 5, 5, 8];
    println!("dup bsearch {:?} key {:?}", dup.binary_search(&5).is_ok(), dup.binary_search_by_key(&16, |x| x * 2));
    let pairs = [(1, 'a'), (3, 'b'), (3, 'c'), (7, 'd')];
    println!("by_key {:?} {:?}", pairs.binary_search_by_key(&7, |p| p.0), pairs.binary_search_by_key(&4, |p| p.0));

    let mut rot: Vec<u32> = (1..=12).collect();
    rot.rotate_left(5);
    println!("rotate_left 5 {:?}", rot);
    rot.rotate_right(8);
    println!("rotate_right 8 {:?}", rot);
    let mut big_rot: Vec<u16> = (0..1000).collect();
    big_rot.rotate_left(337);
    println!("big rotate head {:?} tail {:?}", &big_rot[..5], &big_rot[995..]);
    big_rot[100..900].rotate_right(123);
    println!("sub rotate {:?} sum {}", &big_rot[98..106], big_rot.iter().map(|&x| x as u32).sum::<u32>());

    let mut f = [0u8; 12];
    f[2..7].fill(9);
    f[8..].copy_from_slice(&[1, 2, 3, 4]);
    println!("fill copy {:?}", f);
    f.copy_within(8..12, 0);
    println!("copy_within {:?}", f);
    f.copy_within(0..6, 3);
    println!("copy_within overlap {:?}", f);
    let (a, b) = f.split_at(5);
    println!("split_at {:?} {:?}", a, b);
    println!("split_first {:?} split_last {:?}", f.split_first(), [9, 8].split_last());
    let words = [3, 0, 5, 6, 0, 0, 7, 8, 9, 0];
    println!("split {:?}", words.split(|&x| x == 0).collect::<Vec<_>>());
    println!("splitn {:?}", words.splitn(2, |&x| x == 0).collect::<Vec<_>>());
    println!("rsplit {:?}", words.rsplit(|&x| x == 0).map(|s| s.len()).collect::<Vec<_>>());
    println!("contains {} {} starts {} ends {}", words.contains(&7), words.contains(&4), words.starts_with(&[3, 0]), words.ends_with(&[9, 0]));
    println!("rev position {:?} rposition {:?}", words.iter().rev().position(|&x| x == 0), words.iter().rposition(|&x| x == 5));

    let nested = vec![vec![1, 2], vec![3], vec![], vec![4, 5, 6]];
    println!("concat {:?} join {:?}", nested.concat(), nested.join(&0));
    let sv = vec!["ab", "cd", "ef"];
    println!("str concat {} join {}", sv.concat(), sv.join("-"));

    let mut sel: Vec<u32> = base.clone();
    let (_, median, _) = sel.select_nth_unstable(20);
    println!("select_nth median {}", median);
    let mut sorted_copy = base.clone();
    sorted_copy.sort();
    println!("matches sorted {}", sorted_copy[20]);

    let mut grid = vec![0u32; 24];
    for (i, row) in grid.chunks_mut(6).enumerate() {
        for (j, c) in row.iter_mut().enumerate() {
            *c = (i * 6 + j) as u32 * 7 % 24;
        }
        row.sort_unstable();
    }
    println!("chunks_mut {:?}", grid);
    for row in grid.chunks_exact_mut(8) {
        row.reverse();
    }
    println!("chunks_exact_mut {:?}", grid);
    let (l, r) = grid.split_at_mut(12);
    l.swap_with_slice(r);
    println!("swap_with_slice {:?}", grid);

    let mut cap: Vec<u64> = Vec::with_capacity(4);
    let mut grows = Vec::new();
    let mut last = cap.capacity();
    for i in 0..200 {
        cap.push(i);
        if cap.capacity() != last {
            grows.push(cap.capacity());
            last = cap.capacity();
        }
    }
    println!("growth {:?}", grows);
    cap.truncate(10);
    cap.shrink_to_fit();
    println!("after shrink len {} cap {} sum {}", cap.len(), cap.capacity(), cap.iter().sum::<u64>());
    cap.reserve(100);
    println!("reserve cap>=110 {}", cap.capacity() >= 110);
    let vv: Vec<Vec<u8>> = vec![vec![7; 3]; 4];
    println!("vec! repeat {:?}", vv);
    let mut boxed: Vec<Box<str>> = vec!["x".into(), "yy".into()];
    boxed.push("zzz".into());
    println!("boxed str {:?} total {}", boxed, boxed.iter().map(|s| s.len()).sum::<usize>());
    let arr: [i32; 5] = [5, 4, 3, 2, 1];
    let mut av = arr.to_vec();
    av.sort();
    println!("array to_vec {:?} map {:?} array eq {}", av, arr.map(|x| x * 10), arr == [5, 4, 3, 2, 1]);
    let first_three: [i32; 3] = av[..3].try_into().unwrap_or([0; 3]);
    println!("try_into array {:?}", first_three);
}
