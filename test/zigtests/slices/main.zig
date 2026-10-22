// Arrays, slices and pointers to many: what a Zig program does with memory it
// owns, without an allocator.
//
// Covered: fixed arrays by value (a copy on assignment and on passing) against
// arrays by pointer (shared); slices with run-time bounds, `ptr[a..b]`,
// `s[a..][0..n]` (a slice to an array pointer) and slices of slices;
// sentinel-terminated arrays, slices and many-pointers (`[N:0]u8`, `[:0]u8`,
// `[*:0]const u8`) walked with std.mem.span and std.mem.sliceTo; two- and
// three-dimensional arrays indexed both ways and flattened; `++` on arrays at
// compile time, and repetition built from it (0.17 dropped the `**`
// operator), with @splat for the uniform case; @memcpy, @memset and @memmove, and
// std.mem.copyForwards / copyBackwards on overlapping ranges, which are the
// cases a `memory.copy` lowering has to get the direction of right; many-item
// pointer arithmetic; and std.mem.eql / order on the results.
//
// Every step prints the whole array it changed, so a store that lands one
// element off, or a copy that runs in the wrong direction, shows as a line
// that differs.  Lengths and offsets go through `w.hide`, so the slicing is
// done at run time.
const std = @import("std");
const w = @import("wasi");

fn dumpI32(label: []const u8, s: []const i32) void {
    w.printf("{s} len={d}:", .{ label, s.len });
    for (s) |v| w.printf(" {d}", .{v});
    w.puts("\n");
}

fn dumpU8(label: []const u8, s: []const u8) void {
    w.printf("{s} len={d}:", .{ label, s.len });
    for (s) |v| w.printf(" {d}", .{v});
    w.puts("\n");
}

fn bumpCopy(a: [6]i32) [6]i32 {
    var b = a;
    for (&b) |*x| x.* += 100;
    return b;
}

fn bumpInPlace(a: *[6]i32) void {
    for (a) |*x| x.* *= 2;
}

fn sumSlice(s: []const i32) i64 {
    var t: i64 = 0;
    for (s) |v| t += v;
    return t;
}

fn copySemantics() void {
    var a = [6]i32{ 3, -1, 4, -1, 5, -9 };
    a[w.hide(@as(usize, 2))] = 40;
    const b = a; // a copy
    var c = bumpCopy(a);
    bumpInPlace(&a);
    c[0] = 7;
    dumpI32("a", &a);
    dumpI32("b", &b);
    dumpI32("c", &c);
    const p = &a;
    p[1] = -77;
    dumpI32("a via p", &a);
    var arr2: [2][6]i32 = .{ a, b };
    arr2[1][5] = 1234;
    dumpI32("arr2[0]", &arr2[0]);
    dumpI32("arr2[1]", &arr2[1]);
    dumpI32("b again", &b);
    w.printf("sums a={d} b={d} c={d}\n", .{ sumSlice(&a), sumSlice(&b), sumSlice(&c) });
}

fn runtimeSlicing() void {
    var data: [32]i32 = undefined;
    for (&data, 0..) |*d, i| d.* = @as(i32, @intCast(i * i)) - 100;
    const n = w.hide(@as(usize, 32));
    var lo: usize = 0;
    while (lo < n) : (lo += 5) {
        const hi = @min(n, lo + w.hide(@as(usize, 7)));
        const s = data[lo..hi];
        w.printf("slice[{d}..{d}] first={d} last={d} sum={d}\n", .{ lo, hi, s[0], s[s.len - 1], sumSlice(s) });
    }
    // s[a..][0..n] gives a pointer to an array whose length is comptime.
    const off = w.hide(@as(usize, 9));
    const four: *[4]i32 = data[off..][0..4];
    four[3] = 9999;
    dumpI32("four", four);
    dumpI32("data[8..15]", data[8..15]);
    // Slice of slices.
    const rows = [_][]const i32{ data[0..3], data[10..12], data[20..26], data[31..] };
    for (rows, 0..) |r, i| {
        w.printf("row {d}:", .{i});
        for (r) |v| w.printf(" {d}", .{v});
        w.printf(" (sum {d})\n", .{sumSlice(r)});
    }
    // Empty slices.
    const e = data[w.hide(@as(usize, 5))..w.hide(@as(usize, 5))];
    w.printf("empty len={d} sum={d}\n", .{ e.len, sumSlice(e) });
    // Reverse iteration through a runtime-length slice.
    const tail = data[w.hide(@as(usize, 26))..];
    var i: usize = tail.len;
    w.puts("tail reversed:");
    while (i > 0) {
        i -= 1;
        w.printf(" {d}", .{tail[i]});
    }
    w.puts("\n");
}

