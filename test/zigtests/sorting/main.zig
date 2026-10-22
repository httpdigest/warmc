// std's sorts and searches over arrays of sizes 0, 1, 2, 3, 7, 16, 31 and
// 100, with many duplicates.
//
// Every algorithm std exposes is run on the same inputs: std.mem.sort (the
// stable block sort), std.mem.sortUnstable (pattern-defeating quicksort),
// std.sort.insertion, std.sort.heap, std.sort.block and std.sort.pdq
// directly, and the index-based sortContext / sortUnstableContext /
// insertionContext / heapContext over parallel arrays through a context with
// `lessThan` and `swap`.  Comparators are asc/desc helpers, a key extractor,
// and a three-key record order.  Stability is checked by printing each
// record's original index after a stable sort on a coarse key: equal keys must
// come out in their input order.  The sorted results are searched with
// binarySearch, lowerBound, upperBound, equalRange and partitionPoint, and
// checked with isSorted, argMin/argMax and min/max.  pdq falls back to
// insertion sort below 24 elements and to heapsort on bad pivots, and the
// block sort has its own small-array path, so the sizes reach each branch.
const std = @import("std");
const w = @import("wasi");

const sizes = [_]usize{ 0, 1, 2, 3, 7, 16, 31, 100 };

var rng_state: u32 = 0;

