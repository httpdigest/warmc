// A pointer tree built and dropped repeatedly: allocation, and a garbage
// collector that actually runs.
//
// This is the kernel where the two Go corpora should differ most, and for a
// reason already measured: warmc's own collector never runs for TinyGo output,
// because TinyGo's kernels here do not allocate enough to trigger it.  Go's
// runtime collects on its own schedule with its own write barriers, mark
// workers and sweep, none of which is warmc's -- it is all compiled into the
// module.  So this row measures warmc on somebody else's concurrent collector.
package main

import "fmt"

type node struct {
	l, r *node
	v    uint64
}

//go:noinline
func build(depth int, v uint64) *node {
	if depth == 0 {
		return &node{v: v}
	}
	return &node{l: build(depth-1, v*2), r: build(depth-1, v*2+1), v: v}
}

//go:noinline
func total(n *node) uint64 {
	if n == nil {
		return 0
	}
	return n.v + total(n.l) + total(n.r)
}

func main() {
	var s uint64
	for r := 0; r < 700; r++ {
		// Depth 11 is 4095 nodes allocated and made unreachable each round.
		t := build(11, uint64(r))
		s += total(t)
	}
	fmt.Println("s =", s)
}
