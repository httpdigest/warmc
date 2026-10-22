//! stdout for the Zig correctness tests.
//!
//! The same one-import writer as test/zigbench/wasi.zig: `fd_write` on fd 1,
//! and a decimal formatter by hand, so the lowest layer of every test depends
//! only on the language and on `wasi_snapshot_preview1`.  On top of it, for
//! these tests and not the benchmarks, `printf` formats with std's formatter
//! (`std.mem.print`, which `std.fmt.bufPrint` became) into a stack buffer and
//! writes the result.  The tests print hundreds of lines each, and a miscompile
//! anywhere shows as a line that differs from wasmtime's.
const std = @import("std");

const Iovec = extern struct { buf: [*]const u8, len: usize };
extern "wasi_snapshot_preview1" fn fd_write(fd: i32, iovs: [*]const Iovec,
                                            n: usize, written: *usize) u16;

pub fn puts(s: []const u8) void {
    const io = [1]Iovec{.{ .buf = s.ptr, .len = s.len }};
    var w: usize = 0;
    _ = fd_write(1, &io, 1, &w);
}

/// A label and an unsigned decimal, formatted without std.
pub fn print(label: []const u8, v: u64) void {
    var buf: [24]u8 = undefined;
    var i: usize = buf.len;
    var n = v;
    if (n == 0) {
        i -= 1;
        buf[i] = '0';
    }
    while (n != 0) {
        i -= 1;
        buf[i] = @intCast('0' + n % 10);
        n /= 10;
    }
    puts(label);
    puts(buf[i..]);
    puts("\n");
}

/// A label and a signed decimal, formatted without std.
pub fn printi(label: []const u8, v: i64) void {
    if (v < 0) {
        puts(label);
        // The magnitude of minInt(i64) does not fit an i64, but it does a u64.
        print("-", @as(u64, @bitCast(-%v)));
    } else print(label, @intCast(v));
}

/// `v`, but through a volatile store and load, so the optimiser cannot know
/// it.  A test's inputs go through this; otherwise LLVM folds most of a small
/// test at compile time and the wasm left to check is a list of constants.
pub fn hide(v: anytype) @TypeOf(v) {
    var x = v;
    const p: *volatile @TypeOf(v) = &x;
    return p.*;
}

/// std's formatter into a stack buffer, then out.  A line too long for the
/// buffer is reported as such rather than cut, so it cannot pass silently.
pub fn printf(comptime fmt: []const u8, args: anytype) void {
    var buf: [4096]u8 = undefined;
    const s = std.mem.print(&buf, fmt, args) catch {
        puts("<printf overflow>\n");
        return;
    };
    puts(s);
}
