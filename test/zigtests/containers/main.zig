// std's other containers: the intrusive SinglyLinkedList and DoublyLinkedList
// (bare link nodes embedded in a struct and recovered with @fieldParentPtr),
// PriorityQueue and PriorityDequeue, Deque (a ring buffer that wraps), the
// bit sets (Integer, Array/Static, Dynamic), MultiArrayList (a struct of
// arrays, one array per field) and StaticStringMap (a comptime-built map
// sorted by key length).
//
// @fieldParentPtr is pointer arithmetic by a field's offset, so a wrong
// offset reads another field; the lists are printed after every edit by
// walking them in both directions.  The heaps sift up and down on every push
// and pop; popping everything prints the order, which must be sorted.  The
// deque is pushed at both ends past its capacity so that its head wraps and
// it regrows with the contents split across the end of the buffer.  The bit
// sets cover set/unset/toggle/setRangeValue/count/findFirstSet/findLastSet/
// toggleFirstSet, the set algebra and iteration both ways over set and unset
// bits, on 7-, 64-, 100- and 300-bit sets whose words straddle the i64 /
// i32 boundaries.  MultiArrayList is appended, inserted, removed and sorted
// by a field, and read back by field and by row.
const std = @import("std");
const w = @import("wasi");

const gpa = std.heap.wasm_allocator;

const Item = struct {
    id: u32,
    weight: i16,
    link: std.SinglyLinkedList.Node = .{},
    dlink: std.DoublyLinkedList.Node = .{},
};

fn showSingly(label: []const u8, l: std.SinglyLinkedList) void {
    w.printf("{s} len={d}:", .{ label, l.len() });
    var it = l.first;
    while (it) |n| : (it = n.next) {
        const item: *Item = @fieldParentPtr("link", n);
        w.printf(" {d}/{d}", .{ item.id, item.weight });
    }
    w.puts("\n");
}

fn showDoubly(label: []const u8, l: std.DoublyLinkedList) void {
    w.printf("{s} len={d} fwd:", .{ label, l.len() });
    var it = l.first;
    while (it) |n| : (it = n.next) {
        const item: *Item = @fieldParentPtr("dlink", n);
        w.printf(" {d}", .{item.id});
    }
    w.puts(" bwd:");
    var jt = l.last;
    while (jt) |n| : (jt = n.prev) {
        const item: *Item = @fieldParentPtr("dlink", n);
        w.printf(" {d}", .{item.id});
    }
    w.puts("\n");
}

fn lists() void {
    var items: [12]Item = undefined;
    for (&items, 0..) |*it, i| it.* = .{ .id = @intCast(i), .weight = @intCast(w.hide(@as(i32, @intCast(i))) * 7 - 40) };
    var s: std.SinglyLinkedList = .{};
    for (items[0..6]) |*it| s.prepend(&it.link);
    showSingly("singly prepend 0..5", s);
    items[3].link.insertAfter(&items[8].link);
    showSingly("insertAfter 3 <- 8", s);
    s.remove(&items[0].link);
    showSingly("remove 0", s);
    s.remove(&items[5].link);
    showSingly("remove head 5", s);
    if (s.popFirst()) |n| {
        const it: *Item = @fieldParentPtr("link", n);
        w.printf("popFirst -> {d}\n", .{it.id});
    }
    if (items[3].link.removeNext()) |n| {
        const it: *Item = @fieldParentPtr("link", n);
        w.printf("removeNext after 3 -> {d}\n", .{it.id});
    }
    showSingly("now", s);
    s.prepend(&items[9].link);
    s.prepend(&items[10].link);
    const last: *Item = @fieldParentPtr("link", s.first.?.findLast());
    w.printf("findLast -> {d} countChildren(first)={d}\n", .{ last.id, s.first.?.countChildren() });
    std.SinglyLinkedList.Node.reverse(&s.first);
    showSingly("reversed", s);
    var wsum: i32 = 0;
    while (s.popFirst()) |n| {
        const it: *Item = @fieldParentPtr("link", n);
        wsum += it.weight;
    }
    w.printf("drained weight sum={d} len={d}\n", .{ wsum, s.len() });

    var d: std.DoublyLinkedList = .{};
    for (items[0..5]) |*it| d.append(&it.dlink);
    showDoubly("doubly append 0..4", d);
    d.prepend(&items[5].dlink);
    showDoubly("prepend 5", d);
    d.insertAfter(&items[2].dlink, &items[6].dlink);
    showDoubly("insertAfter 2 <- 6", d);
    d.insertBefore(&items[0].dlink, &items[7].dlink);
    showDoubly("insertBefore 0 <- 7", d);
    d.remove(&items[4].dlink);
    showDoubly("remove 4 (last)", d);
    d.remove(&items[5].dlink);
    showDoubly("remove 5 (first)", d);
    d.remove(&items[2].dlink);
    showDoubly("remove 2 (middle)", d);
    if (d.popFirst()) |n| w.printf("popFirst -> {d}\n", .{@as(*Item, @fieldParentPtr("dlink", n)).id});
    if (d.popLast()) |n| w.printf("popLast -> {d}\n", .{@as(*Item, @fieldParentPtr("dlink", n)).id});
    showDoubly("now", d);
    var d2: std.DoublyLinkedList = .{};
    for (items[8..12]) |*it| d2.append(&it.dlink);
    d.concatByMoving(&d2);
    showDoubly("concatByMoving", d);
    showDoubly("moved-from", d2);
}

