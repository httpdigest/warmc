// Sorting and searching written by hand, generic over T: Ord: insertion sort,
// shell sort, quicksort (Lomuto and Hoare partitions, median of three),
// merge sort top-down and bottom-up, heap sort, LSD radix sort on u32 and
// counting sort on u8, plus binary search variants (lower/upper bound,
// exponential search), quickselect, KMP and Z-function substring search,
// two-pointer and sliding-window searches, and inversion counting via merge.
//
// Every sort is checked against `slice::sort` on the same input and the
// first few elements printed, for several input shapes (random, sorted,
// reversed, all-equal, organ-pipe, few distinct values).  These are tight
// loops of loads, compares and swaps with data-dependent branches, the kind
// of code where an address computation or a signed/unsigned compare picked
// wrong breaks the order without crashing.

use std::fmt::Debug;
use std::hint::black_box as bb;

fn insertion<T: Ord + Copy>(v: &mut [T]) {
    for i in 1..v.len() {
        let x = v[i];
        let mut j = i;
        while j > 0 && v[j - 1] > x {
            v[j] = v[j - 1];
            j -= 1;
        }
        v[j] = x;
    }
}

fn shell<T: Ord + Copy>(v: &mut [T]) {
    let mut gap = 1;
    while gap < v.len() / 3 {
        gap = gap * 3 + 1;
    }
    while gap > 0 {
        for i in gap..v.len() {
            let x = v[i];
            let mut j = i;
            while j >= gap && v[j - gap] > x {
                v[j] = v[j - gap];
                j -= gap;
            }
            v[j] = x;
        }
        gap /= 3;
    }
}

fn quick_lomuto<T: Ord + Copy>(v: &mut [T]) {
    if v.len() <= 1 {
        return;
    }
    let p = v.len() - 1;
    let mut store = 0;
    for i in 0..p {
        if v[i] < v[p] {
            v.swap(i, store);
            store += 1;
        }
    }
    v.swap(store, p);
    let (l, r) = v.split_at_mut(store);
    quick_lomuto(l);
    quick_lomuto(&mut r[1..]);
}

fn quick_hoare<T: Ord + Copy>(v: &mut [T]) {
    if v.len() <= 16 {
        insertion(v);
        return;
    }
    let mid = v.len() / 2;
    let last = v.len() - 1;
    // Median of three into v[mid].
    if v[mid] < v[0] {
        v.swap(mid, 0);
    }
    if v[last] < v[0] {
        v.swap(last, 0);
    }
    if v[last] < v[mid] {
        v.swap(last, mid);
    }
    let pivot = v[mid];
    let (mut i, mut j) = (0usize, last);
    loop {
        while v[i] < pivot {
            i += 1;
        }
        while v[j] > pivot {
            j -= 1;
        }
        if i >= j {
            break;
        }
        v.swap(i, j);
        i += 1;
        j -= 1;
    }
    let (l, r) = v.split_at_mut(j + 1);
    quick_hoare(l);
    quick_hoare(r);
}

fn merge_sort<T: Ord + Copy>(v: &mut [T], buf: &mut Vec<T>) {
    let n = v.len();
    if n <= 1 {
        return;
    }
    let mid = n / 2;
    merge_sort(&mut v[..mid], buf);
    merge_sort(&mut v[mid..], buf);
    buf.clear();
    let (mut i, mut j) = (0, mid);
    while i < mid && j < n {
        if v[j] < v[i] {
            buf.push(v[j]);
            j += 1;
        } else {
            buf.push(v[i]);
            i += 1;
        }
    }
    buf.extend_from_slice(&v[i..mid]);
    buf.extend_from_slice(&v[j..n]);
    v.copy_from_slice(buf);
}

