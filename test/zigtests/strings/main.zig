// Text: UTF-8 and UTF-16 through std.unicode, base64 through std.base64,
// hex through std.fmt, and a handful of hand-written string routines (a
// fixed-capacity string builder, case conversion, palindromes, Caesar and
// rot13, run-length encoding and decoding, word reversal, and a naive
// substring count checked against std.mem.count).
//
// UTF-8 decoding is byte-class branching, shifts and masks on u21 values (a
// 21-bit integer kept in an i32 and re-masked), and validation has an ASCII
// fast path that tests eight bytes at a time against 0x80 before falling back
// to the byte-at-a-time state machine; the inputs mix ASCII runs with 2-, 3-
// and 4-byte sequences and a set of malformed ones (overlong, surrogate, out
// of range, truncated, stray continuation) so both paths and every error
// arm run.  Base64 is table lookups and 6-bit shifts with padding rules, run
// over every length from 0 to 20 in all four codecs and decoded back.
const std = @import("std");
const w = @import("wasi");

const gpa = std.heap.wasm_allocator;

fn utf8() !void {
    const samples = [_][]const u8{
        "plain ascii",
        "caf\u{e9} na\u{ef}ve r\u{e9}sum\u{e9}",
        "\u{3b1}\u{3b2}\u{3b3} \u{3a9}",
        "\u{65e5}\u{672c}\u{8a9e}\u{30c6}\u{30ad}\u{30b9}\u{30c8}",
        "emoji \u{1f600}\u{1f680}\u{1f30d}!",
        "mixed a\u{e9}\u{65e5}\u{1f600}z",
        "",
        "\u{10ffff}\u{7f}\u{80}\u{7ff}\u{800}\u{ffff}\u{10000}",
    };
    for (samples, 0..) |s0, i| {
        const s = w.hide(s0);
        const n = try std.unicode.utf8CountCodepoints(s);
        w.printf("utf8[{d}] bytes={d} codepoints={d} valid={}:", .{ i, s.len, n, std.unicode.utf8ValidateSlice(s) });
        const view = try std.unicode.Utf8View.init(s);
        var it = view.iterator();
        var sum: u32 = 0;
        while (it.nextCodepointSlice()) |cs| {
            const cp = try std.unicode.utf8Decode(cs);
            sum +%= cp;
            w.printf(" U+{X:0>4}/{d}", .{ cp, cs.len });
        }
        w.printf(" sum={d}\n", .{sum});
        // Re-encode every codepoint and compare with the input.
        var buf: [64]u8 = undefined;
        var len: usize = 0;
        var it2 = view.iterator();
        while (it2.nextCodepoint()) |cp| len += try std.unicode.utf8Encode(cp, buf[len..]);
        w.printf("  re-encoded equal={}\n", .{std.mem.eql(u8, buf[0..len], s)});
        // UTF-16 round trip.
        const u16s = try std.unicode.utf8ToUtf16LeAlloc(gpa, s);
        defer gpa.free(u16s);
        const back = try std.unicode.utf16LeToUtf8Alloc(gpa, u16s);
        defer gpa.free(back);
        var surrogates: u32 = 0;
        for (u16s) |u| {
            if (std.unicode.utf16IsHighSurrogate(u) or std.unicode.utf16IsLowSurrogate(u)) surrogates += 1;
        }
        w.printf("  utf16 units={d} surrogates={d} codepoints={d} roundtrip={}\n", .{ u16s.len, surrogates, try std.unicode.utf16CountCodepoints(u16s), std.mem.eql(u8, back, s) });
    }
    var it = (try std.unicode.Utf8View.init(w.hide(@as([]const u8, "a\u{e9}\u{65e5}\u{1f600}")))).iterator();
    w.printf("peek(2)={d} bytes, peekCodepoint={?d}\n", .{ it.peek(2).len, it.peekCodepoint() });
    _ = it.nextCodepoint();
    w.printf("after one: peek(3)={d} bytes\n", .{it.peek(3).len});

    const cps = [_]u21{ 0, 0x41, 0x7f, 0x80, 0x7ff, 0x800, 0xd7ff, 0xe000, 0xfffd, 0xffff, 0x10000, 0x10ffff, 0xd800, 0xdfff, 0x110000 };
    for (cps) |c0| {
        const c = w.hide(c0);
        var buf: [4]u8 = undefined;
        if (std.unicode.utf8Encode(c, &buf)) |n| {
            w.printf("encode U+{X}: len={d} bytes={x} seqlen={d} valid={}\n", .{ c, n, buf[0..n], std.unicode.utf8ByteSequenceLength(buf[0]) catch 0, std.unicode.utf8ValidCodepoint(c) });
        } else |e| w.printf("encode U+{X}: error {s}\n", .{ c, @errorName(e) });
    }
    const bad = [_][]const u8{
        "\xc0\x80", // overlong NUL
        "\xe0\x80\xaf", // overlong
        "\xed\xa0\x80", // surrogate half
        "\xf4\x90\x80\x80", // above U+10FFFF
        "\xe6\x97", // truncated
        "\x80abc", // stray continuation
        "ok\xffok", // invalid byte
        "\xf0\x9f\x98", // truncated 4-byte
        "abcdefghijklmnop\xc3", // long ASCII run then truncated
    };
    for (bad, 0..) |b0, i| {
        const b = w.hide(b0);
        w.printf("bad[{d}] len={d} validate={} count=", .{ i, b.len, std.unicode.utf8ValidateSlice(b) });
        if (std.unicode.utf8CountCodepoints(b)) |n| w.printf("{d}", .{n}) else |e| w.printf("{s}", .{@errorName(e)});
        w.puts(" decode=");
        const sl = std.unicode.utf8ByteSequenceLength(b[0]) catch 0;
        if (sl == 0 or sl > b.len) {
            w.puts("n/a\n");
        } else if (std.unicode.utf8Decode(b[0..sl])) |cp| w.printf("U+{X}\n", .{cp}) else |e| w.printf("{s}\n", .{@errorName(e)});
    }
    // A long valid string: the eight-byte ASCII fast path with multi-byte
    // characters at every alignment.
    var long: [256]u8 = undefined;
    var len: usize = 0;
    var k: usize = 0;
    while (len + 4 <= long.len) : (k += 1) {
        if (k % 9 == 8) {
            len += try std.unicode.utf8Encode(@intCast(0x400 + k), long[len..]);
        } else if (k % 13 == 12) {
            len += try std.unicode.utf8Encode(@intCast(0x1f300 + k), long[len..]);
        } else {
            long[len] = @intCast('a' + k % 26);
            len += 1;
        }
    }
    const ls = w.hide(@as([]const u8, long[0..len]));
    w.printf("long utf8 bytes={d} valid={} codepoints={d}\n", .{ ls.len, std.unicode.utf8ValidateSlice(ls), try std.unicode.utf8CountCodepoints(ls) });
}

