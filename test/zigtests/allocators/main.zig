// std's allocators and the Allocator interface, used the way programs use
// them: FixedBufferAllocator over a static buffer, ArenaAllocator on top of
// it and on top of the wasm allocator, std.heap.wasm_allocator and
// page_allocator directly, BufferFirstAllocator (a stack buffer first, the
// heap after), MemoryPool, and a counting allocator of our own that wraps
// another through the vtable and logs every call it forwards.
//
// The FixedBufferAllocator's offsets are printed relative to its buffer, so
// its bump pointer, its alignment padding, its in-place resize of the last
// allocation and its free of the last allocation are all visible; it is
// allocated from until OutOfMemory and then reset.  Heap pointers are never
// printed, only their remainder modulo the alignment that was asked for
// (always 0) and the contents written through them.  Covered: alloc, free,
// create/destroy, alignedAlloc and alignedCreate up to 4096-byte alignment,
// resize (in place, true or false), remap, realloc growing and shrinking with
// the contents kept, dupe, dupeSentinel, allocSentinel, Allocator.print, the
// arena's reset with free_all, retain_capacity and retain_with_limit, and
// `memory.size` before and after the heap grows.  Every call into an
// allocator is a `call_indirect` through its vtable unless LLVM can see
// through it, and the BrkAllocator under wasm_allocator calls `memory.grow`.
const std = @import("std");
const w = @import("wasi");
const Allocator = std.mem.Allocator;
const Alignment = std.mem.Alignment;

var backing: [4096]u8 align(64) = undefined;
var arena_backing: [64 * 1024]u8 = undefined;

fn off(p: anytype) usize {
    return @intFromPtr(p) - @intFromPtr(&backing);
}

fn fixedBuffer() void {
    var fba = std.heap.FixedBufferAllocator.init(&backing);
    const a = fba.allocator();
    const b1 = a.alloc(u8, w.hide(@as(usize, 10))) catch unreachable;
    w.printf("fba alloc u8x10 at {d}, end_index={d}\n", .{ off(b1.ptr), fba.end_index });
    const b2 = a.alloc(u64, w.hide(@as(usize, 3))) catch unreachable;
    w.printf("fba alloc u64x3 at {d} (aligned to 8: {}), end_index={d}\n", .{ off(b2.ptr), off(b2.ptr) % 8 == 0, fba.end_index });
    const b3 = a.alignedAlloc(u8, .@"64", 5) catch unreachable;
    w.printf("fba alignedAlloc 64 at {d}, end_index={d}\n", .{ off(b3.ptr), fba.end_index });
    w.printf("isLastAllocation b3={} b2={}\n", .{ fba.isLastAllocation(b3), fba.isLastAllocation(std.mem.sliceAsBytes(b2)) });
    w.printf("resize last 5->40: {} end_index={d}\n", .{ a.resize(b3, 40), fba.end_index });
    w.printf("resize not-last u8x10->20: {}\n", .{a.resize(b1, 20)});
    w.printf("resize not-last u8x10->4 (shrink): {}\n", .{a.resize(b1, 4)});
    const b3b = b3.ptr[0..40];
    a.free(b3b);
    w.printf("free last: end_index={d}\n", .{fba.end_index});
    const p = a.create(u32) catch unreachable;
    p.* = 0xabcd;
    w.printf("create u32 at {d}, value {x}\n", .{ off(p), p.* });
    a.destroy(p);
    w.printf("destroy last: end_index={d}\n", .{fba.end_index});
    // Fill to exhaustion with growing requests.
    var n: usize = 1;
    var count: usize = 0;
    while (true) : (n = n * 2 + 1) {
        const s = a.alloc(u16, n) catch {
            w.printf("OutOfMemory at request u16x{d}, end_index={d}, allocations={d}\n", .{ n, fba.end_index, count });
            break;
        };
        @memset(s, @truncate(n));
        count += 1;
        w.printf("  alloc u16x{d} at {d}\n", .{ n, off(s.ptr) });
    }
    w.printf("ownsPtr(backing+100)={} ownsSlice(b1)={}\n", .{ fba.ownsPtr(@ptrCast(&backing[100])), fba.ownsSlice(b1) });
    fba.reset();
    w.printf("after reset end_index={d}\n", .{fba.end_index});
    const again = a.alloc(u8, 4000) catch unreachable;
    w.printf("after reset alloc 4000 at {d}\n", .{off(again.ptr)});
    const fail = a.alloc(u8, 200);
    w.printf("then alloc 200 fails: {}\n", .{if (fail) |_| false else |_| true});
    // remap on the last allocation grows in place.
    fba.reset();
    var r = a.alloc(u32, 4) catch unreachable;
    for (r, 0..) |*v, i| v.* = @intCast(i * 11);
    if (a.remap(r, 12)) |grown| {
        r = grown;
        w.printf("remap 4->12 in place at {d}, kept {d},{d},{d},{d}\n", .{ off(r.ptr), r[0], r[1], r[2], r[3] });
    } else w.puts("remap failed\n");
    r = a.realloc(r, 2) catch unreachable;
    w.printf("realloc shrink -> len {d} [{d},{d}] end_index={d}\n", .{ r.len, r[0], r[1], fba.end_index });
}

