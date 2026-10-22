// The float widths wasm does not have: f16, f80 and f128.
//
// f16 is carried in an f32 register: every operation is `__extendhfsf2` on
// the way in, the f32 instruction, and `__truncsfhf2` on the way out, so a
// chain of f16 arithmetic is a chain of calls with a rounding step between
// each.  f80 and f128 are entirely soft-float: `__addtf3`, `__multf3`,
// `__divtf3`, `__letf2`, `__extenddftf2`, `__fixtfti`, `__floattitf` and the
// x87-format counterparts, compiled from compiler-rt into the module and
// passed and returned through memory as pairs of i64.  That is a lot of
// 64-bit shifting, `i64.clz` normalisation and 128-bit integer arithmetic on
// the mantissas -- a test of the integer back end as much as of floats.
//
// Every value is printed in decimal (std's formatter has f80 and f128 paths
// with u128 digit generation) and as its exact bit pattern.  NaNs, whose sign
// and payload wasm leaves open for the f16 path, print only as "nan".
const std = @import("std");
const w = @import("wasi");
const math = std.math;

fn bitsOf(comptime T: type, x: T) @Int(.unsigned, @bitSizeOf(T)) {
    return @bitCast(x);
}

fn show(comptime T: type, x: T) void {
    if (math.isNan(x)) {
        w.puts("nan");
    } else {
        w.printf("{e}/{x}", .{ x, bitsOf(T, x) });
    }
}

fn binops(comptime T: type, name: []const u8, vals: []const T, stride: usize) void {
    for (vals, 0..) |a0, i| {
        for (vals, 0..) |b0, j| {
            if ((i + j * stride) % 3 != 0) continue;
            const a = w.hide(a0);
            const b = w.hide(b0);
            w.printf("{s}[{d},{d}] ", .{ name, i, j });
            show(T, a + b);
            w.puts(" ");
            show(T, a - b);
            w.puts(" ");
            show(T, a * b);
            w.puts(" ");
            show(T, a / b);
            w.printf(" lt={} le={} eq={} gt={}\n", .{ a < b, a <= b, a == b, a > b });
        }
    }
}

const f16s = [_]f16{ 0.0, -0.0, 1.0, -1.0, 0.1, 3.140625, -2.5, 1000.0, 65504.0, 6.103515625e-5, 5.960464477539063e-8, math.inf(f16), math.nan(f16), 0.333251953125 };
const f80s = [_]f80{ 0.0, -0.0, 1.0, -1.0, 0.1, 1.0 / 3.0, -2.5, 1e4000, -1e-4000, 3.6451995318824746025e-4951, 18446744073709551615.0, math.inf(f80), math.nan(f80), 1.5 };
const f128s = [_]f128{ 0.0, -0.0, 1.0, -1.0, 0.1, 1.0 / 3.0, -2.5, 1e4000, -1e-4000, 6.475175119438025110924438958227646552e-4966, 340282366920938463463374607431768211455.0, math.inf(f128), math.nan(f128), math.pi };

fn f16Extra() void {
    // Rounding into f16 from wider types: ties go to even, overflow to inf.
    const xs = [_]f32{ 1.0, 1.00048828125, 1.000732421875, 1.0009765625, 2049.0, 2051.0, 65519.0, 65520.0, 1e-8, 2.98023223876953125e-8, 3e-8, -0.1, 0.5, 12345.678 };
    for (xs, 0..) |x0, i| {
        const x = w.hide(x0);
        const h: f16 = @floatCast(x);
        const hd: f16 = @floatCast(@as(f64, x));
        w.printf("f16cast[{d}] {e} -> ", .{ i, x });
        show(f16, h);
        w.puts(" via f64 ");
        show(f16, hd);
        w.printf(" back={e}", .{@as(f32, h)});
        if (!math.isInf(h) and @abs(h) < 30000) w.printf(" int={d}", .{@as(i32, @intFromFloat(h))});
        w.puts("\n");
    }
    const ns = [_]i32{ 0, 1, -1, 2047, 2048, 2049, 2050, 4097, -32769, 65504, 1000, 777 };
    for (ns, 0..) |n0, i| {
        const n = w.hide(n0);
        const h: f16 = @floatFromInt(n);
        w.printf("i2f16[{d}] {d} -> ", .{ i, n });
        show(f16, h);
        w.puts("\n");
    }
    // A running product and sum in f16, rounded every step.
    var p: f16 = w.hide(@as(f16, 1.0));
    var s: f16 = 0;
    var k: u32 = 0;
    while (k < 30) : (k += 1) {
        p = p * 1.0625 + 0.0078125;
        s += p / 7.0;
        w.printf("f16acc[{d}] ", .{k});
        show(f16, p);
        w.puts(" ");
        show(f16, s);
        w.printf(" sqrt={x} floor={x} ceil={x} round={x}\n", .{ bitsOf(f16, @sqrt(s)), bitsOf(f16, @floor(s)), bitsOf(f16, @ceil(s)), bitsOf(f16, @round(s)) });
    }
}

