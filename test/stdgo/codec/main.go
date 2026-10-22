// encoding/base64 in all four alphabets, and encoding/hex: a 3,000-byte buffer
// encoded and decoded back each round, plus the allocating string forms on a
// short slice.
//
// Both are table-driven byte loops.  base64's Encode turns three input bytes
// into four by indexing a 64-byte alphabet.  Its Decode takes eight bytes at a
// time through a 256-entry decode map, with assemble64 inlined into it, and
// falls back to decodeQuantum for the tail and the padding.  hex.Encode
// inlines into main as two table loads per byte.  Every byte read is an
// i64.load8_u, widened to the 64 bits the Go back end keeps its values in, and
// the table indexes carry bounds checks.  The length varies by round, so every
// padding case comes up.
package main

import (
	"encoding/base64"
	"encoding/hex"
	"fmt"
)

func main() {
	const n = 3000
	src := make([]byte, n)
	x := uint32(2166136261)
	for i := range src {
		x ^= x << 13
		x ^= x >> 17
		x ^= x << 5
		src[i] = byte(x >> 24)
	}
	encs := []*base64.Encoding{base64.StdEncoding, base64.URLEncoding,
		base64.RawStdEncoding, base64.RawURLEncoding}
	b64 := make([]byte, base64.StdEncoding.EncodedLen(n))
	hx := make([]byte, hex.EncodedLen(n))
	back := make([]byte, n)
	var acc uint64
	for r := 0; r < 10000; r++ {
		src[r%n] ^= byte(r)
		// Vary the length, so every padding case comes up.
		m := n - r%3
		enc := encs[r&3]
		el := enc.EncodedLen(m)
		enc.Encode(b64[:el], src[:m])
		k, err := enc.Decode(back, b64[:el])
		if err != nil || k != m {
			panic("base64")
		}
		acc = acc*31 + uint64(b64[el-1]) + uint64(b64[el/2]) + uint64(k)

		hl := hex.Encode(hx, src[:m])
		k, err = hex.Decode(back, hx[:hl])
		if err != nil || k != m {
			panic("hex")
		}
		acc = acc*31 + uint64(hx[hl-1]) + uint64(back[r%m])

		// The string forms, which allocate.
		s := enc.EncodeToString(src[r%64 : r%64+48])
		d, _ := enc.DecodeString(s)
		acc += uint64(len(s)) + uint64(d[len(d)-1])
	}
	fmt.Println("acc =", acc)
}
