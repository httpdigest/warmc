// The smaller builtins, each over a spread of run-time inputs.
//
// @min and @max with two to five operands of mixed integer types (the result
// type is the narrowest that holds every operand's range, so `@min(u8, u32)`
// is a u8) and on floats with NaN, which they skip; @abs on every signed
// width including minInt, where the result is the unsigned magnitude and
// cannot overflow; @divExact, @divCeil and @shlExact / @shrExact only where
// the result is exact, as the language requires; @intFromPtr / @ptrFromInt
// round trips and @ptrCast / @alignCast between pointer types of different
// element sizes; @fieldParentPtr from a field back to its struct (the
// intrusive-list idiom); @offsetOf and @bitOffsetOf; @splat into arrays and
// vectors; @select; @src for the function name and line; @inComptime in a
// function called both at compile time and at run time; @prefetch, a no-op
// on wasm that must still compile; std.math.clamp, cast, lossyCast,
// negateCast, isPowerOfTwo, log2_int, ceilPowerOfTwo, mulWide, gcd and lcm,
// sign extension and order.
//
// Every value is printed; inputs come through `w.hide`.
const std = @import("std");
const w = @import("wasi");

fn minMax() void {
    const as = [_]i32{ -100, -1, 0, 7, 255, 1000, std.math.minInt(i32), std.math.maxInt(i32) };
    const bs = [_]u8{ 0, 1, 200, 255 };
    for (as, 0..) |a0, i| {
        const a = w.hide(a0);
        const b = w.hide(bs[i % bs.len]);
        const c = w.hide(@as(i64, @intCast(i)) * -37 + 50);
        const d = w.hide(@as(u16, @intCast(i * 9000)));
        const m2 = @min(a, b);
        const x2 = @max(a, b);
        const m4 = @min(a, b, c, d);
        const x4 = @max(a, b, c, d);
        const mu = @min(b, d); // u8: both unsigned, b bounds it
        w.printf("a={d} b={d} c={d} d={d} min2={d} max2={d} min4={d} max4={d} minu={d} ({s})\n", .{
            a, b, c, d, m2, x2, m4, x4, mu, @typeName(@TypeOf(mu)),
        });
    }
    const nan = std.math.nan(f64);
    const fs = [_]f64{ -0.5, 3.25, nan, -1e300, 1e-300, std.math.inf(f64) };
    for (fs, 0..) |f0, i| {
        const f = w.hide(f0);
        const g = w.hide(fs[(i + 2) % fs.len]);
        w.printf("fmin({e},{e})={e} fmax={e} min3={e}\n", .{ f, g, @min(f, g), @max(f, g), @min(f, g, @as(f64, 2.0)) });
    }
    const vmin = @min(w.hide(@as(@Vector(4, i16), .{ -3, 9, 0, 32767 })), @as(@Vector(4, i16), @splat(4)));
    w.printf("vector min {any}\n", .{vmin});
}

fn absAll(comptime T: type) void {
    const vals = [_]T{ 0, 1, -1, std.math.maxInt(T), std.math.minInt(T), std.math.minInt(T) + 1, @divTrunc(std.math.minInt(T), 3) };
    w.printf("{s}:", .{@typeName(T)});
    for (vals) |v0| {
        const v = w.hide(v0);
        const a = @abs(v);
        w.printf(" |{d}|={d}", .{ v, a });
    }
    w.printf(" -> {s}\n", .{@typeName(@TypeOf(@abs(vals[0])))});
}

fn absTests() void {
    absAll(i8);
    absAll(i5);
    absAll(i16);
    absAll(i32);
    absAll(i40);
    absAll(i64);
    absAll(i128);
    for ([_]f32{ -0.0, 1.5, -2.25, -std.math.inf(f32) }) |f| w.printf("fabs({d})={d}\n", .{ f, @abs(w.hide(f)) });
    const v: @Vector(4, i8) = w.hide(@as(@Vector(4, i8), .{ -128, -1, 0, 127 }));
    w.printf("vabs {any}\n", .{@abs(v)});
}

