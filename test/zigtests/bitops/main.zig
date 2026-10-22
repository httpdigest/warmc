// Shifts and bit-counting builtins at many widths.
//
// Zig's `<<` and `>>` take a shift amount of type `Log2Int(T)`, so a shift is
// always in range and LLVM emits a bare `i32.shl`/`i64.shr_u`/`i64.shr_s` --
// except at odd widths, where the value is first masked (u24, u40) or
// sign-extended (i12, i40) into the register, and at 128 bits, where a shift
// is two registers and a funnel of `shl`, `shr_u` and `or` with a select on
// whether the amount crosses 64.  `std.math.shl`/`shr` accept any amount and
// produce zero past the width; `std.math.rotl`/`rotr` become `i32.rotl` and
// `i64.rotr` where the width is native and shift pairs where it is not.
//
// `@popCount` `@clz` `@ctz` map to `i32.popcnt` `i64.clz` `i64.ctz`, with an
// adjustment for narrow types (a clz of a u24 is the i32 clz minus 8, a ctz
// of a u24 must not count past bit 24) and two instructions combined for 128
// bits.  `@byteSwap` and `@bitReverse` have no wasm instruction at all and are
// open-coded as shift-and-mask ladders.  `@shlExact`/`@shrExact` appear only
// where no set bit is shifted out.  Every result is printed for a fixed set of
// patterns that put set bits at both ends and in the middle.
const std = @import("std");
const w = @import("wasi");

fn patterns(comptime T: type) [12]T {
    const U = @Int(.unsigned, @bitSizeOf(T));
    const max: U = std.math.maxInt(U);
    const ps = [12]U{
        0,
        1,
        max,
        max >> 1,
        ~(max >> 1),
        @truncate(0x8040_2010_0804_0201_8040_2010_0804_0201),
        @truncate(0x0123_4567_89ab_cdef_fedc_ba98_7654_3210),
        @truncate(0x5555_5555_5555_5555_5555_5555_5555_5555),
        @truncate(0xf0f0_0f0f_ff00_00ff_ffff_0000_0000_ffff),
        @truncate(0x0000_0000_0001_0000_0000_0000_0010_0000),
        @truncate(0x8000_0000_0000_0000_0000_0000_0000_0003),
        @truncate(0xdead_beef_0bad_f00d_cafe_babe_feed_face),
    };
    var out: [12]T = undefined;
    for (ps, 0..) |p, i| out[i] = @bitCast(p);
    return out;
}

fn counts(comptime T: type) void {
    const ps = patterns(T);
    const U = @Int(.unsigned, @bitSizeOf(T));
    for (ps, 0..) |p0, i| {
        const p = w.hide(p0);
        const u: U = @bitCast(p);
        w.printf("{s}[{d}] v={x} pop={d} clz={d} ctz={d}", .{ @typeName(T), i, u, @popCount(p), @clz(p), @ctz(p) });
        if (@bitSizeOf(T) % 8 == 0) {
            const s: U = @bitCast(@byteSwap(p));
            w.printf(" bswap={x}", .{s});
        }
        const r: U = @bitCast(@bitReverse(p));
        w.printf(" brev={x}", .{r});
        // The classic tricks: lowest set bit, clear lowest, next power of two.
        const low = u & (~u +% 1);
        const clr = u & (u -% 1);
        w.printf(" low={x} clr={x} pow2={}\n", .{ low, clr, u != 0 and clr == 0 });
    }
}

fn shifts(comptime T: type) void {
    const ps = patterns(T);
    const bits = @bitSizeOf(T);
    const S = std.math.Log2Int(T);
    const U = @Int(.unsigned, bits);
    for (ps, 0..) |p0, i| {
        const p = w.hide(p0);
        var h: u64 = 0;
        var s: u32 = 0;
        while (s < bits) : (s += 1) {
            const sh: S = @intCast(w.hide(s));
            const l: U = @bitCast(p << sh);
            const r: U = @bitCast(p >> sh);
            // Rotation is defined on the unsigned bits only.
            const rl = std.math.rotl(U, @bitCast(p), sh);
            const rr = std.math.rotr(U, @bitCast(p), sh);
            h = (h *% 0x100000001b3) ^ @as(u64, @truncate(l)) ^ (@as(u64, @truncate(r)) << 1) ^ (@as(u64, @truncate(rl)) << 2) ^ (@as(u64, @truncate(rr)) << 3);
            if (s % ((bits / 5) + 1) == 0 or s == bits - 1) {
                w.printf("{s}[{d}] s={d} shl={x} shr={x} rotl={x} rotr={x}\n", .{ @typeName(T), i, s, l, r, rl, rr });
            }
        }
        // std.math.shl/shr take any amount, including past the width.
        const big = [_]u32{ bits - 1, bits, bits + 1, 2 * bits, 1000 };
        for (big) |b| {
            const bl: U = @bitCast(std.math.shl(T, p, w.hide(b)));
            const br: U = @bitCast(std.math.shr(T, p, w.hide(b)));
            w.printf("{s}[{d}] math.shl/shr {d}: {x} {x}\n", .{ @typeName(T), i, b, bl, br });
        }
        w.printf("{s}[{d}] hash={x}\n", .{ @typeName(T), i, h });
    }
}

