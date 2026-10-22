// Tagged unions: layout, exhaustive switches and payload captures.
//
// A `union(enum)` is its largest payload plus a tag, and a switch over it is
// a load of the tag and a `br_table` into arms that each read the payload at
// offset 0 with their own type -- a float in one arm, a pointer in the next, a
// three-field struct in a third.  Covered here: exhaustive switches with
// value captures and with pointer captures that rewrite the payload in place,
// `inline else` that captures the tag at comptime, unions over an explicit
// `enum(u8)` tag type with non-dense values, methods on unions, reassigning a
// union to a different variant, a recursive expression tree whose nodes are
// unions holding pointers to other nodes (evaluated, simplified and printed),
// `std.meta.activeTag`, and `extern union` read through the variant it was
// last written as or through a same-sized byte array, which are the
// reinterpretations that are defined.  Every step prints, so an arm reading
// the payload with the wrong width or a tag compare that picks the wrong arm
// is a different line.
const std = @import("std");
const w = @import("wasi");

const Value = union(enum) {
    int: i64,
    float: f64,
    boolean: bool,
    text: []const u8,
    pair: struct { a: i16, b: u8, c: bool },
    nothing,

    fn describe(self: Value) void {
        switch (self) {
            .int => |i| w.printf("int {d}", .{i}),
            .float => |f| w.printf("float {d:.5}", .{f}),
            .boolean => |b| w.printf("bool {}", .{b}),
            .text => |t| w.printf("text \"{s}\" ({d})", .{ t, t.len }),
            .pair => |p| w.printf("pair a={d} b={d} c={}", .{ p.a, p.b, p.c }),
            .nothing => w.puts("nothing"),
        }
    }

    fn truthy(self: Value) bool {
        return switch (self) {
            .int => |i| i != 0,
            .float => |f| f != 0,
            .boolean => |b| b,
            .text => |t| t.len > 0,
            .pair => |p| p.c,
            .nothing => false,
        };
    }

    fn bump(self: *Value) void {
        switch (self.*) {
            .int => |*i| i.* = i.* *% 3 +% 1,
            .float => |*f| f.* = f.* * -0.5,
            .boolean => |*b| b.* = !b.*,
            .text => |*t| t.* = if (t.len > 0) t.*[1..] else t.*,
            .pair => |*p| {
                p.a = p.a +% 1000;
                p.b +%= 77;
                p.c = !p.c;
            },
            .nothing => self.* = .{ .int = 42 },
        }
    }
};

const Kind = enum(u8) { small = 1, medium = 10, large = 200 };
const Sized = union(Kind) {
    small: u8,
    medium: u32,
    large: [3]u64,

    fn weight(self: Sized) u64 {
        return switch (self) {
            inline else => |payload, tag| blk: {
                const base: u64 = @intFromEnum(tag);
                if (tag == .large) break :blk base + payload[0] + payload[1] + payload[2];
                break :blk base * payload;
            },
        };
    }
};

const Expr = union(enum) {
    num: i64,
    neg: *const Expr,
    add: [2]*const Expr,
    mul: [2]*const Expr,
    sub: [2]*const Expr,
    cond: struct { c: *const Expr, t: *const Expr, f: *const Expr },

    fn eval(self: *const Expr) i64 {
        return switch (self.*) {
            .num => |n| n,
            .neg => |e| -%e.eval(),
            .add => |p| p[0].eval() +% p[1].eval(),
            .mul => |p| p[0].eval() *% p[1].eval(),
            .sub => |p| p[0].eval() -% p[1].eval(),
            .cond => |c| if (c.c.eval() != 0) c.t.eval() else c.f.eval(),
        };
    }

    fn depth(self: *const Expr) u32 {
        return 1 + switch (self.*) {
            .num => 0,
            .neg => |e| e.depth(),
            .add, .mul, .sub => |p| @max(p[0].depth(), p[1].depth()),
            .cond => |c| @max(c.c.depth(), @max(c.t.depth(), c.f.depth())),
        };
    }

    fn show(self: *const Expr) void {
        switch (self.*) {
            .num => |n| w.printf("{d}", .{n}),
            .neg => |e| {
                w.puts("-(");
                e.show();
                w.puts(")");
            },
            inline .add, .mul, .sub => |p, tag| {
                w.puts("(");
                p[0].show();
                w.puts(switch (tag) {
                    .add => " + ",
                    .mul => " * ",
                    .sub => " - ",
                    else => unreachable,
                });
                p[1].show();
                w.puts(")");
            },
            .cond => |c| {
                w.puts("(");
                c.c.show();
                w.puts(" ? ");
                c.t.show();
                w.puts(" : ");
                c.f.show();
                w.puts(")");
            },
        }
    }
};

// Depth 5 with three children at most: 1 + 3 + ... + 3^5 = 364 nodes.
var pool: [364]Expr = undefined;
var pool_len: usize = 0;

fn mk(e: Expr) *const Expr {
    pool[pool_len] = e;
    pool_len += 1;
    return &pool[pool_len - 1];
}

