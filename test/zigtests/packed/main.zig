// Packed structs, packed unions and extern structs: bit fields and C layout.
//
// A packed struct is one integer -- u16, u32, u64 or u128 here -- and each
// field read is a shift and a mask of it, each field write a
// read-modify-write that clears the field's bits and ors the new ones in.
// Fields are odd widths (u3, i5, u9, i7, u12, bool, an enum(u2)) that start at
// bit offsets that are not byte boundaries, signed fields need sign-extension
// on the way out, and a packed struct(u128) puts fields across the two i64
// halves.  The program builds values field by field, converts them to and
// from their backing integers with @bitCast and @backingInt/@fromBackingInt,
// writes fields through pointers, compares whole packed structs with `==`,
// keeps arrays of them, and reads packed unions both ways.  extern structs are
// laid out the C way, so their @offsetOf/@sizeOf are printed and their bytes
// are viewed through std.mem.asBytes.  Every field of every value prints, so a
// mask that is one bit off or a missing sign-extension shows.
const std = @import("std");
const w = @import("wasi");

const Mode = enum(u2) { off, low, high, auto };

const Ctrl = packed struct(u32) {
    enable: bool,
    mode: Mode,
    level: u3,
    offset: i5,
    count: u9,
    delta: i7,
    spare: u5,
};

const Wide = packed struct(u64) {
    a: u13,
    b: i19,
    c: u1,
    d: i31,
};

const Huge = packed struct(u128) {
    lo: u60,
    mid: i8,
    hi: u59,
    flag: bool,
};

const Pixel = packed struct(u16) {
    b: u5,
    g: u6,
    r: u5,
};

const Inferred = packed struct {
    x: u4,
    y: i4,
    z: bool,
    q: u7,
};

const Num = packed union {
    u: u32,
    i: i32,
    f: f32,
    c: Ctrl,
};

const CHeader = extern struct {
    magic: u32,
    version: u16,
    flags: u8,
    kind: u8,
    length: u64,
    crc: u32,
    tail: u8,
};

const CPair = extern struct {
    a: u8,
    b: f64,
    c: u16,
    d: [3]u8,
    e: i32,
};

fn showCtrl(tag: []const u8, c: Ctrl) void {
    w.printf("{s}: enable={} mode={s} level={d} offset={d} count={d} delta={d} spare={d} raw={x:0>8}\n", .{
        tag, c.enable, @tagName(c.mode), c.level, c.offset, c.count, c.delta, c.spare, @as(u32, @bitCast(c)),
    });
}

fn showWide(tag: []const u8, v: Wide) void {
    w.printf("{s}: a={d} b={d} c={d} d={d} raw={x:0>16}\n", .{ tag, v.a, v.b, v.c, v.d, @as(u64, @bitCast(v)) });
}

fn showHuge(tag: []const u8, v: Huge) void {
    w.printf("{s}: lo={x} mid={d} hi={x} flag={} raw={x:0>32}\n", .{ tag, v.lo, v.mid, v.hi, v.flag, @as(u128, @bitCast(v)) });
}

fn bumpCtrl(c: *Ctrl) void {
    c.level +%= 3;
    c.offset -%= 7;
    c.count +%= 100;
    c.delta = -%c.delta;
    c.mode = @enumFromInt((@intFromEnum(c.mode) +% 1));
    c.enable = !c.enable;
}

fn levelPtr(c: *Ctrl) *align(4:3:4) u3 {
    return &c.level;
}

