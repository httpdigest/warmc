// Recursion, and data structures that are recursive through pointers.
//
// Ordinary (non-tail) recursion keeps its state on the wasm value stack and in
// LLVM's shadow stack in linear memory, `__stack_pointer`, which a function
// with an address-taken local adjusts on entry and restores on every exit.  So
// this exercises calls whose depth is data-dependent, shadow-stack frames that
// nest a few thousand deep, and results combined after the call returns:
// Ackermann, Towers of Hanoi, the permutations and subsets of a small set,
// N-queens with a board array passed down by pointer, a flood fill over a
// grid, and mutual recursion.  The second half builds a binary search tree on
// a FixedBufferAllocator -- insert, lookup, delete with the in-order
// successor, rotations to rebalance it as an AVL tree -- and walks it
// recursively, printing it in order and as a bracketed shape after each phase.
// Recursive merge sort on a linked list closes it off.
const std = @import("std");
const w = @import("wasi");

fn ackermann(m: u64, n: u64) u64 {
    if (m == 0) return n + 1;
    if (n == 0) return ackermann(m - 1, 1);
    return ackermann(m - 1, ackermann(m, n - 1));
}

var hanoi_moves: u64 = 0;
var hanoi_hash: u64 = 0;
fn hanoi(n: u32, from: u8, to: u8, via: u8) void {
    if (n == 0) return;
    hanoi(n - 1, from, via, to);
    hanoi_moves += 1;
    hanoi_hash = hanoi_hash *% 31 +% (@as(u64, from) * 3 + to);
    if (hanoi_moves <= 8) w.printf("  move {d}: disk {d} {c}->{c}\n", .{ hanoi_moves, n, from, to });
    hanoi(n - 1, via, to, from);
}

var perm_count: u32 = 0;
fn permute(items: []u8, k: usize) void {
    if (k == items.len) {
        perm_count += 1;
        if (perm_count <= 10 or perm_count % 97 == 0) w.printf("  perm {d}: {s}\n", .{ perm_count, items });
        return;
    }
    var i = k;
    while (i < items.len) : (i += 1) {
        std.mem.swap(u8, &items[k], &items[i]);
        permute(items, k + 1);
        std.mem.swap(u8, &items[k], &items[i]);
    }
}

fn subsets(set: []const i32, idx: usize, chosen: *std.ArrayList(i32), target: i32, buf: []u8, found: *u32) void {
    if (idx == set.len) {
        var sum: i32 = 0;
        for (chosen.items) |v| sum += v;
        if (sum == target) {
            found.* += 1;
            var n: usize = 0;
            for (chosen.items) |v| {
                const s = std.mem.print(buf[n..], "{d} ", .{v}) catch unreachable;
                n += s.len;
            }
            w.printf("  subset sum {d}: {s}\n", .{ target, buf[0..n] });
        }
        return;
    }
    subsets(set, idx + 1, chosen, target, buf, found);
    chosen.appendAssumeCapacity(set[idx]);
    subsets(set, idx + 1, chosen, target, buf, found);
    _ = chosen.pop();
}

fn queens(n: usize, row: usize, cols: *[12]usize) u32 {
    if (row == n) return 1;
    var total: u32 = 0;
    var c: usize = 0;
    while (c < n) : (c += 1) {
        var ok = true;
        var r: usize = 0;
        while (r < row) : (r += 1) {
            const pc = cols[r];
            if (pc == c or pc + (row - r) == c or c + (row - r) == pc) {
                ok = false;
                break;
            }
        }
        if (ok) {
            cols[row] = c;
            total += queens(n, row + 1, cols);
        }
    }
    return total;
}

const GW = 24;
const GH = 12;
fn flood(grid: *[GH][GW]u8, x: usize, y: usize, from: u8, to: u8) u32 {
    if (grid[y][x] != from) return 0;
    grid[y][x] = to;
    var n: u32 = 1;
    if (x > 0) n += flood(grid, x - 1, y, from, to);
    if (x + 1 < GW) n += flood(grid, x + 1, y, from, to);
    if (y > 0) n += flood(grid, x, y - 1, from, to);
    if (y + 1 < GH) n += flood(grid, x, y + 1, from, to);
    return n;
}

fn isEven(n: u32) bool {
    return if (n == 0) true else isOdd(n - 1);
}
fn isOdd(n: u32) bool {
    return if (n == 0) false else isEven(n - 1);
}

fn catalan(n: u32) u64 {
    if (n <= 1) return 1;
    var s: u64 = 0;
    var i: u32 = 0;
    while (i < n) : (i += 1) s += catalan(i) * catalan(n - 1 - i);
    return s;
}

// ---- AVL tree ----

const Node = struct {
    key: i32,
    val: u32,
    height: i32 = 1,
    left: ?*Node = null,
    right: ?*Node = null,
};

fn height(n: ?*Node) i32 {
    return if (n) |p| p.height else 0;
}

