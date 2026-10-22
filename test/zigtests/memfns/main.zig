// std.mem's slice functions and std.ascii, on strings the optimiser cannot
// see (they pass through `w.hide`).
//
// Splitting and tokenizing in every flavour (scalar, any-of, sequence,
// backwards, with first/peek/rest/reset), searching (find, findLast, findPos,
// findScalar, findScalarLast, findAny, findNone, findDiff, count,
// countScalar), comparing (eql, order, lessThan, startsWith/endsWith,
// cut/cutScalar/cutLast/cutPrefix/cutSuffix), trimming, replace and
// replaceOwned with replacementSize, reverse, rotate, swap, collapseRepeats,
// join and concat on an allocator, min/max/findMin/findMax/minMax, window,
// readInt/writeInt in both byte orders over u16..u128 and odd byte widths
// (u24, u48), readVarInt, asBytes/toBytes/bytesAsSlice/sliceAsBytes, and the
// ascii classifiers and case functions.  std.mem.findScalar compares a vector
// of bytes at a time (`i8x16.eq`), find switches to Boyer-Moore-Horspool for
// needles longer than four bytes in haystacks of 52 bytes or more, and eql
// compares sixteen bytes at a time past sixteen; the inputs here are chosen
// long and short so both paths of each run.
const std = @import("std");
const w = @import("wasi");

const gpa = std.heap.wasm_allocator;

fn showIter(label: []const u8, it: anytype) void {
    w.printf("{s}:", .{label});
    var n: usize = 0;
    while (it.next()) |part| : (n += 1) w.printf(" [{s}]", .{part});
    w.printf(" ({d})\n", .{n});
}

fn splitting() void {
    const csv = w.hide(@as([]const u8, "alpha,beta,,gamma,delta,,,epsilon,"));
    var s1 = std.mem.splitScalar(u8, csv, ',');
    showIter("splitScalar", &s1);
    var t1 = std.mem.tokenizeScalar(u8, csv, ',');
    showIter("tokenizeScalar", &t1);
    const mixed = w.hide(@as([]const u8, "  one two\tthree\n\nfour;five ;six  "));
    var s2 = std.mem.splitAny(u8, mixed, " \t\n;");
    showIter("splitAny", &s2);
    var t2 = std.mem.tokenizeAny(u8, mixed, " \t\n;");
    showIter("tokenizeAny", &t2);
    const seq = w.hide(@as([]const u8, "a::b::::c:d::"));
    var s3 = std.mem.splitSequence(u8, seq, "::");
    showIter("splitSequence", &s3);
    var t3 = std.mem.tokenizeSequence(u8, seq, "::");
    showIter("tokenizeSequence", &t3);
    var b1 = std.mem.splitBackwardsScalar(u8, csv, ',');
    showIter("splitBackwardsScalar", &b1);
    var b2 = std.mem.splitBackwardsSequence(u8, seq, "::");
    showIter("splitBackwardsSequence", &b2);
    var b3 = std.mem.splitBackwardsAny(u8, mixed, " \n");
    showIter("splitBackwardsAny", &b3);

    var it = std.mem.splitScalar(u8, w.hide(@as([]const u8, "k1=v1&k2=v2&k3=v3")), '&');
    w.printf("first={s}\n", .{it.first()});
    w.printf("peek={?s}\n", .{it.peek()});
    w.printf("rest={s}\n", .{it.rest()});
    _ = it.next();
    w.printf("after next rest={s}\n", .{it.rest()});
    it.reset();
    while (it.next()) |kv| {
        if (std.mem.cutScalar(u8, kv, '=')) |pair| {
            w.printf("  key={s} value={s}\n", .{ pair[0], pair[1] });
        }
    }
    var tk = std.mem.tokenizeScalar(u8, w.hide(@as([]const u8, "   lead and trail   ")), ' ');
    w.printf("tok peek={?s}\n", .{tk.peek()});
    _ = tk.next();
    w.printf("tok rest={s}\n", .{tk.rest()});
    // Empty and delimiter-only inputs.
    var e1 = std.mem.splitScalar(u8, w.hide(@as([]const u8, "")), ',');
    showIter("split empty", &e1);
    var e2 = std.mem.tokenizeScalar(u8, w.hide(@as([]const u8, ",,,,")), ',');
    showIter("tokenize delims", &e2);
    var e3 = std.mem.splitScalar(u8, w.hide(@as([]const u8, ",")), ',');
    showIter("split one delim", &e3);
}

