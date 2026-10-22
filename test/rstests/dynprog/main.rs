// Dynamic programming over 1-D and 2-D tables: longest common subsequence
// with reconstruction, edit distance with the operation script, 0/1 and
// unbounded knapsack, longest increasing subsequence (O(n log n) with
// predecessor links), coin change (ways and minimum coins), matrix-chain
// order with parenthesisation, longest palindromic substring, subset sum
// with a u64 bitset, rod cutting, grid paths with obstacles, Catalan and
// Bell numbers, egg dropping, word break, and the maximum-sum rectangle.
//
// These fill tables with min/max/add recurrences over indices offset by one,
// so they are dense in bounds-checked indexing, `i - 1` arithmetic and
// select-style min/max -- operations LLVM turns into conditional selects and
// that a native back end must lower to csel without inverting a condition.

use std::hint::black_box as bb;

fn lcs(a: &str, b: &str) -> (usize, String) {
    let a: Vec<char> = a.chars().collect();
    let b: Vec<char> = b.chars().collect();
    let mut t = vec![vec![0usize; b.len() + 1]; a.len() + 1];
    for i in 1..=a.len() {
        for j in 1..=b.len() {
            t[i][j] = if a[i - 1] == b[j - 1] { t[i - 1][j - 1] + 1 } else { t[i - 1][j].max(t[i][j - 1]) };
        }
    }
    let (mut i, mut j) = (a.len(), b.len());
    let mut out = vec![];
    while i > 0 && j > 0 {
        if a[i - 1] == b[j - 1] {
            out.push(a[i - 1]);
            i -= 1;
            j -= 1;
        } else if t[i - 1][j] >= t[i][j - 1] {
            i -= 1;
        } else {
            j -= 1;
        }
    }
    (t[a.len()][b.len()], out.iter().rev().collect())
}

fn edit(a: &str, b: &str) -> (usize, String) {
    let a: Vec<char> = a.chars().collect();
    let b: Vec<char> = b.chars().collect();
    let mut d = vec![vec![0usize; b.len() + 1]; a.len() + 1];
    for i in 0..=a.len() {
        d[i][0] = i;
    }
    for j in 0..=b.len() {
        d[0][j] = j;
    }
    for i in 1..=a.len() {
        for j in 1..=b.len() {
            let sub = d[i - 1][j - 1] + (a[i - 1] != b[j - 1]) as usize;
            d[i][j] = sub.min(d[i - 1][j] + 1).min(d[i][j - 1] + 1);
        }
    }
    let (mut i, mut j) = (a.len(), b.len());
    let mut ops = vec![];
    while i > 0 || j > 0 {
        if i > 0 && j > 0 && d[i][j] == d[i - 1][j - 1] + (a[i - 1] != b[j - 1]) as usize {
            ops.push(if a[i - 1] == b[j - 1] { '=' } else { 'S' });
            i -= 1;
            j -= 1;
        } else if i > 0 && d[i][j] == d[i - 1][j] + 1 {
            ops.push('D');
            i -= 1;
        } else {
            ops.push('I');
            j -= 1;
        }
    }
    (d[a.len()][b.len()], ops.iter().rev().collect())
}

fn knapsack01(items: &[(usize, u64)], cap: usize) -> (u64, Vec<usize>) {
    let n = items.len();
    let mut t = vec![vec![0u64; cap + 1]; n + 1];
    for i in 1..=n {
        let (w, v) = items[i - 1];
        for c in 0..=cap {
            t[i][c] = t[i - 1][c];
            if w <= c {
                t[i][c] = t[i][c].max(t[i - 1][c - w] + v);
            }
        }
    }
    let mut chosen = vec![];
    let mut c = cap;
    for i in (1..=n).rev() {
        if t[i][c] != t[i - 1][c] {
            chosen.push(i - 1);
            c -= items[i - 1].0;
        }
    }
    chosen.reverse();
    (t[n][cap], chosen)
}

fn knapsack_unbounded(items: &[(usize, u64)], cap: usize) -> Vec<u64> {
    let mut best = vec![0u64; cap + 1];
    for c in 1..=cap {
        for &(w, v) in items {
            if w <= c {
                best[c] = best[c].max(best[c - w] + v);
            }
        }
    }
    best
}

fn lis(v: &[i32]) -> Vec<i32> {
    let mut tails: Vec<usize> = vec![];
    let mut prev = vec![usize::MAX; v.len()];
    for i in 0..v.len() {
        let pos = tails.partition_point(|&t| v[t] < v[i]);
        if pos > 0 {
            prev[i] = tails[pos - 1];
        }
        if pos == tails.len() {
            tails.push(i);
        } else {
            tails[pos] = i;
        }
    }
    let mut out = vec![];
    let mut k = *tails.last().unwrap_or(&usize::MAX);
    while k != usize::MAX {
        out.push(v[k]);
        k = prev[k];
    }
    out.reverse();
    out
}

