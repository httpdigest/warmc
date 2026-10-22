// Generic types and functions: Zig's generics are functions that take types
// and return types, so every instantiation is a separate monomorphised copy.
//
// `Stack(T)`, `RingBuffer(T, N)`, `Matrix(T, R, C)` and `Pair(A, B)` are each
// instantiated over several element types -- u8, i17, u64, f32, f64 and a
// struct -- so the module holds the same algorithm compiled for i32, i64, f32
// and f64 registers and for memory-held records, with the per-width masking
// that the odd integer widths need.  `anytype` parameters are duck-typed
// (`sumAll` takes anything with `.len` and indexing; `area` anything with an
// `area` method), `@TypeOf` builds result types from arguments, and a
// comptime switch picks an accumulator type per element type.  Every push,
// pop, matrix product and fold is printed, so a slip in any one copy shows as
// a line of its own.
const std = @import("std");
const w = @import("wasi");

fn Stack(comptime T: type) type {
    return struct {
        const Self = @This();
        items: [64]T = undefined,
        len: usize = 0,

        fn push(self: *Self, v: T) void {
            self.items[self.len] = v;
            self.len += 1;
        }
        fn pop(self: *Self) ?T {
            if (self.len == 0) return null;
            self.len -= 1;
            return self.items[self.len];
        }
        fn peek(self: *const Self) ?T {
            return if (self.len == 0) null else self.items[self.len - 1];
        }
    };
}

fn RingBuffer(comptime T: type, comptime N: usize) type {
    return struct {
        const Self = @This();
        const capacity = N;
        buf: [N]T = undefined,
        head: usize = 0,
        count: usize = 0,

        fn put(self: *Self, v: T) ?T {
            var evicted: ?T = null;
            if (self.count == N) {
                evicted = self.buf[self.head];
                self.head = (self.head + 1) % N;
                self.count -= 1;
            }
            self.buf[(self.head + self.count) % N] = v;
            self.count += 1;
            return evicted;
        }
        fn get(self: *const Self, i: usize) T {
            return self.buf[(self.head + i) % N];
        }
    };
}

fn Matrix(comptime T: type, comptime R: usize, comptime C: usize) type {
    return struct {
        const Self = @This();
        const rows = R;
        const cols = C;
        m: [R][C]T,

        fn mul(a: Self, b: Matrix(T, C, R)) Matrix(T, R, R) {
            var out: Matrix(T, R, R) = .{ .m = undefined };
            for (0..R) |i| {
                for (0..R) |j| {
                    var s: T = 0;
                    for (0..C) |k| {
                        if (@typeInfo(T) == .int) {
                            s +%= a.m[i][k] *% b.m[k][j];
                        } else {
                            s += a.m[i][k] * b.m[k][j];
                        }
                    }
                    out.m[i][j] = s;
                }
            }
            return out;
        }
        fn transpose(a: Self) Matrix(T, C, R) {
            var out: Matrix(T, C, R) = .{ .m = undefined };
            for (0..R) |i| {
                for (0..C) |j| out.m[j][i] = a.m[i][j];
            }
            return out;
        }
        fn print(a: Self, name: []const u8) void {
            for (0..R) |i| {
                w.printf("{s}[{d}]:", .{ name, i });
                for (0..C) |j| {
                    if (@typeInfo(T) == .float) w.printf(" {d:.3}", .{a.m[i][j]}) else w.printf(" {d}", .{a.m[i][j]});
                }
                w.puts("\n");
            }
        }
    };
}

fn Pair(comptime A: type, comptime B: type) type {
    return struct {
        first: A,
        second: B,
        fn swap(self: @This()) Pair(B, A) {
            return .{ .first = self.second, .second = self.first };
        }
    };
}

fn pair(a: anytype, b: anytype) Pair(@TypeOf(a), @TypeOf(b)) {
    return .{ .first = a, .second = b };
}

fn Acc(comptime T: type) type {
    return switch (@typeInfo(T)) {
        .int => |i| if (i.signedness == .signed) i128 else u128,
        .float => f64,
        else => @compileError("no accumulator for " ++ @typeName(T)),
    };
}

fn sumAll(xs: anytype) Acc(@TypeOf(xs[0])) {
    const T = Acc(@TypeOf(xs[0]));
    var s: T = 0;
    for (0..xs.len) |i| s += xs[i];
    return s;
}

