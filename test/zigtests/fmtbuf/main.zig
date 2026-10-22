// std's formatter writing into fixed buffers: `std.mem.print` (what
// `std.fmt.bufPrint` became in 0.17) over every conversion it has -- {d} {x}
// {X} {b} {o} on integers of many widths and signs, {d} {e} {E} {x} on floats
// with and without a precision, {s} {c} {u} {q} {x} {b64} on bytes, {t} on
// enums, unions and errors, {?} {!} on optionals and error unions, {any} on
// structs, {B} {Bi} for byte sizes, {f} for a type's own `format` method --
// and every combination of width, fill and alignment, with widths and
// precisions both literal and taken from the arguments.
//
// The format string is parsed at compile time, so what reaches wasm is the
// digit generation itself: 128-bit division by constants for the u128 path
// (`__udivti3` / `__umodti3`), the Ryu-style shortest float printer with its
// 128-bit multiplies and table lookups, the padding loops, and
// `Io.Writer.fixed`'s bounds checks, which here are the ordinary path to
// `error.NoSpaceLeft` when a buffer is too short.  Every
// formatted string is printed between bars so padding is visible.
const std = @import("std");
const w = @import("wasi");

fn out(label: []const u8, comptime fmt: []const u8, args: anytype) void {
    var buf: [200]u8 = undefined;
    if (std.mem.print(&buf, fmt, args)) |s| {
        w.printf("{s}: |{s}| len={d}\n", .{ label, s, s.len });
    } else |err| {
        w.printf("{s}: error {s}\n", .{ label, @errorName(err) });
    }
}

fn ints(comptime T: type, vals: []const T) void {
    for (vals, 0..) |v0, i| {
        const v = w.hide(v0);
        w.printf("{s}[{d}]\n", .{ @typeName(T), i });
        out("  d", "{d}", .{v});
        out("  x", "{x}", .{v});
        out("  X", "{X}", .{v});
        out("  b", "{b}", .{v});
        out("  o", "{o}", .{v});
        out("  d>12", "{d:>12}", .{v});
        out("  d<12", "{d:<12}", .{v});
        out("  d^12", "{d:^12}", .{v});
        out("  d0>10", "{d:0>10}", .{v});
        out("  d010", "{d:010}", .{v});
        out("  x0>16", "{x:0>16}", .{v});
        out("  b_>24", "{b:_>24}", .{v});
        out("  d*^15", "{d:*^15}", .{v});
        out("  any", "{any}", .{v});
        if (@bitSizeOf(T) <= 8) {
            if (@typeInfo(T).int.signedness == .unsigned and v >= 32 and v < 127) out("  c", "{c}", .{v});
        }
    }
}

fn floats() void {
    const xs = [_]f64{ 0.0, -0.0, 1.0, -1.5, 3.141592653589793, 1e21, 1e-7, 123456.789, 0.1, 2.5, 0.000123456, 9.999999, 1.7976931348623157e308, 5e-324, std.math.inf(f64), -std.math.inf(f64) };
    for (xs, 0..) |x0, i| {
        const x = w.hide(x0);
        w.printf("f64[{d}]\n", .{i});
        out("  d", "{d}", .{x});
        out("  e", "{e}", .{x});
        out("  E", "{E}", .{x});
        out("  d.0", "{d:.0}", .{x});
        out("  d.3", "{d:.3}", .{x});
        out("  e.2", "{e:.2}", .{x});
        out("  d>14.4", "{d:>14.4}", .{x});
        out("  d<14.2", "{d:<14.2}", .{x});
        out("  e^20.5", "{e:^20.5}", .{x});
        out("  x", "{x}", .{x});
        out("  X.3", "{X:.3}", .{x});
        out("  default", "{}", .{x});
        const f: f32 = @floatCast(x);
        out("  f32 d", "{d}", .{f});
        out("  f32 e.4", "{e:.4}", .{f});
        const h: f16 = @floatCast(x);
        out("  f16 d", "{d}", .{h});
    }
}

const Color = enum(u8) { red = 1, green = 20, blue = 255 };
const Shape = union(enum) {
    circle: f32,
    rect: struct { w: u16, h: u16 },
    none,
};
const Point = struct { x: i32, y: i32, tag: []const u8 };
const Err = error{ Broken, OutOfThings };

