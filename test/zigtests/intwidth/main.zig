// Integers of arbitrary bit width: u3, u7, i17, u24, u48, i63, u96, u128,
// i128, u256.  Zig gives every width its own wrap-around, so LLVM legalises
// each to the next wasm register (i32 or i64, or a pair or quad of i64 in
// memory for the wide ones) and inserts masks and sign-extensions after every
// operation: `i32.and`, `i64.extend32_s` and shift pairs `shl`/`shr_s` to
// re-sign a 17-bit field.  The 128-bit and wider multiplies and divides are
// compiler-rt calls (`__multi3`, `__udivti3`, `__divti3`, `__umodti3`,
// `__udivei4`) compiled into the module.
//
// Every operation is printed for a spread of inputs that sit on the edges of
// each width (0, 1, max, min, max-1, values with the top bit set), and the
// inputs go through `w.hide` so they are not folded away.  Conversions between
// widths are covered too: @intCast where the value fits, @truncate where it
// does not, @bitCast between signed and unsigned of one width, and widening
// sign- and zero-extension.
const std = @import("std");
const w = @import("wasi");

fn show(comptime T: type, name: []const u8, vals: []const T) void {
    for (vals, 0..) |v, i| {
        const a = w.hide(v);
        const b = w.hide(vals[(i * 7 + 3) % vals.len]);
        w.printf("{s}[{d}] a={d} b={d} a+%b={d} a-%b={d} a*%b={d} a^b={d} a&b={d} a|b={d} ~a={d} lt={} eq={}\n", .{
            name, i, a, b, a +% b, a -% b, a *% b, a ^ b, a & b, a | b, ~a, a < b, a == b,
        });
        if (b != 0) {
            if (@typeInfo(T).int.signedness == .unsigned) {
                w.printf("  a/b={d} a%b={d}\n", .{ a / b, a % b });
            } else if (!(a == std.math.minInt(T) and b == -1)) {
                w.printf("  trunc={d} floor={d} rem={d} mod={d}\n", .{
                    @divTrunc(a, b), @divFloor(a, b), @rem(a, b), @mod(a, b),
                });
            }
        }
    }
}

fn edges(comptime T: type) [10]T {
    const max = std.math.maxInt(T);
    const min = std.math.minInt(T);
    var out: [10]T = undefined;
    out[0] = 0;
    out[1] = 1;
    out[2] = max;
    out[3] = min;
    out[4] = max - 1;
    out[5] = if (min < 0) min + 1 else 2;
    out[6] = max / 3;
    out[7] = if (min < 0) min / 3 else max / 5 * 4;
    out[8] = if (@bitSizeOf(T) > 3) 5 else 3;
    out[9] = if (min < 0) -1 else max >> 1;
    return out;
}

fn widthTable(comptime T: type) void {
    const e = edges(T);
    show(T, @typeName(T), &e);
}

fn convert() void {
    // Widening and narrowing between the odd widths.
    const src = [_]i64{ 0, 1, -1, 65535, -65536, 70000, -70000, 0x7fff_ffff, -0x8000_0000, 0x1_2345_6789, -0x1_2345_6789, std.math.maxInt(i64), std.math.minInt(i64) };
    for (src, 0..) |s0, i| {
        const s = w.hide(s0);
        const t17: i17 = @truncate(s);
        const u24v: u24 = @truncate(@as(u64, @bitCast(s)));
        const u48v: u48 = @truncate(@as(u64, @bitCast(s)));
        const i63v: i63 = @truncate(s);
        const u3v: u3 = @truncate(@as(u64, @bitCast(s)));
        const back: i64 = t17; // sign extension
        const zext: u64 = u24v; // zero extension
        const as_signed: i48 = @bitCast(u48v);
        const wide: i128 = s;
        const uwide: u128 = @bitCast(wide);
        w.printf("conv[{d}] s={d} i17={d} u24={d} u48={d} i48={d} i63={d} u3={d} back={d} zext={d} i128={d} u128={d}\n", .{
            i, s, t17, u24v, u48v, as_signed, i63v, u3v, back, zext, wide, uwide,
        });
        if (s >= std.math.minInt(i17) and s <= std.math.maxInt(i17)) {
            const fit: i17 = @intCast(s);
            w.printf("  fits i17: {d}\n", .{fit});
        }
        if (s >= 0 and s <= std.math.maxInt(u48)) {
            const fit: u48 = @intCast(s);
            w.printf("  fits u48: {d}\n", .{fit});
        }
    }
}

