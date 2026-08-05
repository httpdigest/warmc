// Go's built-in map: a hash, a bucket walk, and eight key slots per bucket.
//
// Deliberately never iterated.  Go randomises map iteration order on purpose,
// so a kernel that ranged over a map would print a different answer every run
// and the differential against wasmtime would fail for a reason that is not
// warmc's.  Every read here is by key.
//
// The counterpart of test/gobench/maphash, and of rsbench/mapint on the
// hashbrown side: three implementations of the same shape, which is what makes
// a claim about hashing rather than about one library possible.
package main

import "fmt"

//go:noinline
func build(n uint32) map[uint32]uint64 {
	m := make(map[uint32]uint64)
	for i := uint32(0); i < n; i++ {
		m[i*2654435761] = uint64(i)
	}
	return m
}

//go:noinline
func probe(m map[uint32]uint64, n uint32) uint64 {
	var s uint64
	for i := uint32(0); i < n; i++ {
		// Half hit and half miss, so the bucket walk's exit is not predictable.
		if v, ok := m[i*2654435761]; ok {
			s += v
		} else {
			s++
		}
	}
	return s
}

func main() {
	m := build(2000)
	var s uint64
	for r := 0; r < 2700; r++ {
		s += probe(m, 4000)
	}
	fmt.Println("s =", s)
}
