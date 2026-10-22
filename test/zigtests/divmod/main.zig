// Integer division and remainder in all of Zig's forms, with negative operands.
//
// `@divTrunc` rounds toward zero and `@rem` takes the dividend's sign: these
// are wasm's `div_s`/`rem_s`.  `@divFloor` rounds toward minus infinity and
// `@mod` takes the divisor's sign, which LLVM builds from a `div_s` and a
// fix-up (subtract one when the signs differ and the remainder is non-zero).
// `@divCeil` (new in 0.17) rounds toward plus infinity.
// `@divExact` promises no remainder and becomes a bare `div_s`, or an exact
// shift and a multiply by an inverse when the divisor is constant.
//
// Division by a constant is where a compiler gets clever: LLVM replaces it
// with a multiply by a magic number, a high-half product (`i64.mul` for i32,
// `__multi3` for i64) and a shift, with sign corrections for the signed forms.
// So every divisor below is tried twice, once as a comptime constant (an
// `inline for` over a tuple, each iteration its own strength-reduced divide)
// and once hidden behind `w.hide`, where it is a real `div` instruction --
// and the two results must agree on every dividend, at every width.
// std.math's checked forms (divTrunc, divFloor, divCeil, divExact, mod, rem)
// are covered with their error returns.
const std = @import("std");
const w = @import("wasi");

const divisors = .{ 1, 2, 3, 5, 7, 10, 16, 37, 100, 255, 641, 1000, 65537, -1, -2, -3, -7, -10, -16, -1000 };

fn table(comptime T: type, dividends: []const T) void {
    const signed = @typeInfo(T).int.signedness == .signed;
    inline for (divisors) |dc| {
        if (comptime (!signed and dc < 0)) continue;
        if (comptime (dc > std.math.maxInt(T) or dc < std.math.minInt(T))) continue;
        const d: T = dc;
        const dr = w.hide(d);
        var h: u64 = 0;
        for (dividends) |n0| {
            const n = w.hide(n0);
            if (signed and n == std.math.minInt(T) and d == -1) continue;
            const q1 = @divTrunc(n, d);
            const r1 = @rem(n, d);
            const f1 = @divFloor(n, d);
            const m1 = @mod(n, d);
            const c1 = @divCeil(n, d);
            const q2 = @divTrunc(n, dr);
            const r2 = @rem(n, dr);
            const f2 = @divFloor(n, dr);
            const m2 = @mod(n, dr);
            const c2 = @divCeil(n, dr);
            if (q1 != q2 or r1 != r2 or f1 != f2 or m1 != m2 or c1 != c2) {
                w.printf("MISMATCH {s} {d}/{d}: {d} {d} {d} {d} {d} vs {d} {d} {d} {d} {d}\n", .{ @typeName(T), n, d, q1, r1, f1, m1, c1, q2, r2, f2, m2, c2 });
            }
            // The identities each pair of forms must satisfy.
            if (q1 *% d +% r1 != n or f1 *% d +% m1 != n) {
                w.printf("IDENTITY {s} {d}/{d}\n", .{ @typeName(T), n, d });
            }
            const mix = bits(q1) *% 3 +% bits(r1) +% bits(f1) *% 7 +% bits(m1) +% bits(c1) *% 11;
            h = h *% 1000003 +% @as(u64, @truncate(mix));
        }
        const n = w.hide(dividends[dividends.len / 2]);
        w.printf("{s} /{d}: sample {d} -> trunc {d} rem {d} floor {d} mod {d} ceil {d}  hash {x}\n", .{
            @typeName(T), d, n, @divTrunc(n, d), @rem(n, d), @divFloor(n, d), @mod(n, d), @divCeil(n, d), h,
        });
    }
}

/// Any integer as the u128 with its two's-complement bits, sign-extended.
fn bits(x: anytype) u128 {
    const I = @TypeOf(x);
    if (@typeInfo(I).int.signedness == .signed) return @bitCast(@as(i128, x));
    return x;
}

fn dividendsFor(comptime T: type, buf: []T) []T {
    const max = std.math.maxInt(T);
    const min = std.math.minInt(T);
    var n: usize = 0;
    buf[n] = 0;
    n += 1;
    buf[n] = max;
    n += 1;
    buf[n] = min;
    n += 1;
    buf[n] = max - 1;
    n += 1;
    var x: u64 = w.hide(@as(u64, 0x9e3779b97f4a7c15));
    while (n < buf.len) : (n += 1) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        // Mostly small values, where off-by-one rounding shows, and some large.
        const v: u64 = if (n % 3 == 0) x else x % 2000;
        // @truncate keeps the signedness, so cut to the unsigned type of the
        // same width and reinterpret.
        const t: T = @bitCast(@as(@Int(.unsigned, @bitSizeOf(T)), @truncate(v)));
        buf[n] = if (@typeInfo(T).int.signedness == .signed and n % 2 == 0 and t != min) -t else t;
    }
    return buf;
}