fn rnd() u32 {
    var x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

fn fill(buf: []i32, modulus: u32) void {
    for (buf) |*v| v.* = @as(i32, @intCast(rnd() % modulus)) - @as(i32, @intCast(modulus / 2));
}

fn hashSlice(s: []const i32) u64 {
    var h: u64 = 1469598103934665603;
    for (s) |v| h = (h ^ @as(u32, @bitCast(v))) *% 1099511628211;
    return h;
}

fn printSlice(label: []const u8, s: []const i32) void {
    w.printf("{s} n={d}:", .{ label, s.len });
    if (s.len <= 31) {
        for (s) |v| w.printf(" {d}", .{v});
    } else {
        w.printf(" first={d} mid={d} last={d}", .{ s[0], s[s.len / 2], s[s.len - 1] });
    }
    w.printf(" hash={x}\n", .{hashSlice(s)});
}

const asc = std.sort.asc(i32);
const desc = std.sort.desc(i32);

fn byAbs(_: void, a: i32, b: i32) bool {
    return @abs(a) < @abs(b);
}

fn algorithms() void {
    var src: [100]i32 = undefined;
    var work: [100]i32 = undefined;
    rng_state = w.hide(@as(u32, 0x1234567));
    for (sizes) |n| {
        fill(src[0..n], if (n < 10) 7 else 40);
        printSlice("input", src[0..n]);
        @memcpy(work[0..n], src[0..n]);
        std.mem.sort(i32, work[0..n], {}, asc);
        printSlice("  mem.sort asc", work[0..n]);
        const ref = hashSlice(work[0..n]);
        @memcpy(work[0..n], src[0..n]);
        std.mem.sortUnstable(i32, work[0..n], {}, asc);
        w.printf("  sortUnstable same={}\n", .{hashSlice(work[0..n]) == ref});
        @memcpy(work[0..n], src[0..n]);
        std.sort.insertion(i32, work[0..n], {}, asc);
        w.printf("  insertion same={}\n", .{hashSlice(work[0..n]) == ref});
        @memcpy(work[0..n], src[0..n]);
        std.sort.heap(i32, work[0..n], {}, asc);
        w.printf("  heap same={}\n", .{hashSlice(work[0..n]) == ref});
        @memcpy(work[0..n], src[0..n]);
        std.sort.block(i32, work[0..n], {}, asc);
        w.printf("  block same={}\n", .{hashSlice(work[0..n]) == ref});
        @memcpy(work[0..n], src[0..n]);
        std.sort.pdq(i32, work[0..n], {}, asc);
        w.printf("  pdq same={} isSorted={}\n", .{ hashSlice(work[0..n]) == ref, std.sort.isSorted(i32, work[0..n], {}, asc) });
        @memcpy(work[0..n], src[0..n]);
        std.mem.sort(i32, work[0..n], {}, desc);
        printSlice("  desc", work[0..n]);
        w.printf("  isSorted asc={} desc={}\n", .{ std.sort.isSorted(i32, work[0..n], {}, asc), std.sort.isSorted(i32, work[0..n], {}, desc) });
        @memcpy(work[0..n], src[0..n]);
        std.mem.sort(i32, work[0..n], {}, byAbs);
        printSlice("  byAbs (stable)", work[0..n]);
        w.printf("  argMin={?d} argMax={?d} min={?d} max={?d}\n", .{
            std.sort.argMin(i32, src[0..n], {}, asc),
            std.sort.argMax(i32, src[0..n], {}, asc),
            std.sort.min(i32, src[0..n], {}, asc),
            std.sort.max(i32, src[0..n], {}, asc),
        });
    }
}

// Already sorted, reversed, all equal, organ pipe and sawtooth: the inputs
// that pdq detects or that would drive a naive quicksort quadratic.
fn patterns() void {
    var buf: [100]i32 = undefined;
    const names = [_][]const u8{ "sorted", "reversed", "equal", "organ", "saw", "fewuniq" };
    for (names, 0..) |name, p| {
        for (&buf, 0..) |*v, idx| {
            const i: i32 = @intCast(w.hide(idx));
            v.* = switch (p) {
                0 => i,
                1 => 100 - i,
                2 => 5,
                3 => if (i < 50) i else 100 - i,
                4 => @mod(i, 9),
                else => @mod(i * 7, 3),
            };
        }
        var a = buf;
        std.mem.sortUnstable(i32, &a, {}, asc);
        var b = buf;
        std.mem.sort(i32, &b, {}, asc);
        var c = buf;
        std.sort.heap(i32, &c, {}, desc);
        w.printf("pattern {s}: unstable={x} stable={x} heapdesc={x} eq={} sorted={}\n", .{ name, hashSlice(&a), hashSlice(&b), hashSlice(&c), std.mem.eql(i32, &a, &b), std.sort.isSorted(i32, &c, {}, desc) });
    }
}

const Rec = struct {
    dept: u8,
    salary: u32,
    name: []const u8,
    orig: u8,
};

fn byDept(_: void, a: Rec, b: Rec) bool {
    return a.dept < b.dept;
}

fn byDeptSalaryName(_: void, a: Rec, b: Rec) bool {
    if (a.dept != b.dept) return a.dept < b.dept;
    if (a.salary != b.salary) return a.salary > b.salary;
    return std.mem.lessThan(u8, a.name, b.name);
}

fn bySalaryThreshold(threshold: u32, a: Rec, b: Rec) bool {
    return @intFromBool(a.salary >= threshold) < @intFromBool(b.salary >= threshold);
}

fn records() void {
    const names = [_][]const u8{ "ada", "bob", "cyd", "dee", "eve", "fay", "gus", "hal", "ivy", "jan", "kim", "lou", "max", "ned", "oz", "pam", "quin", "rex", "sal", "tom", "uma", "vic", "wes", "xia", "yul", "zed" };
    var recs: [26]Rec = undefined;
    rng_state = w.hide(@as(u32, 4242));
    for (&recs, 0..) |*r, i| {
        r.* = .{ .dept = @intCast(rnd() % 4), .salary = 1000 + (rnd() % 5) * 250, .name = names[i], .orig = @intCast(i) };
    }
    var a = recs;
    std.mem.sort(Rec, &a, {}, byDept);
    w.puts("stable by dept (orig indices must rise within a dept):\n");
    var ok = true;
    for (a, 0..) |r, i| {
        w.printf("  d{d} {s} sal={d} orig={d}\n", .{ r.dept, r.name, r.salary, r.orig });
        if (i > 0 and a[i - 1].dept == r.dept and a[i - 1].orig > r.orig) ok = false;
    }
    w.printf("stable ok={}\n", .{ok});
    var b = recs;
    std.mem.sortUnstable(Rec, &b, {}, byDeptSalaryName);
    w.puts("by dept, salary desc, name:\n");
    for (b) |r| w.printf("  d{d} {d} {s}\n", .{ r.dept, r.salary, r.name });
    var c = recs;
    std.mem.sort(Rec, &c, w.hide(@as(u32, 1500)), bySalaryThreshold);
    w.puts("partitioned on salary>=1500 (stable):");
    for (c) |r| w.printf(" {s}", .{r.name});
    w.puts("\n");
    // Stable sort applied twice: by name then by dept gives dept-then-name.
    var d = recs;
    std.mem.sort(Rec, &d, {}, struct {
        fn f(_: void, x: Rec, y: Rec) bool {
            return x.salary < y.salary;
        }
    }.f);
    std.mem.sort(Rec, &d, {}, byDept);
    w.puts("salary then dept:");
    for (d) |r| w.printf(" {d}/{d}/{s}", .{ r.dept, r.salary, r.name });
    w.puts("\n");
}

// Parallel arrays sorted together by index: the context swaps both.
const Parallel = struct {
    keys: []u16,
    vals: []u8,
    pub fn lessThan(ctx: @This(), i: usize, j: usize) bool {
        return ctx.keys[i] < ctx.keys[j];
    }
    pub fn swap(ctx: @This(), i: usize, j: usize) void {
        std.mem.swap(u16, &ctx.keys[i], &ctx.keys[j]);
        std.mem.swap(u8, &ctx.vals[i], &ctx.vals[j]);
    }
};

fn contexts() void {
    var keys: [40]u16 = undefined;
    var vals: [40]u8 = undefined;
    const modes = [_][]const u8{ "sortContext", "sortUnstableContext", "insertionContext", "heapContext", "pdqContext" };
    for (modes, 0..) |mode, m| {
        rng_state = w.hide(@as(u32, 99));
        for (&keys, &vals, 0..) |*k, *v, i| {
            k.* = @intCast(rnd() % 13);
            v.* = 'A' + @as(u8, @intCast(i % 26));
        }
        const ctx = Parallel{ .keys = &keys, .vals = &vals };
        // Sort only the middle, to check the bounds are honoured.
        switch (m) {
            0 => std.mem.sortContext(5, 35, ctx),
            1 => std.mem.sortUnstableContext(5, 35, ctx),
            2 => std.sort.insertionContext(5, 35, ctx),
            3 => std.sort.heapContext(5, 35, ctx),
            else => std.sort.pdqContext(5, 35, ctx),
        }
        w.printf("{s}:", .{mode});
        for (keys, vals) |k, v| w.printf(" {d}{c}", .{ k, v });
        w.puts("\n");
    }
}

fn orderI32(key: i32, item: i32) std.math.Order {
    return std.math.order(key, item);
}

fn isNegative(_: void, x: i32) bool {
    return x < 0;
}

fn searching() void {
    var arr: [31]i32 = undefined;
    rng_state = w.hide(@as(u32, 777));
    fill(&arr, 20);
    std.mem.sort(i32, &arr, {}, asc);
    printSlice("search in", &arr);
    var key: i32 = -12;
    while (key <= 12) : (key += 1) {
        const k = w.hide(key);
        const er = std.sort.equalRange(i32, &arr, k, orderI32);
        w.printf("key {d}: bsearch={?d} lower={d} upper={d} range={d}..{d}\n", .{
            k,
            std.sort.binarySearch(i32, &arr, k, orderI32),
            std.sort.lowerBound(i32, &arr, k, orderI32),
            std.sort.upperBound(i32, &arr, k, orderI32),
            er[0],
            er[1],
        });
    }
    w.printf("partitionPoint(negative)={d}\n", .{std.sort.partitionPoint(i32, &arr, {}, isNegative)});
    const empty: []const i32 = &.{};
    w.printf("empty: bsearch={?d} lower={d} upper={d}\n", .{ std.sort.binarySearch(i32, empty, @as(i32, 3), orderI32), std.sort.lowerBound(i32, empty, @as(i32, 3), orderI32), std.sort.upperBound(i32, empty, @as(i32, 3), orderI32) });
}

fn strings() void {
    var words = [_][]const u8{ "pear", "Apple", "fig", "banana", "apple", "kiwi", "Fig", "date", "cherry", "elderberry", "grape", "apricot", "", "b" };
    const lex = struct {
        fn f(_: void, a: []const u8, b: []const u8) bool {
            return std.mem.lessThan(u8, a, b);
        }
    }.f;
    const ci = struct {
        fn f(_: void, a: []const u8, b: []const u8) bool {
            return std.ascii.lessThanIgnoreCase(a, b);
        }
    }.f;
    const byLen = struct {
        fn f(_: void, a: []const u8, b: []const u8) bool {
            return a.len < b.len;
        }
    }.f;
    std.mem.sort([]const u8, w.hide(@as([][]const u8, &words)), {}, lex);
    w.puts("lex:");
    for (words) |s| w.printf(" [{s}]", .{s});
    w.puts("\n");
    std.mem.sort([]const u8, &words, {}, ci);
    w.puts("ignore case (stable):");
    for (words) |s| w.printf(" [{s}]", .{s});
    w.puts("\n");
    std.mem.sort([]const u8, &words, {}, byLen);
    w.puts("by length (stable):");
    for (words) |s| w.printf(" [{s}]", .{s});
    w.puts("\n");
}

fn floats() void {
    var fs = [_]f64{ 3.5, -1.25, 0.0, 1e10, -1e-10, 2.0, 2.0, -0.5, 7.75, -100.0, 0.125 };
    std.mem.sort(f64, w.hide(@as([]f64, &fs)), {}, std.sort.asc(f64));
    w.puts("f64 asc:");
    for (fs) |f| w.printf(" {d}", .{f});
    w.puts("\n");
    std.mem.sortUnstable(f64, &fs, {}, std.sort.desc(f64));
    w.puts("f64 desc:");
    for (fs) |f| w.printf(" {d}", .{f});
    w.puts("\n");
    var us = [_]u64{ 0xffff_ffff_ffff_ffff, 0, 1 << 63, 1, (1 << 63) - 1, 42, 1 << 32 };
    std.mem.sort(u64, w.hide(@as([]u64, &us)), {}, std.sort.asc(u64));
    w.puts("u64 asc:");
    for (us) |u| w.printf(" {x}", .{u});
    w.puts("\n");
    var is = [_]i64{ std.math.minInt(i64), -1, 0, std.math.maxInt(i64), -(1 << 40), 1 << 40 };
    std.mem.sort(i64, w.hide(@as([]i64, &is)), {}, std.sort.asc(i64));
    w.puts("i64 asc:");
    for (is) |u| w.printf(" {d}", .{u});
    w.puts("\n");
}

pub fn main() void {
    algorithms();
    patterns();
    records();
    contexts();
    searching();
    strings();
    floats();
    w.puts("done\n");
}
