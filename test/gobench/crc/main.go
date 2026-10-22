// hash/crc32 and hash/fnv.  On wasm crc32 has no hardware path, so the IEEE
// and Castagnoli tables are slicing-by-8: per eight bytes, one unaligned
// `i32.load` and four `i32.load8_u` from the data and eight `i32.load`s from
// the tables, shifts and xors.  Koopman takes the byte-at-a-time
// simpleUpdate, one table load per byte.  FNV-1a 64 and FNV-1 32 are
// inlined into main as a loop with one byte load and one `i64.mul` or
// `i32.mul` per byte.  The Update calls feed the data in uneven pieces.
//
// Checksums and hashes are everywhere in real programs, and these are the
// two classic software shapes: a table-lookup kernel bound by loads, and a
// serial multiply chain bound by multiply latency.
package main

import (
	"hash/crc32"
	"hash/fnv"
)

func main() {
	const n = 16384
	data := make([]byte, n)
	x := uint32(0x9e3779b9)
	for i := range data {
		x ^= x << 13
		x ^= x >> 17
		x ^= x << 5
		data[i] = byte(x >> 24)
	}
	castagnoli := crc32.MakeTable(crc32.Castagnoli)
	koopman := crc32.MakeTable(crc32.Koopman)
	h64 := fnv.New64a()
	h32 := fnv.New32()
	var total uint64
	for r := 0; r < 1050; r++ {
		data[(r*997)&(n-1)] ^= byte(r)
		total += uint64(crc32.ChecksumIEEE(data))
		total += uint64(crc32.Checksum(data, castagnoli))
		total += uint64(crc32.Checksum(data[r&63:], koopman))
		// Incremental update in uneven pieces.
		c := uint32(0)
		for off := 0; off < n; off += 1000 + r&255 {
			end := off + 777
			if end > n {
				end = n
			}
			c = crc32.Update(c, crc32.IEEETable, data[off:end])
		}
		total += uint64(c)
		h64.Reset()
		h64.Write(data)
		total += h64.Sum64() >> 8
		h32.Reset()
		h32.Write(data[:n/2])
		total += uint64(h32.Sum32())
	}
	println("total =", total)
}