fn searching() void {
    const hay = w.hide(@as([]const u8, "the quick brown fox jumps over the lazy dog; the quick brown cat naps under the lazy fox; the end"));
    const needles = [_][]const u8{ "the", "fox", "lazy", "quick brown", "the end", "dog;", "cat naps under", "zebra", "", "t", "x;", "e lazy fox; the end" };
    for (needles) |n0| {
        const n = w.hide(n0);
        w.printf("needle [{s}] find={?d} last={?d} pos20={?d} count={d}\n", .{
            n,
            std.mem.find(u8, hay, n),
            std.mem.findLast(u8, hay, n),
            std.mem.findPos(u8, hay, 20, n),
            if (n.len == 0) 0 else std.mem.count(u8, hay, n),
        });
    }
    for ("aeiouxyz;") |c0| {
        const c = w.hide(c0);
        w.printf("scalar {c}: first={?d} last={?d} pos50={?d} count={d}\n", .{
            c,
            std.mem.findScalar(u8, hay, c),
            std.mem.findScalarLast(u8, hay, c),
            std.mem.findScalarPos(u8, hay, 50, c),
            std.mem.countScalar(u8, hay, c),
        });
    }
    w.printf("findAny xyz={?d} findLastAny xyz={?d} findNone 'the '={?d} findLastNone 'end '={?d}\n", .{
        std.mem.findAny(u8, hay, "xyz"),
        std.mem.findLastAny(u8, hay, "xyz"),
        std.mem.findNone(u8, hay, "the "),
        std.mem.findLastNone(u8, hay, "end "),
    });
    // A long haystack of one letter with the needle at the very end.
    var long: [300]u8 = undefined;
    @memset(&long, 'a');
    long[297] = 'b';
    long[298] = 'c';
    long[299] = 'd';
    const lh = w.hide(@as([]const u8, &long));
    w.printf("long find bcd={?d} aab={?d} abc={?d} count aa={d} findScalar b={?d}\n", .{
        std.mem.find(u8, lh, "bcd"),
        std.mem.find(u8, lh, "aab"),
        std.mem.find(u8, lh, "abc"),
        std.mem.count(u8, lh, "aa"),
        std.mem.findScalar(u8, lh, 'b'),
    });
    var long2 = long;
    long2[150] = 'X';
    w.printf("long eql={} findDiff={?d} order={s}\n", .{ std.mem.eql(u8, &long, w.hide(@as([]const u8, &long2))), std.mem.findDiff(u8, &long, &long2), @tagName(std.mem.order(u8, &long, &long2)) });
    const ints = w.hide(@as([]const u32, &.{ 5, 9, 2, 9, 1, 7, 1, 8, 3 }));
    w.printf("ints min={d} max={d} findMin={d} findMax={d} minMax={d},{d} findMinMax={d},{d} find 9,1={?d}\n", .{
        std.mem.min(u32, ints),
        std.mem.max(u32, ints),
        std.mem.findMin(u32, ints),
        std.mem.findMax(u32, ints),
        std.mem.minMax(u32, ints)[0],
        std.mem.minMax(u32, ints)[1],
        std.mem.findMinMax(u32, ints)[0],
        std.mem.findMinMax(u32, ints)[1],
        std.mem.find(u32, ints, &.{ 9, 1 }),
    });
}

