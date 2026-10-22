// std.ArrayList, the unmanaged list of 0.17 that takes its allocator on every
// call, over three element types: u32, a 12-byte record and u8.
//
// Every mutating operation is followed by a dump of the whole list (length,
// capacity and every element), so a wrong element move shows as a line that
// differs.  Covered: append, appendSlice, insert at the front, middle and end,
// insertSlice, orderedRemove, orderedRemoveMany, swapRemove, pop, replaceRange
// that grows, shrinks and keeps the length, addOne/addManyAsSlice/
// addManyAsArray, resize, shrinkRetainingCapacity, shrinkAndFree,
// clearRetainingCapacity, ensureTotalCapacity/ensureUnusedCapacity, clone,
// toOwnedSlice and fromOwnedSlice, and the bounded variants over a list built
// with initBuffer on a stack array.  The element moves are `memory.copy` with
// overlapping ranges in both directions, and growth goes through the Allocator
// vtable (`call_indirect`) into wasm_allocator.  An ArrayList(u8) is used as a
// string builder through its `print`, and an ArrayList of ArrayLists is grown
// and reshaped so that the outer list moves inner list headers around.
const std = @import("std");
const w = @import("wasi");

const gpa = std.heap.wasm_allocator;

const Rec = struct {
    id: u32,
    score: i16,
    tag: u8,
    flag: bool,
    weight: u32,
};

fn dump(label: []const u8, l: std.ArrayList(u32)) void {
    w.printf("{s}: len={d} cap>=len:{} [", .{ label, l.items.len, l.capacity >= l.items.len });
    for (l.items, 0..) |v, i| {
        if (i != 0) w.puts(",");
        w.printf("{d}", .{v});
    }
    w.puts("]\n");
}

fn dumpRecs(label: []const u8, l: std.ArrayList(Rec)) void {
    w.printf("{s}: len={d}\n", .{ label, l.items.len });
    for (l.items, 0..) |r, i| {
        w.printf("  [{d}] id={d} score={d} tag={c} flag={} weight={d}\n", .{ i, r.id, r.score, r.tag, r.flag, r.weight });
    }
}

