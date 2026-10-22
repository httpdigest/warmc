// @Vector at every lane type wasm's simd128 has, and some it does not.
//
// The corpus builds with +simd128, so these arrive as v128 operations: i8x16
// through i64x2 and f32x4/f64x2 arithmetic, the saturating `add_sat_s/u`,
// lane-wise comparisons producing all-ones masks, `v128.bitselect` for
// @select, `i8x16.shuffle` for @shuffle, `i8x16.popcnt`, the narrowing and
// widening conversions, `f32x4.convert_i32x4_s` and `trunc_sat`.  What wasm
// lacks LLVM builds out of what it has: i8 multiplies widen to i16x8 and
// narrow back, u64 compares flip the sign bit first, per-lane shift amounts and
// integer division are scalarised through `extract_lane`/`replace_lane`,
// @reduce is a tree of shuffles, @Vector(32, u8) is two registers and
// @Vector(3, f32) is a v128 with a lane left over.
//
// Inputs come from a xorshift generator whose seed goes through `w.hide`, and
// every intermediate vector is printed, so a wrong lane anywhere is a diff.
// No NaN is ever produced (its bits are not fixed by wasm) and every
// non-wrapping operation stays in range.
const std = @import("std");
const w = @import("wasi");

var rng: u64 = 0;

