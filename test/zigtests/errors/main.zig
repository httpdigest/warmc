// Error unions and the control flow they make: try, catch, errdefer.
//
// An error union is a payload plus a 16-bit error code, returned through
// memory when the payload does not fit a register, and every `try` is a
// compare of that code and an early return.  The tests drive a parser and a
// small resource manager through every error path: `try` up a five-deep call
// chain, `catch` with a default value, with a block, and with `|e| switch` on
// the error, errdefers running in reverse order only on the error path (0.17
// has no `errdefer |e|`), merged error sets (`A || B`),
// inferred error sets, `anyerror`, error unions of optionals and of structs,
// and `@errorName`.  Cleanups are logged into a buffer and printed after each
// operation, so a wrong order of defers, a defer that ran on the success path,
// or a wrong error code reaching a caller is a line that differs.  No error
// return trace is printed: ReleaseFast has none, and the program handles every
// error it makes.
const std = @import("std");
const w = @import("wasi");

var log_buf: [512]u8 = undefined;
var log_len: usize = 0;

fn log(s: []const u8) void {
    if (log_len + s.len + 1 > log_buf.len) return;
    @memcpy(log_buf[log_len..][0..s.len], s);
    log_len += s.len;
    log_buf[log_len] = ' ';
    log_len += 1;
}

fn flushLog(tag: []const u8) void {
    w.printf("  log[{s}]: {s}\n", .{ tag, log_buf[0..log_len] });
    log_len = 0;
}

const ParseError = error{ Empty, BadDigit, Overflow };
const RangeError = error{ TooSmall, TooLarge };
const AllErrors = ParseError || RangeError || error{Odd};

fn parseU16(s: []const u8) ParseError!u16 {
    if (s.len == 0) return error.Empty;
    var v: u16 = 0;
    for (s) |c| {
        if (c < '0' or c > '9') return error.BadDigit;
        const m = @mulWithOverflow(v, 10);
        if (m[1] != 0) return error.Overflow;
        const a = @addWithOverflow(m[0], c - '0');
        if (a[1] != 0) return error.Overflow;
        v = a[0];
    }
    return v;
}

fn checkRange(v: u16, lo: u16, hi: u16) RangeError!u16 {
    if (v < lo) return error.TooSmall;
    if (v > hi) return error.TooLarge;
    return v;
}

fn parseEvenInRange(s: []const u8) AllErrors!u16 {
    log("enter");
    defer log("leave");
    errdefer log("errdefer");
    const v = try parseU16(s);
    log("parsed");
    const r = try checkRange(v, 10, 5000);
    log("ranged");
    if (r % 2 != 0) return error.Odd;
    return r;
}

// Inferred error set: whatever the callees can return, plus its own.
fn level3(x: u32) !u32 {
    defer log("l3-defer");
    if (x % 7 == 0) return error.DivisibleBySeven;
    return x * 3;
}
fn level2(x: u32) !u32 {
    errdefer log("l2-errdefer");
    const v = try level3(x + 1);
    if (v > 100) return error.TooBig;
    return v + 1;
}
fn level1(x: u32) !u32 {
    defer log("l1-defer");
    errdefer log("l1-errdefer");
    const a = try level2(x);
    const b = try level2(a);
    return a + b;
}
fn level0(x: u32) anyerror!u32 {
    return level1(x) catch |e| switch (e) {
        error.DivisibleBySeven => blk: {
            log("recovered-7");
            break :blk 7;
        },
        else => |other| other,
    };
}

const Resource = struct {
    id: u8,
    fn open(id: u8, fail_at: u8) !Resource {
        if (id == fail_at) return error.OpenFailed;
        var buf: [16]u8 = undefined;
        log(std.mem.print(&buf, "open{d}", .{id}) catch "?");
        return .{ .id = id };
    }
    fn close(self: Resource) void {
        var buf: [16]u8 = undefined;
        log(std.mem.print(&buf, "close{d}", .{self.id}) catch "?");
    }
};

// Opens four resources in order; if any open fails, the ones already open
// are closed in reverse by their errdefers.  On success they stay open and
// the caller closes them.
fn openAll(fail_at: u8, out: *[4]Resource) !void {
    const r0 = try Resource.open(0, fail_at);
    errdefer r0.close();
    const r1 = try Resource.open(1, fail_at);
    errdefer r1.close();
    const r2 = try Resource.open(2, fail_at);
    errdefer r2.close();
    const r3 = try Resource.open(3, fail_at);
    out.* = .{ r0, r1, r2, r3 };
}

const Point = struct { x: i32, y: i32 };
fn findPoint(key: i32) error{NotFound}!?Point {
    if (key < 0) return error.NotFound;
    if (@rem(key, 3) == 0) return null;
    return .{ .x = key, .y = key * key };
}

fn divide(a: i32, b: i32) error{ DivByZero, Overflow }!i32 {
    if (b == 0) return error.DivByZero;
    if (a == std.math.minInt(i32) and b == -1) return error.Overflow;
    return @divTrunc(a, b);
}

