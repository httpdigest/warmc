// u128 and i128: a Miller-Rabin primality test over 64-bit odd numbers, whose
// modular multiply is a 64x64->128 product reduced by a 128-bit remainder, and
// a signed dot product of i64 pairs summed in an i128.
//
// wasm has no 128-bit type and, without the wide-arithmetic proposal, no
// high-half multiply, so LLVM hands both operations to compiler-rt, which Zig
// compiles into the module: every `mulmod` is a call to `__multi3` and then
// one to `__umodti3`, a long division of its own, and every term of the dot
// product is a `__multi3` on sign-extended halves.  An i128 does not fit a
// wasm value, so each of those returns its result through a 16-byte stack
// slot the caller passes and then reloads, and the i128 sum is two i64 adds
// with the carry recovered by a compare.  Calls to small leaf functions that
// return through memory, in a hot loop, are a different thing to optimise
// from anything else in the corpus.
const w = @import("wasi");

fn mulmod(a: u64, b: u64, m: u64) u64 {
    return @intCast((@as(u128, a) * b) % m);
}

fn powmod(base: u64, exp: u64, m: u64) u64 {
    var result: u64 = 1;
    var b = base % m;
    var e = exp;
    while (e != 0) : (e >>= 1) {
        if (e & 1 == 1) result = mulmod(result, b, m);
        b = mulmod(b, b, m);
    }
    return result;
}

/// Deterministic for every n < 2^64 with these seven bases.
fn isPrime(n: u64) bool {
    if (n < 2) return false;
    for ([_]u64{ 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37 }) |p| {
        if (n % p == 0) return n == p;
    }
    var d = n - 1;
    var s: u32 = 0;
    while (d & 1 == 0) : (d >>= 1) s += 1;
    for ([_]u64{ 2, 325, 9375, 28178, 450775, 9780504, 1795265022 }) |a| {
        var x = powmod(a, d, n);
        if (x == 0 or x == 1 or x == n - 1) continue;
        var r: u32 = 1;
        while (r < s) : (r += 1) {
            x = mulmod(x, x, n);
            if (x == n - 1) break;
        } else return false;
    }
    return true;
}

var va: [4096]i64 = undefined;
var vb: [4096]i64 = undefined;

pub fn main() void {
    var x: u64 = 0x9e3779b97f4a7c15;
    var primes: u64 = 0;
    var sum: u64 = 0;
    var i: u32 = 0;
    while (i < 70000) : (i += 1) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        const n = x | 1;
        if (isPrime(n)) {
            primes += 1;
            sum +%= n;
        }
    }

    for (&va, &vb) |*a, *b| {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        a.* = @bitCast(x);
        b.* = @bitCast(x *% 0x2545f4914f6cdd1d);
    }
    var dot: i128 = 0;
    var r: u32 = 0;
    while (r < 2000) : (r += 1) {
        for (&va, &vb) |a, b| dot +%= @as(i128, a) * b;
        va[r % va.len] +%= 1;
    }
    const lo: u64 = @truncate(@as(u128, @bitCast(dot)));
    const hi: u64 = @truncate(@as(u128, @bitCast(dot)) >> 64);
    w.print("total = ", primes *% 1000003 +% sum +% lo +% hi *% 3);
}
