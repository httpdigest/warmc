// Word frequencies: `std.mem.tokenizeScalar` splits a generated text on
// spaces, a `std.StringHashMap(u32)` counts the words, `std.mem.eql` checks
// each one against a stop list and `std.mem.find` counts a phrase.
//
// This is std's string handling as a program uses it, and nearly all of it is
// inlined into main: the tokenizer's byte-at-a-time scan, the map's probe, and
// Wyhash, the map's string hash, whose 64x64->128 "mum" step has no wasm
// instruction and is a call to compiler-rt's `__multi3` each time (a dozen
// call sites in main and six more in the out-of-line copy of the hash).
// `std.mem.eql` has a SIMD path, compiled in as `i8x16.ne` and
// `v128.any_true`, but only for slices longer than sixteen bytes; every word
// here is shorter, so it takes the branch-free one instead -- three byte XORs
// below four bytes, four overlapping unaligned u32 loads XORed together up to
// sixteen.  The phrase search is Boyer-Moore-Horspool, which builds a
// 256-entry skip table on the stack at every call.  Short strings, unaligned
// word loads, and a few `__multi3` calls a token are the mix, and none of the
// corpus's other kernels has it.
//
// The map is read back only by looking up the vocabulary in its own order, so
// nothing depends on the map's iteration order.
const std = @import("std");
const w = @import("wasi");

const vocab = [_][]const u8{
    "the",    "of",     "and",    "a",       "to",     "in",     "is",     "you",
    "that",   "it",     "he",     "was",     "for",    "on",     "are",    "as",
    "with",   "his",    "they",   "at",      "be",     "this",   "have",   "from",
    "or",     "one",    "had",    "by",      "word",   "but",    "not",    "what",
    "all",    "were",   "we",     "when",    "your",   "can",    "said",   "there",
    "use",    "an",     "each",   "which",   "she",    "do",     "how",    "their",
    "if",     "will",   "up",     "other",   "about",  "out",    "many",   "then",
    "them",   "these",  "so",     "some",    "her",    "would",  "make",   "like",
    "harbour", "lantern", "quarry", "meridian", "thistle", "ember", "cobalt", "saffron",
};
const stop = [_][]const u8{ "the", "of", "and", "a", "to", "in", "is", "it" };

var text: [96 * 1024]u8 = undefined;
var len: usize = 0;

fn generate() void {
    var x: u32 = 8086;
    while (true) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        // Skewed towards the front of the list, as real text is.
        const pick = @min(x % vocab.len, (x >> 8) % vocab.len);
        const word = vocab[pick];
        const gap: usize = if ((x >> 20) & 7 == 0) 2 else 1;
        if (len + word.len + gap > text.len) break;
        @memcpy(text[len..][0..word.len], word);
        len += word.len;
        @memset(text[len..][0..gap], ' ');
        len += gap;
    }
}

fn round(map: *std.StringHashMap(u32)) !u64 {
    map.clearRetainingCapacity();
    var stops: u64 = 0;
    var it = std.mem.tokenizeScalar(u8, text[0..len], ' ');
    while (it.next()) |tok| {
        const gop = try map.getOrPut(tok);
        if (!gop.found_existing) gop.value_ptr.* = 0;
        gop.value_ptr.* += 1;
        for (stop) |s| {
            if (std.mem.eql(u8, tok, s)) {
                stops += 1;
                break;
            }
        }
    }
    var phrases: u64 = 0;
    var pos: usize = 0;
    while (std.mem.find(u8, text[pos..len], "the one")) |at| {
        phrases += 1;
        pos += at + 1;
    }
    var sum: u64 = map.count();
    for (vocab, 0..) |word, i| sum = sum *% 31 +% (map.get(word) orelse 0) *% (i + 1);
    return sum +% stops *% 7 +% phrases *% 1009;
}

pub fn main() void {
    generate();
    var map = std.StringHashMap(u32).init(std.heap.wasm_allocator);
    defer map.deinit();
    var total: u64 = 0;
    var r: u32 = 0;
    var x: u32 = 1;
    while (r < 100) : (r += 1) {
        total +%= round(&map) catch {
            w.print("out of memory at round ", r);
            return;
        };
        // Turn one letter into a capital, which makes a word the vocabulary
        // does not have.
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        const at = x % len;
        if (text[at] != ' ') text[at] &= 0xdf;
    }
    w.print("total = ", total);
}