fn coins(coins: &[usize], amount: usize) -> (u64, Option<usize>) {
    let mut ways = vec![0u64; amount + 1];
    ways[0] = 1;
    for &c in coins {
        for a in c..=amount {
            ways[a] += ways[a - c];
        }
    }
    let mut min = vec![usize::MAX; amount + 1];
    min[0] = 0;
    for a in 1..=amount {
        for &c in coins {
            if c <= a && min[a - c] != usize::MAX {
                min[a] = min[a].min(min[a - c] + 1);
            }
        }
    }
    (ways[amount], if min[amount] == usize::MAX { None } else { Some(min[amount]) })
}

fn matrix_chain(dims: &[u64]) -> (u64, String) {
    let n = dims.len() - 1;
    let mut m = vec![vec![0u64; n]; n];
    let mut s = vec![vec![0usize; n]; n];
    for len in 2..=n {
        for i in 0..=n - len {
            let j = i + len - 1;
            m[i][j] = u64::MAX;
            for k in i..j {
                let cost = m[i][k] + m[k + 1][j] + dims[i] * dims[k + 1] * dims[j + 1];
                if cost < m[i][j] {
                    m[i][j] = cost;
                    s[i][j] = k;
                }
            }
        }
    }
    fn paren(s: &Vec<Vec<usize>>, i: usize, j: usize) -> String {
        if i == j {
            format!("A{}", i + 1)
        } else {
            format!("({} {})", paren(s, i, s[i][j]), paren(s, s[i][j] + 1, j))
        }
    }
    (m[0][n - 1], paren(&s, 0, n - 1))
}

fn longest_palindrome(s: &str) -> &str {
    let b = s.as_bytes();
    let n = b.len();
    if n == 0 {
        return s;
    }
    let mut p = vec![vec![false; n]; n];
    let (mut best_lo, mut best_len) = (0, 1);
    for i in (0..n).rev() {
        for j in i..n {
            p[i][j] = b[i] == b[j] && (j - i < 2 || p[i + 1][j - 1]);
            if p[i][j] && j - i + 1 > best_len {
                best_lo = i;
                best_len = j - i + 1;
            }
        }
    }
    &s[best_lo..best_lo + best_len]
}

fn subset_sums(v: &[usize]) -> Vec<u64> {
    let mut bits = vec![0u64; 4];
    bits[0] = 1;
    for &x in v {
        let (wshift, bshift) = (x / 64, x % 64);
        let mut next = bits.clone();
        for i in (0..bits.len()).rev() {
            if i < wshift {
                continue;
            }
            let src = i - wshift;
            let mut val = bits[src] << bshift;
            if bshift > 0 && src > 0 {
                val |= bits[src - 1] >> (64 - bshift);
            }
            next[i] |= val;
        }
        bits = next;
    }
    bits
}

fn rod_cut(prices: &[u32], n: usize) -> (u32, Vec<usize>) {
    let mut best = vec![0u32; n + 1];
    let mut cut = vec![0usize; n + 1];
    for len in 1..=n {
        for k in 1..=len.min(prices.len()) {
            let v = prices[k - 1] + best[len - k];
            if v > best[len] {
                best[len] = v;
                cut[len] = k;
            }
        }
    }
    let mut pieces = vec![];
    let mut l = n;
    while l > 0 {
        pieces.push(cut[l]);
        l -= cut[l];
    }
    (best[n], pieces)
}

fn grid_paths(grid: &[&str]) -> u64 {
    let h = grid.len();
    let w = grid[0].len();
    let mut t = vec![vec![0u64; w]; h];
    for r in 0..h {
        for c in 0..w {
            if grid[r].as_bytes()[c] == b'#' {
                continue;
            }
            t[r][c] = if r == 0 && c == 0 { 1 } else { (if r > 0 { t[r - 1][c] } else { 0 }) + (if c > 0 { t[r][c - 1] } else { 0 }) };
        }
    }
    t[h - 1][w - 1]
}

fn egg_drop(eggs: usize, floors: usize) -> usize {
    // dp[m][k] = floors coverable with m moves and k eggs
    let mut dp = vec![vec![0usize; eggs + 1]; floors + 1];
    let mut m = 0;
    while dp[m][eggs] < floors {
        m += 1;
        for k in 1..=eggs {
            dp[m][k] = dp[m - 1][k - 1] + dp[m - 1][k] + 1;
        }
    }
    m
}

fn word_break(s: &str, dict: &[&str]) -> Option<Vec<String>> {
    let n = s.len();
    let mut from = vec![usize::MAX; n + 1];
    from[0] = 0;
    for i in 1..=n {
        for w in dict {
            if w.len() <= i && from[i - w.len()] != usize::MAX && &s[i - w.len()..i] == *w {
                from[i] = i - w.len();
                break;
            }
        }
    }
    if from[n] == usize::MAX {
        return None;
    }
    let mut out = vec![];
    let mut i = n;
    while i > 0 {
        out.push(s[from[i]..i].to_string());
        i = from[i];
    }
    out.reverse();
    Some(out)
}

