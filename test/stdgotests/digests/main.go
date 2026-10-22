// Checksums and cryptographic hashes of fixed inputs: hash/crc32 (IEEE,
// Castagnoli, Koopman, incremental Update), hash/crc64 (ISO, ECMA),
// hash/adler32, hash/fnv (32, 32a, 64, 64a, 128, 128a), crypto/md5,
// crypto/sha1, crypto/sha256 and sha224, crypto/sha512 and its truncations,
// crypto/sha3 and SHAKE, crypto/hmac, crypto/hkdf and crypto/pbkdf2 -- each
// over the standard test vectors plus inputs at every length around the
// block size, written in one call and in uneven pieces, with Sum appended to
// a prefix and BinaryMarshaler state saved and restored mid-stream.
//
// These are the tightest integer loops in the standard library: 32-bit
// rotates and adds (sha256, md5, sha1), 64-bit ones (sha512, keccak), and
// table-driven byte loops (crc).  On wasm there are no assembly versions, so
// the generic Go code is what runs.
package main

import (
	"crypto/hkdf"
	"crypto/hmac"
	"crypto/md5"
	"crypto/pbkdf2"
	"crypto/sha1"
	"crypto/sha256"
	"crypto/sha3"
	"crypto/sha512"
	"encoding"
	"encoding/hex"
	"fmt"
	"hash"
	"hash/adler32"
	"hash/crc32"
	"hash/crc64"
	"hash/fnv"
	"strings"
)

type namedHash struct {
	name string
	new  func() hash.Hash
}

