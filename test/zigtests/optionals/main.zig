// Optionals: ?T for integers, floats, structs, pointers and slices, and the
// control flow over them.
//
// An optional pointer or slice is the bare pointer with 0 for null, so its
// test is a compare against zero; an optional integer or struct carries a
// separate flag byte after the payload, so its test is a load of that byte at
// an offset that depends on the payload's size.  `??T` nests a second flag.
// The program covers `orelse` with a value, with `return`, `break` and
// `continue`; `if` and `while` captures, including pointer captures (`|*p|`)
// that write through into the optional's payload; `.?` where the value is
// known to be present; optional chains through a linked list; optionals as
// struct fields and array elements; comparisons with null and of two
// optionals; and functions returning optionals of each shape.  Every result is
// printed, so a flag read from the wrong offset or a payload written to the
// wrong place shows up.
const std = @import("std");
const w = @import("wasi");

const Node = struct {
    val: i32,
    next: ?*Node,
};

const Config = struct {
    name: ?[]const u8 = null,
    port: ?u16 = null,
    ratio: ?f64 = null,
    parent: ?*const Config = null,
    flags: [4]?bool = .{ null, null, null, null },
};

fn find(xs: []const i32, target: i32) ?usize {
    for (xs, 0..) |x, i| {
        if (x == target) return i;
    }
    return null;
}

fn firstNegative(xs: []const i32) ?*const i32 {
    for (xs) |*x| {
        if (x.* < 0) return x;
    }
    return null;
}

fn sqrtInt(x: i64) ?i64 {
    if (x < 0) return null;
    var r: i64 = 0;
    while ((r + 1) * (r + 1) <= x) r += 1;
    return r;
}

fn half(x: u32) ?u32 {
    return if (x % 2 == 0) x / 2 else null;
}

fn collatzSteps(start: u32) u32 {
    var n = start;
    var steps: u32 = 0;
    while (n != 1) : (steps += 1) {
        n = half(n) orelse 3 * n + 1;
    }
    return steps;
}

fn portOf(c: *const Config) ?u16 {
    var cur: ?*const Config = c;
    while (cur) |cc| : (cur = cc.parent) {
        if (cc.port) |p| return p;
    }
    return null;
}

fn nameOf(c: *const Config) []const u8 {
    return c.name orelse (if (c.parent) |p| nameOf(p) else "<anon>");
}

fn lookup(table: []const ?[]const u8, i: usize) ??[]const u8 {
    if (i >= table.len) return null; // outer null: no such slot
    return table[i]; // inner null: slot present but empty
}

fn sumPresent(xs: []const ?i16) i32 {
    var s: i32 = 0;
    for (xs) |x| s += x orelse continue;
    return s;
}

fn firstOver(xs: []const ?u8, limit: u8) ?u8 {
    for (xs) |mx| {
        const x = mx orelse break;
        if (x > limit) return x;
    }
    return null;
}

