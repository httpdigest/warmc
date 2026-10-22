// Slice semantics.  append growth (len and cap after every append, and the
// point where a new backing array is made and two slices stop sharing),
// copy with overlap in both directions and with mismatched lengths, slicing
// up to capacity, three-index slices that cap the capacity so the next
// append must reallocate, nil against empty slices, slices of slices,
// in-place filtering, insertion and deletion by append, the clear builtin,
// slice to array and array-pointer conversions, and the slices helpers
// Grow/Clip/Insert/Delete/Concat.  Aliasing is the point: each case writes
// through one slice and prints the other.
package main

import (
	"fmt"
	"os"
	"slices"
)

var seed = []int{3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9}

func init() {
	if len(os.Args) > 1000 {
		seed[0] = 0
	}
}

//go:noinline
func appendOne(s []int, v int) []int { return append(s, v) }

//go:noinline
func filterInPlace(s []int, keep func(int) bool) []int {
	out := s[:0]
	for _, v := range s {
		if keep(v) {
			out = append(out, v)
		}
	}
	return out
}

//go:noinline
func insertAt(s []int, i int, vs ...int) []int {
	return append(s[:i], append(vs, s[i:]...)...)
}

//go:noinline
func removeAt(s []int, i int) []int { return append(s[:i], s[i+1:]...) }

func main() {
	fmt.Println("-- growth")
	var s []int
	lastCap := -1
	for i := 0; i < 600; i++ {
		s = appendOne(s, i*seed[i%len(seed)])
		if cap(s) != lastCap {
			fmt.Println("len", len(s), "cap", cap(s))
			lastCap = cap(s)
		}
	}
	sum := 0
	for _, v := range s {
		sum += v
	}
	fmt.Println("sum", sum)
	bs := []byte{}
	lastCap = -1
	for i := 0; i < 3000; i++ {
		bs = append(bs, byte(i))
		if cap(bs) != lastCap {
			fmt.Println("bytes len", len(bs), "cap", cap(bs))
			lastCap = cap(bs)
		}
	}
	bs = append(bs, "a string appended"...)
	fmt.Println(len(bs), string(bs[len(bs)-6:]))
	fmt.Println("-- sharing until reallocation")
	a := make([]int, 3, 5)
	b := append(a, 10)
	c := append(a, 20) // overwrites b[3]: same backing array
	fmt.Println(a, b, c, len(a), cap(a), len(b), cap(b))
	d := append(b, 30, 40) // exceeds cap 5: new array
	d[0] = 99
	b[1] = 77
	fmt.Println(a, b, c, d)
	fmt.Println("-- three-index slices")
	base := []int{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}
	full := base[2:5]
	capped := base[2:5:5]
	fmt.Println(full, len(full), cap(full), capped, len(capped), cap(capped))
	full = append(full, 100)     // writes base[5]
	capped = append(capped, 200) // reallocates
	capped[0] = -2
	fmt.Println(base, full, capped)
	ext := full[:cap(full)]
	fmt.Println(ext, len(ext))
	tail := base[8:]
	tail = append(tail, 10, 11)
	tail[0] = -8
	fmt.Println(base, tail)
	fmt.Println("-- copy")
	x := []int{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}
	n := copy(x[2:], x) // overlap, forward
	fmt.Println(n, x)
	x = []int{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}
	n = copy(x, x[3:]) // overlap, backward
	fmt.Println(n, x)
	short := make([]int, 4)
	n = copy(short, seed)
	fmt.Println(n, short)
	n = copy(seed[:0], short)
	fmt.Println(n)
	str := make([]byte, 5)
	n = copy(str, "héllo wörld")
	fmt.Println(n, str, string(str))
	fmt.Println("-- nil and empty")
	var nilS []int
	empty := []int{}
	made := make([]int, 0)
	fmt.Println(nilS == nil, empty == nil, made == nil, len(nilS), cap(nilS), len(empty))
	fmt.Printf("%v %v %#v %#v\n", nilS, empty, nilS, empty)
	nilS = append(nilS, 1)
	fmt.Println(nilS, nilS[:0] == nil, empty[:0] == nil)
	var nilB []byte
	fmt.Println(string(nilB) == "", len(string(nilB)), slices.Equal(nilS[:0], empty))
	for range nilB {
		fmt.Println("never")
	}
	fmt.Println("-- 2D")
	grid := make([][]int, 6)
	for i := range grid {
		grid[i] = make([]int, i+1)
		for j := range grid[i] {
			if j == 0 || j == i {
				grid[i][j] = 1
			} else {
				grid[i][j] = grid[i-1][j-1] + grid[i-1][j]
			}
		}
		fmt.Println(grid[i])
	}
	rows := grid[2:4]
	rows[0][1] = 42
	fmt.Println(grid[2], len(rows), cap(rows))
	flat := make([]int, 0, 21)
	for _, r := range grid {
		flat = append(flat, r...)
	}
	fmt.Println(flat, len(flat), cap(flat))
	fmt.Println("-- filter, insert, remove")
	f := slices.Clone(seed)
	f = filterInPlace(f, func(v int) bool { return v%2 == 1 })
	fmt.Println(f, len(f))
	g := slices.Clone(seed[:6])
	g = insertAt(g, 2, 70, 71, 72)
	fmt.Println(g)
	g = removeAt(g, 0)
	g = removeAt(g, len(g)-1)
	fmt.Println(g, len(g))
	h := slices.Insert([]int{1, 2, 3}, 1, 9, 8)
	h = slices.Delete(h, 3, 4)
	h = slices.Concat(h, []int{5}, nil, []int{6, 7})
	fmt.Println(h, len(h))
	h = slices.Grow(h, 50)
	fmt.Println(len(h), cap(h) >= 57)
	h = slices.Clip(h)
	fmt.Println(len(h), cap(h))
	for i, j := 0, len(h)-1; i < j; i, j = i+1, j-1 {
		h[i], h[j] = h[j], h[i]
	}
	fmt.Println(h)
	fmt.Println("-- clear and conversions")
	cl := []int{1, 2, 3, 4}
	alias := cl[1:3]
	clear(alias)
	fmt.Println(cl)
	arr := [4]int(cl)
	arr[0] = 100
	ap := (*[2]int)(cl[2:])
	ap[1] = 200
	fmt.Println(cl, arr)
	fmt.Println("-- slices of structs and pointers into them")
	type pt struct{ x, y int }
	pts := make([]pt, 0, 2)
	pts = append(pts, pt{1, 2}, pt{3, 4})
	p0 := &pts[0]
	p0.x = 10
	fmt.Println(pts)
	pts = append(pts, pt{5, 6}) // reallocates; p0 points at the old array
	p0.y = 20
	fmt.Println(pts, *p0)
	for i := range pts {
		pts[i].x *= 2
	}
	for _, p := range pts {
		p.y = 0 // copy
	}
	fmt.Println(pts)
	fmt.Println("-- strings as slices")
	word := "slicing"
	for i := 0; i <= len(word); i++ {
		fmt.Print(word[:i], "|", word[i:], " ")
	}
	fmt.Println()
	runesTotal := 0
	for i := 0; i < 50; i++ {
		window := seed[i%len(seed) : min(len(seed), i%len(seed)+4)]
		for _, v := range window {
			runesTotal += v * (i + 1)
		}
	}
	fmt.Println(runesTotal)
}
