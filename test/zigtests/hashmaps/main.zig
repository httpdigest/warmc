// std's hash maps: AutoHashMap over integer and struct keys, StringHashMap,
// the insertion-ordered array_hash_map (Auto and String), and a HashMap with
// a context of our own (case-insensitive strings).
//
// The open-addressing HashMap keeps a metadata byte per slot (fingerprint,
// used, tombstone) and probes linearly; removes leave tombstones, and enough
// churn forces rehashes into larger tables.  Its key hash for integers is
// Wyhash, whose 64x64->128 multiply is a compiler-rt `__multi3` call on wasm,
// and for struct keys the fields are hashed one after another.  The array hash
// map keeps entries in a MultiArrayList and an index header of 8/16/32-bit
// slots chosen by capacity, so a map that grows past each threshold changes
// representation.  Covered: put, putNoClobber, fetchPut, getOrPut,
// getOrPutValue, get/getPtr/getKey/getEntry, contains, remove, fetchRemove,
// count, ensureTotalCapacity, clearRetainingCapacity, clone, iteration (keys
// sorted before printing, so nothing depends on slot order), swapRemove and
// orderedRemove on the ordered map with its order printed, and sort.
const std = @import("std");
const w = @import("wasi");

const gpa = std.heap.wasm_allocator;

const Point = struct { x: i32, y: i32 };

fn lessU32(_: void, a: u32, b: u32) bool {
    return a < b;
}

fn lessStr(_: void, a: []const u8, b: []const u8) bool {
    return std.mem.lessThan(u8, a, b);
}

fn lessPoint(_: void, a: Point, b: Point) bool {
    if (a.x != b.x) return a.x < b.x;
    return a.y < b.y;
}

fn dumpIntMap(label: []const u8, m: *const std.AutoHashMap(u32, u64)) !void {
    var keys: std.ArrayList(u32) = .empty;
    defer keys.deinit(gpa);
    var it = m.keyIterator();
    while (it.next()) |k| try keys.append(gpa, k.*);
    std.mem.sort(u32, keys.items, {}, lessU32);
    w.printf("{s}: count={d}\n ", .{ label, m.count() });
    for (keys.items) |k| w.printf(" {d}=>{d}", .{ k, m.get(k).? });
    w.puts("\n");
}

fn intMap() !void {
    var m = std.AutoHashMap(u32, u64).init(gpa);
    defer m.deinit();
    const base = w.hide(@as(u32, 1000));
    var i: u32 = 0;
    while (i < 20) : (i += 1) try m.put(base + i * 7, i * i);
    try dumpIntMap("put 20", &m);
    try m.put(base, 4242);
    w.printf("get base={?d} get missing={?d} contains(base+7)={} contains(1)={}\n", .{ m.get(base), m.get(3), m.contains(base + 7), m.contains(1) });
    const old = try m.fetchPut(base + 14, 999);
    w.printf("fetchPut old={?d}\n", .{if (old) |kv| kv.value else null});
    const none = try m.fetchPut(5, 55);
    w.printf("fetchPut new present={}\n", .{none != null});
    try m.putNoClobber(6, 66);
    if (m.getPtr(6)) |p| p.* *= 3;
    w.printf("after getPtr*3: {?d}\n", .{m.get(6)});
    const gop = try m.getOrPut(base + 7);
    w.printf("getOrPut existing found={} value={d}\n", .{ gop.found_existing, gop.value_ptr.* });
    gop.value_ptr.* += 100;
    const gop2 = try m.getOrPut(77);
    w.printf("getOrPut new found={}\n", .{gop2.found_existing});
    gop2.value_ptr.* = 7777;
    const e = try m.getOrPutValue(78, 7878);
    w.printf("getOrPutValue key={d} value={d}\n", .{ e.key_ptr.*, e.value_ptr.* });
    w.printf("remove(base+21)={} remove(12345)={}\n", .{ m.remove(base + 21), m.remove(12345) });
    if (m.fetchRemove(base + 28)) |kv| w.printf("fetchRemove {d}=>{d}\n", .{ kv.key, kv.value });
    try dumpIntMap("edited", &m);
    var c = try m.clone();
    defer c.deinit();
    try c.put(1, 1);
    _ = c.remove(5);
    try dumpIntMap("clone", &c);
    try dumpIntMap("orig", &m);
    var vsum: u64 = 0;
    var vit = m.valueIterator();
    while (vit.next()) |v| vsum += v.*;
    w.printf("value sum={d}\n", .{vsum});

    // Churn: inserts and removes, leaving tombstones and forcing rehashes.
    m.clearRetainingCapacity();
    w.printf("cleared count={d}\n", .{m.count()});
    var x: u32 = w.hide(@as(u32, 2024));
    var step: u32 = 0;
    while (step < 3000) : (step += 1) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        const k = x % 512;
        if (x & 0x30000 == 0) {
            _ = m.remove(k);
        } else {
            const g = try m.getOrPut(k);
            if (!g.found_existing) g.value_ptr.* = 0;
            g.value_ptr.* += x >> 24;
        }
        if (step % 500 == 499) {
            var sum: u64 = 0;
            var it = m.iterator();
            while (it.next()) |en| sum +%= (@as(u64, en.key_ptr.*) << 32) ^ en.value_ptr.*;
            w.printf("churn[{d}] count={d} sum={x}\n", .{ step, m.count(), sum });
        }
    }
    // Probe every possible key and fold the answers in key order.
    var h: u64 = 0;
    var k: u32 = 0;
    while (k < 512) : (k += 1) h = h *% 31 +% (m.get(k) orelse 1_000_003);
    w.printf("probe hash={x}\n", .{h});
    try m.ensureTotalCapacity(5000);
    w.printf("ensureTotalCapacity 5000: count={d} capacity>=5000 {}\n", .{ m.count(), m.capacity() >= 5000 });
}

