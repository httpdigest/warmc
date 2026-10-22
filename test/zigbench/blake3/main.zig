// BLAKE3 from std.crypto over a 64 KiB buffer, each digest folded back into
// the buffer so that no round is the one before it.
//
// std sizes BLAKE3's parallelism from `std.simd.suggestVectorLength(u32)`,
// which is 4 with `+simd128`, so whole 1 KiB chunks are compressed four at a
// time, one per lane of a `@Vector(4, u32)`.
//
// That four-way compressor is `hashMany`, fully unrolled: the message words of
// four chunks are gathered into lanes with `v128.load32_lane`, the rounds are
// `i32x4.add` and `v128.xor`, and since wasm has no vector rotate every one of
// the 224 rotations is an `i32x4.shl`, an `i32x4.shr_u` and a `v128.or`.  What
// cannot fill four lanes -- the tail of a piece, the parent nodes, the final
// block -- goes through the scalar `compressPre`, where a rotate is one
// `i32.rotl`, reached through a recursive `compressSubtreeWide` that splits
// the input as a tree.  simdsum is a vector loop of a few instructions; this
// is long straight-line runs of vector code with the sixteen state rows and
// sixteen message words all live in v128 values, a test of register
// allocation under pressure that a short loop never applies.
const std = @import("std");
const w = @import("wasi");

const Blake3 = std.crypto.hash.Blake3;

var buf: [64 * 1024]u8 = undefined;

pub fn main() void {
    var x: u32 = 0xb1a4e3;
    for (&buf) |*b| {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        b.* = @truncate(x);
    }
    var total: u64 = 0;
    var out: [32]u8 = undefined;
    var r: u32 = 0;
    while (r < 700) : (r += 1) {
        // Whole, and then in uneven pieces through `update`, which takes the
        // chunk-at-a-time path for the pieces that do not fill four chunks.
        Blake3.hash(&buf, &out, .{});
        total +%= std.mem.readInt(u64, out[0..8], .little);
        var h = Blake3.init(.{});
        var pos: usize = 0;
        var step: usize = 1 + r % 977;
        while (pos < buf.len) : (step = step * 3 % 5003 + 1) {
            const end = @min(buf.len, pos + step);
            h.update(buf[pos..end]);
            pos = end;
        }
        h.final(&out);
        total +%= std.mem.readInt(u64, out[8..16], .little);
        @memcpy(buf[(r * 32) % buf.len ..][0..32], &out);
    }
    w.print("total = ", total);
}