fn base64() !void {
    const codecs = [_]struct { name: []const u8, c: std.base64.Codecs }{
        .{ .name = "standard", .c = std.base64.standard },
        .{ .name = "standard_no_pad", .c = std.base64.standard_no_pad },
        .{ .name = "url_safe", .c = std.base64.url_safe },
        .{ .name = "url_safe_no_pad", .c = std.base64.url_safe_no_pad },
    };
    var src: [20]u8 = undefined;
    for (&src, 0..) |*b, i| b.* = @truncate(w.hide(i) * 53 + 251);
    for (codecs) |cd| {
        var n: usize = 0;
        var all_ok = true;
        while (n <= src.len) : (n += 1) {
            var enc: [32]u8 = undefined;
            const e = cd.c.Encoder.encode(&enc, src[0..n]);
            var dec: [20]u8 = undefined;
            const dl = try cd.c.Decoder.calcSizeForSlice(e);
            try cd.c.Decoder.decode(dec[0..dl], e);
            const ok = dl == n and std.mem.eql(u8, dec[0..dl], src[0..n]);
            if (!ok) all_ok = false;
            if (n % 4 == 0 or n < 4) w.printf("{s} n={d} size={d} enc={s} ok={}\n", .{ cd.name, n, cd.c.Encoder.calcSize(n), e, ok });
        }
        w.printf("{s} all lengths round trip={}\n", .{ cd.name, all_ok });
    }
    const text = w.hide(@as([]const u8, "Many hands make light work."));
    var enc: [64]u8 = undefined;
    const e = std.base64.standard.Encoder.encode(&enc, text);
    w.printf("text -> {s}\n", .{e});
    const bad_inputs = [_][]const u8{ "TWFu", "TWE=", "TQ==", "TQ=", "T===", "TW!u", "TWFuZ", "====" };
    for (bad_inputs) |b0| {
        const b = w.hide(b0);
        var out: [16]u8 = undefined;
        if (std.base64.standard.Decoder.calcSizeForSlice(b)) |sz| {
            if (sz > out.len) {
                w.printf("decode {s}: too long\n", .{b});
            } else if (std.base64.standard.Decoder.decode(out[0..sz], b)) |_| {
                w.printf("decode {s}: ok [{s}]\n", .{ b, out[0..sz] });
            } else |err| w.printf("decode {s}: {s}\n", .{ b, @errorName(err) });
        } else |err| w.printf("decode {s}: size {s}\n", .{ b, @errorName(err) });
    }
    const ign = std.base64.standard.decoderWithIgnore(" \n");
    var out: [64]u8 = undefined;
    const spaced = w.hide(@as([]const u8, "TWFu eSBo\nYW5k cyBt YWtl IGxp Z2h0 IHdv\ncmsu"));
    const got = try ign.decode(&out, spaced);
    w.printf("decode with ignore: [{s}]\n", .{out[0..got]});
}

