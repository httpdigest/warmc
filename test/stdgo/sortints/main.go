// sort.Slice over a shuffled slice, the same source as test/gobench/sortints:
// an indirect call to the less closure per comparison and a reflection-built
// swapper per exchange.
package main

import (
	"fmt"
	"sort"
)

func main() {
	const n = 4096
	a := make([]int, n)
	total := 0
	for r := 0; r < 200; r++ {
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
	fmt.Println("total =", total)
}
