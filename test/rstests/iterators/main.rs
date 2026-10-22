// Iterator adapters from std, one per line with its result: map, filter,
// filter_map, fold, scan, take_while, skip_while, map_while, skip, take, zip,
// unzip, chain, flat_map, flatten, rev, step_by, windows, chunks,
// chunks_exact, rchunks, peekable, enumerate, cycle, inspect, partition,
// any/all/find/position/rposition, sum/product, min/max and their _by_key /
// _by forms, count, last, nth, nth_back, cmp/eq/lt between iterators,
// try_fold, reduce, dedup-by-hand with peekable, group runs, and collect into
// Vec, String, BTreeMap and Result.
//
// With opt-level=3 and fat LTO each chain is inlined into a single loop, so
// a chain of five adapters becomes one loop with five conditions folded into
// it; at -O0 in warmc that loop is a single wasm function and every lane of
// state is a local.  The values are chosen so that each adapter visibly
// changes the result.

use std::collections::BTreeMap;
use std::hint::black_box as bb;

fn main() {
    let v: Vec<i32> = bb((1..=20).collect());
    let w: Vec<i32> = bb(vec![5, -3, 8, 8, 0, -3, 12, 7, 7, 7, 1, -9, 4]);

    println!("map {:?}", v.iter().map(|x| x * x - 3).collect::<Vec<_>>());
    println!("filter {:?}", v.iter().filter(|&&x| x % 3 == 0 || x % 5 == 0).collect::<Vec<_>>());
    println!(
        "filter_map {:?}",
        ["1", "x", "-7", "3.5", "40", ""].iter().filter_map(|s| s.parse::<i32>().ok()).collect::<Vec<_>>()
    );
    println!("fold {}", v.iter().fold(0i64, |a, &x| a * 3 + x as i64) % 1_000_000_007);
    println!("fold str {}", w.iter().fold(String::new(), |mut s, x| {
        if !s.is_empty() {
            s.push('|');
        }
        s += &x.to_string();
        s
    }));
    println!(
        "scan {:?}",
        w.iter()
            .scan(0, |acc, &x| {
                *acc += x;
                Some(*acc)
            })
            .collect::<Vec<_>>()
    );
    println!(
        "scan stop {:?}",
        v.iter()
            .scan(1u64, |acc, &x| {
                *acc *= x as u64;
                if *acc > 1_000_000 {
                    None
                } else {
                    Some(*acc)
                }
            })
            .collect::<Vec<_>>()
    );
    println!("take_while {:?}", w.iter().take_while(|&&x| x != 0).collect::<Vec<_>>());
    println!("skip_while {:?}", w.iter().skip_while(|&&x| x != 0).collect::<Vec<_>>());
    println!("map_while {:?}", w.iter().map_while(|&x| if x >= -3 { Some(x * 2) } else { None }).collect::<Vec<_>>());
    println!("skip take {:?}", v.iter().skip(3).take(5).collect::<Vec<_>>());
    println!("zip {:?}", v.iter().zip(w.iter()).map(|(a, b)| a * b).collect::<Vec<_>>());
    let (evens, odds): (Vec<i32>, Vec<i32>) = v.iter().partition(|&&x| x % 2 == 0);
    println!("partition {:?} {:?}", evens, odds);
    let (xs, ys): (Vec<i32>, Vec<char>) = w.iter().take(5).map(|&x| (x, (b'a' + (x.unsigned_abs() % 26) as u8) as char)).unzip();
    println!("unzip {:?} {:?}", xs, ys);
    println!("chain {:?}", v.iter().take(3).chain(w.iter().rev().take(3)).chain([100, 200].iter()).collect::<Vec<_>>());
    println!("flat_map {:?}", (1..5).flat_map(|i| (0..i).map(move |j| i * 10 + j)).collect::<Vec<_>>());
    println!("flatten {:?}", vec![vec![1, 2], vec![], vec![3], vec![4, 5, 6]].into_iter().flatten().collect::<Vec<_>>());
    println!("flatten opt {:?}", vec![Some(1), None, Some(3), None].into_iter().flatten().collect::<Vec<_>>());
    println!("rev {:?}", v.iter().rev().step_by(3).collect::<Vec<_>>());
    println!("step_by {:?}", (0..100).step_by(7).collect::<Vec<_>>());
    println!("range rev step {:?}", (0..=50).rev().step_by(9).collect::<Vec<_>>());
    println!("windows {:?}", w.windows(3).map(|s| s.iter().sum::<i32>()).collect::<Vec<_>>());
    println!("windows max {:?}", w.windows(4).map(|s| *s.iter().max().unwrap_or(&0)).collect::<Vec<_>>());
    println!("chunks {:?}", w.chunks(4).map(|c| c.to_vec()).collect::<Vec<_>>());
    println!("chunks_exact {:?} rem {:?}", w.chunks_exact(5).map(|c| c.len()).collect::<Vec<_>>(), w.chunks_exact(5).remainder());
    println!("rchunks {:?}", w.rchunks(3).collect::<Vec<_>>());
    println!("enumerate {:?}", w.iter().enumerate().filter(|(i, &x)| (*i as i32) < x).collect::<Vec<_>>());
    println!("cycle {:?}", [1, 2, 3].iter().cycle().take(10).collect::<Vec<_>>());
    let mut seen = Vec::new();
    let total: i32 = w.iter().inspect(|&&x| seen.push(x * 10)).filter(|&&x| x > 0).sum();
    println!("inspect total {} seen {:?}", total, seen);

    println!(
        "any {} all {} find {:?} position {:?} rposition {:?} find_map {:?}",
        w.iter().any(|&x| x > 10),
        w.iter().all(|&x| x > -10),
        w.iter().find(|&&x| x % 4 == 0 && x != 0),
        w.iter().position(|&x| x == 7),
        w.iter().rposition(|&x| x == 7),
        w.iter().find_map(|&x| if x < -5 { Some(x * 100) } else { None })
    );
    println!(
        "sum {} product {} count {} last {:?} nth {:?} nth_back {:?}",
        w.iter().sum::<i32>(),
        w.iter().filter(|&&x| x != 0).map(|&x| x as i64).product::<i64>(),
        w.iter().filter(|&&x| x > 0).count(),
        w.iter().last(),
        w.iter().nth(6),
        w.iter().nth_back(2)
    );
    println!(
        "min {:?} max {:?} min_by_key {:?} max_by_key {:?} max_by {:?} min_by {:?}",
        w.iter().min(),
        w.iter().max(),
        w.iter().min_by_key(|&&x| (x - 6).abs()),
        w.iter().max_by_key(|&&x| x.abs()),
        w.iter().max_by(|a, b| (*a % 5).cmp(&(*b % 5))),
        w.iter().min_by(|a, b| b.cmp(a))
    );
    let words = ["delta", "alpha", "charlie", "bravo", "echo", "foxtrot", "golf"];
    println!(
        "longest {:?} shortest {:?} max_by_key last-wins {:?} min_by_key first-wins {:?}",
        words.iter().max_by_key(|s| s.len()),
        words.iter().min_by_key(|s| s.len()),
        words.iter().max_by_key(|s| s.len() % 2),
        words.iter().min_by_key(|s| s.len() % 2)
    );
    println!("reduce {:?} empty {:?}", w.iter().cloned().reduce(|a, b| a.max(b) - 1), Vec::<i32>::new().into_iter().reduce(|a, b| a + b));
    let tf: Option<i32> = w.iter().try_fold(0i32, |a, &x| if x > -5 { a.checked_add(x) } else { None });
    let tf2: Option<i32> = v.iter().try_fold(0i32, |a, &x| a.checked_add(x * 1000));
    println!("try_fold {:?} {:?}", tf, tf2);
    let r: Result<Vec<i32>, String> = ["1", "2", "3"].iter().map(|s| s.parse::<i32>().map_err(|e| e.to_string())).collect();
    let r2: Result<Vec<i32>, String> = ["1", "zz", "3"].iter().map(|s| s.parse::<i32>().map_err(|e| format!("{}: {}", s, e))).collect();
    println!("collect result {:?} {:?}", r, r2);
    let o: Option<Vec<u32>> = "123x".chars().map(|c| c.to_digit(10)).collect();
    let o2: Option<Vec<u32>> = "9081".chars().map(|c| c.to_digit(10)).collect();
    println!("collect option {:?} {:?}", o, o2);
    let s: String = words.iter().map(|w| w.chars().next().unwrap_or('?')).collect();
    let s2: String = words.iter().flat_map(|w| w.chars().rev().take(2)).collect();
    println!("collect string {} {}", s, s2);
    let m: BTreeMap<usize, Vec<&str>> = words.iter().fold(BTreeMap::new(), |mut m, w| {
        m.entry(w.len()).or_insert_with(Vec::new).push(*w);
        m
    });
    println!("group by len {:?}", m);
    println!(
        "cmp {:?} {:?} {:?} eq {} lt {} le {}",
        [1, 2, 3].iter().cmp([1, 2, 4].iter()),
        [1, 2].iter().cmp([1, 2, 0].iter()),
        [5].iter().cmp([1, 9].iter()),
        (1..4).eq([1, 2, 3].iter().cloned()),
        (1..4).lt(1..5),
        [2.0, f64::NAN].iter().le([2.0, 1.0].iter())
    );

    // Peekable: run-length encoding and merge of two sorted lists.
    let data = bb(vec![1, 1, 2, 3, 3, 3, 3, 1, 5, 5, 0, 0, 0]);
    let mut it = data.iter().peekable();
    let mut runs = Vec::new();
    while let Some(&x) = it.next() {
        let mut n = 1;
        while it.peek() == Some(&&x) {
            it.next();
            n += 1;
        }
        runs.push((x, n));
    }
    println!("rle {:?}", runs);
    let a = [1, 4, 4, 9, 12, 20];
    let b = [2, 4, 5, 13, 21, 22, 23];
    let (mut pa, mut pb) = (a.iter().peekable(), b.iter().peekable());
    let mut merged = Vec::new();
    loop {
        match (pa.peek(), pb.peek()) {
            (Some(&&x), Some(&&y)) => {
                if x <= y {
                    merged.push(x);
                    pa.next();
                } else {
                    merged.push(y);
                    pb.next();
                }
            }
            (Some(_), None) => merged.extend(pa.by_ref()),
            (None, Some(_)) => merged.extend(pb.by_ref()),
            (None, None) => break,
        }
    }
    println!("merged {:?}", merged);
    let mut pk = (1..10).peekable();
    let firsts: Vec<i32> = std::iter::from_fn(|| pk.next_if(|&x| x < 4)).collect();
    println!("next_if {:?} rest {:?}", firsts, pk.collect::<Vec<_>>());

    // Sizes and double-ended behaviour.
    let mut de = (1..11).map(|x| x * x);
    println!("size_hint {:?} back {:?} front {:?} back {:?} len {}", de.size_hint(), de.next_back(), de.next(), de.next_back(), de.len());
    let mut ch = "héllo wörld".chars();
    println!("chars back {:?} {:?} front {:?} rest {:?}", ch.next_back(), ch.next_back(), ch.next(), ch.as_str());
    let mut z = v.iter().zip(w.iter().rev());
    println!("zip next {:?} {:?} size {:?}", z.next(), z.next(), z.size_hint());

    // Nested loops as iterator chains: pythagorean triples, primes.
    let triples: Vec<(u32, u32, u32)> = (1..30u32)
        .flat_map(|a| (a..30).map(move |b| (a, b)))
        .filter_map(|(a, b)| {
            let c2 = a * a + b * b;
            let c = (c2 as f64).sqrt() as u32;
            if c * c == c2 && c < 40 {
                Some((a, b, c))
            } else {
                None
            }
        })
        .collect();
    println!("triples {:?}", triples);
    let primes: Vec<u32> = (2..120u32).filter(|&n| (2..n).take_while(|d| d * d <= n).all(|d| n % d != 0)).collect();
    println!("primes {:?}", primes);
    let sums: Vec<u64> = (1..=10u64).map(|n| (1..=n).map(|k| k * k * k).sum()).collect();
    println!("cube sums {:?}", sums);
    let fact: Vec<u64> = (1..=20u64).scan(1u64, |p, k| {
        *p *= k;
        Some(*p)
    }).collect();
    println!("factorials {:?}", fact);
    let digits: u32 = (1..=1000u32).map(|n| n.to_string().len() as u32).sum();
    println!("digits 1..1000 {}", digits);
    let rows: Vec<String> = (0..6).map(|r| (0..=r).map(|c| binom(r, c).to_string()).collect::<Vec<_>>().join(" ")).collect();
    for row in rows {
        println!("pascal {}", row);
    }
    println!("sum f64 {} product f32 {}", (1..=10).map(|x| 1.0 / x as f64).sum::<f64>(), (1..=6).map(|x| x as f32 * 0.5).product::<f32>());
    let mut sorted = w.clone();
    sorted.sort_unstable();
    sorted.dedup();
    println!("is_sorted {} {} sorted-dedup {:?}", w.is_sorted(), sorted.is_sorted(), sorted);
}

fn binom(n: u64, k: u64) -> u64 {
    (0..k).fold(1, |acc, i| acc * (n - i) / (i + 1))
}
