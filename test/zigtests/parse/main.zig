// std.fmt's parsers: parseInt, parseUnsigned and parseFloat over many inputs,
// bases and integer widths, with their errors, and round trips through the
// formatter.
//
// parseInt is a digit loop with a checked multiply-add per character, so
// what reaches wasm is `@mulWithOverflow` and `@addWithOverflow` at the
// result's own width -- u7, i13, u128 and the rest -- each lowered to a
// widening multiply or a compare against the high half, plus the base-0
// prefix detection and '_' skipping.  parseFloat is Eisel-Lemire: a 64x128
// multiply against a table of powers of five (`__multi3` on wasm32), a
// fast path for small exponents in plain f64 arithmetic, and a slow path
// through a big decimal for the ties it cannot decide, plus a separate hex
// float parser.  Errors are printed by name: Overflow and InvalidCharacter.
//
// The round trips format a random integer or float and parse it back, and
// print both: for floats the shortest-repr printer and the parser must agree
// to the bit.  NaN is printed only as "nan", never by its bits.
const std = @import("std");
const w = @import("wasi");

fn tryInt(comptime T: type, s: []const u8, base: u8) void {
    const str = w.hide(s);
    if (std.fmt.parseInt(T, str, base)) |v| {
        w.printf("parseInt({s}, \"{s}\", {d}) = {d}\n", .{ @typeName(T), str, base, v });
    } else |err| {
        w.printf("parseInt({s}, \"{s}\", {d}) = error.{s}\n", .{ @typeName(T), str, base, @errorName(err) });
    }
}

fn tryUnsigned(comptime T: type, s: []const u8, base: u8) void {
    const str = w.hide(s);
    if (std.fmt.parseUnsigned(T, str, base)) |v| {
        w.printf("parseUnsigned({s}, \"{s}\", {d}) = {d}\n", .{ @typeName(T), str, base, v });
    } else |err| {
        w.printf("parseUnsigned({s}, \"{s}\", {d}) = error.{s}\n", .{ @typeName(T), str, base, @errorName(err) });
    }
}

fn tryFloat(comptime T: type, s: []const u8) void {
    const str = w.hide(s);
    if (std.fmt.parseFloat(T, str)) |v| {
        if (std.math.isNan(v)) {
            w.printf("parseFloat({s}, \"{s}\") = nan\n", .{ @typeName(T), str });
        } else {
            w.printf("parseFloat({s}, \"{s}\") = {e} bits={x}\n", .{ @typeName(T), str, v, @as(@Int(.unsigned, @bitSizeOf(T)), @bitCast(v)) });
        }
    } else |err| {
        w.printf("parseFloat({s}, \"{s}\") = error.{s}\n", .{ @typeName(T), str, @errorName(err) });
    }
}

const int_inputs = [_][]const u8{
    "0",         "1",        "-1",          "+42",        "127",          "128",
    "-128",      "-129",     "255",         "256",        "1_000_000",    "_1",
    "1__0",      "",         "-",           "+",          "0x1F",         "0X1f",
    "0b1011",    "0o777",    "0o778",       "-0x80",      "0x",           "12a",
    " 1",        "1 ",       "2147483647",  "2147483648", "-2147483648",  "4294967295",
    "4294967296", "9223372036854775807", "-9223372036854775808", "9223372036854775808",
    "18446744073709551615", "18446744073709551616",
    "340282366920938463463374607431768211455", "340282366920938463463374607431768211456",
    "-170141183460469231731687303715884105728", "00000000000000000000000000000000000000000000000000123",
};

fn intTable() void {
    for (int_inputs) |s| {
        tryInt(i8, s, 10);
        tryInt(u8, s, 0);
        tryInt(i32, s, 0);
        tryInt(u32, s, 10);
        tryInt(i64, s, 0);
        tryInt(u64, s, 10);
        tryInt(i128, s, 0);
        tryInt(u128, s, 10);
        tryInt(u7, s, 0);
        tryInt(i13, s, 10);
        tryUnsigned(u16, s, 0);
    }
}

