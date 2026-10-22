// Allocation and garbage collection.  TinyGo's collector is a conservative
// mark-sweep over linear memory that finds roots by scanning the shadow
// stack and globals, and grows the heap with memory.grow.  This allocates
// far more than it keeps: churned binary trees of which only some survive,
// a linked list rebuilt many times, strings concatenated and discarded,
// slices that grow and shrink, maps filled and cleared, closures and boxed
// interfaces created in loops.  Every surviving structure is verified by a
// checksum after the garbage around it has been collected, so a collector
// that frees a live object, or an engine that corrupts memory around a
// memory.grow or a bulk copy, shows as a wrong number.
package main

import (
	"fmt"
	"os"
	"runtime"
	"strings"
)

type tree struct {
	left, right *tree
	val         int
	pad         [6]int
}

func build(depth, seed int) *tree {
	if depth == 0 {
		return nil
	}
	t := &tree{val: seed}
	t.pad[seed%6] = seed
	t.left = build(depth-1, seed*2+1)
	t.right = build(depth-1, seed*2+2)
	return t
}

func (t *tree) sum() int {
	if t == nil {
		return 0
	}
	return t.val + t.pad[t.val%6] + t.left.sum() + t.right.sum()
}

func (t *tree) count() int {
	if t == nil {
		return 0
	}
	return 1 + t.left.count() + t.right.count()
}

type link struct {
	next *link
	data []byte
	name string
}

var scale = 1

func init() {
	if len(os.Args) > 1000 {
		scale = 2
	}
}

var keep []*tree // a global root

func main() {
	fmt.Println("-- churned trees")
	longLived := build(12, 1)
	want := longLived.sum()
	for round := 0; round < 30; round++ {
		tmp := build(10, round)
		s := tmp.sum()
		if round%3 == 0 {
			keep = append(keep, tmp)
		}
		if round%5 == 0 {
			fmt.Println(round, s, tmp.count(), longLived.sum() == want)
		}
	}
	total := 0
	for _, t := range keep {
		total += t.sum()
	}
	fmt.Println("kept", len(keep), total, longLived.sum() == want, longLived.count())
	keep = keep[:2]
	runtime.GC()
	fmt.Println("after GC", keep[0].sum(), keep[1].sum(), longLived.sum() == want)
	fmt.Println("-- linked lists")
	var head *link
	for round := 0; round < 20; round++ {
		head = nil
		for i := 0; i < 500*scale; i++ {
			d := make([]byte, 16+i%48)
			for j := range d {
				d[j] = byte(i + j + round)
			}
			head = &link{head, d, fmt.Sprint("n", i)}
		}
		if round%4 == 0 {
			cs, n := 0, 0
			for l := head; l != nil; l = l.next {
				for _, b := range l.data {
					cs = (cs*31 + int(b)) & 0xffffff
				}
				cs += len(l.name)
				n++
			}
			fmt.Println(round, n, cs)
		}
	}
	fmt.Println("-- strings")
	acc := ""
	parts := []string{}
	for i := 0; i < 3000; i++ {
		s := strings.Repeat(string(rune('a'+i%26)), i%17+1)
		acc += s
		if len(acc) > 2000 {
			parts = append(parts, acc[:10])
			acc = acc[1500:]
		}
	}
	fmt.Println(len(parts), len(acc), parts[0], parts[len(parts)-1], acc[:20])
	fmt.Println("-- slices growing and shrinking")
	var ss [][]int
	for i := 0; i < 400; i++ {
		row := make([]int, 0)
		for j := 0; j < i%50; j++ {
			row = append(row, i*j)
		}
		ss = append(ss, row)
		if len(ss) > 64 {
			ss = append([][]int(nil), ss[32:]...)
		}
	}
	cs := 0
	for _, row := range ss {
		for _, v := range row {
			cs = (cs + v) % 1000003
		}
	}
	fmt.Println(len(ss), cs)
	fmt.Println("-- maps")
	for round := 0; round < 6; round++ {
		m := map[string][]int{}
		for i := 0; i < 2000; i++ {
			k := fmt.Sprint("key", i%(300+round*50))
			m[k] = append(m[k], i)
		}
		sum := 0
		for k, v := range m {
			sum += len(k) * len(v)
			for _, x := range v {
				sum += x % 7
			}
		}
		fmt.Println(round, len(m), sum)
		clear(m)
	}
	fmt.Println("-- closures and interfaces")
	var fns []func() int
	var boxes []any
	for i := 0; i < 5000; i++ {
		x := i
		f := func() int { return x * 3 }
		if i%250 == 0 {
			fns = append(fns, f)
			boxes = append(boxes, x, fmt.Sprint(x), []int{x}, &x)
		}
		_ = f()
	}
	r := 0
	for _, f := range fns {
		r += f()
	}
	for _, b := range boxes {
		switch v := b.(type) {
		case int:
			r += v
		case string:
			r += len(v)
		case []int:
			r += v[0]
		case *int:
			r += *v
		}
	}
	runtime.GC()
	fmt.Println(len(fns), len(boxes), r)
	var ms runtime.MemStats
	runtime.ReadMemStats(&ms)
	fmt.Println("heap grew:", ms.HeapSys > 0, "gc ran:", ms.NumGC > 0, longLived.sum() == want)
}
