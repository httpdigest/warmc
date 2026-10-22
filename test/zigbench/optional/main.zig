// Optionals: a binary search tree whose links are `?*Node`, lookups that
// return `?u32`, and a memo table of `?Entry` slots, consumed with `orelse`,
// `if (x) |v|` and `while (x) |v|`.
//
// Zig gives the three different representations.  `?*Node` is a plain pointer
// with null as 0, so `while (cur) |n|` is a `br_if` on the pointer itself and
// the step to a child is a `select` between field offsets 8 and 12.  `?u32`
// does not fit one wasm value with its flag: `find` and `floor` (kept out of
// line) return it through a stack slot the caller passes, the value at offset 0
// and a flag byte at offset 4.  `?Entry` is the struct plus a flag byte at
// offset 8, twelve bytes a slot.  On the caller's side a returned flag becomes
// data rather than control: `if (find(..)) |v|` is counted with `select`s on
// the reloaded byte, and `orelse 0xdead` is one more, while the memo slot's
// flag stays a branch.  A null check that is a store, a reload and a select is
// worth having beside the corpus's hand-written hashmap, whose misses are a
// branch on a bool array.
const w = @import("wasi");

const Node = struct {
    key: u32,
    val: u32,
    left: ?*Node = null,
    right: ?*Node = null,
};

const POOL = 3000;
var pool: [POOL]Node = undefined;
var used: usize = 0;
const Entry = struct { key: u32, val: u32 };
var memo: [1024]?Entry = undefined;

fn insert(root: *?*Node, key: u32, val: u32) void {
    var link = root;
    while (link.*) |n| {
        if (key == n.key) {
            n.val +%= val;
            return;
        }
        link = if (key < n.key) &n.left else &n.right;
    }
    pool[used] = .{ .key = key, .val = val };
    link.* = &pool[used];
    used += 1;
}

noinline fn find(root: ?*Node, key: u32) ?u32 {
    var cur = root;
    while (cur) |n| {
        if (key == n.key) return n.val;
        cur = if (key < n.key) n.left else n.right;
    }
    return null;
}

/// The value at the greatest key not above `key`, if there is one.
noinline fn floor(root: ?*Node, key: u32) ?u32 {
    var best: ?u32 = null;
    var cur = root;
    while (cur) |n| {
        if (n.key == key) return n.val;
        if (n.key < key) {
            best = n.val;
            cur = n.right;
        } else cur = n.left;
    }
    return best;
}

fn minKey(root: ?*Node) ?u32 {
    var n = root orelse return null;
    while (n.left) |l| n = l;
    return n.key;
}

pub fn main() void {
    var hits: u64 = 0;
    var misses: u64 = 0;
    var total: u64 = 0;
    var r: u32 = 0;
    while (r < 140) : (r += 1) {
        used = 0;
        var root: ?*Node = null;
        @memset(&memo, null);
        var x: u32 = r *% 2654435761 +% 7;
        var i: u32 = 0;
        while (i < POOL) : (i += 1) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            insert(&root, x & 0xfffff, i);
        }
        var y: u32 = undefined;
        i = 0;
        while (i < 6000) : (i += 1) {
            if (i % POOL == 0) y = r *% 2654435761 +% 7;
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            y ^= y << 13;
            y ^= y >> 17;
            y ^= y << 5;
            // Half the probes replay an inserted key (`y` walks the insertion
            // sequence again) and half are fresh, so which way `if` goes is a
            // coin toss.
            const k = (if (x & 1 == 0) y else x) & 0xfffff;
            if (find(root, k)) |v| {
                hits += 1;
                total +%= v;
            } else misses += 1;
            const q = ((x >> 9) & 0xfff) << 8;
            const slot = &memo[(q >> 8) & 1023];
            const cached: ?u32 = if (slot.*) |e| (if (e.key == q) e.val else null) else null;
            const f = cached orelse blk: {
                const v = floor(root, q) orelse 0xdead;
                slot.* = .{ .key = q, .val = v };
                break :blk v;
            };
            total +%= f ^ (q >> 3);
        }
        total +%= minKey(root) orelse 0;
    }
    w.print("total = ", total *% 3 +% hits *% 1000003 +% misses);
}
