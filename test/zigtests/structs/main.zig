// Structs, tuples and defer.
//
// Struct methods taking `self` by value, by `*Self` and by `*const Self`, so
// some calls copy the struct and some share it; default field values,
// including ones that refer to other declarations; nested structs and arrays
// of them assigned whole (an aggregate copy, which is a `memory.copy` or a
// run of loads and stores) against pointers to them; namespaces: structs used
// only for their declarations, with `pub const` values and functions, and
// container-level `var`s mutated through them; struct literals typed by their
// result location; tuples indexed at compile time, concatenated with
// `++`, iterated with `inline for` and destructured with
// `const a, const b = ...`.
//
// The second half is `defer` and `errdefer`: several defers in nested scopes,
// defers inside loop bodies (run once per iteration), a defer that sees a
// variable's value at scope exit rather than at the `defer`, early returns
// from the middle of a function with defers on both sides of them, and
// errdefers that run only on the error paths.
// Each defer appends a character to a log, and the log is printed, so an exit
// path that runs the defers in the wrong order, twice, or not at all shows as
// a different string.
const std = @import("std");
const w = @import("wasi");

const Vec2 = struct {
    x: i32 = 0,
    y: i32 = 0,

    const zero: Vec2 = .{};
    const unit_x: Vec2 = .{ .x = 1 };

    fn add(a: Vec2, b: Vec2) Vec2 {
        return .{ .x = a.x + b.x, .y = a.y + b.y };
    }
    fn scale(self: Vec2, k: i32) Vec2 {
        return .{ .x = self.x * k, .y = self.y * k };
    }
    fn dot(self: *const Vec2, o: Vec2) i64 {
        return @as(i64, self.x) * o.x + @as(i64, self.y) * o.y;
    }
    fn rotate90(self: *Vec2) void {
        const t = self.x;
        self.x = -self.y;
        self.y = t;
    }
};

const Body = struct {
    name: []const u8,
    pos: Vec2,
    vel: Vec2 = Vec2.unit_x,
    mass: u32 = default_mass * 2,
    tags: [3]u8 = .{ 'a', 'b', 'c' },
    history: [4]Vec2 = [_]Vec2{Vec2.zero} ++ [_]Vec2{.{ .x = -1, .y = -1 }} ++ [_]Vec2{ Vec2.zero, Vec2.unit_x },

    const default_mass = 21;

    fn step(self: *Body, n: u32) void {
        var i: u32 = 0;
        while (i < n) : (i += 1) {
            // Shift history: an overlapping copy within the struct.
            std.mem.copyBackwards(Vec2, self.history[1..], self.history[0..3]);
            self.history[0] = self.pos;
            self.pos = self.pos.add(self.vel);
            self.vel.rotate90();
        }
    }
    fn energy(self: Body) i64 {
        return @as(i64, self.mass) * self.vel.dot(self.vel);
    }
    fn show(self: *const Body) void {
        w.printf("{s}: pos=({d},{d}) vel=({d},{d}) mass={d} tags={s} hist=", .{
            self.name, self.pos.x, self.pos.y, self.vel.x, self.vel.y, self.mass, &self.tags,
        });
        for (self.history) |h| w.printf("({d},{d})", .{ h.x, h.y });
        w.puts("\n");
    }
};

fn methods() void {
    var a = Vec2{ .x = w.hide(@as(i32, 3)), .y = w.hide(@as(i32, -4)) };
    const b: Vec2 = .{ .x = 7 };
    w.printf("a=({d},{d}) b=({d},{d})\n", .{ a.x, a.y, b.x, b.y });
    const c = a.add(b).scale(w.hide(@as(i32, -2)));
    w.printf("c=({d},{d}) a.b={d} c.c={d}\n", .{ c.x, c.y, a.dot(b), c.dot(c) });
    var i: u32 = 0;
    while (i < 5) : (i += 1) {
        a.rotate90();
        w.printf("rot {d}: ({d},{d})\n", .{ i, a.x, a.y });
    }
    // Method call through a pointer and through a value copy.
    const pa = &a;
    pa.rotate90();
    var copy = a;
    copy.rotate90();
    w.printf("a=({d},{d}) copy=({d},{d}) zero=({d},{d})\n", .{ a.x, a.y, copy.x, copy.y, Vec2.zero.x, Vec2.zero.y });
}