fn cstrlen(p: [*:0]const u8) usize {
    var n: usize = 0;
    while (p[n] != 0) n += 1;
    return n;
}

fn sentinels() void {
    const lit: [:0]const u8 = "hello, sentinel";
    w.printf("lit len={d} sentinel={d} last={c}\n", .{ lit.len, lit[lit.len], lit[lit.len - 1] });
    var buf: [20:0]u8 = undefined;
    @memset(&buf, 'x');
    buf[20] = 0;
    const cut = w.hide(@as(usize, 7));
    buf[cut] = 0;
    const many: [*:0]const u8 = &buf;
    const sp = std.mem.span(many);
    w.printf("span len={d} \"{s}\" cstrlen={d}\n", .{ sp.len, sp, cstrlen(many) });
    const to = std.mem.sliceTo(&buf, 0);
    w.printf("sliceTo len={d}\n", .{to.len});
    buf[cut] = 'y';
    w.printf("after unset: span len={d} sliceTo={d}\n", .{ std.mem.span(many).len, std.mem.sliceTo(&buf, 0).len });
    // A slice with a sentinel taken at a runtime index.
    var words: [32]u8 = undefined;
    const text = "alpha beta gamma delta";
    @memcpy(words[0..text.len], text);
    var start: usize = 0;
    var k: usize = 0;
    while (k <= text.len) : (k += 1) {
        if (k == text.len or words[k] == ' ') {
            words[k] = 0;
            const z: [:0]u8 = words[start..k :0];
            w.printf("word '{s}' len={d} sentinel={d} cstrlen={d}\n", .{ z, z.len, z[z.len], cstrlen(z.ptr) });
            start = k + 1;
        }
    }
    // Arrays with sentinels of other types.
    const nums = [_:-1]i16{ 5, 4, 3, 2, 1 };
    var j: usize = 0;
    const np: [*:-1]const i16 = &nums;
    while (np[j] != -1) : (j += 1) w.printf("nums[{d}]={d}\n", .{ j, np[j] });
    w.printf("nums sentinel at {d}\n", .{std.mem.indexOfSentinel(i16, -1, np)});
}

