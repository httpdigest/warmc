// Functions as values: every way a Zig program calls code it does not name at
// the call site.
//
// Run-time function pointers (`*const fn`) are wasm `call_indirect` through
// the module's table, with a type check against the signature at every call;
// a comptime function parameter is the opposite, a direct call (usually
// inlined) in a copy of the caller made for that function.  Both are used
// here for the same work, so the two lowerings have to agree.  Covered:
// tables of function pointers indexed by run-time values (several
// signatures, so several wasm types share the table); a callback with a
// type-erased context (`*anyopaque` cast back with @ptrCast and @alignCast);
// the vtable interface pattern std.mem.Allocator uses, with two
// implementations behind one interface type; function pointers stored in
// struct fields and returned from functions; optional function pointers;
// and map / filter / fold written both as generic (comptime fn) and as
// run-time (fn pointer) higher-order functions.
//
// Selectors come through `w.hide`, so LLVM cannot turn an indirect call into
// a direct one; every result is printed.
const std = @import("std");
const w = @import("wasi");

const BinOp = *const fn (i64, i64) i64;

fn add(a: i64, b: i64) i64 {
    return a +% b;
}
fn sub(a: i64, b: i64) i64 {
    return a -% b;
}
fn mul(a: i64, b: i64) i64 {
    return a *% b;
}
fn maxf(a: i64, b: i64) i64 {
    return @max(a, b);
}
fn xorf(a: i64, b: i64) i64 {
    return a ^ b;
}
fn gcd(a: i64, b: i64) i64 {
    var x: u64 = @abs(a);
    var y: u64 = @abs(b);
    while (y != 0) {
        const t = x % y;
        x = y;
        y = t;
    }
    return @intCast(x);
}

const ops = [_]struct { name: []const u8, f: BinOp }{
    .{ .name = "add", .f = add },
    .{ .name = "sub", .f = sub },
    .{ .name = "mul", .f = mul },
    .{ .name = "max", .f = maxf },
    .{ .name = "xor", .f = xorf },
    .{ .name = "gcd", .f = gcd },
};

fn tables() void {
    const xs = [_]i64{ 12, -7, 100, 0, 36, -48 };
    var sel: u32 = w.hide(@as(u32, 1));
    for (xs, 0..) |a, i| {
        const b = xs[(i + 2) % xs.len];
        for (0..ops.len) |k| {
            const idx = (k + sel) % ops.len;
            const op = w.hide(ops[idx]);
            w.printf("{s}({d},{d})={d}  ", .{ op.name, a, b, op.f(a, b) });
        }
        w.puts("\n");
        sel = sel *% 7 +% 3;
    }
    // A fold through a run-time chosen op.
    for (0..ops.len) |k| {
        const f = w.hide(ops[k].f);
        var acc: i64 = xs[0];
        for (xs[1..]) |x| acc = f(acc, x);
        w.printf("fold {s} = {d}\n", .{ ops[k].name, acc });
    }
}

// Several signatures through one table.
fn unaryNeg(x: i32) i32 {
    return -%x;
}
fn unarySq(x: i32) i32 {
    return x *% x;
}
fn toF(x: i32, scale: f64) f64 {
    return @as(f64, @floatFromInt(x)) * scale;
}
fn pred3(x: u8) bool {
    return x % 3 == 0;
}
fn predOdd(x: u8) bool {
    return x & 1 == 1;
}

fn signatures() void {
    const unary = [_]*const fn (i32) i32{ unaryNeg, unarySq };
    const preds = [_]*const fn (u8) bool{ pred3, predOdd };
    const conv: *const fn (i32, f64) f64 = w.hide(@as(*const fn (i32, f64) f64, toF));
    var i: i32 = -3;
    while (i <= 3) : (i += 1) {
        const u = unary[w.hide(@as(usize, @intCast(i + 3))) % 2];
        const p = preds[w.hide(@as(usize, @intCast(i + 4))) % 2];
        w.printf("i={d} unary={d} pred={} conv={d:.3}\n", .{ i, u(i), p(@intCast(i + 10)), conv(i, 0.25) });
    }
}

// Callbacks with a type-erased context.
const Visitor = struct {
    ctx: *anyopaque,
    visit: *const fn (ctx: *anyopaque, value: u32) void,
};

fn walkTree(n: u32, depth: u32, v: Visitor) void {
    v.visit(v.ctx, n);
    if (depth == 0) return;
    walkTree(n * 2, depth - 1, v);
    walkTree(n * 2 + 1, depth - 1, v);
}

const SumCtx = struct {
    total: u64 = 0,
    count: u32 = 0,
    fn visit(ctx: *anyopaque, value: u32) void {
        const self: *SumCtx = @ptrCast(@alignCast(ctx));
        self.total += value;
        self.count += 1;
    }
};