fn exactOps() void {
    const nums = [_]i32{ 0, 12, -12, 1024, -4096, 360, -720, 999999 };
    for (nums) |n0| {
        const n = w.hide(n0);
        // n * 6 is always exactly divisible by 6 and by 3.
        const e6 = @divExact(n * 6, 6);
        const e3 = @divExact(n * 6, -3);
        const ceil7 = @divCeil(n, 7);
        const ceilm7 = @divCeil(n, -7);
        const floor7 = @divFloor(n, 7);
        w.printf("n={d} divExact6={d} divExact-3={d} divCeil7={d} divCeil-7={d} divFloor7={d}\n", .{ n, e6, e3, ceil7, ceilm7, floor7 });
    }
    const us = [_]u32{ 1, 3, 0x10, 0xff, 0x7fff };
    for (us) |uv| {
        const u = w.hide(uv);
        const sh = w.hide(@as(u5, 8));
        const l = @shlExact(u, sh); // u < 2^24, so no 1 bits shifted out
        const r = @shrExact(l, sh); // the low 8 bits of l are zero
        w.printf("shlExact({d},8)={d} shrExact back={d}\n", .{ u, l, r });
    }
    const si = [_]i16{ -1, -64, 63, 100, -128 };
    for (si) |s0| {
        const s = w.hide(s0);
        const l = @shlExact(s, w.hide(@as(u4, 7)));
        w.printf("shlExact i16 {d}<<7={d} >>7={d}\n", .{ s, l, l >> 7 });
    }
    w.printf("std.math.divCeil(-7,2)={d} divCeil(7,2)={d}\n", .{
        std.math.divCeil(i32, w.hide(@as(i32, -7)), 2) catch 0, std.math.divCeil(i32, w.hide(@as(i32, 7)), 2) catch 0,
    });
}

const Node = struct {
    value: u32,
    link: Link = .{},
    tag: u8,
};
const Link = struct {
    next: ?*Link = null,
};

fn pointers() void {
    var arr = [_]u32{ 0x11223344, 0x55667788, 0x99aabbcc, 0xddeeff00 };
    const addr = @intFromPtr(&arr[w.hide(@as(usize, 1))]);
    const back: *u32 = @ptrFromInt(addr);
    back.* +%= 1;
    const step = @intFromPtr(&arr[2]) - @intFromPtr(&arr[1]);
    w.printf("step={d} back={x} aligned4={}\n", .{ step, back.*, addr % 4 == 0 });
    // Byte view of the same words (little endian).
    const bytes: *[16]u8 = @ptrCast(&arr);
    w.puts("bytes:");
    for (bytes) |b| w.printf(" {x:0>2}", .{b});
    w.puts("\n");
    // u16 view at a run-time index, cast back with alignment.
    const halves: [*]u16 = @ptrCast(@alignCast(&arr));
    const hi = w.hide(@as(usize, 5));
    halves[hi] = 0xbeef;
    w.printf("halves[{d}]={x} arr[2]={x}\n", .{ hi, halves[hi], arr[2] });
    const u8p: [*]u8 = @ptrCast(&arr);
    const reword: *align(1) u32 = @ptrCast(u8p + w.hide(@as(usize, 2)));
    w.printf("unaligned word at 2 = {x}\n", .{reword.*});
    reword.* = 0x01020304;
    w.printf("after store: {x} {x}\n", .{ arr[0], arr[1] });
    // @fieldParentPtr walks from embedded links back to their nodes.
    var nodes: [5]Node = undefined;
    for (&nodes, 0..) |*n, i| n.* = .{ .value = @intCast(i * i + 3), .tag = @intCast('A' + i) };
    var head: ?*Link = null;
    for ([_]usize{ 3, 0, 4, 1, 2 }) |k| {
        const n = &nodes[w.hide(k)];
        n.link.next = head;
        head = &n.link;
    }
    var it = head;
    while (it) |l| : (it = l.next) {
        const n: *Node = @fieldParentPtr("link", l);
        w.printf("node {c} value {d} index {d}\n", .{ n.tag, n.value, (@intFromPtr(n) - @intFromPtr(&nodes)) / @sizeOf(Node) });
    }
    w.printf("offsetOf value={d} link={d} tag={d} size={d}\n", .{ @offsetOf(Node, "value"), @offsetOf(Node, "link"), @offsetOf(Node, "tag"), @sizeOf(Node) });
    const P = packed struct { a: u3, b: u5, c: u12, d: u4 };
    w.printf("bitOffsetOf a={d} b={d} c={d} d={d} bits={d}\n", .{ @bitOffsetOf(P, "a"), @bitOffsetOf(P, "b"), @bitOffsetOf(P, "c"), @bitOffsetOf(P, "d"), @bitSizeOf(P) });
    // @prefetch is a hint and must not change anything.
    @prefetch(&arr, .{});
    @prefetch(&nodes[1], .{ .rw = .write, .locality = 1 });
}

