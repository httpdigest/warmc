// sort.Slice over a shuffled slice: an indirect call per comparison and a
// swap per exchange, which is the shape most real Go sorting has.
package main

import "sort"

func main() {
	const n = 4096
	a := make([]int, n)
	total := 0
	for r := 0; r < 400; r++ {
		x := uint32(r)*2654435761 + 12345
		for i := 0; i < n; i++ {
			x ^= x << 13
			x ^= x >> 17
			x ^= x << 5
			a[i] = int(x & 0xffffff)
		}
		sort.Slice(a, func(i, j int) bool { return a[i] < a[j] })
		total += a[0] + a[n/2] + a[n-1]
	}
	println("total =", total)
}