fn maxOf(comptime T: type, xs: []const T) T {
    var m = xs[0];
    for (xs[1..]) |x| {
        if (x > m) m = x;
    }
    return m;
}

const Rect = struct {
    wd: f64,
    ht: f64,
    fn area(self: Rect) f64 {
        return self.wd * self.ht;
    }
};
const Circle = struct {
    r: f64,
    fn area(self: Circle) f64 {
        return std.math.pi * self.r * self.r;
    }
};
const Tri = struct {
    b: u32,
    h: u32,
    fn area(self: Tri) f64 {
        return @as(f64, @floatFromInt(self.b * self.h)) / 2;
    }
};

fn area(shape: anytype) f64 {
    return shape.area();
}

const Point = struct {
    x: i32,
    y: i32,
    fn lessThan(a: Point, b: Point) bool {
        return if (a.x != b.x) a.x < b.x else a.y < b.y;
    }
};

fn insertionSort(comptime T: type, xs: []T, comptime less: fn (T, T) bool) void {
    var i: usize = 1;
    while (i < xs.len) : (i += 1) {
        const v = xs[i];
        var j = i;
        while (j > 0 and less(v, xs[j - 1])) : (j -= 1) xs[j] = xs[j - 1];
        xs[j] = v;
    }
}

fn lessNum(comptime T: type) fn (T, T) bool {
    return struct {
        fn f(a: T, b: T) bool {
            return a < b;
        }
    }.f;
}

// The low bits of `x` as a T of any integer width and signedness.
fn bitsOf(comptime T: type, x: u32) T {
    const U = @Int(.unsigned, @bitSizeOf(T));
    const wide: u64 = x;
    return @bitCast(@as(U, @truncate(wide *% 0x9e3779b97f4a7c15 >> 7)));
}

fn stackDemo(comptime T: type, seed: u32) void {
    var s: Stack(T) = .{};
    var x = w.hide(seed);
    for (0..10) |i| {
        x = x *% 1103515245 +% 12345;
        const v: T = if (@typeInfo(T) == .float) @floatFromInt(@as(i32, @bitCast(x)) >> 20) else bitsOf(T, x >> 8);
        s.push(v);
        if (@typeInfo(T) == .float) w.printf("{s} push[{d}] {d:.2}\n", .{ @typeName(T), i, v }) else w.printf("{s} push[{d}] {d}\n", .{ @typeName(T), i, v });
        if (i % 3 == 2) {
            const p = s.pop().?;
            if (@typeInfo(T) == .float) w.printf("  pop {d:.2}\n", .{p}) else w.printf("  pop {d}\n", .{p});
        }
    }
    w.printf("{s} len={d}\n", .{ @typeName(T), s.len });
    while (s.pop()) |v| {
        if (@typeInfo(T) == .float) w.printf("  drain {d:.2}\n", .{v}) else w.printf("  drain {d}\n", .{v});
    }
    w.printf("  peek after drain null={}\n", .{s.peek() == null});
}

fn sortDemo(comptime T: type, seed: u32) void {
    var xs: [12]T = undefined;
    var x = w.hide(seed);
    for (&xs) |*e| {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        e.* = if (@typeInfo(T) == .float) @as(T, @floatFromInt(x % 1000)) / 8 else bitsOf(T, x);
    }
    insertionSort(T, &xs, lessNum(T));
    w.printf("sorted {s}:", .{@typeName(T)});
    for (xs) |e| {
        if (@typeInfo(T) == .float) w.printf(" {d:.3}", .{e}) else w.printf(" {d}", .{e});
    }
    if (@typeInfo(T) == .float) w.printf(" | max={d:.3} sum={d:.3}\n", .{ maxOf(T, &xs), sumAll(xs) }) else w.printf(" | max={d} sum={d}\n", .{ maxOf(T, &xs), sumAll(xs) });
}

