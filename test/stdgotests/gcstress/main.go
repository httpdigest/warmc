// Allocation-heavy work that makes Go's garbage collector run many times:
// millions of small objects in linked lists and binary trees that are built,
// checked and dropped, maps of slices grown and shrunk, strings built and
// discarded, pointer-rich structs held across explicit runtime.GC() calls,
// and slices of interfaces holding pointers to escaping values.
//
// Go's collector on wasm scans the goroutine stacks it keeps in linear memory
// and the module's data segment, so a value the compiled code holds anywhere
// the collector cannot see -- or a write the compiled code reorders across a
// write barrier -- becomes a freed object that is still in use, and shows up
// here as a wrong checksum.  Every phase prints checksums of what survived.
// No finalisers: their timing is not deterministic.
package main

import (
	"fmt"
	"runtime"
	"sort"
	"strings"
)

type node struct {
	left, right *node
	val         int
}

func build(depth, v int) *node {
	if depth == 0 {
		return &node{val: v}
	}
	return &node{build(depth-1, 2*v), build(depth-1, 2*v+1), v}
}

func (n *node) check() int {
	if n.left == nil {
		return n.val
	}
	return n.val + n.left.check() - n.right.check()
}

func (n *node) count() int {
	if n == nil {
		return 0
	}
	return 1 + n.left.count() + n.right.count()
}

type cell struct {
	next *cell
	data [3]int
	name string
}

type record struct {
	id    int
	tags  []string
	attrs map[string]*int
	child *record
}

type item interface{ weight() int }
type small struct{ w int }
type large struct {
	w   int
	pad [32]int64
	ref *small
}

func (s *small) weight() int { return s.w }
func (l *large) weight() int { return l.w + l.ref.w + int(l.pad[31]) }

func main() {
	// Binary trees, the classic GC benchmark shape, scaled down.
	long := build(12, 1)
	for d := 4; d <= 12; d += 2 {
		iters := 1 << (14 - d)
		chk := 0
		for i := 0; i < iters; i++ {
			chk += build(d, i).check()
		}
		fmt.Printf("trees depth %2d iters %4d check %d\n", d, iters, chk)
	}
	fmt.Println("long-lived tree nodes", long.count(), "check", long.check())

	// Linked lists of cells, many dropped, every 10th kept.
	var kept []*cell
	for round := 0; round < 40; round++ {
		var head *cell
		for i := 0; i < 2000; i++ {
			head = &cell{next: head, data: [3]int{round, i, round * i}, name: fmt.Sprint(i % 17)}
		}
		if round%10 == 0 {
			kept = append(kept, head)
		}
		if round%13 == 0 {
			runtime.GC()
		}
	}
	for k, h := range kept {
		n, s := 0, 0
		names := 0
		for c := h; c != nil; c = c.next {
			n++
			s += c.data[0] + c.data[1] - c.data[2]%7
			names += len(c.name)
		}
		fmt.Printf("kept list %d: len %d sum %d names %d\n", k, n, s, names)
	}

	// Maps of slices: grow, shrink, rebuild.
	m := map[string][]int{}
	for i := 0; i < 20000; i++ {
		k := fmt.Sprintf("k%03d", i%300)
		m[k] = append(m[k], i)
		if i%997 == 0 {
			delete(m, fmt.Sprintf("k%03d", (i/997)%300))
		}
	}
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	total := 0
	for _, k := range keys {
		for _, v := range m[k] {
			total += v
		}
	}
	fmt.Println("map of slices: keys", len(keys), "total", total)
	for _, k := range keys[:5] {
		v := m[k]
		fmt.Println("  ", k, len(v), v[:3], v[len(v)-1])
	}
	runtime.GC()
	for _, k := range keys[len(keys)-5:] {
		v := m[k]
		fmt.Println("  ", k, len(v), v[:3], v[len(v)-1])
	}

	// Records with nested maps of pointers, a GC in the middle of building.
	recs := make([]*record, 0, 500)
	for i := 0; i < 500; i++ {
		r := &record{id: i, attrs: map[string]*int{}}
		for j := 0; j < i%7; j++ {
			r.tags = append(r.tags, strings.Repeat(string(rune('a'+j)), j+1))
			v := i * j
			r.attrs[fmt.Sprint("a", j)] = &v
		}
		if i > 0 {
			r.child = recs[i-1]
		}
		recs = append(recs, r)
		if i == 250 {
			runtime.GC()
		}
	}
	runtime.GC()
	sumAttrs, sumTags, chain := 0, 0, 0
	for _, r := range recs {
		for _, p := range r.attrs {
			sumAttrs += *p
		}
		for _, t := range r.tags {
			sumTags += len(t)
		}
	}
	for r := recs[len(recs)-1]; r != nil; r = r.child {
		chain++
	}
	fmt.Println("records attrs", sumAttrs, "tags", sumTags, "chain", chain)
	for _, i := range []int{0, 6, 13, 250, 499} {
		r := recs[i]
		ks := make([]string, 0)
		for k := range r.attrs {
			ks = append(ks, k)
		}
		sort.Strings(ks)
		var parts []string
		for _, k := range ks {
			parts = append(parts, fmt.Sprintf("%s=%d", k, *r.attrs[k]))
		}
		fmt.Println("  record", i, r.tags, parts)
	}

	// Interfaces holding pointers to heap values.
	items := make([]item, 0, 3000)
	for i := 0; i < 3000; i++ {
		if i%3 == 0 {
			l := &large{w: i, ref: &small{i % 11}}
			l.pad[31] = int64(i % 5)
			items = append(items, l)
		} else {
			items = append(items, &small{i})
		}
		if i%1000 == 999 {
			runtime.GC()
		}
	}
	wsum := 0
	for _, it := range items {
		wsum += it.weight()
	}
	fmt.Println("interface weights", wsum)

	// String churn: build many strings, keep a few.
	var keep []string
	h := uint32(2166136261)
	for i := 0; i < 20000; i++ {
		var b strings.Builder
		for j := 0; j < i%13; j++ {
			b.WriteByte(byte('A' + (i+j)%26))
		}
		s := b.String()
		for k := 0; k < len(s); k++ {
			h = (h ^ uint32(s[k])) * 16777619
		}
		if i%2000 == 0 {
			keep = append(keep, s+"|"+strings.ToLower(s))
		}
	}
	fmt.Printf("string churn hash %08x\n", h)
	for _, s := range keep {
		fmt.Println("  kept", s)
	}

	// Slices of slices reallocated many times.
	grid := make([][]int, 0)
	for r := 0; r < 200; r++ {
		row := []int{}
		for c := 0; c < r%50; c++ {
			row = append(row, r*c)
		}
		grid = append(grid, row)
	}
	gs := 0
	for _, row := range grid {
		for _, v := range row {
			gs += v
		}
	}
	fmt.Println("grid sum", gs, "rows", len(grid), "cap>=len", cap(grid) >= len(grid))

	// Closures capturing heap state, kept across a GC.
	var fns []func() int
	for i := 0; i < 1000; i++ {
		buf := make([]int, i%20+1)
		for j := range buf {
			buf[j] = i + j
		}
		fns = append(fns, func() int {
			s := 0
			for _, v := range buf {
				s += v
			}
			return s
		})
	}
	runtime.GC()
	fs := 0
	for _, f := range fns {
		fs += f()
	}
	fmt.Println("closure sum", fs)

	var ms runtime.MemStats
	runtime.ReadMemStats(&ms)
	fmt.Println("collector ran:", ms.NumGC > 0)
	fmt.Println("long tree still intact:", long.check(), long.count())
}
