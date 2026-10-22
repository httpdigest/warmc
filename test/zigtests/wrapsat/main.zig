// Wrapping, saturating and overflow-reporting arithmetic at every width.
//
// `+%` `-%` `*%` and unary `-%` wrap at the type's own width; `+|` `-|` `*|`
// and `<<|` clamp to its range; `@addWithOverflow`, `@subWithOverflow`,
// `@mulWithOverflow` and `@shlWithOverflow` return the wrapped result and a u1
// flag; `std.math.add`/`sub`/`mul`/`shlExact`-free `shl` report overflow as an
// error.  Each is a different lowering: wasm has no saturating scalar
// arithmetic and no overflow flag, so LLVM builds them from compares and
// selects (`i32.lt_u` after an add for unsigned carry, the sign-bit test
// `(a ^ r) & (b ^ r) < 0` for signed overflow) and, for multiplies, from a
// double-width product -- an `i64.mul` for i32, `__multi3` or `__muloti4`
// for i64 and i128.  Narrow types (u5, i11) are computed in i32 and checked
// against their own range afterwards.
//
// Every operation runs over the cross product of a set of edge values for the
// type (0, 1, -1, max, min, max-1, min+1 and a few middles), so every carry,
// borrow and clamp boundary is crossed in both directions.
const std = @import("std");
const w = @import("wasi");

fn edgeList(comptime T: type) [9]T {
    const max = std.math.maxInt(T);
    const min = std.math.minInt(T);
    const signed = @typeInfo(T).int.signedness == .signed;
    return .{
        0,
        1,
        max,
        min,
        max - 1,
        if (signed) min + 1 else 2,
        if (signed) -1 else max / 2,
        max / 2 + 1,
        if (signed) min / 2 else 3,
    };
}

fn run(comptime T: type) void {
    const name = @typeName(T);
    const e = edgeList(T);
    for (e, 0..) |a0, i| {
        for (e, 0..) |b0, j| {
            const a = w.hide(a0);
            const b = w.hide(b0);
            const ao = @addWithOverflow(a, b);
            const so = @subWithOverflow(a, b);
            const mo = @mulWithOverflow(a, b);
            w.printf("{s} {d},{d}: a={d} b={d} wrap +{d} -{d} *{d} sat +{d} -{d} *{d} ovf +{d}/{d} -{d}/{d} *{d}/{d}\n", .{
                name, i,     j,     a,     b,
                a +% b, a -% b, a *% b, a +| b, a -| b,
                a *| b, ao[0], ao[1], so[0], so[1],
                mo[0], mo[1],
            });
        }
        const a = w.hide(a0);
        w.printf("{s} {d}: neg%={d}", .{ name, i, -%a });
        if (@typeInfo(T).int.signedness == .signed and a != std.math.minInt(T)) {
            w.printf(" neg={d} abs={d}", .{ -a, @abs(a) });
        }
        // Shifts by every amount below the width, wrapping, saturating and
        // reporting the bits lost.
        const bits = @typeInfo(T).int.bits;
        const ShiftT = std.math.Log2Int(T);
        var s: u32 = 0;
        while (s < bits) : (s += (bits / 7) + 1) {
            const sh: ShiftT = @intCast(w.hide(s));
            const lo = @shlWithOverflow(a, sh);
            w.printf(" <<{d}:{d}/{d}/{d}", .{ s, a << sh, a <<| sh, lo[1] });
        }
        w.puts("\n");
    }
}

fn checked(comptime T: type) void {
    // std.math's error-returning forms, over the same edges.
    const e = edgeList(T);
    var ok: u32 = 0;
    var overflow: u32 = 0;
    var digest: u64 = 0;
    for (e) |a0| {
        for (e) |b0| {
            const a = w.hide(a0);
            const b = w.hide(b0);
            inline for (.{ std.math.add, std.math.sub, std.math.mul }) |f| {
                if (f(T, a, b)) |r| {
                    ok += 1;
                    digest = digest *% 31 +% @as(u64, @truncate(@as(u128, @bitCast(@as(i128, r)))));
                } else |err| {
                    overflow += 1;
                    digest = digest *% 31 +% @intFromError(err);
                    if (overflow <= 3) w.printf("  {s}: {d} op {d} -> {s}\n", .{ @typeName(T), a, b, @errorName(err) });
                }
            }
        }
    }
    w.printf("checked {s}: ok={d} overflow={d} digest={x}\n", .{ @typeName(T), ok, overflow, digest });
}

