// Mutual tail recursion, `noinline` so that neither end can be folded into the
// other and turned into a loop -- which is what LLVM does to a self-recursive
// tail call, and why the obvious version of this kernel emitted no tail call at
// all.  What comes out is `return_call`, the *direct* form, which warmc lowers
// differently from the indirect one interp exercises: the target is known, so
// the branch is direct and only the frame teardown is shared.
const w = @import("wasi");

noinline fn even(n: u32, acc: u64) u64 {
    if (n == 0) return acc;
    return @call(.always_tail, odd, .{ n - 1, acc +% (@as(u64, n) *% 2654435761) });
}

noinline fn odd(n: u32, acc: u64) u64 {
    if (n == 0) return acc ^ 0x5a5a5a5a;
    return @call(.always_tail, even, .{ n - 1, (acc << 1) | (acc >> 63) });
}

pub fn main() void {
    var total: u64 = 0;
    var r: u32 = 0;
    while (r < 800) : (r += 1) total +%= even(250000 + r, r);
    w.print("total = ", total);
}
