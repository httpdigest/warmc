// Reflection: code driven by @typeInfo, and types built from comptime data.
//
// `dump` walks any value by switching on its type's `@typeInfo` and recursing
// into struct fields (`inline for` over `field_names` and `@field`), arrays,
// slices, optionals, error unions, enums (`@tagName`), tagged unions (an
// `inline else` that binds the active payload and its tag) and vectors.  Each
// instantiation is a separately compiled walker over one concrete layout, so
// the module ends up with loads at every field offset of every nested type,
// and a wrong offset, a wrong sign- or zero-extension of a narrow field or a
// wrong union tag compare prints a different line.  The same is done for
// types made at comptime by `@Struct`, `@Enum` and `@Int`, and the static
// facts -- `@sizeOf`, `@alignOf`, `@offsetOf`, `@bitOffsetOf`, `@bitSizeOf`,
// `@hasField`, `@hasDecl`, `@FieldType`, `@typeName` and the std.meta helpers
// -- are printed too, so the test also records what the compiler decided
// about the layouts the walker reads.
const std = @import("std");
const w = @import("wasi");

fn indent(depth: usize) void {
    for (0..depth) |_| w.puts("  ");
}

fn dump(name: []const u8, v: anytype, depth: usize) void {
    const T = @TypeOf(v);
    indent(depth);
    switch (@typeInfo(T)) {
        .int => w.printf("{s}: {s} = {d}\n", .{ name, @typeName(T), v }),
        .float => w.printf("{s}: {s} = {d:.4}\n", .{ name, @typeName(T), v }),
        .bool => w.printf("{s}: bool = {}\n", .{ name, v }),
        .void => w.printf("{s}: void\n", .{name}),
        .@"enum" => w.printf("{s}: enum {s} = .{s} ({d})\n", .{ name, @typeName(T), @tagName(v), @intFromEnum(v) }),
        .optional => {
            if (v) |inner| {
                w.printf("{s}: optional, some\n", .{name});
                dump("?", inner, depth + 1);
            } else w.printf("{s}: optional, null\n", .{name});
        },
        .error_union => {
            if (v) |inner| {
                w.printf("{s}: error union, ok\n", .{name});
                dump("ok", inner, depth + 1);
            } else |e| w.printf("{s}: error union, error.{s}\n", .{ name, @errorName(e) });
        },
        .array => |a| {
            if (a.child == u8) {
                w.printf("{s}: [{d}]u8 = \"{s}\"\n", .{ name, a.len, &v });
            } else {
                w.printf("{s}: array of {d} {s}\n", .{ name, a.len, @typeName(a.child) });
                for (v, 0..) |e, i| {
                    var buf: [16]u8 = undefined;
                    dump(std.mem.print(&buf, "[{d}]", .{i}) catch "?", e, depth + 1);
                }
            }
        },
        .pointer => |p| {
            if (p.size == .slice and p.child == u8) {
                w.printf("{s}: string \"{s}\" len={d}\n", .{ name, v, v.len });
            } else if (p.size == .slice) {
                w.printf("{s}: slice of {d} {s}\n", .{ name, v.len, @typeName(p.child) });
                for (v, 0..) |e, i| {
                    var buf: [16]u8 = undefined;
                    dump(std.mem.print(&buf, "[{d}]", .{i}) catch "?", e, depth + 1);
                }
            } else {
                w.printf("{s}: pointer to {s}\n", .{ name, @typeName(p.child) });
                dump("*", v.*, depth + 1);
            }
        },
        .vector => |vi| {
            w.printf("{s}: vector of {d} {s}:", .{ name, vi.len, @typeName(vi.child) });
            const arr: [vi.len]vi.child = v;
            for (arr) |e| w.printf(" {d}", .{e});
            w.puts("\n");
        },
        .@"struct" => |s| {
            w.printf("{s}: {s} {s} ({d} fields, size {d})\n", .{ name, if (s.is_tuple) "tuple" else "struct", @typeName(T), s.field_names.len, @sizeOf(T) });
            inline for (s.field_names) |f| {
                dump(f, @field(v, f), depth + 1);
            }
        },
        .@"union" => |u| {
            if (u.tag_type) |_| {
                switch (v) {
                    inline else => |payload, tag| {
                        w.printf("{s}: union {s} active .{s}\n", .{ name, @typeName(T), @tagName(tag) });
                        dump(@tagName(tag), payload, depth + 1);
                    },
                }
            } else w.printf("{s}: bare union {s}\n", .{ name, @typeName(T) });
        },
        else => w.printf("{s}: <{s}>\n", .{ name, @typeName(T) }),
    }
}

