// crypto/sha256 over a 4 KiB buffer.
//
// Go has assembly for this block function on amd64 and arm64, and none for
// wasm, so what runs here is the portable Go one: 64 rounds of 32-bit rotates,
// adds and the two choice functions over eight working variables, with the
// message schedule in a 64-word array on the stack.  A real library kernel
// whose arithmetic is all 32-bit, from a compiler that otherwise keeps
// everything 64-bit.
package main

import (
	"crypto/sha256"
	"fmt"
)

func main() {
	buf := make([]byte, 4096)
	for i := range buf {
		buf[i] = byte(i * 31)
	}
	var acc uint64
	for r := 0; r < 12000; r++ {
		buf[r&4095] = byte(r)
		sum := sha256.Sum256(buf)
		for i := 0; i < 8; i++ {
			acc = acc*31 + uint64(sum[i])
		}
	}
	fmt.Println("acc =", acc)
}
