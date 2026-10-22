// std::collections beyond Vec: VecDeque as a ring (push/pop at both ends,
// rotate, make_contiguous, range iteration, as_slices), BTreeMap with range
// queries, entry API, first/last/pop, split_off, retain; BTreeSet with set
// algebra and ranges; BinaryHeap as max-heap and with Reverse as min-heap,
// into_sorted_vec, peek_mut; HashMap and HashSet with output sorted (their
// iteration order is seeded from the WASI random source, so never printed raw)
// and a HashMap with a fixed BuildHasherDefault hasher; LinkedList.
//
// Each of these is a different allocation pattern: VecDeque wraps a head index
// around a buffer, BTreeMap allocates 11-key nodes and splits them, BinaryHeap
// sifts within a Vec, HashMap is SwissTable with 16-byte control groups probed
// by SIMD-like bit tricks on u64 words.

use std::cmp::Reverse;
use std::collections::hash_map::DefaultHasher;
use std::collections::{BTreeMap, BTreeSet, BinaryHeap, HashMap, HashSet, LinkedList, VecDeque};
use std::hash::{BuildHasherDefault, Hasher};
use std::hint::black_box as bb;
use std::ops::Bound::{Excluded, Included, Unbounded};

fn deque() {
    let mut d: VecDeque<i32> = VecDeque::with_capacity(4);
    for i in 0..6 {
        if i % 2 == 0 {
            d.push_back(i);
        } else {
            d.push_front(-i);
        }
        println!("deque push {} -> {:?}", i, d);
    }
    println!("front {:?} back {:?} get(2) {:?} len {}", d.front(), d.back(), d.get(2), d.len());
    d.rotate_left(2);
    println!("rotate_left 2 {:?}", d);
    d.rotate_right(3);
    println!("rotate_right 3 {:?}", d);
    let (a, b) = d.as_slices();
    println!("as_slices lens {} {} total {}", a.len(), b.len(), a.len() + b.len());
    d.make_contiguous().sort();
    println!("sorted contiguous {:?}", d);
    println!("range {:?}", d.range(1..4).collect::<Vec<_>>());
    println!("pop {:?} {:?} {:?}", d.pop_front(), d.pop_back(), d);
    d.insert(1, 100);
    d.remove(3);
    d.retain(|&x| x != 4);
    println!("insert/remove/retain {:?} contains 100 {}", d, d.contains(&100));
    d.extend([7, 8, 9]);
    let drained: Vec<i32> = d.drain(..2).collect();
    println!("drain {:?} rest {:?}", drained, d);
    // Sliding window maximum with a monotone deque.
    let data = [1, 3, -1, -3, 5, 3, 6, 7, 2, 2, 9, 0, 4];
    let k = 3;
    let mut q: VecDeque<usize> = VecDeque::new();
    let mut out = Vec::new();
    for i in 0..data.len() {
        while let Some(&j) = q.front() {
            if j + k <= i {
                q.pop_front();
            } else {
                break;
            }
        }
        while let Some(&j) = q.back() {
            if data[j] <= data[i] {
                q.pop_back();
            } else {
                break;
            }
        }
        q.push_back(i);
        if i + 1 >= k {
            out.push(data[q[0]]);
        }
    }
    println!("window max {:?}", out);
    // BFS over a grid with a deque.
    let grid = ["S.#.....", ".##.###.", "....#...", ".##...#E", "...##..."];
    let g: Vec<&[u8]> = grid.iter().map(|r| r.as_bytes()).collect();
    let (h, w) = (g.len(), g[0].len());
    let mut dist = vec![vec![-1i32; w]; h];
    let mut bfs = VecDeque::new();
    dist[0][0] = 0;
    bfs.push_back((0usize, 0usize));
    while let Some((r, c)) = bfs.pop_front() {
        let dirs: [(isize, isize); 4] = [(0, 1), (1, 0), (0, -1), (-1, 0)];
        for (dr, dc) in dirs {
            let nr = r as isize + dr;
            let nc = c as isize + dc;
            if nr < 0 || nc < 0 || nr >= h as isize || nc >= w as isize {
                continue;
            }
            let (nr, nc) = (nr as usize, nc as usize);
            if g[nr][nc] != b'#' && dist[nr][nc] < 0 {
                dist[nr][nc] = dist[r][c] + 1;
                bfs.push_back((nr, nc));
            }
        }
    }
    for row in &dist {
        println!("bfs {}", row.iter().map(|d| if *d < 0 { " ##".to_string() } else { format!("{:3}", d) }).collect::<String>());
    }
}