fn wideMath() void {
    // u128 and i128 through full multiply, divide and remainder.
    const xs = [_]u128{
        0,
        1,
        0xffff_ffff_ffff_ffff,
        0x1_0000_0000_0000_0000,
        0xdead_beef_cafe_babe_0123_4567_89ab_cdef,
        std.math.maxInt(u128),
        std.math.maxInt(u128) / 3,
        12345678901234567890123456789,
    };
    for (xs, 0..) |x0, i| {
        for (xs, 0..) |y0, j| {
            const x = w.hide(x0);
            const y = w.hide(y0);
            const p = x *% y;
            const s = x +% y;
            w.printf("u128[{d},{d}] sum={d} prod={x} shl={x} shr={x}", .{ i, j, s, p, x << @truncate(j * 17), y >> @truncate(i * 15) });
            if (y != 0) w.printf(" div={d} mod={d}", .{ x / y, x % y });
            w.puts("\n");
            const sx: i128 = @bitCast(x);
            const sy: i128 = @bitCast(y);
            w.printf("i128[{d},{d}] {d} {d} prod={d} sar={d}", .{ i, j, sx, sy, sx *% sy, sx >> @truncate(j * 13) });
            if (sy != 0 and !(sx == std.math.minInt(i128) and sy == -1)) {
                w.printf(" trunc={d} floor={d} rem={d} mod={d}", .{ @divTrunc(sx, sy), @divFloor(sx, sy), @rem(sx, sy), @mod(sx, sy) });
            }
            w.puts("\n");
        }
    }
}

fn u256Math() void {
    var a: u256 = w.hide(@as(u256, 0x0123_4567_89ab_cdef_fedc_ba98_7654_3210_0f1e_2d3c_4b5a_6978_8796_a5b4_c3d2_e1f0));
    var b: u256 = w.hide(@as(u256, 0xffff_0000_ffff_0000_1234_5678_9abc_def0_1111_2222_3333_4444_5555_6666_7777_8888));
    var k: u32 = 0;
    while (k < 12) : (k += 1) {
        const p = a *% b;
        const q = a / (b >> @intCast(k * 20 + 1) | 1);
        const r = a % (b >> @intCast(k * 20 + 1) | 1);
        w.printf("u256[{d}] a={x} b={x}\n  p={x}\n  q={x} r={x}\n", .{ k, a, b, p, q, r });
        a = (a << 7) ^ (a >> 3) ^ p;
        b = b *% 0x9e37_79b9_7f4a_7c15 +% k;
    }
}

fn accumulate() void {
    // Running sums kept in odd widths, so every step wraps at its own width.
    var s3: u3 = 0;
    var s7: u7 = 0;
    var s17: i17 = 0;
    var s24: u24 = 0;
    var s48: u48 = 0;
    var s63: i63 = 0;
    var x: u32 = w.hide(@as(u32, 2463534242));
    var i: u32 = 0;
    while (i < 40) : (i += 1) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        s3 +%= @truncate(x);
        s7 = s7 *% 5 +% @as(u7, @truncate(x >> 3));
        s17 -%= @bitCast(@as(u17, @truncate(x >> 7)));
        s24 = (s24 << 3) ^ @as(u24, @truncate(x));
        s48 = s48 *% 31 +% x;
        s63 = s63 *% -3 +% @as(i63, @as(i32, @bitCast(x)));
        w.printf("acc[{d}] x={d} u3={d} u7={d} i17={d} u24={d} u48={d} i63={d}\n", .{ i, x, s3, s7, s17, s24, s48, s63 });
    }
}

pub fn main() void {
    widthTable(u3);
    widthTable(u7);
    widthTable(i17);
    widthTable(u24);
    widthTable(u48);
    widthTable(i63);
    widthTable(u96);
    widthTable(i128);
    convert();
    wideMath();
    u256Math();
    accumulate();
    w.puts("done\n");
}