fn comparing() void {
    const words = [_][]const u8{ "apple", "apples", "app", "banana", "Apple", "", "apricot", "apple" };
    for (words, 0..) |a0, i| {
        const a = w.hide(a0);
        const b = w.hide(words[(i + 1) % words.len]);
        w.printf("cmp [{s}] [{s}] eql={} order={s} lt={} starts={} ends={} eqlIC={} orderIC={s}\n", .{
            a,                                    b,
            std.mem.eql(u8, a, b),                @tagName(std.mem.order(u8, a, b)),
            std.mem.lessThan(u8, a, b),           std.mem.startsWith(u8, a, b),
            std.mem.endsWith(u8, a, b),           std.ascii.eqlIgnoreCase(a, b),
            @tagName(std.ascii.orderIgnoreCase(a, b)),
        });
    }
    const path = w.hide(@as([]const u8, "/usr/local/lib/libfoo.so.1"));
    if (std.mem.cut(u8, path, "/lib/")) |p| w.printf("cut /lib/: [{s}] [{s}]\n", .{ p[0], p[1] });
    if (std.mem.cutLast(u8, path, ".")) |p| w.printf("cutLast .: [{s}] [{s}]\n", .{ p[0], p[1] });
    if (std.mem.cutScalarLast(u8, path, '/')) |p| w.printf("cutScalarLast /: [{s}] [{s}]\n", .{ p[0], p[1] });
    w.printf("cutPrefix /usr={?s} cutPrefix /opt={?s} cutSuffix .1={?s}\n", .{
        std.mem.cutPrefix(u8, path, "/usr"),
        std.mem.cutPrefix(u8, path, "/opt"),
        std.mem.cutSuffix(u8, path, ".1"),
    });
    const padded = w.hide(@as([]const u8, " \t xx hello world xx \n "));
    w.printf("trim=[{s}] trimStart=[{s}] trimEnd=[{s}] trim2=[{s}]\n", .{
        std.mem.trim(u8, padded, " \t\n"),
        std.mem.trimStart(u8, padded, " \t\n"),
        std.mem.trimEnd(u8, padded, " \t\n"),
        std.mem.trim(u8, padded, " \t\nx"),
    });
}

fn editing() !void {
    const src = w.hide(@as([]const u8, "one fish two fish red fish blue fish"));
    var out: [128]u8 = undefined;
    const reps = [_][2][]const u8{ .{ "fish", "cat" }, .{ "fish", "whale" }, .{ " ", "" }, .{ "o", "0" }, .{ "nothing", "x" } };
    for (reps) |r| {
        const size = std.mem.replacementSize(u8, src, r[0], r[1]);
        const n = std.mem.replace(u8, src, r[0], r[1], out[0..size]);
        const owned = try std.mem.replaceOwned(u8, gpa, src, r[0], r[1]);
        defer gpa.free(owned);
        w.printf("replace {s}->{s}: n={d} size={d} [{s}] owned eql={}\n", .{ r[0], r[1], n, size, out[0..size], std.mem.eql(u8, owned, out[0..size]) });
    }
    var buf: [26]u8 = undefined;
    for (&buf, 0..) |*c, i| c.* = 'a' + @as(u8, @intCast(i));
    const b = w.hide(@as([]u8, &buf));
    std.mem.reverse(u8, b);
    w.printf("reverse: {s}\n", .{b});
    var k: usize = 0;
    while (k < 30) : (k += 7) {
        std.mem.rotate(u8, b, k % b.len);
        w.printf("rotate {d}: {s}\n", .{ k % b.len, b });
    }
    std.mem.swap(u8, &b[0], &b[25]);
    w.printf("swap ends: {s}\n", .{b});
    var rit = std.mem.reverseIterator(@as([]const u8, b[0..6]));
    w.puts("reverseIterator:");
    while (rit.next()) |c| w.printf(" {c}", .{c});
    w.puts("\n");
    var spaces = "a  b   c    d e  ".*;
    const collapsed = std.mem.collapseRepeats(u8, w.hide(@as([]u8, &spaces)), ' ');
    w.printf("collapseRepeats=[{s}] len={d}\n", .{ collapsed, collapsed.len });
    var nums = [_]i32{ 1, 2, 2, 2, 3, 3, 4, 2, 2 };
    const cn = std.mem.collapseRepeats(i32, w.hide(@as([]i32, &nums)), 2);
    w.printf("collapse ints len={d}:", .{cn.len});
    for (cn) |v| w.printf(" {d}", .{v});
    w.puts("\n");
    var rs = "a.b.c.d".*;
    std.mem.replaceScalar(u8, &rs, '.', '/');
    w.printf("replaceScalar: {s}\n", .{rs});

    const parts = [_][]const u8{ "usr", "local", "share", "zig" };
    const joined = try std.mem.join(gpa, "/", &parts);
    defer gpa.free(joined);
    const joinedz = try std.mem.joinZ(gpa, ", ", &parts);
    defer gpa.free(joinedz);
    const cat = try std.mem.concat(gpa, u8, &parts);
    defer gpa.free(cat);
    const cati = try std.mem.concat(gpa, u16, &.{ &[_]u16{ 1, 2 }, &[_]u16{}, &[_]u16{ 3, 4, 5 } });
    defer gpa.free(cati);
    w.printf("join={s} joinZ={s} (sentinel {d}) concat={s} concat u16 len={d} last={d}\n", .{ joined, joinedz, joinedz[joinedz.len], cat, cati.len, cati[cati.len - 1] });
    const empty_join = try std.mem.join(gpa, "-", &.{});
    defer gpa.free(empty_join);
    w.printf("empty join len={d}\n", .{empty_join.len});

    var win = std.mem.window(u8, w.hide(@as([]const u8, "abcdefghij")), 4, 3);
    w.puts("window 4/3:");
    while (win.next()) |s| w.printf(" {s}", .{s});
    w.puts("\n");
    var win2 = std.mem.window(u8, w.hide(@as([]const u8, "abcdefg")), 3, 1);
    w.puts("window 3/1:");
    while (win2.next()) |s| w.printf(" {s}", .{s});
    w.puts("\n");
}

