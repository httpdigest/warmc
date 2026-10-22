// f32 and f64: arithmetic over the IEEE special values, comparisons, the
// rounding builtins, conversions to and from integers, and std.math.
//
// Wasm has the four basic operations, sqrt, min/max, the rounding instructions
// and conversions as instructions of its own, and nothing else: @sin, @cos,
// @tan, @exp, @exp2, @log, @log2, @log10 and @mulAdd are calls into
// compiler-rt compiled into the module, and pow, atan2, hypot, cbrt, the
// hyperbolics, frexp, ldexp and nextAfter are std.math's own Zig, so they
// arrive as long runs of f64 arithmetic, `i64.reinterpret_f64` and integer
// bit-twiddling on the exponent.  Zig's @min/@max are not wasm's min/max (they
// return the other operand for a NaN), and @intFromFloat is the trapping
// `i32.trunc_f64_s` family, so every conversion here is range-checked first.
//
// Each result is printed in scientific notation and as its bit pattern, so a
// difference cannot hide in the formatting.  NaN's sign and payload are not
// fixed by wasm (an arithmetic NaN may come out either way), so a NaN prints
// as just "nan" and nothing in the test depends on its bits.
const std = @import("std");
const w = @import("wasi");
const math = std.math;

fn s64(x: f64) void {
    if (math.isNan(x)) {
        w.puts("nan");
    } else {
        w.printf("{e}/{x}", .{ x, @as(u64, @bitCast(x)) });
    }
}

fn s32(x: f32) void {
    if (math.isNan(x)) {
        w.puts("nan");
    } else {
        w.printf("{e}/{x}", .{ x, @as(u32, @bitCast(x)) });
    }
}

fn line64(label: []const u8, x: f64) void {
    w.puts(label);
    w.puts("=");
    s64(x);
    w.puts("\n");
}

const vals64 = [_]f64{
    0.0,                     -0.0,                     1.0,                   -1.0,
    0.1,                     1.0 / 3.0,                -2.5,                  1e300,
    -1e-300,                 5e-324,                   2.2250738585072014e-308, math.floatMax(f64),
    math.inf(f64),           -math.inf(f64),           math.nan(f64),         123456789.125,
};

fn arith64() void {
    for (vals64, 0..) |a0, i| {
        for (vals64, 0..) |b0, j| {
            if ((i + j) % 3 != 0) continue;
            const a = w.hide(a0);
            const b = w.hide(b0);
            w.printf("f64[{d},{d}] ", .{ i, j });
            s64(a + b);
            w.puts(" ");
            s64(a - b);
            w.puts(" ");
            s64(a * b);
            w.puts(" ");
            s64(a / b);
            w.puts(" ");
            s64(@min(a, b));
            w.puts(" ");
            s64(@max(a, b));
            w.printf(" lt={} le={} eq={} ne={} gt={} ge={}\n", .{ a < b, a <= b, a == b, a != b, a > b, a >= b });
        }
    }
}

const vals32 = [_]f32{
    0.0,              -0.0,            1.0,             -1.0,
    0.1,              1.0 / 3.0,       16777216.0,      16777217.0,
    1e38,             -1e-38,          1e-45,           math.floatMax(f32),
    math.inf(f32),    -math.inf(f32),  math.nan(f32),   -7.75,
};

fn arith32() void {
    for (vals32, 0..) |a0, i| {
        for (vals32, 0..) |b0, j| {
            if ((i * 3 + j) % 4 != 1) continue;
            const a = w.hide(a0);
            const b = w.hide(b0);
            w.printf("f32[{d},{d}] ", .{ i, j });
            s32(a + b);
            w.puts(" ");
            s32(a - b);
            w.puts(" ");
            s32(a * b);
            w.puts(" ");
            s32(a / b);
            w.puts(" ");
            s32(@min(a, b));
            w.puts(" ");
            s32(@max(a, b));
            w.printf(" lt={} eq={} gt={}\n", .{ a < b, a == b, a > b });
        }
    }
}

fn classify() void {
    for (vals64, 0..) |a0, i| {
        const a = w.hide(a0);
        w.printf("class[{d}] nan={} inf={} pinf={} ninf={} finite={} normal={} pzero={} nzero={}", .{
            i,                    math.isNan(a),          math.isInf(a),          math.isPositiveInf(a),
            math.isNegativeInf(a), math.isFinite(a),      math.isNormal(a),       math.isPositiveZero(a),
            math.isNegativeZero(a),
        });
        if (!math.isNan(a)) {
            w.printf(" signbit={} abs=", .{math.signbit(a)});
            s64(@abs(a));
            w.puts(" neg=");
            s64(-a);
            w.puts(" copysign(2,a)=");
            s64(math.copysign(@as(f64, 2.0), a));
        }
        w.puts("\n");
    }
}