func main() {
	vectors := []string{"", "a", "abc", "message digest", "abcdefghijklmnopqrstuvwxyz",
		"The quick brown fox jumps over the lazy dog",
		"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
		strings.Repeat("1234567890", 8)}
	hashes := []namedHash{
		{"md5", md5.New}, {"sha1", sha1.New}, {"sha224", sha256.New224}, {"sha256", sha256.New},
		{"sha384", sha512.New384}, {"sha512", sha512.New}, {"sha512/224", sha512.New512_224}, {"sha512/256", sha512.New512_256},
		{"sha3-224", func() hash.Hash { return sha3.New224() }}, {"sha3-256", func() hash.Hash { return sha3.New256() }},
		{"sha3-512", func() hash.Hash { return sha3.New512() }},
		{"fnv32", func() hash.Hash { return fnv.New32() }}, {"fnv32a", func() hash.Hash { return fnv.New32a() }},
		{"fnv64", func() hash.Hash { return fnv.New64() }}, {"fnv64a", func() hash.Hash { return fnv.New64a() }},
		{"fnv128", fnv.New128}, {"fnv128a", fnv.New128a},
		{"crc32", func() hash.Hash { return crc32.NewIEEE() }}, {"crc32c", func() hash.Hash { return crc32.New(crc32.MakeTable(crc32.Castagnoli)) }},
		{"crc32k", func() hash.Hash { return crc32.New(crc32.MakeTable(crc32.Koopman)) }},
		{"crc64iso", func() hash.Hash { return crc64.New(crc64.MakeTable(crc64.ISO)) }},
		{"crc64ecma", func() hash.Hash { return crc64.New(crc64.MakeTable(crc64.ECMA)) }},
		{"adler32", func() hash.Hash { return adler32.New() }},
	}
	for _, v := range vectors {
		label := v
		if len(label) > 20 {
			label = label[:17] + "..."
		}
		fmt.Printf("input %q (len %d)\n", label, len(v))
		for _, h := range hashes {
			d := h.new()
			d.Write([]byte(v))
			fmt.Printf("  %-10s %x\n", h.name, d.Sum(nil))
		}
	}

	// Every length 0..200 for the block-structured hashes: digest of digests.
	data := make([]byte, 300)
	for i := range data {
		data[i] = byte(i*131 + 7)
	}
	for _, h := range hashes[:11] {
		acc := h.new()
		for n := 0; n <= 200; n++ {
			d := h.new()
			d.Write(data[:n])
			acc.Write(d.Sum(nil))
		}
		fmt.Printf("lengths 0..200 %-10s %x\n", h.name, acc.Sum(nil))
	}

	// Chunked writes equal one-shot writes; Sum appends and does not reset.
	for _, h := range []namedHash{hashes[0], hashes[3], hashes[5], hashes[9], hashes[17], hashes[22]} {
		one := h.new()
		one.Write(data)
		chunked := h.new()
		for i, step := 0, 1; i < len(data); step = step%13 + 1 {
			end := min(i+step, len(data))
			chunked.Write(data[i:end])
			i = end
		}
		pre := chunked.Sum([]byte("prefix:"))
		again := chunked.Sum(nil)
		fmt.Printf("chunked %-10s equal=%v sumTwice=%v prefix=%q size=%d block=%d\n", h.name,
			string(one.Sum(nil)) == string(again), string(pre[7:]) == string(again), pre[:7], one.Size(), one.BlockSize())
		chunked.Reset()
		fmt.Printf("  after Reset %x\n", chunked.Sum(nil))
	}

	// Marshal state mid-stream and resume.
	for _, h := range []namedHash{hashes[0], hashes[1], hashes[3], hashes[5], hashes[17]} {
		d := h.new()
		d.Write(data[:77])
		state, err := d.(encoding.BinaryMarshaler).MarshalBinary()
		if err != nil {
			fmt.Println(h.name, "marshal error", err)
			continue
		}
		d2 := h.new()
		err = d2.(encoding.BinaryUnmarshaler).UnmarshalBinary(state)
		d.Write(data[77:])
		d2.Write(data[77:])
		fmt.Printf("resume %-8s err=%v state=%d bytes equal=%v %x\n", h.name, err, len(state), string(d.Sum(nil)) == string(d2.Sum(nil)), d2.Sum(nil))
	}

	// One-shot helpers.
	fmt.Printf("md5.Sum %x\nsha1.Sum %x\nsha256.Sum256 %x\nsha256.Sum224 %x\nsha512.Sum512 %x\nsha3.Sum256 %x\n",
		md5.Sum(data), sha1.Sum(data), sha256.Sum256(data), sha256.Sum224(data), sha512.Sum512(data), sha3.Sum256(data))
	fmt.Printf("crc32 ChecksumIEEE %08x Checksum(castagnoli) %08x Update %08x\n", crc32.ChecksumIEEE(data),
		crc32.Checksum(data, crc32.MakeTable(crc32.Castagnoli)), crc32.Update(crc32.ChecksumIEEE(data[:100]), crc32.IEEETable, data[100:]))
	fmt.Printf("crc64 Checksum %016x adler32 Checksum %08x\n", crc64.Checksum(data, crc64.MakeTable(crc64.ECMA)), adler32.Checksum(data))

	// SHAKE.
	sh := sha3.NewSHAKE128()
	sh.Write([]byte("shake it"))
	out := make([]byte, 40)
	sh.Read(out)
	fmt.Printf("SHAKE128 %x\n", out)
	fmt.Printf("SHAKE256 %x\n", sha3.SumSHAKE256([]byte("shake it"), 48))

	// HMAC (RFC 4231 test case 2 and others).
	for _, kv := range [][2]string{{"Jefe", "what do ya want for nothing?"}, {"key", "The quick brown fox jumps over the lazy dog"}, {strings.Repeat("k", 200), "long key gets hashed first"}} {
		for _, h := range []namedHash{hashes[0], hashes[1], hashes[3], hashes[5]} {
			m := hmac.New(h.new, []byte(kv[0]))
			m.Write([]byte(kv[1]))
			fmt.Printf("hmac-%-7s key=%-6.6q %x\n", h.name, kv[0], m.Sum(nil))
		}
	}
	m1 := hmac.New(sha256.New, []byte("k"))
	m1.Write([]byte("msg"))
	m2 := hmac.New(sha256.New, []byte("k"))
	m2.Write([]byte("msg"))
	fmt.Println("hmac.Equal", hmac.Equal(m1.Sum(nil), m2.Sum(nil)), hmac.Equal(m1.Sum(nil), []byte("x")))

	// HKDF and PBKDF2 (RFC 5869 test case 1, RFC 6070-style vectors).
	ikm, _ := hex.DecodeString("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b")
	salt, _ := hex.DecodeString("000102030405060708090a0b0c")
	info, _ := hex.DecodeString("f0f1f2f3f4f5f6f7f8f9")
	okm, err := hkdf.Key(sha256.New, ikm, salt, string(info), 42)
	fmt.Printf("hkdf %x %v\n", okm, err)
	prk, _ := hkdf.Extract(sha256.New, ikm, salt)
	fmt.Printf("hkdf prk %x\n", prk)
	for _, iter := range []int{1, 2, 4096} {
		dk, err := pbkdf2.Key(sha1.New, "password", []byte("salt"), iter, 20)
		fmt.Printf("pbkdf2-sha1 iter=%d %x %v\n", iter, dk, err)
	}
	dk, _ := pbkdf2.Key(sha256.New, "passwordPASSWORDpassword", []byte("saltSALTsaltSALTsaltSALTsaltSALTsalt"), 100, 40)
	fmt.Printf("pbkdf2-sha256 %x\n", dk)

	// Hash many messages and fold.
	fold := sha256.New()
	var cnt [256]int
	for i := 0; i < 2000; i++ {
		msg := fmt.Sprintf("message number %d with payload %x", i, i*i)
		s := sha256.Sum256([]byte(msg))
		fold.Write(s[:4])
		cnt[s[0]]++
	}
	maxc, minc := 0, 1<<30
	for _, c := range cnt {
		maxc = max(maxc, c)
		minc = min(minc, c)
	}
	fmt.Printf("fold of 2000 sha256 %x first-byte spread %d..%d\n", fold.Sum(nil), minc, maxc)
}