fn wideExtra(comptime T: type, name: []const u8) void {
    // Newton's square root of 2, 3 and 5 to full precision.
    const seeds = [_]T{ 2.0, 3.0, 5.0, 0.5, 1e100 };
    for (seeds, 0..) |a0, i| {
        const a = w.hide(a0);
        var g: T = a;
        if (a > 1e10) g = 1e50;
        var it: u32 = 0;
        while (it < 12) : (it += 1) g = (g + a / g) / 2;
        w.printf("{s}.newton[{d}] ", .{ name, i });
        show(T, g);
        w.puts(" sqrt=");
        show(T, @sqrt(a));
        w.puts("\n");
    }
    // Rounding builtins.
    const rs = [_]T{ 0.5, -0.5, 1.5, -1.5, 2.5, 1e30, -7.25, 123456789.987654321 };
    for (rs, 0..) |r0, i| {
        const r = w.hide(r0);
        w.printf("{s}.round[{d}] {e} floor={e} ceil={e} trunc={e} round={e}\n", .{ name, i, r, @floor(r), @ceil(r), @trunc(r), @round(r) });
    }
    // Integer conversions, both ways, through the full 128-bit range.
    const ints = [_]i128{ 0, 1, -1, 1 << 64, -(1 << 100), std.math.maxInt(i64), 12345678901234567890123456789, -98765432109876543210, 1 << 112, (1 << 113) - 1 };
    for (ints, 0..) |n0, i| {
        const n = w.hide(n0);
        const x: T = @floatFromInt(n);
        const u: T = @floatFromInt(@as(u128, @bitCast(n)) >> 1);
        w.printf("{s}.i2f[{d}] {d} -> ", .{ name, i, n });
        show(T, x);
        w.puts(" u>>1 -> ");
        show(T, u);
        const back: i128 = @intFromFloat(x);
        const half: T = x / 3;
        const back3: i128 = @intFromFloat(half);
        w.printf(" back={d} third={d}\n", .{ back, back3 });
    }
    // Conversions to and from the narrower floats.
    const ds = [_]f64{ 0.1, 1.0 / 3.0, 1e300, 5e-324, -2.2250738585072014e-308, 123.456 };
    for (ds, 0..) |d0, i| {
        const d = w.hide(d0);
        const x: T = d;
        const third = x / 3;
        const back: f64 = @floatCast(third);
        const back32: f32 = @floatCast(third);
        const back16: f16 = @floatCast(x);
        w.printf("{s}.ext[{d}] ", .{ name, i });
        show(T, x);
        w.puts(" /3=");
        show(T, third);
        w.printf(" ->f64 {x} ->f32 {x} ->f16 {x}\n", .{ bitsOf(f64, back), bitsOf(f32, back32), bitsOf(f16, back16) });
    }
    // A long accumulation: sum of 1/k^2, which converges to pi^2/6.
    var acc: T = 0;
    var k: u32 = 1;
    while (k <= 600) : (k += 1) {
        const kf: T = @floatFromInt(w.hide(k));
        acc += 1 / (kf * kf);
        if (k % 50 == 0) {
            w.printf("{s}.basel[{d}] ", .{ name, k });
            show(T, acc);
            w.puts("\n");
        }
    }
    const fma = @mulAdd(T, w.hide(@as(T, 1.0 / 3.0)), 3.0, -1.0);
    w.printf("{s}.mulAdd ", .{name});
    show(T, fma);
    w.puts("\n");
}

pub fn main() void {
    binops(f16, "f16", &f16s, 1);
    binops(f80, "f80", &f80s, 2);
    binops(f128, "f128", &f128s, 2);
    f16Extra();
    wideExtra(f80, "f80");
    wideExtra(f128, "f128");
    w.puts("done\n");
}
