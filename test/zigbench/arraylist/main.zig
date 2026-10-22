// std.ArrayList grown from empty, one element at a time, on std.heap's
// wasm_allocator, which takes its pages from `memory.grow` and keeps freed
// blocks on a free list per power-of-two size class.  Forty-eight lists of u32
// and one of three-field structs, with inserts at the front, ordered removes
// from the middle, swap removes and clears mixed in.
//
// The append itself is inlined: a capacity compare and a store.  Growing is
// out of line for the u32 lists (`ensureTotalCapacity`) and inlined for the
// struct list, and either way it allocates with a `call_indirect` through the
// Allocator vtable into `BrkAllocator.alloc` and moves the old contents with
// `memory.copy`; the ordered insert and remove are `memory.copy` too, with
// overlapping ranges.  Lists are freed and rebuilt every round, and after the
// first one the allocator serves them all from its free lists: the memory is
// 281 pages after round one and still 281 at the end.  This is the most
// common container in Zig programs, and the one kernel here whose time is
// split between a tight inlined fast path and bulk moves of every size.
const std = @import("std");
const w = @import("wasi");

const gpa = std.heap.wasm_allocator;

const Point = struct { x: i32, y: i32, weight: u16 };

const LISTS = 48;

fn xorshift(x: u32) u32 {
    var y = x;
    y ^= y << 13;
    y ^= y >> 17;
    y ^= y << 5;
    return y;
}

fn round(seed: u32) !u64 {
    var ints: [LISTS]std.ArrayList(u32) = @splat(.empty);
    var points: std.ArrayList(Point) = .empty;
    defer {
        for (&ints) |*l| l.deinit(gpa);
        points.deinit(gpa);
    }
    var x = seed;
    var i: u32 = 0;
    while (i < 30000) : (i += 1) {
        x = xorshift(x);
        const l = &ints[x % LISTS];
        switch ((x >> 20) & 31) {
            // Into the front: everything behind it moves up.
            0 => try l.insert(gpa, 0, x),
            // Out of the middle, keeping the order: everything behind it moves
            // down.
            1 => if (l.items.len > 0) {
                _ = l.orderedRemove(l.items.len / 2);
            },
            2 => if (l.items.len > 0) {
                _ = l.swapRemove(x % l.items.len);
            },
            3 => l.clearRetainingCapacity(),
            4 => try l.appendSlice(gpa, &.{ x, x >> 1, x >> 2, x >> 3 }),
            else => try l.append(gpa, x),
        }
        try points.append(gpa, .{
            .x = @bitCast(x),
            .y = @bitCast(x *% 7),
            .weight = @truncate(i),
        });
    }
    var sum: u64 = 0;
    for (&ints, 0..) |l, k| {
        sum +%= l.items.len *% (k + 1);
        for (l.items) |v| sum = sum *% 31 +% v;
    }
    for (points.items) |p| sum +%= @as(u64, @bitCast(@as(i64, p.x) - p.y)) ^ p.weight;
    return sum;
}

pub fn main() void {
    var total: u64 = 0;
    var r: u32 = 0;
    while (r < 450) : (r += 1) {
        total +%= round(r *% 2654435761 +% 3) catch {
            w.print("out of memory at round ", r);
            return;
        };
    }
    w.print("total = ", total);
}
