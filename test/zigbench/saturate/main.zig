// Saturating arithmetic: `+|`, `-|`, `*|` and `<<|` on i16 audio samples and
// u8 pixels, where a result that does not fit sticks at the type's bound
// instead of wrapping.  The filter's taps are a comptime array walked with
// `inline for`, so each tap is its own constant multiply in straight-line code.
//
// wasm SIMD has saturating adds for exactly these lane types, and the corpus
// builds with `+simd128`, but none of this comes out as v128: LLVM keeps every
// loop scalar.  So each saturating op is lowered by hand into compares and
// `select`s -- `+|` on i16 clamps against both bounds, `-|` on u8 is a
// subtract kept only if it did not wrap, `<<|` shifts back and compares, and
// `*|` is an `i64.mul` whose high word is checked against the sign of the low
// one -- about thirty `select`s across the kernel and almost no branches.  A
// loop of dependent selects is a different test of instruction selection from
// the branchy code that dominates the rest of the corpus.
const w = @import("wasi");

const N = 32768;
const taps = [_]i16{ 3, -18, 70, 200, 70, -18, 3 };
const half = taps.len / 2;

var samples: [N]i16 = undefined;
var filtered: [N]i16 = undefined;
var pixels: [N]u8 = undefined;

fn filter() void {
    var i: usize = half;
    while (i < N - half) : (i += 1) {
        var acc: i16 = 0;
        // Saturation is not associative, so the order the taps are summed in
        // is part of the answer and nothing may reassociate it.
        inline for (taps, 0..) |t, j| acc +|= (samples[i + j - half] >> 3) *| t;
        filtered[i] = acc;
    }
}

fn mix(gain: i16) void {
    for (&samples, &filtered) |*s, f| s.* = (s.* -| (s.* >> 2)) +| ((f >> 4) *| gain);
}

fn tone(bright: u8, dark: u8, shift: u3) void {
    for (&pixels) |*p| p.* = ((p.* +| bright) -| dark) <<| shift;
}

pub fn main() void {
    var x: u32 = 555;
    for (&samples, &pixels) |*s, *p| {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        s.* = @bitCast(@as(u16, @truncate(x)));
        p.* = @truncate(x >> 16);
    }
    @memset(&filtered, 0);
    var total: u64 = 0;
    var r: u32 = 0;
    while (r < 400) : (r += 1) {
        filter();
        mix(@intCast(1 + r % 5));
        tone(@truncate(r *% 37), @truncate(r *% 91 + 60), @truncate(r % 3));
        samples[(r *% 2654435761) % N] = @bitCast(@as(u16, @truncate(r *% 40503)));
        pixels[(r *% 40503) % N] = @truncate(r);
        for (&filtered) |f| total +%= @as(u16, @bitCast(f));
        for (&pixels) |p| total +%= p;
    }
    w.print("total = ", total);
}
