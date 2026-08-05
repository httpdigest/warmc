// Allocation, enough of it that TinyGo's own collector runs.
//
// The guest has a garbage collector of its own inside its linear memory, and
// nothing else here makes it work: a tree built and dropped a hundred times
// over sweeps and reuses, and a map churned in place rehashes.  What is
// checked is a checksum over the surviving structure, so a collector that
// freed something still reachable changes the answer rather than crashing
// somewhere later.
package main

import "fmt"

type node struct {
	left, right *node
	val         int
	pad         [3]int64
}

func build(depth, seed int) *node {
	if depth == 0 {
		return &node{val: seed}
	}
	return &node{
		left:  build(depth-1, seed*2+1),
		right: build(depth-1, seed*2+2),
		val:   seed,
	}
}

func sum(n *node) int {
	if n == nil {
		return 0
	}
	return n.val + sum(n.left) + sum(n.right)
}

func main() {
	total := 0
	for i := 0; i < 60; i++ {
		t := build(9, i)
		total = total*31 + sum(t)
	}
	fmt.Println("trees", total)

	// A long-lived tree kept across the churn above and below: if the
	// collector moves or frees it, the second sum disagrees with the first.
	keep := build(11, 7)
	before := sum(keep)

	m := map[int][]byte{}
	for i := 0; i < 4000; i++ {
		m[i%800] = make([]byte, i%64+1)
		if i%3 == 0 {
			delete(m, (i*7)%800)
		}
	}
	bytes := 0
	for _, v := range m {
		bytes += len(v)
	}
	fmt.Println("map", len(m), bytes)

	var slices [][]int
	for i := 0; i < 300; i++ {
		s := make([]int, 0, 1)
		for j := 0; j < i%50; j++ {
			s = append(s, i*j)
		}
		if i%5 == 0 {
			slices = append(slices, s)
		}
	}
	acc := 0
	for _, s := range slices {
		for _, v := range s {
			acc = acc*7 + v
		}
	}
	fmt.Println("slices", len(slices), acc)
	fmt.Println("kept", before, sum(keep) == before)
}
