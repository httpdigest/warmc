// Checksums, hashes and encodings from the standard library, all pure Go
// under TinyGo: CRC-32 (IEEE and Castagnoli, table-driven and slicing-by-8),
// Adler-32, FNV-1 and FNV-1a at 32, 64 and 128 bits, MD5, SHA-1, SHA-256,
// SHA-224, SHA-512 and SHA-384 (rotations, 32- and 64-bit additions, byte
// swaps), incremental hashing in uneven chunks checked against one-shot
// sums, HMAC built by hand from SHA-256, and the encodings hex (with Dump),
// base64 (standard, URL, raw), base32, and encoding/binary's fixed-width and
// varint forms in both byte orders.  Inputs are generated, of lengths around
// the block sizes.
package main

import (
	"crypto/md5"
	"crypto/sha1"
	"crypto/sha256"
	"crypto/sha512"
	"encoding/base32"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"hash"
	"hash/adler32"
	"hash/crc32"
	"hash/fnv"
	"os"
)

var step byte = 7

func init() {
	if len(os.Args) > 1000 {
		step = 3
	}
}

func data(n int) []byte {
	b := make([]byte, n)
	x := byte(1)
	for i := range b {
		x = x*step + byte(i>>3)
		b[i] = x
	}
	return b
}

func hmacSHA256(key, msg []byte) []byte {
	const block = 64
	if len(key) > block {
		k := sha256.Sum256(key)
		key = k[:]
	}
	ipad := make([]byte, block)
	opad := make([]byte, block)
	copy(ipad, key)
	copy(opad, key)
	for i := range ipad {
		ipad[i] ^= 0x36
		opad[i] ^= 0x5c
	}
	inner := sha256.New()
	inner.Write(ipad)
	inner.Write(msg)
	outer := sha256.New()
	outer.Write(opad)
	outer.Write(inner.Sum(nil))
	return outer.Sum(nil)
}