fn btree() {
    let mut m: BTreeMap<i32, String> = BTreeMap::new();
    for i in [50, 20, 80, 10, 30, 70, 90, 25, 35, 65, 5, 95, 60] {
        m.insert(bb(i), format!("v{}", i));
    }
    println!("map {:?}", m);
    println!("range 25..=65 {:?}", m.range(25..=65).map(|(k, _)| *k).collect::<Vec<_>>());
    println!("range ..30 {:?}", m.range(..30).map(|(k, _)| *k).collect::<Vec<_>>());
    println!("range (Excluded 30, Unbounded) {:?}", m.range((Excluded(30), Unbounded)).map(|(k, _)| *k).collect::<Vec<_>>());
    println!("range (Included 66, Excluded 90) {:?}", m.range((Included(66), Excluded(90))).map(|(k, _)| *k).collect::<Vec<_>>());
    println!("next below 63 {:?} next at/above 63 {:?}", m.range(..63).next_back(), m.range(63..).next());
    println!("first {:?} last {:?}", m.first_key_value(), m.last_key_value());
    println!("pop_first {:?} pop_last {:?}", m.pop_first(), m.pop_last());
    *m.entry(20).or_insert_with(String::new) += "+";
    m.entry(21).or_insert_with(|| "new21".into());
    m.entry(30).and_modify(|v| v.push_str("!")).or_default();
    if let Some(v) = m.get_mut(&70) {
        v.make_ascii_uppercase();
    }
    println!("after entries {:?}", m);
    let hi = m.split_off(&50);
    println!("split_off lo {:?}", m.keys().collect::<Vec<_>>());
    println!("split_off hi {:?}", hi.keys().collect::<Vec<_>>());
    let mut merged = m.clone();
    merged.extend(hi.into_iter().map(|(k, v)| (k * 10, v)));
    merged.retain(|k, _| k % 20 != 0);
    println!("merged retained {:?}", merged);
    println!("remove {:?} contains {} len {}", merged.remove(&21), merged.contains_key(&21), merged.len());

    // Word frequency, sorted by count then word.
    let text = "the quick brown fox jumps over the lazy dog the fox barks and the dog runs over the hill";
    let mut freq: BTreeMap<&str, usize> = BTreeMap::new();
    for w in text.split(' ') {
        *freq.entry(w).or_insert(0) += 1;
    }
    let mut by_count: Vec<(&str, usize)> = freq.iter().map(|(k, v)| (*k, *v)).collect();
    by_count.sort_by(|a, b| b.1.cmp(&a.1).then(a.0.cmp(b.0)));
    println!("freq {:?}", by_count);
    // Intervals keyed by start: find the one containing a point.
    let mut iv: BTreeMap<u32, (u32, &str)> = BTreeMap::new();
    iv.insert(0, (10, "a"));
    iv.insert(15, (20, "b"));
    iv.insert(20, (40, "c"));
    iv.insert(55, (56, "d"));
    for p in [0u32, 9, 10, 15, 19, 20, 39, 40, 55, 56, 99] {
        let hit = iv.range(..=p).next_back().filter(|(_, &(end, _))| p < end).map(|(_, &(_, n))| n);
        println!("interval at {} -> {:?}", p, hit);
    }

    let a: BTreeSet<u32> = (1..20).filter(|x| x % 2 == 0).collect();
    let b: BTreeSet<u32> = (1..20).filter(|x| x % 3 == 0).collect();
    println!("set a {:?} b {:?}", a, b);
    println!("union {:?}", a.union(&b).collect::<Vec<_>>());
    println!("intersection {:?}", a.intersection(&b).collect::<Vec<_>>());
    println!("difference {:?}", a.difference(&b).collect::<Vec<_>>());
    println!("symmetric {:?}", a.symmetric_difference(&b).collect::<Vec<_>>());
    println!("subset {} disjoint {} range {:?}", [6u32, 12].iter().cloned().collect::<BTreeSet<_>>().is_subset(&a), a.is_disjoint(&b), a.range(5..13).collect::<Vec<_>>());
    let mut s: BTreeSet<String> = ["delta", "alpha", "charlie", "bravo"].iter().map(|s| s.to_string()).collect();
    s.insert("aardvark".into());
    println!("string set {:?} first {:?} last {:?}", s, s.first(), s.last());
    println!("range from 'b' {:?}", s.range::<str, _>((Included("b"), Unbounded)).collect::<Vec<_>>());
}