fn minOrder(_: void, a: i32, b: i32) std.math.Order {
    return std.math.order(a, b);
}

const Task = struct { pri: u8, seq: u16, name: []const u8 };

fn taskOrder(_: void, a: Task, b: Task) std.math.Order {
    // Higher priority first; equal priorities in arrival order.
    if (a.pri != b.pri) return std.math.order(b.pri, a.pri);
    return std.math.order(a.seq, b.seq);
}

fn byDistance(target: i32, a: i32, b: i32) std.math.Order {
    return std.math.order(@abs(a - target), @abs(b - target)).differ() orelse std.math.order(a, b);
}

fn queues() !void {
    var pq: std.PriorityQueue(i32, void, minOrder) = .empty;
    defer pq.deinit(gpa);
    var x: u32 = w.hide(@as(u32, 31337));
    var i: u32 = 0;
    while (i < 40) : (i += 1) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        try pq.push(gpa, @as(i32, @intCast(x % 200)) - 100);
        if (i % 5 == 4) w.printf("pq push#{d} count={d} peek={?d}\n", .{ i, pq.count(), pq.peek() });
    }
    try pq.pushSlice(gpa, &.{ -1000, 1000, 0 });
    try pq.update(1000, -999);
    w.puts("pq drain:");
    var prev: i32 = std.math.minInt(i32);
    var sorted = true;
    while (pq.pop()) |v| {
        w.printf(" {d}", .{v});
        if (v < prev) sorted = false;
        prev = v;
    }
    w.printf("\npq sorted={} pop empty={?d}\n", .{ sorted, pq.pop() });

    var tq: std.PriorityQueue(Task, void, taskOrder) = .empty;
    defer tq.deinit(gpa);
    const names = [_][]const u8{ "parse", "lex", "emit", "link", "opt", "verify", "load", "run", "gc", "log" };
    for (names, 0..) |n, k| try tq.push(gpa, .{ .pri = @intCast(w.hide(k) * 7 % 4), .seq = @intCast(k), .name = n });
    _ = tq.popIndex(2);
    w.puts("tasks:");
    while (tq.pop()) |t| w.printf(" {s}(p{d},#{d})", .{ t.name, t.pri, t.seq });
    w.puts("\n");

    var dq: std.PriorityQueue(i32, i32, byDistance) = .initContext(w.hide(@as(i32, 50)));
    defer dq.deinit(gpa);
    for ([_]i32{ 10, 90, 49, 51, 50, 0, 100, 75, 25, 48, 52 }) |v| try dq.push(gpa, v);
    w.puts("closest to 50:");
    while (dq.pop()) |v| w.printf(" {d}", .{v});
    w.puts("\n");

    var pdq: std.PriorityDequeue(i32, void, minOrder) = .empty;
    defer pdq.deinit(gpa);
    i = 0;
    while (i < 25) : (i += 1) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        try pdq.push(gpa, @intCast(x % 1000));
    }
    w.puts("minmax deque alternating:");
    var turn = false;
    while (pdq.count() > 0) : (turn = !turn) {
        const v = if (turn) pdq.popMax() else pdq.popMin();
        w.printf(" {d}", .{v.?});
    }
    w.puts("\n");
}

