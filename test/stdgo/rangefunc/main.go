// Range-over-func iterators: an iter.Seq over a binary tree's in-order walk,
// through generic filter and map adapters and an iter.Seq2 that numbers the
// values, broken out of partway one round in four.
//
// Go compiles a `for range` over a function by making the loop body a closure
// and passing it in as yield.  Here the compiler inlines the iterator
// functions themselves into main, down to the recursive walk, but every loop
// body stays a closure of its own, so each element is a chain of
// call_indirect: walk calls filter's body, which calls the keep predicate and
// then map's body, which calls the mapping function and then enumerate's body,
// which calls main's.  Each body also checks a state word shared with its loop,
// which is how Go panics on an iterator that calls yield after being told to
// stop.  The newest Go control-flow feature, and nothing but indirect calls.
package main

import (
	"fmt"
	"iter"
)

type node struct {
	key         int
	left, right *node
}

func insert(t *node, k int) *node {
	if t == nil {
		return &node{key: k}
	}
	if k < t.key {
		t.left = insert(t.left, k)
	} else {
		t.right = insert(t.right, k)
	}
	return t
}

// In-order walk of a binary tree as a push iterator, recursive, stopping
// early when the consumer breaks.
func (t *node) all() iter.Seq[int] {
	return func(yield func(int) bool) {
		t.walk(yield)
	}
}

func (t *node) walk(yield func(int) bool) bool {
	if t == nil {
		return true
	}
	return t.left.walk(yield) && yield(t.key) && t.right.walk(yield)
}

func filter[T any](s iter.Seq[T], keep func(T) bool) iter.Seq[T] {
	return func(yield func(T) bool) {
		for v := range s {
			if keep(v) && !yield(v) {
				return
			}
		}
	}
}

func mapped[T, U any](s iter.Seq[T], f func(T) U) iter.Seq[U] {
	return func(yield func(U) bool) {
		for v := range s {
			if !yield(f(v)) {
				return
			}
		}
	}
}

func enumerate[T any](s iter.Seq[T]) iter.Seq2[int, T] {
	return func(yield func(int, T) bool) {
		i := 0
		for v := range s {
			if !yield(i, v) {
				return
			}
			i++
		}
	}
}

func main() {
	var root *node
	x := uint32(2463534242)
	for i := 0; i < 2000; i++ {
		x ^= x << 13
		x ^= x >> 17
		x ^= x << 5
		root = insert(root, int(x%100000))
	}
	var acc uint64
	for r := 0; r < 1500; r++ {
		mod := r%5 + 2
		odd := filter(root.all(), func(k int) bool { return k%mod != 0 })
		sq := mapped(odd, func(k int) uint64 { return uint64(k) * uint64(k+r) })
		for i, v := range enumerate(sq) {
			acc += v ^ uint64(i)
			// Break out partway on some rounds, through three layers.
			if r&3 == 0 && i == 1000+r {
				break
			}
		}
	}
	fmt.Println("acc =", acc)
}