pub fn main() void {
    const data = [_]i32{ 4, 8, -15, 16, 23, -42, 0, 99 };
    const xs = w.hide(@as([]const i32, &data));
    const targets = [_]i32{ 4, 16, -42, 99, 7, 0, -1 };
    for (targets) |t0| {
        const t = w.hide(t0);
        const idx = find(xs, t);
        w.printf("find {d}: {?d} isNull={} orelse={d}\n", .{ t, idx, idx == null, idx orelse 999 });
        if (idx) |i| w.printf("  xs[{d}]={d}\n", .{ i, xs[i] });
    }
    if (firstNegative(xs)) |p| w.printf("firstNegative {d} at offset {d}\n", .{ p.*, (@intFromPtr(p) - @intFromPtr(xs.ptr)) / 4 });
    w.printf("firstNegative of positives null={}\n", .{firstNegative(xs[0..2]) == null});

    var opt_ints: [10]?u32 = undefined;
    for (&opt_ints, 0..) |*o, i| o.* = if (i % 3 == 1) null else w.hide(@as(u32, @intCast(i * i)));
    for (&opt_ints) |*o| {
        if (o.*) |*p| p.* += 1000; // write through the payload
    }
    for (opt_ints, 0..) |o, i| w.printf("opt_ints[{d}] = {?d}\n", .{ i, o });

    var vals = [_]i64{ -9, 0, 1, 2, 15, 16, 17, 1000000, 999999, -1 };
    for (&vals) |*v| {
        const r = sqrtInt(w.hide(v.*));
        w.printf("sqrtInt({d}) = {?d}\n", .{ v.*, r });
    }

    for (1..25) |i| w.printf("collatz({d}) = {d}\n", .{ i, collatzSteps(w.hide(@as(u32, @intCast(i)))) });

    // Linked list with optional next pointers.
    var nodes: [8]Node = undefined;
    for (&nodes, 0..) |*n, i| {
        n.* = .{ .val = w.hide(@as(i32, @intCast(i))) * 11 - 30, .next = if (i + 1 < nodes.len) &nodes[i + 1] else null };
    }
    nodes[3].next = &nodes[5]; // skip 4
    var cur: ?*Node = &nodes[0];
    var sum: i32 = 0;
    while (cur) |n| : (cur = n.next) {
        sum += n.val;
        w.printf("node {d} next={s}\n", .{ n.val, if (n.next == null) "null" else "set" });
    }
    w.printf("list sum {d}\n", .{sum});
    // Through a chain: the second node's next's next's value, or a default.
    const third = if (nodes[1].next) |a| (if (a.next) |b| b.val else null) else null;
    w.printf("chain third={?d}\n", .{third});
    w.printf("nodes[7].next null={} .? val of nodes[0].next={d}\n", .{ nodes[7].next == null, nodes[0].next.?.val });
    w.printf("sizes ?*Node={d} *Node={d} ?u32={d} ?u8={d} ?[]u8={d} ?f64={d} ??u8={d} ?bool={d}\n", .{
        @sizeOf(?*Node), @sizeOf(*Node), @sizeOf(?u32), @sizeOf(?u8), @sizeOf(?[]u8), @sizeOf(?f64), @sizeOf(??u8), @sizeOf(?bool),
    });

    const root = Config{ .name = "root", .port = 8080, .ratio = 0.5 };
    const mid = Config{ .parent = &root, .ratio = 1.25, .flags = .{ true, null, false, null } };
    const leaf = Config{ .name = "leaf", .parent = &mid, .flags = .{ null, true, null, true } };
    const lone = Config{};
    const cfgs = [_]*const Config{ &root, &mid, &leaf, &lone };
    for (cfgs, 0..) |c0, i| {
        const c = w.hide(c0);
        w.printf("cfg[{d}] name={s} port={?d} ratio={?d:.3} inherited-port={?d}\n", .{ i, nameOf(c), c.port, c.ratio, portOf(c) });
        for (c.flags, 0..) |f, j| {
            if (f) |b| w.printf("  flag[{d}]={}\n", .{ j, b }) else w.printf("  flag[{d}]=unset\n", .{j});
        }
    }

    const table = [_]?[]const u8{ "zero", null, "two", null, "four" };
    for (0..7) |ii| {
        const i = w.hide(ii);
        const r = lookup(&table, i);
        if (r) |inner| {
            if (inner) |s| w.printf("lookup {d}: \"{s}\"\n", .{ i, s }) else w.printf("lookup {d}: empty slot\n", .{i});
        } else w.printf("lookup {d}: out of range\n", .{i});
        w.printf("  isNull outer={} inner-or-null={}\n", .{ r == null, if (r) |inner| inner == null else true });
    }

    const shorts = [_]?i16{ 5, null, -300, 32767, null, -32768, 1 };
    w.printf("sumPresent={d} first4={d}\n", .{ sumPresent(w.hide(@as([]const ?i16, &shorts))), sumPresent(w.hide(@as([]const ?i16, shorts[0..4]))) });
    const bytes = [_]?u8{ 3, 9, 200, null, 255 };
    for ([_]u8{ 0, 5, 9, 199, 200, 254 }) |lim| {
        w.printf("firstOver({d}) = {?d}\n", .{ lim, firstOver(w.hide(@as([]const ?u8, &bytes)), w.hide(lim)) });
    }

    // Optionals compared and assigned.
    var a: ?u64 = w.hide(@as(?u64, 77));
    var b: ?u64 = null;
    w.printf("a==b {} a==77 {} b==null {}\n", .{ a == b, a == 77, b == null });
    b = a;
    w.printf("after b=a: a==b {} b={?d}\n", .{ a == b, b });
    a = null;
    w.printf("after a=null: a={?d} b={?d} a orelse b.? = {d}\n", .{ a, b, a orelse b.? });

    var maybe_f: ?f32 = w.hide(@as(?f32, -2.5));
    var it: u32 = 0;
    while (maybe_f) |f| : (it += 1) {
        w.printf("maybe_f iter {d}: {d:.4}\n", .{ it, f });
        maybe_f = if (f * f > 100) null else f * -1.75;
    }

    // An optional slice: null versus an empty but present slice.
    const empty: []const u8 = w.hide(@as([]const u8, ""));
    const slices = [_]?[]const u8{ null, empty, "abc", null, "z" };
    for (slices, 0..) |s, i| {
        if (s) |v| w.printf("slice[{d}] present len={d} \"{s}\"\n", .{ i, v.len, v }) else w.printf("slice[{d}] null\n", .{i});
    }

    // Labeled break out of a search with orelse.
    const grid = [_][4]?u8{ .{ 1, null, 3, 4 }, .{ null, null, 7, null }, .{ 9, 10, null, 12 } };
    var found: u32 = 0;
    outer: for (grid, 0..) |row, r| {
        for (row, 0..) |cell, c| {
            const v = cell orelse continue;
            if (v > 6) {
                w.printf("grid first >6 at ({d},{d}) = {d}\n", .{ r, c, v });
                found += 1;
                if (found == 2) break :outer;
            }
        }
    }
    w.printf("found={d}\n", .{found});
}