fn showDeque(label: []const u8, d: *const std.Deque(u16)) void {
    w.printf("{s} len={d}:", .{ label, d.len });
    var k: usize = 0;
    while (k < d.len) : (k += 1) w.printf(" {d}", .{d.at(k)});
    w.printf(" front={?d} back={?d}\n", .{ d.front(), d.back() });
}

fn deque() !void {
    var d: std.Deque(u16) = .empty;
    defer d.deinit(gpa);
    try d.ensureTotalCapacityPrecise(gpa, 6);
    var v: u16 = w.hide(@as(u16, 1));
    var i: u32 = 0;
    while (i < 5) : (i += 1) {
        try d.pushBack(gpa, v);
        v += 1;
    }
    showDeque("pushBack x5", &d);
    _ = d.popFront();
    _ = d.popFront();
    _ = d.popFront();
    showDeque("popFront x3", &d);
    // Push to the back until the ring wraps past the end of its buffer.
    i = 0;
    while (i < 3) : (i += 1) {
        try d.pushBack(gpa, 100 + v);
        v += 1;
    }
    showDeque("pushBack wraps", &d);
    try d.pushFront(gpa, 900);
    try d.pushFront(gpa, 901);
    showDeque("pushFront x2 (grows while wrapped)", &d);
    try d.pushBackSlice(gpa, &.{ 7, 8, 9, 10, 11, 12, 13 });
    try d.pushFrontSlice(gpa, &.{ 70, 71, 72 });
    showDeque("slices both ends", &d);
    d.atPtr(2).* = 4242;
    if (d.frontPtr()) |p| p.* += 1;
    if (d.backPtr()) |p| p.* *= 2;
    showDeque("edited through pointers", &d);
    var it = d.iterator();
    var sum: u32 = 0;
    while (it.next()) |e| sum += e;
    w.printf("iterator sum={d}\n", .{sum});
    // Use as a FIFO queue of a breadth-first walk on an implicit binary tree.
    var q: std.Deque(u16) = .empty;
    defer q.deinit(gpa);
    try q.pushBack(gpa, 1);
    w.puts("bfs:");
    while (q.popFront()) |n| {
        w.printf(" {d}", .{n});
        if (n * 2 <= 40) try q.pushBack(gpa, n * 2);
        if (n * 2 + 1 <= 40 and n % 3 != 0) try q.pushBack(gpa, n * 2 + 1);
    }
    w.puts("\n");
    // And as a stack from the back.
    var st: [4]u16 = undefined;
    var bd = std.Deque(u16).initBuffer(&st);
    for ([_]u16{ 5, 6, 7, 8, 9 }) |e| {
        bd.pushBackBounded(e) catch w.printf("bounded deque full at {d}\n", .{e});
    }
    w.puts("bounded pops:");
    while (bd.popBack()) |e| w.printf(" {d}", .{e});
    w.puts("\n");
}

fn showIter(label: []const u8, it: anytype) void {
    w.printf("{s}:", .{label});
    var n: usize = 0;
    while (it.next()) |b| : (n += 1) {
        if (n < 40) w.printf(" {d}", .{b});
    }
    w.printf(" (n={d})\n", .{n});
}

