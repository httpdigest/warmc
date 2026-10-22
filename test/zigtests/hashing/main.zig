// std's hash functions and a selection of its cryptography, over inputs of
// every length from 0 to 130 bytes and at every alignment.
//
// Hashes are the densest integer code a program carries: 32- and 64-bit
// rotates (`i32.rotl`, `i64.rotr`), multiplies keeping the high half
// (Wyhash's 64x64->128 `mum` through `__multi3`), little-endian loads at any
// alignment, and tails of 1..7 bytes assembled by shifts.  Every function here
// is run over the same message sliced at offsets 0..7 into a buffer, so the
// unaligned-load paths and the tail paths are all taken, and each digest is
// printed in full.  The non-cryptographic ones: Adler32, CRC-32 and a few
// other CRC widths from std.hash.crc's catalogue (u3, u5, u16, u64),
// FNV-1a at 32/64/128 bits, Murmur2/3, CityHash32/64, Wyhash, XxHash32/64/3,
// SipHash64/128.  The cryptographic ones: MD5, SHA-1, SHA-256, SHA-512,
// SHA3-256, BLAKE2s/2b, BLAKE3, HMAC-SHA256, ChaCha20 keystream, Poly1305,
// and an AES-128-GCM / ChaCha20-Poly1305 round trip whose tag is checked and
// then broken by flipping one bit.  The known-answer vectors for "abc" are
// printed first, so the output can be checked against the standards.
const std = @import("std");
const w = @import("wasi");

var storage: [256]u8 align(16) = undefined;

fn hex(bytes: []const u8) void {
    for (bytes) |b| w.printf("{x:0>2}", .{b});
}

fn line(name: []const u8, digest: []const u8) void {
    w.printf("  {s:<10} ", .{name});
    hex(digest);
    w.puts("\n");
}

fn cryptoHashes(msg: []const u8) void {
    const h = std.crypto.hash;
    var md5: [16]u8 = undefined;
    h.Md5.hash(msg, &md5, .{});
    line("md5", &md5);
    var sha1: [20]u8 = undefined;
    h.Sha1.hash(msg, &sha1, .{});
    line("sha1", &sha1);
    var s256: [32]u8 = undefined;
    h.sha2.Sha256.hash(msg, &s256, .{});
    line("sha256", &s256);
    var s512: [64]u8 = undefined;
    h.sha2.Sha512.hash(msg, &s512, .{});
    line("sha512", &s512);
    var s3: [32]u8 = undefined;
    h.sha3.Sha3_256.hash(msg, &s3, .{});
    line("sha3-256", &s3);
    var b2s: [32]u8 = undefined;
    h.blake2.Blake2s256.hash(msg, &b2s, .{});
    line("blake2s", &b2s);
    var b2b: [64]u8 = undefined;
    h.blake2.Blake2b512.hash(msg, &b2b, .{});
    line("blake2b", &b2b);
    var b3: [32]u8 = undefined;
    h.Blake3.hash(msg, &b3, .{});
    line("blake3", &b3);
    var mac: [32]u8 = undefined;
    std.crypto.auth.hmac.sha2.HmacSha256.create(&mac, msg, "key");
    line("hmac256", &mac);
}