fn sumAll(xs: []const []const u8) !u32 {
    var s: u32 = 0;
    for (xs) |x| s += try parseU16(x);
    return s;
}

pub fn main() void {
    const inputs = [_][]const u8{ "1234", "", "12a", "70000", "65535", "7", "5002", "4999", "10", "0010", "9", "4096", "65536", "x" };
    for (inputs) |in0| {
        const in = w.hide(in0);
        w.printf("input \"{s}\"\n", .{in});
        if (parseEvenInRange(in)) |v| {
            w.printf("  ok {d}\n", .{v});
        } else |e| {
            const kind: []const u8 = switch (e) {
                error.Empty, error.BadDigit, error.Overflow => "parse",
                error.TooSmall, error.TooLarge => "range",
                error.Odd => "odd",
            };
            w.printf("  error.{s} ({s})\n", .{ @errorName(e), kind });
        }
        flushLog(in);
        const d = parseU16(in) catch 9999;
        const e2 = parseU16(in) catch |e| blk: {
            if (e == error.Overflow) break :blk @as(u16, 65535);
            break :blk @as(u16, 0);
        };
        w.printf("  catch-default={d} catch-block={d}\n", .{ d, e2 });
    }

    var x: u32 = w.hide(@as(u32, 0));
    while (x < 30) : (x += 1) {
        const r = level0(x);
        if (r) |v| {
            w.printf("level0({d}) = {d}\n", .{ x, v });
        } else |e| {
            w.printf("level0({d}) error.{s}\n", .{ x, @errorName(e) });
        }
        flushLog("chain");
    }

    for (0..6) |f| {
        var res: [4]Resource = undefined;
        const fail: u8 = w.hide(@as(u8, @intCast(f)));
        if (openAll(fail, &res)) |_| {
            w.printf("openAll(fail={d}) ok\n", .{fail});
            var i: usize = res.len;
            while (i > 0) {
                i -= 1;
                res[i].close();
            }
        } else |e| w.printf("openAll(fail={d}) error.{s}\n", .{ fail, @errorName(e) });
        flushLog("open");
    }

    var k: i32 = w.hide(@as(i32, -3));
    while (k < 10) : (k += 1) {
        if (findPoint(k)) |maybe| {
            if (maybe) |p| w.printf("findPoint({d}) = ({d},{d})\n", .{ k, p.x, p.y }) else w.printf("findPoint({d}) = null\n", .{k});
        } else |e| w.printf("findPoint({d}) error.{s}\n", .{ k, @errorName(e) });
        const flat = (findPoint(k) catch null) orelse Point{ .x = -1, .y = -1 };
        w.printf("  flattened ({d},{d})\n", .{ flat.x, flat.y });
    }

    const pairs = [_][2]i32{ .{ 100, 7 }, .{ -100, 7 }, .{ 100, -7 }, .{ 5, 0 }, .{ std.math.minInt(i32), -1 }, .{ std.math.minInt(i32), 1 }, .{ 0, -5 } };
    for (pairs) |pr0| {
        const pr = w.hide(pr0);
        const q = divide(pr[0], pr[1]) catch |e| {
            w.printf("divide({d},{d}) error.{s}\n", .{ pr[0], pr[1], @errorName(e) });
            continue;
        };
        w.printf("divide({d},{d}) = {d}\n", .{ pr[0], pr[1], q });
    }

    const lists = [_][]const []const u8{
        &.{ "1", "2", "3" },
        &.{ "100", "x", "3" },
        &.{ "60000", "60000" },
        &.{},
        &.{ "65535", "" },
    };
    for (lists, 0..) |l, i| {
        if (sumAll(w.hide(l))) |s| w.printf("sumAll[{d}] = {d}\n", .{ i, s }) else |e| w.printf("sumAll[{d}] error.{s}\n", .{ i, @errorName(e) });
    }

    // Errors as values: compare, store in arrays, convert to anyerror.
    const errs = [_]anyerror{ error.Empty, error.Odd, error.TooLarge, error.OpenFailed, error.Empty };
    for (errs, 0..) |e, i| {
        const h = w.hide(e);
        w.printf("err[{d}] {s} eqEmpty={} isParse={}\n", .{ i, @errorName(h), h == error.Empty, isParse(h) });
    }
    var ok_count: u32 = 0;
    var err_count: u32 = 0;
    for (0..50) |i| {
        const v: u16 = w.hide(@as(u16, @intCast(i * 997 % 6000)));
        _ = checkRange(v, 1000, 4000) catch {
            err_count += 1;
            continue;
        };
        ok_count += 1;
    }
    w.printf("range ok={d} err={d}\n", .{ ok_count, err_count });
}

fn isParse(e: anyerror) bool {
    return switch (e) {
        error.Empty, error.BadDigit, error.Overflow => true,
        else => false,
    };
}
