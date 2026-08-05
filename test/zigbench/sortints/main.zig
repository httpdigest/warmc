// Quicksort over a fixed array, the same shape gobench, asbench and rsbench all
// have -- there so this producer can be compared with those on one workload
// rather than only on the ones that are new here.
const w = @import("wasi");

var a: [4096]u32 = undefined;

fn sort(lo: usize, hi: usize) void {
    if (hi <= lo + 1) return;
    const pivot = a[(lo + hi) / 2];
    var i = lo;
    var j = hi - 1;
    while (i <= j) {
        while (a[i] < pivot) i += 1;
        while (a[j] > pivot) j -= 1;
        if (i > j) break;
        const t = a[i];
        a[i] = a[j];
        a[j] = t;
        i += 1;
        if (j == 0) break;
        j -= 1;
    }
    sort(lo, j + 1);
    sort(i, hi);
}

pub fn main() void {
    var total: u64 = 0;
    var r: u32 = 0;
    while (r < 1100) : (r += 1) {
        var x: u32 = r *% 2654435761 +% 1;
        for (&a) |*slot| {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            slot.* = x;
        }
        sort(0, a.len);
        total +%= a[0] +% a[a.len / 2] +% a[a.len - 1];
    }
    w.print("total = ", total);
}
