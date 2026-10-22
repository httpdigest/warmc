// std.Random: every generator in std, seeded with fixed values, and the
// distribution helpers on top of them.
//
// The generators are each a different kind of integer kernel: Xoshiro256 and
// Xoroshiro128 are 64-bit shifts, rotates and XORs over a state array; Pcg is
// a 64-bit multiply with a data-dependent rotate of the output; Sfc64 and
// RomuTrio add and rotate; SplitMix64 is a multiply-xorshift finaliser;
// Isaac64 is a table walk with indirect indexing; ChaCha and Ascon are
// cryptographic permutations used as stream generators.  All of them reach
// the program through `std.Random`, an interface of a context pointer and a
// `fill` function pointer, so each draw is a `call_indirect` into a function
// that fills a byte buffer -- and the helpers then assemble integers from
// those bytes: `uintLessThan` is Lemire's multiply-shift rejection method
// (a 64x64->128 multiply for u64 bounds), `float` builds the mantissa from
// the low bits and counts leading zeros for the exponent, `floatNorm` and
// `floatExp` are ziggurat tables, `shuffle` is Fisher-Yates and
// `weightedIndex` a cumulative scan.
//
// Everything is printed: raw outputs, bounded draws, histograms of a few
// thousand draws, a shuffled deck, and a Monte Carlo estimate of pi.
const std = @import("std");
const w = @import("wasi");

const Suit = enum { clubs, diamonds, hearts, spades };

fn raw(name: []const u8, r: std.Random) void {
    w.printf("{s}:", .{name});
    var i: u32 = 0;
    while (i < 6) : (i += 1) w.printf(" {x:0>16}", .{r.int(u64)});
    w.printf("\n  u8 {d} {d} {d} i16 {d} {d} u3 {d} {d} bool {} {} u128 {x}\n", .{
        r.int(u8), r.int(u8), r.int(u8), r.int(i16), r.int(i16), r.int(u3), r.int(u3), r.boolean(), r.boolean(), r.int(u128),
    });
    var bytes: [13]u8 = undefined;
    r.bytes(&bytes);
    w.puts("  bytes");
    for (bytes) |b| w.printf(" {x:0>2}", .{b});
    w.puts("\n");
}

fn helpers(r: std.Random) void {
    w.puts("bounded:");
    for ([_]u64{ 1, 2, 3, 10, 1000, 1 << 33, std.math.maxInt(u64) }) |n| {
        w.printf(" <{d}:{d}", .{ n, r.uintLessThan(u64, w.hide(n)) });
    }
    w.puts("\n  ranges:");
    var i: u32 = 0;
    while (i < 12) : (i += 1) {
        w.printf(" {d}/{d}/{d}", .{ r.intRangeAtMost(i32, -5, 5), r.intRangeLessThan(i8, -100, 100), r.uintAtMost(u16, 999) });
    }
    w.puts("\n  floats:");
    i = 0;
    while (i < 8) : (i += 1) {
        const f32v = r.float(f32);
        const f64v = r.float(f64);
        w.printf(" {d:.6}/{x:0>8}/{d:.9}", .{ f32v, @as(u32, @bitCast(f32v)), f64v });
    }
    w.puts("\n  norm/exp:");
    i = 0;
    while (i < 8) : (i += 1) w.printf(" {d:.5}/{d:.5}", .{ r.floatNorm(f64), r.floatExp(f64) });
    w.puts("\n  enums:");
    i = 0;
    while (i < 10) : (i += 1) w.printf(" {s}", .{@tagName(r.enumValueWithIndex(Suit, u32))});
    w.puts("\n");
}

fn histogram(r: std.Random) void {
    var buckets: [10]u32 = @splat(0);
    var sum: u64 = 0;
    var i: u32 = 0;
    while (i < 5000) : (i += 1) {
        const v = r.uintLessThan(u32, 1000);
        buckets[v / 100] += 1;
        sum += v;
    }
    w.printf("uniform histogram:", .{});
    for (buckets) |b| w.printf(" {d}", .{b});
    w.printf(" mean*1000={d}\n", .{sum * 1000 / 5000});

    var weights = [_]u32{ 1, 0, 5, 10, 3, 1 };
    var counts: [6]u32 = @splat(0);
    i = 0;
    while (i < 4000) : (i += 1) counts[r.weightedIndex(u32, &weights)] += 1;
    w.printf("weighted {any}:", .{weights});
    for (counts) |c| w.printf(" {d}", .{c});
    w.puts("\n");

    var normal: [12]u32 = @splat(0);
    i = 0;
    while (i < 4000) : (i += 1) {
        const v = r.floatNorm(f64) * 2.0 + 6.0;
        if (v >= 0 and v < 12) normal[@intFromFloat(v)] += 1;
    }
    w.printf("normal(6,2):", .{});
    for (normal) |c| w.printf(" {d}", .{c});
    w.puts("\n");
}

