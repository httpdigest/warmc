// Enums: explicit tag values, @tagName, conversions, non-exhaustive enums and
// the std.enums containers.
//
// An enum is its tag integer, but the integer need not be dense: values here
// have gaps, are negative (`enum(i8)`), or sit at the top of a u16, so a
// `switch` over one becomes a `br_table` with holes or a chain of compares,
// and `@tagName` becomes a lookup into a table of string slices that LLVM
// builds per enum.  Non-exhaustive enums (`_`) take any integer of their tag
// type, and their `else` prong must catch the unnamed ones; `std.enums.fromInt`
// and `std.enums.tagName` answer for values that may not be named, and
// `std.meta.stringToEnum` goes from a name back to the enum.  Enum methods and
// decls, switches with several items per prong and ranges over the integer
// value, and std.enums.EnumArray / EnumSet / EnumMap (an array indexed by a
// dense remapping of the tags, and bit sets over them) are each printed for
// every value, so a wrong table index or a misplaced hole shows.
const std = @import("std");
const w = @import("wasi");

const Planet = enum(u8) {
    mercury = 1,
    venus = 2,
    earth = 3,
    mars = 4,
    jupiter = 10,
    saturn = 11,
    uranus = 20,
    neptune = 21,

    const count = @typeInfo(Planet).@"enum".field_names.len;

    fn isGiant(p: Planet) bool {
        return switch (p) {
            .jupiter, .saturn, .uranus, .neptune => true,
            .mercury, .venus, .earth, .mars => false,
        };
    }
    fn moons(p: Planet) u16 {
        return switch (p) {
            .mercury, .venus => 0,
            .earth => 1,
            .mars => 2,
            .jupiter => 95,
            .saturn => 146,
            .uranus => 28,
            .neptune => 16,
        };
    }
    fn next(p: Planet) ?Planet {
        const tags = std.meta.tags(Planet);
        for (tags, 0..) |t, i| {
            if (t == p) return if (i + 1 < tags.len) tags[i + 1] else null;
        }
        unreachable;
    }
};

const Temp = enum(i8) {
    freezing = -128,
    cold = -40,
    cool = -1,
    zero = 0,
    mild = 15,
    warm = 30,
    hot = 127,
};

const Opcode = enum(u16) {
    nop = 0x0000,
    load = 0x0010,
    store = 0x0011,
    add = 0x0100,
    jump = 0x8000,
    halt = 0xffff,
    _,
};

const Small = enum { a, b, c, d, e };

fn classify(op: Opcode) []const u8 {
    return switch (op) {
        .nop => "nop",
        .load, .store => "memory",
        .add => "alu",
        .jump => "control",
        .halt => "stop",
        _ => "unknown",
    };
}

fn rangeOf(t: Temp) []const u8 {
    return switch (@intFromEnum(t)) {
        -128...-41 => "arctic",
        -40...-1 => "below zero",
        0 => "zero",
        1...29 => "temperate",
        30...127 => "tropical",
    };
}