fn splatSelect() void {
    const k = w.hide(@as(i32, -9));
    const arr: [6]i32 = @splat(k);
    const v: @Vector(8, i32) = @splat(k * 3);
    w.printf("arr {any} v {any}\n", .{ arr, v });
    var grid: [3][4]u8 = @splat(@splat(w.hide(@as(u8, 7))));
    grid[1][2] = 0;
    w.printf("grid {any}\n", .{grid});
    const a = w.hide(@as(@Vector(8, i32), .{ 1, -2, 3, -4, 5, -6, 7, -8 }));
    const b = w.hide(@as(@Vector(8, i32), .{ 10, 20, 30, 40, 50, 60, 70, 80 }));
    const pred = a > @as(@Vector(8, i32), @splat(0));
    w.printf("select {any}\n", .{@select(i32, pred, a, b)});
    w.printf("select2 {any}\n", .{@select(i32, b > @as(@Vector(8, i32), @splat(35)), b - a, a * a)});
}

fn whereAmI() std.lang.SourceLocation {
    return @src();
}

fn twice(x: u32) u32 {
    if (@inComptime()) return x * 2 + 1000;
    return x * 2;
}

fn srcAndComptime() void {
    const s = whereAmI();
    const here = @src();
    w.printf("src fn={s} line={d} col={d}\n", .{ s.fn_name, s.line, s.column });
    w.printf("src fn={s} line={d}\n", .{ here.fn_name, here.line });
    const ct = comptime twice(5);
    const rt = twice(w.hide(@as(u32, 5)));
    w.printf("inComptime: comptime={d} runtime={d}\n", .{ ct, rt });
}

fn mathHelpers() void {
    const vals = [_]i64{ -300, -1, 0, 1, 99, 100, 255, 256, 70000, -70000 };
    for (vals) |v0| {
        const v = w.hide(v0);
        const c = std.math.clamp(v, -100, 100);
        const as_u8 = std.math.cast(u8, v);
        const as_i16 = std.math.cast(i16, v);
        const lossy = std.math.lossyCast(u8, v);
        w.printf("v={d} clamp={d} cast_u8={?d} cast_i16={?d} lossy_u8={d}\n", .{ v, c, as_u8, as_i16, lossy });
    }
    const us = [_]u32{ 1, 2, 3, 4, 63, 64, 65, 1000, 1 << 20, (1 << 31) + 1 };
    for (us) |uv| {
        const u = w.hide(uv);
        const neg = std.math.negateCast(u) catch -1;
        const cp = std.math.ceilPowerOfTwo(u32, u) catch 0;
        w.printf("u={d} pow2={} log2={d} ceilPow2={d} negate={d}\n", .{ u, std.math.isPowerOfTwo(u), std.math.log2_int(u32, u), cp, neg });
    }
    const pairs = [_][2]u64{ .{ 12, 18 }, .{ 17, 5 }, .{ 0xffff_ffff, 0x1_0000_0001 }, .{ 360, 84 }, .{ 1, 1 } };
    for (pairs) |p| {
        const a = w.hide(p[0]);
        const b = w.hide(p[1]);
        w.printf("gcd({d},{d})={d} lcm={d} mulWide={d} order={s}\n", .{ a, b, std.math.gcd(a, b), std.math.lcm(a, b), std.math.mulWide(u64, a, b), @tagName(std.math.order(a, b)) });
    }
    for ([_]u12{ 0, 1, 0x7ff, 0x800, 0xfff }) |x0| {
        const x = w.hide(x0);
        const se: i32 = std.math.signExtend(i32, x);
        w.printf("signExtend u12 {x} -> {d}\n", .{ x, se });
    }
}

pub fn main() void {
    minMax();
    absTests();
    exactOps();
    pointers();
    splatSelect();
    srcAndComptime();
    mathHelpers();
    w.puts("done\n");
}