fn hex() !void {
    const bytes = w.hide([_]u8{ 0x00, 0x01, 0x7f, 0x80, 0xab, 0xcd, 0xef, 0xff });
    w.printf("bytesToHex lower={s} upper={s}\n", .{ std.fmt.bytesToHex(bytes, .lower), std.fmt.bytesToHex(bytes, .upper) });
    w.printf("hex(u32 0x12345678)={s} hex(u64)={s}\n", .{ std.fmt.hex(w.hide(@as(u32, 0x12345678))), std.fmt.hex(w.hide(@as(u64, 0x0123456789abcdef))) });
    var out: [8]u8 = undefined;
    const back = try std.fmt.hexToBytes(&out, "00017f80ABcdEFff");
    w.printf("hexToBytes eq={} len={d}\n", .{ std.mem.eql(u8, back, &bytes), back.len });
    for ([_][]const u8{ "abc", "zz", "0g", "" }) |h| {
        if (std.fmt.hexToBytes(&out, w.hide(h))) |r| {
            w.printf("hexToBytes [{s}] ok len={d}\n", .{ h, r.len });
        } else |e| w.printf("hexToBytes [{s}] {s}\n", .{ h, @errorName(e) });
    }
    w.printf("charToDigit: ", .{});
    for ("09afAFz") |c| {
        if (std.fmt.charToDigit(c, 16)) |d| w.printf("{c}={d} ", .{ c, d }) else |_| w.printf("{c}=err ", .{c});
    }
    w.printf("digitToChar 11 lower={c} upper={c}\n", .{ std.fmt.digitToChar(11, .lower), std.fmt.digitToChar(11, .upper) });
}

/// A string builder over a fixed buffer, as code without an allocator writes
/// it.
const Builder = struct {
    buf: [256]u8 = undefined,
    len: usize = 0,

    fn add(b: *Builder, s: []const u8) void {
        const n = @min(s.len, b.buf.len - b.len);
        @memcpy(b.buf[b.len..][0..n], s[0..n]);
        b.len += n;
    }
    fn addChar(b: *Builder, c: u8) void {
        if (b.len < b.buf.len) {
            b.buf[b.len] = c;
            b.len += 1;
        }
    }
    fn addInt(b: *Builder, v: i64) void {
        if (v < 0) b.addChar('-');
        var mag: u64 = @abs(v);
        var tmp: [20]u8 = undefined;
        var i: usize = tmp.len;
        while (true) {
            i -= 1;
            tmp[i] = @intCast('0' + mag % 10);
            mag /= 10;
            if (mag == 0) break;
        }
        b.add(tmp[i..]);
    }
    fn slice(b: *const Builder) []const u8 {
        return b.buf[0..b.len];
    }
};

fn isPalindrome(s: []const u8) bool {
    var i: usize = 0;
    var j: usize = s.len;
    while (true) {
        while (i < j and !std.ascii.isAlphanumeric(s[i])) i += 1;
        while (j > i and !std.ascii.isAlphanumeric(s[j - 1])) j -= 1;
        if (j <= i + 1) return true;
        if (std.ascii.toLower(s[i]) != std.ascii.toLower(s[j - 1])) return false;
        i += 1;
        j -= 1;
    }
}

fn caesar(out: []u8, s: []const u8, shift: u8) []u8 {
    for (s, 0..) |c, i| {
        out[i] = if (std.ascii.isLower(c))
            'a' + (c - 'a' + shift) % 26
        else if (std.ascii.isUpper(c))
            'A' + (c - 'A' + shift) % 26
        else
            c;
    }
    return out[0..s.len];
}

