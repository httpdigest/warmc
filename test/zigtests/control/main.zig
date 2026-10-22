// Control flow as Zig writes it, which is more of it than a C program has:
// labeled blocks that yield a value with `break :blk v`; labeled nested loops
// that break or continue an outer one; `while` with a continue expression, an
// optional or error-union condition and an `else` branch; `for` over ranges
// and over several sequences at once (`for (a, b, 0..) |x, y, i|`), with an
// `else` taken when no `break` ran; `switch` on ranges, lists of items, bools,
// odd-width integers (u2, i5) and enums, with `inline else` stamping out one
// prong per value; and a labeled `switch` driven by `continue :sw next`, the
// construct Zig added for state machines and interpreters, which LLVM lowers
// to a jump table re-entered from every prong rather than a loop around a
// switch.
//
// Every branch taken is printed, and the inputs come through `w.hide`, so the
// branches are decided at run time: a block that yields the wrong value, an
// `else` taken when a `break` ran, or a jump-table entry that goes to the
// wrong prong each show as a line that differs.
const std = @import("std");
const w = @import("wasi");

fn classify(x: i32) []const u8 {
    return switch (x) {
        std.math.minInt(i32)...-1000 => "very negative",
        -999...-1 => "negative",
        0 => "zero",
        1, 2, 3, 5, 7, 11, 13 => "small prime",
        4, 6, 8...10, 12 => "small composite",
        14...99 => "two digits",
        100...std.math.maxInt(i32) => "large",
    };
}

fn labeledBlocks() void {
    const inputs = [_]i32{ -5000, -3, 0, 1, 4, 9, 13, 50, 100, 12345 };
    for (inputs) |raw| {
        const x = w.hide(raw);
        const sign = blk: {
            if (x < 0) break :blk @as(i32, -1);
            if (x == 0) break :blk @as(i32, 0);
            break :blk @as(i32, 1);
        };
        const digits = blk: {
            var n: u32 = @abs(x);
            var d: u32 = 1;
            while (n >= 10) : (n /= 10) d += 1;
            break :blk d;
        };
        const parity = if (@rem(x, 2) == 0) "even" else "odd";
        w.printf("x={d} sign={d} digits={d} {s} class={s}\n", .{ x, sign, digits, parity, classify(x) });
    }
}

fn nestedLoops() void {
    // Find the first pair (i, j) with i*j == target, breaking out of both.
    const targets = [_]u32{ 12, 35, 49, 97, 1 };
    for (targets) |t0| {
        const t = w.hide(t0);
        const found: ?[2]u32 = outer: for (2..10) |i| {
            for (2..10) |j| {
                if (i * j == t) break :outer .{ @intCast(i), @intCast(j) };
            }
        } else null;
        if (found) |f| {
            w.printf("target {d} = {d}*{d}\n", .{ t, f[0], f[1] });
        } else w.printf("target {d} not found\n", .{t});
    }
    // continue to an outer label: skip rows with a negative entry.
    const grid = [_][4]i8{
        .{ 1, 2, 3, 4 },
        .{ 5, -6, 7, 8 },
        .{ 9, 10, 11, 12 },
        .{ -1, 0, 0, 0 },
        .{ 2, 2, 2, -2 },
        .{ 3, 3, 3, 3 },
    };
    var total: i32 = 0;
    rows: for (grid, 0..) |row0, r| {
        const row = w.hide(row0);
        var s: i32 = 0;
        for (row) |v| {
            if (v < 0) {
                w.printf("row {d}: skipped at {d}\n", .{ r, v });
                continue :rows;
            }
            s += v;
        }
        total += s;
        w.printf("row {d}: sum {d}, running {d}\n", .{ r, s, total });
    }
    // Three-deep with a labeled while.
    var count: u32 = 0;
    var a: u32 = 0;
    search: while (a < 20) : (a += 1) {
        var b: u32 = a;
        while (b < 20) : (b += 1) {
            var c: u32 = b;
            while (c < 30) : (c += 1) {
                if (a * a + b * b == c * c and a > 0) {
                    count += 1;
                    w.printf("triple {d} {d} {d}\n", .{ a, b, c });
                    if (count == w.hide(@as(u32, 5))) break :search;
                    continue :search;
                }
            }
        }
    }
    w.printf("triples found {d}, a stopped at {d}\n", .{ count, a });
}

