//! stdout for the Zig kernels, without the standard library.
//!
//! One WASI import and a decimal formatter by hand.  `std.Io` was rewritten in
//! 0.16 and `std.io.getStdOut` no longer exists, so a corpus that printed
//! through it would need editing on every Zig release -- and a benchmark that
//! stops building is a benchmark nobody runs.  This depends on the language and
//! on `wasi_snapshot_preview1`, and neither of those moves.
//!
//! The same reason test/asbench has its own: see that file's header.
const Iovec = extern struct { buf: [*]const u8, len: usize };
extern "wasi_snapshot_preview1" fn fd_write(fd: i32, iovs: [*]const Iovec,
                                            n: usize, written: *usize) u16;

pub fn puts(s: []const u8) void {
    const io = [1]Iovec{.{ .buf = s.ptr, .len = s.len }};
    var w: usize = 0;
    _ = fd_write(1, &io, 1, &w);
}

/// A label and an unsigned decimal.  Every kernel prints exactly one of these,
/// so warmc's output and wasmtime's are comparable as whole lines.
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