fn fix(n: *Node) void {
    n.height = 1 + @max(height(n.left), height(n.right));
}

fn rotRight(y: *Node) *Node {
    const x = y.left.?;
    y.left = x.right;
    x.right = y;
    fix(y);
    fix(x);
    return x;
}

fn rotLeft(x: *Node) *Node {
    const y = x.right.?;
    x.right = y.left;
    y.left = x;
    fix(x);
    fix(y);
    return y;
}

fn balance(n: *Node) *Node {
    fix(n);
    const bf = height(n.left) - height(n.right);
    if (bf > 1) {
        if (height(n.left.?.left) < height(n.left.?.right)) n.left = rotLeft(n.left.?);
        return rotRight(n);
    }
    if (bf < -1) {
        if (height(n.right.?.right) < height(n.right.?.left)) n.right = rotRight(n.right.?);
        return rotLeft(n);
    }
    return n;
}

fn insert(gpa: std.mem.Allocator, root: ?*Node, key: i32, val: u32) !*Node {
    const n = root orelse {
        const nn = try gpa.create(Node);
        nn.* = .{ .key = key, .val = val };
        return nn;
    };
    if (key < n.key) {
        n.left = try insert(gpa, n.left, key, val);
    } else if (key > n.key) {
        n.right = try insert(gpa, n.right, key, val);
    } else {
        n.val +%= val;
        return n;
    }
    return balance(n);
}

fn minNode(n: *Node) *Node {
    return if (n.left) |l| minNode(l) else n;
}

fn remove(gpa: std.mem.Allocator, root: ?*Node, key: i32) ?*Node {
    const n = root orelse return null;
    if (key < n.key) {
        n.left = remove(gpa, n.left, key);
    } else if (key > n.key) {
        n.right = remove(gpa, n.right, key);
    } else {
        if (n.left == null or n.right == null) {
            const child = n.left orelse n.right;
            gpa.destroy(n);
            return child;
        }
        const succ = minNode(n.right.?);
        n.key = succ.key;
        n.val = succ.val;
        n.right = remove(gpa, n.right, succ.key);
    }
    return balance(n);
}

fn lookup(n: ?*Node, key: i32) ?u32 {
    const p = n orelse return null;
    if (key < p.key) return lookup(p.left, key);
    if (key > p.key) return lookup(p.right, key);
    return p.val;
}

fn count(n: ?*Node) u32 {
    const p = n orelse return 0;
    return 1 + count(p.left) + count(p.right);
}

fn checkAvl(n: ?*Node, lo: i64, hi: i64) bool {
    const p = n orelse return true;
    if (p.key <= lo or p.key >= hi) return false;
    const bf = height(p.left) - height(p.right);
    if (bf > 1 or bf < -1) return false;
    if (p.height != 1 + @max(height(p.left), height(p.right))) return false;
    return checkAvl(p.left, lo, p.key) and checkAvl(p.right, p.key, hi);
}

const Out = struct {
    buf: [8192]u8 = undefined,
    len: usize = 0,
    fn add(o: *Out, comptime f: []const u8, args: anytype) void {
        const s = std.mem.print(o.buf[o.len..], f, args) catch return;
        o.len += s.len;
    }
};

fn shape(n: ?*Node, o: *Out) void {
    const p = n orelse {
        o.add(".", .{});
        return;
    };
    if (p.left == null and p.right == null) {
        o.add("{d}", .{p.key});
        return;
    }
    o.add("({d} ", .{p.key});
    shape(p.left, o);
    o.add(" ", .{});
    shape(p.right, o);
    o.add(")", .{});
}

fn inorder(n: ?*Node, o: *Out) void {
    const p = n orelse return;
    inorder(p.left, o);
    o.add("{d}:{d} ", .{ p.key, p.val });
    inorder(p.right, o);
}

fn report(label: []const u8, root: ?*Node) void {
    var o: Out = .{};
    shape(root, &o);
    w.printf("{s}: count={d} height={d} avl={}\n  shape {s}\n", .{ label, count(root), height(root), checkAvl(root, std.math.minInt(i64), std.math.maxInt(i64)), o.buf[0..o.len] });
    o.len = 0;
    inorder(root, &o);
    w.printf("  inorder {s}\n", .{o.buf[0..o.len]});
}

// ---- linked-list merge sort ----

const Cell = struct { v: i32, next: ?*Cell };

fn split(head: *Cell) ?*Cell {
    var slow = head;
    var fast = head.next;
    while (fast) |f| {
        fast = f.next;
        if (fast) |f2| {
            fast = f2.next;
            slow = slow.next.?;
        }
    }
    const rest = slow.next;
    slow.next = null;
    return rest;
}

fn merge(a: ?*Cell, b: ?*Cell) ?*Cell {
    const x = a orelse return b;
    const y = b orelse return a;
    if (x.v <= y.v) {
        x.next = merge(x.next, y);
        return x;
    }
    y.next = merge(x, y.next);
    return y;
}