fn multiDim() void {
    var m: [4][5]i32 = undefined;
    for (&m, 0..) |*row, r| {
        for (row, 0..) |*cell, c| cell.* = @as(i32, @intCast(r * 10 + c)) * w.hide(@as(i32, 3)) - 20;
    }
    for (m, 0..) |row, r| {
        w.printf("m[{d}]:", .{r});
        for (row) |v| w.printf(" {d:>4}", .{v});
        w.puts("\n");
    }
    // Transpose through indices.
    var t: [5][4]i32 = undefined;
    for (0..4) |r| {
        for (0..5) |c| t[c][r] = m[r][c];
    }
    for (t, 0..) |row, r| {
        w.printf("t[{d}]:", .{r});
        for (row) |v| w.printf(" {d:>4}", .{v});
        w.puts("\n");
    }
    // Column sums with a runtime column.
    var c: usize = w.hide(@as(usize, 0));
    while (c < 5) : (c += 1) {
        var s: i32 = 0;
        for (m) |row| s += row[c];
        w.printf("colsum[{d}]={d}\n", .{ c, s });
    }
    // Flatten through a byte view of the same memory.
    const flat: *[20]i32 = @ptrCast(&m);
    dumpI32("flat", flat);
    var cube: [3][3][3]u8 = undefined;
    for (&cube, 0..) |*plane, x| {
        for (plane, 0..) |*row, y| {
            for (row, 0..) |*v, z| v.* = @intCast((x * 9 + y * 3 + z) * 7 % 256);
        }
    }
    const xs = w.hide(@as(usize, 2));
    cube[xs][1][xs] = 255;
    for (cube, 0..) |plane, x| {
        for (plane, 0..) |row, y| {
            w.printf("cube[{d}][{d}] = {d} {d} {d}\n", .{ x, y, row[0], row[1], row[2] });
        }
    }
    // Diagonal and anti-diagonal.
    var d1: u32 = 0;
    var d2: u32 = 0;
    for (0..3) |i| {
        d1 += cube[i][i][i];
        d2 += cube[i][2 - i][i];
    }
    w.printf("diagonals {d} {d}\n", .{ d1, d2 });
}

/// `a` repeated `n` times, which is what the `**` operator used to be.
fn repeat(comptime a: anytype, comptime n: usize) [a.len * n]@TypeOf(a[0]) {
    const out = comptime blk: {
        var o: [a.len * n]@TypeOf(a[0]) = undefined;
        for (0..n) |i| @memcpy(o[i * a.len ..][0..a.len], &a);
        break :blk o;
    };
    return out;
}

fn comptimeArrays() void {
    const a = [_]u8{ 1, 2, 3 };
    const b = [_]u8{ 9, 8 };
    const ab = a ++ b;
    const rep = repeat(a, 3);
    const pat = repeat([_]i16{ -1, 0, 1 }, 4) ++ [_]i16{42};
    dumpU8("a++b", &ab);
    dumpU8("repeat(a,3)", &rep);
    w.printf("pat len={d}:", .{pat.len});
    for (pat) |v| w.printf(" {d}", .{v});
    w.puts("\n");
    const greet = "Hel" ++ "lo" ++ repeat("!".*, 3);
    w.printf("greet {s} len={d}\n", .{ greet, greet.len });
    // Run-time copies of comptime arrays, then mutated.
    var m = rep;
    m[w.hide(@as(usize, 4))] = 200;
    dumpU8("m", &m);
    dumpU8("rep unchanged", &rep);
    const zeros: [8]u32 = @splat(0);
    var z = zeros;
    for (&z, 0..) |*v, i| v.* = @intCast(i * i * w.hide(@as(usize, 3)));
    w.printf("z:", .{});
    for (z) |v| w.printf(" {d}", .{v});
    w.puts("\n");
}

