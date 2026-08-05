// Packed structs: fields at bit offsets, which Zig lays out itself rather than
// leaving to the back end.  What reaches warmc is a run of shifts and masks
// against one loaded word, and whether they fuse is the question.
const w = @import("wasi");

const Cell = packed struct {
    kind: u3,
    flags: u5,
    weight: u12,
    tag: u12,
};

var cells: [8192]Cell = undefined;

pub fn main() void {
    var seed: u32 = 12345;
    for (&cells) |*c| {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        c.* = .{
            .kind = @truncate(seed),
            .flags = @truncate(seed >> 3),
            .weight = @truncate(seed >> 8),
            .tag = @truncate(seed >> 20),
        };
    }
    var total: u64 = 0;
    var r: u32 = 0;
    while (r < 24000) : (r += 1) {
        for (&cells) |*c| {
            if (c.kind > 3) c.flags ^= 1;
            total +%= @as(u64, c.weight) *% (c.kind + 1) +% c.tag;
            c.weight +%= @truncate(r);
        }
    }
    w.print("total = ", total);
}
