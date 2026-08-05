// @Vector: Zig writes the SIMD itself, so this arrives as v128 already and asks
// what warmc does with a producer's vectors rather than whether its own
// vectoriser finds the loop.
//
// It only arrives that way because the corpus builds with `+simd128`, which is
// not in `baseline`: without it Zig scalarises `@Vector` and this kernel was
// measuring scalar code while claiming otherwise -- 1.12x against wasmtime,
// which became 1.00x once the vectors were real.
const w = @import("wasi");

const V = @Vector(4, u32);
var data: [65536]u32 = undefined;

pub fn main() void {
    var seed: u32 = 7;
    for (&data) |*d| {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        d.* = seed;
    }
    var total: u64 = 0;
    var r: u32 = 0;
    while (r < 12000) : (r += 1) {
        var acc: V = @splat(0);
        var i: usize = 0;
        while (i < data.len) : (i += 4) {
            const v: V = data[i..][0..4].*;
            acc +%= (v ^ @as(V, @splat(0x5a5a5a5a))) *% @as(V, @splat(3));
        }
        total +%= @reduce(.Add, acc);
        data[r % data.len] +%= 1;
    }
    w.print("total = ", total);
}
