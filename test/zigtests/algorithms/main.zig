// Classic algorithms over small generated inputs, every intermediate printed.
//
// Dynamic programming over 2-D tables (longest common subsequence with the
// subsequence recovered by walking the table back, edit distance, 0/1
// knapsack with the chosen items, coin change counts in u64, longest
// increasing subsequence by patience sorting with binary search); graphs
// (BFS shortest paths on a grid maze with the path drawn, DFS topological
// order with cycle detection, Dijkstra with std.PriorityQueue, Floyd-Warshall,
// Kruskal's minimum spanning tree over a union-find with path compression);
// and number and string algorithms (a sieve, KMP failure function and search,
// fast exponentiation mod a prime, Fibonacci by 2x2 matrix power in u128).
//
// What this exercises in a compiler is the mix real programs have: nested
// loops over row-major tables with computed indices, data-dependent branches,
// an allocator-backed heap, and early exits -- each algorithm's answer is
// checked against an independent property where one exists and printed.
const std = @import("std");
const w = @import("wasi");

var rng: u32 = 0;
fn next() u32 {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

fn lcs(a: []const u8, b: []const u8) void {
    var t: [33][33]u16 = undefined;
    for (0..a.len + 1) |i| t[i][0] = 0;
    for (0..b.len + 1) |j| t[0][j] = 0;
    for (1..a.len + 1) |i| {
        for (1..b.len + 1) |j| {
            t[i][j] = if (a[i - 1] == b[j - 1]) t[i - 1][j - 1] + 1 else @max(t[i - 1][j], t[i][j - 1]);
        }
    }
    var out: [32]u8 = undefined;
    var n: usize = t[a.len][b.len];
    const len = n;
    var i = a.len;
    var j = b.len;
    while (i > 0 and j > 0) {
        if (a[i - 1] == b[j - 1]) {
            n -= 1;
            out[n] = a[i - 1];
            i -= 1;
            j -= 1;
        } else if (t[i - 1][j] >= t[i][j - 1]) i -= 1 else j -= 1;
    }
    w.printf("lcs {s} {s} = {d} \"{s}\"\n", .{ a, b, len, out[0..len] });
}

fn editDistance(a: []const u8, b: []const u8) u32 {
    var prev: [40]u32 = undefined;
    var cur: [40]u32 = undefined;
    for (0..b.len + 1) |j| prev[j] = @intCast(j);
    for (1..a.len + 1) |i| {
        cur[0] = @intCast(i);
        for (1..b.len + 1) |j| {
            const sub = prev[j - 1] + @intFromBool(a[i - 1] != b[j - 1]);
            cur[j] = @min(sub, prev[j] + 1, cur[j - 1] + 1);
        }
        @memcpy(prev[0 .. b.len + 1], cur[0 .. b.len + 1]);
    }
    return prev[b.len];
}

fn knapsack(weights: []const u32, values: []const u32, cap: u32) void {
    var t: [21][101]u32 = undefined;
    for (0..cap + 1) |c| t[0][c] = 0;
    for (1..weights.len + 1) |i| {
        for (0..cap + 1) |c| {
            t[i][c] = t[i - 1][c];
            if (weights[i - 1] <= c) t[i][c] = @max(t[i][c], t[i - 1][c - weights[i - 1]] + values[i - 1]);
        }
    }
    var c: usize = cap;
    var i = weights.len;
    var wsum: u32 = 0;
    var vsum: u32 = 0;
    w.printf("knapsack cap {d}: best {d}, items", .{ cap, t[weights.len][cap] });
    while (i > 0) : (i -= 1) {
        if (t[i][c] != t[i - 1][c]) {
            w.printf(" {d}(w{d},v{d})", .{ i - 1, weights[i - 1], values[i - 1] });
            c -= weights[i - 1];
            wsum += weights[i - 1];
            vsum += values[i - 1];
        }
    }
    w.printf(" -> weight {d} value {d}\n", .{ wsum, vsum });
}

fn coinWays(coins: []const u32, amount: usize) u64 {
    var ways: [501]u64 = undefined;
    @memset(ways[0 .. amount + 1], 0);
    ways[0] = 1;
    for (coins) |c| {
        var a: usize = c;
        while (a <= amount) : (a += 1) ways[a] += ways[a - c];
    }
    return ways[amount];
}

fn lis(xs: []const i32) usize {
    var tails: [64]i32 = undefined;
    var n: usize = 0;
    for (xs) |x| {
        // lower bound over tails[0..n]
        var lo: usize = 0;
        var hi: usize = n;
        while (lo < hi) {
            const mid = (lo + hi) / 2;
            if (tails[mid] < x) lo = mid + 1 else hi = mid;
        }
        tails[lo] = x;
        if (lo == n) n += 1;
    }
    return n;
}

// ---- graphs ----

const MW = 21;
const MH = 11;

fn maze() void {
    var g: [MH][MW]u8 = undefined;
    for (0..MH) |y| {
        for (0..MW) |x| {
            g[y][x] = if (x == 0 or y == 0 or x == MW - 1 or y == MH - 1) '#' else if (next() % 100 < 28) '#' else '.';
        }
    }
    g[1][1] = '.';
    g[MH - 2][MW - 2] = '.';
    var dist: [MH][MW]i32 = undefined;
    for (&dist) |*row| @memset(row, -1);
    var qx: [MH * MW]u8 = undefined;
    var qy: [MH * MW]u8 = undefined;
    var head: usize = 0;
    var tail: usize = 0;
    qx[tail] = 1;
    qy[tail] = 1;
    tail += 1;
    dist[1][1] = 0;
    const dirs = [4][2]i32{ .{ 1, 0 }, .{ 0, 1 }, .{ -1, 0 }, .{ 0, -1 } };
    while (head < tail) : (head += 1) {
        const x: i32 = qx[head];
        const y: i32 = qy[head];
        for (dirs) |d| {
            const nx: usize = @intCast(x + d[0]);
            const ny: usize = @intCast(y + d[1]);
            if (g[ny][nx] == '.' and dist[ny][nx] < 0) {
                dist[ny][nx] = dist[@intCast(y)][@intCast(x)] + 1;
                qx[tail] = @intCast(nx);
                qy[tail] = @intCast(ny);
                tail += 1;
            }
        }
    }
    const goal = dist[MH - 2][MW - 2];
    w.printf("maze: reached {d} cells, goal distance {d}\n", .{ tail, goal });
    if (goal > 0) {
        // Walk back from the goal along decreasing distance.
        var x: usize = MW - 2;
        var y: usize = MH - 2;
        while (dist[y][x] != 0) {
            g[y][x] = '*';
            for (dirs) |d| {
                const nx: usize = @intCast(@as(i32, @intCast(x)) + d[0]);
                const ny: usize = @intCast(@as(i32, @intCast(y)) + d[1]);
                if (dist[ny][nx] == dist[y][x] - 1) {
                    x = nx;
                    y = ny;
                    break;
                }
            }
        }
        g[y][x] = 'S';
    }
    for (&g) |*row| w.printf("  {s}\n", .{row});
}

fn topo() void {
    const N = 12;
    var adj: [N][N]bool = @splat(@splat(false));
    // A DAG: edges only from lower to higher index, then a relabelling.
    var perm: [N]u8 = undefined;
    for (&perm, 0..) |*p, i| p.* = @intCast(i);
    var i: usize = N - 1;
    while (i > 0) : (i -= 1) std.mem.swap(u8, &perm[i], &perm[next() % (i + 1)]);
    var edges: u32 = 0;
    for (0..N) |a| {
        for (a + 1..N) |b| {
            if (next() % 4 == 0) {
                adj[perm[a]][perm[b]] = true;
                edges += 1;
            }
        }
    }
    var state: [N]u8 = @splat(0);
    var order: [N]u8 = undefined;
    var n: usize = N;
    const Dfs = struct {
        fn visit(v: usize, ad: *const [N][N]bool, st: *[N]u8, ord: *[N]u8, k: *usize) bool {
            if (st[v] == 1) return false;
            if (st[v] == 2) return true;
            st[v] = 1;
            for (0..N) |u| {
                if (ad[v][u] and !visit(u, ad, st, ord, k)) return false;
            }
            st[v] = 2;
            k.* -= 1;
            ord[k.*] = @intCast(v);
            return true;
        }
    };
    var ok = true;
    for (0..N) |v| ok = ok and Dfs.visit(v, &adj, &state, &order, &n);
    w.printf("topo: {d} edges acyclic={} order", .{ edges, ok });
    for (order) |v| w.printf(" {d}", .{v});
    var pos: [N]usize = undefined;
    for (order, 0..) |v, k| pos[v] = k;
    var valid = true;
    for (0..N) |a| {
        for (0..N) |b| {
            if (adj[a][b] and pos[a] >= pos[b]) valid = false;
        }
    }
    w.printf(" valid={}\n", .{valid});
    // Add a back edge and expect a cycle.
    adj[order[N - 1]][order[0]] = true;
    state = @splat(0);
    n = N;
    ok = true;
    for (0..N) |v| ok = ok and Dfs.visit(v, &adj, &state, &order, &n);
    w.printf("topo with back edge: acyclic={}\n", .{ok});
}

const Edge = struct { to: u8, cost: u32 };
const QItem = struct { node: u8, dist: u32 };

fn qcmp(_: void, a: QItem, b: QItem) std.math.Order {
    if (a.dist != b.dist) return std.math.order(a.dist, b.dist);
    return std.math.order(a.node, b.node);
}

fn graphs(gpa: std.mem.Allocator) !void {
    const N = 16;
    const INF: u32 = std.math.maxInt(u32);
    var adj: [N][N]u32 = @splat(@splat(INF));
    var list: [N][8]Edge = undefined;
    var deg: [N]u8 = @splat(0);
    for (0..N) |a| {
        adj[a][a] = 0;
        var k: u32 = 0;
        while (k < 3) : (k += 1) {
            const b: u8 = @intCast(next() % N);
            const c = 1 + next() % 40;
            if (b == a or adj[a][b] != INF) continue;
            adj[a][b] = c;
            adj[b][a] = c;
            list[a][deg[a]] = .{ .to = b, .cost = c };
            deg[a] += 1;
            list[b][deg[b]] = .{ .to = @intCast(a), .cost = c };
            deg[b] += 1;
        }
    }
    // Dijkstra from 0 with a binary heap.
    var dist: [N]u32 = @splat(INF);
    var prev: [N]u8 = @splat(255);
    var pq = std.PriorityQueue(QItem, void, qcmp).empty;
    defer pq.deinit(gpa);
    dist[0] = 0;
    try pq.push(gpa, .{ .node = 0, .dist = 0 });
    var pops: u32 = 0;
    while (pq.pop()) |it| {
        pops += 1;
        if (it.dist > dist[it.node]) continue;
        for (list[it.node][0..deg[it.node]]) |e| {
            const nd = it.dist + e.cost;
            if (nd < dist[e.to]) {
                dist[e.to] = nd;
                prev[e.to] = it.node;
                try pq.push(gpa, .{ .node = e.to, .dist = nd });
            }
        }
    }
    w.printf("dijkstra: {d} pops\n", .{pops});
    // Floyd-Warshall must agree.
    var fw = adj;
    for (0..N) |k| {
        for (0..N) |a| {
            for (0..N) |b| {
                if (fw[a][k] != INF and fw[k][b] != INF and fw[a][k] + fw[k][b] < fw[a][b]) fw[a][b] = fw[a][k] + fw[k][b];
            }
        }
    }
    for (0..N) |v| {
        w.printf("  node {d:>2}: dist {d:>10} fw {d:>10} path", .{ v, dist[v], fw[0][v] });
        var p: u8 = @intCast(v);
        var hops: u32 = 0;
        while (p != 255 and hops < N) : (hops += 1) {
            w.printf(" {d}", .{p});
            p = prev[p];
        }
        w.puts("\n");
    }
    var rowsum: u64 = 0;
    for (fw) |row| for (row) |d| {
        if (d != INF) rowsum += d;
    };
    w.printf("floyd total {d}\n", .{rowsum});

    // Kruskal over the same graph.
    const KE = struct { a: u8, b: u8, c: u32 };
    var es: [N * N]KE = undefined;
    var ne: usize = 0;
    for (0..N) |a| {
        for (a + 1..N) |b| {
            if (adj[a][b] != INF) {
                es[ne] = .{ .a = @intCast(a), .b = @intCast(b), .c = adj[a][b] };
                ne += 1;
            }
        }
    }
    std.mem.sort(KE, es[0..ne], {}, struct {
        fn lt(_: void, x: KE, y: KE) bool {
            if (x.c != y.c) return x.c < y.c;
            if (x.a != y.a) return x.a < y.a;
            return x.b < y.b;
        }
    }.lt);
    var parent: [N]u8 = undefined;
    for (&parent, 0..) |*p, i| p.* = @intCast(i);
    const Uf = struct {
        fn find(par: *[N]u8, x: u8) u8 {
            var r = x;
            while (par[r] != r) r = par[r];
            var c = x;
            while (par[c] != r) {
                const nx = par[c];
                par[c] = r;
                c = nx;
            }
            return r;
        }
    };
    var total: u32 = 0;
    var used: u32 = 0;
    w.puts("kruskal:");
    for (es[0..ne]) |e| {
        const ra = Uf.find(&parent, e.a);
        const rb = Uf.find(&parent, e.b);
        if (ra != rb) {
            parent[ra] = rb;
            total += e.c;
            used += 1;
            w.printf(" {d}-{d}({d})", .{ e.a, e.b, e.c });
        }
    }
    w.printf("\n  {d} edges of {d}, weight {d}\n", .{ used, ne, total });
}

// ---- numbers and strings ----

fn sieve() void {
    var composite: [2000]bool = @splat(false);
    var count: u32 = 0;
    var sum: u64 = 0;
    var i: usize = 2;
    while (i < composite.len) : (i += 1) {
        if (composite[i]) continue;
        count += 1;
        sum += i;
        if (count <= 30 or count % 25 == 0) w.printf("prime #{d} = {d}\n", .{ count, i });
        var j = i * i;
        while (j < composite.len) : (j += i) composite[j] = true;
    }
    w.printf("primes below 2000: {d}, sum {d}\n", .{ count, sum });
}

fn kmp(text: []const u8, pat: []const u8) void {
    var fail: [32]usize = undefined;
    fail[0] = 0;
    var k: usize = 0;
    for (1..pat.len) |i| {
        while (k > 0 and pat[i] != pat[k]) k = fail[k - 1];
        if (pat[i] == pat[k]) k += 1;
        fail[i] = k;
    }
    w.printf("kmp \"{s}\" fail", .{pat});
    for (fail[0..pat.len]) |f| w.printf(" {d}", .{f});
    w.puts(" at");
    k = 0;
    var hits: u32 = 0;
    for (text, 0..) |c, i| {
        while (k > 0 and c != pat[k]) k = fail[k - 1];
        if (c == pat[k]) k += 1;
        if (k == pat.len) {
            hits += 1;
            w.printf(" {d}", .{i + 1 - pat.len});
            k = fail[k - 1];
        }
    }
    w.printf(" ({d} hits, {d} without overlaps)\n", .{ hits, std.mem.count(u8, text, pat) });
}

fn powmod(b0: u64, e0: u64, m: u64) u64 {
    var b = b0 % m;
    var e = e0;
    var r: u64 = 1;
    while (e > 0) : (e >>= 1) {
        if (e & 1 == 1) r = @intCast(@as(u128, r) * b % m);
        b = @intCast(@as(u128, b) * b % m);
    }
    return r;
}

fn fibMatrix(n: u32) u128 {
    var r = [4]u128{ 1, 0, 0, 1 };
    var m = [4]u128{ 1, 1, 1, 0 };
    var e = n;
    while (e > 0) : (e >>= 1) {
        // Through a temporary: an array literal assigned to `r` is written
        // into `r` element by element, so its later elements would read the
        // earlier ones already replaced.
        if (e & 1 == 1) {
            const t: [4]u128 = .{ r[0] * m[0] + r[1] * m[2], r[0] * m[1] + r[1] * m[3], r[2] * m[0] + r[3] * m[2], r[2] * m[1] + r[3] * m[3] };
            r = t;
        }
        if (e > 1) {
            const t: [4]u128 = .{ m[0] * m[0] + m[1] * m[2], m[0] * m[1] + m[1] * m[3], m[2] * m[0] + m[3] * m[2], m[2] * m[1] + m[3] * m[3] };
            m = t;
        }
    }
    return r[1];
}

var heap: [64 * 1024]u8 = undefined;

pub fn main() !void {
    rng = w.hide(@as(u32, 0xA5A5_1234));
    const words = [_][]const u8{ "AGGTAB", "GXTXAYB", "kitten", "sitting", "intention", "execution", "abcdefghij", "acegikmoq", "zigzagzig", "wasmwarmc", "", "a" };
    var i: usize = 0;
    while (i + 1 < words.len) : (i += 2) {
        const a = w.hide(words[i]);
        const b = w.hide(words[i + 1]);
        lcs(a, b);
        w.printf("edit {s} -> {s} = {d} (reverse {d})\n", .{ a, b, editDistance(a, b), editDistance(b, a) });
    }

    var weights: [20]u32 = undefined;
    var values: [20]u32 = undefined;
    for (&weights, &values) |*wt, *v| {
        wt.* = 1 + next() % 30;
        v.* = 1 + next() % 60;
    }
    for ([_]u32{ 0, 10, 37, 64, 100 }) |cap| knapsack(&weights, &values, w.hide(cap));

    const coin_sets = [_][]const u32{ &.{ 1, 2, 5 }, &.{ 2, 3, 7 }, &.{ 1, 5, 10, 25, 50 }, &.{ 3, 7, 11, 13 } };
    for (coin_sets) |cs| {
        for ([_]usize{ 0, 1, 11, 100, 257, 500 }) |amt| {
            w.printf("coins {any} amount {d}: {d} ways\n", .{ cs, amt, coinWays(w.hide(cs), w.hide(amt)) });
        }
    }

    var seq: [60]i32 = undefined;
    for (&seq) |*s| s.* = @as(i32, @intCast(next() % 200)) - 100;
    for ([_]usize{ 1, 5, 10, 20, 40, 60 }) |n| w.printf("lis of first {d}: {d}\n", .{ n, lis(seq[0..w.hide(n)]) });

    maze();
    topo();
    var fba = std.heap.FixedBufferAllocator.init(&heap);
    try graphs(fba.allocator());

    sieve();
    const text = w.hide("abababcabababcababcabcabcababababcab");
    kmp(text, "ababc");
    kmp(text, "abcab");
    kmp(text, "ab");
    kmp(text, "cabab");
    for ([_]u64{ 2, 3, 10, 12345, 999999 }) |b| {
        w.printf("powmod {d}: ^(p-1)={d} ^1000={d} ^(2^40)={d}\n", .{ b, powmod(w.hide(b), 1_000_000_006, 1_000_000_007), powmod(b, w.hide(@as(u64, 1000)), 1_000_000_007), powmod(b, @as(u64, 1) << 40, 998_244_353) });
    }
    var n: u32 = 0;
    while (n <= 180) : (n += 15) w.printf("fib({d}) = {d}\n", .{ n, fibMatrix(w.hide(n)) });
    w.puts("done\n");
}
