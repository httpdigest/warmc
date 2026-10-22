// std's sorts over records with a comparator of our own: `std.mem.sortUnstable`
// (pattern-defeating quicksort) on a three-key order whose last key compares
// byte strings with `std.mem.order`, then `std.mem.sort` (a stable block sort)
// on a coarser key, which must keep the first sort's order within each group.
//
// The comparators are comptime parameters, so each sort is monomorphised
// around its own.  pdq is inlined into main with the comparator inlined into
// its partition loops; only the median-of-three helper calls it out of line,
// and there the string compare is unrolled into six byte compares, each
// dispatching on a three-way order with a `br_table`.  The block sort is one
// large function of its own with the band comparator inlined throughout -- 78
// `i32.div_s` in it, because the band width is a run-time value -- and record
// moves as `memory.copy`; its merge helper ends in a `return_call`, which the
// corpus's tail-call build makes of a sibling call.  sortints is a
// hand-written quicksort over bare integers; this is the sort Zig programs
// actually call, over 16-byte records with a compare of several branches.
const std = @import("std");
const w = @import("wasi");

const Rec = struct {
    dept: u8,
    score: i32,
    id: u32,
    name: [6]u8,
};

const N = 4096;
var recs: [N]Rec = undefined;

fn byDeptScoreName(_: void, a: Rec, b: Rec) bool {
    if (a.dept != b.dept) return a.dept < b.dept;
    if (a.score != b.score) return a.score > b.score;
    return std.mem.order(u8, &a.name, &b.name) == .lt;
}

fn byBand(band: i32, a: Rec, b: Rec) bool {
    return @divFloor(a.score, band) < @divFloor(b.score, band);
}

fn positional(rs: []const Rec) u64 {
    var h: u64 = 0;
    for (rs) |r| h = (h ^ r.id) *% 0x100000001b3;
    return h;
}

pub fn main() void {
    var total: u64 = 0;
    var x: u32 = 77;
    var r: u32 = 0;
    while (r < 280) : (r += 1) {
        for (&recs, 0..) |*rec, i| {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            rec.* = .{
                .dept = @truncate(x % 12),
                .score = @as(i32, @intCast((x >> 4) % 2000)) - 1000,
                .id = @intCast(i),
                .name = undefined,
            };
            // A small alphabet, so equal prefixes are common and the string
            // compare has to look past the first byte.
            for (&rec.name, 0..) |*c, k| c.* = 'a' + @as(u8, @truncate((x >> @intCast(3 * k + 8)) & 3));
        }
        std.mem.sortUnstable(Rec, &recs, {}, byDeptScoreName);
        total +%= positional(&recs);
        std.mem.sort(Rec, &recs, @as(i32, 250 + @as(i32, @intCast(r % 7))), byBand);
        total +%= positional(&recs) *% 3;
    }
    w.print("total = ", total);
}
