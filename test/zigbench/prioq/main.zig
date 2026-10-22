// Dijkstra's shortest paths over a weighted grid, driven by std.PriorityQueue:
// a binary heap in an allocator-grown slice, ordered by a comparator passed as
// a comptime function parameter, with stale entries skipped on pop rather than
// decreased in place.
//
// The whole search collapses into main: push with its sift-up, pop with its
// sift-down, and the comparator, which is a comptime parameter and so is
// inlined at every compare.  The allocator is the one call left, a
// `call_indirect` through the Allocator vtable on the path where the heap
// outgrows its slice.  The four `?u32` neighbours, by contrast, survive as
// real memory: each one is built as an 8-byte value-and-flag pair in a stack
// slot, or pointed at a null constant in the data segment when the cell is on
// an edge, then copied with `i64.load`/`i64.store` into the array the loop
// walks, so each pop stores and reloads four optionals before the first
// neighbour is tested.  A library data structure, a comparator and a
// store-to-load round trip that LLVM did not remove are a program shape
// rather than a loop someone wrote.
const std = @import("std");
const w = @import("wasi");

const SIDE = 160;
const CELLS = SIDE * SIDE;

const Entry = struct { dist: u32, node: u32 };

fn closer(_: void, a: Entry, b: Entry) std.math.Order {
    if (a.dist != b.dist) return std.math.order(a.dist, b.dist);
    return std.math.order(a.node, b.node);
}

const Queue = std.PriorityQueue(Entry, void, closer);

var weight: [CELLS]u8 = undefined;
var dist: [CELLS]u32 = undefined;

fn shortest(gpa: std.mem.Allocator, q: *Queue, src: u32, pops: *u64) !void {
    @memset(&dist, std.math.maxInt(u32));
    q.clearRetainingCapacity();
    dist[src] = 0;
    try q.push(gpa, .{ .dist = 0, .node = src });
    while (q.pop()) |e| {
        pops.* += 1;
        if (e.dist != dist[e.node]) continue;
        const x = e.node % SIDE;
        const y = e.node / SIDE;
        const around = [4]?u32{
            if (x > 0) e.node - 1 else null,
            if (x + 1 < SIDE) e.node + 1 else null,
            if (y > 0) e.node - SIDE else null,
            if (y + 1 < SIDE) e.node + SIDE else null,
        };
        for (around) |maybe| {
            const n = maybe orelse continue;
            const d = e.dist + weight[n];
            if (d < dist[n]) {
                dist[n] = d;
                try q.push(gpa, .{ .dist = d, .node = n });
            }
        }
    }
}

pub fn main() void {
    const gpa = std.heap.wasm_allocator;
    var x: u32 = 4711;
    for (&weight) |*c| {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        c.* = @truncate(1 + x % 9);
    }
    var q = Queue.initContext({});
    defer q.deinit(gpa);
    var total: u64 = 0;
    var pops: u64 = 0;
    var r: u32 = 0;
    while (r < 75) : (r += 1) {
        const src = (r *% 2654435761) % CELLS;
        shortest(gpa, &q, src, &pops) catch {
            w.print("out of memory at round ", r);
            return;
        };
        for (&dist, 0..) |d, i| total +%= @as(u64, d) *% (i | 1);
        // A wall of heavy cells somewhere new each round.
        const row = (r * 37) % SIDE;
        for (weight[row * SIDE ..][0 .. SIDE / 2]) |*c| c.* = 200;
    }
    w.print("total = ", total +% pops);
}