fn signTable() void {
    // The four sign combinations, printed in full.
    const ns = [_]i32{ 7, -7, 6, -6, 1, -1, 0, 13, -13, 2147483647, -2147483648 };
    const ds = [_]i32{ 2, -2, 3, -3, 4, -4, 7, -7, 1 };
    for (ns) |n0| {
        for (ds) |d0| {
            const n = w.hide(n0);
            const d = w.hide(d0);
            w.printf("{d:>11} {d:>3}: trunc {d:>11} floor {d:>11} ceil {d:>11} rem {d:>3} mod {d:>3}\n", .{ n, d, @divTrunc(n, d), @divFloor(n, d), @divCeil(n, d), @rem(n, d), @mod(n, d) });
        }
    }
}

fn mathChecked() void {
    const pairs = [_][2]i32{ .{ 7, 2 }, .{ -7, 2 }, .{ 7, -2 }, .{ -7, -2 }, .{ 8, 4 }, .{ 9, 0 }, .{ -2147483648, -1 }, .{ 10, 3 }, .{ -10, 3 }, .{ 0, 5 } };
    for (pairs) |p| {
        const a = w.hide(p[0]);
        const b = w.hide(p[1]);
        w.printf("math {d} {d}:", .{ a, b });
        if (std.math.divTrunc(i32, a, b)) |v| w.printf(" divTrunc={d}", .{v}) else |e| w.printf(" divTrunc={s}", .{@errorName(e)});
        if (std.math.divFloor(i32, a, b)) |v| w.printf(" divFloor={d}", .{v}) else |e| w.printf(" divFloor={s}", .{@errorName(e)});
        if (std.math.divCeil(i32, a, b)) |v| w.printf(" divCeil={d}", .{v}) else |e| w.printf(" divCeil={s}", .{@errorName(e)});
        if (std.math.divExact(i32, a, b)) |v| w.printf(" divExact={d}", .{v}) else |e| w.printf(" divExact={s}", .{@errorName(e)});
        if (std.math.mod(i32, a, b)) |v| w.printf(" mod={d}", .{v}) else |e| w.printf(" mod={s}", .{@errorName(e)});
        if (std.math.rem(i32, a, b)) |v| w.printf(" rem={d}", .{v}) else |e| w.printf(" rem={s}", .{@errorName(e)});
        w.puts("\n");
    }
}

fn exact() void {
    // @divExact where the remainder is known to be zero: products divided back.
    var x: u32 = w.hide(@as(u32, 12345));
    var i: u32 = 0;
    while (i < 24) : (i += 1) {
        x = x *% 1103515245 +% 12345;
        const a: i32 = @as(i32, @intCast(x % 50000)) - 25000;
        const b: i32 = @as(i32, @intCast((x >> 16) % 300)) - 150;
        if (b == 0) continue;
        const p = a * b;
        const p64 = @as(i64, a) * 1_000_003 * b;
        w.printf("exact[{d}] {d}*{d}={d}: /b={d} /a={d} /6={d} wide={d}\n", .{
            i, a, b, p, @divExact(p, b), if (a != 0) @divExact(p, a) else 0, @divExact(p * 6, 6), @divExact(p64, @as(i64, b) * 1_000_003),
        });
    }
}

fn unsignedPow2() void {
    // Unsigned divides by powers of two and their neighbours: the boundary
    // between a shift and a magic multiply.
    var kk: u32 = 1;
    while (kk < 63) : (kk += 5) {
        const k: u6 = @intCast(kk);
        const p = @as(u64, 1) << k;
        const n = w.hide(@as(u64, 0xfedc_ba98_7654_3210));
        w.printf("u64 n/2^{d}: {d} {d} n/(2^{d}-1): {d} {d} n/(2^{d}+1): {d} {d}\n", .{
            k, n / p, n % p, k, n / (p - 1), n % (p - 1), k, n / (p + 1), n % (p + 1),
        });
        const s = w.hide(@as(i64, -0x7edc_ba98_7654_3210));
        const sp: i64 = @intCast(p);
        w.printf("i64 s/2^{d}: trunc {d} floor {d} rem {d} mod {d} sar {d}\n", .{ k, @divTrunc(s, sp), @divFloor(s, sp), @rem(s, sp), @mod(s, sp), s >> k });
    }
}

pub fn main() void {
    var b8: [64]i8 = undefined;
    var b16: [64]i16 = undefined;
    var b32: [96]i32 = undefined;
    var b64: [96]i64 = undefined;
    var bu32: [96]u32 = undefined;
    var bu64: [96]u64 = undefined;
    var b13: [48]i13 = undefined;
    var b128: [48]i128 = undefined;
    var bu128: [48]u128 = undefined;
    table(i8, dividendsFor(i8, &b8));
    table(i16, dividendsFor(i16, &b16));
    table(i32, dividendsFor(i32, &b32));
    table(i64, dividendsFor(i64, &b64));
    table(u32, dividendsFor(u32, &bu32));
    table(u64, dividendsFor(u64, &bu64));
    table(i13, dividendsFor(i13, &b13));
    table(i128, dividendsFor(i128, &b128));
    table(u128, dividendsFor(u128, &bu128));
    signTable();
    mathChecked();
    exact();
    unsignedPow2();
    w.puts("done\n");
}