fn nested() void {
    var bodies = [_]Body{
        .{ .name = "sun", .pos = .{}, .mass = 1000 },
        .{ .name = "earth", .pos = .{ .x = 10, .y = 0 }, .vel = .{ .x = 0, .y = 3 } },
        .{ .name = "moon", .pos = .{ .x = 11, .y = 0 }, .vel = .{ .x = 1, .y = 4 }, .tags = "xyz".* },
    };
    for (&bodies) |*b| b.show();
    for (&bodies, 0..) |*b, k| b.step(w.hide(@as(u32, @intCast(k + 2))));
    for (&bodies) |*b| b.show();
    // Whole-struct copy, then mutate the copy only.
    var snap = bodies[1];
    snap.name = "snapshot";
    snap.step(3);
    snap.history[2].x = 99;
    snap.show();
    bodies[1].show();
    // Copy of an array of structs.
    var again = bodies;
    again[0].mass = 1;
    again[2].tags[1] = '!';
    for (again, bodies) |x, y| w.printf("{s} {d} {s} / {s} {d} {s}\n", .{ x.name, x.mass, &x.tags, y.name, y.mass, &y.tags });
    var e: i64 = 0;
    for (bodies) |b| e += b.energy();
    w.printf("energy {d}\n", .{e});
}

// A namespace with state.
const Counter = struct {
    var hits: u32 = 0;
    var log: [64]u8 = undefined;
    var log_len: usize = 0;

    pub const limit = 10;

    pub fn hit(c: u8) void {
        hits += 1;
        if (log_len < log.len) {
            log[log_len] = c;
            log_len += 1;
        }
    }
    pub fn take() []const u8 {
        const s = log[0..log_len];
        return s;
    }
    pub fn reset() void {
        log_len = 0;
    }
};

fn tuples() void {
    const t = .{ @as(i32, -5), @as(u8, 200), "tuple", true, @as(f32, 1.5) };
    w.printf("t.len={d} t[0]={d} t[1]={d} t[2]={s} t[3]={} t[4]={d}\n", .{ t.len, w.hide(t[0]), w.hide(t[1]), t[2], t[3], t[4] });
    const u = t ++ .{ @as(u64, 77), 'z' };
    w.printf("u.len={d} u[5]={d} u[6]={c}\n", .{ u.len, u[5], u[6] });
    inline for (u, 0..) |field, i| {
        w.printf("field {d}: {s}\n", .{ i, @typeName(@TypeOf(field)) });
    }
    // Destructuring a tuple returned by a function.
    for ([_]u32{ 17, 100, 12345, 0 }) |n| {
        const q, const r = divmod(w.hide(n), 7);
        const lo, const hi = minmax(w.hide(@as(i32, @intCast(n))) - 50, 3);
        w.printf("divmod({d},7)=({d},{d}) minmax=({d},{d})\n", .{ n, q, r, lo, hi });
    }
    // Destructure into existing variables.  The tuple is built first: written
    // as `a, b = .{ b, a }` its fields are stored straight into a and b in
    // order, so b would read the new a.
    var a: i32 = w.hide(@as(i32, 1));
    var b: i32 = w.hide(@as(i32, 2));
    const pair: struct { i32, i32 } = .{ b, a };
    a, b = pair;
    w.printf("swapped a={d} b={d}\n", .{ a, b });
    a, b = .{ a * 10, a + b };
    w.printf("in order a={d} b={d}\n", .{ a, b });
    // Destructure an array.
    const arr = [3]u16{ 10, 20, 30 };
    const x, const y, const z = w.hide(arr);
    w.printf("array destructure {d} {d} {d}\n", .{ x, y, z });
    // A struct literal whose type comes from the result location.
    const named: Vec2 = .{ .x = w.hide(@as(i32, 5)), .y = 6 };
    const sum = Vec2.add(named, .{ .y = -10 });
    w.printf("named ({d},{d}) sum ({d},{d})\n", .{ named.x, named.y, sum.x, sum.y });
}