fn heaps() {
    let mut h = BinaryHeap::new();
    for x in [5, 1, 8, 3, 9, 2, 7, 7, 0] {
        h.push(bb(x));
    }
    println!("max-heap peek {:?} len {}", h.peek(), h.len());
    let mut order = Vec::new();
    while let Some(x) = h.pop() {
        order.push(x);
    }
    println!("pop order {:?}", order);
    let mut mh: BinaryHeap<Reverse<(u32, &str)>> = BinaryHeap::new();
    for (p, n) in [(3, "c"), (1, "a"), (4, "d"), (1, "aa"), (5, "e"), (2, "b")] {
        mh.push(Reverse((p, n)));
    }
    let mut mins = Vec::new();
    while let Some(Reverse((p, n))) = mh.pop() {
        mins.push(format!("{}{}", p, n));
    }
    println!("min-heap order {:?}", mins);
    let h2: BinaryHeap<i32> = vec![4, -2, 11, 0, 6].into_iter().collect();
    println!("into_sorted_vec {:?}", h2.clone().into_sorted_vec());
    let mut h3 = h2;
    if let Some(mut top) = h3.peek_mut() {
        *top = -100;
    }
    println!("after peek_mut {:?}", h3.into_sorted_vec());
    // k smallest of a stream with a bounded max-heap.
    let mut s = 7u32;
    let mut keep: BinaryHeap<u32> = BinaryHeap::new();
    for _ in 0..500 {
        s = s.wrapping_mul(1664525).wrapping_add(1013904223);
        let x = s >> 20;
        keep.push(x);
        if keep.len() > 8 {
            keep.pop();
        }
    }
    println!("8 smallest {:?}", keep.into_sorted_vec());
    // Dijkstra on a small graph.
    let edges = [(0, 1, 7), (0, 2, 9), (0, 5, 14), (1, 2, 10), (1, 3, 15), (2, 3, 11), (2, 5, 2), (3, 4, 6), (4, 5, 9), (5, 6, 1), (6, 4, 3)];
    let n = 7;
    let mut adj = vec![Vec::new(); n];
    for &(a, b, w) in &edges {
        adj[a].push((b, w));
        adj[b].push((a, w));
    }
    let mut dist = vec![u32::MAX; n];
    let mut prev = vec![usize::MAX; n];
    let mut pq = BinaryHeap::new();
    dist[0] = 0;
    pq.push(Reverse((0u32, 0usize)));
    while let Some(Reverse((d, u))) = pq.pop() {
        if d > dist[u] {
            continue;
        }
        for &(v, w) in &adj[u] {
            let nd = d + w;
            if nd < dist[v] {
                dist[v] = nd;
                prev[v] = u;
                pq.push(Reverse((nd, v)));
            }
        }
    }
    for t in 0..n {
        let mut path = vec![t];
        let mut c = t;
        while prev[c] != usize::MAX {
            c = prev[c];
            path.push(c);
        }
        path.reverse();
        println!("dijkstra 0->{} dist {} path {:?}", t, dist[t], path);
    }
}