fn bytes() void {
    var buf: [16]u8 = undefined;
    for (&buf, 0..) |*b, i| b.* = @intCast(0x10 + i * 0x0f);
    const bb = w.hide(buf);
    w.printf("u16 le={x} be={x}\n", .{ std.mem.readInt(u16, bb[0..2], .little), std.mem.readInt(u16, bb[0..2], .big) });
    w.printf("u24 le={x} be={x}\n", .{ std.mem.readInt(u24, bb[1..4], .little), std.mem.readInt(u24, bb[1..4], .big) });
    w.printf("u32 le={x} be={x}\n", .{ std.mem.readInt(u32, bb[3..7], .little), std.mem.readInt(u32, bb[3..7], .big) });
    w.printf("i32 le={d} be={d}\n", .{ std.mem.readInt(i32, bb[12..16], .little), std.mem.readInt(i32, bb[12..16], .big) });
    w.printf("u48 le={x} be={x}\n", .{ std.mem.readInt(u48, bb[5..11], .little), std.mem.readInt(u48, bb[5..11], .big) });
    w.printf("u64 le={x} be={x}\n", .{ std.mem.readInt(u64, bb[7..15], .little), std.mem.readInt(u64, bb[7..15], .big) });
    w.printf("u128 le={x} be={x}\n", .{ std.mem.readInt(u128, &bb, .little), std.mem.readInt(u128, &bb, .big) });
    w.printf("varint 3 le={x} be={x} 5 be={x}\n", .{ std.mem.readVarInt(u32, bb[0..3], .little), std.mem.readVarInt(u32, bb[0..3], .big), std.mem.readVarInt(u64, bb[2..7], .big) });
    var out: [16]u8 = @splat(0);
    std.mem.writeInt(u32, out[0..4], w.hide(@as(u32, 0xdeadbeef)), .little);
    std.mem.writeInt(u32, out[4..8], w.hide(@as(u32, 0xdeadbeef)), .big);
    std.mem.writeInt(i16, out[8..10], w.hide(@as(i16, -2)), .big);
    std.mem.writeInt(u48, out[10..16], w.hide(@as(u48, 0x0102_0304_0506)), .little);
    w.puts("writeInt:");
    for (out) |b| w.printf(" {x:0>2}", .{b});
    w.puts("\n");
    var ow: [16]u8 = undefined;
    std.mem.writeInt(u128, &ow, w.hide(@as(u128, 0x00112233_44556677_8899aabb_ccddeeff)), .big);
    w.printf("u128 be bytes: {x}\n", .{&ow});
    w.printf("u128 round trip le: {x}\n", .{std.mem.readInt(u128, &ow, .little)});

    const val = w.hide(@as(u32, 0x11223344));
    const as = std.mem.asBytes(&val);
    w.printf("asBytes: {x} {x} {x} {x}\n", .{ as[0], as[1], as[2], as[3] });
    const tb = std.mem.toBytes(w.hide(@as(u64, 0x0807060504030201)));
    w.printf("toBytes: {any}\n", .{tb});
    w.printf("bytesToValue u32: {x}\n", .{std.mem.bytesToValue(u32, bb[4..8])});
    var words: [4]u32 align(4) = .{ 0x03020100, 0x07060504, 0x0b0a0908, 0x0f0e0d0c };
    const wb = std.mem.sliceAsBytes(w.hide(@as([]u32, &words)));
    w.printf("sliceAsBytes len={d} [5]={d} [14]={d}\n", .{ wb.len, wb[5], wb[14] });
    const halves = std.mem.bytesAsSlice(u16, wb);
    w.printf("bytesAsSlice u16 len={d}:", .{halves.len});
    for (halves) |h| w.printf(" {x}", .{h});
    w.puts("\n");
    w.printf("byteSwapAllElements: ", .{});
    var sw = [_]u16{ 0x0102, 0x0304, 0xa0b0 };
    std.mem.byteSwapAllElements(u16, w.hide(@as([]u16, &sw)));
    for (sw) |s| w.printf(" {x}", .{s});
    w.puts("\n");
    w.printf("nativeToBig 0x1234={x} littleToNative={x} bigToNative 0x11223344={x}\n", .{ std.mem.nativeToBig(u16, w.hide(@as(u16, 0x1234))), std.mem.littleToNative(u32, w.hide(@as(u32, 0xaabbccdd))), std.mem.bigToNative(u32, w.hide(@as(u32, 0x11223344))) });
    w.printf("allEqual={} {}\n", .{ std.mem.allEqual(u8, w.hide(@as([]const u8, "zzzz")), 'z'), std.mem.allEqual(u8, w.hide(@as([]const u8, "zzaz")), 'z') });
    w.printf("alignForward(13,8)={d} alignBackward(13,8)={d} isAligned(64,16)={}\n", .{ std.mem.alignForward(usize, w.hide(@as(usize, 13)), 8), std.mem.alignBackward(usize, w.hide(@as(usize, 13)), 8), std.mem.isAligned(w.hide(@as(usize, 64)), 16) });
}