fn deck(r: std.Random) void {
    var cards: [52]u8 = undefined;
    for (&cards, 0..) |*c, i| c.* = @intCast(i);
    var round: u32 = 0;
    while (round < 3) : (round += 1) {
        r.shuffleWithIndex(u8, &cards, u32);
        w.printf("deal {d}:", .{round});
        for (cards[0..13]) |c| {
            const ranks = "A23456789TJQK";
            const suits = "cdhs";
            w.printf(" {c}{c}", .{ ranks[c % 13], suits[c / 13] });
        }
        var check: u64 = 0;
        for (cards) |c| check += @as(u64, 1) << @intCast(c);
        w.printf(" complete={}\n", .{check == (@as(u64, 1) << 52) - 1});
    }
}

fn montecarlo(r: std.Random) void {
    var inside: u32 = 0;
    var i: u32 = 0;
    while (i < 20000) : (i += 1) {
        const x = r.float(f64);
        const y = r.float(f64);
        if (x * x + y * y <= 1.0) inside += 1;
        if ((i + 1) % 4000 == 0) w.printf("pi after {d}: {d:.5}\n", .{ i + 1, 4.0 * @as(f64, @floatFromInt(inside)) / @as(f64, @floatFromInt(i + 1)) });
    }
    // A random walk in two dimensions, with its farthest excursion.
    var px: i32 = 0;
    var py: i32 = 0;
    var far: i32 = 0;
    i = 0;
    while (i < 3000) : (i += 1) {
        switch (r.int(u2)) {
            0 => px += 1,
            1 => px -= 1,
            2 => py += 1,
            3 => py -= 1,
        }
        far = @max(far, px * px + py * py);
    }
    w.printf("walk ends at ({d},{d}), farthest r^2 {d}\n", .{ px, py, far });
    // Reservoir sampling of 5 from 1000.
    var res: [5]u32 = undefined;
    var k: u32 = 0;
    while (k < 1000) : (k += 1) {
        if (k < 5) res[k] = k else {
            const j = r.uintLessThan(u32, k + 1);
            if (j < 5) res[j] = k;
        }
    }
    w.printf("reservoir {any}\n", .{res});
}

pub fn main() void {
    const seed = w.hide(@as(u64, 0x0123_4567_89ab_cdef));

    var xo = std.Random.Xoshiro256.init(seed);
    raw("xoshiro256", xo.random());
    var xr = std.Random.Xoroshiro128.init(seed);
    raw("xoroshiro128", xr.random());
    var pcg = std.Random.Pcg.init(seed);
    raw("pcg", pcg.random());
    var sfc = std.Random.Sfc64.init(seed);
    raw("sfc64", sfc.random());
    var romu = std.Random.RomuTrio.init(seed);
    raw("romutrio", romu.random());
    var isaac = std.Random.Isaac64.init(seed);
    raw("isaac64", isaac.random());
    var key: [32]u8 = undefined;
    for (&key, 0..) |*b, i| b.* = @truncate(seed >> @intCast((i % 8) * 8));
    var cc = std.Random.ChaCha.init(key);
    raw("chacha", cc.random());
    var asc = std.Random.Ascon.init(key);
    raw("ascon", asc.random());
    var sm = std.Random.SplitMix64.init(seed);
    w.puts("splitmix64:");
    for (0..6) |_| w.printf(" {x:0>16}", .{sm.next()});
    w.puts("\n");

    // Jumping ahead: the jumped stream differs from the original.
    var a = std.Random.Xoshiro256.init(42);
    var b = a;
    b.jump();
    w.printf("jump: {x} {x} / {x} {x}\n", .{ a.next(), a.next(), b.next(), b.next() });

    // Seeds that differ by one bit give unrelated streams.
    var s: u64 = 0;
    while (s < 4) : (s += 1) {
        var g = std.Random.DefaultPrng.init(w.hide(s));
        w.printf("seed {d}: {x:0>16} {x:0>16}\n", .{ s, g.next(), g.next() });
    }

    var prng = std.Random.DefaultPrng.init(seed);
    const r = prng.random();
    helpers(r);
    histogram(r);
    deck(r);
    montecarlo(r);
    var p2 = std.Random.Pcg.init(seed ^ 0xffff);
    helpers(p2.random());
    w.puts("done\n");
}