const Money = struct {
    cents: i64,
    pub fn format(self: Money, writer: *std.Io.Writer) std.Io.Writer.Error!void {
        const neg = self.cents < 0;
        const mag: u64 = @abs(self.cents);
        try writer.print("{s}${d}.{d:0>2}", .{ if (neg) "-" else "", mag / 100, mag % 100 });
    }
};

fn others() void {
    const strs = [_][]const u8{ "", "a", "hello", "tab\there", "quote\"q", "naïve", "0123456789abcdef!" };
    for (strs, 0..) |s0, i| {
        const s = w.hide(s0);
        w.printf("str[{d}]\n", .{i});
        out("  s", "{s}", .{s});
        out("  s>10", "{s:>10}", .{s});
        out("  s<10", "{s:<10}", .{s});
        out("  s^11", "{s:^11}", .{s});
        out("  s.^9", "{s:.^9}", .{s});
        out("  q", "{q}", .{s});
        out("  x", "{x}", .{s});
        out("  X", "{X}", .{s});
        out("  b64", "{b64}", .{s});
    }
    const cps = [_]u21{ 'A', 0xe9, 0x20ac, 0x1f600, '0' };
    for (cps) |c| out("u", "{u}", .{w.hide(c)});

    const colors = [_]Color{ .red, .green, .blue };
    for (colors) |c0| {
        const c = w.hide(c0);
        out("enum t", "{t}", .{c});
        out("enum any", "{any}", .{c});
        out("enum d", "{d}", .{c});
        out("enum x", "{x}", .{c});
    }
    const shapes = [_]Shape{ .{ .circle = 1.5 }, .{ .rect = .{ .w = 3, .h = 4 } }, .none };
    for (shapes) |s0| {
        const s = w.hide(s0);
        out("union t", "{t}", .{s});
        out("union any", "{any}", .{s});
    }
    const p = w.hide(Point{ .x = -3, .y = 42, .tag = "pt" });
    out("struct any", "{any}", .{p});
    out("struct fields", "({d},{d}) {s}", .{ p.x, p.y, p.tag });
    out("tuple any", "{any}", .{.{ @as(u8, 1), true, @as(f32, 0.5) }});
    out("array any", "{any}", .{w.hide([4]u16{ 1, 200, 3000, 40000 })});
    out("array any i8", "{any}", .{w.hide([3]i8{ -1, 0, 1 })});
    out("bool", "{} {}", .{ w.hide(true), w.hide(false) });
    out("bool>8", "{:>8}|{:<8}", .{ w.hide(true), w.hide(false) });

    const opts = [_]?u32{ null, 0, 77 };
    for (opts) |o0| {
        const o = w.hide(o0);
        out("opt ?d", "{?d}", .{o});
        out("opt ?x", "{?x}", .{o});
        out("opt any", "{any}", .{o});
    }
    const eus = [_]Err!i16{ -5, error.Broken, 300, error.OutOfThings };
    for (eus) |e0| {
        const e = w.hide(e0);
        out("eu !d", "{!d}", .{e});
        out("eu any", "{any}", .{e});
        if (e) |_| {} else |err| {
            out("err t", "{t}", .{err});
            out("err any", "{any}", .{err});
        }
    }

    const sizes = [_]u64{ 0, 999, 1000, 1023, 1024, 1536, 1000000, 1048576, 5368709120, 1 << 60 };
    for (sizes) |s0| {
        const s = w.hide(s0);
        out("B", "{B}", .{s});
        out("Bi", "{Bi}", .{s});
        out("B.2", "{B:.2}", .{s});
        out("Bi>12.1", "{Bi:>12.1}", .{s});
    }

    const ms = [_]Money{ .{ .cents = 0 }, .{ .cents = 5 }, .{ .cents = -1234 }, .{ .cents = 100000099 } };
    for (ms) |m0| out("money f", "<{f}>", .{w.hide(m0)});
}

fn positional() void {
    var i: u32 = 0;
    while (i < 6) : (i += 1) {
        const a = w.hide(i * 37 + 1);
        const b = w.hide(@as(i32, @intCast(i)) - 3);
        out("pos", "{1d} {0d} {1d} {0x}", .{ a, b });
        out("named", "{[name]s}={[val]d:>6} ({[val]x})", .{ .name = "k", .val = a });
        out("dyn width", "{[v]d:>[wd]}|{[v]d:<[wd]}|", .{ .v = b, .wd = @as(usize, i + 2) });
        out("dyn prec", "{[v]d:.[p]}", .{ .v = @as(f64, @floatFromInt(a)) / 7.0, .p = @as(usize, i) });
        out("escape", "{{{d}}}", .{a});
    }
}