fn rounding() void {
    const xs = [_]f64{ 0.5, -0.5, 1.5, -1.5, 2.5, -2.5, 2.4999999999999996, 0.49999999999999994, -0.49999999999999994, 4503599627370495.5, -4503599627370497.0, 1e20, -1e-20, 7.0, -7.000001, 123.456, -0.0 };
    for (xs, 0..) |x0, i| {
        const x = w.hide(x0);
        w.printf("round64[{d}] x={e} floor={d} ceil={d} round={d} trunc={d}\n", .{ i, x, @floor(x), @ceil(x), @round(x), @trunc(x) });
        const f: f32 = @floatCast(x);
        w.printf("round32[{d}] x={e} floor={d} ceil={d} round={d} trunc={d} bits={x}\n", .{ i, f, @floor(f), @ceil(f), @round(f), @trunc(f), @as(u32, @bitCast(@round(f))) });
        const m = math.modf(x);
        w.printf("  modf ipart={d} fpart={e}\n", .{ m.ipart, m.fpart });
    }
}

fn conversions() void {
    const ints = [_]i64{ 0, 1, -1, 16777217, -16777217, 9007199254740993, -9007199254740993, math.maxInt(i64), math.minInt(i64), 0x7fff_ffff, -0x8000_0000, 1 << 53, (1 << 53) + 1, 123456789012345 };
    for (ints, 0..) |n0, i| {
        const n = w.hide(n0);
        const u: u64 = @bitCast(n);
        const a: f64 = @floatFromInt(n);
        const b: f32 = @floatFromInt(n);
        const c: f64 = @floatFromInt(u);
        const d: f32 = @floatFromInt(u);
        const e: f64 = @floatFromInt(@as(i32, @truncate(n)));
        const f: f32 = @floatFromInt(@as(u32, @truncate(u)));
        w.printf("i2f[{d}] n={d} f64={d} f32={d} u64->f64={d} u64->f32={d} i32->f64={d} u32->f32={d}\n", .{ i, n, a, b, c, d, e, f });
        w.printf("  bits {x} {x} {x} {x}\n", .{ @as(u64, @bitCast(a)), @as(u32, @bitCast(b)), @as(u64, @bitCast(c)), @as(u32, @bitCast(d)) });
    }
    const fs = [_]f64{ 0.0, -0.0, 0.9999, -0.9999, 1.5, -1.5, 2147483647.9, -2147483648.9, 4294967295.5, 9.2e18, -9.2e18, 1.8e19, 123456.789, -98765.4321 };
    for (fs, 0..) |x0, i| {
        const x = w.hide(x0);
        w.printf("f2i[{d}] x={e}", .{ i, x });
        if (x > -2147483649.0 and x < 2147483648.0) w.printf(" i32={d}", .{@as(i32, @intFromFloat(x))});
        if (x > -1.0 and x < 4294967296.0) w.printf(" u32={d}", .{@as(u32, @intFromFloat(x))});
        if (x >= -9223372036854775808.0 and x < 9223372036854775808.0) w.printf(" i64={d}", .{@as(i64, @intFromFloat(x))});
        if (x > -1.0 and x < 18446744073709551616.0) w.printf(" u64={d}", .{@as(u64, @intFromFloat(x))});
        if (x > -129.0 and x < 128.0) w.printf(" i8={d}", .{@as(i8, @intFromFloat(x))});
        if (x > -1.0 and x < 65536.0) w.printf(" u16={d}", .{@as(u16, @intFromFloat(x))});
        const g: f32 = @floatCast(x);
        if (g > -2147483649.0 and g < 2147483648.0) w.printf(" f32->i32={d}", .{@as(i32, @intFromFloat(g))});
        w.puts("\n");
    }
    // f64 -> f32 rounding: halfway cases go to even.
    const narrow = [_]f64{ 1.0000000596046448, 1.0000001788139343, 3.4028235677973366e38, 3.4028234663852886e38, 1e-46, 7.006492321624085e-46, 1.401298464324817e-45, 0.1, -0.1, 1e39 };
    for (narrow, 0..) |x0, i| {
        const x = w.hide(x0);
        const g: f32 = @floatCast(x);
        w.printf("narrow[{d}] {e} -> {e} bits={x} back={e}\n", .{ i, x, g, @as(u32, @bitCast(g)), @as(f64, g) });
    }
}

