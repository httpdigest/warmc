// Register-only arithmetic behind a call, the same source as test/gobench/mix:
// nothing to vectorise and nothing to hoist.  Here the call itself is Go's: the
// argument and the result go through the stack in linear memory rather than
// wasm parameters, and the caller tests the flag every Go function on wasm
// returns, which says whether the stack is unwinding.
package main

import "fmt"

//go:noinline
func mix(x uint64) uint64 {
	x ^= x >> 33
	x *= 0xff51afd7ed558ccd
	x ^= x >> 33
	x *= 0xc4ceb9fe1a85ec53
	x ^= x >> 33
	return x
}

func main() {
	var h uint64 = 1
	for i := 0; i < 30000000; i++ {
		h = mix(h)
	}
	fmt.Println("h =", h)
}