fn accumulateSat() void {
    // A running saturating sum that reaches each rail and comes back.
    var su8: u8 = 0;
    var si8: i8 = 0;
    var su16: u16 = 0;
    var si32: i32 = 0;
    var si64: i64 = 0;
    var su5: u5 = 0;
    var si11: i11 = 0;
    var x: u32 = w.hide(@as(u32, 99991));
    var i: u32 = 0;
    while (i < 60) : (i += 1) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        const d: i32 = @as(i32, @intCast(x % 201)) - 100 + (if (i < 30) @as(i32, 40) else -40);
        su8 = if (d >= 0) su8 +| @as(u8, @intCast(@min(d, 255))) else su8 -| @as(u8, @intCast(@min(-d, 255)));
        si8 +|= @as(i8, @intCast(std.math.clamp(d, -128, 127)));
        su16 = if (d >= 0) su16 +| @as(u16, @intCast(d)) *| 400 else su16 -| @as(u16, @intCast(-d)) *| 400;
        si32 = si32 *| 3 +| d *| 1_000_000;
        si64 = si64 *| 7 +| @as(i64, d) *| 1_000_000_000_000;
        su5 = if (d >= 0) su5 +| @as(u5, @truncate(@as(u32, @intCast(d)))) else su5 -| 3;
        si11 = si11 +| @as(i11, @intCast(d)) *| 9;
        w.printf("sat[{d}] d={d} u8={d} i8={d} u16={d} i32={d} i64={d} u5={d} i11={d}\n", .{ i, d, su8, si8, su16, si32, si64, su5, si11 });
    }
}

fn wideOverflow() void {
    // The 128-bit forms go through compiler-rt; walk a product towards the
    // edge and report where each width first overflows.
    var p128: u128 = w.hide(@as(u128, 1));
    var s128: i128 = w.hide(@as(i128, -1));
    var p64: u64 = w.hide(@as(u64, 1));
    var s64: i64 = w.hide(@as(i64, 1));
    var k: u32 = 0;
    while (k < 45) : (k += 1) {
        const f: u32 = 3 + k % 5;
        const m = @mulWithOverflow(p128, f);
        const n = @mulWithOverflow(s128, -@as(i128, f));
        const o = @mulWithOverflow(p64, f);
        const q = @mulWithOverflow(s64, -@as(i64, f));
        w.printf("mul[{d}] x{d}: u128={d}/{d} i128={d}/{d} u64={d}/{d} i64={d}/{d}\n", .{ k, f, m[0], m[1], n[0], n[1], o[0], o[1], q[0], q[1] });
        p128 = if (m[1] == 0) m[0] else p128 / 1000 + 1;
        s128 = if (n[1] == 0) n[0] else @divTrunc(s128, 1000) - 1;
        p64 = if (o[1] == 0) o[0] else p64 / 1000 + 1;
        s64 = if (q[1] == 0) q[0] else @divTrunc(s64, 1000) - 1;
    }
    w.printf("sat128: {d} {d} {d} {d}\n", .{
        w.hide(@as(u128, std.math.maxInt(u128) - 5)) +| 10,
        w.hide(@as(i128, std.math.minInt(i128) + 5)) -| 10,
        w.hide(@as(i128, std.math.maxInt(i128) / 3)) *| -4,
        w.hide(@as(u128, 1)) <<| 200,
    });
}

pub fn main() void {
    run(u8);
    run(i8);
    run(u5);
    run(i11);
    run(u16);
    run(i16);
    run(u32);
    run(i32);
    run(u64);
    run(i64);
    run(u128);
    run(i128);
    checked(u8);
    checked(i8);
    checked(i16);
    checked(u32);
    checked(i32);
    checked(i64);
    checked(u64);
    accumulateSat();
    wideOverflow();
    w.puts("done\n");
}