fn arena(child: Allocator, label: []const u8) !void {
    var ar = std.heap.ArenaAllocator.init(child);
    defer ar.deinit();
    const a = ar.allocator();
    var round: u32 = 0;
    while (round < 4) : (round += 1) {
        var total: u64 = 0;
        var i: u32 = 0;
        var keep: []u32 = &.{};
        while (i < 50 + round * 40) : (i += 1) {
            const len = 1 + (i * 7 + round) % 23;
            const s = try a.alloc(u32, len);
            for (s, 0..) |*v, j| v.* = @intCast(i * 100 + j);
            if (i % 9 == 0) {
                // Grow the most recent allocation: the arena extends it.
                const g = try a.realloc(s, len * 3);
                for (g[len..], 0..) |*v, j| v.* = @intCast(j + round);
                keep = g;
            }
            for (s) |v| total += v;
        }
        var ksum: u64 = 0;
        for (keep) |v| ksum += v;
        const cap = ar.queryCapacity();
        w.printf("{s} round {d}: total={d} keep.len={d} ksum={d} capacity>0={}\n", .{ label, round, total, keep.len, ksum, cap > 0 });
        const mode: std.heap.ArenaAllocator.ResetMode = switch (round % 3) {
            0 => .free_all,
            1 => .retain_capacity,
            else => .{ .retain_with_limit = 256 },
        };
        const ok = ar.reset(mode);
        w.printf("  reset {s} -> {} capacity after: zero={} <=limit={}\n", .{ @tagName(mode), ok, ar.queryCapacity() == 0, ar.queryCapacity() <= 256 });
    }
    // Strings in the arena.
    const s1 = try a.dupe(u8, "arena string");
    const s2 = try a.dupeSentinel(u8, "zero terminated", 0);
    const s3 = try a.print("{s}/{d}/{x}", .{ label, 1234, 0xbeef });
    const s4 = try a.allocSentinel(u16, 3, 0xffff);
    s4[0] = 1;
    s4[1] = 2;
    s4[2] = 3;
    w.printf("dupe={s} dupeSentinel={s} (sentinel {d}, len {d}) print={s} allocSentinel={d},{d},{d} sentinel={x}\n", .{ s1, s2, s2[s2.len], s2.len, s3, s4[0], s4[1], s4[2], s4[s4.len] });
}