const TraceCtx = struct {
    buf: [128]u8 = undefined,
    len: usize = 0,
    fn visit(ctx: *anyopaque, value: u32) void {
        const self: *TraceCtx = @ptrCast(@alignCast(ctx));
        const s = std.mem.print(self.buf[self.len..], "{d},", .{value}) catch return;
        self.len += s.len;
    }
};

fn callbacks() void {
    var depth: u32 = 0;
    while (depth < 5) : (depth += 1) {
        var sum = SumCtx{};
        var trace = TraceCtx{};
        walkTree(1, w.hide(depth), .{ .ctx = &sum, .visit = SumCtx.visit });
        walkTree(1, w.hide(@min(depth, 3)), .{ .ctx = &trace, .visit = TraceCtx.visit });
        w.printf("depth {d}: count={d} total={d} trace={s}\n", .{ depth, sum.count, sum.total, trace.buf[0..trace.len] });
    }
}

// The vtable interface pattern.
const Shape = struct {
    ptr: *anyopaque,
    vtable: *const VTable,

    const VTable = struct {
        area: *const fn (*anyopaque) f64,
        perimeter: *const fn (*anyopaque) f64,
        scale: *const fn (*anyopaque, f64) void,
        name: []const u8,
    };

    fn area(s: Shape) f64 {
        return s.vtable.area(s.ptr);
    }
    fn perimeter(s: Shape) f64 {
        return s.vtable.perimeter(s.ptr);
    }
    fn scale(s: Shape, k: f64) void {
        s.vtable.scale(s.ptr, k);
    }
};

const Rect = struct {
    w: f64,
    h: f64,
    fn shape(self: *Rect) Shape {
        return .{ .ptr = self, .vtable = &.{ .area = area, .perimeter = perim, .scale = scale, .name = "rect" } };
    }
    fn area(p: *anyopaque) f64 {
        const self: *Rect = @ptrCast(@alignCast(p));
        return self.w * self.h;
    }
    fn perim(p: *anyopaque) f64 {
        const self: *Rect = @ptrCast(@alignCast(p));
        return 2 * (self.w + self.h);
    }
    fn scale(p: *anyopaque, k: f64) void {
        const self: *Rect = @ptrCast(@alignCast(p));
        self.w *= k;
        self.h *= k;
    }
};

const Circle = struct {
    r: f64,
    fn shape(self: *Circle) Shape {
        return .{ .ptr = self, .vtable = &.{ .area = area, .perimeter = perim, .scale = scale, .name = "circle" } };
    }
    fn area(p: *anyopaque) f64 {
        const self: *Circle = @ptrCast(@alignCast(p));
        return std.math.pi * self.r * self.r;
    }
    fn perim(p: *anyopaque) f64 {
        const self: *Circle = @ptrCast(@alignCast(p));
        return 2 * std.math.pi * self.r;
    }
    fn scale(p: *anyopaque, k: f64) void {
        const self: *Circle = @ptrCast(@alignCast(p));
        self.r *= k;
    }
};

fn interfaces() void {
    var r1 = Rect{ .w = w.hide(@as(f64, 3)), .h = 4 };
    var r2 = Rect{ .w = 0.5, .h = w.hide(@as(f64, 10)) };
    var c1 = Circle{ .r = w.hide(@as(f64, 1)) };
    var c2 = Circle{ .r = 2.5 };
    const shapes = [_]Shape{ r1.shape(), c1.shape(), r2.shape(), c2.shape() };
    var round: u32 = 0;
    while (round < 3) : (round += 1) {
        var total: f64 = 0;
        for (shapes, 0..) |s, i| {
            w.printf("round {d} {s}#{d}: area={d:.4} perimeter={d:.4}\n", .{ round, s.vtable.name, i, s.area(), s.perimeter() });
            total += s.area();
            s.scale(if (i % 2 == 0) 2 else 0.5);
        }
        w.printf("round {d} total area {d:.4}\n", .{ round, total });
    }
    w.printf("after: r1={d}x{d} r2={d}x{d} c1={d} c2={d}\n", .{ r1.w, r1.h, r2.w, r2.h, c1.r, c2.r });
}

// Returning function pointers, and optional ones.
fn pick(name: []const u8) ?BinOp {
    for (ops) |o| {
        if (std.mem.eql(u8, o.name, name)) return o.f;
    }
    return null;
}

fn compose(comptime f: fn (i64) i64, comptime g: fn (i64) i64) fn (i64) i64 {
    return struct {
        fn h(x: i64) i64 {
            return g(f(x));
        }
    }.h;
}

fn inc(x: i64) i64 {
    return x + 1;
}
fn dbl(x: i64) i64 {
    return x * 2;
}
fn sq(x: i64) i64 {
    return x * x;
}