pub fn main() void {
    stackDemo(u8, 1);
    stackDemo(i17, 2);
    stackDemo(u64, 3);
    stackDemo(f32, 4);
    stackDemo(f64, 5);

    var ps: Stack(Point) = .{};
    for (0..6) |i| ps.push(.{ .x = w.hide(@as(i32, @intCast(i))) * 3 - 7, .y = @as(i32, @intCast(i * i)) });
    while (ps.pop()) |p| w.printf("point ({d},{d})\n", .{ p.x, p.y });

    var rb: RingBuffer(u16, 5) = .{};
    for (0..13) |i| {
        const v: u16 = w.hide(@as(u16, @intCast(i * 1000 + 7)));
        const ev = rb.put(v);
        w.printf("ring put {d} evicted={?d} count={d} contents:", .{ v, ev, rb.count });
        for (0..rb.count) |k| w.printf(" {d}", .{rb.get(k)});
        w.puts("\n");
    }
    w.printf("ring capacity {d}\n", .{@TypeOf(rb).capacity});

    var rbs: RingBuffer([]const u8, 3) = .{};
    const words = [_][]const u8{ "one", "two", "three", "four", "five" };
    for (words) |wd| {
        const ev = rbs.put(w.hide(wd));
        w.printf("ring str put {s} evicted={s}\n", .{ wd, ev orelse "-" });
    }

    const MI = Matrix(i32, 2, 3);
    var a: MI = .{ .m = undefined };
    var seed: i32 = w.hide(@as(i32, 5));
    for (0..2) |i| for (0..3) |j| {
        seed = seed *% 31 +% 7;
        a.m[i][j] = @rem(seed, 21) - 10;
    };
    a.print("A");
    const at = a.transpose();
    at.print("At");
    a.mul(at).print("A*At");
    at.mul(a).print("At*A");

    const MF = Matrix(f64, 3, 3);
    var f: MF = .{ .m = undefined };
    for (0..3) |i| for (0..3) |j| {
        f.m[i][j] = w.hide(@as(f64, @floatFromInt(i * 3 + j))) / 4.0 - 1.0;
    };
    f.print("F");
    var p = f;
    for (0..4) |k| {
        p = p.mul(f);
        w.printf("F^{d}\n", .{k + 2});
        p.print("  P");
    }

    const MU = Matrix(u8, 4, 2);
    var u: MU = .{ .m = undefined };
    for (0..4) |i| for (0..2) |j| {
        u.m[i][j] = w.hide(@as(u8, @intCast(i * 50 + j * 30 + 17)));
    };
    u.mul(u.transpose()).print("U*Ut");

    const pr = pair(w.hide(@as(u8, 42)), w.hide(@as(f32, 2.5)));
    const sw = pr.swap();
    w.printf("pair {d} {d:.2} -> {d:.2} {d} types {s} {s}\n", .{ pr.first, pr.second, sw.first, sw.second, @typeName(@TypeOf(sw.first)), @typeName(@TypeOf(sw.second)) });
    const pp = pair(pr, pair(w.hide(@as(i64, -9)), w.hide(true)));
    w.printf("nested pair {d} {d:.2} {d} {}\n", .{ pp.first.first, pp.first.second, pp.second.first, pp.second.second });

    const shapes_r = [_]Rect{ .{ .wd = 2, .ht = 3 }, .{ .wd = 1.5, .ht = 4.25 } };
    for (shapes_r) |s| w.printf("rect area {d:.4}\n", .{area(w.hide(s))});
    const circles = [_]Circle{ .{ .r = 1 }, .{ .r = 2.5 } };
    for (circles) |c| w.printf("circle area {d:.4}\n", .{area(w.hide(c))});
    w.printf("tri area {d:.4}\n", .{area(w.hide(Tri{ .b = 7, .h = 9 }))});

    sortDemo(u8, 11);
    sortDemo(i17, 12);
    sortDemo(u48, 13);
    sortDemo(i64, 14);
    sortDemo(f32, 15);
    sortDemo(f64, 16);

    var pts: [8]Point = undefined;
    var x: u32 = w.hide(@as(u32, 99));
    for (&pts) |*e| {
        x = x *% 1664525 +% 1013904223;
        e.* = .{ .x = @as(i32, @intCast(x >> 29)) - 3, .y = @as(i32, @intCast((x >> 10) & 15)) };
    }
    insertionSort(Point, &pts, Point.lessThan);
    for (pts) |e| w.printf("pt ({d},{d})\n", .{ e.x, e.y });

    const arr_i = [_]i8{ -128, 127, -1, 5 };
    const arr_u = [_]u32{ 0xffffffff, 0xffffffff, 3 };
    const arr_f = [_]f32{ 0.5, 0.25, -2.0 };
    w.printf("sumAll i8={d} u32={d} f32={d:.3}\n", .{ sumAll(w.hide(arr_i)), sumAll(w.hide(arr_u)), sumAll(w.hide(arr_f)) });
    w.printf("acc types {s} {s} {s}\n", .{ @typeName(Acc(i8)), @typeName(Acc(u32)), @typeName(Acc(f32)) });
}