fn rle(out: []u8, s: []const u8) []u8 {
    var o: usize = 0;
    var i: usize = 0;
    while (i < s.len) {
        var j = i;
        while (j < s.len and s[j] == s[i] and j - i < 9) j += 1;
        out[o] = @intCast('0' + (j - i));
        out[o + 1] = s[i];
        o += 2;
        i = j;
    }
    return out[0..o];
}

fn unrle(out: []u8, s: []const u8) []u8 {
    var o: usize = 0;
    var i: usize = 0;
    while (i + 1 < s.len) : (i += 2) {
        const n = s[i] - '0';
        @memset(out[o..][0..n], s[i + 1]);
        o += n;
    }
    return out[0..o];
}

fn naiveCount(hay: []const u8, needle: []const u8) usize {
    var n: usize = 0;
    var i: usize = 0;
    while (i + needle.len <= hay.len) {
        if (std.mem.eql(u8, hay[i..][0..needle.len], needle)) {
            n += 1;
            i += needle.len;
        } else i += 1;
    }
    return n;
}

fn handWritten() void {
    var b = Builder{};
    var i: i64 = -3;
    while (i <= 3) : (i += 1) {
        b.add("[");
        b.addInt(w.hide(i) * 1234567);
        b.add("]");
    }
    b.addInt(std.math.minInt(i64));
    w.printf("builder len={d}: {s}\n", .{ b.len, b.slice() });

    const phrases = [_][]const u8{ "A man, a plan, a canal: Panama", "racecar", "Zig is not a palindrome", "Was it a car or a cat I saw?", "", "ab", "No 'x' in Nixon", "12321", "123421" };
    for (phrases) |p| w.printf("palindrome [{s}] = {}\n", .{ p, isPalindrome(w.hide(p)) });

    const msg = w.hide(@as([]const u8, "The Quick Brown Fox Jumps Over The Lazy Dog, 2024!"));
    var buf1: [64]u8 = undefined;
    var buf2: [64]u8 = undefined;
    var shift: u8 = 0;
    while (shift < 26) : (shift += 5) {
        const enc = caesar(&buf1, msg, shift);
        const dec = caesar(&buf2, enc, 26 - shift);
        w.printf("caesar {d}: {s} back={}\n", .{ shift, enc, std.mem.eql(u8, dec, msg) });
    }
    const r13 = caesar(&buf1, msg, 13);
    w.printf("rot13: {s}\n", .{r13});
    w.printf("rot13 twice: {s}\n", .{caesar(&buf2, r13, 13)});

    const runs = [_][]const u8{ "aaabccddddde", "abc", "zzzzzzzzzzzzzzzzzzzzzz", "", "aabbaabb", "xyyyyyyyyyyyyx" };
    for (runs) |r0| {
        const r = w.hide(r0);
        var eb: [64]u8 = undefined;
        var db: [64]u8 = undefined;
        const e = rle(&eb, r);
        const d = unrle(&db, e);
        w.printf("rle [{s}] -> [{s}] -> back={}\n", .{ r, e, std.mem.eql(u8, d, r) });
    }

    var up: [64]u8 = undefined;
    var low: [64]u8 = undefined;
    var title: [64]u8 = undefined;
    for (msg, 0..) |c, k| {
        up[k] = std.ascii.toUpper(c);
        low[k] = std.ascii.toLower(c);
        title[k] = if (k == 0 or msg[k - 1] == ' ') std.ascii.toUpper(c) else std.ascii.toLower(c);
    }
    w.printf("upper={s}\nlower={s}\ntitle={s}\n", .{ up[0..msg.len], low[0..msg.len], title[0..msg.len] });

    const hay = w.hide(@as([]const u8, "abababababcabcabcaaaabbbbabab"));
    for ([_][]const u8{ "ab", "aba", "abc", "aa", "b", "abab", "x" }) |n| {
        w.printf("count [{s}] naive={d} std={d}\n", .{ n, naiveCount(hay, n), std.mem.count(u8, hay, n) });
    }
}

pub fn main() void {
    utf8() catch |e| w.printf("utf8: {s}\n", .{@errorName(e)});
    base64() catch |e| w.printf("base64: {s}\n", .{@errorName(e)});
    hex() catch |e| w.printf("hex: {s}\n", .{@errorName(e)});
    handWritten();
    w.puts("done\n");
}