fn returning() void {
    const names = [_][]const u8{ "mul", "nope", "gcd", "sub", "", "xor" };
    for (names) |n| {
        if (pick(w.hide(n))) |f| {
            w.printf("pick {s}: f(84,-36)={d}\n", .{ n, f(84, -36) });
        } else {
            w.printf("pick {s}: none\n", .{n});
        }
    }
    const h1 = compose(inc, dbl);
    const h2 = compose(dbl, inc);
    const h3 = compose(compose(inc, sq), dbl);
    const chain = [_]*const fn (i64) i64{ h1, h2, h3, inc, sq };
    var x: i64 = w.hide(@as(i64, 3));
    for (0..10) |i| {
        const f = chain[w.hide(i) % chain.len];
        const y = f(x);
        w.printf("chain {d}: f({d})={d}\n", .{ i, x, y });
        x = @mod(y, 1000) - 7;
    }
}

// Higher-order helpers, generic and run-time.
fn mapComptime(comptime f: fn (i32) i32, xs: []i32) void {
    for (xs) |*x| x.* = f(x.*);
}
fn mapRuntime(f: *const fn (i32) i32, xs: []i32) void {
    for (xs) |*x| x.* = f(x.*);
}
fn filterInto(comptime T: type, out: []T, xs: []const T, pred: *const fn (T) bool) []T {
    var n: usize = 0;
    for (xs) |x| {
        if (pred(x)) {
            out[n] = x;
            n += 1;
        }
    }
    return out[0..n];
}
fn foldl(comptime T: type, comptime A: type, xs: []const T, init: A, f: fn (A, T) A) A {
    var acc = init;
    for (xs) |x| acc = f(acc, x);
    return acc;
}
fn triple(x: i32) i32 {
    return x *% 3 -% 1;
}
fn isPos(x: i32) bool {
    return x > 0;
}
fn isEven(x: i32) bool {
    return @rem(x, 2) == 0;
}
fn hashStep(acc: u64, x: i32) u64 {
    return (acc ^ @as(u32, @bitCast(x))) *% 0x100000001b3;
}

fn higherOrder() void {
    var a: [12]i32 = undefined;
    var b: [12]i32 = undefined;
    for (&a, &b, 0..) |*x, *y, i| {
        x.* = w.hide(@as(i32, @intCast(i))) * 5 - 27;
        y.* = x.*;
    }
    mapComptime(triple, &a);
    mapRuntime(w.hide(@as(*const fn (i32) i32, triple)), &b);
    w.printf("map same={}\n", .{std.mem.eql(i32, &a, &b)});
    w.puts("mapped:");
    for (a) |x| w.printf(" {d}", .{x});
    w.puts("\n");
    var out: [12]i32 = undefined;
    const preds = [_]*const fn (i32) bool{ isPos, isEven };
    for (preds, 0..) |p, k| {
        const f = filterInto(i32, &out, &a, w.hide(p));
        w.printf("filter {d}: n={d}:", .{ k, f.len });
        for (f) |x| w.printf(" {d}", .{x});
        w.printf(" hash={x}\n", .{foldl(i32, u64, f, 0xcbf29ce484222325, hashStep)});
    }
    const s = foldl(i32, i64, &a, 0, struct {
        fn f(acc: i64, x: i32) i64 {
            return acc * 3 + x;
        }
    }.f);
    w.printf("fold sum3 {d}\n", .{s});
}

// A struct holding function pointers as state handlers.
const Machine = struct {
    state: u8 = 0,
    acc: i32 = 0,
    handlers: [4]*const fn (*Machine, u8) void,

    fn feed(self: *Machine, c: u8) void {
        self.handlers[self.state](self, c);
    }
    fn idle(m: *Machine, c: u8) void {
        if (c == '+') m.state = 1 else if (c == '-') m.state = 2 else if (c == '*') m.state = 3;
    }
    fn plus(m: *Machine, c: u8) void {
        m.acc += c - '0';
        m.state = 0;
    }
    fn minus(m: *Machine, c: u8) void {
        m.acc -= c - '0';
        m.state = 0;
    }
    fn times(m: *Machine, c: u8) void {
        m.acc *= c - '0';
        m.state = 0;
    }
};

fn machine() void {
    var m = Machine{ .handlers = .{ Machine.idle, Machine.plus, Machine.minus, Machine.times } };
    const prog = w.hide(@as([]const u8, "+5+3*4-9 *2+7-1*3+0-8"));
    for (prog, 0..) |c, i| {
        m.feed(c);
        w.printf("step {d} '{c}' state={d} acc={d}\n", .{ i, c, m.state, m.acc });
    }
}

pub fn main() void {
    tables();
    signatures();
    callbacks();
    interfaces();
    returning();
    higherOrder();
    machine();
    w.puts("done\n");
}