fn merge_bottom_up<T: Ord + Copy + Default>(v: &mut [T]) {
    let n = v.len();
    let mut src = v.to_vec();
    let mut dst = vec![T::default(); n];
    let mut width = 1;
    while width < n {
        let mut lo = 0;
        while lo < n {
            let mid = (lo + width).min(n);
            let hi = (lo + 2 * width).min(n);
            let (mut i, mut j, mut k) = (lo, mid, lo);
            while k < hi {
                if i < mid && (j >= hi || src[i] <= src[j]) {
                    dst[k] = src[i];
                    i += 1;
                } else {
                    dst[k] = src[j];
                    j += 1;
                }
                k += 1;
            }
            lo = hi;
        }
        std::mem::swap(&mut src, &mut dst);
        width *= 2;
    }
    v.copy_from_slice(&src);
}

fn heap_sort<T: Ord + Copy>(v: &mut [T]) {
    fn sift<T: Ord + Copy>(v: &mut [T], mut root: usize, end: usize) {
        loop {
            let mut child = 2 * root + 1;
            if child >= end {
                break;
            }
            if child + 1 < end && v[child] < v[child + 1] {
                child += 1;
            }
            if v[root] >= v[child] {
                break;
            }
            v.swap(root, child);
            root = child;
        }
    }
    let n = v.len();
    for i in (0..n / 2).rev() {
        sift(v, i, n);
    }
    for end in (1..n).rev() {
        v.swap(0, end);
        sift(v, 0, end);
    }
}

fn radix_u32(v: &mut Vec<u32>) {
    let mut buf = vec![0u32; v.len()];
    for shift in [0u32, 8, 16, 24] {
        let mut count = [0usize; 257];
        for &x in v.iter() {
            count[((x >> shift) & 0xff) as usize + 1] += 1;
        }
        for i in 0..256 {
            count[i + 1] += count[i];
        }
        for &x in v.iter() {
            let b = ((x >> shift) & 0xff) as usize;
            buf[count[b]] = x;
            count[b] += 1;
        }
        std::mem::swap(v, &mut buf);
    }
}

fn counting_u8(v: &mut [u8]) {
    let mut c = [0usize; 256];
    for &x in v.iter() {
        c[x as usize] += 1;
    }
    let mut k = 0;
    for (val, &n) in c.iter().enumerate() {
        for _ in 0..n {
            v[k] = val as u8;
            k += 1;
        }
    }
}