pub fn main() void {
    // From raw bits: every field is a slice of the word.
    var raw: u32 = w.hide(@as(u32, 0x9e3779b9));
    for (0..12) |i| {
        const c: Ctrl = @bitCast(raw);
        var buf: [16]u8 = undefined;
        showCtrl(std.mem.print(&buf, "ctrl{d}", .{i}) catch "?", c);
        const back: u32 = @backingInt(c);
        if (back != raw) w.puts("  MISMATCH backing\n");
        raw = raw *% 0x01000193 ^ (raw >> 11);
    }

    // Field by field, then through pointers.
    var c: Ctrl = .{
        .enable = w.hide(true),
        .mode = w.hide(Mode.high),
        .level = w.hide(@as(u3, 5)),
        .offset = w.hide(@as(i5, -16)),
        .count = w.hide(@as(u9, 511)),
        .delta = w.hide(@as(i7, 63)),
        .spare = 0,
    };
    showCtrl("built", c);
    for (0..8) |i| {
        bumpCtrl(&c);
        var buf: [16]u8 = undefined;
        showCtrl(std.mem.print(&buf, "bump{d}", .{i}) catch "?", c);
    }
    const lp = levelPtr(&c);
    lp.* = w.hide(@as(u3, 2));
    showCtrl("via-bitptr", c);
    const cp = &c.count;
    cp.* = w.hide(@as(u9, 257));
    showCtrl("via-countptr", c);

    var c2 = c;
    w.printf("eq same={} ", .{c == c2});
    c2.spare = 1;
    w.printf("eq after spare={} ", .{c == c2});
    c2.spare = 0;
    w.printf("eq restored={}\n", .{c == c2});

    // Wide and Huge cross word boundaries.
    var wv: Wide = .{ .a = w.hide(@as(u13, 8191)), .b = w.hide(@as(i19, -262144)), .c = 1, .d = w.hide(@as(i31, -1)) };
    showWide("wide0", wv);
    for (0..8) |i| {
        wv.a +%= 1234;
        wv.b +%= 99999;
        wv.c ^= 1;
        wv.d = wv.d *% -3 +% 17;
        var buf: [16]u8 = undefined;
        showWide(std.mem.print(&buf, "wide{d}", .{i + 1}) catch "?", wv);
    }
    var hv: Huge = @bitCast(w.hide(@as(u128, 0x0123456789abcdef_fedcba9876543210)));
    showHuge("huge0", hv);
    for (0..6) |i| {
        hv.lo = hv.lo *% 3 +% 1;
        hv.mid = hv.mid -% 77;
        hv.hi = (hv.hi << 5) | (hv.hi >> 54);
        hv.flag = !hv.flag;
        var buf: [16]u8 = undefined;
        showHuge(std.mem.print(&buf, "huge{d}", .{i + 1}) catch "?", hv);
    }
    w.printf("sizes Ctrl={d} Wide={d} Huge={d} Pixel={d} Inferred={d}/{d}bits Num={d}\n", .{
        @sizeOf(Ctrl), @sizeOf(Wide), @sizeOf(Huge), @sizeOf(Pixel), @sizeOf(Inferred), @bitSizeOf(Inferred), @sizeOf(Num),
    });
    inline for (.{ "enable", "mode", "level", "offset", "count", "delta", "spare" }) |f| {
        w.printf("Ctrl.{s} bitoffset={d}\n", .{ f, @bitOffsetOf(Ctrl, f) });
    }

    // An array of 16-bit pixels: blend and convert.
    var px: [12]Pixel = undefined;
    var x: u16 = w.hide(@as(u16, 0xbeef));
    for (&px) |*p| {
        x = x *% 25173 +% 13849;
        p.* = @bitCast(x);
    }
    for (px, 0..) |p, i| {
        const r8: u8 = (@as(u8, p.r) << 3) | (p.r >> 2);
        const g8: u8 = (@as(u8, p.g) << 2) | (p.g >> 4);
        const b8: u8 = (@as(u8, p.b) << 3) | (p.b >> 2);
        const gray: u8 = @intCast((@as(u16, r8) * 77 + @as(u16, g8) * 150 + @as(u16, b8) * 29) >> 8);
        w.printf("px[{d}] {x:0>4} r={d} g={d} b={d} rgb888={x:0>2}{x:0>2}{x:0>2} gray={d}\n", .{ i, @as(u16, @bitCast(p)), p.r, p.g, p.b, r8, g8, b8, gray });
    }
    for (px[0 .. px.len - 1], px[1..], 0..) |a, b, i| {
        const m: Pixel = .{ .r = @intCast((@as(u6, a.r) + b.r) / 2), .g = @intCast((@as(u7, a.g) + b.g) / 2), .b = @intCast((@as(u6, a.b) + b.b) / 2) };
        w.printf("blend[{d}] {x:0>4}\n", .{ i, @as(u16, @bitCast(m)) });
    }

    var inf: Inferred = @bitCast(w.hide(@as(u16, 0x5a5a)));
    for (0..6) |i| {
        w.printf("inferred[{d}] x={d} y={d} z={} q={d} backing={x}\n", .{ i, inf.x, inf.y, inf.z, inf.q, @as(u16, @bitCast(inf)) });
        inf.x +%= 7;
        inf.y -%= 3;
        inf.z = !inf.z;
        inf.q = inf.q *% 5;
    }

    const nums = [_]u32{ 0, 1, 0x80000000, 0x3f800000, 0xbf800000, 0x7f800000, 0xffffffff, 0x12345678 };
    for (nums) |n0| {
        const n: Num = .{ .u = w.hide(n0) };
        w.printf("num u={x:0>8} i={d} f={e} ctrl.level={d} ctrl.count={d}\n", .{ n.u, n.i, n.f, n.c.level, n.c.count });
    }
    const nf: Num = .{ .f = w.hide(@as(f32, -0.15625)) };
    w.printf("num from f: u={x:0>8} i={d}\n", .{ nf.u, nf.i });
    const nc: Num = .{ .c = c };
    w.printf("num from ctrl: u={x:0>8}\n", .{nc.u});

    // extern structs: C layout, padding included.
    inline for (.{ CHeader, CPair }) |T| {
        w.printf("{s}: size={d} align={d}\n", .{ @typeName(T), @sizeOf(T), @alignOf(T) });
        inline for (@typeInfo(T).@"struct".field_names) |f| {
            w.printf("  {s} @ {d}\n", .{ f, @offsetOf(T, f) });
        }
    }
    var hdr: CHeader = undefined;
    @memset(std.mem.asBytes(&hdr), 0);
    hdr.magic = w.hide(@as(u32, 0x464c457f));
    hdr.version = w.hide(@as(u16, 0x0102));
    hdr.flags = 0xa5;
    hdr.kind = 7;
    hdr.length = w.hide(@as(u64, 0x1122334455667788));
    hdr.crc = 0xdeadbeef;
    hdr.tail = 0x5c;
    const bytes = std.mem.asBytes(&hdr);
    w.puts("header bytes:");
    for (bytes) |b| w.printf(" {x:0>2}", .{b});
    w.puts("\n");
    var copy: CHeader = undefined;
    @memcpy(std.mem.asBytes(&copy), bytes);
    copy.length +%= 1;
    w.printf("copy magic={x} version={x} length={x} crc={x} tail={x}\n", .{ copy.magic, copy.version, copy.length, copy.crc, copy.tail });

    var pairs: [4]CPair = undefined;
    for (&pairs, 0..) |*p, i| {
        @memset(std.mem.asBytes(p), 0);
        // Field by field, so the padding keeps the zeros just written.
        p.a = @intCast(i);
        p.b = w.hide(@as(f64, @floatFromInt(i))) * 1.5;
        p.c = @intCast(i * 1000);
        p.d = .{ 1, 2, @intCast(i) };
        p.e = -@as(i32, @intCast(i));
    }
    const pbytes = std.mem.sliceAsBytes(&pairs);
    var h: u32 = 2166136261;
    for (pbytes) |b| h = (h ^ b) *% 16777619;
    w.printf("pairs bytes={d} fnv={x:0>8}\n", .{ pbytes.len, h });
}
