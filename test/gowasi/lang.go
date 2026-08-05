// The language rather than the syscalls: maps, sorting, interfaces, closures,
// defer and recover, string building and type switches.
//
// These reach the parts of a compiler that arithmetic does not -- indirect
// calls through interface tables, a closure's captured environment, the stack
// unwinding a recover needs, and the hashing a map does on its own.
package main

import (
	"fmt"
	"sort"
	"strconv"
	"strings"
)

type shape interface{ area() int }

type sq struct{ s int }
type rect struct{ w, h int }

func (x sq) area() int   { return x.s * x.s }
func (x rect) area() int { return x.w * x.h }

func counter() func() int {
	n := 0
	return func() int { n += 3; return n }
}

// Deferred calls in a chain, which run in reverse and each capture something
// -- no panic and no recover: TinyGo's recover does nothing on wasip1, so a
// case built on it dies on both engines and tests neither.
func deferred(i int) string {
	var b strings.Builder
	for k := 0; k < i; k++ {
		defer func(k int) { fmt.Fprintf(&b, "<%d", k) }(k)
	}
	defer fmt.Fprintf(&b, "|")
	func() {
		for k := 0; k < i; k++ {
			defer func(k int) { fmt.Fprintf(&b, "[%d", k) }(k)
		}
	}()
	return b.String()
}

func main() {
	m := map[string]int{}
	for i := 0; i < 200; i++ {
		m["k"+strconv.Itoa(i*7%200)] = i * i
	}
	keys := make([]string, 0, len(m))
	sum := 0
	for k, v := range m {
		keys = append(keys, k)
		sum += v
	}
	sort.Strings(keys)
	fmt.Println("map", len(m), sum, keys[0], keys[len(keys)-1])

	xs := []int{}
	for i := 0; i < 50; i++ {
		xs = append(xs, (i*37+11)%50)
	}
	sort.Ints(xs)
	fmt.Println("sorted", xs[0], xs[len(xs)-1], xs[25])

	shapes := []shape{sq{3}, rect{2, 5}, sq{7}, rect{9, 1}}
	total := 0
	for _, s := range shapes {
		total += s.area()
		switch v := s.(type) {
		case sq:
			total += v.s
		case rect:
			total += v.w - v.h
		}
	}
	fmt.Println("shapes", total)

	c := counter()
	fmt.Println("closure", c(), c(), c())

	var b strings.Builder
	for i := 0; i < 20; i++ {
		fmt.Fprintf(&b, "%d:%s;", i, strconv.FormatInt(int64(i)*1234567, 16))
	}
	s := b.String()
	fmt.Println("built", len(s), s[:24])

	for i := 0; i < 5; i++ {
		fmt.Println("defer", i, deferred(i))
	}
}
