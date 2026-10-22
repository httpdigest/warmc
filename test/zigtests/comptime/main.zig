// Compile-time evaluation and what it leaves behind for run time.
//
// Tables built entirely at comptime -- primes by sieve, Fibonacci numbers in
// u64, a CRC-32 table, Pascal's triangle, a string assembled with `++` and
// `std.fmt.comptimePrint` -- arrive in the module as data segments, and the
// run-time code reads them at indices that pass through `w.hide`, so the
// loads stay as loads from linear memory at computed offsets.  Comptime
// parameters specialise functions per value (a power function unrolled per
// exponent, a shift-and-mask per field width), `inline for` and `inline while`
// unroll over tuples of mixed types and over comptime arrays, and comptime
// branches pick a body per instantiation.  Each specialisation is a separate
// function or an unrolled run of straight-line code in the module, so a
// miscompile in one shows as one line of the output that is wrong.
const std = @import("std");
const w = @import("wasi");

const primes = blk: {
    @setEvalBranchQuota(100000);
    const N = 2000;
    var sieve: [N]bool = @splat(true);
    sieve[0] = false;
    sieve[1] = false;
    var count: usize = 0;
    var i: usize = 2;
    while (i < N) : (i += 1) {
        if (sieve[i]) {
            count += 1;
            var j = i * i;
            while (j < N) : (j += i) sieve[j] = false;
        }
    }
    var out: [count]u16 = undefined;
    var k: usize = 0;
    for (sieve, 0..) |p, n| {
        if (p) {
            out[k] = n;
            k += 1;
        }
    }
    break :blk out;
};

const fib = blk: {
    var t: [94]u64 = undefined;
    t[0] = 0;
    t[1] = 1;
    for (2..t.len) |i| t[i] = t[i - 1] + t[i - 2];
    break :blk t;
};

const crc_table = blk: {
    @setEvalBranchQuota(10000);
    var t: [256]u32 = undefined;
    for (&t, 0..) |*e, n| {
        var c: u32 = n;
        for (0..8) |_| c = if (c & 1 != 0) 0xedb88320 ^ (c >> 1) else c >> 1;
        e.* = c;
    }
    break :blk t;
};

