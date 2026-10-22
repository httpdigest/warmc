// @Vector(4, f32): a Mandelbrot set four pixels at a time, where the lanes
// that have escaped are masked off with `@select` and the loop ends when
// `@reduce(.Or, ...)` says none is left, then a pass that normalises 3-vectors
// padded to four lanes with `@sqrt` and sums them with `@reduce(.Add, ...)`.
//
// simdsum is the corpus's integer vectors; these are floating point, and the
// interesting parts are the ones that cross between lanes and control.  The
// escape test is `f32x4.le` into `v128.any_true` into a `br_if`, so a vector
// compare decides the loop.  The masked count needs no select at all: a true
// lane is all ones, so LLVM subtracts the mask with `i32x4.sub`.  The
// horizontal sum is four `f32x4.extract_lane` and three scalar `f32.add` in
// order, since float addition may not be reassociated, and its `f32.sqrt`
// is scalar, splatted back for an `f32x4.div`.  Every one of these must give
// wasmtime's bits exactly: the escape counts and the sum of norms are both in
// the checksum.
const w = @import("wasi");

const V = @Vector(4, f32);
const U = @Vector(4, u32);

const WIDTH = 256;
const HEIGHT = 160;
const MAX_ITER = 96;

var counts: [WIDTH * HEIGHT]u32 = undefined;
var pts: [4096]V = undefined;

fn mandel(x0: f32, y0: f32, scale: f32) u64 {
    const lane: V = .{ 0, 1, 2, 3 };
    var total: u64 = 0;
    var py: usize = 0;
    while (py < HEIGHT) : (py += 1) {
        const ci: V = @splat(y0 + @as(f32, @floatFromInt(py)) * scale);
        var px: usize = 0;
        while (px < WIDTH) : (px += 4) {
            const cr = @as(V, @splat(x0 + @as(f32, @floatFromInt(px)) * scale)) + lane * @as(V, @splat(scale));
            var zr: V = @splat(0);
            var zi: V = @splat(0);
            var n: U = @splat(0);
            var it: u32 = 0;
            while (it < MAX_ITER) : (it += 1) {
                const zr2 = zr * zr;
                const zi2 = zi * zi;
                const alive = zr2 + zi2 <= @as(V, @splat(4.0));
                if (!@reduce(.Or, alive)) break;
                n += @select(u32, alive, @as(U, @splat(1)), @as(U, @splat(0)));
                zi = @as(V, @splat(2.0)) * zr * zi + ci;
                zr = zr2 - zi2 + cr;
            }
            counts[py * WIDTH + px ..][0..4].* = n;
            total += @reduce(.Add, n);
        }
    }
    return total;
}

fn normalise() f32 {
    var acc: V = @splat(0);
    for (&pts) |*p| {
        const len = @sqrt(@reduce(.Add, p.* * p.*));
        const unit = p.* / @as(V, @splat(len + 1.0e-6));
        acc += @abs(unit);
        p.* = unit * @as(V, @splat(1.5)) + @as(V, .{ 0.25, -0.5, 0.125, 0 });
    }
    return @reduce(.Add, acc);
}

pub fn main() void {
    var x: u32 = 99;
    for (&pts) |*p| {
        var v: [4]f32 = .{ 0, 0, 0, 0 };
        for (v[0..3]) |*c| {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            c.* = @as(f32, @floatFromInt(x & 0xffff)) / 4096.0 - 8.0;
        }
        p.* = v;
    }
    var total: u64 = 0;
    var norm: f64 = 0;
    var r: u32 = 0;
    while (r < 60) : (r += 1) {
        const zoom = 1.0 - @as(f32, @floatFromInt(r)) * 0.012;
        total +%= mandel(-2.0 * zoom, -1.0 * zoom, 2.6 * zoom / WIDTH);
        norm += normalise();
    }
    var h: u64 = 0;
    for (&counts) |c| h = h *% 31 +% c;
    w.print("total = ", total +% h +% @as(u64, @intFromFloat(norm * 1000.0)));
}