fn divmod(n: u32, d: u32) struct { u32, u32 } {
    return .{ n / d, n % d };
}

fn minmax(a: i32, b: i32) [2]i32 {
    return .{ @min(a, b), @max(a, b) };
}

// ---- defer ----

fn nestedDefers(depth: u32) void {
    Counter.hit('A');
    defer Counter.hit('a');
    {
        Counter.hit('B');
        defer Counter.hit('b');
        if (depth > 1) {
            Counter.hit('C');
            defer Counter.hit('c');
            defer Counter.hit('d');
        }
        Counter.hit('D');
    }
    Counter.hit('E');
}

fn loopDefers(n: u32) void {
    var i: u32 = 0;
    while (i < n) : (i += 1) {
        defer Counter.hit('0' + @as(u8, @intCast(i)));
        if (i == 1) continue;
        Counter.hit('x');
        if (i == 3) break;
    }
    Counter.hit('.');
}

fn earlyReturn(k: u32) u32 {
    var v: u32 = 1;
    defer Counter.hit('r');
    v += k;
    if (v > 5) {
        defer Counter.hit('s');
        return v * 10;
    }
    defer Counter.hit('t');
    if (v == 3) return v * 100;
    v += 1;
    return v;
}

fn lateValue() u32 {
    var x: u32 = 1;
    // The defer reads x at scope exit, after it is changed below.
    defer Counter.hit(@intCast('0' + x));
    x = 7;
    return x;
}

const E = error{Fail};

fn errDefers(fail_at: u32) E!u32 {
    Counter.hit('[');
    defer Counter.hit(']');
    errdefer Counter.hit('!');
    var i: u32 = 0;
    while (i < 4) : (i += 1) {
        errdefer Counter.hit('e');
        defer Counter.hit('-');
        if (i == fail_at) return error.Fail;
        Counter.hit('+');
    }
    return i;
}

fn errDeferCapture(fail: bool) E!void {
    errdefer Counter.hit('<');
    defer Counter.hit('>');
    if (fail) return error.Fail;
    Counter.hit('o');
}

fn defers() void {
    for ([_]u32{ 1, 2 }) |d| {
        Counter.reset();
        nestedDefers(w.hide(d));
        w.printf("nested({d}): {s}\n", .{ d, Counter.take() });
    }
    for ([_]u32{ 0, 1, 2, 5 }) |n| {
        Counter.reset();
        loopDefers(w.hide(n));
        w.printf("loop({d}): {s}\n", .{ n, Counter.take() });
    }
    for ([_]u32{ 0, 1, 2, 3, 9 }) |k| {
        Counter.reset();
        const r = earlyReturn(w.hide(k));
        w.printf("early({d})={d}: {s}\n", .{ k, r, Counter.take() });
    }
    Counter.reset();
    const lv = lateValue();
    w.printf("late={d}: {s}\n", .{ lv, Counter.take() });
    for ([_]u32{ 0, 2, 9 }) |f| {
        Counter.reset();
        const r = errDefers(w.hide(f));
        if (r) |v| {
            w.printf("errdefer({d}) ok {d}: {s}\n", .{ f, v, Counter.take() });
        } else |err| {
            w.printf("errdefer({d}) {s}: {s}\n", .{ f, @errorName(err), Counter.take() });
        }
    }
    for ([_]bool{ false, true }) |f| {
        Counter.reset();
        errDeferCapture(w.hide(f)) catch |err| {
            for (@errorName(err)) |ch| Counter.hit(ch);
        };
        w.printf("capture({}): {s}\n", .{ f, Counter.take() });
    }
    w.printf("total hits {d} (limit decl {d})\n", .{ Counter.hits, Counter.limit });
}

pub fn main() void {
    methods();
    nested();
    tuples();
    defers();
    w.printf("sizes Vec2={d} Body={d}\n", .{ @sizeOf(Vec2), @sizeOf(Body) });
    w.puts("done\n");
}