fn bases() void {
    const cases = [_]struct { s: []const u8, base: u8 }{
        .{ .s = "101010", .base = 2 },       .{ .s = "777", .base = 8 },
        .{ .s = "deadBEEF", .base = 16 },    .{ .s = "zz", .base = 36 },
        .{ .s = "ZIG", .base = 36 },         .{ .s = "number", .base = 36 },
        .{ .s = "1y2p0ij32e8e7", .base = 36 }, .{ .s = "1y2p0ij32e8e8", .base = 36 },
        .{ .s = "12", .base = 3 },           .{ .s = "13", .base = 3 },
        .{ .s = "-101", .base = 2 },         .{ .s = "g", .base = 16 },
        .{ .s = "G", .base = 17 },           .{ .s = "7fffffffffffffff", .base = 16 },
        .{ .s = "8000000000000000", .base = 16 }, .{ .s = "-8000000000000000", .base = 16 },
        .{ .s = "1111111111111111111111111111111111111111111111111111111111111111", .base = 2 },
        .{ .s = "11111111111111111111111111111111111111111111111111111111111111111", .base = 2 },
        .{ .s = "1777777777777777777777", .base = 8 }, .{ .s = "2000000000000000000000", .base = 8 },
        .{ .s = "f_f", .base = 16 },         .{ .s = "0", .base = 36 },
    };
    for (cases) |c| {
        tryInt(i64, c.s, c.base);
        tryUnsigned(u64, c.s, c.base);
        tryUnsigned(u128, c.s, c.base);
        tryInt(i16, c.s, c.base);
    }
    var b: u8 = 2;
    while (b <= 36) : (b += 1) {
        tryUnsigned(u32, "10", w.hide(b));
        tryUnsigned(u64, "zz", w.hide(b));
    }
    // charToDigit and digitToChar for every base.
    for ([_]u8{ '0', '9', 'a', 'A', 'f', 'z', 'Z', '_', '/' }) |c| {
        for ([_]u8{ 2, 10, 16, 36 }) |base| {
            if (std.fmt.charToDigit(w.hide(c), base)) |d| {
                w.printf("charToDigit('{c}', {d}) = {d}\n", .{ c, base, d });
            } else |err| w.printf("charToDigit('{c}', {d}) = {s}\n", .{ c, base, @errorName(err) });
        }
    }
    var d: u8 = 0;
    while (d < 36) : (d += 5) w.printf("digitToChar({d}) = {c} {c}\n", .{ d, std.fmt.digitToChar(w.hide(d), .lower), std.fmt.digitToChar(d, .upper) });
    for ([_][]const u8{ "1", "2k", "2K", "2Ki", "2KiB", "3M", "3Mi", "1G", "1Gi", "5x", "" }) |s| {
        if (std.fmt.parseIntSizeSuffix(w.hide(s), 10)) |v| {
            w.printf("sizeSuffix(\"{s}\") = {d}\n", .{ s, v });
        } else |err| w.printf("sizeSuffix(\"{s}\") = {s}\n", .{ s, @errorName(err) });
    }
}

const float_inputs = [_][]const u8{
    "0",                        "-0",                       "1",                     "-1.5",
    "3.141592653589793",        "2.718281828459045235360287", "1e10",               "1E-10",
    "1.7976931348623157e308",   "1.7976931348623159e308",   "2e308",                 "4.9406564584124654e-324",
    "2.4703282292062327e-324",  "2.4703282292062328e-324",  "1e-400",                "2.2250738585072011e-308",
    "0.1",                      "0.30000000000000004",      ".5",                    "5.",
    "+.5e+2",                   "inf",                      "-inf",                  "INF",
    "infinity",                 "-Infinity",                "nan",                   "NaN",
    "0x1p0",                    "0x1.8p1",                  "0x.8p-1",               "-0x1.fffffffffffffp1023",
    "0x1p-1074",                "0x1p-1075",                "1_000.5",               "1e",
    "e5",                       "1.2.3",                    "--1",                   "",
    "9007199254740993",         "123456789012345678901234567890", "1e23",           "8.589973e9",
    "0.000000000000000000000000000000000000000000001", "7.006492321624085e-46", "3.4028235e38", "3.4028236e38",
};

fn floatTable() void {
    for (float_inputs) |s| {
        tryFloat(f64, s);
        tryFloat(f32, s);
    }
    for ([_][]const u8{ "1", "0.1", "65504", "65520", "6e-8", "1e-8", "3.14159" }) |s| tryFloat(f16, s);
    for ([_][]const u8{ "0.1", "3.14159265358979323846264338327950288", "1e4000", "1e-4950", "0x1.ffffffffffffffffffffffffffffp16383" }) |s| tryFloat(f128, s);
}