fn bitsets() !void {
    var a = std.bit_set.Integer(7).empty;
    a.set(w.hide(@as(usize, 1)));
    a.set(5);
    a.toggle(6);
    a.toggle(1);
    a.setValue(0, true);
    w.printf("int7 mask={b:0>7} count={d} first={?d} last={?d}\n", .{ a.mask, a.count(), a.findFirstSet(), a.findLastSet() });
    const c = a.complement();
    w.printf("int7 complement={b:0>7} union={b:0>7} inter={b:0>7} xor full={b:0>7}\n", .{ c.mask, a.unionWith(c).mask, a.intersectWith(c).mask, a.xorWith(.full).mask });

    var b64 = std.bit_set.Integer(64).empty;
    var k: usize = 0;
    while (k < 64) : (k += w.hide(@as(usize, 7))) b64.set(k);
    b64.setRangeValue(.{ .start = 30, .end = 40 }, true);
    w.printf("int64 mask={x} count={d} first={?d} last={?d}\n", .{ b64.mask, b64.count(), b64.findFirstSet(), b64.findLastSet() });
    var it1 = b64.iterator(.{});
    showIter("  set fwd", &it1);
    var it2 = b64.iterator(.{ .direction = .reverse });
    showIter("  set rev", &it2);
    var it3 = b64.iterator(.{ .kind = .unset });
    showIter("  unset fwd", &it3);
    while (b64.toggleFirstSet()) |f| {
        if (f > 20) break;
        w.printf("  toggleFirstSet -> {d}\n", .{f});
    }

    var s100 = std.bit_set.Static(100).empty;
    var t100 = std.bit_set.Static(100).full;
    k = 0;
    while (k < 100) : (k += 1) {
        if (k % 3 == 0) s100.set(k);
        if (k % 5 == 0) t100.unset(k);
    }
    w.printf("static100 s.count={d} t.count={d} s&t={d} s|t={d} s^t={d} s-t={d}\n", .{
        s100.count(),                     t100.count(),
        s100.intersectWith(t100).count(), s100.unionWith(t100).count(),
        s100.xorWith(t100).count(),       s100.differenceWith(t100).count(),
    });
    w.printf("static100 subsetOf={} first={?d} last={?d} eql self={}\n", .{ s100.intersectWith(t100).subsetOf(s100), s100.findFirstSet(), s100.findLastSet(), s100.eql(s100) });
    var it4 = s100.differenceWith(t100).iterator(.{});
    showIter("  s-t members", &it4);
    var it5 = t100.iterator(.{ .kind = .unset, .direction = .reverse });
    showIter("  t unset rev", &it5);

    var dyn = try std.bit_set.Dynamic.initEmpty(gpa, w.hide(@as(usize, 300)));
    defer dyn.deinit(gpa);
    // Sieve of Eratosthenes on the dynamic set: set bits are composites.
    var p: usize = 2;
    while (p * p < 300) : (p += 1) {
        if (dyn.isSet(p)) continue;
        var m = p * p;
        while (m < 300) : (m += p) dyn.set(m);
    }
    dyn.set(0);
    dyn.set(1);
    w.printf("dyn300 composites={d} primes={d}\n", .{ dyn.count(), 300 - dyn.count() });
    var it6 = dyn.iterator(.{ .kind = .unset });
    showIter("  primes", &it6);
    try dyn.resize(gpa, 340, true);
    w.printf("resize to 340 fill=true count={d} last={?d}\n", .{ dyn.count(), dyn.findLastSet() });
    dyn.setRangeValue(.{ .start = 60, .end = 200 }, false);
    w.printf("clear 60..200 count={d}\n", .{dyn.count()});
    dyn.toggleAll();
    w.printf("toggleAll count={d} first={?d}\n", .{ dyn.count(), dyn.findFirstSet() });
    var cl = try dyn.clone(gpa);
    defer cl.deinit(gpa);
    cl.unsetAll();
    cl.set(299);
    w.printf("clone count={d} subsetOf={}\n", .{ cl.count(), cl.subsetOf(dyn) });
}

const Particle = struct {
    x: f32,
    y: f32,
    mass: u16,
    alive: bool,
    tag: u8,
};

fn showParticles(label: []const u8, l: std.MultiArrayList(Particle)) void {
    w.printf("{s} len={d}\n", .{ label, l.len });
    var i: usize = 0;
    while (i < l.len) : (i += 1) {
        const p = l.get(i);
        w.printf("  [{d}] x={d:.2} y={d:.2} mass={d} alive={} tag={c}\n", .{ i, p.x, p.y, p.mass, p.alive, p.tag });
    }
}