fn whileForms() void {
    // Continue expression runs on `continue` too.
    var i: u32 = 0;
    var evens: u32 = 0;
    var steps: u32 = 0;
    while (i < w.hide(@as(u32, 20))) : ({
        i += 1;
        steps += 1;
    }) {
        if (i % 2 == 1) continue;
        evens += i;
    }
    w.printf("evens sum {d} steps {d}\n", .{ evens, steps });
    // while with else: taken only when the condition fails, not on break.
    for ([_]u32{ 3, 7, 50 }) |lim0| {
        const lim = w.hide(lim0);
        var k: u32 = 1;
        const r = while (k < 40) : (k *= 2) {
            if (k > lim) break k;
        } else 999;
        w.printf("first power of two above {d} below 40: {d}\n", .{ lim, r });
    }
    // while over an optional: an iterator that ends with null.
    var it = Countdown{ .n = w.hide(@as(u8, 6)) };
    while (it.next()) |v| {
        w.printf("countdown {d}\n", .{v});
    } else {
        w.puts("countdown exhausted\n");
    }
    // while over an error union: stop at the first error, see it in else.
    var p = Parser{ .s = "12,7,x,9" };
    while (p.nextNumber()) |v| {
        w.printf("parsed {d}\n", .{v});
    } else |err| {
        w.printf("parser stopped: {s} at {d}\n", .{ @errorName(err), p.pos });
    }
}

const Countdown = struct {
    n: u8,
    fn next(self: *Countdown) ?u8 {
        if (self.n == 0) return null;
        self.n -= 1;
        return self.n;
    }
};

const Parser = struct {
    s: []const u8,
    pos: usize = 0,
    fn nextNumber(self: *Parser) error{ End, Bad }!u32 {
        if (self.pos >= self.s.len) return error.End;
        var v: u32 = 0;
        var any = false;
        while (self.pos < self.s.len and self.s[self.pos] != ',') : (self.pos += 1) {
            const c = self.s[self.pos];
            if (c < '0' or c > '9') return error.Bad;
            v = v * 10 + (c - '0');
            any = true;
        }
        if (self.pos < self.s.len) self.pos += 1;
        if (!any) return error.Bad;
        return v;
    }
};

fn forForms() void {
    const names = [_][]const u8{ "ant", "bee", "cat", "dog", "eel" };
    const legs = [_]u8{ 6, 6, 4, 4, 0 };
    var weights = [_]f32{ 0.001, 0.0001, 4.5, 20.0, 1.5 };
    for (names, legs, &weights, 0..) |n, l, *wt, i| {
        wt.* *= 2;
        w.printf("{d}: {s} legs={d} weight={d:.4}\n", .{ i, n, l, wt.* });
    }
    // Ranges with runtime bounds, including an empty one.
    const lo = w.hide(@as(usize, 3));
    const hi = w.hide(@as(usize, 9));
    var acc: usize = 0;
    for (lo..hi) |k| acc = acc * 3 + k;
    w.printf("range {d}..{d} fold {d}\n", .{ lo, hi, acc });
    for (hi..hi) |_| w.puts("never\n");
    // for ... else: else runs when no break.
    for ([_]u8{ 4, 9 }) |needle0| {
        const needle = w.hide(needle0);
        const where = for (legs, 0..) |l, k| {
            if (l == needle) break k;
        } else legs.len;
        w.printf("legs {d} first at {d}\n", .{ needle, where });
    }
    // Index starting other than 0.
    for (names[1..4], 10..) |n, k| w.printf("offset index {d}: {s}\n", .{ k, n });
}

const Color = enum { red, green, blue, cyan, magenta, yellow };

fn switches() void {
    // Odd widths: every value of a u2 and an i5.
    var u: u2 = 0;
    while (true) : (u +%= 1) {
        const s = switch (w.hide(u)) {
            0 => "zero",
            1 => "one",
            2 => "two",
            3 => "three",
        };
        w.printf("u2 {d} {s}\n", .{ u, s });
        if (u == 3) break;
    }
    var v: i5 = -16;
    while (true) : (v += 1) {
        const r: i8 = switch (w.hide(v)) {
            -16...-9 => -2,
            -8...-1 => -1,
            0 => 0,
            1...7 => 1,
            8...15 => 2,
        };
        w.printf("i5 {d} bucket {d}\n", .{ v, r });
        if (v == 15) break;
    }
    // Bools.
    for ([_]bool{ true, false }) |b| {
        const s = switch (w.hide(b)) {
            true => "yes",
            false => "no",
        };
        w.printf("bool {} {s}\n", .{ b, s });
    }
    // inline else: each value gets its own prong, with a comptime-known tag.
    for (0..6) |k| {
        const c: Color = @enumFromInt(w.hide(@as(u8, @intCast(k))));
        const info = switch (c) {
            inline .red, .green, .blue => |p| "primary " ++ @tagName(p),
            inline else => |p| comptime std.fmt.comptimePrint("{s} has {d} letters", .{ @tagName(p), @tagName(p).len }),
        };
        w.printf("color {d}: {s}\n", .{ k, info });
    }
    // Switch with a capture and a guard-like nested if.
    const vals = [_]u16{ 0, 1, 9, 10, 99, 100, 255, 256, 1000, 65535 };
    for (vals) |raw| {
        const x = w.hide(raw);
        const msg = switch (x) {
            0 => "nil",
            1...9 => |d| if (d % 2 == 0) "even digit" else "odd digit",
            10...99 => "two digits",
            100...255 => |b| if (b == 255) "byte max" else "byte",
            else => |big| if (big > 60000) "huge" else "big",
        };
        w.printf("u16 {d}: {s}\n", .{ x, msg });
    }
}