const Color = enum(u8) { red = 1, green = 2, blue = 4, alpha = 8 };
const Shape = union(enum) {
    circle: f32,
    rect: struct { w: u16, h: u16 },
    poly: [3]i8,
    none,
};
const Inner = struct {
    tag: Color,
    small: i5,
    flag: bool,
    ratio: f64,
};
const Outer = struct {
    id: u32,
    name: []const u8,
    code: [4]u8,
    inner: Inner,
    maybe: ?u16,
    maybe_inner: ?Inner,
    shapes: [3]Shape,
    vec: @Vector(4, i16),
    result: anyerror!u8,
    pub const version = 3;
    pub fn describe() []const u8 {
        return "outer";
    }
};

const Ext = extern struct { a: u8, b: u32, c: u16, d: u64, e: u8 };
const Auto = struct { a: u8, b: u32, c: u16, d: u64, e: u8 };
const Pck = packed struct { a: u3, b: u9, c: bool, d: i7, e: u12 };

// @bitSizeOf is defined for scalars and packed types, not for padded ones.
fn bitsOf(comptime T: type) usize {
    return switch (@typeInfo(T)) {
        .int, .float, .bool, .@"enum" => @bitSizeOf(T),
        .@"struct" => |s| if (s.layout == .@"packed") @bitSizeOf(T) else @sizeOf(T) * 8,
        else => @sizeOf(T) * 8,
    };
}

fn layout(comptime T: type) void {
    const info = @typeInfo(T).@"struct";
    w.printf("layout {s}: size={d} align={d} bits={d} layout={s}\n", .{ @typeName(T), @sizeOf(T), @alignOf(T), bitsOf(T), @tagName(info.layout) });
    inline for (info.field_names, info.field_types) |f, FT| {
        w.printf("  .{s}: {s} offset={d} bitoffset={d} size={d} bits={d}\n", .{ f, @typeName(FT), @offsetOf(T, f), @bitOffsetOf(T, f), @sizeOf(FT), bitsOf(FT) });
    }
}

// A struct type made at comptime from a list of field types.
fn Record(comptime types: []const type) type {
    var names: [types.len][]const u8 = undefined;
    for (&names, 0..) |*n, i| n.* = std.fmt.comptimePrint("f{d}", .{i});
    const attrs: [types.len]std.lang.Type.Struct.FieldAttributes = @splat(.{});
    return @Struct(.auto, null, &names, types[0..types.len], &attrs);
}

const Level = @Enum(u8, .exhaustive, &.{ "low", "mid", "high", "max" }, &.{ 10, 20, 30, 255 });

fn sumFields(v: anytype) i128 {
    var s: i128 = 0;
    inline for (@typeInfo(@TypeOf(v)).@"struct".field_names) |f| {
        const x = @field(v, f);
        switch (@typeInfo(@TypeOf(x))) {
            .int => s += x,
            .bool => s += @intFromBool(x),
            .@"enum" => s += @intFromEnum(x),
            .float => s += @as(i128, @intFromFloat(x)),
            else => {},
        }
    }
    return s;
}

fn fillFrom(comptime T: type, seed: u64) T {
    var out: T = undefined;
    var x = seed;
    inline for (@typeInfo(T).@"struct".field_names, @typeInfo(T).@"struct".field_types) |f, FT| {
        x = x *% 6364136223846793005 +% 1442695040888963407;
        const bits = x >> 11;
        @field(out, f) = switch (@typeInfo(FT)) {
            .int => |i| @bitCast(@as(@Int(.unsigned, i.bits), @truncate(bits))),
            .bool => bits & 1 == 1,
            .float => @as(FT, @floatFromInt(bits & 0xffff)) / 16,
            .@"enum" => std.meta.tags(FT)[@intCast(bits % std.meta.tags(FT).len)],
            else => @compileError("unsupported"),
        };
    }
    return out;
}