fn helpers() void {
    // count, allocPrint into a fixed buffer, printInt, digits2, hex helpers.
    var backing: [512]u8 = undefined;
    var fba = std.heap.FixedBufferAllocator.init(&backing);
    const gpa = fba.allocator();
    var k: u32 = 0;
    while (k < 8) : (k += 1) {
        const v = w.hide(k * 1234567 + 89);
        const n = std.fmt.count("{d}-{x}-{s}", .{ v, v, "zz" });
        const s = std.fmt.allocPrint(gpa, "{d}-{x}-{s}", .{ v, v, "zz" }) catch {
            w.puts("alloc failed\n");
            return;
        };
        w.printf("count={d} alloc=|{s}| len={d} used={d}\n", .{ n, s, s.len, fba.end_index });
        if (k % 3 == 2) fba.reset();
        var buf: [80]u8 = undefined;
        const l1 = std.fmt.printInt(&buf, v, 7, .lower, .{});
        w.printf("  base7=|{s}|", .{buf[0..l1]});
        const l2 = std.fmt.printInt(&buf, @as(i64, -@as(i64, v)), 36, .upper, .{ .width = 12, .fill = '.' });
        w.printf(" base36=|{s}|", .{buf[0..l2]});
        const d2 = std.fmt.digits2(@intCast(v % 100));
        w.printf(" digits2={s}\n", .{&d2});
    }
    const bytes = w.hide([6]u8{ 0xde, 0xad, 0x00, 0xbe, 0xef, 0x7f });
    const hex = std.fmt.bytesToHex(bytes, .lower);
    w.printf("bytesToHex={s} upper={s}\n", .{ &hex, &std.fmt.bytesToHex(bytes, .upper) });
    var back: [6]u8 = undefined;
    if (std.fmt.hexToBytes(&back, &hex)) |b| {
        w.printf("hexToBytes={x}\n", .{b});
    } else |err| w.printf("hexToBytes error {s}\n", .{@errorName(err)});
    if (std.fmt.hexToBytes(&back, "abc")) |b| {
        w.printf("odd hex={x}\n", .{b});
    } else |err| w.printf("odd hex error {s}\n", .{@errorName(err)});
    const ct = comptime std.fmt.comptimePrint("{d}+{x}={s}", .{ 40, 255, "ok" });
    w.printf("comptimePrint={s}\n", .{ct});

    // Too-small buffers fail cleanly.
    var tiny: [5]u8 = undefined;
    var sz: usize = 0;
    while (sz <= tiny.len) : (sz += 1) {
        const slice = tiny[0..w.hide(sz)];
        if (std.mem.print(slice, "{d}", .{w.hide(@as(u32, 1234))})) |s| {
            w.printf("tiny[{d}] ok |{s}|\n", .{ sz, s });
        } else |err| w.printf("tiny[{d}] {s}\n", .{ sz, @errorName(err) });
    }
    // printSentinel leaves a zero terminator.
    var zb: [32]u8 = undefined;
    if (std.mem.printSentinel(&zb, "z{d}", .{w.hide(@as(u8, 9))}, 0)) |z| {
        w.printf("sentinel len={d} term={d}\n", .{ z.len, zb[z.len] });
    } else |err| w.printf("sentinel error {s}\n", .{@errorName(err)});
}

pub fn main() void {
    ints(u8, &.{ 0, 1, 42, 127, 128, 255 });
    ints(i8, &.{ 0, -1, -128, 127, 100 });
    ints(u16, &.{ 0, 65535, 4660 });
    ints(i32, &.{ -2147483648, 2147483647, -42, 1000000 });
    ints(u64, &.{ 18446744073709551615, 1 << 63, 1234567890123 });
    ints(i64, &.{ -9223372036854775808, -1, 9223372036854775807 });
    ints(u128, &.{ 340282366920938463463374607431768211455, 1 << 100, 12345678901234567890123456789 });
    ints(i128, &.{ -170141183460469231731687303715884105728, -987654321098765432109876543210 });
    ints(u7, &.{ 0, 100, 127 });
    ints(i33, &.{ -4294967296, 4294967295, -77 });
    floats();
    others();
    positional();
    helpers();
    w.puts("done\n");
}
