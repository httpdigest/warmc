// A 96x96 f64 multiply, the corpus's floating-point kernel and the one every
// other corpus also has.
const w = @import("wasi");

const N = 96;
var x: [N * N]f64 = undefined;
var y: [N * N]f64 = undefined;
var z: [N * N]f64 = undefined;

pub fn main() void {
    var i: usize = 0;
    while (i < N * N) : (i += 1) {
        x[i] = @floatFromInt((i * 7) % 13);
        y[i] = @floatFromInt((i * 5) % 11);
    }
    var r: u32 = 0;
    while (r < 480) : (r += 1) {
        var row: usize = 0;
        while (row < N) : (row += 1) {
            var col: usize = 0;
            while (col < N) : (col += 1) {
                var s: f64 = 0;
                var k: usize = 0;
                while (k < N) : (k += 1) s += x[row * N + k] * y[k * N + col];
                z[row * N + col] = s;
            }
        }
        x[r % (N * N)] += 1;
    }
    var bits: u64 = 0;
    for (z) |v| bits +%= @as(u64, @intFromFloat(@abs(v)));
    w.print("total = ", bits);
}