fn heapAllocs(a: Allocator, label: []const u8) !void {
    // Alignment of every request, up to a page.
    inline for (.{ Alignment.@"1", Alignment.@"8", Alignment.@"16", Alignment.@"64", @as(Alignment, @enumFromInt(8)), @as(Alignment, @enumFromInt(12)) }) |al| {
        const s = try a.alignedAlloc(u8, al, w.hide(@as(usize, 100)));
        defer a.free(s);
        @memset(s, 0x5a);
        var sum: u32 = 0;
        for (s) |b| sum += b;
        w.printf("{s} alignedAlloc align={d} rem={d} sum={d}\n", .{ label, al.toByteUnits(), @intFromPtr(s.ptr) % al.toByteUnits(), sum });
    }
    const Big = struct { a: u64, b: [3]u32, c: u8 };
    const pb = try a.create(Big);
    pb.* = .{ .a = 0x1122334455667788, .b = .{ 1, 2, 3 }, .c = 9 };
    w.printf("{s} create Big rem8={d} a={x} b2={d} c={d}\n", .{ label, @intFromPtr(pb) % 8, pb.a, pb.b[2], pb.c });
    a.destroy(pb);
    const pa = try a.alignedCreate(u32, comptime .fromByteUnits(256));
    pa.* = 77;
    w.printf("{s} alignedCreate 256 rem={d} value={d}\n", .{ label, @intFromPtr(pa) % 256, pa.* });
    a.destroy(pa);
    // realloc chain: grow and shrink, the contents must survive every move.
    var s = try a.alloc(u32, 1);
    s[0] = 1;
    const lens = [_]usize{ 2, 5, 17, 64, 1000, 3, 300, 5000, 1, 70 };
    for (lens) |nl0| {
        const nl = w.hide(nl0);
        const old = s.len;
        s = try a.realloc(s, nl);
        var ok = true;
        for (s[0..@min(old, nl)], 0..) |v, i| {
            if (v != i * i + 1) ok = false;
        }
        for (s, 0..) |*v, i| v.* = @intCast(i * i + 1);
        w.printf("{s} realloc {d}->{d} kept={}\n", .{ label, old, nl, ok });
    }
    a.free(s);
    // resize in place: may or may not succeed, but it must not lose data.
    const t = try a.alloc(u8, 64);
    @memset(t, 3);
    const grew = a.resize(t, 32);
    w.printf("{s} resize shrink 64->32 ok={} data={d}\n", .{ label, grew, t[31] });
    a.free(if (grew) t[0..32] else t);
    // Many small blocks freed in an interleaved order, then reused.
    var blocks: [64][]u8 = undefined;
    for (&blocks, 0..) |*b, i| {
        b.* = try a.alloc(u8, 8 + (i % 7) * 24);
        @memset(b.*, @intCast(i));
    }
    var i: usize = 0;
    while (i < 64) : (i += 2) a.free(blocks[i]);
    i = 0;
    while (i < 64) : (i += 2) {
        blocks[i] = try a.alloc(u8, 8 + (i % 7) * 24);
        @memset(blocks[i], @intCast(i + 100));
    }
    var sum: u64 = 0;
    for (blocks) |b| {
        for (b) |v| sum += v;
    }
    w.printf("{s} 64 blocks reused, byte sum={d}\n", .{ label, sum });
    for (blocks) |b| a.free(b);
}

/// Forwards to a child allocator and logs each call: sizes, alignments and
/// results only.
const Counting = struct {
    child: Allocator,
    allocs: u32 = 0,
    resizes: u32 = 0,
    remaps: u32 = 0,
    frees: u32 = 0,
    live: usize = 0,
    quiet: bool = false,

    fn allocator(self: *Counting) Allocator {
        return .{ .ptr = self, .vtable = &.{ .alloc = alloc, .resize = resize, .remap = remap, .free = free } };
    }
    fn alloc(ctx: *anyopaque, len: usize, al: Alignment, ra: usize) ?[*]u8 {
        const self: *Counting = @ptrCast(@alignCast(ctx));
        const r = self.child.rawAlloc(len, al, ra);
        self.allocs += 1;
        if (r != null) self.live += len;
        if (!self.quiet) w.printf("    [alloc len={d} align={d} ok={}]\n", .{ len, al.toByteUnits(), r != null });
        return r;
    }
    fn resize(ctx: *anyopaque, mem: []u8, al: Alignment, new_len: usize, ra: usize) bool {
        const self: *Counting = @ptrCast(@alignCast(ctx));
        const ok = self.child.rawResize(mem, al, new_len, ra);
        self.resizes += 1;
        if (ok) self.live = self.live - mem.len + new_len;
        if (!self.quiet) w.printf("    [resize {d}->{d} align={d} ok={}]\n", .{ mem.len, new_len, al.toByteUnits(), ok });
        return ok;
    }
    fn remap(ctx: *anyopaque, mem: []u8, al: Alignment, new_len: usize, ra: usize) ?[*]u8 {
        const self: *Counting = @ptrCast(@alignCast(ctx));
        const r = self.child.rawRemap(mem, al, new_len, ra);
        self.remaps += 1;
        if (r != null) self.live = self.live - mem.len + new_len;
        if (!self.quiet) w.printf("    [remap {d}->{d} align={d} ok={}]\n", .{ mem.len, new_len, al.toByteUnits(), r != null });
        return r;
    }
    fn free(ctx: *anyopaque, mem: []u8, al: Alignment, ra: usize) void {
        const self: *Counting = @ptrCast(@alignCast(ctx));
        self.child.rawFree(mem, al, ra);
        self.frees += 1;
        self.live -= mem.len;
        if (!self.quiet) w.printf("    [free len={d} align={d}]\n", .{ mem.len, al.toByteUnits() });
    }
};

