// A keyword scanner whose automaton is built entirely at compile time.
//
// `Automaton(words)` is a comptime function that returns a type: it builds an
// Aho-Corasick trie, its failure links and the full 256-way transition table
// while the compiler runs, picks the narrowest integer that can number the
// states with `std.math.IntFittingRange`, and hands back a struct with the
// table as a constant.  (That evaluation is most of the five seconds this file
// takes to build.)
//
// What reaches warmc is two tables in a data segment and, per byte, a serial
// chain of loads: the text byte, then the next state at `state << 8 | byte`
// with the table's address folded into the load's offset -- `i32.load8_u` for
// the 42-state u6 automaton, `i32.load16_u` for the 433-state u9 one.  Zig does
// not tell LLVM that a stored u6 is below 64, so each loaded state is masked
// again (`i32.and 63`, `i32.and 511`) before it indexes the next load.  The
// load latency is the loop, which makes this a direct measure of how warmc
// forms addresses and schedules dependent loads.
const w = @import("wasi");
const std = @import("std");

fn trieSize(comptime words: []const []const u8) usize {
    var n: usize = 1;
    for (words) |word| n += word.len;
    return n;
}

fn Automaton(comptime words: []const []const u8) type {
    @setEvalBranchQuota(20_000_000);
    const max = trieSize(words);
    const built = blk: {
        var go: [max][256]u32 = undefined;
        var out: [max]u8 = undefined;
        var fail: [max]u32 = undefined;
        for (&go) |*row| row.* = @splat(std.math.maxInt(u32));
        @memset(&out, 0);
        var n: u32 = 1;
        for (words) |word| {
            var s: u32 = 0;
            for (word) |c| {
                if (go[s][c] == std.math.maxInt(u32)) {
                    go[s][c] = n;
                    n += 1;
                }
                s = go[s][c];
            }
            out[s] += 1;
        }
        // Breadth-first over the trie: a state's missing edges are its failure
        // state's edges, so the table needs no failure walk at run time.
        var queue: [max]u32 = undefined;
        var head: usize = 0;
        var tail: usize = 0;
        for (0..256) |c| {
            if (go[0][c] == std.math.maxInt(u32)) {
                go[0][c] = 0;
            } else {
                fail[go[0][c]] = 0;
                queue[tail] = go[0][c];
                tail += 1;
            }
        }
        while (head < tail) : (head += 1) {
            const s = queue[head];
            out[s] += out[fail[s]];
            for (0..256) |c| {
                const t = go[s][c];
                if (t == std.math.maxInt(u32)) {
                    go[s][c] = go[fail[s]][c];
                } else {
                    fail[t] = go[fail[s]][c];
                    queue[tail] = t;
                    tail += 1;
                }
            }
        }
        break :blk .{ .go = go, .out = out, .n = n };
    };

    return struct {
        pub const states = built.n;
        pub const State = std.math.IntFittingRange(0, states - 1);
        pub const delta: [states][256]State = blk: {
            @setEvalBranchQuota(20_000_000);
            var t: [states][256]State = undefined;
            for (0..states) |s| {
                for (0..256) |c| t[s][c] = @intCast(built.go[s][c]);
            }
            break :blk t;
        };
        pub const hits: [states]u8 = built.out[0..states].*;

        pub fn scan(bytes: []const u8, start: State, matches: *u64) State {
            var s = start;
            var m: u64 = 0;
            for (bytes) |c| {
                s = delta[s][c];
                m += hits[s];
            }
            matches.* +%= m;
            return s;
        }
    };
}

// A hand-picked list small enough for byte states, and a generated one -- also
// at compile time, from a PRNG -- large enough to need 16-bit ones.
const Small = Automaton(&.{ "cab", "bad", "dead", "fade", "face", "cafe", "bead", "head", "egg", "hag", "added", "faced", "ache", "beach" });

const big_words = blk: {
    var words: [96][]const u8 = undefined;
    var x: u32 = 0x1234567;
    for (&words) |*word| {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        const len = 4 + x % 5;
        var buf: [8]u8 = undefined;
        for (0..len) |i| buf[i] = 'a' + @as(u8, @intCast((x >> @intCast(3 * i + 3)) & 7));
        const copy = buf[0..len].*;
        word.* = &copy;
    }
    break :blk words;
};
const Big = Automaton(&big_words);

// 42 states and 433: a u6 table of bytes and a u9 table of halfwords.
comptime {
    std.debug.assert(@bitSizeOf(Small.State) <= 8 and @bitSizeOf(Big.State) > 8);
}

var text: [65536]u8 = undefined;

pub fn main() void {
    var seed: u32 = 2024;
    for (&text) |*b| {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        b.* = 'a' + @as(u8, @truncate(seed & 7));
    }
    var small_hits: u64 = 0;
    var big_hits: u64 = 0;
    var s1: Small.State = 0;
    var s2: Big.State = 0;
    var finals: u64 = 0;
    var r: u32 = 0;
    while (r < 300) : (r += 1) {
        s1 = Small.scan(&text, s1, &small_hits);
        s2 = Big.scan(&text, s2, &big_hits);
        finals +%= @as(u64, s1) *% 131 +% s2;
        text[(r *% 2654435761) % text.len] = 'a' + @as(u8, @truncate(r & 7));
    }
    w.print("total = ", small_hits *% 1000003 +% big_hits *% 7 +% finals +% Small.states *% 10 +% Big.states);
}
