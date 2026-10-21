// copy(), the same source as test/gobench/memcpy.  Go calls runtime.memmove,
// which is a memory.copy, so both engines run their bulk copy at the machine's
// L2 bandwidth ceiling.
//
// A million iterations for the same reason as in test/gobench: the two differ
// by well under a percent here, and a shorter run is all fixed cost and
// noise.
package main

import "fmt"

//go:noinline
func blit(dst, src []byte) { copy(dst, src) }

func main() {
	a := make([]byte, 65536)
	b := make([]byte, 65536)
	for i := range a {
		a[i] = byte(i)
	}
	for r := 0; r < 1000000; r++ {
		a[r&65535] = byte(r)
		blit(b, a)
	}
	var s int
	for i := range b {
		s += int(b[i])
	}
	fmt.Println("s =", s)
}