fn lower_bound<T: Ord>(v: &[T], x: &T) -> usize {
    let (mut lo, mut hi) = (0, v.len());
    while lo < hi {
        let mid = lo + (hi - lo) / 2;
        if v[mid] < *x {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    lo
}

fn upper_bound<T: Ord>(v: &[T], x: &T) -> usize {
    let (mut lo, mut hi) = (0, v.len());
    while lo < hi {
        let mid = lo + (hi - lo) / 2;
        if v[mid] <= *x {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    lo
}

fn exp_search(v: &[i64], x: i64) -> Option<usize> {
    if v.is_empty() {
        return None;
    }
    let mut bound = 1;
    while bound < v.len() && v[bound] < x {
        bound *= 2;
    }
    let lo = bound / 2;
    let hi = (bound + 1).min(v.len());
    let i = lo + lower_bound(&v[lo..hi], &x);
    if i < v.len() && v[i] == x {
        Some(i)
    } else {
        None
    }
}

fn quickselect(v: &mut [i64], k: usize) -> i64 {
    let (mut lo, mut hi) = (0, v.len() - 1);
    loop {
        if lo == hi {
            return v[lo];
        }
        let p = v[(lo + hi) / 2];
        let (mut i, mut j) = (lo, hi);
        while i <= j {
            while v[i] < p {
                i += 1;
            }
            while v[j] > p {
                j -= 1;
            }
            if i <= j {
                v.swap(i, j);
                i += 1;
                if j == 0 {
                    break;
                }
                j -= 1;
            }
        }
        if k <= j {
            hi = j;
        } else if k >= i {
            lo = i;
        } else {
            return v[k];
        }
    }
}

fn kmp(hay: &[u8], pat: &[u8]) -> Vec<usize> {
    let mut fail = vec![0usize; pat.len()];
    let mut k = 0;
    for i in 1..pat.len() {
        while k > 0 && pat[i] != pat[k] {
            k = fail[k - 1];
        }
        if pat[i] == pat[k] {
            k += 1;
        }
        fail[i] = k;
    }
    let mut out = vec![];
    k = 0;
    for (i, &c) in hay.iter().enumerate() {
        while k > 0 && c != pat[k] {
            k = fail[k - 1];
        }
        if c == pat[k] {
            k += 1;
        }
        if k == pat.len() {
            out.push(i + 1 - k);
            k = fail[k - 1];
        }
    }
    out
}

fn z_function(s: &[u8]) -> Vec<usize> {
    let n = s.len();
    let mut z = vec![0; n];
    let (mut l, mut r) = (0, 0);
    for i in 1..n {
        if i < r {
            z[i] = (r - i).min(z[i - l]);
        }
        while i + z[i] < n && s[z[i]] == s[i + z[i]] {
            z[i] += 1;
        }
        if i + z[i] > r {
            l = i;
            r = i + z[i];
        }
    }
    z
}

fn inversions(v: &mut [i32]) -> u64 {
    let n = v.len();
    if n < 2 {
        return 0;
    }
    let mid = n / 2;
    let mut c = inversions(&mut v[..mid]) + inversions(&mut v[mid..]);
    let mut merged = Vec::with_capacity(n);
    let (mut i, mut j) = (0, mid);
    while i < mid && j < n {
        if v[i] <= v[j] {
            merged.push(v[i]);
            i += 1;
        } else {
            c += (mid - i) as u64;
            merged.push(v[j]);
            j += 1;
        }
    }
    merged.extend_from_slice(&v[i..mid]);
    merged.extend_from_slice(&v[j..]);
    v.copy_from_slice(&merged);
    c
}

fn shapes(n: usize, seed: u64) -> Vec<(&'static str, Vec<i64>)> {
    let mut s = seed;
    let mut rnd = move || {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        s
    };
    let random: Vec<i64> = (0..n).map(|_| (rnd() % 2001) as i64 - 1000).collect();
    let sorted: Vec<i64> = (0..n as i64).collect();
    let reversed: Vec<i64> = (0..n as i64).rev().collect();
    let equal = vec![7i64; n];
    let organ: Vec<i64> = (0..n as i64).map(|i| if i < n as i64 / 2 { i } else { n as i64 - i }).collect();
    let few: Vec<i64> = (0..n).map(|_| (rnd() % 4) as i64).collect();
    vec![("random", random), ("sorted", sorted), ("reversed", reversed), ("equal", equal), ("organ", organ), ("few", few)]
}

fn check<T: Ord + Copy + Debug>(name: &str, shape: &str, got: &[T], want: &[T]) {
    println!("{:>10} {:>8}: ok {} head {:?}", name, shape, got == want, &got[..got.len().min(6)]);
}

fn main() {
    for n in [0usize, 1, 2, 13, 100, 777] {
        println!("--- n = {}", n);
        for (shape, data) in shapes(n, bb(0x9e37_79b9_7f4a_7c15 ^ n as u64)) {
            let mut want = data.clone();
            want.sort();
            let mut a = data.clone();
            insertion(&mut a);
            check("insertion", shape, &a, &want);
            let mut a = data.clone();
            shell(&mut a);
            check("shell", shape, &a, &want);
            if shape != "equal" && shape != "sorted" && shape != "reversed" || n < 200 {
                let mut a = data.clone();
                quick_lomuto(&mut a);
                check("lomuto", shape, &a, &want);
            }
            let mut a = data.clone();
            quick_hoare(&mut a);
            check("hoare", shape, &a, &want);
            let mut a = data.clone();
            merge_sort(&mut a, &mut Vec::new());
            check("merge", shape, &a, &want);
            let mut a = data.clone();
            merge_bottom_up(&mut a);
            check("merge_bu", shape, &a, &want);
            let mut a = data.clone();
            heap_sort(&mut a);
            check("heap", shape, &a, &want);
        }
    }
    let mut s = bb(12345u32);
    let mut r: Vec<u32> = (0..1000).map(|_| {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        s
    }).collect();
    let mut want = r.clone();
    want.sort_unstable();
    radix_u32(&mut r);
    println!("radix ok {} first {:?} last {:?}", r == want, &r[..3], &r[997..]);
    let mut bytes: Vec<u8> = "the quick brown fox jumps over the lazy dog".bytes().collect();
    counting_u8(&mut bytes);
    println!("counting {:?}", String::from_utf8_lossy(&bytes));
    let mut words = vec!["pear", "fig", "apple", "kiwi", "banana", "date", "cherry", "fig"];
    heap_sort(&mut words);
    println!("heap sort strs {:?}", words);
    let mut tuples: Vec<(u8, char)> = vec![(3, 'c'), (1, 'z'), (3, 'a'), (2, 'm'), (1, 'a')];
    quick_hoare(&mut tuples);
    println!("hoare tuples {:?}", tuples);

    let v: Vec<i64> = (0..60).map(|i| (i / 3) * 5).collect();
    for x in [-1i64, 0, 5, 7, 50, 95, 96, 200] {
        println!("bounds {:3}: lower {:2} upper {:2} exp {:?} std {:?}", x, lower_bound(&v, &x), upper_bound(&v, &x), exp_search(&v, x), v.binary_search(&x).is_ok());
    }
    let base: Vec<i64> = shapes(101, 77)[0].1.clone();
    let mut sorted = base.clone();
    sorted.sort();
    for k in [0usize, 1, 50, 99, 100] {
        let mut w = base.clone();
        println!("quickselect k={} -> {} (sorted {})", k, quickselect(&mut w, k), sorted[k]);
    }
    let hay = b"abracadabra abracadabra cadabra";
    for pat in [&b"abra"[..], b"cad", b"a", b"abracadabra", b"zzz", b"ra c"] {
        println!("kmp {:?} -> {:?}", String::from_utf8_lossy(pat), kmp(hay, pat));
    }
    println!("z {:?}", z_function(b"aabxaabxcaabxaabxay"));
    let mut inv = vec![8, 4, 2, 1, 7, 3, 9, 0, 5, 6];
    println!("inversions {} sorted {:?}", inversions(&mut inv), inv);
    let mut rev: Vec<i32> = (0..500).rev().collect();
    println!("inversions of reversed 500 {}", inversions(&mut rev));
    // Two-sum on sorted data with two pointers; longest window with <= k distinct.
    let sorted2: Vec<i32> = vec![-8, -3, -1, 0, 2, 4, 5, 9, 11, 15];
    for target in [1, 6, 26, 100, -11] {
        let (mut i, mut j) = (0, sorted2.len() - 1);
        let mut found = None;
        while i < j {
            let s = sorted2[i] + sorted2[j];
            if s == target {
                found = Some((sorted2[i], sorted2[j]));
                break;
            } else if s < target {
                i += 1;
            } else {
                j -= 1;
            }
        }
        println!("two-sum {} -> {:?}", target, found);
    }
    let text = b"abcbbbbcccbdddadacb";
    for k in 1..=3 {
        let mut count = [0usize; 256];
        let (mut distinct, mut lo, mut best, mut at) = (0, 0, 0, 0);
        for hi in 0..text.len() {
            if count[text[hi] as usize] == 0 {
                distinct += 1;
            }
            count[text[hi] as usize] += 1;
            while distinct > k {
                count[text[lo] as usize] -= 1;
                if count[text[lo] as usize] == 0 {
                    distinct -= 1;
                }
                lo += 1;
            }
            if hi + 1 - lo > best {
                best = hi + 1 - lo;
                at = lo;
            }
        }
        println!("window k={} best {} {:?}", k, best, String::from_utf8_lossy(&text[at..at + best]));
    }
}