fn ints() !void {
    var l: std.ArrayList(u32) = .empty;
    defer l.deinit(gpa);
    dump("empty", l);
    const base = w.hide(@as(u32, 100));
    var i: u32 = 0;
    while (i < 10) : (i += 1) {
        try l.append(gpa, base + i * 3);
        dump("append", l);
    }
    try l.appendSlice(gpa, &.{ base, base + 1, base + 2 });
    dump("appendSlice", l);
    try l.insert(gpa, 0, 7);
    dump("insert front", l);
    try l.insert(gpa, l.items.len / 2, 8);
    dump("insert middle", l);
    try l.insert(gpa, l.items.len, 9);
    dump("insert end", l);
    try l.insertSlice(gpa, 3, &.{ 1000, 1001, 1002, 1003 });
    dump("insertSlice@3", l);
    w.printf("orderedRemove(5) -> {d}\n", .{l.orderedRemove(w.hide(@as(usize, 5)))});
    dump("after", l);
    w.printf("orderedRemove(0) -> {d}\n", .{l.orderedRemove(0)});
    dump("after", l);
    w.printf("swapRemove(2) -> {d}\n", .{l.swapRemove(w.hide(@as(usize, 2)))});
    dump("after", l);
    w.printf("swapRemove(last) -> {d}\n", .{l.swapRemove(l.items.len - 1)});
    dump("after", l);
    w.printf("pop -> {?d}\n", .{l.pop()});
    w.printf("pop -> {?d}\n", .{l.pop()});
    dump("after pops", l);
    try l.replaceRange(gpa, 2, 3, &.{ 1, 2, 3, 4, 5, 6 });
    dump("replaceRange grow", l);
    try l.replaceRange(gpa, 1, 6, &.{ 77, 78 });
    dump("replaceRange shrink", l);
    try l.replaceRange(gpa, 0, 2, &.{ 55, 56 });
    dump("replaceRange same", l);
    try l.replaceRange(gpa, l.items.len, 0, &.{ 900, 901 });
    dump("replaceRange append", l);
    l.orderedRemoveMany(&.{ 0, 2, 2, 5, 7 });
    dump("orderedRemoveMany", l);
    (try l.addOne(gpa)).* = 4242;
    const many = try l.addManyAsSlice(gpa, 4);
    for (many, 0..) |*m, k| m.* = @intCast(k * k + 1);
    const arr = try l.addManyAsArray(gpa, 3);
    arr.* = .{ 31, 32, 33 };
    dump("addOne/addMany", l);
    w.printf("last={d} getLast={d}\n", .{ l.last().?, l.getLast() });
    l.lastPtr().?.* += 1;
    dump("lastPtr+=1", l);
    try l.resize(gpa, 6);
    dump("resize 6", l);
    l.shrinkRetainingCapacity(4);
    dump("shrinkRetaining 4", l);
    l.shrinkAndFree(gpa, 2);
    dump("shrinkAndFree 2", l);
    try l.ensureTotalCapacity(gpa, 64);
    w.printf("ensureTotalCapacity 64: cap>=64 {}\n", .{l.capacity >= 64});
    try l.ensureUnusedCapacity(gpa, 100);
    w.printf("ensureUnusedCapacity 100: unused>=100 {}\n", .{l.capacity - l.items.len >= 100});
    var c = try l.clone(gpa);
    defer c.deinit(gpa);
    try c.append(gpa, 1);
    dump("clone+1", c);
    dump("orig", l);
    l.clearRetainingCapacity();
    dump("cleared", l);
    w.printf("pop on empty -> {?d}\n", .{l.pop()});

    // Many appends with a running checksum, crossing several growths.
    var x: u32 = w.hide(@as(u32, 12345));
    var k: u32 = 0;
    while (k < 600) : (k += 1) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        if (x % 7 == 0 and l.items.len > 0) {
            _ = l.orderedRemove(x % l.items.len);
        } else if (x % 5 == 0) {
            try l.insert(gpa, x % (l.items.len + 1), x >> 20);
        } else {
            try l.append(gpa, x >> 16);
        }
        if (k % 50 == 49) {
            var h: u64 = 0;
            for (l.items) |v| h = h *% 1099511628211 +% v;
            w.printf("churn[{d}] len={d} hash={x}\n", .{ k, l.items.len, h });
        }
    }
    const owned = try l.toOwnedSlice(gpa);
    w.printf("toOwnedSlice len={d} list len after={d} first={d} last={d}\n", .{ owned.len, l.items.len, owned[0], owned[owned.len - 1] });
    var back = std.ArrayList(u32).fromOwnedSlice(owned);
    w.printf("fromOwnedSlice len={d}\n", .{back.items.len});
    back.shrinkRetainingCapacity(5);
    dump("back", back);
    back.deinit(gpa);
}

fn bounded() void {
    var storage: [8]u16 = undefined;
    var l = std.ArrayList(u16).initBuffer(&storage);
    var i: u16 = w.hide(@as(u16, 1));
    while (true) : (i *= 3) {
        l.appendBounded(i) catch {
            w.printf("appendBounded full at {d}, len={d}\n", .{ i, l.items.len });
            break;
        };
        w.printf("appendBounded {d} len={d}\n", .{ i, l.items.len });
    }
    _ = l.orderedRemove(1);
    l.insertBounded(0, 999) catch w.puts("insertBounded failed\n");
    l.insertBounded(0, 998) catch w.puts("insertBounded failed (full)\n");
    for (l.items, 0..) |v, k| w.printf("  b[{d}]={d}\n", .{ k, v });
    l.appendSliceBounded(&.{ 1, 2 }) catch w.puts("appendSliceBounded failed\n");
    l.clearRetainingCapacity();
    l.appendSliceBounded(&.{ 5, 6, 7 }) catch w.puts("appendSliceBounded failed\n");
    for (l.items, 0..) |v, k| w.printf("  b'[{d}]={d}\n", .{ k, v });
}

fn records() !void {
    var l: std.ArrayList(Rec) = .empty;
    defer l.deinit(gpa);
    var x: u32 = w.hide(@as(u32, 99));
    var i: u32 = 0;
    while (i < 12) : (i += 1) {
        x = x *% 1103515245 +% 12345;
        try l.append(gpa, .{
            .id = i,
            .score = @as(i16, @truncate(@as(i32, @bitCast(x >> 8)))) >> 4,
            .tag = 'a' + @as(u8, @intCast(x % 26)),
            .flag = x & 0x100 != 0,
            .weight = x >> 22,
        });
    }
    dumpRecs("records", l);
    _ = l.orderedRemove(3);
    _ = l.swapRemove(0);
    try l.insert(gpa, 4, .{ .id = 100, .score = -32768, .tag = 'Z', .flag = true, .weight = 0xffff_ffff });
    dumpRecs("records edited", l);
    // In-place edit through pointers.
    for (l.items) |*r| {
        if (r.flag) r.score = r.score >> 1 else r.weight +%= 1;
    }
    dumpRecs("records after edit", l);
    var sum: i64 = 0;
    for (l.items) |r| sum += r.score;
    w.printf("score sum={d}\n", .{sum});
}