fn mathFns() void {
    const xs = [_]f64{ 0.0, 0.25, 0.5, 1.0, 1.5, 2.0, 3.0, 10.0, 100.0, 0.001, 1e-10, 12.5, 30.0, 3.141592653589793, 0.7853981633974483 };
    for (xs, 0..) |x0, i| {
        const x = w.hide(x0);
        w.printf("fn[{d}] x={e}\n", .{ i, x });
        line64("  sqrt", @sqrt(x));
        line64("  sin", @sin(x));
        line64("  cos", @cos(x));
        line64("  tan", @tan(x));
        line64("  exp", @exp(x));
        line64("  exp2", @exp2(x));
        line64("  expm1", math.expm1(x));
        if (x > 0) {
            line64("  log", @log(x));
            line64("  log2", @log2(x));
            line64("  log10", @log10(x));
        }
        line64("  log1p", math.log1p(x));
        line64("  cbrt", math.cbrt(x));
        line64("  cbrt(-x)", math.cbrt(-x));
        line64("  atan", math.atan(x));
        line64("  sinh", math.sinh(x));
        line64("  cosh", math.cosh(x));
        line64("  tanh", math.tanh(x));
        line64("  asinh", math.asinh(x));
        if (x >= 1.0) line64("  acosh", math.acosh(x));
        if (x <= 1.0) {
            line64("  asin", math.asin(x));
            line64("  acos", math.acos(x));
        }
        if (x < 1.0) line64("  atanh", math.atanh(x));
        line64("  pow(x,2.5)", math.pow(f64, x, 2.5));
        line64("  pow(1.5,x)", math.pow(f64, 1.5, x));
        line64("  pow(-2,3)", math.pow(f64, w.hide(@as(f64, -2.0)), 3.0));
        line64("  atan2(x,-1)", math.atan2(x, w.hide(@as(f64, -1.0))));
        line64("  atan2(-1,x)", math.atan2(w.hide(@as(f64, -1.0)), x));
        line64("  hypot(x,3)", math.hypot(x, w.hide(@as(f64, 3.0))));
        line64("  mulAdd", @mulAdd(f64, x, 1.0 / 3.0, -0.1));
        const fr = math.frexp(x);
        w.printf("  frexp significand={e} exponent={d}\n", .{ fr.significand, fr.exponent });
        line64("  ldexp(x,-1030)", math.ldexp(x, -1030));
        line64("  ldexp(x,20)", math.ldexp(x, 20));
        line64("  nextAfter up", math.nextAfter(f64, x, math.inf(f64)));
        line64("  nextAfter down", math.nextAfter(f64, x, -math.inf(f64)));
        if (x != 0) w.printf("  ilogb={d}\n", .{math.ilogb(x)});
        const xf: f32 = @floatCast(x);
        w.puts("  f32 sqrt/sin/exp/log2/pow = ");
        s32(@sqrt(xf));
        w.puts(" ");
        s32(@sin(xf));
        w.puts(" ");
        s32(@exp(xf));
        w.puts(" ");
        s32(@log2(xf + 1.0));
        w.puts(" ");
        s32(math.pow(f32, xf, 0.75));
        w.puts(" ");
        s32(@mulAdd(f32, xf, xf, -2.0));
        w.puts("\n");
    }
}

fn series() void {
    // Harmonic sums, naive and compensated, and Newton's square root.
    var naive: f32 = 0;
    var kahan: f32 = 0;
    var comp: f32 = 0;
    var d: f64 = 0;
    var k: u32 = 1;
    while (k <= 4000) : (k += 1) {
        const t: f32 = 1.0 / @as(f32, @floatFromInt(w.hide(k)));
        naive += t;
        const y = t - comp;
        const s = kahan + y;
        comp = (s - kahan) - y;
        kahan = s;
        d += 1.0 / @as(f64, @floatFromInt(k));
        if (k % 250 == 0) w.printf("harm[{d}] naive={x} kahan={x} f64={x}\n", .{ k, @as(u32, @bitCast(naive)), @as(u32, @bitCast(kahan)), @as(u64, @bitCast(d)) });
    }
    var n: u32 = 2;
    while (n < 40) : (n += 3) {
        const a: f64 = @floatFromInt(w.hide(n));
        var g = a;
        var it: u32 = 0;
        while (it < 8) : (it += 1) g = 0.5 * (g + a / g);
        w.printf("newton({d}) = {d:.15} bits={x} sqrt={x}\n", .{ n, g, @as(u64, @bitCast(g)), @as(u64, @bitCast(@sqrt(a))) });
    }
    // Trapezoidal integration of sin over [0, pi].
    var steps: u32 = 4;
    while (steps <= 1024) : (steps *= 2) {
        const h = math.pi / @as(f64, @floatFromInt(w.hide(steps)));
        var acc: f64 = 0;
        var i: u32 = 1;
        while (i < steps) : (i += 1) acc += @sin(h * @as(f64, @floatFromInt(i)));
        w.printf("trapz({d}) = {d:.12} {x}\n", .{ steps, acc * h, @as(u64, @bitCast(acc * h)) });
    }
}

fn bitcasts() void {
    const pats = [_]u64{ 0, 0x8000000000000000, 0x3ff0000000000000, 0x0000000000000001, 0x000fffffffffffff, 0x7fefffffffffffff, 0x7ff0000000000000, 0x4009_21fb_5444_2d18, 0xc0b8_0000_0000_0000 };
    for (pats, 0..) |p0, i| {
        const p = w.hide(p0);
        const x: f64 = @bitCast(p);
        const lo: f32 = @bitCast(@as(u32, @truncate(p >> 16)));
        w.printf("bits[{d}] {x} -> {e} ; f32 {x} -> ", .{ i, p, x, @as(u32, @truncate(p >> 16)) });
        s32(lo);
        w.puts("\n");
    }
}

pub fn main() void {
    arith64();
    arith32();
    classify();
    rounding();
    conversions();
    mathFns();
    series();
    bitcasts();
    w.puts("done\n");
}
