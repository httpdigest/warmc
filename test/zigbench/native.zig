//! wasi.zig's two functions for the kernels' native build (test/gobench.py
//! --native): the same output, written with libc's write instead of WASI's
//! fd_write.
extern "c" fn write(fd: c_int, buf: [*]const u8, n: usize) isize;

pub fn puts(s: []const u8) void {
    _ = write(1, s.ptr, s.len);
}

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