fn msort(head: ?*Cell) ?*Cell {
    const h = head orelse return null;
    if (h.next == null) return h;
    const rest = split(h);
    return merge(msort(h), msort(rest));
}

var heap_buf: [64 * 1024]u8 = undefined;

pub fn main() !void {
    var m: u64 = 0;
    while (m <= 3) : (m += 1) {
        var n: u64 = 0;
        while (n <= 5) : (n += 1) {
            w.printf("ack({d},{d}) = {d}\n", .{ m, n, ackermann(w.hide(m), w.hide(n)) });
        }
    }

    var disks: u32 = 1;
    while (disks <= 12) : (disks += 3) {
        hanoi_moves = 0;
        hanoi_hash = 0;
        w.printf("hanoi {d}:\n", .{disks});
        hanoi(w.hide(disks), 'A', 'C', 'B');
        w.printf("  total {d} hash {x}\n", .{ hanoi_moves, hanoi_hash });
    }

    var letters = w.hide([5]u8{ 'a', 'b', 'c', 'd', 'e' });
    permute(&letters, 0);
    w.printf("permutations: {d}, back to {s}\n", .{ perm_count, &letters });

    const set = w.hide([_]i32{ 3, 34, 4, 12, 5, 2, -7, 9, 11 });
    var chosen_buf: [16]i32 = undefined;
    var chosen = std.ArrayList(i32).initBuffer(&chosen_buf);
    var line: [256]u8 = undefined;
    for ([_]i32{ 9, 20, 0, 41 }) |t| {
        var found: u32 = 0;
        subsets(&set, 0, &chosen, w.hide(t), &line, &found);
        w.printf("subsets summing to {d}: {d}\n", .{ t, found });
    }

    var qn: usize = 1;
    while (qn <= 9) : (qn += 1) {
        var cols: [12]usize = undefined;
        w.printf("queens({d}) = {d}\n", .{ qn, queens(w.hide(qn), 0, &cols) });
    }

    var grid: [GH][GW]u8 = undefined;
    var x: u32 = w.hide(@as(u32, 4321));
    for (&grid) |*row| {
        for (row) |*c| {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            c.* = if (x % 10 < 6) '.' else '#';
        }
    }
    var region: u8 = 'a';
    for (0..GH) |yy| {
        for (0..GW) |xx| {
            if (grid[yy][xx] == '.') {
                const size = flood(&grid, xx, yy, '.', region);
                w.printf("region {c} at ({d},{d}) size {d}\n", .{ region, xx, yy, size });
                region = if (region == 'z') 'A' else region + 1;
            }
        }
    }
    for (&grid) |*row| w.printf("  {s}\n", .{row});

    var k: u32 = 0;
    while (k < 30) : (k += 7) w.printf("even({d})={} odd({d})={}\n", .{ k * 37, isEven(w.hide(k * 37)), k * 37 + 1, isOdd(w.hide(k * 37 + 1)) });
    k = 0;
    while (k <= 15) : (k += 1) w.printf("catalan({d}) = {d}\n", .{ k, catalan(w.hide(k)) });

    var fba = std.heap.FixedBufferAllocator.init(&heap_buf);
    const gpa = fba.allocator();
    var root: ?*Node = null;
    var i: i32 = 0;
    while (i < 40) : (i += 1) {
        root = try insert(gpa, root, w.hide(i), @intCast(i * 10));
        if (i == 6 or i == 14) report("ascending", root);
    }
    report("ascending 40", root);
    x = 777;
    i = 0;
    while (i < 60) : (i += 1) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        root = try insert(gpa, root, @as(i32, @intCast(x % 200)) - 50, 1);
    }
    report("random 60", root);
    for ([_]i32{ -50, 0, 7, 39, 100, 149, 150, -51, 22 }) |key| {
        w.printf("lookup {d}: {?d}\n", .{ key, lookup(root, w.hide(key)) });
    }
    i = 0;
    while (i < 40) : (i += 3) root = remove(gpa, root, w.hide(i));
    report("after removes", root);
    i = -60;
    while (i < 160) : (i += 1) {
        if (@mod(i, 4) != 1) root = remove(gpa, root, w.hide(i));
    }
    report("mostly removed", root);

    var cells: [50]Cell = undefined;
    var head: ?*Cell = null;
    for (&cells, 0..) |*c, ci| {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        c.* = .{ .v = @as(i32, @intCast(x % 1000)) - 500, .next = head };
        _ = ci;
        head = c;
    }
    head = msort(head);
    var o: Out = .{};
    var cur = head;
    var sorted = true;
    var prev: i32 = std.math.minInt(i32);
    while (cur) |c| : (cur = c.next) {
        o.add("{d} ", .{c.v});
        if (c.v < prev) sorted = false;
        prev = c.v;
    }
    w.printf("msort sorted={}: {s}\n", .{ sorted, o.buf[0..o.len] });
    w.puts("done\n");
}