#[derive(Default)]
struct Fnv(u64);
impl Hasher for Fnv {
    fn finish(&self) -> u64 {
        self.0
    }
    fn write(&mut self, bytes: &[u8]) {
        if self.0 == 0 {
            self.0 = 0xcbf2_9ce4_8422_2325;
        }
        for &b in bytes {
            self.0 ^= b as u64;
            self.0 = self.0.wrapping_mul(0x0100_0000_01b3);
        }
    }
}

fn hashes() {
    let mut hm: HashMap<String, Vec<u32>> = HashMap::new();
    let words = ["apple", "avocado", "banana", "blueberry", "cherry", "cranberry", "date", "apricot", "bilberry", "clementine"];
    for (i, w) in words.iter().enumerate() {
        hm.entry(w[..1].to_string()).or_default().push(i as u32);
    }
    let mut keys: Vec<_> = hm.iter().collect();
    keys.sort();
    println!("hashmap sorted {:?}", keys);
    println!("get a {:?} get z {:?} len {}", hm.get("a"), hm.get("z"), hm.len());
    if let Some(v) = hm.get_mut("b") {
        v.retain(|x| x % 2 == 0);
    }
    hm.remove("d");
    let mut after: Vec<(String, Vec<u32>)> = hm.into_iter().collect();
    after.sort();
    println!("after mutate {:?}", after);

    let mut big: HashMap<u64, u64> = HashMap::new();
    for i in 0..2000u64 {
        *big.entry(i * i % 997).or_insert(0) += i;
    }
    let mut total = 0u64;
    let mut maxk = 0;
    for (&k, &v) in &big {
        total = total.wrapping_add(k.wrapping_mul(v));
        maxk = maxk.max(k);
    }
    println!("big len {} weighted {} max key {} get(4) {:?}", big.len(), total, maxk, big.get(&4));
    big.retain(|k, _| k % 3 == 0);
    let mut ks: Vec<_> = big.keys().cloned().collect();
    ks.sort_unstable();
    println!("retained {} first {:?}", ks.len(), &ks[..8.min(ks.len())]);

    let a: HashSet<char> = "hello world".chars().collect();
    let b: HashSet<char> = "lower case".chars().collect();
    let mut i: Vec<_> = a.intersection(&b).collect();
    i.sort();
    let mut u: Vec<_> = a.union(&b).cloned().collect();
    u.sort();
    let mut dif: Vec<_> = a.difference(&b).collect();
    dif.sort();
    println!("set inter {:?} union {:?} diff {:?}", i, u, dif);
    println!("contains {} {} insert-dup {}", a.contains(&'w'), a.contains(&'z'), a.clone().insert('h'));

    // A fixed hasher: same output every run, so iteration order is stable.
    let mut fixed: HashMap<&str, i32, BuildHasherDefault<Fnv>> = HashMap::default();
    for (i, w) in words.iter().enumerate() {
        fixed.insert(w, i as i32 * 3);
    }
    let order: Vec<_> = fixed.iter().map(|(k, v)| format!("{}={}", k, v)).collect();
    println!("fnv map order {:?}", order);
    let mut fh = Fnv::default();
    fh.write(b"warmc");
    println!("fnv hash {:016x}", fh.finish());
    let mut dh = DefaultHasher::new();
    dh.write(b"warmc");
    dh.write_u32(7);
    println!("default hasher {:016x}", dh.finish());
}

fn lists() {
    let mut l: LinkedList<i32> = LinkedList::new();
    for i in 0..5 {
        l.push_back(i * i);
        l.push_front(-i);
    }
    println!("linked {:?} len {}", l, l.len());
    let mut tail = l.split_off(4);
    println!("split {:?} | {:?}", l, tail);
    tail.append(&mut l);
    println!("append {:?} left {:?}", tail, l);
    println!("pop {:?} {:?} contains 9 {}", tail.pop_front(), tail.pop_back(), tail.contains(&9));
    for x in tail.iter_mut() {
        *x *= 2;
    }
    println!("iter_mut {:?} rev {:?}", tail, tail.iter().rev().collect::<Vec<_>>());
}

fn main() {
    deque();
    btree();
    heaps();
    hashes();
    lists();
}