fn crc32(data: []const u8) u32 {
    var c: u32 = 0xffffffff;
    for (data) |b| c = crc_table[(c ^ b) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffff;
}

const pascal = blk: {
    var t: [20][20]u32 = undefined;
    for (0..20) |r| {
        for (0..20) |c| {
            t[r][c] = if (c == 0 or c == r) 1 else if (c > r) 0 else t[r - 1][c - 1] + t[r - 1][c];
        }
    }
    break :blk t;
};

// A string assembled at comptime: names joined by commas, then a formatted
// summary line.
const names = [_][]const u8{ "alpha", "beta", "gamma", "delta", "epsilon" };
const joined = blk: {
    var s: []const u8 = "";
    for (names, 0..) |n, i| {
        s = s ++ (if (i == 0) "" else ",") ++ n;
    }
    break :blk s;
};
const summary = std.fmt.comptimePrint("{d} names, {d} bytes, first={s}, crc-of-joined=0x{x:0>8}, prime#100={d}", .{
    names.len, joined.len, names[0], crc32(joined), primes[99],
});

fn comptimeFact(comptime n: u32) u64 {
    comptime var r: u64 = 1;
    comptime var i: u32 = 2;
    inline while (i <= n) : (i += 1) r *= i;
    return r;
}

// Unrolled per exponent: each call site is its own instantiation.
fn powc(x: u64, comptime e: u32) u64 {
    var r: u64 = 1;
    comptime var k = e;
    var b = x;
    inline while (k > 0) : (k >>= 1) {
        if (k & 1 != 0) r *%= b;
        b *%= b;
    }
    return r;
}

fn field(v: u64, comptime off: u6, comptime width: u7) u64 {
    const mask: u64 = if (width == 64) ~@as(u64, 0) else (@as(u64, 1) << @intCast(width)) - 1;
    return (v >> off) & mask;
}

fn describe(comptime T: type, v: T) void {
    if (comptime @typeInfo(T) == .int) {
        const info = @typeInfo(T).int;
        if (info.signedness == .signed) {
            w.printf("  signed {d}-bit {s}: {d} (min {d})\n", .{ info.bits, @typeName(T), v, std.math.minInt(T) });
        } else {
            w.printf("  unsigned {d}-bit {s}: {d} (max {d})\n", .{ info.bits, @typeName(T), v, std.math.maxInt(T) });
        }
    } else if (comptime @typeInfo(T) == .float) {
        w.printf("  float {s}: {d:.4}\n", .{ @typeName(T), v });
    } else if (comptime T == bool) {
        w.printf("  bool: {}\n", .{v});
    } else {
        w.printf("  other {s}: {s}\n", .{ @typeName(T), v });
    }
}

fn sumTuple(t: anytype) i64 {
    var s: i64 = 0;
    inline for (t) |x| {
        s += @as(i64, @intCast(x));
    }
    return s;
}

const Op = enum { add, sub, mul, xor, rotl };
fn applyOp(comptime op: Op, a: u32, b: u32) u32 {
    return switch (op) {
        .add => a +% b,
        .sub => a -% b,
        .mul => a *% b,
        .xor => a ^ b,
        .rotl => std.math.rotl(u32, a, b & 31),
    };
}

// A comptime-generated dispatch table of function pointers, one per op.
const op_table = blk: {
    var t: [std.meta.fieldNames(Op).len]*const fn (u32, u32) u32 = undefined;
    for (std.meta.tags(Op), 0..) |op, i| {
        const S = struct {
            fn f(a: u32, b: u32) u32 {
                return applyOp(op, a, b);
            }
        };
        t[i] = &S.f;
    }
    break :blk t;
};

fn countBits(comptime n: comptime_int) comptime_int {
    var c = 0;
    var x = n;
    while (x != 0) : (x >>= 1) c += x & 1;
    return c;
}

pub fn main() void {
    w.printf("primes: {d} below 2000, last {d}\n", .{ primes.len, primes[primes.len - 1] });
    var idx: usize = w.hide(@as(usize, 3));
    for (0..40) |i| {
        idx = (idx * 37 + 11) % primes.len;
        w.printf("prime[{d}]={d} fib[{d}]={d} crc[{d}]={x:0>8}\n", .{ idx, primes[idx], idx % fib.len, fib[idx % fib.len], idx & 255, crc_table[idx & 255] });
        _ = i;
    }
    const texts = [_][]const u8{ "", "a", "abc", "message digest", "The quick brown fox jumps over the lazy dog", joined };
    for (texts) |t| {
        const h = w.hide(t);
        w.printf("crc32(\"{s}\") = {x:0>8}\n", .{ h, crc32(h) });
    }
    const r = w.hide(@as(usize, 13));
    for (0..r + 1) |c| w.printf("pascal[{d}][{d}]={d}\n", .{ r, c, pascal[r][c] });
    w.printf("joined={s}\nsummary={s}\n", .{ joined, summary });

    w.printf("fact: {d} {d} {d} {d}\n", .{ comptimeFact(5), comptimeFact(10), comptimeFact(15), comptimeFact(20) });
    const bases = [_]u64{ 0, 1, 2, 3, 7, 10, 255, 65537, 0xdeadbeef };
    for (bases) |b0| {
        const b = w.hide(b0);
        w.printf("pow {d}: ^0={d} ^1={d} ^2={d} ^3={d} ^5={d} ^8={d} ^13={d} ^31={d} ^64={d}\n", .{
            b, powc(b, 0), powc(b, 1), powc(b, 2), powc(b, 3), powc(b, 5), powc(b, 8), powc(b, 13), powc(b, 31), powc(b, 64),
        });
    }
    const v = w.hide(@as(u64, 0xfedc_ba98_7654_3210));
    w.printf("fields: {x} {x} {x} {x} {x} {x}\n", .{ field(v, 0, 4), field(v, 4, 12), field(v, 16, 16), field(v, 31, 5), field(v, 40, 24), field(v, 0, 64) });

    describe(u8, w.hide(@as(u8, 200)));
    describe(i5, w.hide(@as(i5, -9)));
    describe(u40, w.hide(@as(u40, 1 << 39)));
    describe(i64, w.hide(@as(i64, -1234567890123)));
    describe(f32, w.hide(@as(f32, 3.25)));
    describe(f64, w.hide(@as(f64, -0.0625)));
    describe(bool, w.hide(true));
    describe([]const u8, w.hide(@as([]const u8, "text")));

    const tup = .{ @as(u8, 200), @as(i16, -300), @as(u32, 70000), @as(i64, -5), @as(u3, 7) };
    w.printf("sumTuple={d}\n", .{sumTuple(tup)});
    inline for (tup, 0..) |x, i| {
        const hx = w.hide(x);
        w.printf("tuple[{d}] {s} = {d} bits={d}\n", .{ i, @typeName(@TypeOf(x)), hx, @bitSizeOf(@TypeOf(x)) });
    }

    var a: u32 = w.hide(@as(u32, 0x12345678));
    var b: u32 = w.hide(@as(u32, 0x9abcdef1));
    for (0..12) |round| {
        inline for (comptime std.meta.tags(Op)) |op| {
            const direct = applyOp(op, a, b);
            const viaTable = op_table[@intFromEnum(op)](a, b);
            w.printf("round {d} {s}: {x:0>8} {x:0>8} same={}\n", .{ round, @tagName(op), direct, viaTable, direct == viaTable });
        }
        const sel = w.hide(@as(usize, round)) % op_table.len;
        const t = op_table[sel](a, b);
        a = b ^ t;
        b = t +% @as(u32, @intCast(round));
    }

    comptime var acc: u32 = 0;
    inline for (.{ 3, 5, 255, 1023, 65535 }) |n| {
        acc += countBits(n);
        w.printf("countBits({d})={d} acc={d}\n", .{ n, countBits(n), acc });
    }

    // inline while over a comptime array with a runtime accumulator.
    const weights = [_]u16{ 3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9 };
    comptime var k: usize = 0;
    var dot: u64 = 0;
    var x: u64 = w.hide(@as(u64, 7));
    inline while (k < weights.len) : (k += 1) {
        x = x *% 6364136223846793005 +% 1442695040888963407;
        dot +%= (x >> 33) * weights[k];
        w.printf("dot[{d}] w={d} x={d} dot={d}\n", .{ k, weights[k], x >> 33, dot });
    }
}
