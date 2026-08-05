// Register-only arithmetic behind a call: nothing to vectorise and nothing to
// hoist, so it measures the call and the loop overhead.
package main

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
	for i := 0; i < 60000000; i++ {
		h = mix(h)
	}
	println("h =", h)
}
