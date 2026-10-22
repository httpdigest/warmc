// Dynamic programming, the table-filling loops whose inner bodies are a
// min/max over neighbouring cells: longest common subsequence with the
// subsequence recovered by walking the table back, edit distance (Levenshtein)
// on strings, 0/1 knapsack in a rolling 1-D array (iterated backwards, so a
// wrong loop direction shows), coin change counted in i64, longest
// increasing subsequence by patience sorting with binary search, matrix-chain
// multiplication order with its parenthesisation, and the number of lattice
// paths through a grid with obstacles.  Every table is indexed with computed
// offsets into a flat StaticArray or typed array, and the answers are printed
// alongside the reconstructed solutions.
import { print } from "../wasi";

function lcs(a: string, b: string): string {
  let n = a.length, m = b.length;
  let t = new Int32Array((n + 1) * (m + 1));
  for (let i = 1; i <= n; i++)
    for (let j = 1; j <= m; j++)
      t[i * (m + 1) + j] = a.charCodeAt(i - 1) == b.charCodeAt(j - 1)
        ? t[(i - 1) * (m + 1) + j - 1] + 1
        : max(t[(i - 1) * (m + 1) + j], t[i * (m + 1) + j - 1]);
  let out = "";
  let i = n, j = m;
  while (i > 0 && j > 0) {
    if (a.charCodeAt(i - 1) == b.charCodeAt(j - 1)) { out = a.charAt(i - 1) + out; i--; j--; }
    else if (t[(i - 1) * (m + 1) + j] >= t[i * (m + 1) + j - 1]) i--;
    else j--;
  }
  return t[n * (m + 1) + m].toString() + ":" + out;
}

function edit(a: string, b: string): i32 {
  let m = b.length;
  let prev = new Int32Array(m + 1), cur = new Int32Array(m + 1);
  for (let j = 0; j <= m; j++) prev[j] = j;
  for (let i = 1; i <= a.length; i++) {
    cur[0] = i;
    for (let j = 1; j <= m; j++) {
      let cost = a.charCodeAt(i - 1) == b.charCodeAt(j - 1) ? 0 : 1;
      cur[j] = min(min(prev[j] + 1, cur[j - 1] + 1), prev[j - 1] + cost);
    }
    let t = prev; prev = cur; cur = t;
  }
  return prev[m];
}

function knapsack(w: i32[], v: i32[], cap: i32): string {
  let best = new Int32Array(cap + 1);
  let take = new Uint8Array(w.length * (cap + 1));
  for (let i = 0; i < w.length; i++) {
    for (let c = cap; c >= w[i]; c--) {
      let with_ = best[c - w[i]] + v[i];
      if (with_ > best[c]) { best[c] = with_; take[i * (cap + 1) + c] = 1; }
    }
  }
  let items = "";
  let c = cap;
  for (let i = w.length - 1; i >= 0; i--) if (take[i * (cap + 1) + c]) { items = i.toString() + " " + items; c -= w[i]; }
  return best[cap].toString() + " items " + items;
}

function coinWays(coins: i32[], amount: i32): i64 {
  let ways = new Int64Array(amount + 1);
  ways[0] = 1;
  for (let k = 0; k < coins.length; k++)
    for (let a = coins[k]; a <= amount; a++) ways[a] += ways[a - coins[k]];
  return ways[amount];
}

function minCoins(coins: i32[], amount: i32): i32 {
  let best = new Int32Array(amount + 1);
  for (let a = 1; a <= amount; a++) {
    best[a] = 0x3fffffff;
    for (let k = 0; k < coins.length; k++) if (coins[k] <= a) best[a] = min(best[a], best[a - coins[k]] + 1);
  }
  return best[amount] >= 0x3fffffff ? -1 : best[amount];
}

function lis(a: i32[]): string {
  let tails: i32[] = [];
  let tailIdx: i32[] = [];
  let parent = new Int32Array(a.length);
  for (let i = 0; i < a.length; i++) {
    let lo = 0, hi = tails.length;
    while (lo < hi) { let mid = (lo + hi) >>> 1; if (tails[mid] < a[i]) lo = mid + 1; else hi = mid; }
    if (lo == tails.length) { tails.push(a[i]); tailIdx.push(i); } else { tails[lo] = a[i]; tailIdx[lo] = i; }
    parent[i] = lo > 0 ? tailIdx[lo - 1] : -1;
  }
  let seq: i32[] = [];
  let k = tailIdx.length > 0 ? tailIdx[tailIdx.length - 1] : -1;
  while (k >= 0) { seq.unshift(a[k]); k = parent[k]; }
  return tails.length.toString() + " " + seq.join(",");
}