fn pointMap() !void {
    var m = std.AutoHashMap(Point, []const u8).init(gpa);
    defer m.deinit();
    const names = [_][]const u8{ "origin", "east", "north", "west", "south", "ne", "nw", "se", "sw" };
    const pts = [_]Point{ .{ .x = 0, .y = 0 }, .{ .x = 1, .y = 0 }, .{ .x = 0, .y = 1 }, .{ .x = -1, .y = 0 }, .{ .x = 0, .y = -1 }, .{ .x = 1, .y = 1 }, .{ .x = -1, .y = 1 }, .{ .x = 1, .y = -1 }, .{ .x = -1, .y = -1 } };
    for (pts, names) |p, n| try m.put(w.hide(p), n);
    // Grid walk: look up each cell.
    var y: i32 = 2;
    while (y >= -2) : (y -= 1) {
        var x: i32 = -2;
        while (x <= 2) : (x += 1) {
            const v = m.get(.{ .x = w.hide(x), .y = y }) orelse ".";
            w.printf("{s:>7}", .{v});
        }
        w.puts("\n");
    }
    _ = m.remove(.{ .x = 0, .y = 0 });
    try m.put(.{ .x = 100, .y = -100 }, "far");
    var keys: std.ArrayList(Point) = .empty;
    defer keys.deinit(gpa);
    var it = m.keyIterator();
    while (it.next()) |k| try keys.append(gpa, k.*);
    std.mem.sort(Point, keys.items, {}, lessPoint);
    for (keys.items) |k| w.printf("({d},{d})={s}\n", .{ k.x, k.y, m.get(k).? });
}

fn stringMap() !void {
    var m = std.StringHashMap(u32).init(gpa);
    defer m.deinit();
    const text = w.hide(@as([]const u8, "it was the best of times it was the worst of times it was the age of wisdom it was the age of foolishness it was the epoch of belief it was the epoch of incredulity"));
    var it = std.mem.tokenizeScalar(u8, text, ' ');
    while (it.next()) |word| {
        const g = try m.getOrPut(word);
        if (!g.found_existing) g.value_ptr.* = 0;
        g.value_ptr.* += 1;
    }
    var keys: std.ArrayList([]const u8) = .empty;
    defer keys.deinit(gpa);
    var kit = m.keyIterator();
    while (kit.next()) |k| try keys.append(gpa, k.*);
    std.mem.sort([]const u8, keys.items, {}, lessStr);
    for (keys.items) |k| w.printf("word {s}: {d}\n", .{ k, m.get(k).? });
    // Keys built at run time in a buffer must still find the literals.
    var buf: [16]u8 = undefined;
    for ([_][]const u8{ "was", "age", "epoch", "nothing" }) |probe| {
        @memcpy(buf[0..probe.len], probe);
        w.printf("lookup {s} -> {?d} (getKey same len={})\n", .{ probe, m.get(buf[0..probe.len]), if (m.getKey(buf[0..probe.len])) |k| k.len == probe.len else false });
    }
    if (m.getEntry("of")) |en| {
        en.value_ptr.* = 1000;
        w.printf("entry of -> {d}\n", .{m.get("of").?});
    }
}

