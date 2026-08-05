// Error unions propagated through six layers.
//
// Zig returns an error as part of the value rather than by unwinding, so what
// reaches warmc is a branch on a tag after every call and a merge on the way
// out -- no landing pads, no tags, and no `throw`.  That is a different shape
// from ktbench's real exceptions and from Rust's `?`, which LLVM flattens.
const w = @import("wasi");

const Err = error{ TooBig, Odd };

fn leaf(v: u32) Err!u32 {
    if (v > 0xffff00) return Err.TooBig;
    if (v & 1 == 1) return Err.Odd;
    return v >> 1;
}
fn l1(v: u32) Err!u32 { return (try leaf(v)) +% 3; }
fn l2(v: u32) Err!u32 { return (try l1(v)) ^ 0x5a5a; }
fn l3(v: u32) Err!u32 { return (try l2(v)) *% 7; }
fn l4(v: u32) Err!u32 { return (try l3(v)) -% 11; }
fn l5(v: u32) Err!u32 { return (try l4(v)) | 1; }

pub fn main() void {
    var ok: u64 = 0;
    var toobig: u64 = 0;
    var odd: u64 = 0;
    var x: u32 = 1;
    var i: u32 = 0;
    while (i < 80000000) : (i += 1) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        if (l5(x)) |v| ok +%= v else |e| switch (e) {
            Err.TooBig => toobig += 1,
            Err.Odd => odd += 1,
        }
    }
    w.print("total = ", ok +% toobig *% 31 +% odd *% 17);
}