fn multiArray() !void {
    var l: std.MultiArrayList(Particle) = .empty;
    defer l.deinit(gpa);
    var i: u16 = 0;
    while (i < 9) : (i += 1) {
        const f: f32 = @floatFromInt(w.hide(i));
        try l.append(gpa, .{ .x = f * 1.5, .y = 10 - f * f / 4, .mass = (i * 37) % 11 + 1, .alive = i % 3 != 0, .tag = 'a' + @as(u8, @intCast(i)) });
    }
    showParticles("appended", l);
    try l.insert(gpa, 2, .{ .x = -1, .y = -1, .mass = 99, .alive = true, .tag = 'Z' });
    l.orderedRemove(5);
    l.swapRemove(0);
    showParticles("insert/remove", l);
    // Column access: integrate positions through the x and y arrays.
    const xs = l.items(.x);
    const ys = l.items(.y);
    const ms = l.items(.mass);
    for (xs, ys, ms) |*x, *y, m| {
        x.* += @as(f32, @floatFromInt(m)) * 0.25;
        y.* -= 0.5;
    }
    var alive: u32 = 0;
    for (l.items(.alive)) |a| alive += @intFromBool(a);
    w.printf("alive={d}\n", .{alive});
    const ByMass = struct {
        masses: []const u16,
        pub fn lessThan(ctx: @This(), a: usize, b: usize) bool {
            return ctx.masses[a] < ctx.masses[b];
        }
    };
    l.sort(ByMass{ .masses = l.items(.mass) });
    showParticles("sorted by mass (stable)", l);
    l.set(0, .{ .x = 0, .y = 0, .mass = 0, .alive = false, .tag = '0' });
    if (l.pop()) |p| w.printf("pop tag={c} mass={d}\n", .{ p.tag, p.mass });
    var sl = l.slice();
    sl.swap(0, 1);
    w.printf("after swap tags: {s}\n", .{sl.items(.tag)});
}

const Keyword = enum { kw_if, kw_else, kw_while, kw_for, kw_fn, kw_return, kw_const, kw_var };
const keywords = std.StaticStringMap(Keyword).initComptime(.{
    .{ "if", .kw_if },
    .{ "else", .kw_else },
    .{ "while", .kw_while },
    .{ "for", .kw_for },
    .{ "fn", .kw_fn },
    .{ "return", .kw_return },
    .{ "const", .kw_const },
    .{ "var", .kw_var },
});
const units = std.StaticStringMap(u32).initComptime(.{
    .{ "k", 1000 },
    .{ "kib", 1024 },
    .{ "m", 1000000 },
    .{ "mib", 1048576 },
    .{ "g", 1000000000 },
});

fn staticMaps() void {
    const src = w.hide(@as([]const u8, "fn main ( ) { const x = 1 ; if x return x else while y for z var iff fnn retur constant }"));
    var it = std.mem.tokenizeScalar(u8, src, ' ');
    while (it.next()) |tok| {
        if (keywords.get(tok)) |kw| {
            w.printf("{s:<9} keyword {s} (index {?d})\n", .{ tok, @tagName(kw), keywords.getIndex(tok) });
        } else w.printf("{s:<9} ident? has={}\n", .{ tok, keywords.has(tok) });
    }
    for ([_][]const u8{ "kib", "kibibyte", "m", "mib42", "g", "x", "" }) |q| {
        if (units.getLongestPrefix(w.hide(q))) |kv| {
            w.printf("longest prefix of [{s}] = {s} -> {d}\n", .{ q, kv.key, kv.value });
        } else w.printf("longest prefix of [{s}] = none\n", .{q});
    }
    w.puts("keys by length:");
    for (units.keys(), units.values()) |k, v| w.printf(" {s}={d}", .{ k, v });
    w.puts("\n");
}

pub fn main() void {
    lists();
    queues() catch w.puts("queues: out of memory\n");
    deque() catch w.puts("deque: out of memory\n");
    bitsets() catch w.puts("bitsets: out of memory\n");
    multiArray() catch w.puts("multiArray: out of memory\n");
    staticMaps();
    w.puts("done\n");
}
