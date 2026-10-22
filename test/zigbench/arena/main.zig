// std.heap.ArenaAllocator stacked on a FixedBufferAllocator over a static
// buffer: every round builds a linked list of records, each with a variable
// length slice that is sometimes grown with `realloc` and sometimes freed, sums
// it, and throws the whole arena away.
//
// Two layers of `std.mem.Allocator` vtables, resolved differently.  The
// arena's own is seen through: `create` and `alloc` are direct calls to
// `ArenaAllocator.alloc`, and its resize and free are inlined into the caller.
// The FixedBufferAllocator underneath is reached only by `call_indirect`
// through the child's vtable.  And it is reached on nearly every allocation:
// once the arena's first chunk is full it grows that chunk in place by exactly
// the request (counted with a wrapping allocator: 1000 small allocations made
// one child `alloc` and 994 child `resize` calls).  The arena is written to be
// thread-safe, but this target is single-threaded, so its `@atomicRmw` and
// `@cmpxchgStrong` arrive as plain loads and stores.  What is measured is an
// allocator's fast path with an indirect call in it, which no other kernel in
// the corpus has.
const std = @import("std");
const w = @import("wasi");

const Item = struct {
    id: u32,
    tags: []u16,
    next: ?*Item,
};

var backing: [4 << 20]u8 = undefined;

fn xorshift(x: u32) u32 {
    var y = x;
    y ^= y << 13;
    y ^= y >> 17;
    y ^= y << 5;
    return y;
}

fn build(gpa: std.mem.Allocator, seed: u32, n: u32) !?*Item {
    var head: ?*Item = null;
    var x = seed;
    var i: u32 = 0;
    while (i < n) : (i += 1) {
        x = xorshift(x);
        const it = try gpa.create(Item);
        const len = 1 + x % 24;
        var tags = try gpa.alloc(u16, len);
        for (tags, 0..) |*t, j| t.* = @truncate((x >> @intCast(j & 15)) ^ j);
        switch ((x >> 24) & 7) {
            // The newest allocation, so the arena grows it where it stands.
            0, 1 => {
                tags = try gpa.realloc(tags, len * 2);
                for (tags[len..], 0..) |*t, j| t.* = @truncate(x +% j);
            },
            // Freed and replaced: the arena takes the space back first.
            2 => {
                gpa.free(tags);
                tags = try gpa.alloc(u16, 3);
                tags[0] = @truncate(x);
                tags[1] = @truncate(x >> 16);
                tags[2] = @truncate(i);
            },
            else => {},
        }
        it.* = .{ .id = x, .tags = tags, .next = head };
        head = it;
    }
    return head;
}

fn digest(head: ?*Item) u64 {
    var sum: u64 = 0;
    var cur = head;
    while (cur) |it| : (cur = it.next) {
        var h: u64 = it.id;
        for (it.tags) |t| h = h *% 31 +% t;
        sum +%= h ^ (sum >> 7);
    }
    return sum;
}

pub fn main() void {
    var total: u64 = 0;
    var r: u32 = 0;
    while (r < 120) : (r += 1) {
        var fba = std.heap.FixedBufferAllocator.init(&backing);
        var arena = std.heap.ArenaAllocator.init(fba.allocator());
        const head = build(arena.allocator(), r *% 2654435761 +% 1, 20000) catch {
            w.print("out of memory at round ", r);
            return;
        };
        total +%= digest(head);
        arena.deinit();
    }
    w.print("total = ", total);
}