fn counting() !void {
    var fba = std.heap.FixedBufferAllocator.init(&backing);
    var c = Counting{ .child = fba.allocator() };
    const a = c.allocator();
    w.puts("counting over fba: ArrayList growth\n");
    var list: std.ArrayList(u64) = .empty;
    var i: u64 = 0;
    while (i < 40) : (i += 1) try list.append(a, i * i);
    var s: u64 = 0;
    for (list.items) |v| s += v;
    w.printf("  list sum={d} live={d}\n", .{ s, c.live });
    list.deinit(a);
    w.printf("  after deinit live={d} allocs={d} resizes={d} remaps={d} frees={d}\n", .{ c.live, c.allocs, c.resizes, c.remaps, c.frees });

    w.puts("counting under arena over fba:\n");
    fba.reset();
    c = .{ .child = fba.allocator() };
    var ar = std.heap.ArenaAllocator.init(c.allocator());
    const aa = ar.allocator();
    var k: usize = 0;
    while (k < 12) : (k += 1) {
        const m = try aa.alloc(u8, 30 + k * 10);
        @memset(m, 1);
    }
    w.printf("  arena live={d} allocs={d} resizes={d} remaps={d}\n", .{ c.live, c.allocs, c.resizes, c.remaps });
    ar.deinit();
    w.printf("  after arena deinit live={d} frees={d}\n", .{ c.live, c.frees });

    w.puts("counting over wasm_allocator, hash map growth (quiet):\n");
    var c2 = Counting{ .child = std.heap.wasm_allocator, .quiet = true };
    var map = std.AutoHashMap(u32, u32).init(c2.allocator());
    var j: u32 = 0;
    while (j < 500) : (j += 1) try map.put(j *% 2654435761, j);
    w.printf("  map count={d} allocs={d} frees={d} live>0={}\n", .{ map.count(), c2.allocs, c2.frees, c2.live > 0 });
    map.deinit();
    w.printf("  after deinit live={d} allocs={d} frees={d}\n", .{ c2.live, c2.allocs, c2.frees });
}

fn bufferFirst() !void {
    var stack: [256]u8 = undefined;
    var bf = std.heap.BufferFirstAllocator.init(&stack, std.heap.wasm_allocator);
    const a = bf.allocator();
    var parts: [10][]u8 = undefined;
    for (&parts, 0..) |*p, i| {
        p.* = try a.alloc(u8, 20 + i * 15);
        @memset(p.*, @intCast('a' + i));
        const in_stack = @intFromPtr(p.ptr) >= @intFromPtr(&stack) and @intFromPtr(p.ptr) < @intFromPtr(&stack) + stack.len;
        w.printf("bufferFirst alloc {d}: in stack buffer={}\n", .{ p.len, in_stack });
    }
    var tot: u64 = 0;
    for (parts) |p| {
        for (p) |b| tot += b;
    }
    w.printf("bufferFirst bytes sum={d}\n", .{tot});
    var idx: usize = parts.len;
    while (idx > 0) {
        idx -= 1;
        a.free(parts[idx]);
    }
}

fn pool() !void {
    const Node = struct { val: u64, next: ?*@This() };
    var p: std.heap.MemoryPool(Node) = .empty;
    defer p.deinit(std.heap.wasm_allocator);
    var head: ?*Node = null;
    var i: u64 = 0;
    while (i < 100) : (i += 1) {
        const n = try p.create(std.heap.wasm_allocator);
        n.* = .{ .val = i * 3, .next = head };
        head = n;
        if (i % 4 == 3) {
            // Pop and destroy one: its slot is reused by the next create.
            const top = head.?;
            head = top.next;
            p.destroy(top);
        }
    }
    var sum: u64 = 0;
    var len: u32 = 0;
    var it = head;
    while (it) |n| : (it = n.next) {
        sum += n.val;
        len += 1;
    }
    w.printf("pool list len={d} sum={d}\n", .{ len, sum });
}

pub fn main() void {
    fixedBuffer();
    var fba = std.heap.FixedBufferAllocator.init(&arena_backing);
    arena(fba.allocator(), "arena/fba") catch w.puts("arena/fba: out of memory\n");
    arena(std.heap.wasm_allocator, "arena/wasm") catch w.puts("arena/wasm: out of memory\n");
    const before = @wasmMemorySize(0);
    heapAllocs(std.heap.wasm_allocator, "wasm") catch w.puts("wasm: out of memory\n");
    heapAllocs(std.heap.page_allocator, "page") catch w.puts("page: out of memory\n");
    w.printf("memory grew: {}\n", .{@wasmMemorySize(0) > before});
    counting() catch w.puts("counting: out of memory\n");
    bufferFirst() catch w.puts("bufferFirst: out of memory\n");
    pool() catch w.puts("pool: out of memory\n");
    w.puts("done\n");
}
