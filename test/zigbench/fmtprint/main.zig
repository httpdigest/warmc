// std's formatter writing into a stack buffer: `std.mem.print`, which is what
// `std.fmt.bufPrint` became (0.17 keeps the old name only as a deprecated
// alias), with decimal, padded hex, binary, signed, string and fixed-precision
// float conversions in one format string.
//
// The format string is parsed at compile time, so no interpreter of it reaches
// wasm: main is the six conversions in sequence, inlined, with out-of-line
// calls to `Io.Writer.writeAll` and `Io.Writer.splatByteAll` for the copies
// and the padding, which end in `memory.copy` and `memory.fill`.  The writer's
// `drain` is a `call_indirect` through its vtable, on the path taken only when
// the 256-byte buffer would overflow, which no line here comes near.  The f64
// conversion brings 128-bit arithmetic with it: compiler-rt's `__multi3`,
// `__udivti3` and `__umodti3`, compiled into the module.  Formatting numbers
// into text is a large share of what real programs spend outside their inner
// loops, and nothing else in the corpus produces digits other than the final
// print.
const std = @import("std");
const w = @import("wasi");

const names = [_][]const u8{ "alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta" };

pub fn main() void {
    var buf: [256]u8 = undefined;
    var total: u64 = 0;
    var bytes: u64 = 0;
    var x: u32 = 4242;
    var i: u32 = 0;
    while (i < 650000) : (i += 1) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        const neg: i32 = @as(i32, @bitCast(x)) >> 7;
        const f: f64 = @as(f64, @floatFromInt(x & 0xfffff)) / 1024.0;
        const line = std.mem.print(&buf, "#{d:>7} {s}: hex={x:0>8} bits={b:0>12} neg={d} f={d:.3}\n", .{
            i,
            names[x % names.len],
            x,
            (x >> 20) & 0xfff,
            neg,
            f,
        }) catch {
            w.print("no space at line ", i);
            return;
        };
        bytes += line.len;
        var h: u64 = 0xcbf29ce484222325;
        for (line) |c| h = (h ^ c) *% 0x100000001b3;
        total +%= h;
    }
    w.print("total = ", total +% bytes);
}