fn roundTrips() void {
    var x: u64 = w.hide(@as(u64, 0x2545F4914F6CDD1D));
    var i: u32 = 0;
    var buf: [128]u8 = undefined;
    while (i < 60) : (i += 1) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        const sv: i64 = @bitCast(x);
        const base: u8 = @intCast(2 + x % 35);
        // Integer: format in a base, parse back.
        const s1 = std.mem.print(&buf, "{d}", .{sv}) catch unreachable;
        const p1 = std.fmt.parseInt(i64, s1, 10) catch 0;
        w.printf("rt[{d}] i64 {s} -> {d} same={}\n", .{ i, s1, p1, p1 == sv });
        const n = std.fmt.printInt(&buf, x, base, .lower, .{});
        const p2 = std.fmt.parseUnsigned(u64, buf[0..n], base) catch 0;
        w.printf("  u64 base{d} {s} -> {d} same={}\n", .{ base, buf[0..n], p2, p2 == x });
        // Float: a finite double from the bits, printed shortest and parsed.
        var fb = x;
        if ((fb >> 52) & 0x7ff == 0x7ff) fb ^= 1 << 62; // keep it finite
        const f: f64 = @bitCast(fb);
        const s3 = std.mem.print(&buf, "{e}", .{f}) catch unreachable;
        const p3 = std.fmt.parseFloat(f64, s3) catch 0;
        w.printf("  f64 {s} -> {x} same={}\n", .{ s3, @as(u64, @bitCast(p3)), @as(u64, @bitCast(p3)) == fb });
        const f32v: f32 = @bitCast(@as(u32, @truncate(fb >> 3)) & 0x7f7f_ffff);
        const s4 = std.mem.print(&buf, "{d}", .{f32v}) catch unreachable;
        const p4 = std.fmt.parseFloat(f32, s4) catch 0;
        w.printf("  f32 {s} -> {x} same={}\n", .{ s4, @as(u32, @bitCast(p4)), p4 == f32v });
        // A decimal with a fixed precision, parsed back and compared.
        const g: f64 = @as(f64, @floatFromInt(x % 1000000)) / 1000.0;
        const s5 = std.mem.print(&buf, "{d:.3}", .{g}) catch unreachable;
        const p5 = std.fmt.parseFloat(f64, s5) catch 0;
        w.printf("  fixed {s} -> {d} same={}\n", .{ s5, p5, p5 == g });
    }
}

fn csvSum() void {
    // Parse a small CSV of mixed numbers, summing per column.
    const text =
        \\id,count,ratio,hex
        \\1,10,0.5,0x10
        \\2,-3,1.25,0xff
        \\3,1_000,-2.5e-1,0b101
        \\4,abc,1e3,0o17
        \\5,99999999999,nan,0x
        \\6,7,inf,12
    ;
    var lines = std.mem.splitScalar(u8, w.hide(@as([]const u8, text)), '\n');
    _ = lines.next();
    var count_sum: i64 = 0;
    var ratio_sum: f64 = 0;
    var hex_sum: u64 = 0;
    while (lines.next()) |line| {
        var cols = std.mem.splitScalar(u8, line, ',');
        const id = cols.next().?;
        const c = cols.next().?;
        const r = cols.next().?;
        const h = cols.next().?;
        w.printf("row {s}:", .{id});
        if (std.fmt.parseInt(i32, c, 10)) |v| {
            count_sum += v;
            w.printf(" count={d}", .{v});
        } else |err| w.printf(" count=<{s}>", .{@errorName(err)});
        if (std.fmt.parseFloat(f64, r)) |v| {
            if (std.math.isFinite(v)) {
                ratio_sum += v;
                w.printf(" ratio={d}", .{v});
            } else w.printf(" ratio=<{s}>", .{if (std.math.isNan(v)) "nan" else "inf"});
        } else |err| w.printf(" ratio=<{s}>", .{@errorName(err)});
        if (std.fmt.parseUnsigned(u64, h, 0)) |v| {
            hex_sum += v;
            w.printf(" hex={d}\n", .{v});
        } else |err| w.printf(" hex=<{s}>\n", .{@errorName(err)});
    }
    w.printf("sums count={d} ratio={d} hex={d}\n", .{ count_sum, ratio_sum, hex_sum });
}

pub fn main() void {
    intTable();
    bases();
    floatTable();
    roundTrips();
    csvSum();
    w.puts("done\n");
}