pub fn main() void {
    for (std.meta.tags(Planet)) |p0| {
        const p = w.hide(p0);
        w.printf("planet {s} = {d} giant={} moons={d} next={s}\n", .{
            @tagName(p), @intFromEnum(p), p.isGiant(), p.moons(), if (p.next()) |n| @tagName(n) else "-",
        });
    }
    w.printf("planet count {d} size={d}\n", .{ Planet.count, @sizeOf(Planet) });
    var i: u8 = w.hide(@as(u8, 0));
    while (i < 24) : (i += 1) {
        if (std.enums.fromInt(Planet, i)) |p| {
            w.printf("fromInt({d}) = .{s}\n", .{ i, @tagName(p) });
        } else w.printf("fromInt({d}) = null\n", .{i});
    }
    const valid = [_]u8{ 1, 4, 10, 21, 3 };
    for (valid) |v| {
        const p: Planet = @enumFromInt(w.hide(v));
        w.printf("enumFromInt({d}) = {s}\n", .{ v, @tagName(p) });
    }

    for (std.meta.tags(Temp)) |t0| {
        const t = w.hide(t0);
        w.printf("temp {s} = {d} range={s} raw={x:0>2}\n", .{ @tagName(t), @intFromEnum(t), rangeOf(t), @as(u8, @bitCast(@intFromEnum(t))) });
    }
    var tv: i8 = w.hide(@as(i8, -128));
    var named: u32 = 0;
    while (true) {
        if (std.enums.fromInt(Temp, tv)) |t| {
            named += 1;
            w.printf("temp value {d} is .{s}\n", .{ tv, @tagName(t) });
        }
        if (tv == 127) break;
        tv += 1;
    }
    w.printf("named temps hit={d}\n", .{named});

    const raw_ops = [_]u16{ 0, 0x10, 0x11, 0x12, 0x100, 0x8000, 0x8001, 0xffff, 0x1234, 7 };
    for (raw_ops) |r| {
        const op: Opcode = @enumFromInt(w.hide(r));
        const name = std.enums.tagName(Opcode, op) orelse "(unnamed)";
        w.printf("opcode {x:0>4}: {s} class={s}\n", .{ @intFromEnum(op), name, classify(op) });
    }

    const words = [_][]const u8{ "earth", "pluto", "neptune", "", "Mars", "mars", "jupiter" };
    for (words) |wd| {
        const p = std.meta.stringToEnum(Planet, w.hide(wd));
        w.printf("stringToEnum \"{s}\" -> {s}\n", .{ wd, if (p) |x| @tagName(x) else "null" });
    }

    // EnumArray: one slot per tag, densely indexed.
    var weights = std.enums.EnumArray(Planet, f64).initFill(0);
    for (std.meta.tags(Planet), 0..) |p, k| weights.set(p, w.hide(@as(f64, @floatFromInt(k + 1))) * 0.75 + @as(f64, @floatFromInt(@intFromEnum(p))));
    var it = weights.iterator();
    while (it.next()) |e| w.printf("weights[{s}] = {d:.3}\n", .{ @tagName(e.key), e.value.* });
    w.printf("weights.get(saturn)={d:.3} len={d}\n", .{ weights.get(.saturn), @TypeOf(weights).len });

    var counts = std.enums.EnumArray(Small, u32).initFill(0);
    var x: u32 = w.hide(@as(u32, 12345));
    for (0..300) |_| {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        const s: Small = @enumFromInt(x % 5);
        counts.getPtr(s).* += 1;
    }
    for (std.meta.tags(Small)) |s| w.printf("counts[{s}] = {d}\n", .{ @tagName(s), counts.get(s) });

    // EnumSet: bit set over the tags.
    var giants: std.enums.EnumSet(Planet) = .{};
    for (std.meta.tags(Planet)) |p| {
        if (p.isGiant()) giants.insert(p);
    }
    var inner = std.enums.EnumSet(Planet).initMany(&.{ .mercury, .venus, .earth, .mars });
    w.printf("giants={d} inner={d} disjoint={} union={d} complement-of-giants-eq-inner={}\n", .{
        giants.count(), inner.count(), giants.intersectWith(inner).count() == 0, giants.unionWith(inner).count(), giants.complement().eql(inner),
    });
    inner.toggle(.earth);
    inner.insert(.jupiter);
    var sit = inner.iterator();
    w.puts("inner now:");
    while (sit.next()) |p| w.printf(" {s}", .{@tagName(p)});
    w.puts("\n");
    const xo = inner.xorWith(giants);
    var xit = xo.iterator();
    w.puts("xor:");
    while (xit.next()) |p| w.printf(" {s}", .{@tagName(p)});
    w.printf(" subset={} superset={}\n", .{ inner.subsetOf(giants), giants.supersetOf(std.enums.EnumSet(Planet).initOne(.uranus)) });

    // EnumMap: optional value per tag.
    var visits = std.enums.EnumMap(Planet, u32){};
    visits.put(.mars, w.hide(@as(u32, 3)));
    visits.put(.venus, 1);
    visits.put(.neptune, 9);
    if (visits.getPtr(.mars)) |v| v.* += 10;
    const old = visits.fetchPut(.venus, 2);
    visits.remove(.neptune);
    w.printf("visits count={d} old venus={?d} has neptune={}\n", .{ visits.count(), old, visits.contains(.neptune) });
    var mit = visits.iterator();
    while (mit.next()) |e| w.printf("  visit {s} = {d}\n", .{ @tagName(e.key), e.value.* });
    for (std.meta.tags(Planet)) |p| w.printf("  get {s} = {?d}\n", .{ @tagName(p), visits.get(p) });

    // A small state machine driven by an enum.
    const State = enum { idle, running, paused, done };
    var st: State = .idle;
    var y: u32 = w.hide(@as(u32, 77));
    for (0..30) |step| {
        y = y *% 1103515245 +% 12345;
        const ev = (y >> 16) % 4;
        const prev = st;
        st = switch (st) {
            .idle => if (ev == 0) .idle else .running,
            .running => switch (ev) {
                0 => .paused,
                1 => .done,
                else => .running,
            },
            .paused => if (ev < 2) .running else .paused,
            .done => .idle,
        };
        w.printf("step {d}: {s} --{d}--> {s}\n", .{ step, @tagName(prev), ev, @tagName(st) });
    }
}
