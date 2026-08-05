// An open-addressing map written out by hand, inserted into and probed.
//
// The same workload as asbench/mapint and gobench/maphash, but with no runtime
// behind it: no allocator, no growth, no GC -- so what is left is the probe
// itself, which is what makes the three comparable.
const w = @import("wasi");

const CAP = 1 << 14;
var keys: [CAP]u32 = undefined;
var vals: [CAP]u32 = undefined;
var used: [CAP]bool = undefined;

fn hash(k: u32) u32 {
    var h = k *% 0x9e3779b1;
    h ^= h >> 15;
    h *%= 0x85ebca6b;
    h ^= h >> 13;
    return h;
}

fn put(k: u32, v: u32) void {
    var i = hash(k) & (CAP - 1);
    while (used[i] and keys[i] != k) i = (i + 1) & (CAP - 1);
    used[i] = true;
    keys[i] = k;
    vals[i] = v;
}

fn get(k: u32) u32 {
    var i = hash(k) & (CAP - 1);
    while (used[i]) : (i = (i + 1) & (CAP - 1)) if (keys[i] == k) return vals[i];
    return 0;
}

pub fn main() void {
    var total: u64 = 0;
    var r: u32 = 0;
    while (r < 5600) : (r += 1) {
        @memset(&used, false);
        var x: u32 = r *% 2654435761 +% 1;
        var i: u32 = 0;
        while (i < 4000) : (i += 1) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            put(x & 0xffff, i);
        }
        x = r *% 2654435761 +% 1;
        i = 0;
        while (i < 4000) : (i += 1) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            total +%= get(x & 0xffff);
        }
    }
    w.print("total = ", total);
}
