// hash/crc32 with the IEEE and Castagnoli polynomials, hash/fnv's FNV-1a 64 and
// FNV-1 32, and crypto/md5, all through the hash.Hash interface over a 4 KiB
// buffer written in two uneven pieces.
//
// None of the three has wasm assembly, so this is their portable Go: three
// different shapes of hashing.  crc32 has no carry-less multiply or CRC
// instruction to reach for, so both polynomials use slicing-by-8 -- eight
// 256-entry tables, eight table loads per eight input bytes -- through an
// update function chosen once at first use and called as a function value.
// FNV is a multiply and an xor per byte, one serial dependency chain.  md5's
// block function is 64 steps of 32-bit adds, boolean functions and an i32.rotl
// each; sha256 next door is the same idea with a message schedule.
package main

import (
	"crypto/md5"
	"fmt"
	"hash"
	"hash/crc32"
	"hash/fnv"
)

func main() {
	buf := make([]byte, 4096)
	for i := range buf {
		buf[i] = byte(i*131 + i>>7)
	}
	castagnoli := crc32.MakeTable(crc32.Castagnoli)
	hs := []hash.Hash{crc32.NewIEEE(), crc32.New(castagnoli), fnv.New64a(),
		fnv.New32(), md5.New()}
	var acc uint64
	sum := make([]byte, 0, 16)
	for r := 0; r < 3900; r++ {
		buf[r&4095] = byte(r)
		for k, h := range hs {
			h.Reset()
			// In uneven chunks, so the tail and head handling runs as well
			// as the bulk loop.
			h.Write(buf[:1000+r%7])
			h.Write(buf[1000+r%7:])
			sum = h.Sum(sum[:0])
			for _, b := range sum {
				acc = acc*31 + uint64(b)
			}
			acc += uint64(k)
		}
	}
	fmt.Println("acc =", acc)
}