let splitAt: Int32Array = new Int32Array(0);
let chainN = 0;
function paren(i: i32, j: i32): string {
  if (i == j) return "M" + i.toString();
  let k = splitAt[i * chainN + j];
  return "(" + paren(i, k) + paren(k + 1, j) + ")";
}
function matrixChain(dims: i32[]): string {
  let n = dims.length - 1;
  chainN = n;
  let cost = new Int64Array(n * n);
  splitAt = new Int32Array(n * n);
  for (let len = 2; len <= n; len++) {
    for (let i = 0; i + len - 1 < n; i++) {
      let j = i + len - 1;
      cost[i * n + j] = 0x7fffffffffffffff;
      for (let k = i; k < j; k++) {
        let c = cost[i * n + k] + cost[(k + 1) * n + j] + <i64>dims[i] * <i64>dims[k + 1] * <i64>dims[j + 1];
        if (c < cost[i * n + j]) { cost[i * n + j] = c; splitAt[i * n + j] = k; }
      }
    }
  }
  return cost[n - 1].toString() + " " + paren(0, n - 1);
}

function gridPaths(r: i32, c: i32, seed: u32): string {
  let g = new Uint64Array(r * c);
  let x = seed;
  let blocked = 0;
  for (let i = 0; i < r; i++) {
    for (let j = 0; j < c; j++) {
      x = x * 1103515245 + 12345;
      let wall = (i + j) > 0 && (i != r - 1 || j != c - 1) && ((x >> 16) % 7 == 0);
      if (wall) { blocked++; g[i * c + j] = 0; continue; }
      if (i == 0 && j == 0) { g[0] = 1; continue; }
      let up: u64 = i > 0 ? g[(i - 1) * c + j] : 0;
      let left: u64 = j > 0 ? g[i * c + j - 1] : 0;
      g[i * c + j] = up + left;
    }
  }
  return g[r * c - 1].toString() + " blocked=" + blocked.toString();
}

function run(): void {
  const pairs = [
    "ABCBDAB", "BDCABA", "AGGTAB", "GXTXAYB", "kitten", "sitting", "intention", "execution",
    "the quick brown fox", "the quack brown box", "", "abc", "assemblyscript", "typescript",
    "GATTACAGATTACAGATTACA", "TAGACCATAGACCATTAGAC",
  ];
  for (let i = 0; i + 1 < pairs.length; i += 2) {
    print("lcs '" + pairs[i] + "' '" + pairs[i + 1] + "' = " + lcs(pairs[i], pairs[i + 1]) +
      " edit=" + edit(pairs[i], pairs[i + 1]).toString() + " rev-edit=" + edit(pairs[i + 1], pairs[i]).toString());
  }
  let w: i32[] = [], v: i32[] = [];
  let x: u32 = 17;
  for (let i = 0; i < 24; i++) { x = x * 1664525 + 1013904223; w.push(<i32>(x >> 27) + 1); v.push(<i32>((x >> 9) & 63) + 1); }
  print("weights " + w.join(",") + " values " + v.join(","));
  for (let cap = 10; cap <= 90; cap += 20) print("knapsack cap=" + cap.toString() + " best=" + knapsack(w, v, cap));
  const coinSets: i32[][] = [[1, 2, 5], [2, 5, 3, 6], [1, 5, 10, 25, 50, 100], [7, 13], [3, 7, 11]];
  for (let s = 0; s < coinSets.length; s++) {
    let line = "coins " + coinSets[s].join("+") + ":";
    for (let a = 0; a <= 300; a += 37) line += " " + a.toString() + "=" + coinWays(coinSets[s], a).toString() + "/" + minCoins(coinSets[s], a).toString();
    print(line);
  }
  print("coins big " + coinWays([1, 2, 5, 10, 20, 50, 100, 200], 2000).toString());
  let seq: i32[] = [];
  x = 99;
  for (let i = 0; i < 60; i++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; seq.push(<i32>(x % 100)); }
  print("seq " + seq.join(","));
  print("lis " + lis(seq) + " | " + lis([5, 4, 3, 2, 1]) + " | " + lis([1, 2, 3]) + " | " + lis([]));
  print("chain " + matrixChain([40, 20, 30, 10, 30]) + " | " + matrixChain([10, 20, 30, 40, 30]) + " | " +
    matrixChain([30, 35, 15, 5, 10, 20, 25]) + " | " + matrixChain([5, 100, 3, 80, 7, 60, 9, 40]));
  for (let s: u32 = 1; s <= 5; s++) print("grid " + (6 + s * 4).toString() + "x" + (8 + s * 5).toString() + " paths=" + gridPaths(6 + s * 4, 8 + s * 5, s));
}
run();