fn fastHashes(msg: []const u8, seed: u64) [16]u64 {
    const H = std.hash;
    const key: [16]u8 = .{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    return .{
        H.Adler32.hash(msg),
        H.Crc32.hash(msg),
        H.crc.@"CRC-16/ARC".hash(msg),
        H.crc.@"CRC-64/XZ".hash(msg),
        H.crc.@"CRC-3/GSM".hash(msg),
        H.crc.@"CRC-5/USB".hash(msg),
        H.Fnv1a_32.hash(msg),
        H.Fnv1a_64.hash(msg),
        @truncate(H.Fnv1a_128.hash(msg) >> 32),
        H.Murmur2_64.hash(msg) ^ H.Murmur3_32.hash(msg),
        H.CityHash64.hash(msg) ^ H.CityHash32.hash(msg),
        H.Wyhash.hash(seed, msg),
        H.XxHash64.hash(seed, msg),
        H.XxHash32.hash(@truncate(seed), msg),
        H.XxHash3.hash(seed, msg),
        H.SipHash64(2, 4).toInt(msg, &key) ^ @as(u64, @truncate(H.SipHash128(1, 3).toInt(msg, &key))),
    };
}

const names = [16][]const u8{ "adler32", "crc32", "crc16arc", "crc64xz", "crc3gsm", "crc5usb", "fnv32", "fnv64", "fnv128mid", "murmur", "city", "wyhash", "xxh64", "xxh32", "xxh3", "sip" };

fn streams() void {
    var key: [32]u8 = undefined;
    for (&key, 0..) |*k, i| k.* = @intCast(i * 7 + 1);
    const nonce12: [12]u8 = .{ 0, 0, 0, 9, 0, 0, 0, 0x4a, 0, 0, 0, 0 };
    var zeros: [150]u8 = @splat(0);
    var ks: [150]u8 = undefined;
    std.crypto.stream.chacha.ChaCha20IETF.xor(&ks, w.hide(&zeros), 1, key, nonce12);
    w.puts("chacha20 keystream:\n");
    var i: usize = 0;
    while (i < ks.len) : (i += 50) {
        w.puts("  ");
        hex(ks[i..@min(ks.len, i + 50)]);
        w.puts("\n");
    }
    // Decrypting is the same XOR.
    var back: [150]u8 = undefined;
    std.crypto.stream.chacha.ChaCha20IETF.xor(&back, &ks, 1, key, nonce12);
    w.printf("  round trip zero={}\n", .{std.mem.allEqual(u8, &back, 0)});

    var pkey: [32]u8 = undefined;
    for (&pkey, 0..) |*k, j| k.* = @intCast((j * 13 + 5) & 0xff);
    for ([_]usize{ 0, 1, 15, 16, 17, 33, 64, 100 }) |len| {
        var tag: [16]u8 = undefined;
        std.crypto.onetimeauth.Poly1305.create(&tag, storage[3 .. 3 + w.hide(len)], &pkey);
        w.printf("poly1305 len {d}: ", .{len});
        hex(&tag);
        w.puts("\n");
    }
}

fn aead(comptime A: type, name: []const u8) void {
    var key: [A.key_length]u8 = undefined;
    for (&key, 0..) |*k, i| k.* = @intCast(0x40 + i);
    var nonce: [A.nonce_length]u8 = undefined;
    for (&nonce, 0..) |*n, i| n.* = @intCast(0xa0 + i);
    const ad = "header bytes";
    for ([_]usize{ 0, 1, 16, 31, 64, 77 }) |len| {
        const m = storage[1 .. 1 + w.hide(len)];
        var c: [128]u8 = undefined;
        var tag: [A.tag_length]u8 = undefined;
        A.encrypt(c[0..len], &tag, m, ad, nonce, key);
        w.printf("{s} len {d}: tag ", .{ name, len });
        hex(&tag);
        w.puts(" ct ");
        hex(c[0..@min(len, 24)]);
        var p: [128]u8 = undefined;
        const ok = if (A.decrypt(p[0..len], c[0..len], tag, ad, nonce, key)) |_| std.mem.eql(u8, p[0..len], m) else |_| false;
        tag[len % A.tag_length] ^= 0x10;
        const forged = if (A.decrypt(p[0..len], c[0..len], tag, ad, nonce, key)) |_| "accepted" else |e| @errorName(e);
        w.printf(" decrypt={} forged={s}\n", .{ ok, forged });
    }
}

pub fn main() void {
    w.puts("known answers for \"abc\":\n");
    cryptoHashes(w.hide("abc"));

    // A message with structure, so every byte value and run shows up.
    for (&storage, 0..) |*b, i| b.* = @truncate(i *% 167 +% (i >> 3) *% 29 +% 11);

    for ([_]usize{ 0, 1, 3, 4, 7, 8, 15, 16, 17, 31, 32, 33, 55, 56, 63, 64, 65, 100, 127, 128, 130 }) |len| {
        w.printf("len {d}:\n", .{len});
        if (len == 0 or len == 3 or len == 64 or len == 130) cryptoHashes(storage[5 .. 5 + w.hide(len)]);
        var off: usize = 0;
        while (off < 8) : (off += 1) {
            const msg = storage[w.hide(off) .. off + len];
            const hs = fastHashes(msg, 0x1234_5678_9abc_def0 + @as(u64, off));
            if (off == 0 or off == 3 or off == 7) {
                for (hs, names) |hv, n| w.printf("  off {d} {s:<9} {x:0>16}\n", .{ off, n, hv });
            } else {
                var x: u64 = 0;
                for (hs) |hv| x = (x ^ hv) *% 0x100000001b3;
                w.printf("  off {d} combined {x:0>16}\n", .{ off, x });
            }
        }
    }

    // Streaming in pieces must agree with hashing in one go.
    var whole: [32]u8 = undefined;
    std.crypto.hash.sha2.Sha256.hash(storage[0..200], &whole, .{});
    for ([_]usize{ 1, 7, 63, 64, 65, 199 }) |step| {
        var st = std.crypto.hash.sha2.Sha256.init(.{});
        var at: usize = 0;
        while (at < 200) : (at += step) st.update(storage[at..@min(200, at + w.hide(step))]);
        var d: [32]u8 = undefined;
        st.final(&d);
        var x = std.hash.XxHash64.init(7);
        var crc = std.hash.Crc32.init();
        at = 0;
        while (at < 200) : (at += step) {
            x.update(storage[at..@min(200, at + step)]);
            crc.update(storage[at..@min(200, at + step)]);
        }
        w.printf("sha256 in steps of {d}: same={} xxh64={x} crc32={x}\n", .{ step, std.mem.eql(u8, &d, &whole), x.final(), crc.final() });
    }
    w.printf("one-shot xxh64={x} crc32={x}\n", .{ std.hash.XxHash64.hash(7, storage[0..200]), std.hash.Crc32.hash(storage[0..200]) });

    streams();
    aead(std.crypto.aead.aes_gcm.Aes128Gcm, "aes128gcm");
    aead(std.crypto.aead.chacha_poly.ChaCha20Poly1305, "chachapoly");
    w.puts("done\n");
}