func main() {
	lengths := []int{0, 1, 3, 55, 56, 63, 64, 65, 111, 112, 127, 128, 129, 1000, 4099}
	castagnoli := crc32.MakeTable(crc32.Castagnoli)
	koopman := crc32.MakeTable(crc32.Koopman)
	fmt.Println("-- checksums")
	for _, n := range lengths {
		d := data(n)
		f32, f32a := fnv.New32(), fnv.New32a()
		f64, f64a := fnv.New64(), fnv.New64a()
		f128a := fnv.New128a()
		for _, h := range []hash.Hash{f32, f32a, f64, f64a, f128a} {
			h.Write(d)
		}
		fmt.Printf("%4d crc=%08x cast=%08x koop=%08x adler=%08x fnv=%08x %08x %016x %016x %x\n", n,
			crc32.ChecksumIEEE(d), crc32.Checksum(d, castagnoli), crc32.Checksum(d, koopman),
			adler32.Checksum(d), f32.Sum32(), f32a.Sum32(), f64.Sum64(), f64a.Sum64(), f128a.Sum(nil))
	}
	fmt.Println("-- digests")
	for _, n := range lengths {
		d := data(n)
		fmt.Printf("%4d md5=%x sha1=%x\n", n, md5.Sum(d), sha1.Sum(d))
		fmt.Printf("     sha224=%x sha256=%x\n", sha256.Sum224(d), sha256.Sum256(d))
		fmt.Printf("     sha384=%x\n     sha512=%x\n", sha512.Sum384(d), sha512.Sum512(d))
	}
	fmt.Println("-- incremental equals one-shot")
	d := data(5000)
	hs := map[string]func() hash.Hash{
		"md5": md5.New, "sha1": sha1.New, "sha256": sha256.New, "sha512": sha512.New,
		"crc32": func() hash.Hash { return crc32.NewIEEE() }, "adler": func() hash.Hash { return adler32.New() },
	}
	for _, name := range []string{"md5", "sha1", "sha256", "sha512", "crc32", "adler"} {
		one := hs[name]()
		one.Write(d)
		want := one.Sum(nil)
		inc := hs[name]()
		pos, chunk := 0, 1
		for pos < len(d) {
			end := min(pos+chunk, len(d))
			inc.Write(d[pos:end])
			pos = end
			chunk = chunk*3%197 + 1
		}
		got := inc.Sum(nil)
		inc.Reset()
		inc.Write(d[:100])
		fmt.Printf("%s %v %x %d %d\n", name, string(got) == string(want), inc.Sum(nil), inc.Size(), inc.BlockSize())
	}
	fmt.Println("-- HMAC")
	for _, kl := range []int{0, 16, 64, 100} {
		fmt.Printf("%d %x\n", kl, hmacSHA256(data(kl), []byte("The quick brown fox jumps over the lazy dog")))
	}
	fmt.Println("-- hex")
	h := hex.EncodeToString(data(20))
	back, err := hex.DecodeString(h)
	fmt.Println(h, string(back) == string(data(20)), err)
	_, err = hex.DecodeString("abc")
	fmt.Println(err)
	_, err = hex.DecodeString("zz")
	fmt.Println(err)
	// hex.Dump and hex.Dumper write through an io.Writer other than
	// os.Stdout, which pulls timer code into the module that TinyGo cannot
	// build without a scheduler; this is the same layout by hand.
	dd := data(40)
	for off := 0; off < len(dd); off += 16 {
		row := dd[off:min(off+16, len(dd))]
		text := []byte(string(row))
		for i, c := range text {
			if c < 32 || c > 126 {
				text[i] = '.'
			}
		}
		fmt.Printf("%08x  %-48s |%s|\n", off, hex.EncodeToString(row), text)
	}
	fmt.Println("-- base64 and base32")
	for _, n := range []int{0, 1, 2, 3, 4, 5, 20, 57} {
		d := data(n)
		std := base64.StdEncoding.EncodeToString(d)
		url := base64.URLEncoding.EncodeToString(d)
		raw := base64.RawStdEncoding.EncodeToString(d)
		b32 := base32.StdEncoding.EncodeToString(d)
		hx32 := base32.HexEncoding.WithPadding(base32.NoPadding).EncodeToString(d)
		dec, err := base64.StdEncoding.DecodeString(std)
		dec32, err32 := base32.StdEncoding.DecodeString(b32)
		fmt.Printf("%d %s %s %s %s %s %v %v %v %v\n", n, std, url, raw, b32, hx32,
			string(dec) == string(d), err, string(dec32) == string(d), err32)
	}
	_, err = base64.StdEncoding.DecodeString("not*base64")
	fmt.Println(err)
	fmt.Println("-- binary")
	buf := make([]byte, 8)
	for _, v := range []uint64{0, 1, 0x0102030405060708, 0xfedcba9876543210, 1<<63 + 1} {
		binary.BigEndian.PutUint64(buf, v)
		be := fmt.Sprintf("% x", buf)
		binary.LittleEndian.PutUint64(buf, v)
		le := fmt.Sprintf("% x", buf)
		fmt.Println(be, "|", le, "|", binary.BigEndian.Uint32(buf), binary.LittleEndian.Uint16(buf[6:]),
			binary.BigEndian.Uint64(buf) == v, binary.LittleEndian.Uint64(buf) == v)
	}
	var out []byte
	for _, v := range []int64{0, 1, -1, 63, -64, 64, 300, -300, 1 << 40, -1 << 62, 1<<63 - 1} {
		out = binary.AppendVarint(out, v)
	}
	for _, v := range []uint64{0, 127, 128, 16383, 16384, 1<<64 - 1} {
		out = binary.AppendUvarint(out, v)
	}
	fmt.Printf("%d % x\n", len(out), out)
	p := out
	for i := 0; i < 11; i++ {
		v, n := binary.Varint(p)
		fmt.Print(v, "/", n, " ")
		p = p[n:]
	}
	for i := 0; i < 6; i++ {
		v, n := binary.Uvarint(p)
		fmt.Print(v, "/", n, " ")
		p = p[n:]
	}
	fmt.Println(len(p))
	be := binary.BigEndian.AppendUint32(nil, 0xdeadbeef)
	be = binary.LittleEndian.AppendUint16(be, 0x1234)
	be = binary.BigEndian.AppendUint64(be, 42)
	fmt.Printf("% x %d\n", be, binary.PutUvarint(buf, 1<<35))
}