fn copies() void {
    var buf: [24]u8 = undefined;
    for (&buf, 0..) |*b, i| b.* = @intCast(i);
    const n = w.hide(@as(usize, 8));
    // Non-overlapping copy.
    @memcpy(buf[16..][0..n], buf[0..n]);
    dumpU8("memcpy", &buf);
    // Overlapping, forward: dest below source.
    for (&buf, 0..) |*b, i| b.* = @intCast(i);
    std.mem.copyForwards(u8, buf[2..20], buf[5..23]);
    dumpU8("copyForwards", &buf);
    // Overlapping, backward: dest above source.
    for (&buf, 0..) |*b, i| b.* = @intCast(i);
    std.mem.copyBackwards(u8, buf[5..23], buf[2..20]);
    dumpU8("copyBackwards", &buf);
    // @memmove in both directions.
    for (&buf, 0..) |*b, i| b.* = @intCast(i + 100);
    @memmove(buf[w.hide(@as(usize, 1))..][0..10], buf[4..14]);
    dumpU8("memmove down", &buf);
    for (&buf, 0..) |*b, i| b.* = @intCast(i + 100);
    @memmove(buf[w.hide(@as(usize, 9))..][0..12], buf[3..15]);
    dumpU8("memmove up", &buf);
    // @memset on a runtime-length slice of a wider type.
    var wide: [12]u64 = undefined;
    @memset(&wide, 0x0102030405060708);
    @memset(wide[w.hide(@as(usize, 3))..w.hide(@as(usize, 9))], 0xffff_ffff_ffff_ffff);
    w.puts("wide:");
    for (wide) |v| w.printf(" {x}", .{v});
    w.puts("\n");
    // Copying a struct array.
    const P = struct { x: i16, y: i16, tag: u8 };
    var ps: [6]P = undefined;
    for (&ps, 0..) |*p, i| p.* = .{ .x = @intCast(i), .y = -@as(i16, @intCast(i * 3)), .tag = @intCast('a' + i) };
    var qs: [6]P = undefined;
    @memcpy(&qs, &ps);
    std.mem.reverse(P, &qs);
    for (ps, qs) |p, q| w.printf("p=({d},{d},{c}) q=({d},{d},{c})\n", .{ p.x, p.y, p.tag, q.x, q.y, q.tag });
}

fn manyPointers() void {
    var arr = [_]u32{ 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
    const base: [*]u32 = &arr;
    var p = base + w.hide(@as(usize, 2));
    w.printf("p[0]={d} p[1]={d}\n", .{ p[0], p[1] });
    p += 3;
    p[0] += 5;
    w.printf("p[0]={d} back[-1]={d}\n", .{ p[0], (p - 1)[0] });
    const s = p[0..4];
    w.printf("p[0..4] len={d} sum={d}\n", .{ s.len, s[0] + s[1] + s[2] + s[3] });
    const diff = (@intFromPtr(p) - @intFromPtr(base)) / @sizeOf(u32);
    w.printf("distance {d}\n", .{diff});
    // A walk with a stride through a many-pointer.
    var q: [*]const u32 = &arr;
    var k: usize = 0;
    while (k < 5) : (k += 1) {
        w.printf("stride {d}: {d}\n", .{ k, q[0] });
        q += 2;
    }
    // Single-item pointer to an array element, and back to a slice.
    const one: *u32 = &arr[w.hide(@as(usize, 7))];
    one.* = 777;
    const as_slice: []u32 = one[0..1];
    as_slice[0] += 1;
    w.puts("arr:");
    for (arr) |v| w.printf(" {d}", .{v});
    w.puts("\n");
}

fn compare() void {
    var a: [16]u8 = undefined;
    var b: [16]u8 = undefined;
    for (&a, &b, 0..) |*x, *y, i| {
        x.* = @intCast(i * 3);
        y.* = @intCast(i * 3);
    }
    var i: usize = 0;
    while (i < 17) : (i += 3) {
        const lim = @min(i, 16);
        if (lim < 16) b[lim] +%= 1;
        w.printf("eql[{d}]={} order={s} prefix(8)={} diff={?d}\n", .{
            i,
            std.mem.eql(u8, &a, &b),
            @tagName(std.mem.order(u8, &a, &b)),
            std.mem.eql(u8, a[0..8], b[0..8]),
            std.mem.indexOfDiff(u8, &a, &b),
        });
        if (lim < 16) b[lim] -%= 1;
    }
    const x = [_]i32{ 1, 2, 3, 4 };
    const y = [_]i32{ 1, 2, 3, 4 };
    const z = [_]i32{ 1, 2, 3 };
    w.printf("i32 eql x,y={} x,z={} lessThan z,x={}\n", .{ std.mem.eql(i32, &x, &y), std.mem.eql(i32, &x, &z), std.mem.lessThan(i32, &z, &x) });
}

pub fn main() void {
    copySemantics();
    runtimeSlicing();
    sentinels();
    multiDim();
    comptimeArrays();
    copies();
    manyPointers();
    compare();
    w.puts("done\n");
}