fn ascii() !void {
    const s = w.hide(@as([]const u8, "Hello, World! 0123 xyz_ABC\t~"));
    w.puts("classes:\n");
    for (s) |c| {
        w.printf("  {d:>3} alpha={} digit={} alnum={} upper={} lower={} space={} punct={} hex={} print={} -> up={c} low={c}\n", .{
            c,                         std.ascii.isAlphabetic(c), std.ascii.isDigit(c),
            std.ascii.isAlphanumeric(c), std.ascii.isUpper(c),    std.ascii.isLower(c),
            std.ascii.isWhitespace(c), std.ascii.isPunctuation(c), std.ascii.isHex(c),
            std.ascii.isPrint(c),      std.ascii.toUpper(c),      std.ascii.toLower(c),
        });
    }
    var lo: [64]u8 = undefined;
    var up: [64]u8 = undefined;
    w.printf("lower=[{s}] upper=[{s}]\n", .{ std.ascii.lowerString(&lo, s), std.ascii.upperString(&up, s) });
    const al = try std.ascii.allocUpperString(gpa, s);
    defer gpa.free(al);
    w.printf("allocUpper=[{s}]\n", .{al});
    w.printf("startsWithIC={} endsWithIC={} findIC={?d} findIC miss={?d} lessThanIC={}\n", .{
        std.ascii.startsWithIgnoreCase(s, "hELLO"),
        std.ascii.endsWithIgnoreCase(s, "abc\t~"),
        std.ascii.findIgnoreCase(s, "WORLD"),
        std.ascii.findIgnoreCase(s, "planet"),
        std.ascii.lessThanIgnoreCase("abc", "ABD"),
    });
}

pub fn main() void {
    splitting();
    searching();
    comparing();
    editing() catch w.puts("editing: out of memory\n");
    bytes();
    ascii() catch w.puts("ascii: out of memory\n");
    w.puts("done\n");
}