fn exactShifts() void {
    // Values built so the shift loses nothing.
    var x: u32 = w.hide(@as(u32, 0x1357));
    var i: u5 = 0;
    while (i < 16) : (i += 1) {
        const up = @shlExact(x, i);
        const down = @shrExact(up, i);
        const sx: i32 = -@as(i32, @intCast(x));
        const sup = @shlExact(sx, i);
        w.printf("exact {d}: {x} -> {x} -> {x}  signed {d} -> {d} -> {d}\n", .{ i, x, up, down, sx, sup, sup >> i });
        x = (x *% 3) & 0x7fff;
    }
}

fn mathHelpers() void {
    const vals = [_]u64{ 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 1000, 1023, 1024, 1025, 0xffff_ffff, 0x1_0000_0000, 0x8000_0000_0000_0000 };
    for (vals) |v0| {
        const v = w.hide(v0);
        w.printf("{d}: log2={d} log2ceil={d} ispow2={} ceilpow2={d} floorpow2={d} log10={d}\n", .{
            v,
            std.math.log2_int(u64, v),
            std.math.log2_int_ceil(u64, v),
            std.math.isPowerOfTwo(v),
            std.math.ceilPowerOfTwo(u64, v) catch 0,
            std.math.floorPowerOfTwo(u64, v),
            std.math.log10_int(v),
        });
    }
}

fn fields() void {
    // Bit-field extraction and insertion by hand: a 32-bit instruction word
    // cut into fields of 7, 5, 3, 5, 5 and 7 bits, sign-extending the last.
    var x: u32 = w.hide(@as(u32, 0xC0FFEE11));
    var i: u32 = 0;
    while (i < 20) : (i += 1) {
        x = x *% 1664525 +% 1013904223;
        const opcode: u7 = @truncate(x);
        const rd: u5 = @truncate(x >> 7);
        const f3: u3 = @truncate(x >> 12);
        const rs1: u5 = @truncate(x >> 15);
        const rs2: u5 = @truncate(x >> 20);
        const f7: i7 = @bitCast(@as(u7, @truncate(x >> 25)));
        const imm: i12 = @bitCast(@as(u12, @truncate(x >> 20)));
        const rebuilt = @as(u32, opcode) | @as(u32, rd) << 7 | @as(u32, f3) << 12 | @as(u32, rs1) << 15 | @as(u32, rs2) << 20 | @as(u32, @as(u7, @bitCast(f7))) << 25;
        w.printf("insn {x:0>8}: op={d} rd={d} f3={d} rs1={d} rs2={d} f7={d} imm={d} same={}\n", .{ x, opcode, rd, f3, rs1, rs2, f7, imm, rebuilt == x });
    }
}

fn signedShifts() void {
    const vals = [_]i64{ -1, -2, -3, -1000, std.math.minInt(i64), std.math.minInt(i64) + 1, 12345, -12345, 0x4000_0000_0000_0000 };
    for (vals) |v0| {
        const v = w.hide(v0);
        var s: u6 = 0;
        w.printf("sar {d}:", .{v});
        while (true) : (s += 9) {
            w.printf(" {d}", .{v >> w.hide(s)});
            if (s >= 54) break;
        }
        const v12: i12 = @truncate(v);
        const v40: i40 = @truncate(v);
        w.printf(" | i12 {d} >>3 {d} <<3 {d} | i40 {d} >>7 {d} <<7 {d}\n", .{ v12, v12 >> w.hide(@as(u4, 3)), v12 << w.hide(@as(u4, 3)), v40, v40 >> w.hide(@as(u6, 7)), v40 << w.hide(@as(u6, 7)) });
    }
}

pub fn main() void {
    counts(u8);
    counts(u16);
    counts(u24);
    counts(u32);
    counts(i32);
    counts(u40);
    counts(u64);
    counts(i64);
    counts(u128);
    counts(u3);
    counts(u13);
    shifts(u8);
    shifts(i16);
    shifts(u24);
    shifts(u32);
    shifts(i32);
    shifts(i40);
    shifts(u64);
    shifts(i64);
    shifts(u128);
    shifts(i128);
    exactShifts();
    mathHelpers();
    fields();
    signedShifts();
    w.puts("done\n");
}