fn stringBuilder() !void {
    var sb: std.ArrayList(u8) = .empty;
    defer sb.deinit(gpa);
    const n = w.hide(@as(u32, 15));
    var i: u32 = 1;
    while (i <= n) : (i += 1) {
        if (i % 15 == 0) {
            try sb.appendSlice(gpa, "FizzBuzz");
        } else if (i % 3 == 0) {
            try sb.appendSlice(gpa, "Fizz");
        } else if (i % 5 == 0) {
            try sb.appendSlice(gpa, "Buzz");
        } else try sb.print(gpa, "{d}", .{i});
        try sb.append(gpa, if (i == n) '\n' else ' ');
    }
    w.puts(sb.items);
    sb.clearRetainingCapacity();
    i = 0;
    while (i < 8) : (i += 1) {
        try sb.print(gpa, "[{d:>3}|{x:0>4}|{s}]", .{ i * 37, i * 4099, if (i % 2 == 0) "even" else "odd" });
    }
    try sb.append(gpa, '\n');
    w.puts(sb.items);
    // Reverse the words in place.
    sb.clearRetainingCapacity();
    try sb.appendSlice(gpa, "the quick brown fox jumps over the lazy dog");
    std.mem.reverse(u8, sb.items);
    var start: usize = 0;
    for (sb.items, 0..) |ch, k| {
        if (ch == ' ') {
            std.mem.reverse(u8, sb.items[start..k]);
            start = k + 1;
        }
    }
    std.mem.reverse(u8, sb.items[start..]);
    w.printf("reversed words: {s}\n", .{sb.items});
    const z = try sb.toOwnedSliceSentinel(gpa, 0);
    defer gpa.free(z);
    w.printf("sentinel slice len={d} sentinel={d}\n", .{ z.len, z[z.len] });
}

fn nested() !void {
    var rows: std.ArrayList(std.ArrayList(i32)) = .empty;
    defer {
        for (rows.items) |*r| r.deinit(gpa);
        rows.deinit(gpa);
    }
    var r: i32 = 0;
    while (r < 7) : (r += 1) {
        var row: std.ArrayList(i32) = .empty;
        var c: i32 = 0;
        while (c <= r) : (c += 1) try row.append(gpa, w.hide(r * 10 - c * c));
        try rows.append(gpa, row);
    }
    // Move inner lists around: the outer list holds headers only.
    var moved = rows.orderedRemove(2);
    try moved.append(gpa, -1);
    try rows.insert(gpa, 0, moved);
    const sw = rows.swapRemove(1);
    try rows.append(gpa, sw);
    try rows.items[3].insertSlice(gpa, 0, &.{ 111, 222 });
    for (rows.items, 0..) |row, i| {
        w.printf("row[{d}] len={d}:", .{ i, row.items.len });
        for (row.items) |v| w.printf(" {d}", .{v});
        w.puts("\n");
    }
    // Pascal's triangle built row from row.
    var tri: std.ArrayList(std.ArrayList(u64)) = .empty;
    defer {
        for (tri.items) |*t| t.deinit(gpa);
        tri.deinit(gpa);
    }
    var k: usize = 0;
    while (k < w.hide(@as(usize, 22))) : (k += 1) {
        var row: std.ArrayList(u64) = .empty;
        try row.append(gpa, 1);
        if (k > 0) {
            const prev = tri.items[k - 1].items;
            for (prev[0 .. prev.len - 1], prev[1..]) |a, b| try row.append(gpa, a + b);
            try row.append(gpa, 1);
        }
        try tri.append(gpa, row);
    }
    for (tri.items, 0..) |row, i| {
        if (i % 3 != 0 and i < 18) continue;
        w.printf("pascal[{d}]:", .{i});
        for (row.items) |v| w.printf(" {d}", .{v});
        w.puts("\n");
    }
}

pub fn main() void {
    ints() catch w.puts("ints: out of memory\n");
    bounded();
    records() catch w.puts("records: out of memory\n");
    stringBuilder() catch w.puts("stringBuilder: out of memory\n");
    nested() catch w.puts("nested: out of memory\n");
    w.puts("done\n");
}
