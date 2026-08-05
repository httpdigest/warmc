// Allocation and pointer chasing: a binary tree built and walked.  This is
// where the collector and the object layout show up rather than the loops.
package main

type node struct {
	left, right *node
	val         int
}

//go:noinline
func build(depth, v int) *node {
	if depth == 0 {
		return &node{val: v}
	}
	return &node{left: build(depth-1, v*2), right: build(depth-1, v*2+1), val: v}
}

//go:noinline
func walk(n *node) int {
	if n == nil {
		return 0
	}
	return n.val + walk(n.left) + walk(n.right)
}

func main() {
	total := 0
	for r := 0; r < 250; r++ {
		t := build(14, r)
		total += walk(t) & 0xffff
	}
	println("total =", total)
}