fn next() u64 {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

fn randVec(comptime T: type, comptime n: usize) @Vector(n, T) {
    var a: [n]T = undefined;
    for (&a) |*x| {
        const r = next();
        x.* = switch (@typeInfo(T)) {
            .int => @bitCast(@as(@Int(.unsigned, @bitSizeOf(T)), @truncate(r))),
            // Small floats with a fractional part and either sign.
            .float => @as(T, @floatFromInt(@as(i32, @intCast(r % 2001)) - 1000)) / 8.0,
            else => unreachable,
        };
    }
    return a;
}

fn intOps(comptime T: type, comptime n: usize) void {
    const V = @Vector(n, T);
    const name = @typeName(V);
    const bits = @bitSizeOf(T);
    const S = std.math.Log2Int(T);
    var round: usize = 0;
    while (round < 4) : (round += 1) {
        const a: V = randVec(T, n);
        const b: V = randVec(T, n);
        w.printf("{s}[{d}] a={d}\n  b={d}\n", .{ name, round, a, b });
        w.printf("  add%={d}\n  sub%={d}\n  mul%={d}\n", .{ a +% b, a -% b, a *% b });
        w.printf("  add|={d}\n  sub|={d}\n  mul|={d}\n", .{ a +| b, a -| b, a *| b });
        w.printf("  and={d}\n  or={d}\n  xor={d}\n  not={d}\n", .{ a & b, a | b, a ^ b, ~a });
        w.printf("  min={d}\n  max={d}\n", .{ @min(a, b), @max(a, b) });
        const lt = a < b;
        const eq = a == (b & @as(V, @splat(3))) ;
        const one: @Vector(n, u8) = @splat(1);
        const zero: @Vector(n, u8) = @splat(0);
        w.printf("  lt={d}\n  ge={d}\n  eq={d}\n", .{ @select(u8, lt, one, zero), @select(u8, a >= b, one, zero), @select(u8, eq, one, zero) });
        w.printf("  sel={d}\n", .{@select(T, lt, a, b)});
        // Shifts: uniform and per-lane amounts, both kept below the width.
        const sh: @Vector(n, S) = @truncate(@as(@Vector(n, @Int(.unsigned, bits)), @bitCast(b)) % @as(@Vector(n, @Int(.unsigned, bits)), @splat(bits)));
        const k: S = @intCast(round * 3 % bits);
        w.printf("  shl={d}\n  shr={d}\n  shlk={d}\n  shrk={d}\n", .{ a << sh, a >> sh, a << @splat(k), a >> @splat(k) });
        w.printf("  popcnt={d}\n  clz={d}\n  ctz={d}\n", .{ @popCount(a), @clz(a), @ctz(b) });
        if (bits >= 16) w.printf("  bswap={x}\n", .{@byteSwap(a)});
        w.printf("  bitrev={x}\n", .{@bitReverse(a)});
        if (@typeInfo(T).int.signedness == .signed) {
            w.printf("  abs={d}\n  neg%={d}\n", .{ @abs(a), -%a });
        }
        // Division by lanes that are never zero, and never min / -1.
        const d = (b & @as(V, @splat(0x3f))) | @as(V, @splat(1));
        w.printf("  divT={d}\n  rem={d}\n  divF={d}\n  mod={d}\n", .{ @divTrunc(a, d), @rem(a, d), @divFloor(a, d), @mod(a, d) });
        w.printf("  red add={d} mul={d} min={d} max={d} and={d} or={d} xor={d}\n", .{
            @reduce(.Add, a), @reduce(.Mul, a), @reduce(.Min, a), @reduce(.Max, a),
            @reduce(.And, a), @reduce(.Or, a),  @reduce(.Xor, a),
        });
        w.printf("  any lt={} all lt={} count={d}\n", .{ @reduce(.Or, lt), @reduce(.And, lt), std.simd.countTrues(lt) });
    }
}

fn shuffles() void {
    const A = @Vector(8, i16);
    var round: usize = 0;
    while (round < 3) : (round += 1) {
        const a: A = randVec(i16, 8);
        const b: A = randVec(i16, 8);
        const rev = @shuffle(i16, a, undefined, @Vector(8, i32){ 7, 6, 5, 4, 3, 2, 1, 0 });
        const inter = @shuffle(i16, a, b, @Vector(8, i32){ 0, -1, 1, -2, 2, -3, 3, -4 });
        const hi = @shuffle(i16, a, b, @Vector(4, i32){ 4, 5, ~@as(i32, 6), ~@as(i32, 7) });
        const dup = @shuffle(i16, a, undefined, @Vector(16, i32){ 0, 0, 1, 1, 2, 2, 3, 3, 7, 7, 6, 6, 5, 5, 4, 4 });
        w.printf("shuf[{d}] a={d} b={d}\n  rev={d}\n  inter={d}\n  hi={d}\n  dup={d}\n", .{ round, a, b, rev, inter, hi, dup });
        const bytes: @Vector(16, u8) = @bitCast(a);
        const swapped = @shuffle(u8, bytes, undefined, @Vector(16, i32){ 1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14 });
        w.printf("  bytes={x}\n  swapped-as-i16={d}\n", .{ bytes, @as(A, @bitCast(swapped)) });
        w.printf("  rotl2={d}\n  shiftR3={d}\n  reverse={d}\n  join={d}\n", .{
            std.simd.rotateElementsLeft(a, 2),
            std.simd.shiftElementsRight(a, 3, -1),
            std.simd.reverseOrder(b),
            std.simd.join(@as(@Vector(4, i16), @shuffle(i16, a, undefined, @Vector(4, i32){ 0, 2, 4, 6 })), @as(@Vector(4, i16), @shuffle(i16, b, undefined, @Vector(4, i32){ 1, 3, 5, 7 }))),
        });
        w.printf("  prefix add={d}\n", .{std.simd.prefixScan(.Add, 1, a)});
    }
    w.printf("iota u8 x16={d}\n", .{std.simd.iota(u8, 16)});
    w.printf("iota f32 x4={d}\n", .{std.simd.iota(f32, 4)});
}

fn floatOps(comptime T: type, comptime n: usize) void {
    const V = @Vector(n, T);
    const name = @typeName(V);
    var round: usize = 0;
    while (round < 4) : (round += 1) {
        const a: V = randVec(T, n);
        const b: V = randVec(T, n) + @as(V, @splat(0.0625)); // never zero
        w.printf("{s}[{d}] a={d}\n  b={d}\n", .{ name, round, a, b });
        w.printf("  add={d}\n  sub={d}\n  mul={d}\n  div={d}\n", .{ a + b, a - b, a * b, a / b });
        w.printf("  min={d}\n  max={d}\n  abs={d}\n  neg={d}\n", .{ @min(a, b), @max(a, b), @abs(a), -a });
        w.printf("  sqrt|a|={d}\n  floor(a/b)={d}\n  ceil(a/b)={d}\n  trunc(a/b)={d}\n  round(a/b)={d}\n", .{ @sqrt(@abs(a)), @floor(a / b), @ceil(a / b), @trunc(a / b), @round(a / b) });
        w.printf("  mulAdd={d}\n", .{@mulAdd(V, a, b, @as(V, @splat(0.5)))});
        const one: @Vector(n, u8) = @splat(1);
        const zero: @Vector(n, u8) = @splat(0);
        w.printf("  lt={d} eq0={d}\n", .{ @select(u8, a < b, one, zero), @select(u8, a == @as(V, @splat(0)), one, zero) });
        w.printf("  red add={e} mul={e} min={d} max={d}\n", .{ @reduce(.Add, a), @reduce(.Mul, b), @reduce(.Min, a), @reduce(.Max, a) });
        const I = @Int(.signed, @bitSizeOf(T));
        const ai: @Vector(n, I) = @intFromFloat(a);
        const back: V = @floatFromInt(ai *% @as(@Vector(n, I), @splat(3)));
        w.printf("  toint={d}\n  back={d}\n  bits={x}\n", .{ ai, back, @as(@Vector(n, @Int(.unsigned, @bitSizeOf(T))), @bitCast(a / b)) });
    }
}

fn conversions() void {
    var round: usize = 0;
    while (round < 3) : (round += 1) {
        const a: @Vector(16, u8) = randVec(u8, 16);
        const wide: @Vector(16, u16) = a;
        const swide: @Vector(16, i16) = @as(@Vector(16, i8), @bitCast(a));
        const narrowed: @Vector(16, u8) = @truncate(wide *% @as(@Vector(16, u16), @splat(257)) >> @splat(4));
        w.printf("conv[{d}] a={d}\n  zext={d}\n  sext={d}\n  narrow={d}\n", .{ round, a, wide, swide, narrowed });
        const q: @Vector(4, i32) = randVec(i32, 4);
        const qd: @Vector(4, f64) = @floatFromInt(q);
        const qf: @Vector(4, f32) = @floatFromInt(q);
        const qh: @Vector(4, f32) = @floatCast(qd * @as(@Vector(4, f64), @splat(1e-3)));
        const q64: @Vector(4, i64) = q;
        const qu: @Vector(4, u64) = @as(@Vector(4, u32), @bitCast(q));
        w.printf("  q={d}\n  f64={d}\n  f32={d}\n  demote={d}\n  i64={d}\n  u64={d}\n", .{ q, qd, qf, qh, q64 *% q64, qu *% qu });
        // Arrays and vectors convert both ways; a lane picked at run time
        // goes through the array, since a vector takes only constant indices.
        var arr: [8]u32 = randVec(u32, 8);
        var v: @Vector(8, u32) = arr;
        v = w.hide(v);
        arr[round] = 7;
        const v2: @Vector(8, u32) = arr;
        const idx = w.hide(round * 2 + 1);
        w.printf("  v={d}\n  v2={d}\n  lane[{d}]={d} sum={d}\n", .{ v, v2, idx, @as([8]u32, v)[idx], @reduce(.Add, v2 -% v) });
        var ma: [4]i32 = q;
        ma[w.hide(round)] = -5;
        const m: @Vector(4, i32) = ma;
        w.printf("  m={d}\n", .{m});
    }
    // Odd lengths: three f32 lanes, five u16, 32 u8 across two registers.
    var r: usize = 0;
    while (r < 3) : (r += 1) {
        const p: @Vector(3, f32) = randVec(f32, 3);
        const q: @Vector(3, f32) = randVec(f32, 3);
        const cross = @shuffle(f32, p, undefined, @Vector(3, i32){ 1, 2, 0 }) * @shuffle(f32, q, undefined, @Vector(3, i32){ 2, 0, 1 }) -
            @shuffle(f32, p, undefined, @Vector(3, i32){ 2, 0, 1 }) * @shuffle(f32, q, undefined, @Vector(3, i32){ 1, 2, 0 });
        w.printf("vec3[{d}] p={d} q={d} dot={d} cross={d}\n", .{ r, p, q, @reduce(.Add, p * q), cross });
        const five: @Vector(5, u16) = randVec(u16, 5);
        w.printf("u16x5={d} sat+={d} max={d}\n", .{ five, five +| five, @reduce(.Max, five) });
        const big: @Vector(32, u8) = randVec(u8, 32);
        const big2: @Vector(32, u8) = randVec(u8, 32);
        w.printf("u8x32 avg={d}\n  sum={d} eqcount={d}\n", .{
            @as(@Vector(32, u8), @truncate((@as(@Vector(32, u16), big) + big2 + @as(@Vector(32, u16), @splat(1))) >> @splat(1))),
            @reduce(.Add, @as(@Vector(32, u32), big)),
            std.simd.countTrues((big & @as(@Vector(32, u8), @splat(1))) == (big2 & @as(@Vector(32, u8), @splat(1)))),
        });
    }
}

fn kernel() void {
    // A small dot product and a byte histogram written with vectors, checked
    // against the scalar loop.
    var xs: [64]f32 = undefined;
    var ys: [64]f32 = undefined;
    for (&xs, &ys, 0..) |*x, *y, i| {
        x.* = @as(f32, @floatFromInt(@as(i32, @intCast(next() % 200)) - 100)) * 0.25 + @as(f32, @floatFromInt(i));
        y.* = @as(f32, @floatFromInt(@as(i32, @intCast(next() % 64)))) * 0.125;
    }
    var acc: @Vector(4, f32) = @splat(0);
    var i: usize = 0;
    while (i < xs.len) : (i += 4) {
        const a: @Vector(4, f32) = xs[i..][0..4].*;
        const b: @Vector(4, f32) = ys[i..][0..4].*;
        acc += a * b;
        if (i % 16 == 0) w.printf("dot partial[{d}]={d}\n", .{ i, acc });
    }
    var scalar: [4]f32 = .{ 0, 0, 0, 0 };
    for (xs, ys, 0..) |x, y, j| scalar[j % 4] += x * y;
    w.printf("dot vec={d} scalar={d}\n", .{ acc, @as(@Vector(4, f32), scalar) });

    var text: [128]u8 = undefined;
    for (&text) |*c| c.* = @intCast('a' + next() % 6);
    var counts: @Vector(16, u8) = @splat(0);
    var j: usize = 0;
    while (j < text.len) : (j += 16) {
        const chunk: @Vector(16, u8) = text[j..][0..16].*;
        const is_a = chunk == @as(@Vector(16, u8), @splat('a'));
        counts += @select(u8, is_a, @as(@Vector(16, u8), @splat(1)), @as(@Vector(16, u8), @splat(0)));
        const first = std.simd.firstTrue(chunk == @as(@Vector(16, u8), @splat('f')));
        w.printf("chunk[{d}] {s} a-lanes={d} first-f={?d}\n", .{ j, text[j..][0..16], counts, first });
    }
    var scalar_a: u32 = 0;
    for (text) |c| scalar_a += @intFromBool(c == 'a');
    w.printf("a count vec={d} scalar={d}\n", .{ @reduce(.Add, @as(@Vector(16, u32), counts)), scalar_a });
}

pub fn main() void {
    rng = w.hide(@as(u64, 0x9E3779B97F4A7C15));
    intOps(i8, 16);
    intOps(u8, 16);
    intOps(i16, 8);
    intOps(u16, 8);
    intOps(i32, 4);
    intOps(u32, 4);
    intOps(i64, 2);
    intOps(u64, 2);
    intOps(u8, 32);
    shuffles();
    floatOps(f32, 4);
    floatOps(f64, 2);
    floatOps(f32, 8);
    conversions();
    kernel();
    w.puts("done\n");
}