fn max_rect(m: &[Vec<i32>]) -> (i32, (usize, usize, usize, usize)) {
    let (h, w) = (m.len(), m[0].len());
    let mut best = (i32::MIN, (0, 0, 0, 0));
    for top in 0..h {
        let mut col = vec![0i32; w];
        for bot in top..h {
            for c in 0..w {
                col[c] += m[bot][c];
            }
            let (mut cur, mut start) = (0i32, 0usize);
            for c in 0..w {
                if cur <= 0 {
                    cur = col[c];
                    start = c;
                } else {
                    cur += col[c];
                }
                if cur > best.0 {
                    best = (cur, (top, start, bot, c));
                }
            }
        }
    }
    best
}

fn main() {
    let pairs = [("ABCBDAB", "BDCABA"), ("kitten", "sitting"), ("intention", "execution"), ("", "abc"), ("dynamic", "programming"), ("AGGTAB", "GXTXAYB"), ("sunday", "saturday")];
    for (a, b) in pairs {
        let (a, b) = (bb(a), bb(b));
        println!("lcs({:?}, {:?}) = {:?}  edit = {:?}", a, b, lcs(a, b), edit(a, b));
    }
    let items = [(12, 4), (2, 2), (1, 1), (1, 2), (4, 10), (3, 7), (7, 13), (5, 8)];
    for cap in [0, 5, 10, 15, 20, 35] {
        println!("knapsack01 cap {:2} -> {:?}", cap, knapsack01(&items, bb(cap)));
    }
    println!("unbounded {:?}", knapsack_unbounded(&[(3, 5), (4, 7), (7, 13)], 25));
    for v in [vec![10, 9, 2, 5, 3, 7, 101, 18], vec![0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15], vec![5, 4, 3], vec![]] {
        println!("lis {:?} -> {:?}", v, lis(&v));
    }
    for (cs, amt) in [(vec![1, 2, 5], 11), (vec![2], 3), (vec![1, 5, 10, 25, 50], 100), (vec![3, 7, 405, 436], 8839), (vec![186, 419, 83, 408], 6249)] {
        println!("coins {:?} {} -> {:?}", cs, amt, coins(&cs, bb(amt)));
    }
    for dims in [vec![10u64, 30, 5, 60], vec![40, 20, 30, 10, 30], vec![30, 35, 15, 5, 10, 20, 25], vec![5, 10]] {
        println!("matrix chain {:?} -> {:?}", dims, matrix_chain(&dims));
    }
    for s in ["babad", "cbbd", "forgeeksskeegfor", "a", "abacdfgdcaba", "racecar level"] {
        println!("palindrome {:?} -> {:?}", s, longest_palindrome(bb(s)));
    }
    let sums = subset_sums(&[3, 34, 4, 12, 5, 2]);
    let reach: Vec<usize> = (0..256).filter(|&i| sums[i / 64] >> (i % 64) & 1 == 1).collect();
    println!("subset sums reachable {} -> {:?}", reach.len(), reach);
    let primes_sums = subset_sums(&[2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37]);
    println!("prime subset sums count {} bits {:x?}", primes_sums.iter().map(|w| w.count_ones()).sum::<u32>(), primes_sums);
    let prices = [1, 5, 8, 9, 10, 17, 17, 20];
    for n in [1, 4, 8, 13, 20] {
        println!("rod {} -> {:?}", n, rod_cut(&prices, n));
    }
    println!("grid paths {} {}", grid_paths(&["....", "....", "...."]), grid_paths(&["...#....", ".#...#..", "....#...", "#.......", "...#..#."]));
    let catalan: Vec<u64> = {
        let mut c = vec![1u64; 20];
        for n in 1..20 {
            c[n] = (0..n).map(|i| c[i] * c[n - 1 - i]).sum();
        }
        c
    };
    println!("catalan {:?}", catalan);
    let mut bell_row = vec![1u64];
    let mut bells = vec![1u64];
    for _ in 0..15 {
        let mut next = vec![*bell_row.last().unwrap_or(&1)];
        for &x in &bell_row {
            let v = *next.last().unwrap_or(&0) + x;
            next.push(v);
        }
        bells.push(next[0]);
        bell_row = next;
    }
    println!("bell {:?}", bells);
    for (e, f) in [(1, 10), (2, 10), (2, 100), (3, 100), (3, 1000), (5, 10000)] {
        println!("egg drop {} eggs {} floors -> {} moves", e, f, egg_drop(e, f));
    }
    let dict = ["apple", "pen", "applepen", "pine", "pineapple", "cats", "dog", "sand", "and", "cat"];
    for s in ["applepenapple", "pineapplepenapple", "catsandog", "catsanddog", "pen"] {
        println!("word break {:?} -> {:?}", s, word_break(s, &dict));
    }
    let m = vec![vec![1, 2, -1, -4, -20], vec![-8, -3, 4, 2, 1], vec![3, 8, 10, 1, 3], vec![-4, -1, 1, 7, -6]];
    println!("max rect {:?}", max_rect(&m));
    let mut tri: Vec<Vec<u64>> = vec![vec![1]];
    for r in 1..25 {
        let prev = &tri[r - 1];
        let mut row = vec![1u64; r + 1];
        for c in 1..r {
            row[c] = prev[c - 1] + prev[c];
        }
        tri.push(row);
    }
    println!("pascal row 24 {:?}", tri[24]);
}