pub fn main() void {
    const o = w.hide(Outer{
        .id = 0xdeadbeef,
        .name = "widget",
        .code = "ZG17".*,
        .inner = .{ .tag = .blue, .small = -11, .flag = true, .ratio = 0.375 },
        .maybe = null,
        .maybe_inner = .{ .tag = .alpha, .small = 15, .flag = false, .ratio = -2.5 },
        .shapes = .{ .{ .circle = 1.5 }, .{ .rect = .{ .w = 640, .h = 480 } }, .{ .poly = .{ -1, 0, 127 } } },
        .vec = .{ -32768, -1, 0, 32767 },
        .result = error.OutOfRange,
    });
    dump("o", o, 0);
    var o2 = o;
    o2.maybe = w.hide(@as(u16, 65535));
    o2.maybe_inner = null;
    o2.shapes[0] = .none;
    o2.shapes[2] = .{ .circle = -0.0625 };
    o2.result = w.hide(@as(u8, 200));
    o2.inner.small = w.hide(@as(i5, -16));
    dump("o2", o2, 0);
    const p = &o2;
    dump("ptr", p, 0);
    const pts = [_]Inner{ o.inner, o2.inner };
    dump("slice", w.hide(@as([]const Inner, &pts)), 0);
    dump("tuple", .{ w.hide(@as(u7, 99)), w.hide(@as(i33, -4000000000)), w.hide(Color.green) }, 0);

    layout(Ext);
    layout(Auto);
    layout(Pck);
    layout(Inner);
    w.printf("Outer: hasField id={} hasField nope={} hasDecl version={} hasDecl describe={} hasDecl missing={} version={d} describe={s}\n", .{
        @hasField(Outer, "id"), @hasField(Outer, "nope"), @hasDecl(Outer, "version"), @hasDecl(Outer, "describe"), @hasDecl(Outer, "missing"), Outer.version, Outer.describe(),
    });
    w.printf("FieldType Outer.inner = {s}, Inner.small = {s}\n", .{ @typeName(@FieldType(Outer, "inner")), @typeName(@FieldType(Inner, "small")) });
    inline for (comptime std.meta.fieldNames(Outer), 0..) |f, i| w.printf("field {d}: {s} index={?d}\n", .{ i, f, std.meta.fieldIndex(Outer, f) });
    inline for (comptime std.meta.fieldNames(Shape)) |f| w.printf("Shape field {s}\n", .{f});

    const names = [_][]const u8{ "red", "green", "blue", "alpha", "purple", "" };
    for (names) |n0| {
        const n = w.hide(n0);
        if (std.meta.stringToEnum(Color, n)) |c| {
            w.printf("stringToEnum {s} -> .{s} = {d}\n", .{ n, @tagName(c), @intFromEnum(c) });
        } else w.printf("stringToEnum {s} -> null\n", .{n});
    }
    const FE = std.meta.FieldEnum(Inner);
    inline for (comptime std.meta.tags(FE)) |t| w.printf("FieldEnum(Inner).{s} = {d}\n", .{ @tagName(t), @intFromEnum(t) });
    for (o.shapes) |s| w.printf("activeTag {s}\n", .{@tagName(std.meta.activeTag(w.hide(s)))});
    w.printf("meta.eql inner/inner={} inner/inner2={} shapes0={}\n", .{
        std.meta.eql(o.inner, w.hide(o.inner)), std.meta.eql(o.inner, o2.inner), std.meta.eql(o.shapes[1], w.hide(o.shapes[1])),
    });

    const R = Record(&.{ u8, i16, u24, bool, f32, Color, i64, u1 });
    w.printf("Record: {s} size={d} fields={d}\n", .{ @typeName(R), @sizeOf(R), @typeInfo(R).@"struct".field_names.len });
    var seed: u64 = w.hide(@as(u64, 12345));
    for (0..6) |i| {
        const r = fillFrom(R, seed);
        var buf: [16]u8 = undefined;
        dump(std.mem.print(&buf, "rec{d}", .{i}) catch "?", r, 0);
        w.printf("  sumFields={d}\n", .{sumFields(r)});
        seed = seed *% 31 +% @as(u32, @truncate(@as(u128, @bitCast(sumFields(r)))));
    }
    const Q = fillFrom(Inner, w.hide(@as(u64, 777)));
    dump("inner-filled", Q, 0);

    inline for (comptime std.meta.tags(Level)) |l| w.printf("Level.{s} = {d}\n", .{ @tagName(l), @intFromEnum(l) });
    const lv: Level = w.hide(Level.high);
    w.printf("Level hidden {s} {d}\n", .{ @tagName(lv), @intFromEnum(lv) });

    inline for (.{ 1, 7, 13, 31, 33, 64, 65, 100, 128 }) |bits| {
        const U = @Int(.unsigned, bits);
        const S = @Int(.signed, bits);
        const um: U = w.hide(@as(U, std.math.maxInt(U)));
        const sm: S = w.hide(@as(S, std.math.minInt(S)));
        w.printf("@Int {d}: {s} max={d} size={d} align={d} | {s} min={d} size={d}\n", .{ bits, @typeName(U), um, @sizeOf(U), @alignOf(U), @typeName(S), sm, @sizeOf(S) });
    }
}