// A labeled switch as a state machine: a tiny lexer over a string, each state
// jumping straight to the next with `continue :state`.
const State = enum { start, ident, number, space, done };

fn lex(src: []const u8) void {
    var i: usize = 0;
    var tok_start: usize = 0;
    var idents: u32 = 0;
    var numbers: u32 = 0;
    state: switch (State.start) {
        .start => {
            if (i >= src.len) continue :state .done;
            tok_start = i;
            const c = src[i];
            if (std.ascii.isAlphabetic(c) or c == '_') continue :state .ident;
            if (std.ascii.isDigit(c)) continue :state .number;
            continue :state .space;
        },
        .ident => {
            while (i < src.len and (std.ascii.isAlphanumeric(src[i]) or src[i] == '_')) i += 1;
            idents += 1;
            w.printf("  ident '{s}' @{d}\n", .{ src[tok_start..i], tok_start });
            continue :state .start;
        },
        .number => {
            var v: u64 = 0;
            while (i < src.len and std.ascii.isDigit(src[i])) : (i += 1) v = v * 10 + (src[i] - '0');
            numbers += 1;
            w.printf("  number {d} @{d}\n", .{ v, tok_start });
            continue :state .start;
        },
        .space => {
            if (src[i] != ' ') w.printf("  punct '{c}' @{d}\n", .{ src[i], i });
            i += 1;
            continue :state .start;
        },
        .done => w.printf("  {d} idents, {d} numbers\n", .{ idents, numbers }),
    }
}

// A labeled switch over integers: a Collatz walk driven entirely by
// `continue :s`, counting the steps.
fn collatzSwitch(n0: u64) u32 {
    var n = n0;
    var steps: u32 = 0;
    s: switch (@as(u2, @intCast(@min(n, 2)))) {
        0, 1 => return steps,
        2, 3 => {
            steps += 1;
            n = if (n % 2 == 0) n / 2 else 3 * n + 1;
            continue :s @intCast(@min(n, 2));
        },
    }
}

fn loopConditions() void {
    // orelse inside a loop condition.
    const table = [_]?u8{ 3, 1, 4, null, 5, 9 };
    var i: usize = 0;
    var sum: u32 = 0;
    while ((w.hide(table)[i] orelse 0) != 0) : (i += 1) sum += table[i].?;
    w.printf("sum before null {d} (stopped at {d})\n", .{ sum, i });
    // catch inside a loop condition.
    const strs = [_][]const u8{ "10", "20", "x3", "40" };
    var k: usize = 0;
    var tot: u32 = 0;
    while (k < strs.len and (std.fmt.parseInt(u32, w.hide(strs[k]), 10) catch 0) != 0) : (k += 1) {
        tot += std.fmt.parseInt(u32, strs[k], 10) catch unreachable;
    }
    w.printf("parsed total {d} stopped at {d}\n", .{ tot, k });
}

pub fn main() void {
    labeledBlocks();
    nestedLoops();
    whileForms();
    forForms();
    switches();
    const progs = [_][]const u8{ "let x1 = 42 + y_2;", "a1b2 33 c", "  9 ;;; zz", "" };
    for (progs) |p| {
        w.printf("lex \"{s}\"\n", .{p});
        lex(w.hide(p));
    }
    for ([_]u64{ 1, 2, 3, 6, 7, 27, 97, 871, 77031 }) |n| {
        w.printf("collatz {d}: {d} steps\n", .{ n, collatzSwitch(w.hide(n)) });
    }
    loopConditions();
    w.puts("done\n");
}