fn build(seed: *u32, d: u32) *const Expr {
    seed.* = seed.* *% 1103515245 +% 12345;
    const r = (seed.* >> 16) % 8;
    if (d == 0 or r == 0) return mk(.{ .num = @as(i64, @intCast((seed.* >> 8) % 41)) - 20 });
    return switch (r) {
        1 => mk(.{ .neg = build(seed, d - 1) }),
        2, 3 => mk(.{ .add = .{ build(seed, d - 1), build(seed, d - 1) } }),
        4, 5 => mk(.{ .mul = .{ build(seed, d - 1), build(seed, d - 1) } }),
        6 => mk(.{ .sub = .{ build(seed, d - 1), build(seed, d - 1) } }),
        else => mk(.{ .cond = .{ .c = build(seed, d - 1), .t = build(seed, d - 1), .f = build(seed, d - 1) } }),
    };
}

const Bits = extern union {
    word: u32,
    bytes: [4]u8,
    half: [2]u16,
};

const FloatBits = extern union {
    f: f32,
    u: u32,
};

pub fn main() void {
    var values = [_]Value{
        .{ .int = w.hide(@as(i64, -7)) },
        .{ .float = w.hide(@as(f64, 3.0625)) },
        .{ .boolean = w.hide(false) },
        .{ .text = w.hide(@as([]const u8, "hello")) },
        .{ .pair = .{ .a = w.hide(@as(i16, 32000)), .b = 250, .c = true } },
        .nothing,
        .{ .int = w.hide(@as(i64, std.math.maxInt(i64))) },
        .{ .text = "" },
    };
    for (0..4) |round| {
        for (&values, 0..) |*v, i| {
            w.printf("r{d} v[{d}] {s}: ", .{ round, i, @tagName(std.meta.activeTag(v.*)) });
            v.describe();
            w.printf(" truthy={}\n", .{v.truthy()});
            v.bump();
        }
    }
    // Reassign to another variant and back.
    values[0] = .{ .text = "switched" };
    values[1] = .nothing;
    values[2] = .{ .pair = .{ .a = -1, .b = 1, .c = false } };
    for (values[0..3], 0..) |v, i| {
        w.printf("reassigned[{d}] ", .{i});
        v.describe();
        w.puts("\n");
    }
    w.printf("sizes Value={d} Sized={d} Expr={d} Bits={d}\n", .{ @sizeOf(Value), @sizeOf(Sized), @sizeOf(Expr), @sizeOf(Bits) });

    const sized = [_]Sized{
        .{ .small = w.hide(@as(u8, 255)) },
        .{ .medium = w.hide(@as(u32, 4000000000)) },
        .{ .large = .{ w.hide(@as(u64, 1)), 2, 3 } },
        .{ .small = 0 },
        .{ .large = .{ std.math.maxInt(u32), std.math.maxInt(u32), w.hide(@as(u64, 5)) } },
    };
    for (sized, 0..) |s, i| {
        const t = std.meta.activeTag(s);
        w.printf("sized[{d}] tag={s} tagval={d} weight={d}\n", .{ i, @tagName(t), @intFromEnum(t), s.weight() });
    }

    var seed: u32 = w.hide(@as(u32, 2024));
    for (0..24) |i| {
        pool_len = 0;
        const e = build(&seed, 5);
        w.printf("expr[{d}] nodes={d} depth={d} value={d}\n  ", .{ i, pool_len, e.depth(), e.eval() });
        e.show();
        w.puts("\n");
    }

    var bits: Bits = .{ .word = w.hide(@as(u32, 0x11223344)) };
    w.printf("bits word={x} bytes={d},{d},{d},{d} half={x},{x}\n", .{ bits.word, bits.bytes[0], bits.bytes[1], bits.bytes[2], bits.bytes[3], bits.half[0], bits.half[1] });
    bits.bytes[1] = w.hide(@as(u8, 0xff));
    w.printf("bits after byte write word={x}\n", .{bits.word});
    bits.half[1] = w.hide(@as(u16, 0xabcd));
    w.printf("bits after half write word={x} bytes={x},{x},{x},{x}\n", .{ bits.word, bits.bytes[0], bits.bytes[1], bits.bytes[2], bits.bytes[3] });

    const floats = [_]f32{ 0.0, -0.0, 1.0, -2.5, 3.4028235e38, 1.0e-45, 0.1 };
    for (floats) |f0| {
        const fb: FloatBits = .{ .f = w.hide(f0) };
        w.printf("floatbits {e} -> {x:0>8}\n", .{ fb.f, fb.u });
    }

    // A switch on a union inside a loop, counting variants.
    var counts: [6]u32 = @splat(0);
    var x: u32 = w.hide(@as(u32, 99));
    var acc: i64 = 0;
    for (0..200) |_| {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        const v: Value = switch (x % 6) {
            0 => .{ .int = @as(i32, @bitCast(x)) },
            1 => .{ .float = @as(f64, @floatFromInt(x % 1000)) / 8 },
            2 => .{ .boolean = x & 0x100 != 0 },
            3 => .{ .text = "abcdefgh"[0 .. x % 9] },
            4 => .{ .pair = .{ .a = @bitCast(@as(u16, @truncate(x))), .b = @truncate(x >> 16), .c = x & 1 == 0 } },
            else => .nothing,
        };
        counts[@intFromEnum(std.meta.activeTag(v))] += 1;
        acc +%= switch (v) {
            .int => |i| i,
            .float => |f| @intFromFloat(f * 8),
            .boolean => |b| if (b) 1 else -1,
            .text => |t| @intCast(t.len * 100),
            .pair => |p| @as(i64, p.a) * p.b,
            .nothing => 7,
        };
    }
    w.printf("variant counts {d} {d} {d} {d} {d} {d} acc={d}\n", .{ counts[0], counts[1], counts[2], counts[3], counts[4], counts[5], acc });
}