fn arrayMap() !void {
    var m: std.array_hash_map.Auto(u32, i32) = .empty;
    defer m.deinit(gpa);
    var x: u32 = w.hide(@as(u32, 77));
    var i: i32 = 0;
    while (i < 24) : (i += 1) {
        x = x *% 1664525 +% 1013904223;
        try m.put(gpa, x % 100, i);
    }
    w.printf("ordered count={d}\n", .{m.count()});
    for (m.keys(), m.values()) |k, v| w.printf(" {d}:{d}", .{ k, v });
    w.puts("\n");
    const first = m.keys()[0];
    w.printf("swapRemove {d} -> {}\n", .{ first, m.swapRemove(first) });
    const second = m.keys()[1];
    w.printf("orderedRemove {d} -> {}\n", .{ second, m.orderedRemove(second) });
    for (m.keys(), m.values()) |k, v| w.printf(" {d}:{d}", .{ k, v });
    w.puts("\n");
    w.printf("getIndex of last key -> {?d}\n", .{m.getIndex(m.keys()[m.count() - 1])});
    const SortCtx = struct {
        keys: []const u32,
        pub fn lessThan(ctx: @This(), a: usize, b: usize) bool {
            return ctx.keys[a] < ctx.keys[b];
        }
    };
    m.sort(SortCtx{ .keys = m.keys() });
    w.puts("sorted:");
    for (m.keys(), m.values()) |k, v| w.printf(" {d}:{d}", .{ k, v });
    w.puts("\n");
    // The index must have been rebuilt: every key still found at its position.
    var ok = true;
    for (m.keys(), 0..) |k, idx| {
        if (m.getIndex(k) != idx) ok = false;
    }
    w.printf("index consistent after sort: {}\n", .{ok});
    // Grow past the linear-scan limit and the u8 index width.
    var j: u32 = 0;
    while (j < 600) : (j += 1) try m.put(gpa, 1000 + j * 3, @intCast(j));
    var s: i64 = 0;
    for (m.keys(), m.values(), 0..) |k, v, idx| s += @as(i64, k) * v + @as(i64, @intCast(idx));
    w.printf("grown count={d} weighted={d} get(1300)={?d} get(1301)={?d}\n", .{ m.count(), s, m.get(1300), m.get(1301) });
    if (m.pop()) |kv| w.printf("pop {d}:{d}\n", .{ kv.key, kv.value });

    var sm: std.array_hash_map.String(u8) = .empty;
    defer sm.deinit(gpa);
    for ([_][]const u8{ "zeta", "alpha", "mu", "beta", "alpha", "omega", "mu" }, 0..) |name, idx| {
        const g = try sm.getOrPut(gpa, name);
        if (g.found_existing) g.value_ptr.* += 10 else g.value_ptr.* = @intCast(idx);
    }
    for (sm.keys(), sm.values()) |k, v| w.printf("smap {s}={d}\n", .{ k, v });
}

const CaseInsensitive = struct {
    pub fn hash(_: @This(), s: []const u8) u64 {
        var h = std.hash.Wyhash.init(0);
        for (s) |ch| h.update(&[1]u8{std.ascii.toLower(ch)});
        return h.final();
    }
    pub fn eql(_: @This(), a: []const u8, b: []const u8) bool {
        return std.ascii.eqlIgnoreCase(a, b);
    }
};

fn customMap() !void {
    var m = std.HashMap([]const u8, u32, CaseInsensitive, 80).init(gpa);
    defer m.deinit();
    const words = [_][]const u8{ "Hello", "HELLO", "world", "World", "Zig", "zIG", "wasm", "WASM", "wAsM", "native" };
    for (words) |wd| {
        const g = try m.getOrPut(w.hide(wd));
        if (!g.found_existing) g.value_ptr.* = 0;
        g.value_ptr.* += 1;
    }
    w.printf("case-insensitive count={d}\n", .{m.count()});
    for ([_][]const u8{ "hello", "WORLD", "zig", "Wasm", "NATIVE", "other" }) |q| {
        w.printf("  {s} -> {?d} key={s}\n", .{ q, m.get(q), m.getKey(q) orelse "-" });
    }
}

pub fn main() void {
    intMap() catch w.puts("intMap: out of memory\n");
    pointMap() catch w.puts("pointMap: out of memory\n");
    stringMap() catch w.puts("stringMap: out of memory\n");
    arrayMap() catch w.puts("arrayMap: out of memory\n");
    customMap() catch w.puts("customMap: out of memory\n");
    w.puts("done\n");
}
