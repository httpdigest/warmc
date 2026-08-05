// Byte scanning over a slice: bounds checks Zig inserts itself, a loop the
// vectoriser should reach, and a branch per byte that it should not.
const w = @import("wasi");

var buf: [65536]u8 = undefined;

pub fn main() void {
    var seed: u32 = 99;
    for (&buf) |*b| {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        b.* = @truncate(seed);
    }
    var spaces: u64 = 0;
    var high: u64 = 0;
    var sum: u64 = 0;
    var r: u32 = 0;
    while (r < 6300) : (r += 1) {
        for (buf) |b| {
            sum +%= b;
            if (b == ' ') spaces += 1;
            if (b >= 0x80) high += 1;
        }
        buf[r % buf.len] +%= 1;
    }
    w.print("total = ", sum +% spaces *% 3 +% high);
}
